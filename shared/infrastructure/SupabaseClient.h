#pragma once
#include "../application/ApiResult.h"
#include <drogon/HttpTypes.h>
namespace mirage
{
// Two Supabase projects: this service's own database (database per service) and IAM's
// project, used only to validate who a bearer token belongs to.
enum class SupabaseTarget
{
    // SUPABASE_URL + SUPABASE_SECRET_KEY: server-side only. The caller's bearer is not sent.
    Wardrobe,
    // IAM_SUPABASE_URL + IAM_SUPABASE_PUBLISHABLE_KEY: the caller's bearer is forwarded.
    Identity
};
class SupabaseClient
{
  public:
    static void request(SupabaseTarget target, drogon::HttpMethod method, const std::string &path,
                        const Json::Value &body, const std::string &bearer, Completion done);
    // Configured URL of a target without validation; request() performs the checks.
    static std::string baseUrl(SupabaseTarget target);
};
} // namespace mirage
