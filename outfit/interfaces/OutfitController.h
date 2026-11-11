#pragma once
#include <drogon/HttpController.h>
namespace mirage::outfit
{
// Saved outfits. Recommendation keeps its own proposals; the ones the user decides to keep are
// saved here with POST /outfits.
class OutfitController : public drogon::HttpController<OutfitController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OutfitController::listOutfits, "/api/v1/outfits", drogon::Get);
    ADD_METHOD_TO(OutfitController::createOutfit, "/api/v1/outfits", drogon::Post);
    ADD_METHOD_TO(OutfitController::getOutfit, "/api/v1/outfits/{1}", drogon::Get);
    ADD_METHOD_TO(OutfitController::deleteOutfit, "/api/v1/outfits/{1}", drogon::Delete);
    ADD_METHOD_TO(OutfitController::imageUpload, "/api/v1/outfits/{1}/image-upload",
                  drogon::Post);
    ADD_METHOD_TO(OutfitController::setImage, "/api/v1/outfits/{1}/image", drogon::Put);
    ADD_METHOD_TO(OutfitController::imageAccess, "/api/v1/outfits/{1}/image-access",
                  drogon::Get);
    METHOD_LIST_END
    using Callback = std::function<void(const drogon::HttpResponsePtr &)>;
    void listOutfits(const drogon::HttpRequestPtr &, Callback &&);
    void createOutfit(const drogon::HttpRequestPtr &, Callback &&);
    void getOutfit(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void deleteOutfit(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void imageUpload(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void setImage(const drogon::HttpRequestPtr &, Callback &&, std::string id);
    void imageAccess(const drogon::HttpRequestPtr &, Callback &&, std::string id);
};
} // namespace mirage::outfit
