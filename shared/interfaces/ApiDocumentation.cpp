#include "ApiDocumentation.h"
#include <drogon/drogon.h>
namespace mirage::http
{
// shortcut: same helper as iam-service/shared (minus its auth e-mail pages); extract to a
// common library if a third service needs it.
void registerApiDocumentation(const std::filesystem::path &directory)
{
    // Expose only documentation files; never make the project folder a static root.
    const auto serve = [&directory](const std::string &route, const char *file) {
        const auto path = (directory / file).string();
        drogon::app().registerHandler(
            route,
            [path](const drogon::HttpRequestPtr &request,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
                auto response =
                    drogon::HttpResponse::newFileResponse(path, "", drogon::CT_NONE, "", request);
                response->addHeader("Cache-Control", "no-store");
                response->addHeader("X-Content-Type-Options", "nosniff");
                response->addHeader("Referrer-Policy", "no-referrer");
                response->addHeader(
                    "Content-Security-Policy",
                    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                    "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'");
                callback(response);
            },
            {drogon::Get});
    };
    for (const auto *route : {"/docs", "/docs/", "/swagger", "/swagger/"})
        serve(route, "index.html");
    serve("/openapi.json", "openapi.json");
    serve("/docs/swagger-ui.css", "swagger-ui.css");
    serve("/docs/swagger-ui-bundle.js", "swagger-ui-bundle.js");
    serve("/docs/swagger-initializer.js", "swagger-initializer.js");
}
} // namespace mirage::http
