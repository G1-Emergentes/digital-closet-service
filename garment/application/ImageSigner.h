#pragma once
#include "../domain/repositories/GarmentRepository.h"
namespace mirage::garment
{
struct SignResult
{
    int status;
    std::string url, error;
};
using SignCompletion = std::function<void(SignResult)>;
// Port: produces a temporary HTTPS URL for a private image. Implemented over Supabase Storage.
class ImageSigner
{
  public:
    virtual ~ImageSigner() = default;
    virtual void sign(const GarmentAccess &, const std::string &path, int seconds,
                      SignCompletion) const = 0;
};
} // namespace mirage::garment
