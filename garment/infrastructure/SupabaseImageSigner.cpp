#include "SupabaseImageSigner.h"
#include "../../shared/infrastructure/SupabaseClient.h"
namespace mirage::garment
{
void SupabaseImageSigner::sign(const GarmentAccess &access, const std::string &path, int seconds,
                               SignCompletion done) const
{
    Json::Value body;
    body["expiresIn"] = seconds;
    SupabaseClient::request(
        drogon::Post, "/storage/v1/object/sign/garment-images/" + path, body, access.bearer,
        [done = std::move(done)](ApiResult result) {
            if (result.status >= 400)
            {
                if (result.status == 401 || result.status == 403)
                    done({401, "", "UNAUTHENTICATED"});
                else if (result.status == 404)
                    done({404, "", "IMAGE_NOT_FOUND"});
                else if (result.status >= 500)
                    done({503, "", "SERVICE_UNAVAILABLE"});
                else
                    done({500, "", "INTERNAL_ERROR"});
                return;
            }
            const auto relative = result.body.get("signedURL", "").asString();
            if (relative.empty() || relative[0] != '/')
            {
                done({503, "", "SERVICE_UNAVAILABLE"});
                return;
            }
            // Storage answers with a path relative to its own /storage/v1 root.
            done({200, SupabaseClient::baseUrl() + "/storage/v1" + relative, ""});
        });
}
} // namespace mirage::garment
