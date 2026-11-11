#include "SupabaseGarmentRepository.h"
#include "../../shared/infrastructure/SupabaseClient.h"
#include "GarmentJsonMapper.h"
namespace mirage::garment
{
namespace
{
struct Failure
{
    int status;
    const char *code;
};
// Maps a PostgREST/PostgreSQL error to the contract's HTTP status and error code.
Failure failureOf(const ApiResult &result)
{
    const auto code = result.body.get("code", "").asString();
    if (code == "PGRST202" || code == "42P01")
        return {503, "SERVICE_UNAVAILABLE"}; // migration not installed
    if (code == "P0002")
        return {404, "GARMENT_NOT_FOUND"};
    if (code == "42501" || code == "PGRST301" || code == "PGRST303" || result.status == 401)
        return {401, "UNAUTHENTICATED"};
    if (code == "23514" || code == "22P02" || code == "22007" || code == "MG001")
        return {400, "INVALID_ARGUMENT"};
    if (result.status >= 500)
        return {503, "SERVICE_UNAVAILABLE"};
    return {500, "INTERNAL_ERROR"};
}
using Handler = std::function<void(const Json::Value &)>;
// Calls an RPC and routes failures to `fail`; `ok` only sees successful JSON bodies.
void rpc(const char *name, const GarmentAccess &access, const Json::Value &parameters,
         std::function<void(int, const char *)> fail, Handler ok)
{
    SupabaseClient::request(
        drogon::Post, std::string("/rest/v1/rpc/") + name, parameters, access.bearer,
        [fail = std::move(fail), ok = std::move(ok)](ApiResult result) {
            if (result.status >= 400)
            {
                const auto failure = failureOf(result);
                fail(failure.status, failure.code);
                return;
            }
            try
            {
                ok(result.body);
            }
            catch (const std::exception &)
            {
                fail(503, "SERVICE_UNAVAILABLE"); // storage returned an unexpected shape
            }
        });
}
void garmentRpc(const char *name, const GarmentAccess &access, const Json::Value &parameters,
                GarmentCompletion done)
{
    auto shared = std::make_shared<GarmentCompletion>(std::move(done));
    rpc(
        name, access, parameters,
        [shared](int status, const char *code) { (*shared)({status, std::nullopt, code}); },
        [shared](const Json::Value &body) {
            if (body.isNull())
            {
                (*shared)({404, std::nullopt, "GARMENT_NOT_FOUND"});
                return;
            }
            (*shared)({200, GarmentJsonMapper::garment(body), ""});
        });
}
} // namespace
void SupabaseGarmentRepository::create(const GarmentAccess &access, const Garment &garment,
                                       GarmentCompletion done) const
{
    Json::Value p;
    p["p_garment"] = GarmentJsonMapper::toJson(garment);
    garmentRpc("mirage_create_garment", access, p, std::move(done));
}
void SupabaseGarmentRepository::update(const GarmentAccess &access, const std::string &id,
                                       const GarmentDetails &details, GarmentCompletion done) const
{
    // Reuse the read-model mapper so the payload shape stays in one place.
    Json::Value p;
    p["p_id"] = id;
    p["p_garment"] = GarmentJsonMapper::toJson(Garment(id, details, std::nullopt, 0, ""));
    garmentRpc("mirage_update_garment", access, p, std::move(done));
}
void SupabaseGarmentRepository::remove(const GarmentAccess &access, const std::string &id,
                                       StatusCompletion done) const
{
    Json::Value p;
    p["p_id"] = id;
    auto shared = std::make_shared<StatusCompletion>(std::move(done));
    rpc(
        "mirage_delete_garment", access, p,
        [shared](int status, const char *code) { (*shared)({status, code}); },
        [shared](const Json::Value &) { (*shared)({204, ""}); });
}
void SupabaseGarmentRepository::setImage(const GarmentAccess &access, const std::string &id,
                                         const std::string &imageId, GarmentCompletion done) const
{
    Json::Value p;
    p["p_id"] = id;
    p["p_image_id"] = imageId;
    garmentRpc("mirage_set_garment_image", access, p, std::move(done));
}
void SupabaseGarmentRepository::findById(const GarmentAccess &access, const std::string &id,
                                         GarmentCompletion done) const
{
    Json::Value p;
    p["p_id"] = id;
    garmentRpc("mirage_get_garment", access, p, std::move(done));
}
void SupabaseGarmentRepository::list(const GarmentAccess &access, int limit,
                                     const std::optional<std::string> &cursor,
                                     PageCompletion done) const
{
    Json::Value p;
    p["p_limit"] = limit;
    p["p_cursor"] = cursor ? Json::Value(*cursor) : Json::Value();
    auto shared = std::make_shared<PageCompletion>(std::move(done));
    rpc(
        "mirage_list_garments", access, p,
        [shared](int status, const char *code) { (*shared)({status, {}, std::nullopt, code}); },
        [shared](const Json::Value &body) {
            GarmentPage page{200, {}, std::nullopt, ""};
            for (const auto &item : body["items"])
                page.items.push_back(GarmentJsonMapper::garment(item));
            if (body["next_cursor"].isString())
                page.nextCursor = body["next_cursor"].asString();
            (*shared)(std::move(page));
        });
}
void SupabaseGarmentRepository::resolve(const GarmentAccess &access,
                                        const std::vector<std::string> &ids,
                                        ResolveCompletion done) const
{
    Json::Value p;
    p["p_ids"] = Json::Value(Json::arrayValue);
    for (const auto &id : ids)
        p["p_ids"].append(id);
    auto shared = std::make_shared<ResolveCompletion>(std::move(done));
    rpc(
        "mirage_resolve_garments", access, p,
        [shared](int status, const char *code) { (*shared)({status, {}, code}); },
        [shared](const Json::Value &body) {
            ResolveResult result{200, {}, ""};
            for (const auto &item : body["results"])
            {
                const auto status = item["status"].asString();
                ResolvedGarment entry{item["id"].asString(), ResolveStatus::NotFound, std::nullopt};
                if (status == "AVAILABLE")
                {
                    entry.status = ResolveStatus::Available;
                    entry.garment = GarmentJsonMapper::garment(item["garment"]);
                }
                else if (status == "UNAVAILABLE")
                    entry.status = ResolveStatus::Unavailable;
                else if (status != "NOT_FOUND")
                    throw std::invalid_argument("invalid_resolve_status");
                result.results.push_back(std::move(entry));
            }
            (*shared)(std::move(result));
        });
}
void SupabaseGarmentRepository::imageReference(const GarmentAccess &access, const std::string &id,
                                               ImageReferenceCompletion done) const
{
    Json::Value p;
    p["p_id"] = id;
    auto shared = std::make_shared<ImageReferenceCompletion>(std::move(done));
    rpc(
        "mirage_garment_image", access, p,
        [shared](int status, const char *code) {
            (*shared)({status, "", "", "", code, 0});
        },
        [shared](const Json::Value &body) {
            if (!body["image_id"].isString())
            {
                (*shared)({404, "", "", "", "IMAGE_NOT_FOUND", 0});
                return;
            }
            (*shared)({200, body["image_id"].asString(), body["path"].asString(),
                       body["mime_type"].asString(), "", body["image_version"].asInt()});
        });
}
} // namespace mirage::garment
