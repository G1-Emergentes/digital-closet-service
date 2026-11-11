#include "GarmentController.h"
#include "../../shared/interfaces/HttpSupport.h"
#include "../application/GarmentServices.h"
#include "../infrastructure/GarmentJsonMapper.h"
#include "../infrastructure/SupabaseGarmentRepository.h"
#include "../infrastructure/SupabaseIdentityProvider.h"
#include "../infrastructure/SupabaseImageSigner.h"
using namespace drogon;
namespace mirage::garment
{
namespace
{
const SupabaseGarmentRepository repository;
const SupabaseImageSigner signer;
const SupabaseIdentityProvider identities;
const GarmentCommandService commands(repository);
const GarmentQueryService queries(repository);
const GarmentImageService images(repository, signer);
using Callback = http::Callback;
using Authorized = std::function<void(GarmentAccess)>;

void fail(const Callback &callback, int status, const std::string &code)
{
    http::error(callback, static_cast<HttpStatusCode>(status), code.c_str());
}
// Domain validation errors are the contract's 400.
void badRequest(const Callback &callback, const std::invalid_argument &error)
{
    http::error(callback, k400BadRequest, "INVALID_ARGUMENT", error.what());
}
// The owner is never read from the request: IAM tells us who the token belongs to, and that
// verified id is what every database call receives.
void authorize(const HttpRequestPtr &req, const Callback &callback, Authorized done)
{
    std::string token;
    if (!http::bearer(req, token, callback))
        return;
    identities.currentUser(token, [callback, token, done = std::move(done)](IdentityResult who) {
        if (who.status != 200)
        {
            fail(callback, who.status, who.error);
            return;
        }
        done(GarmentAccess{token, who.userId});
    });
}
GarmentCompletion respond(Callback callback, HttpStatusCode ok = k200OK)
{
    return [callback, ok](GarmentResult result) {
        if (!result.garment)
        {
            fail(callback, result.status, result.error);
            return;
        }
        http::reply(callback, ok, GarmentJsonMapper::toJson(*result.garment));
    };
}
PageCompletion respondPage(Callback callback)
{
    return [callback](GarmentPage page) {
        if (page.status != 200)
        {
            fail(callback, page.status, page.error);
            return;
        }
        Json::Value json;
        json["items"] = Json::Value(Json::arrayValue);
        for (const auto &garment : page.items)
            json["items"].append(GarmentJsonMapper::toJson(garment));
        json["next_cursor"] = page.nextCursor ? Json::Value(*page.nextCursor) : Json::Value();
        http::reply(callback, k200OK, json);
    };
}
ResolveCompletion respondResolve(Callback callback)
{
    return [callback](ResolveResult result) {
        if (result.status != 200)
        {
            fail(callback, result.status, result.error);
            return;
        }
        Json::Value json;
        json["results"] = Json::Value(Json::arrayValue);
        for (const auto &entry : result.results)
        {
            Json::Value item;
            item["id"] = entry.id;
            item["status"] = entry.status == ResolveStatus::Available     ? "AVAILABLE"
                             : entry.status == ResolveStatus::Unavailable ? "UNAVAILABLE"
                                                                          : "NOT_FOUND";
            item["garment"] =
                entry.garment ? GarmentJsonMapper::toJson(*entry.garment) : Json::Value();
            json["results"].append(std::move(item));
        }
        http::reply(callback, k200OK, json);
    };
}
StatusCompletion respondDeleted(Callback callback)
{
    return [callback](StatusResult result) {
        if (result.status >= 400)
            fail(callback, result.status, result.error);
        else
            http::noContent(callback);
    };
}
ImageUploadCompletion respondUpload(Callback callback)
{
    return [callback](ImageUpload result) {
        if (result.status != 200)
        {
            fail(callback, result.status, result.error);
            return;
        }
        Json::Value json;
        json["image_id"] = result.imageId;
        json["upload_url"] = result.url;
        json["method"] = "PUT";
        http::reply(callback, k200OK, json);
    };
}
ImageAccessCompletion respondImageAccess(Callback callback)
{
    return [callback](ImageAccess result) {
        if (result.status != 200)
        {
            fail(callback, result.status, result.error);
            return;
        }
        Json::Value json;
        json["image_id"] = result.imageId;
        json["image_version"] = result.version;
        json["url"] = result.url;
        json["expires_at"] = result.expiresAt;
        json["mime_type"] = result.mimeType;
        http::reply(callback, k200OK, json);
    };
}
// Strict integer: "50abc" or "1e2" are rejected rather than truncated.
int parseLimit(const std::string &text)
{
    size_t used = 0;
    int value = 0;
    try
    {
        value = std::stoi(text, &used);
    }
    catch (const std::exception &)
    {
        used = 0;
    }
    if (used != text.size())
        throw std::invalid_argument("limit debe ser un entero de 1 a 100.");
    return value;
}
} // namespace
void GarmentController::listGarments(const HttpRequestPtr &req, Callback &&callback)
{
    const auto limit = req->getParameter("limit");
    const auto cursor = req->getParameter("cursor");
    authorize(req, callback, [limit, cursor, callback](GarmentAccess access) {
        try
        {
            ListGarmentsQuery query{access};
            if (!limit.empty())
                query.limit = parseLimit(limit);
            if (!cursor.empty())
                query.cursor = cursor;
            queries.handle(query, respondPage(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::createGarment(const HttpRequestPtr &req, Callback &&callback)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    authorize(req, callback, [body, callback](GarmentAccess access) {
        try
        {
            commands.handle(CreateGarmentCommand{access, drogon::utils::getUuid(),
                                                 GarmentJsonMapper::details(body, false)},
                            respond(callback, k201Created));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::resolveGarments(const HttpRequestPtr &req, Callback &&callback)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    authorize(req, callback, [body, callback](GarmentAccess access) {
        try
        {
            const auto &ids = body["ids"];
            if (!ids.isArray())
                throw std::invalid_argument("ids debe contener entre 1 y 100 UUID únicos.");
            ResolveGarmentsQuery query{access};
            for (const auto &id : ids)
            {
                if (!id.isString())
                    throw std::invalid_argument("ids debe contener entre 1 y 100 UUID únicos.");
                query.ids.push_back(id.asString());
            }
            queries.handle(query, respondResolve(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::getGarment(const HttpRequestPtr &req, Callback &&callback, std::string id)
{
    authorize(req, callback, [id, callback](GarmentAccess access) {
        try
        {
            queries.handle(GetGarmentQuery{access, id}, respond(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::updateGarment(const HttpRequestPtr &req, Callback &&callback,
                                      std::string id)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    authorize(req, callback, [body, id, callback](GarmentAccess access) {
        try
        {
            commands.handle(
                UpdateGarmentCommand{access, id, GarmentJsonMapper::details(body, true)},
                respond(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::deleteGarment(const HttpRequestPtr &req, Callback &&callback,
                                      std::string id)
{
    authorize(req, callback, [id, callback](GarmentAccess access) {
        try
        {
            commands.handle(DeleteGarmentCommand{access, id}, respondDeleted(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::imageUpload(const HttpRequestPtr &req, Callback &&callback,
                                    std::string id)
{
    authorize(req, callback, [id, callback](GarmentAccess access) {
        try
        {
            images.handle(GarmentImageUploadQuery{access, id, drogon::utils::getUuid()},
                          respondUpload(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::setImage(const HttpRequestPtr &req, Callback &&callback, std::string id)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    authorize(req, callback, [body, id, callback](GarmentAccess access) {
        try
        {
            if (!body["image_id"].isString())
                throw std::invalid_argument("image_id debe ser un UUID.");
            commands.handle(SetGarmentImageCommand{access, id, body["image_id"].asString()},
                            respond(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
void GarmentController::imageAccess(const HttpRequestPtr &req, Callback &&callback,
                                    std::string id)
{
    authorize(req, callback, [id, callback](GarmentAccess access) {
        try
        {
            images.handle(GarmentImageAccessQuery{access, id}, respondImageAccess(callback));
        }
        catch (const std::invalid_argument &e)
        {
            badRequest(callback, e);
        }
    });
}
} // namespace mirage::garment
