#include "GarmentController.h"
#include "../../shared/interfaces/HttpSupport.h"
#include "../application/GarmentServices.h"
#include "../infrastructure/GarmentJsonMapper.h"
#include "../infrastructure/SupabaseGarmentRepository.h"
#include "../infrastructure/SupabaseImageSigner.h"
using namespace drogon;
namespace mirage::garment
{
namespace
{
const SupabaseGarmentRepository repository;
const SupabaseImageSigner signer;
const GarmentCommandService commands(repository);
const GarmentQueryService queries(repository);
const GarmentImageService images(repository, signer);
using Callback = http::Callback;

HttpStatusCode statusOf(int status)
{
    return static_cast<HttpStatusCode>(status);
}
void fail(const Callback &callback, int status, const std::string &code)
{
    http::error(callback, statusOf(status), code.c_str());
}
// The owner is never read from the request: the JWT travels to PostgreSQL, which
// derives it from the session.
bool authorize(const HttpRequestPtr &req, const Callback &callback, GarmentAccess &access)
{
    return http::bearer(req, access.bearer, callback);
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
// Runs an action, turning domain validation errors into the contract's 400.
template <class Action> void guarded(const Callback &callback, Action action)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument &e)
    {
        http::error(callback, k400BadRequest, "INVALID_ARGUMENT", e.what());
    }
}
} // namespace
void GarmentController::listGarments(const HttpRequestPtr &req, Callback &&callback)
{
    GarmentAccess access;
    if (!authorize(req, callback, access))
        return;
    guarded(callback, [&] {
        ListGarmentsQuery query{access};
        const auto limit = req->getParameter("limit");
        if (!limit.empty())
        {
            // Strict integer: "50abc" or "1e2" are rejected rather than truncated.
            size_t used = 0;
            try
            {
                query.limit = std::stoi(limit, &used);
            }
            catch (const std::exception &)
            {
                used = 0;
            }
            if (used != limit.size())
                throw std::invalid_argument("limit debe ser un entero de 1 a 100.");
        }
        const auto cursor = req->getParameter("cursor");
        if (!cursor.empty())
            query.cursor = cursor;
        queries.handle(query, [callback](GarmentPage page) {
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
        });
    });
}
void GarmentController::createGarment(const HttpRequestPtr &req, Callback &&callback)
{
    GarmentAccess access;
    Json::Value body;
    if (!authorize(req, callback, access) || !http::json(req, body, callback))
        return;
    guarded(callback, [&] {
        commands.handle(CreateGarmentCommand{access, drogon::utils::getUuid(),
                                             GarmentJsonMapper::details(body, false)},
                        respond(callback, k201Created));
    });
}
void GarmentController::resolveGarments(const HttpRequestPtr &req, Callback &&callback)
{
    GarmentAccess access;
    Json::Value body;
    if (!authorize(req, callback, access) || !http::json(req, body, callback))
        return;
    guarded(callback, [&] {
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
        queries.handle(query, [callback](ResolveResult result) {
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
        });
    });
}
void GarmentController::getGarment(const HttpRequestPtr &req, Callback &&callback, std::string id)
{
    GarmentAccess access;
    if (!authorize(req, callback, access))
        return;
    guarded(callback, [&] { queries.handle(GetGarmentQuery{access, id}, respond(callback)); });
}
void GarmentController::updateGarment(const HttpRequestPtr &req, Callback &&callback,
                                      std::string id)
{
    GarmentAccess access;
    Json::Value body;
    if (!authorize(req, callback, access) || !http::json(req, body, callback))
        return;
    guarded(callback, [&] {
        commands.handle(UpdateGarmentCommand{access, id, GarmentJsonMapper::details(body, true)},
                        respond(callback));
    });
}
void GarmentController::deleteGarment(const HttpRequestPtr &req, Callback &&callback,
                                      std::string id)
{
    GarmentAccess access;
    if (!authorize(req, callback, access))
        return;
    guarded(callback, [&] {
        commands.handle(DeleteGarmentCommand{access, id}, [callback](StatusResult result) {
            if (result.status >= 400)
                fail(callback, result.status, result.error);
            else
                http::noContent(callback);
        });
    });
}
void GarmentController::setImage(const HttpRequestPtr &req, Callback &&callback, std::string id)
{
    GarmentAccess access;
    Json::Value body;
    if (!authorize(req, callback, access) || !http::json(req, body, callback))
        return;
    guarded(callback, [&] {
        if (!body["image_id"].isString())
            throw std::invalid_argument("image_id debe ser un UUID.");
        commands.handle(SetGarmentImageCommand{access, id, body["image_id"].asString()},
                        respond(callback));
    });
}
void GarmentController::imageAccess(const HttpRequestPtr &req, Callback &&callback,
                                    std::string id)
{
    GarmentAccess access;
    if (!authorize(req, callback, access))
        return;
    guarded(callback, [&] {
        images.handle(GarmentImageAccessQuery{access, id}, [callback](ImageAccess result) {
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
        });
    });
}
} // namespace mirage::garment
