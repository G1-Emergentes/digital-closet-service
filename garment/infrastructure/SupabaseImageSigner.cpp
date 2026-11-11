#include "SupabaseImageSigner.h"
#include "../../shared/infrastructure/SupabaseClient.h"
namespace mirage::garment
{
namespace
{
// Storage answers with a path relative to its own /storage/v1 root, under `key`.
void signed_(const std::string &path, Json::Value body, const char *key, SignCompletion done)
{
    SupabaseClient::request(
        SupabaseTarget::Wardrobe, drogon::Post, path, body, "",
        [key, done = std::move(done)](ApiResult result) {
            if (result.status >= 400)
            {
                if (result.status == 404)
                    done({404, "", "IMAGE_NOT_FOUND"});
                else if (result.status >= 500 || result.status == 401 || result.status == 403)
                    done({503, "", "SERVICE_UNAVAILABLE"}); // our own credentials, not the user's
                else
                    done({500, "", "INTERNAL_ERROR"});
                return;
            }
            const auto relative = result.body.get(key, "").asString();
            if (relative.empty() || relative[0] != '/')
            {
                done({503, "", "SERVICE_UNAVAILABLE"});
                return;
            }
            done({200, SupabaseClient::baseUrl(SupabaseTarget::Wardrobe) + "/storage/v1" + relative,
                  ""});
        });
}
} // namespace
void SupabaseImageSigner::sign(const GarmentAccess &, const std::string &path, int seconds,
                               SignCompletion done) const
{
    Json::Value body;
    body["expiresIn"] = seconds;
    signed_("/storage/v1/object/sign/garment-images/" + path, body, "signedURL", std::move(done));
}
void SupabaseImageSigner::signUpload(const GarmentAccess &, const std::string &path,
                                     SignCompletion done) const
{
    signed_("/storage/v1/object/upload/sign/garment-images/" + path,
            Json::Value(Json::objectValue), "url", std::move(done));
}
} // namespace mirage::garment
