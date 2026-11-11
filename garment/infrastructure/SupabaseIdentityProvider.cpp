#include "SupabaseIdentityProvider.h"
#include "../../shared/infrastructure/SupabaseClient.h"
#include "../domain/model/Garment.h"
namespace mirage::garment
{
namespace
{
// Provider failures (including a missing configuration) are 503; anything else that is
// not a valid session is a rejected token.
IdentityResult failureOf(const ApiResult &result)
{
    if (result.status >= 500)
        return {503, "", "SERVICE_UNAVAILABLE"};
    return {401, "", "UNAUTHENTICATED"};
}
} // namespace
void SupabaseIdentityProvider::currentUser(const std::string &bearer,
                                           IdentityCompletion done) const
{
    SupabaseClient::request(
        SupabaseTarget::Identity, drogon::Get, "/auth/v1/user", Json::Value(Json::objectValue),
        bearer, [bearer, done = std::move(done)](ApiResult user) mutable {
            const auto id = user.body.get("id", "").asString();
            if (user.status != 200 || !isUuid(id))
            {
                done(user.status == 200 ? IdentityResult{503, "", "SERVICE_UNAVAILABLE"}
                                        : failureOf(user));
                return;
            }
            SupabaseClient::request(
                SupabaseTarget::Identity, drogon::Post, "/rest/v1/rpc/mirage_session_active",
                Json::Value(Json::objectValue), bearer,
                [id, done = std::move(done)](ApiResult active) {
                    if (active.status >= 400 || !active.body.isBool())
                    {
                        done(failureOf(active));
                        return;
                    }
                    if (!active.body.asBool())
                    {
                        done({401, "", "UNAUTHENTICATED"}); // revoked session
                        return;
                    }
                    done({200, id, ""});
                });
        });
}
} // namespace mirage::garment
