#include "velocita/tape/signing.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>

#include <cstdio>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace velocita::tape {
namespace {

EVP_PKEY* as_key(void* p) { return static_cast<EVP_PKEY*>(p); }

std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
    std::FILE* f = std::fopen(path.c_str(), mode);
    if (!f) throw std::runtime_error("cannot open key file " + path.string());
    return f;
}

std::FILE* create_file(const std::filesystem::path& path, mode_t mode) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0) throw std::runtime_error("cannot create key file " + path.string());
    std::FILE* f = ::fdopen(fd, "wb");
    if (!f) {
        ::close(fd);
        throw std::runtime_error("cannot create key file " + path.string());
    }
    return f;
}

void finish(std::FILE* f, bool ok, const char* what) {
    ok = ok && std::fflush(f) == 0 && ::fsync(::fileno(f)) == 0;
    ok = std::fclose(f) == 0 && ok;
    if (!ok) throw std::runtime_error(what);
}

}

SigningKey SigningKey::load(const std::filesystem::path& pem) {
    std::FILE* f = open_file(pem, "rb");
    EVP_PKEY* key = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
    std::fclose(f);
    if (!key || EVP_PKEY_get_base_id(key) != EVP_PKEY_ED25519) {
        EVP_PKEY_free(key);
        throw std::runtime_error("not an Ed25519 private key: " + pem.string());
    }
    return SigningKey(key);
}

SigningKey SigningKey::generate() {
    EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
    if (!key) throw std::runtime_error("Ed25519 key generation failed");
    return SigningKey(key);
}

SigningKey::~SigningKey() { EVP_PKEY_free(as_key(key_)); }
SigningKey::SigningKey(SigningKey&& o) noexcept : key_(o.key_) { o.key_ = nullptr; }
SigningKey& SigningKey::operator=(SigningKey&& o) noexcept {
    std::swap(key_, o.key_);
    return *this;
}

std::array<std::byte, kSignatureBytes> SigningKey::sign(std::span<const std::byte> message) const {
    std::array<std::byte, kSignatureBytes> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::size_t length = out.size();
    const bool ok = ctx && EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, as_key(key_)) == 1 &&
                    EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(out.data()), &length,
                                   reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1 &&
                    length == out.size();
    EVP_MD_CTX_free(ctx);
    if (!ok) throw std::runtime_error("Ed25519 signing failed");
    return out;
}

void SigningKey::save_private(const std::filesystem::path& pem) const {
    std::FILE* f = create_file(pem, 0600);
    finish(f, PEM_write_PrivateKey(f, as_key(key_), nullptr, nullptr, 0, nullptr, nullptr) == 1, "cannot write private key");
}

void SigningKey::save_public(const std::filesystem::path& pem) const {
    std::FILE* f = create_file(pem, 0644);
    finish(f, PEM_write_PUBKEY(f, as_key(key_)) == 1, "cannot write public key");
}

VerifyKey VerifyKey::load(const std::filesystem::path& pem) {
    std::FILE* f = open_file(pem, "rb");
    EVP_PKEY* key = PEM_read_PUBKEY(f, nullptr, nullptr, nullptr);
    std::fclose(f);
    if (!key || EVP_PKEY_get_base_id(key) != EVP_PKEY_ED25519) {
        EVP_PKEY_free(key);
        throw std::runtime_error("not an Ed25519 public key: " + pem.string());
    }
    return VerifyKey(key);
}

VerifyKey::~VerifyKey() { EVP_PKEY_free(as_key(key_)); }
VerifyKey::VerifyKey(VerifyKey&& o) noexcept : key_(o.key_) { o.key_ = nullptr; }
VerifyKey& VerifyKey::operator=(VerifyKey&& o) noexcept {
    std::swap(key_, o.key_);
    return *this;
}

bool VerifyKey::verify(std::span<const std::byte> message, std::span<const std::byte> signature) const {
    if (signature.size() != kSignatureBytes) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, as_key(key_)) == 1 &&
                    EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
                                     reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
    EVP_MD_CTX_free(ctx);
    return ok;
}

}
