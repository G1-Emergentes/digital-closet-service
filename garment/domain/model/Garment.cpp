#include "Garment.h"
#include <algorithm>
#include <cctype>
#include <set>
namespace mirage::garment
{
namespace
{
const std::set<std::string> kColors{"BLACK",  "WHITE",  "GRAY",   "BLUE",   "RED",
                                    "GREEN",  "YELLOW", "ORANGE", "PURPLE", "PINK",
                                    "BROWN",  "BEIGE",  "MULTICOLOR"};
const std::set<std::string> kStyles{"CASUAL",     "FORMAL",   "SPORT",   "STREETWEAR",
                                    "MINIMALIST", "CLASSIC",  "BOHEMIAN"};
size_t characters(const std::string &value)
{
    size_t count = 0;
    for (unsigned char c : value)
        if ((c & 0xC0) != 0x80)
            ++count;
    return count;
}
void text(const std::string &value, const char *field, size_t max)
{
    const bool blank = std::all_of(value.begin(), value.end(),
                                   [](unsigned char c) { return std::isspace(c) != 0; });
    if (blank || characters(value) > max || value.find('\0') != std::string::npos)
        throw std::invalid_argument(std::string(field) + " debe tener entre 1 y " +
                                    std::to_string(max) + " caracteres.");
}
void codes(const std::vector<std::string> &values, const std::set<std::string> &allowed,
           const char *field)
{
    std::set<std::string> seen;
    for (const auto &value : values)
        if (!allowed.count(value) || !seen.insert(value).second)
            throw std::invalid_argument(std::string(field) +
                                        " contiene códigos desconocidos o repetidos.");
}
void subcategory(const std::string &value)
{
    const bool valid = !value.empty() && value.size() <= 60 &&
                       std::all_of(value.begin(), value.end(), [](unsigned char c) {
                           return std::isupper(c) || std::isdigit(c) || c == '_';
                       });
    if (!valid)
        throw std::invalid_argument(
            "subcategory debe tener 1-60 caracteres en MAYÚSCULAS, dígitos o guion bajo.");
}
} // namespace
const char *toString(Category category)
{
    switch (category)
    {
    case Category::Top:
        return "TOP";
    case Category::Bottom:
        return "BOTTOM";
    case Category::OnePiece:
        return "ONE_PIECE";
    case Category::Outerwear:
        return "OUTERWEAR";
    case Category::Footwear:
        return "FOOTWEAR";
    }
    return "ACCESSORY";
}
const char *toString(Availability availability)
{
    return availability == Availability::Available ? "AVAILABLE" : "UNAVAILABLE";
}
Category parseCategory(const std::string &value)
{
    for (auto category : {Category::Top, Category::Bottom, Category::OnePiece, Category::Outerwear,
                          Category::Footwear, Category::Accessory})
        if (value == toString(category))
            return category;
    throw std::invalid_argument("category no es un código válido.");
}
Availability parseAvailability(const std::string &value)
{
    for (auto availability : {Availability::Available, Availability::Unavailable})
        if (value == toString(availability))
            return availability;
    throw std::invalid_argument("availability debe ser AVAILABLE o UNAVAILABLE.");
}
bool isUuid(const std::string &value)
{
    if (value.size() != 36)
        return false;
    for (size_t i = 0; i < value.size(); ++i)
    {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? value[i] != '-' : !std::isxdigit(static_cast<unsigned char>(value[i])))
            return false;
    }
    return true;
}
void validate(const GarmentDetails &details)
{
    text(details.name, "name", 120);
    text(details.description, "description", 1000);
    if (details.subcategory)
        subcategory(*details.subcategory);
    codes(details.colors, kColors, "colors");
    codes(details.styles, kStyles, "styles");
}
Garment::Garment(std::string id, GarmentDetails details, std::optional<GarmentImage> image,
                 int revision, std::string updatedAt)
    : id_(std::move(id)), details_(std::move(details)), image_(std::move(image)),
      revision_(revision), updatedAt_(std::move(updatedAt))
{
    if (!isUuid(id_))
        throw std::invalid_argument("id debe ser un UUID.");
    validate(details_);
}
void Garment::update(GarmentDetails details)
{
    validate(details); // validate first: an invalid update changes nothing
    details_ = std::move(details);
}
} // namespace mirage::garment
