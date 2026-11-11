#include "../garment/application/GarmentServices.h"
#include <iostream>
using namespace mirage::garment;
void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Test failed");
}
template <class Action> void rejects(Action action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected);
}
const std::string kId = "d8e548df-feb8-4c82-a7fa-d81b88430bcd";
const std::string kImage = "ff94c59f-92ba-4050-b380-f91649d8f85d";
GarmentDetails jacket()
{
    GarmentDetails d;
    d.name = "Mi casaca denim";
    d.category = Category::Outerwear;
    d.subcategory = "DENIM_JACKET";
    d.colors = {"BLUE"};
    d.styles = {"CASUAL"};
    d.description = "Casaca azul denim, manga larga, sin estampado.";
    return d;
}
class FakeRepository final : public GarmentRepository
{
  public:
    mutable int calls = 0;
    mutable std::vector<std::string> resolved;
    mutable ImageReference reference{200, kImage, "user/garment/image", "image/jpeg", "", 1};
    void create(const GarmentAccess &, const Garment &g, GarmentCompletion done) const override
    {
        ++calls;
        done({200, g, ""});
    }
    void update(const GarmentAccess &, const std::string &id, const GarmentDetails &d,
                GarmentCompletion done) const override
    {
        ++calls;
        done({200, Garment(id, d, std::nullopt, 2, "2026-11-10T15:30:00Z"), ""});
    }
    void remove(const GarmentAccess &, const std::string &, StatusCompletion done) const override
    {
        ++calls;
        done({200, ""});
    }
    void setImage(const GarmentAccess &, const std::string &id, const std::string &imageId,
                  GarmentCompletion done) const override
    {
        ++calls;
        done({200, Garment(id, jacket(), GarmentImage{imageId, 1}, 3, "2026-11-10T15:30:00Z"), ""});
    }
    mutable bool missing = false;
    void findById(const GarmentAccess &, const std::string &id,
                  GarmentCompletion done) const override
    {
        ++calls;
        if (missing)
            done({404, std::nullopt, "GARMENT_NOT_FOUND"});
        else
            done({200, Garment(id, jacket(), std::nullopt, 1, "2026-11-10T15:30:00Z"), ""});
    }
    void list(const GarmentAccess &, int, const std::optional<std::string> &,
              PageCompletion done) const override
    {
        ++calls;
        done({200, {}, std::nullopt, ""});
    }
    void resolve(const GarmentAccess &, const std::vector<std::string> &ids,
                 ResolveCompletion done) const override
    {
        ++calls;
        resolved = ids;
        done({200, {}, ""});
    }
    void imageReference(const GarmentAccess &, const std::string &,
                        ImageReferenceCompletion done) const override
    {
        ++calls;
        done(reference);
    }
};
class FakeSigner final : public ImageSigner
{
  public:
    mutable std::string signedPath, uploadPath;
    void signUpload(const GarmentAccess &, const std::string &path,
                    SignCompletion done) const override
    {
        uploadPath = path;
        done({200, "https://storage.example.com/upload/x?token=t", ""});
    }
    void sign(const GarmentAccess &, const std::string &path, int seconds,
              SignCompletion done) const override
    {
        require(seconds == 300);
        signedPath = path;
        done({200, "https://storage.example.com/signed/x?token=t", ""});
    }
};
int main()
{
    // Vocabulary and identifiers.
    require(parseCategory("ONE_PIECE") == Category::OnePiece);
    rejects([] { parseCategory("SHOES"); });
    rejects([] { parseAvailability("MAYBE"); });
    require(isUuid(kId) && !isUuid("not-a-uuid") && !isUuid(kId + "0"));

    // Attribute rules.
    validate(jacket());
    auto bad = jacket();
    bad.name = "   ";
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.name = std::string(121, 'a');
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.name = std::string(120, 'a');
    validate(bad); // exactly the limit is accepted
    bad = jacket();
    bad.description = std::string(1001, 'a');
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.colors = {"BLUE", "BLUE"};
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.colors = {"TEAL"};
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.styles = {"GOTHIC"};
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.subcategory = "denim jacket";
    rejects([&] { validate(bad); });
    bad = jacket();
    bad.colors.clear(); // unknown colours and styles are allowed to be empty
    bad.styles.clear();
    bad.subcategory.reset();
    validate(bad);

    // A rejected update leaves the aggregate unchanged.
    Garment garment(kId, jacket(), std::nullopt, 1, "2026-11-10T15:30:00Z");
    bad = jacket();
    bad.name = "";
    rejects([&] { garment.update(bad); });
    require(garment.details().name == "Mi casaca denim");

    FakeRepository repository;
    FakeSigner signer;
    GarmentCommandService commands(repository);
    GarmentQueryService queries(repository);
    GarmentImageService images(repository, signer);
    const std::string kUser = "22222222-2222-2222-2222-222222222222";
    GarmentAccess access{"Bearer token", kUser};

    // Invalid input never reaches the repository.
    rejects([&] { commands.handle(CreateGarmentCommand{access, "bad", jacket()}, {}); });
    bad = jacket();
    bad.category = Category::Top;
    bad.colors = {"BLUE", "BLUE"};
    rejects([&] { commands.handle(CreateGarmentCommand{access, kId, bad}, {}); });
    rejects([&] { commands.handle(UpdateGarmentCommand{access, "bad", jacket()}, {}); });
    rejects([&] { commands.handle(DeleteGarmentCommand{access, "bad"}, {}); });
    rejects([&] { commands.handle(SetGarmentImageCommand{access, kId, "bad"}, {}); });
    rejects([&] { queries.handle(GetGarmentQuery{access, "bad"}, {}); });
    rejects([&] { queries.handle(ListGarmentsQuery{access, 0, std::nullopt}, {}); });
    rejects([&] { queries.handle(ListGarmentsQuery{access, 101, std::nullopt}, {}); });
    rejects([&] { queries.handle(ListGarmentsQuery{access, 50, std::string("bad")}, {}); });
    rejects([&] { queries.handle(ResolveGarmentsQuery{access, {}}, {}); });
    rejects([&] { queries.handle(ResolveGarmentsQuery{access, {kId, kId}}, {}); });
    rejects([&] { queries.handle(ResolveGarmentsQuery{access, {kId, "bad"}}, {}); });
    std::vector<std::string> many;
    for (int i = 0; i < 101; ++i)
        many.push_back("00000000-0000-0000-0000-" + std::string(12 - std::to_string(i).size(), '0') +
                       std::to_string(i));
    rejects([&] { queries.handle(ResolveGarmentsQuery{access, many}, {}); });
    require(repository.calls == 0);

    // Valid requests reach the repository.
    int status = 0;
    commands.handle(CreateGarmentCommand{access, kId, jacket()},
                    [&](GarmentResult r) { status = r.status; });
    require(status == 200 && repository.calls == 1);
    many.pop_back();
    queries.handle(ResolveGarmentsQuery{access, many}, [&](ResolveResult r) { status = r.status; });
    require(repository.resolved.size() == 100); // the exact limit is accepted
    queries.handle(ListGarmentsQuery{access, 100, std::string(kId)},
                   [&](GarmentPage r) { status = r.status; });
    require(status == 200);

    // Image access: signs the stored path and reports the contract fields.
    ImageAccess result{};
    images.handle(GarmentImageAccessQuery{access, kId}, [&](ImageAccess r) { result = r; });
    require(result.status == 200 && result.imageId == kImage && result.version == 1);
    require(result.mimeType == "image/jpeg" && signer.signedPath == "user/garment/image");
    require(result.url.rfind("https://", 0) == 0);
    require(result.expiresAt.size() == 20 && result.expiresAt.back() == 'Z');

    // A garment without an image does not call the signer.
    repository.reference = {404, "", "", "", "IMAGE_NOT_FOUND", 0};
    signer.signedPath.clear();
    images.handle(GarmentImageAccessQuery{access, kId}, [&](ImageAccess r) { result = r; });
    require(result.status == 404 && result.error == "IMAGE_NOT_FOUND" && signer.signedPath.empty());
    rejects([&] { images.handle(GarmentImageAccessQuery{access, "bad"}, {}); });

    // Upload URL: the object path is {verified user}/{garment}/{image}, never client-chosen.
    ImageUpload upload{};
    images.handle(GarmentImageUploadQuery{access, kId, kImage}, [&](ImageUpload r) { upload = r; });
    require(upload.status == 200 && upload.imageId == kImage && upload.url.rfind("https://", 0) == 0);
    require(signer.uploadPath == kUser + "/" + kId + "/" + kImage);
    // A garment that is not the caller's (or does not exist) never gets an upload URL.
    signer.uploadPath.clear();
    repository.missing = true;
    images.handle(GarmentImageUploadQuery{access, kId, kImage}, [&](ImageUpload r) { upload = r; });
    require(upload.status == 404 && upload.error == "GARMENT_NOT_FOUND" && signer.uploadPath.empty());
    rejects([&] { images.handle(GarmentImageUploadQuery{access, "bad", kImage}, {}); });
    rejects([&] { images.handle(GarmentImageUploadQuery{access, kId, "bad"}, {}); });

    std::cout << "garment tests passed\n";
}
