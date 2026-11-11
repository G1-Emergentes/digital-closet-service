#pragma once
#include "../application/ApiResult.h"
#include <drogon/HttpTypes.h>
namespace mirage
{
class SupabaseClient
{
  public:
    static void request(drogon::HttpMethod method, const std::string &path, const Json::Value &body,
                        const std::string &bearer, Completion done);
    // Configured SUPABASE_URL without validation; request() performs the checks.
    static std::string baseUrl();
};
} // namespace mirage
