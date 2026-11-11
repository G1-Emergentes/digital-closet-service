#include "OutfitController.h"
#include "../../garment/infrastructure/SupabaseIdentityProvider.h"
#include "../../garment/infrastructure/SupabaseImageSigner.h"
#include "../../shared/infrastructure/SupabaseClient.h"
#include "../../shared/interfaces/HttpSupport.h"
using namespace drogon;
// shortcut: no domain layer; the rules (1-3 distinct garments, ownership, availability) live in
// the SQL functions, which are the only writer. Add one if outfits gain behaviour.
namespace mirage::outfit
{
namespace
{
using Callback = http::Callback;
const garment::SupabaseIdentityProvider identities;
const garment::SupabaseImageSigner signer;
constexpr int kLinkSeconds = 300; // same five minutes as garment photos

void fail(const Callback &callback, int status, const char *code, const char *message = nullptr)
{
    http::error(callback, static_cast<HttpStatusCode>(status), code, message);
}
// Maps a PostgREST/PostgreSQL error; `notFound` is the code for P0002 (no_data_found).
void failDatabase(const Callback &callback, const ApiResult &result, const char *notFound)
{
    const auto code = result.body.get("code", "").asString();
    if (code == "P0002")
        fail(callback, 404, notFound);
    else if (code == "MG003")
        fail(callback, 404, "GARMENT_NOT_FOUND");
    else if (code == "23514" || code == "22P02" || code == "22007" || code == "22001" ||
             code == "MG001")
        fail(callback, 400, "INVALID_ARGUMENT");
    // The caller was verified by IAM, so a rejected service credential is our misconfiguration.
    else if (code == "PGRST202" || code == "42P01" || code == "42501" || result.status >= 500 ||
             result.status == 401 || result.status == 403)
        fail(callback, 503, "SERVICE_UNAVAILABLE");
    else
        fail(callback, 500, "INTERNAL_ERROR");
}
// Runs `rpcName` for the owner IAM verified; the owner is never read from the request.
void call(const HttpRequestPtr &req, const Callback &callback, const char *rpcName,
          Json::Value parameters, const char *notFound,
          std::function<void(const Json::Value &, const std::string &)> ok)
{
    std::string token;
    if (!http::bearer(req, token, callback))
        return;
    identities.currentUser(token, [=, ok = std::move(ok)](garment::IdentityResult who) mutable {
        if (who.status != 200)
        {
            fail(callback, who.status, who.error.c_str());
            return;
        }
        parameters["p_user_id"] = who.userId;
        SupabaseClient::request(SupabaseTarget::Wardrobe, drogon::Post,
                                std::string("/rest/v1/rpc/") + rpcName, parameters, "",
                                [=](ApiResult result) {
                                    if (result.status >= 400)
                                        failDatabase(callback, result, notFound);
                                    else
                                        ok(result.body, who.userId);
                                });
    });
}
// A null or non-string optional field stays null; a wrong type is a 400.
Json::Value optionalText(const Json::Value &body, const char *key)
{
    const auto &value = body[key];
    if (value.isNull())
        return Json::Value();
    if (!value.isString())
        throw std::invalid_argument(std::string(key) + " debe ser texto.");
    return value;
}
// Whitelisted request fields only (the owner is never read from the body).
Json::Value outfitPayload(const Json::Value &body)
{
    Json::Value outfit;
    const auto &ids = body["garment_ids"];
    if (!ids.isArray())
        throw std::invalid_argument("garment_ids debe contener entre 1 y 3 UUID únicos.");
    outfit["garment_ids"] = Json::Value(Json::arrayValue);
    for (const auto &id : ids)
    {
        if (!id.isString())
            throw std::invalid_argument("garment_ids debe contener entre 1 y 3 UUID únicos.");
        outfit["garment_ids"].append(id);
    }
    for (const char *key : {"name", "occasion", "style", "recommendation_id"})
        outfit[key] = optionalText(body, key);
    return outfit;
}
std::string utcAfter(int seconds)
{
    const std::time_t at = std::time(nullptr) + seconds;
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &at);
#else
    gmtime_r(&at, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}
} // namespace
void OutfitController::createOutfit(const HttpRequestPtr &req, Callback &&callback)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    Json::Value outfit;
    try
    {
        outfit = outfitPayload(body);
        outfit["id"] = drogon::utils::getUuid(); // the id is ours
    }
    catch (const std::invalid_argument &e)
    {
        http::error(callback, k400BadRequest, "INVALID_ARGUMENT", e.what());
        return;
    }
    Json::Value p;
    p["p_outfit"] = outfit;
    // A missing or foreign garment is GARMENT_NOT_FOUND, as in the contract.
    call(req, callback, "mirage_create_outfit", p, "GARMENT_NOT_FOUND",
         [callback](const Json::Value &saved, const std::string &) { http::reply(callback, k201Created, saved); });
}
void OutfitController::listOutfits(const HttpRequestPtr &req, Callback &&callback)
{
    Json::Value p;
    const auto limit = req->getParameter("limit");
    const auto cursor = req->getParameter("cursor");
    try
    {
        size_t used = 0;
        p["p_limit"] = limit.empty() ? 50 : std::stoi(limit, &used);
        if (!limit.empty() && used != limit.size())
            throw std::invalid_argument("limit");
    }
    catch (const std::exception &)
    {
        http::error(callback, k400BadRequest, "INVALID_ARGUMENT",
                    "limit debe ser un entero de 1 a 100.");
        return;
    }
    p["p_cursor"] = cursor.empty() ? Json::Value() : Json::Value(cursor);
    call(req, callback, "mirage_list_outfits", p, "OUTFIT_NOT_FOUND",
         [callback](const Json::Value &page, const std::string &) { http::reply(callback, k200OK, page); });
}
void OutfitController::getOutfit(const HttpRequestPtr &req, Callback &&callback, std::string id)
{
    Json::Value p;
    p["p_id"] = id;
    call(req, callback, "mirage_get_outfit", p, "OUTFIT_NOT_FOUND",
         [callback](const Json::Value &outfit, const std::string &) {
             if (outfit.isNull())
                 fail(callback, 404, "OUTFIT_NOT_FOUND");
             else
                 http::reply(callback, k200OK, outfit);
         });
}
void OutfitController::updateOutfit(const HttpRequestPtr &req, Callback &&callback,
                                    std::string id)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    Json::Value p;
    try
    {
        p["p_outfit"] = outfitPayload(body);
    }
    catch (const std::invalid_argument &e)
    {
        http::error(callback, k400BadRequest, "INVALID_ARGUMENT", e.what());
        return;
    }
    p["p_id"] = id;
    call(req, callback, "mirage_update_outfit", p, "OUTFIT_NOT_FOUND",
         [callback](const Json::Value &outfit, const std::string &) {
             http::reply(callback, k200OK, outfit);
         });
}
void OutfitController::deleteOutfit(const HttpRequestPtr &req, Callback &&callback,
                                    std::string id)
{
    Json::Value p;
    p["p_id"] = id;
    call(req, callback, "mirage_remove_outfit", p, "OUTFIT_NOT_FOUND",
         [callback](const Json::Value &, const std::string &) { http::noContent(callback); });
}
// Same flow as garment photos: the client PUTs the file to a signed URL, then registers it.
void OutfitController::imageUpload(const HttpRequestPtr &req, Callback &&callback,
                                   std::string id)
{
    Json::Value p;
    p["p_id"] = id;
    // Checking ownership first keeps other people's outfit ids from getting upload URLs.
    call(req, callback, "mirage_get_outfit", p, "OUTFIT_NOT_FOUND",
         [callback, id](const Json::Value &outfit, const std::string &userId) {
             if (outfit.isNull())
             {
                 fail(callback, 404, "OUTFIT_NOT_FOUND");
                 return;
             }
             const auto imageId = drogon::utils::getUuid();
             signer.signUpload({}, userId + "/outfits/" + id + "/" + imageId,
                               [callback, imageId](garment::SignResult signed_) {
                                   if (signed_.status != 200)
                                   {
                                       fail(callback, signed_.status, signed_.error.c_str());
                                       return;
                                   }
                                   Json::Value json;
                                   json["image_id"] = imageId;
                                   json["upload_url"] = signed_.url;
                                   json["method"] = "PUT";
                                   http::reply(callback, k200OK, json);
                               });
         });
}
void OutfitController::setImage(const HttpRequestPtr &req, Callback &&callback, std::string id)
{
    Json::Value body;
    if (!http::json(req, body, callback))
        return;
    if (!body["image_id"].isString())
    {
        http::error(callback, k400BadRequest, "INVALID_ARGUMENT", "image_id debe ser un UUID.");
        return;
    }
    Json::Value p;
    p["p_id"] = id;
    p["p_image_id"] = body["image_id"];
    call(req, callback, "mirage_set_outfit_image", p, "OUTFIT_NOT_FOUND",
         [callback](const Json::Value &outfit, const std::string &) {
             http::reply(callback, k200OK, outfit);
         });
}
void OutfitController::imageAccess(const HttpRequestPtr &req, Callback &&callback,
                                   std::string id)
{
    Json::Value p;
    p["p_id"] = id;
    call(req, callback, "mirage_outfit_image", p, "OUTFIT_NOT_FOUND",
         [callback](const Json::Value &image, const std::string &) {
             if (!image["image_id"].isString())
             {
                 fail(callback, 404, "IMAGE_NOT_FOUND");
                 return;
             }
             signer.sign({}, image["path"].asString(), kLinkSeconds,
                         [callback, image](garment::SignResult signed_) {
                             if (signed_.status != 200)
                             {
                                 fail(callback, signed_.status, signed_.error.c_str());
                                 return;
                             }
                             Json::Value json;
                             json["image_id"] = image["image_id"];
                             json["image_version"] = image["image_version"];
                             json["url"] = signed_.url;
                             json["expires_at"] = utcAfter(kLinkSeconds);
                             json["mime_type"] = image["mime_type"];
                             http::reply(callback, k200OK, json);
                         });
         });
}
} // namespace mirage::outfit
