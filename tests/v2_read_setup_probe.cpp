// Offline deployment helper: canonical identity review and fresh private session creation.
#include "velocita/v2/node_config.hpp"
#include "velocita/v2/session_fence.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    using namespace velocita::v2;
    if (argc != 3) return 64;
    const std::string mode=argv[1];
    if (mode == "--identity") {
        const auto parsed=load_node_config(argv[2]);
        if (!parsed.config) return 65;
        std::array<std::byte,kMaxStaticIdentityBytes> bytes{};
        const auto encoded=encode_static_identity(parsed.config->static_identity(),bytes);
        VerifiedStaticIdentity identity;
        if (encoded.error != StaticIdentityCodecError::none ||
            VerifiedStaticIdentity::decode(std::span(bytes).first(encoded.bytes_written),identity) != StaticIdentityCodecError::none) return 66;
        std::cout << "{\"node\":" << parsed.config->node_id << ",\"authority_domain\":\""
                  << to_hex(identity.authority_domain()) << "\",\"manifest_digest\":\""
                  << to_hex(identity.manifest_digest()) << "\"}\n";
        return 0;
    }
    if (mode == "--create-session") {
        const auto uid=geteuid();
        if ((uid!=29001 && uid!=29002) || getuid()!=uid || getgid()!=uid || getegid()!=uid) return 67;
        const std::string root=uid==29001?"/var/lib/velocita-swap-gateway":"/var/lib/velocita-swap-signer";
        if (argv[2]!=root+"/client.session") return 68;
        struct stat s{};
        if (lstat(root.c_str(),&s)!=0 || !S_ISDIR(s.st_mode) || s.st_uid!=uid || s.st_gid!=uid || (s.st_mode&07777)!=0700) return 69;
        umask(0077);
        const auto result=create_session_epoch_file(argv[2]);
        if (result.error!=SessionEpochError::none || result.epoch!=1) return 70;
        std::cout << "session_created epoch=1 uid=" << uid << '\n';return 0;
    }
    return 64;
}
