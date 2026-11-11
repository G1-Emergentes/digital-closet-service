#pragma once
#include "../application/IdentityProvider.h"
namespace mirage::garment
{
// Same mechanism as iam-service: ask IAM's Auth for the user, then confirm the session is
// still active so a logout takes effect immediately.
class SupabaseIdentityProvider final : public IdentityProvider
{
  public:
    void currentUser(const std::string &bearer, IdentityCompletion) const override;
};
} // namespace mirage::garment
