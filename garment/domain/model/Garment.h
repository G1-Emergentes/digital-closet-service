#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace mirage::garment
{
enum class Category
{
    Top,
    Bottom,
    OnePiece,
    Outerwear,
    Footwear,
    Accessory
};
enum class Availability
{
    Available,
    Unavailable
};
const char *toString(Category);
const char *toString(Availability);
// Both throw std::invalid_argument for codes outside the contract vocabulary.
Category parseCategory(const std::string &);
Availability parseAvailability(const std::string &);
bool isUuid(const std::string &);

// Value object: the source photo of a garment. `version` grows when its content changes.
struct GarmentImage
{
    std::string id;
    int version;
};

// Editable attributes, validated as a unit so a rejected update leaves no partial state.
struct GarmentDetails
{
    std::string name;
    Category category = Category::Top;
    std::optional<std::string> subcategory;
    std::vector<std::string> colors, styles;
    std::string description;
    Availability availability = Availability::Available;
};
void validate(const GarmentDetails &); // throws std::invalid_argument

// Aggregate root. The owner is not part of the domain: PostgreSQL derives it from the
// authenticated session. `revision` and `updatedAt` are assigned by persistence.
class Garment
{
    std::string id_;
    GarmentDetails details_;
    std::optional<GarmentImage> image_;
    int revision_;
    std::string updatedAt_;

  public:
    Garment(std::string id, GarmentDetails details, std::optional<GarmentImage> image,
            int revision, std::string updatedAt);
    const std::string &id() const
    {
        return id_;
    }
    const GarmentDetails &details() const
    {
        return details_;
    }
    const std::optional<GarmentImage> &image() const
    {
        return image_;
    }
    int revision() const
    {
        return revision_;
    }
    const std::string &updatedAt() const
    {
        return updatedAt_;
    }
    void update(GarmentDetails details);
};
} // namespace mirage::garment
