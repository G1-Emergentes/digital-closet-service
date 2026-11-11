#pragma once
#include <functional>
#include <string>
namespace mirage::garment
{
// `error` carries a contract error code when status != 200.
struct IdentityResult
{
    int status;
    std::string userId, error;
};
using IdentityCompletion = std::function<void(IdentityResult)>;
// Port: who does this bearer token belong to? Users live in IAM, not in this database.
class IdentityProvider
{
  public:
    virtual ~IdentityProvider() = default;
    virtual void currentUser(const std::string &bearer, IdentityCompletion) const = 0;
};
} // namespace mirage::garment
