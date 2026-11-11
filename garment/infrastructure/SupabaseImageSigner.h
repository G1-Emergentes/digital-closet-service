#pragma once
#include "../application/ImageSigner.h"
namespace mirage::garment
{
// Signs objects in the private `garment-images` bucket with the service's secret key.
class SupabaseImageSigner final : public ImageSigner
{
  public:
    void sign(const GarmentAccess &, const std::string &path, int seconds,
              SignCompletion) const override;
    void signUpload(const GarmentAccess &, const std::string &path,
                    SignCompletion) const override;
};
} // namespace mirage::garment
