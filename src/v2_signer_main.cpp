// velocita-v2-signer: the inspecting signer of ADR 0019. It accepts only a test key; its signatures are valid at no venue.
#include "velocita/v2/signer_service.hpp"

#include <atomic>
#include <charconv>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;

struct Usage : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Arguments {
    std::filesystem::path socket, test_key_file, floor_file, audit_file;
    std::uint64_t allowed_uid{}, caller{}, venue{}, account{}, instrument{}, key_version{1};
    bool have_uid{}, init_floor{};
};

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true); }

std::uint64_t number(const std::string& text, const char* what) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw Usage(std::string(what) + " must be a non-negative integer");
    return value;
}

Arguments parse(const int argc, char** argv) {
    Arguments a;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--init-floor") {
            a.init_floor = true;
            continue;
        }
        if (i + 1 >= argc) throw Usage(key + " needs a value");
        const std::string value = argv[++i];
        if (key == "--socket") a.socket = value;
        else if (key == "--test-key-file") a.test_key_file = value;
        else if (key == "--floor-file") a.floor_file = value;
        else if (key == "--audit-file") a.audit_file = value;
        else if (key == "--allowed-uid") { a.allowed_uid = number(value, "allowed uid"); a.have_uid = true; }
        else if (key == "--caller-id") a.caller = number(value, "caller id");
        else if (key == "--venue-id") a.venue = number(value, "venue id");
        else if (key == "--account-id") a.account = number(value, "account id");
        else if (key == "--instrument-id") a.instrument = number(value, "instrument id");
        else if (key == "--key-version") a.key_version = number(value, "key version");
        else throw Usage("unknown option " + key);
    }
    if (a.socket.empty() || a.test_key_file.empty() || a.floor_file.empty() || a.audit_file.empty() || !a.have_uid ||
        a.caller == 0 || a.venue == 0 || a.account == 0 || a.instrument == 0 || a.key_version == 0 ||
        a.key_version > 0xffffffffULL || a.allowed_uid > 0xffffffffULL)
        throw Usage("needs --socket --allowed-uid --test-key-file --floor-file --audit-file --caller-id "
                    "--venue-id --account-id --instrument-id; only a test key is accepted");
    return a;
}

int run(const Arguments& a) {
    auto key = signer::TestKey::load(a.test_key_file);
    if (!key) throw Usage("the key file is not a velocita test signing key; no other key is accepted");
    if (a.init_floor) {
        const auto created = signer::FloorFile::create(a.floor_file);
        if (created != signer::FloorError::none)
            throw Usage(created == signer::FloorError::already_exists
                            ? "--init-floor never replaces an existing floor file"
                            : "the floor file could not be created");
    }
    signer::FloorFile floor;
    if (const auto opened = signer::FloorFile::open(a.floor_file, floor); opened != signer::FloorError::none)
        throw Usage(opened == signer::FloorError::missing ? "no floor file; the first start needs --init-floor"
                                                          : "the floor file is not readable or not valid");
    signer::AuditFile audit;
    if (!audit.open(a.audit_file)) throw Usage("the audit file could not be opened for appending");
    signer::SignerCore core({CallerId{a.caller}, VenueId{a.venue}, AccountId{a.account}, InstrumentId{a.instrument}},
                            &signer::decode_mock_swap_payload, key->holder(static_cast<std::uint32_t>(a.key_version)),
                            audit.sink(), floor.store(), signer::monotonic_clock(), floor.floor());
    signer::SignerServer server;
    if (!server.listen(a.socket, static_cast<std::uint32_t>(a.allowed_uid)))
        throw Usage("the socket could not be created at " + a.socket.string());
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    std::signal(SIGPIPE, SIG_IGN);
    std::cout << "READY signer test_key_only=1 payloads=mock sequence_floor=" << floor.floor() << " key_version="
              << a.key_version << std::endl;
    while (!g_stop.load()) (void)server.serve_once(core, 200);
    const auto& counters = server.counters();
    std::cout << "STOP served=" << counters.served << " peer_refusals=" << counters.peer_refusals << " malformed="
              << counters.malformed << " io_failures=" << counters.io_failures << " sequence_floor="
              << core.sequence_floor() << std::endl;
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const Usage& error) {
        std::cerr << "velocita-v2-signer: " << error.what() << '\n';
        return 64;
    } catch (const std::exception& error) {
        std::cerr << "velocita-v2-signer: " << error.what() << '\n';
        return 1;
    }
}
