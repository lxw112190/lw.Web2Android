#include "core/Hash.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#endif

namespace lw::web2android {

std::string Sha256File(const std::filesystem::path& file) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> hashObject;
    try {
        auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0) throw std::runtime_error("BCryptOpenAlgorithmProvider failed: " + std::to_string(status));
        DWORD objectSize = 0;
        DWORD copied = 0;
        status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                   reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &copied, 0);
        if (status < 0) throw std::runtime_error("BCryptGetProperty failed: " + std::to_string(status));
        hashObject.resize(objectSize);
        status = BCryptCreateHash(algorithm, &hash, hashObject.data(), objectSize, nullptr, 0, 0);
        if (status < 0) throw std::runtime_error("BCryptCreateHash failed: " + std::to_string(status));

        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("Unable to open file for SHA-256: " + file.u8string());
        std::array<std::uint8_t, 64 * 1024> buffer{};
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0) {
                status = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0);
                if (status < 0) throw std::runtime_error("BCryptHashData failed: " + std::to_string(status));
            }
        }
        if (!input.eof()) throw std::runtime_error("Unable to read file for SHA-256: " + file.u8string());

        std::array<std::uint8_t, 32> digest{};
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        if (status < 0) throw std::runtime_error("BCryptFinishHash failed: " + std::to_string(status));
        BCryptDestroyHash(hash);
        hash = nullptr;
        BCryptCloseAlgorithmProvider(algorithm, 0);
        algorithm = nullptr;
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
        return output.str();
    } catch (...) {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
#else
    (void)file;
    throw std::runtime_error("SHA-256 file hashing is available on Windows only");
#endif
}

}  // namespace lw::web2android
