#include "SupabaseClient.h"
#include <cstdlib>
#include <drogon/HttpClient.h>
#include <regex>
using namespace drogon;
namespace mirage
{
namespace
{
std::string environment(const char *name)
{
#ifdef _WIN32
    char *value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0)
        return {};
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const auto value = std::getenv(name);
    return value ? value : "";
#endif
}

} // namespace
std::string SupabaseClient::baseUrl()
{
    return environment("SUPABASE_URL");
}
void SupabaseClient::request(HttpMethod method, const std::string &path, const Json::Value &body,
                             const std::string &bearer, Completion done)
{
    const auto url = environment("SUPABASE_URL");
    const auto key = environment("SUPABASE_PUBLISHABLE_KEY");
    static const std::regex origin(
        R"(^https://[A-Za-z0-9.-]+(:[0-9]+)?$|^http://(127\.0\.0\.1|localhost):[0-9]+$)");
    if (url.empty() || key.empty() || !std::regex_match(url, origin) ||
        key.find_first_of("\r\n") != std::string::npos)
    {
        done(failure(503, "auth_not_configured"));
        return;
    }
    auto client = HttpClient::newHttpClient(url);
    auto req = HttpRequest::newHttpJsonRequest(body);
    req->setMethod(method);
    const auto query = path.find('?');
    req->setPath(path.substr(0, query));
    if (query != std::string::npos)
    {
        const auto equals = path.find('=', query);
        req->setQueryParameter(path.substr(query + 1, equals - query - 1), path.substr(equals + 1));
    }
    req->addHeader("apikey", key);
    if (!bearer.empty())
        req->addHeader("Authorization", bearer);
    client->sendRequest(
        req,
        [client, done = std::move(done)](ReqResult result, const HttpResponsePtr &res) {
            if (result != ReqResult::Ok || !res)
            {
                done(failure(502, "provider_unavailable"));
                return;
            }
            auto value = res->getJsonObject();
            if (res->statusCode() != k204NoContent && !value)
            {
                done(failure(502, "invalid_provider_response"));
                return;
            }
            done({static_cast<int>(res->statusCode()),
                  value ? *value : Json::Value(Json::objectValue)});
        },
        10.0);
}
} // namespace mirage
