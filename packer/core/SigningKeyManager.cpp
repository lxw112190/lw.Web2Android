#include "core/SigningKeyManager.h"

#include "core/Json.h"
#include "core/ProjectValidator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <ncrypt.h>
#include <wincrypt.h>
#endif

namespace lw::web2android {
namespace {

constexpr int kRsaBits = 3072;

std::runtime_error WindowsFailure(const std::string& operation) {
#ifdef _WIN32
    return std::runtime_error(operation + " failed with Windows error " + std::to_string(GetLastError()));
#else
    return std::runtime_error(operation + " is available on Windows only");
#endif
}

#ifdef _WIN32
std::runtime_error SecurityFailure(const std::string& operation, SECURITY_STATUS status) {
    return std::runtime_error(operation + " failed with security status " + std::to_string(status));
}
#endif

std::vector<std::uint8_t> ReadBinary(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Unable to open signing file: " + file.u8string());
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("Unable to determine signing file size: " + file.u8string());
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!result.empty()) input.read(reinterpret_cast<char*>(result.data()), size);
    if (!input) throw std::runtime_error("Unable to read signing file: " + file.u8string());
    return result;
}

std::string ReadText(const std::filesystem::path& file) {
    const auto bytes = ReadBinary(file);
    return std::string(bytes.begin(), bytes.end());
}

void WriteBinary(const std::filesystem::path& file, const std::vector<std::uint8_t>& content) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to create signing file: " + file.u8string());
    if (!content.empty()) {
        output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
    }
    output.flush();
    if (!output) throw std::runtime_error("Unable to write signing file: " + file.u8string());
}

void WriteText(const std::filesystem::path& file, const std::string& content) {
    WriteBinary(file, std::vector<std::uint8_t>(content.begin(), content.end()));
}

std::filesystem::path TemporarySibling(const std::filesystem::path& file) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return file.parent_path() /
           std::filesystem::u8path(file.filename().u8string() + ".tmp-" + std::to_string(nonce));
}

void PublishBinary(const std::filesystem::path& file, const std::vector<std::uint8_t>& content) {
    const auto temporary = TemporarySibling(file);
    try {
        WriteBinary(temporary, content);
        std::filesystem::rename(temporary, file);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void PublishText(const std::filesystem::path& file, const std::string& content) {
    PublishBinary(file, std::vector<std::uint8_t>(content.begin(), content.end()));
}

std::string CurrentUtcTimestamp() {
#ifdef _WIN32
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << time.wYear << '-' << std::setw(2) << time.wMonth << '-'
           << std::setw(2) << time.wDay << 'T' << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
           << ':' << std::setw(2) << time.wSecond << 'Z';
    return output.str();
#else
    throw WindowsFailure("GetSystemTime");
#endif
}

std::string LowerHex(const std::uint8_t* bytes, std::size_t size) {
    constexpr std::array<char, 16> digits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                              '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result;
    result.reserve(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
        result.push_back(digits[(bytes[index] >> 4U) & 0x0fU]);
        result.push_back(digits[bytes[index] & 0x0fU]);
    }
    return result;
}

#ifdef _WIN32

DWORD CheckedDword(std::size_t size, const char* label) {
    if (size > std::numeric_limits<DWORD>::max()) {
        throw std::runtime_error(std::string(label) + " exceeds Windows API limits");
    }
    return static_cast<DWORD>(size);
}

class StorageProviderHandle {
public:
    ~StorageProviderHandle() {
        if (value != 0) NCryptFreeObject(value);
    }
    NCRYPT_PROV_HANDLE value = 0;
};

class KeyHandle {
public:
    KeyHandle() = default;
    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;
    KeyHandle(KeyHandle&& other) noexcept : value(other.value), deleteOnDestroy(other.deleteOnDestroy) {
        other.value = 0;
        other.deleteOnDestroy = false;
    }
    KeyHandle& operator=(KeyHandle&& other) noexcept {
        if (this != &other) {
            Release();
            value = other.value;
            deleteOnDestroy = other.deleteOnDestroy;
            other.value = 0;
            other.deleteOnDestroy = false;
        }
        return *this;
    }
    ~KeyHandle() { Release(); }
    void Delete() {
        if (value == 0 || !deleteOnDestroy) return;
        const auto handle = value;
        value = 0;
        deleteOnDestroy = false;
        const auto status = NCryptDeleteKey(handle, NCRYPT_SILENT_FLAG);
        if (status != ERROR_SUCCESS) {
            NCryptFreeObject(handle);
            throw SecurityFailure("NCryptDeleteKey(temporary export key)", status);
        }
    }
    void Release() noexcept {
        if (value != 0) {
            if (!deleteOnDestroy || NCryptDeleteKey(value, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS) {
                NCryptFreeObject(value);
            }
        }
        value = 0;
        deleteOnDestroy = false;
    }
    NCRYPT_KEY_HANDLE value = 0;
    bool deleteOnDestroy = false;
};

class CertificateHandle {
public:
    CertificateHandle() = default;
    CertificateHandle(const CertificateHandle&) = delete;
    CertificateHandle& operator=(const CertificateHandle&) = delete;
    CertificateHandle(CertificateHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    CertificateHandle& operator=(CertificateHandle&& other) noexcept {
        if (this != &other) {
            if (value != nullptr) CertFreeCertificateContext(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~CertificateHandle() {
        if (value != nullptr) CertFreeCertificateContext(value);
    }
    PCCERT_CONTEXT value = nullptr;
};

class CertificateStoreHandle {
public:
    ~CertificateStoreHandle() {
        if (value != nullptr) CertCloseStore(value, 0);
    }
    HCERTSTORE value = nullptr;
};

class SensitiveBuffer {
public:
    explicit SensitiveBuffer(std::size_t size) : bytes(size) {}
    ~SensitiveBuffer() {
        if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
    }
    std::vector<std::uint8_t> bytes;
};

class MutexLock {
public:
    explicit MutexLock(const std::string& packageName) {
        std::wstring name = L"Local\\lw.Web2Android.Signing.";
        name.append(packageName.begin(), packageName.end());
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (handle_ == nullptr) throw WindowsFailure("CreateMutexW");
        const auto wait = WaitForSingleObject(handle_, 30'000U);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("Timed out waiting for the package signing identity lock");
        }
        locked_ = true;
    }
    ~MutexLock() {
        if (locked_) ReleaseMutex(handle_);
        if (handle_ != nullptr) CloseHandle(handle_);
    }

private:
    HANDLE handle_ = nullptr;
    bool locked_ = false;
};

std::vector<std::uint8_t> ProtectKey(const std::vector<std::uint8_t>& privateKey,
                                     const std::string& packageName) {
    DATA_BLOB input{CheckedDword(privateKey.size(), "Private key"),
                    const_cast<BYTE*>(reinterpret_cast<const BYTE*>(privateKey.data()))};
    DATA_BLOB entropy{CheckedDword(packageName.size(), "Package name"),
                      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(packageName.data()))};
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"lw.Web2Android package signing key", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
        throw WindowsFailure("CryptProtectData");
    }
    std::vector<std::uint8_t> result(encrypted.pbData, encrypted.pbData + encrypted.cbData);
    SecureZeroMemory(encrypted.pbData, encrypted.cbData);
    LocalFree(encrypted.pbData);
    return result;
}

std::vector<std::uint8_t> UnprotectKey(const std::vector<std::uint8_t>& encryptedKey,
                                       const std::string& packageName) {
    DATA_BLOB input{CheckedDword(encryptedKey.size(), "Encrypted key"),
                    const_cast<BYTE*>(reinterpret_cast<const BYTE*>(encryptedKey.data()))};
    DATA_BLOB entropy{CheckedDword(packageName.size(), "Package name"),
                      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(packageName.data()))};
    DATA_BLOB decrypted{};
    LPWSTR description = nullptr;
    if (!CryptUnprotectData(&input, &description, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &decrypted)) {
        throw WindowsFailure("CryptUnprotectData");
    }
    if (description != nullptr) LocalFree(description);
    std::vector<std::uint8_t> result(decrypted.pbData, decrypted.pbData + decrypted.cbData);
    SecureZeroMemory(decrypted.pbData, decrypted.cbData);
    LocalFree(decrypted.pbData);
    return result;
}

std::vector<std::uint8_t> ExportPkcs8(NCRYPT_KEY_HANDLE key) {
    DWORD size = 0;
    auto status = NCryptExportKey(key, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, nullptr, nullptr, 0, &size, 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptExportKey(size)", status);
    std::vector<std::uint8_t> result(size);
    status = NCryptExportKey(key, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, nullptr, result.data(), size, &size, 0);
    if (status != ERROR_SUCCESS) {
        SecureZeroMemory(result.data(), result.size());
        throw SecurityFailure("NCryptExportKey", status);
    }
    result.resize(size);
    return result;
}

std::wstring TemporaryKeyName() {
    std::array<std::uint8_t, 16> random{};
    const auto status = BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) throw SecurityFailure("BCryptGenRandom", status);
    const auto suffix = LowerHex(random.data(), random.size());
    return L"lw.Web2Android.PfxExport." + std::wstring(suffix.begin(), suffix.end());
}

KeyHandle ImportPkcs8(std::vector<std::uint8_t>& privateKey, const std::wstring& keyName) {
    DWORD privateInfoSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_PRIVATE_KEY_INFO,
                             privateKey.data(), CheckedDword(privateKey.size(), "Private key"), 0, nullptr,
                             nullptr, &privateInfoSize)) {
        throw WindowsFailure("CryptDecodeObjectEx(PKCS#8 size)");
    }
    SensitiveBuffer privateInfoBuffer(privateInfoSize);
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_PRIVATE_KEY_INFO,
                             privateKey.data(), CheckedDword(privateKey.size(), "Private key"), 0, nullptr,
                             privateInfoBuffer.bytes.data(), &privateInfoSize)) {
        throw WindowsFailure("CryptDecodeObjectEx(PKCS#8)");
    }
    const auto* privateInfo = reinterpret_cast<const CRYPT_PRIVATE_KEY_INFO*>(privateInfoBuffer.bytes.data());
    if (privateInfo->Algorithm.pszObjId == nullptr ||
        std::string(privateInfo->Algorithm.pszObjId) != szOID_RSA_RSA) {
        throw std::runtime_error("Signing identity PKCS#8 does not contain an RSA key");
    }

    StorageProviderHandle provider;
    auto status = NCryptOpenStorageProvider(&provider.value, MS_KEY_STORAGE_PROVIDER, 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptOpenStorageProvider", status);
    NCryptBuffer nameBuffer{};
    nameBuffer.BufferType = NCRYPTBUFFER_PKCS_KEY_NAME;
    nameBuffer.pvBuffer = const_cast<wchar_t*>(keyName.c_str());
    nameBuffer.cbBuffer = CheckedDword((keyName.size() + 1U) * sizeof(wchar_t), "Temporary key name");
    NCryptBufferDesc parameters{};
    parameters.ulVersion = NCRYPTBUFFER_VERSION;
    parameters.cBuffers = 1;
    parameters.pBuffers = &nameBuffer;
    KeyHandle key;
    status = NCryptImportKey(provider.value, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, &parameters, &key.value,
                             privateKey.data(), CheckedDword(privateKey.size(), "Private key"),
                             NCRYPT_DO_NOT_FINALIZE_FLAG);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptImportKey(PKCS#8)", status);
    key.deleteOnDestroy = true;
    DWORD exportPolicy = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    status = NCryptSetProperty(key.value, NCRYPT_EXPORT_POLICY_PROPERTY,
                              reinterpret_cast<PBYTE>(&exportPolicy), sizeof(exportPolicy), NCRYPT_PERSIST_FLAG);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptSetProperty(export policy)", status);
    status = NCryptFinalizeKey(key.value, NCRYPT_SILENT_FLAG);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptFinalizeKey(imported key)", status);
    return key;
}

std::vector<std::uint8_t> EncodeSubject(const std::string& packageName) {
    const std::wstring subject = L"CN=" + std::wstring(packageName.begin(), packageName.end()) +
                                 L", O=lw.Web2Android, C=CN";
    DWORD size = 0;
    if (!CertStrToNameW(X509_ASN_ENCODING, subject.c_str(), CERT_X500_NAME_STR, nullptr, nullptr, &size, nullptr)) {
        throw WindowsFailure("CertStrToNameW(size)");
    }
    std::vector<std::uint8_t> encoded(size);
    if (!CertStrToNameW(X509_ASN_ENCODING, subject.c_str(), CERT_X500_NAME_STR, nullptr, encoded.data(), &size,
                        nullptr)) {
        throw WindowsFailure("CertStrToNameW");
    }
    encoded.resize(size);
    return encoded;
}

CertificateHandle CreateCertificate(NCRYPT_KEY_HANDLE key, const std::string& packageName) {
    auto encodedSubject = EncodeSubject(packageName);
    CERT_NAME_BLOB subject{CheckedDword(encodedSubject.size(), "Certificate subject"), encodedSubject.data()};
    CRYPT_ALGORITHM_IDENTIFIER signature{};
    signature.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    SYSTEMTIME start{};
    GetSystemTime(&start);
    SYSTEMTIME end = start;
    end.wYear = static_cast<WORD>(end.wYear + 30U);
    FILETIME validityCheck{};
    if (!SystemTimeToFileTime(&end, &validityCheck)) {
        end.wDay = 28U;
    }
    CertificateHandle certificate;
    certificate.value = CertCreateSelfSignCertificate(
        static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(key), &subject, CERT_CREATE_SELFSIGN_NO_KEY_INFO, nullptr,
        &signature, &start, &end, nullptr);
    if (certificate.value == nullptr) throw WindowsFailure("CertCreateSelfSignCertificate");
    return certificate;
}

std::string CertificatePem(const CERT_CONTEXT& certificate) {
    DWORD size = 0;
    if (!CryptBinaryToStringA(certificate.pbCertEncoded, certificate.cbCertEncoded, CRYPT_STRING_BASE64HEADER,
                              nullptr, &size)) {
        throw WindowsFailure("CryptBinaryToStringA(size)");
    }
    std::string result(size, '\0');
    if (!CryptBinaryToStringA(certificate.pbCertEncoded, certificate.cbCertEncoded, CRYPT_STRING_BASE64HEADER,
                              result.data(), &size)) {
        throw WindowsFailure("CryptBinaryToStringA");
    }
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

std::vector<std::uint8_t> DecodeCertificate(const std::string& pem) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(pem.data(), CheckedDword(pem.size(), "Certificate PEM"), CRYPT_STRING_BASE64HEADER,
                              nullptr, &size, nullptr, nullptr)) {
        throw WindowsFailure("CryptStringToBinaryA(size)");
    }
    std::vector<std::uint8_t> result(size);
    if (!CryptStringToBinaryA(pem.data(), CheckedDword(pem.size(), "Certificate PEM"), CRYPT_STRING_BASE64HEADER,
                              result.data(), &size, nullptr, nullptr)) {
        throw WindowsFailure("CryptStringToBinaryA");
    }
    result.resize(size);
    return result;
}

std::string CertificateSha256(const std::uint8_t* certificate, DWORD size) {
    std::array<std::uint8_t, 32> digest{};
    DWORD digestSize = static_cast<DWORD>(digest.size());
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, certificate, size, digest.data(), &digestSize)) {
        throw WindowsFailure("CryptHashCertificate2");
    }
    return LowerHex(digest.data(), digestSize);
}

struct GeneratedIdentity {
    std::vector<std::uint8_t> encryptedPrivateKey;
    std::string certificatePem;
    std::string certificateSha256;
};

GeneratedIdentity GenerateIdentity(const std::string& packageName) {
    StorageProviderHandle provider;
    auto status = NCryptOpenStorageProvider(&provider.value, MS_KEY_STORAGE_PROVIDER, 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptOpenStorageProvider", status);
    KeyHandle key;
    status = NCryptCreatePersistedKey(provider.value, &key.value, NCRYPT_RSA_ALGORITHM, nullptr, 0, 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptCreatePersistedKey", status);
    DWORD keyBits = kRsaBits;
    status = NCryptSetProperty(key.value, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&keyBits), sizeof(keyBits), 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptSetProperty(length)", status);
    DWORD exportPolicy = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    status = NCryptSetProperty(key.value, NCRYPT_EXPORT_POLICY_PROPERTY, reinterpret_cast<PBYTE>(&exportPolicy),
                              sizeof(exportPolicy), 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptSetProperty(export policy)", status);
    status = NCryptFinalizeKey(key.value, 0);
    if (status != ERROR_SUCCESS) throw SecurityFailure("NCryptFinalizeKey", status);
    auto privateKey = ExportPkcs8(key.value);
    GeneratedIdentity result;
    try {
        auto certificate = CreateCertificate(key.value, packageName);
        result.encryptedPrivateKey = ProtectKey(privateKey, packageName);
        result.certificatePem = CertificatePem(*certificate.value);
        result.certificateSha256 =
            CertificateSha256(certificate.value->pbCertEncoded, certificate.value->cbCertEncoded);
    } catch (...) {
        SecureZeroMemory(privateKey.data(), privateKey.size());
        throw;
    }
    SecureZeroMemory(privateKey.data(), privateKey.size());
    return result;
}

#endif

SigningIdentity LoadIdentity(const std::filesystem::path& directory, const std::string& packageName) {
    SigningIdentity identity;
    identity.packageName = packageName;
    identity.directory = directory;
    identity.encryptedKeyFile = directory / "signing.key.lw";
    identity.certificateFile = directory / "certificate.pem";
    identity.metadataFile = directory / "metadata.json";
    const auto metadata = JsonObject::Parse(ReadText(identity.metadataFile));
    if (metadata.RequiredInteger("schemaVersion") != 1 || metadata.RequiredString("algorithm") != "RSA" ||
        metadata.RequiredInteger("keySize") != kRsaBits) {
        throw std::runtime_error("Signing identity metadata uses an unsupported schema or algorithm");
    }
    if (metadata.RequiredString("packageName") != packageName) {
        throw std::runtime_error("Signing identity metadata package does not match its directory");
    }
    identity.certificateSha256 = metadata.RequiredString("certificateSha256");
    identity.createdAtUtc = metadata.RequiredString("createdAtUtc");
#ifdef _WIN32
    const auto certificate = DecodeCertificate(ReadText(identity.certificateFile));
    const auto actualHash = CertificateSha256(certificate.data(), CheckedDword(certificate.size(), "Certificate"));
    if (actualHash != identity.certificateSha256) {
        throw std::runtime_error("Signing certificate SHA-256 does not match metadata");
    }
#endif
    return identity;
}

}  // namespace

SigningKeyManager::SigningKeyManager(std::filesystem::path keysRoot)
    : keysRoot_(keysRoot.empty() ? DefaultKeysRoot() : std::filesystem::absolute(keysRoot).lexically_normal()) {}

std::filesystem::path SigningKeyManager::DefaultKeysRoot() {
#ifdef _WIN32
    wchar_t* value = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&value, &size, L"LOCALAPPDATA") != 0 || value == nullptr) {
        throw std::runtime_error("LOCALAPPDATA is unavailable; pass --keys-dir explicitly");
    }
    const std::filesystem::path root = std::filesystem::path(value) / L"lw.Web2Android" / L"keys";
    std::free(value);
    return root;
#else
    throw WindowsFailure("DPAPI signing identity storage");
#endif
}

SigningIdentity SigningKeyManager::Resolve(const std::string& packageName) const {
#ifdef _WIN32
    ProjectValidator::ValidatePackageName(packageName);
    MutexLock lock(packageName);
    const auto directory = keysRoot_ / std::filesystem::u8path(packageName);
    const auto keyFile = directory / "signing.key.lw";
    const auto certificateFile = directory / "certificate.pem";
    const auto metadataFile = directory / "metadata.json";
    const bool hasKey = std::filesystem::is_regular_file(keyFile);
    const bool hasCertificate = std::filesystem::is_regular_file(certificateFile);
    const bool hasMetadata = std::filesystem::is_regular_file(metadataFile);
    if (hasKey || hasCertificate || hasMetadata) {
        if (!hasKey || !hasCertificate || !hasMetadata) {
            throw std::runtime_error("Signing identity is incomplete; restore it from backup: " + directory.u8string());
        }
        return LoadIdentity(directory, packageName);
    }

    std::filesystem::create_directories(directory);
    const auto generated = GenerateIdentity(packageName);
    const auto createdAt = CurrentUtcTimestamp();
    const std::string metadata =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"packageName\": \"" + EscapeJson(packageName) + "\",\n"
        "  \"algorithm\": \"RSA\",\n"
        "  \"keySize\": " + std::to_string(kRsaBits) + ",\n"
        "  \"certificateSha256\": \"" + generated.certificateSha256 + "\",\n"
        "  \"createdAtUtc\": \"" + createdAt + "\"\n"
        "}\n";
    try {
        PublishBinary(keyFile, generated.encryptedPrivateKey);
        PublishText(certificateFile, generated.certificatePem);
        PublishText(metadataFile, metadata);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(keyFile, ignored);
        std::filesystem::remove(certificateFile, ignored);
        std::filesystem::remove(metadataFile, ignored);
        throw;
    }
    auto identity = LoadIdentity(directory, packageName);
    identity.newlyCreated = true;
    return identity;
#else
    (void)packageName;
    throw WindowsFailure("SigningKeyManager::Resolve");
#endif
}

SigningIdentity SigningKeyManager::Load(const std::string& packageName) const {
#ifdef _WIN32
    ProjectValidator::ValidatePackageName(packageName);
    MutexLock lock(packageName);
    const auto directory = keysRoot_ / std::filesystem::u8path(packageName);
    const auto keyFile = directory / "signing.key.lw";
    const auto certificateFile = directory / "certificate.pem";
    const auto metadataFile = directory / "metadata.json";
    const bool hasKey = std::filesystem::is_regular_file(keyFile);
    const bool hasCertificate = std::filesystem::is_regular_file(certificateFile);
    const bool hasMetadata = std::filesystem::is_regular_file(metadataFile);
    if (!hasKey && !hasCertificate && !hasMetadata) {
        throw std::runtime_error("Signing identity does not exist: " + directory.u8string());
    }
    if (!hasKey || !hasCertificate || !hasMetadata) {
        throw std::runtime_error("Signing identity is incomplete; restore it from backup: " + directory.u8string());
    }
    return LoadIdentity(directory, packageName);
#else
    (void)packageName;
    throw WindowsFailure("SigningKeyManager::Load");
#endif
}

void SigningKeyManager::ExportPkcs12(const SigningIdentity& identity,
                                     const std::filesystem::path& destination,
                                     const std::wstring& password) const {
#ifdef _WIN32
    if (password.empty()) throw std::runtime_error("PKCS#12 backup password must not be empty");
    if (std::filesystem::exists(destination)) {
        throw std::runtime_error("Refusing to overwrite an existing PKCS#12 backup: " + destination.u8string());
    }
    if (!destination.parent_path().empty() && !std::filesystem::is_directory(destination.parent_path())) {
        throw std::runtime_error("PKCS#12 destination directory does not exist: " +
                                 destination.parent_path().u8string());
    }

    auto encrypted = ReadBinary(identity.encryptedKeyFile);
    auto privateKey = UnprotectKey(encrypted, identity.packageName);
    const auto keyName = TemporaryKeyName();
    KeyHandle key;
    try {
        key = ImportPkcs8(privateKey, keyName);
    } catch (...) {
        SecureZeroMemory(privateKey.data(), privateKey.size());
        throw;
    }
    SecureZeroMemory(privateKey.data(), privateKey.size());

    std::vector<std::uint8_t> content;
    {
        const auto certificateDer = DecodeCertificate(ReadText(identity.certificateFile));
        CertificateHandle certificate;
        certificate.value = CertCreateCertificateContext(
            X509_ASN_ENCODING, certificateDer.data(), CheckedDword(certificateDer.size(), "Certificate"));
        if (certificate.value == nullptr) throw WindowsFailure("CertCreateCertificateContext");

        CertificateStoreHandle store;
        store.value = CertOpenStore(CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
        if (store.value == nullptr) throw WindowsFailure("CertOpenStore(memory)");
        CertificateHandle storedCertificate;
        if (!CertAddCertificateContextToStore(store.value, certificate.value, CERT_STORE_ADD_ALWAYS,
                                              &storedCertificate.value)) {
            throw WindowsFailure("CertAddCertificateContextToStore");
        }
        CRYPT_KEY_PROV_INFO providerInfo{};
        providerInfo.pwszContainerName = const_cast<wchar_t*>(keyName.c_str());
        providerInfo.pwszProvName = const_cast<wchar_t*>(MS_KEY_STORAGE_PROVIDER);
        providerInfo.dwProvType = 0;
        providerInfo.dwKeySpec = CERT_NCRYPT_KEY_SPEC;
        if (!CertSetCertificateContextProperty(storedCertificate.value, CERT_KEY_PROV_INFO_PROP_ID,
                                               CERT_SET_PROPERTY_INHIBIT_PERSIST_FLAG, &providerInfo)) {
            throw WindowsFailure("CertSetCertificateContextProperty(CNG provider info)");
        }
        HCRYPTPROV_OR_NCRYPT_KEY_HANDLE acquiredKey = 0;
        DWORD acquiredKeySpec = 0;
        BOOL callerMustFree = FALSE;
        if (!CryptAcquireCertificatePrivateKey(storedCertificate.value,
                                               CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG,
                                               nullptr, &acquiredKey, &acquiredKeySpec, &callerMustFree)) {
            throw WindowsFailure("CryptAcquireCertificatePrivateKey(export validation)");
        }
        if (callerMustFree) NCryptFreeObject(acquiredKey);
        if (acquiredKeySpec != CERT_NCRYPT_KEY_SPEC) {
            throw std::runtime_error("Temporary export key did not resolve through CNG");
        }

        CRYPT_DATA_BLOB pfx{};
        constexpr DWORD flags = EXPORT_PRIVATE_KEYS | REPORT_NO_PRIVATE_KEY | REPORT_NOT_ABLE_TO_EXPORT_PRIVATE_KEY;
        if (!PFXExportCertStoreEx(store.value, &pfx, password.c_str(), nullptr, flags)) {
            throw WindowsFailure("PFXExportCertStoreEx(size)");
        }
        content.resize(pfx.cbData);
        pfx.pbData = content.data();
        if (!PFXExportCertStoreEx(store.value, &pfx, password.c_str(), nullptr, flags)) {
            throw WindowsFailure("PFXExportCertStoreEx");
        }
        content.resize(pfx.cbData);
    }
    key.Delete();
    PublishBinary(destination, content);
#else
    (void)identity;
    (void)destination;
    (void)password;
    throw WindowsFailure("SigningKeyManager::ExportPkcs12");
#endif
}

void SigningKeyManager::WriteTemporaryPrivateKey(const SigningIdentity& identity,
                                                 const std::filesystem::path& destination) const {
#ifdef _WIN32
    auto encrypted = ReadBinary(identity.encryptedKeyFile);
    auto privateKey = UnprotectKey(encrypted, identity.packageName);
    try {
        WriteBinary(destination, privateKey);
    } catch (...) {
        SecureZeroMemory(privateKey.data(), privateKey.size());
        SecureDeleteFile(destination);
        throw;
    }
    SecureZeroMemory(privateKey.data(), privateKey.size());
#else
    (void)identity;
    (void)destination;
    throw WindowsFailure("SigningKeyManager::WriteTemporaryPrivateKey");
#endif
}

void SecureDeleteFile(const std::filesystem::path& file) noexcept {
    try {
        if (!std::filesystem::is_regular_file(file)) return;
        const auto size = std::filesystem::file_size(file);
        std::ofstream output(file, std::ios::binary | std::ios::in | std::ios::out);
        std::array<char, 4096> zeros{};
        std::uintmax_t remaining = size;
        while (remaining > 0U) {
            const auto count = static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining, zeros.size()));
            output.write(zeros.data(), count);
            remaining -= static_cast<std::uintmax_t>(count);
        }
        output.flush();
        output.close();
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    }
}

}  // namespace lw::web2android
