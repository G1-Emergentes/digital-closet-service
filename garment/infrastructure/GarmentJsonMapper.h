#pragma once
#include "../domain/model/Garment.h"
#include <json/json.h>
namespace mirage::garment
{
// JSON in snake_case per the contract; kept outside the domain.
class GarmentJsonMapper
{
  public:
    // Request body -> editable attributes. Throws std::invalid_argument on any wrong type.
    static GarmentDetails details(const Json::Value &, bool availabilityRequired);
    // GarmentReadModel produced by the database.
    static Garment garment(const Json::Value &);
    static Json::Value toJson(const Garment &);
};
} // namespace mirage::garment
