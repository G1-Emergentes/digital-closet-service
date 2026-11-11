#pragma once
#include "GarmentCommands.h"
#include "ImageSigner.h"
namespace mirage::garment
{
// Validation errors are reported synchronously as std::invalid_argument.
class GarmentCommandService
{
    const GarmentRepository &repository_;

  public:
    explicit GarmentCommandService(const GarmentRepository &repository) : repository_(repository) {}
    void handle(const CreateGarmentCommand &, GarmentCompletion) const;
    void handle(const UpdateGarmentCommand &, GarmentCompletion) const;
    void handle(const DeleteGarmentCommand &, StatusCompletion) const;
    void handle(const SetGarmentImageCommand &, GarmentCompletion) const;
};
class GarmentQueryService
{
    const GarmentRepository &repository_;

  public:
    explicit GarmentQueryService(const GarmentRepository &repository) : repository_(repository) {}
    void handle(const GetGarmentQuery &, GarmentCompletion) const;
    void handle(const ListGarmentsQuery &, PageCompletion) const;
    void handle(const ResolveGarmentsQuery &, ResolveCompletion) const;
};
struct ImageAccess
{
    int status;
    std::string imageId, url, expiresAt, mimeType, error;
    int version = 0;
};
using ImageAccessCompletion = std::function<void(ImageAccess)>;
struct ImageUpload
{
    int status;
    std::string imageId, url, error;
};
using ImageUploadCompletion = std::function<void(ImageUpload)>;
// Contract section 7: the link lasts five minutes and is never persisted.
class GarmentImageService
{
    const GarmentRepository &repository_;
    const ImageSigner &signer_;

  public:
    static constexpr int kLinkSeconds = 300;
    GarmentImageService(const GarmentRepository &repository, const ImageSigner &signer)
        : repository_(repository), signer_(signer)
    {
    }
    void handle(const GarmentImageAccessQuery &, ImageAccessCompletion) const;
    // Only for an existing garment of the caller; the object path is {user}/{garment}/{image}.
    void handle(const GarmentImageUploadQuery &, ImageUploadCompletion) const;
};
} // namespace mirage::garment
