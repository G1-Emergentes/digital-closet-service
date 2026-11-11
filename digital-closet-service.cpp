#include "shared/interfaces/ApiDocumentation.h"
#include <drogon/drogon.h>

int main(int argc, char *argv[])
{
    drogon::app().setFileTypes({});
    mirage::http::registerApiDocumentation(std::filesystem::absolute(argv[0]).parent_path() /
                                           "docs");
    LOG_INFO << "Servidor Digital Closet iniciando en http://localhost:8081";

    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            response->setBody("ok");
            callback(response);
        },
        {drogon::Get});

    // 8081: IAM already uses 8080.
    drogon::app().addListener("0.0.0.0", 8081);
    drogon::app().run();
    return 0;
}
