#pragma once
#include "../application/ImageSigner.h"
namespace mirage::garment
{
// Signs private objects in the `garment-images` bucket with the caller's own JWT, so
// Storage policies decide access and no service key is needed.
class SupabaseImageSigner final : public ImageSigner
{
  public:
    void sign(const GarmentAccess &, const std::string &path, int seconds,
              SignCompletion) const override;
};
} // namespace mirage::garment
