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
// Port: temporary HTTPS URLs for a private image. Implemented over Supabase Storage with the
// service's secret key, so clients never get direct Storage access.
class ImageSigner
{
  public:
    virtual ~ImageSigner() = default;
    virtual void sign(const GarmentAccess &, const std::string &path, int seconds,
                      SignCompletion) const = 0;
    // URL the client PUTs the image file to; valid for a limited time chosen by Storage.
    virtual void signUpload(const GarmentAccess &, const std::string &path,
                            SignCompletion) const = 0;
};
} // namespace mirage::garment
