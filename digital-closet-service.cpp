#include <drogon/drogon.h>

int main()
{
    drogon::app().setFileTypes({});
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
