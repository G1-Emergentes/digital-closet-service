#include "GarmentServices.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <set>
namespace mirage::garment
{
namespace
{
void requireUuid(const std::string &value, const char *field)
{
    if (!isUuid(value))
        throw std::invalid_argument(std::string(field) + " debe ser un UUID.");
}
std::string utcAfter(int seconds)
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() +
                                                           std::chrono::seconds(seconds));
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &time);
#else
    gmtime_r(&time, &parts);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buffer;
}
} // namespace
void GarmentCommandService::handle(const CreateGarmentCommand &command,
                                   GarmentCompletion done) const
{
    // Building the aggregate validates the id and every attribute.
    repository_.create(command.access, Garment(command.id, command.details, std::nullopt, 0, ""),
                       std::move(done));
}
void GarmentCommandService::handle(const UpdateGarmentCommand &command,
                                   GarmentCompletion done) const
{
    requireUuid(command.id, "id");
    validate(command.details);
    repository_.update(command.access, command.id, command.details, std::move(done));
}
void GarmentCommandService::handle(const DeleteGarmentCommand &command,
                                   StatusCompletion done) const
{
    requireUuid(command.id, "id");
    repository_.remove(command.access, command.id, std::move(done));
}
void GarmentCommandService::handle(const SetGarmentImageCommand &command,
                                   GarmentCompletion done) const
{
    requireUuid(command.id, "id");
    requireUuid(command.imageId, "image_id");
    repository_.setImage(command.access, command.id, command.imageId, std::move(done));
}
void GarmentQueryService::handle(const GetGarmentQuery &query, GarmentCompletion done) const
{
    requireUuid(query.id, "id");
    repository_.findById(query.access, query.id, std::move(done));
}
void GarmentQueryService::handle(const ListGarmentsQuery &query, PageCompletion done) const
{
    if (query.limit < 1 || query.limit > 100)
        throw std::invalid_argument("limit debe estar entre 1 y 100.");
    if (query.cursor)
        requireUuid(*query.cursor, "cursor");
    repository_.list(query.access, query.limit, query.cursor, std::move(done));
}
void GarmentQueryService::handle(const ResolveGarmentsQuery &query, ResolveCompletion done) const
{
    std::set<std::string> unique;
    bool valid = !query.ids.empty() && query.ids.size() <= 100;
    for (auto id : query.ids)
    {
        std::transform(id.begin(), id.end(), id.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        valid = valid && isUuid(id) && unique.insert(id).second;
    }
    if (!valid)
        throw std::invalid_argument("ids debe contener entre 1 y 100 UUID únicos.");
    repository_.resolve(query.access, query.ids, std::move(done));
}
void GarmentImageService::handle(const GarmentImageAccessQuery &query,
                                 ImageAccessCompletion done) const
{
    requireUuid(query.id, "id");
    repository_.imageReference(
        query.access, query.id,
        [this, access = query.access, done = std::move(done)](ImageReference reference) {
            if (reference.status != 200)
            {
                done({reference.status, "", "", "", "", reference.error});
                return;
            }
            signer_.sign(access, reference.path, kLinkSeconds,
                         [reference, done](SignResult signed_) {
                             if (signed_.status != 200)
                             {
                                 done({signed_.status, "", "", "", "", signed_.error});
                                 return;
                             }
                             ImageAccess result{200,
                                                reference.imageId,
                                                signed_.url,
                                                utcAfter(kLinkSeconds),
                                                reference.mimeType,
                                                "",
                                                reference.version};
                             done(std::move(result));
                         });
        });
}
} // namespace mirage::garment
