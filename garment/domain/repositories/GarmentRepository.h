#pragma once
#include "../model/Garment.h"
#include <functional>
namespace mirage::garment
{
// Credentials stay in the request context; the owner is derived from them by the database.
struct GarmentAccess
{
    std::string bearer;
};
// `error` carries a contract error code (e.g. GARMENT_NOT_FOUND) when status != 2xx.
struct GarmentResult
{
    int status;
    std::optional<Garment> garment;
    std::string error;
};
struct StatusResult
{
    int status;
    std::string error;
};
struct GarmentPage
{
    int status;
    std::vector<Garment> items;
    std::optional<std::string> nextCursor;
    std::string error;
};
enum class ResolveStatus
{
    Available,
    Unavailable,
    NotFound
};
struct ResolvedGarment
{
    std::string id;
    ResolveStatus status;
    std::optional<Garment> garment; // only for Available
};
struct ResolveResult
{
    int status;
    std::vector<ResolvedGarment> results;
    std::string error;
};
// Storage location of the garment photo, relative to the private bucket.
struct ImageReference
{
    int status;
    std::string imageId, path, mimeType, error;
    int version = 0;
};
using GarmentCompletion = std::function<void(GarmentResult)>;
using StatusCompletion = std::function<void(StatusResult)>;
using PageCompletion = std::function<void(GarmentPage)>;
using ResolveCompletion = std::function<void(ResolveResult)>;
using ImageReferenceCompletion = std::function<void(ImageReference)>;
class GarmentRepository
{
  public:
    virtual ~GarmentRepository() = default;
    virtual void create(const GarmentAccess &, const Garment &, GarmentCompletion) const = 0;
    virtual void update(const GarmentAccess &, const std::string &id, const GarmentDetails &,
                        GarmentCompletion) const = 0;
    virtual void remove(const GarmentAccess &, const std::string &id, StatusCompletion) const = 0;
    virtual void setImage(const GarmentAccess &, const std::string &id,
                          const std::string &imageId, GarmentCompletion) const = 0;
    virtual void findById(const GarmentAccess &, const std::string &id,
                          GarmentCompletion) const = 0;
    virtual void list(const GarmentAccess &, int limit, const std::optional<std::string> &cursor,
                      PageCompletion) const = 0;
    virtual void resolve(const GarmentAccess &, const std::vector<std::string> &ids,
                         ResolveCompletion) const = 0;
    virtual void imageReference(const GarmentAccess &, const std::string &id,
                                ImageReferenceCompletion) const = 0;
};
} // namespace mirage::garment
