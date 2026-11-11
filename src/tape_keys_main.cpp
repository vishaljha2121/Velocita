// velocita-tape-keys: creates the tape master secret and the voters' Ed25519 head-signing keys.
#include "velocita/tape/config.hpp"
#include "velocita/tape/signing.hpp"

#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/random.h>
#include <unistd.h>

using namespace velocita::tape;

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    try {
        if (mode == "master" && argc == 3) {
            const std::filesystem::path out = argv[2];
            if (std::filesystem::exists(out)) throw std::runtime_error("refusing to overwrite " + out.string());
            unsigned char key[32];
            if (::getentropy(key, sizeof(key)) != 0) throw std::runtime_error("entropy unavailable");
            const int fd = ::open(out.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0 || ::write(fd, key, sizeof(key)) != static_cast<ssize_t>(sizeof(key)) || ::fsync(fd) != 0)
                throw std::runtime_error("cannot write " + out.string());
            ::close(fd);
            return 0;
        }
        if (mode == "head" && argc == 4) {
            const std::filesystem::path priv = argv[2], pub = argv[3];
            if (std::filesystem::exists(priv) || std::filesystem::exists(pub))
                throw std::runtime_error("refusing to overwrite an existing key");
            const auto key = SigningKey::generate();
            key.save_private(priv);
            key.save_public(pub);
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "velocita-tape-keys: " << e.what() << "\n";
        return 70;
    }
    std::cerr << "usage: velocita-tape-keys master OUT | head PRIVATE.pem PUBLIC.pem\n";
    return 64;
}
