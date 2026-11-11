#include "GarmentJsonMapper.h"
namespace mirage::garment
{
namespace
{
std::string text(const Json::Value &json, const char *name)
{
    if (!json[name].isString())
        throw std::invalid_argument(std::string(name) + " debe ser texto.");
    return json[name].asString();
}
std::vector<std::string> list(const Json::Value &json, const char *name)
{
    const auto &values = json[name];
    if (!values.isArray() || values.size() > 50)
        throw std::invalid_argument(std::string(name) + " debe ser una lista de texto.");
    std::vector<std::string> result;
    for (const auto &value : values)
    {
        if (!value.isString())
            throw std::invalid_argument(std::string(name) + " debe ser una lista de texto.");
        result.push_back(value.asString());
    }
    return result;
}
Json::Value array(const std::vector<std::string> &values)
{
    Json::Value json(Json::arrayValue);
    for (const auto &value : values)
        json.append(value);
    return json;
}
} // namespace
GarmentDetails GarmentJsonMapper::details(const Json::Value &json, bool availabilityRequired)
{
    if (!json.isObject())
        throw std::invalid_argument("El cuerpo debe ser un objeto JSON.");
    GarmentDetails d;
    d.name = text(json, "name");
    d.category = parseCategory(text(json, "category"));
    if (json.isMember("subcategory") && !json["subcategory"].isNull())
        d.subcategory = text(json, "subcategory");
    d.colors = list(json, "colors");
    d.styles = list(json, "styles");
    d.description = text(json, "description");
    if (json.isMember("availability"))
        d.availability = parseAvailability(text(json, "availability"));
    else if (availabilityRequired)
        throw std::invalid_argument("availability es obligatorio.");
    return d;
}
Garment GarmentJsonMapper::garment(const Json::Value &json)
{
    if (!json.isObject() || !json["revision"].isInt())
        throw std::invalid_argument("invalid_garment_storage_response");
    std::optional<GarmentImage> image;
    const auto &stored = json["image"];
    if (!stored.isNull())
    {
        if (!stored["id"].isString() || !stored["version"].isInt())
            throw std::invalid_argument("invalid_garment_storage_response");
        image = GarmentImage{stored["id"].asString(), stored["version"].asInt()};
    }
    return Garment(text(json, "id"), details(json, true), image, json["revision"].asInt(),
                   text(json, "updated_at"));
}
Json::Value GarmentJsonMapper::toJson(const Garment &garment)
{
    const auto &d = garment.details();
    Json::Value json;
    json["id"] = garment.id();
    json["name"] = d.name;
    json["category"] = toString(d.category);
    json["subcategory"] = d.subcategory ? Json::Value(*d.subcategory) : Json::Value();
    json["colors"] = array(d.colors);
    json["styles"] = array(d.styles);
    json["description"] = d.description;
    json["availability"] = toString(d.availability);
    if (garment.image())
    {
        json["image"]["id"] = garment.image()->id;
        json["image"]["version"] = garment.image()->version;
    }
    else
        json["image"] = Json::Value();
    json["revision"] = garment.revision();
    json["updated_at"] = garment.updatedAt();
    return json;
}
} // namespace mirage::garment
