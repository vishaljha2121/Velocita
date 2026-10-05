#include "velocita/v2/replication.hpp"
#include "v2_identity_test_support.hpp"

#include <iostream>
#include <fstream>
#include <map>
#include <optional>
#include <type_traits>
#include <unistd.h>

namespace {
using namespace velocita::v2;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "v2-kernel-startup.XXXXXX").string();
        pattern.push_back('\0');
        const auto* created = ::mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
using Image = std::map<std::string, std::string>;
Image image(const std::filesystem::path& path) {
    Image result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        const auto name = entry.path().lexically_relative(path).string();
        if (entry.is_directory()) { result.emplace(name + "/", ""); continue; }
        std::ifstream input(entry.path(), std::ios::binary);
        result.emplace(name, std::string(std::istreambuf_iterator<char>(input), {}));
    }
    return result;
}
template<class F> std::optional<IdentityStartupErrorCode> refusal(F&& operation) {
    try { operation(); }
    catch (const IdentityStartupError& error) { return error.code(); }
    return {};
}

void api() {
    require(!std::is_constructible_v<ReplicatedKernel, std::int32_t, std::filesystem::path>,
            "raw constructor bypasses verified startup ownership");
    require(!std::is_constructible_v<ReplicatedKernel, std::int32_t, std::filesystem::path,
                std::vector<std::int32_t>, StorageCheckpoints, VerifiedExecutionCatalog,
                Digest256, velocita::MetadataCheckpoints>, "full raw constructor remains available");
    static_assert(!std::is_copy_constructible_v<VerifiedIdentityStartup>);
    static_assert(!std::is_default_constructible_v<VerifiedIdentityStartup>);
}

void owned_lifetime() {
    TempDirectory temp;
    const auto dir = temp.path / "voter";
    Digest256 manifest{};
    std::int64_t term{};
    {
        ReplicatedKernel node(identity_test::bootstrap(dir, 1, {1}));
        const auto expected = identity_test::identity(1, {1});
        require(node.static_identity().value() == expected, "kernel changed verified identity");
        require(node.status().node_id == 1, "kernel did not derive local node ID");
        require(node.static_identity().authority_domain() == identity_test::authority_domain({1}),
                "kernel did not derive authority domain");
        manifest = node.static_identity().manifest_digest();
        node.tick(2000);
        require(node.is_leader(), "one verified voter cannot elect itself");
        term = node.status().term;
        node.start_writer();
        const auto before = image(dir);
        const auto code = refusal([&] { (void)identity_test::recover(dir, 1, {1}); });
        require(image(dir) == before, "ownership contender changed active kernel storage");
        require(code == IdentityStartupErrorCode::directory_owned, "live kernel released its directory lock");
    }
    ReplicatedKernel restarted(identity_test::recover(dir, 1, {1}));
    require(restarted.static_identity().manifest_digest() == manifest, "restart changed cluster manifest");
    require(restarted.status().term == term, "restart lost persisted election term");
}

void three_voters() {
    TempDirectory temp;
    ReplicatedKernel node(identity_test::bootstrap(temp.path / "voter", 2, {1, 2, 3}));
    for (int i = 0; i != 8; ++i) node.tick(2000);
    require(!node.is_leader(), "verified three-voter membership collapsed into self quorum");
    require(node.status().node_id == 2, "kernel ignored verified local node ID");
}

void moved_token() {
    TempDirectory temp;
    const auto dir = temp.path / "voter";
    auto source = identity_test::bootstrap(dir, 1, {1});
    auto holder = std::move(source);
    const auto before = image(dir);
    const auto code = refusal([&] { ReplicatedKernel invalid(std::move(source)); });
    require(image(dir) == before, "moved token opened writable storage");
    require(code == IdentityStartupErrorCode::invalid_identity, "moved token was not refused at ownership boundary");
    require(holder.owns_directory(), "refused moved token invalidated the real owner");
    ReplicatedKernel valid(std::move(holder));
}

void stale_path() {
    TempDirectory temp;
    const auto dir = temp.path / "voter";
    auto startup = identity_test::bootstrap(dir, 1, {1});
    std::filesystem::rename(dir, temp.path / "detached");
    std::filesystem::create_directory(dir);
    const auto before = image(temp.path);
    const auto code = refusal([&] { ReplicatedKernel invalid(std::move(startup)); });
    require(image(temp.path) == before, "stale token opened writable storage in replacement directory");
    require(code == IdentityStartupErrorCode::invalid_file, "stale path was not refused at ownership boundary");
}

void changed_identity() {
    TempDirectory temp;
    const auto dir = temp.path / "voter";
    auto startup = identity_test::bootstrap(dir, 1, {1});
    std::ofstream(dir / kStaticIdentityFileName, std::ios::binary | std::ios::trunc) << "corruption";
    const auto before = image(dir);
    const auto code = refusal([&] { ReplicatedKernel invalid(std::move(startup)); });
    require(image(dir) == before, "changed identity opened writable storage");
    require(code == IdentityStartupErrorCode::invalid_file, "changed identity was not revalidated");
}

void constructor_unwind() {
    TempDirectory temp;
    const auto dir = temp.path / "voter";
    auto startup = identity_test::bootstrap(dir, 1, {1});
    const auto healthy = image(dir);
    const auto metadata_path = identity_test::runtime_directory(dir) /
        velocita::kV2MetadataFileName;
    std::ofstream(metadata_path, std::ios::binary) << "corruption";
    const auto before = image(dir);
    bool metadata_refused = false;
    try { ReplicatedKernel invalid(std::move(startup)); }
    catch (const IdentityStartupError&) { throw; }
    catch (const BoundMetadataError& error) {
        metadata_refused = error.frame_error() == BoundFrameError::truncated;
    }
    require(metadata_refused, "constructor fixture did not reach corrupt metadata");
    require(image(dir) == before, "failed constructor changed corrupt history");
    std::ofstream(metadata_path, std::ios::binary | std::ios::trunc)
        << healthy.at(metadata_path.lexically_relative(dir).string());
    auto next_owner = identity_test::recover(dir, 1, {1});
    require(next_owner.owns_directory(), "constructor unwind leaked directory ownership");
}
}

int main(int argc, char** argv) {
    try {
        const auto run = [](const std::string& name) {
            if (name == "api") api();
            else if (name == "lifetime") owned_lifetime();
            else if (name == "three-voters") three_voters();
            else if (name == "moved-token") moved_token();
            else if (name == "stale-path") stale_path();
            else if (name == "changed-identity") changed_identity();
            else if (name == "unwind") constructor_unwind();
            else throw std::runtime_error("unknown kernel startup case");
        };
        if (argc == 2) run(argv[1]);
        else for (const auto* name : {"api", "lifetime", "three-voters", "moved-token",
                                      "stale-path", "changed-identity", "unwind"}) run(name);
        std::cout << "kernel startup tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "kernel startup test failed: " << error.what() << '\n';
        return 1;
    }
}
