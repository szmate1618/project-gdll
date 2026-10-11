#include "file_fingerprint.hpp"
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace viewer {
std::string fileSha256(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open fingerprinted file '" + path.string() + "'");
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot initialize SHA-256 for '" + path.string() + "'");
    std::array<char, 65536> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (stream.gcount() > 0 && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(stream.gcount())) != 1)
            throw std::runtime_error("Cannot hash file '" + path.string() + "'");
    }
    if (!stream.eof()) throw std::runtime_error("Cannot finish reading fingerprinted file '" + path.string() + "'");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &size) != 1 || size != 32)
        throw std::runtime_error("Cannot finalize SHA-256 for '" + path.string() + "'");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned int i = 0; i < size; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 15]);
    }
    return result;
}

}
