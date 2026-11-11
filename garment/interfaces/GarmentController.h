#pragma once
#include <drogon/HttpController.h>
namespace mirage::garment
{
class GarmentController : public drogon::HttpController<GarmentController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GarmentController::listGarments, "/api/v1/garments", drogon::Get);
    ADD_METHOD_TO(GarmentController::createGarment, "/api/v1/garments", drogon::Post);
    ADD_METHOD_TO(GarmentController::resolveGarments, "/api/v1/garments/resolve", drogon::Post);
    ADD_METHOD_TO(GarmentController::getGarment, "/api/v1/garments/{1}", drogon::Get);
    ADD_METHOD_TO(GarmentController::updateGarment, "/api/v1/garments/{1}", drogon::Put);
    ADD_METHOD_TO(GarmentController::deleteGarment, "/api/v1/garments/{1}", drogon::Delete);
    ADD_METHOD_TO(GarmentController::imageUpload, "/api/v1/garments/{1}/image-upload",
                  drogon::Post);
    ADD_METHOD_TO(GarmentController::setImage, "/api/v1/garments/{1}/image", drogon::Put);
    ADD_METHOD_TO(GarmentController::imageAccess, "/api/v1/garments/{1}/image-access",
                  drogon::Get);
    METHOD_LIST_END
    using Callback = std::function<void(const drogon::HttpResponsePtr &)>;
    void listGarments(const drogon::HttpRequestPtr &, Callback &&);
    void createGarment(const drogon::HttpRequestPtr &, Callback &&);
    void resolveGarments(const drogon::HttpRequestPtr &, Callback &&);
    void getGarment(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void updateGarment(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void deleteGarment(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void imageUpload(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void setImage(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void imageAccess(const drogon::HttpRequestPtr &, Callback &&, std::string id);
};
} // namespace mirage::garment
