#pragma once
#include "../domain/repositories/GarmentRepository.h"
namespace mirage::garment
{
// Persistence through Supabase RPC with the caller's JWT: PostgreSQL derives the owner.
class SupabaseGarmentRepository final : public GarmentRepository
{
  public:
    void create(const GarmentAccess &, const Garment &, GarmentCompletion) const override;
    void update(const GarmentAccess &, const std::string &id, const GarmentDetails &,
                GarmentCompletion) const override;
    void remove(const GarmentAccess &, const std::string &id, StatusCompletion) const override;
    void setImage(const GarmentAccess &, const std::string &id, const std::string &imageId,
                  GarmentCompletion) const override;
    void findById(const GarmentAccess &, const std::string &id, GarmentCompletion) const override;
    void list(const GarmentAccess &, int limit, const std::optional<std::string> &cursor,
              PageCompletion) const override;
    void resolve(const GarmentAccess &, const std::vector<std::string> &ids,
                 ResolveCompletion) const override;
    void imageReference(const GarmentAccess &, const std::string &id,
                        ImageReferenceCompletion) const override;
};
} // namespace mirage::garment
