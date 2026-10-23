// Qualification only: one authenticated full-state read, never an order or authority request.
// Link against the same committed snapshot schema as the intended voters. No compatibility fallback.
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef VELOCITA_AUTH_READ_ARGUMENTS_ONLY
#include "velocita/v2/aeron_node_transport.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/node_files.hpp"
#include <chrono>
#include <sys/file.h>
#include <sys/random.h>
#include <thread>
#endif

namespace {
struct Arguments {
    std::string role, incarnation, catalog_root, authority_domain;
    std::filesystem::path key, session, catalog, aeron;
    unsigned response_port{};
    bool check{};
    unsigned uid() const { return role == "gateway" ? 29001 : 29002; }
    int key_id() const { return role == "gateway" ? 13 : 14; }
    unsigned caller() const { return role == "gateway" ? 22 : 23; }
};

bool nonzero_hex(const std::string& value, std::size_t length) {
    return value.size() == length && value.find_first_not_of("0123456789abcdef") == std::string::npos &&
           value.find_first_not_of('0') != std::string::npos;
}

Arguments parse(int argc, char** argv) {
    const std::set<std::string> required{"--role", "--incarnation", "--catalog-root", "--authority-domain",
        "--response-port", "--key-file", "--session-file", "--catalog", "--aeron-dir"};
    std::map<std::string, std::string> values;
    bool mode = false, check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--check-config" || option == "--run") {
            if (mode) throw std::runtime_error("exactly one execution mode required");
            mode = true; check = option == "--check-config"; continue;
        }
        if (!required.contains(option) || i + 1 == argc || values.contains(option))
            throw std::runtime_error("unknown, duplicate or missing option");
        values.emplace(option, argv[++i]);
    }
    if (!mode || values.size() != required.size()) throw std::runtime_error("mode and every pin required");
    Arguments a;
    a.role = values.at("--role");
    a.incarnation = values.at("--incarnation");
    a.catalog_root = values.at("--catalog-root");
    a.authority_domain = values.at("--authority-domain");
    a.key = values.at("--key-file"); a.session = values.at("--session-file");
    a.catalog = values.at("--catalog"); a.aeron = values.at("--aeron-dir"); a.check = check;
    if ((a.role != "gateway" && a.role != "signer") || !nonzero_hex(a.incarnation, 32) ||
        !nonzero_hex(a.catalog_root, 64) || !nonzero_hex(a.authority_domain, 64))
        throw std::runtime_error("role or cryptographic pins refused");
    const auto& port = values.at("--response-port");
    const auto number = std::from_chars(port.data(), port.data() + port.size(), a.response_port);
    if (number.ec != std::errc{} || number.ptr != port.data() + port.size() ||
        a.response_port != (a.role == "gateway" ? 44413U : 44414U))
        throw std::runtime_error("response port refused");
    std::set<std::filesystem::path> paths;
    for (const auto& path : {a.key, a.session, a.catalog, a.aeron}) {
        if (!path.is_absolute() || path.lexically_normal() != path || !paths.insert(path).second)
            throw std::runtime_error("absolute distinct normalized paths required");
    }
    return a;
}

class ParentDirectory {
    int fd_{-1};
public:
    const std::string leaf;
    ParentDirectory(const std::filesystem::path& path, unsigned owner) : leaf(path.filename().string()) {
        fd_ = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd_ < 0) throw std::runtime_error("root directory unavailable");
        try {
            for (const auto& component : path.parent_path().relative_path()) {
                const int child = ::openat(fd_, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (child < 0) throw std::runtime_error("parent traversal refused");
                ::close(fd_); fd_ = child;
                struct stat info{};
                if (::fstat(fd_, &info) != 0 || (info.st_uid != 0 && info.st_uid != owner) ||
                    (info.st_mode & 0022) != 0)
                    throw std::runtime_error("foreign or writable ancestor refused");
            }
        } catch (...) { ::close(fd_); fd_ = -1; throw; }
    }
    ~ParentDirectory() { if (fd_ >= 0) ::close(fd_); }
    ParentDirectory(const ParentDirectory&) = delete;
    ParentDirectory& operator=(const ParentDirectory&) = delete;
    int fd() const { return fd_; }
    std::filesystem::path anchored_path() const { return "/proc/self/fd/" + std::to_string(fd_) + "/" + leaf; }
    void private_leaf(unsigned owner, const std::string& suffix = "", bool optional = false) const {
        const int input = ::openat(fd_, (leaf + suffix).c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        if (input < 0) {
            if (optional && errno == ENOENT) return;
            throw std::runtime_error("private file open refused");
        }
        struct stat info{};
        const bool okay = ::fstat(input, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == owner &&
            (info.st_mode & 07777) == 0600 && info.st_nlink == 1;
        ::close(input);
        if (!okay) throw std::runtime_error("private file permissions or links refused");
    }
    void absent_temporary() const {
        struct stat info{};
        if (::fstatat(fd_, (leaf + ".tmp").c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
            throw std::runtime_error("existing session temporary file requires review");
    }
};

#ifndef VELOCITA_AUTH_READ_ARGUMENTS_ONLY
using namespace velocita::v2;
static_assert(kStateSnapshotSchemaVersion == 12, "re-review the probe when snapshot schema changes");

Tick now_ms() {
    return static_cast<Tick>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct SessionLock {
    int fd{-1};
    explicit SessionLock(const std::filesystem::path& session) {
        const auto path = session.string() + ".read-probe.lock";
        fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        struct stat info{};
        if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_uid != ::geteuid() || (info.st_mode & 0777) != 0600 || info.st_nlink != 1 ||
            ::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            if (fd >= 0) ::close(fd);
            throw std::runtime_error("exclusive private probe lock refused");
        }
    }
    ~SessionLock() { if (fd >= 0) ::close(fd); }
    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;
};

int run(const Arguments& a) {
    if (::getuid() != a.uid() || ::geteuid() != a.uid() || ::getgid() != a.uid() || ::getegid() != a.uid())
        throw std::runtime_error("wrong production service identity");
    const auto expected_root = *parse_digest256(a.catalog_root);
    const auto expected_domain = *parse_digest256(a.authority_domain);
    ParentDirectory catalog_path(a.catalog, 0);
    ParentDirectory key_path(a.key, a.uid());
    ParentDirectory session_path(a.session, a.uid());
    ParentDirectory aeron_path(a.aeron, a.uid());
    session_path.private_leaf(a.uid());
    session_path.absent_temporary();
    key_path.private_leaf(a.uid());
    key_path.private_leaf(a.uid(), ".next", true);
    key_path.private_leaf(a.uid(), ".previous", true);
    const auto catalog = load_catalog_file(catalog_path.anchored_path(), expected_root);
    if (catalog.error != NodeFileError::none) throw std::runtime_error("pinned catalog refused");
    SessionLock lock(session_path.anchored_path());
    auto secrets = load_secret_set(key_path.anchored_path());
    if (!secrets.secrets) throw std::runtime_error("private caller key refused");
    const auto epoch = advance_session_epoch_file(session_path.anchored_path());
    if (epoch.error != SessionEpochError::none) throw std::runtime_error("existing session epoch refused");
    std::array<std::byte, 16> incarnation{}, process{};
    for (std::size_t i = 0; i < incarnation.size(); ++i) {
        unsigned value{};
        const auto* first = a.incarnation.data() + i * 2;
        (void)std::from_chars(first, first + 2, value, 16);
        incarnation[i] = static_cast<std::byte>(value);
    }
    if (::getentropy(process.data(), process.size()) != 0) throw std::runtime_error("process entropy refused");
    std::vector<AuthSecret> keys;
    keys.push_back(std::move(secrets.secrets->current));
    if (secrets.secrets->next) keys.push_back(std::move(*secrets.secrets->next));
    if (secrets.secrets->previous) keys.push_back(std::move(*secrets.secrets->previous));
    const std::map<std::int32_t, Endpoint> nodes{{1, {"100.72.127.102", 44301}},
        {2, {"100.84.151.116", 44302}}, {3, {"100.85.36.22", 44303}}};
    std::vector<ClientRoute> routes;
    for (const auto& [id, endpoint] : nodes) {
        (void)endpoint;
        auto ingress = AuthKey::derive(keys.front(), {AuthRole::ingress, a.key_id(), id}, incarnation);
        if (!ingress) throw std::runtime_error("caller ingress derivation refused");
        std::vector<AuthKey> responses;
        for (const auto& key : keys) {
            auto response = AuthKey::derive(key, {AuthRole::response, id, a.key_id()}, incarnation);
            if (!response) throw std::runtime_error("caller response derivation refused");
            responses.push_back(std::move(*response));
        }
        routes.push_back(ClientRoute{id, std::move(*ingress), std::move(responses)});
    }
    if (::mkdirat(aeron_path.fd(), aeron_path.leaf.c_str(), 0700) != 0)
        throw std::runtime_error("fresh private Aeron directory required");
    velocita::AeronRuntime aeron(aeron_path.anchored_path().string());
    AeronClientTransport transport(aeron, nodes, {"100.72.127.102", static_cast<std::uint16_t>(a.response_port)});
    GatewayLink link(epoch.epoch, process, std::move(routes), transport);
    if (!link.request_state(now_ms(), 12000)) throw std::runtime_error("state request refused");
    LinkOutcome result;
    do {
        result = link.pump(now_ms());
        if (result.status == LinkStatus::pending) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (result.status == LinkStatus::pending);
    if (result.status != LinkStatus::done || result.request != LinkRequest::state)
        throw std::runtime_error("authenticated full-state read unavailable or schema refused");
    const auto& reading = result.reading;
    const auto& state = link.state();
    if (!nodes.contains(reading.responder_id) || reading.responder_id != reading.leader_id ||
        reading.term <= 0 || reading.applied_index <= 0 || reading.authority_domain != expected_domain ||
        state.execution_catalog_root != expected_root || reading.state_digest.is_zero())
        throw std::runtime_error("authenticated state identity or catalog mismatch");
    const auto& counters = link.counters();
    if (counters.unauthentic || counters.fenced || counters.uncorrelated || counters.proofs_refused ||
        transport.dropped_frames()) throw std::runtime_error("unclean authenticated read counters");
    std::cout << "{\"role\":\"" << a.role << "\",\"uid\":" << a.uid()
              << ",\"caller\":" << a.caller() << ",\"key_id\":" << a.key_id()
              << ",\"session_epoch\":" << epoch.epoch
              << ",\"term\":" << reading.term << ",\"applied_index\":" << reading.applied_index
              << ",\"leader\":" << reading.leader_id << ",\"state_version\":" << state.state_version
              << ",\"catalog_root\":\"" << to_hex(expected_root)
              << "\",\"authority_domain\":\"" << to_hex(expected_domain)
              << "\",\"state_digest\":\"" << to_hex(reading.state_digest)
              << "\",\"authenticated_voter_read\":true,\"read_only\":true,\"live_admitted\":false}\n";
    return 0;
}
#endif
}

int main(int argc, char** argv) {
    try {
        const auto a = parse(argc, argv);
        if (a.check) {
            std::cout << "CONFIG_OK role=" << a.role << " uid=" << a.uid() << " caller=" << a.caller()
                      << " key_id=" << a.key_id() << " authenticated_read=0 live_admitted=0\n";
            return 0;
        }
#ifdef VELOCITA_AUTH_READ_ARGUMENTS_ONLY
        throw std::runtime_error("parser-only build cannot access credentials or transport");
#else
        return run(a);
#endif
    } catch (const std::exception& e) {
        std::cerr << "AUTHENTICATED_READ_REFUSED " << e.what() << '\n';
        return 1;
    }
}
