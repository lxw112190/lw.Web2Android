#pragma once

#include <filesystem>
#include <string>

namespace lw::web2android {

struct SigningIdentity {
    std::string packageName;
    std::string certificateSha256;
    std::string createdAtUtc;
    std::filesystem::path directory;
    std::filesystem::path encryptedKeyFile;
    std::filesystem::path certificateFile;
    std::filesystem::path metadataFile;
    bool newlyCreated = false;
};

class SigningKeyManager {
public:
    explicit SigningKeyManager(std::filesystem::path keysRoot = {});

    static std::filesystem::path DefaultKeysRoot();
    SigningIdentity Resolve(const std::string& packageName) const;
    void WriteTemporaryPrivateKey(const SigningIdentity& identity,
                                  const std::filesystem::path& destination) const;

private:
    std::filesystem::path keysRoot_;
};

void SecureDeleteFile(const std::filesystem::path& file) noexcept;

}  // namespace lw::web2android
