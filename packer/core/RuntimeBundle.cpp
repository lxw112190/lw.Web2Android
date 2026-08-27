#include "core/RuntimeBundle.h"

#include "core/Hash.h"
#include "core/Json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace lw::web2android {
namespace {

std::string ReadBinaryText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to read Runtime file: " + file.u8string());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool IsSha256(const std::string& value) {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

std::vector<std::filesystem::path> FindDexFiles(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error(
            "Runtime Bundle directory does not exist; use the Runtime from the current release: " +
            directory.u8string());
    }

    const std::regex namePattern("classes([2-9][0-9]*)?\\.dex");
    std::vector<std::pair<int, std::filesystem::path>> indexed;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file()) continue;
        const auto name = item.path().filename().u8string();
        std::smatch match;
        if (!std::regex_match(name, match, namePattern)) continue;
        const int index = match[1].matched ? std::stoi(match[1].str()) : 1;
        indexed.emplace_back(index, item.path());
    }
    std::sort(indexed.begin(), indexed.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    if (indexed.empty() || indexed.front().first != 1) {
        throw std::runtime_error("Runtime Bundle is missing classes.dex; replace toolchain/runtime with the current release");
    }

    std::vector<std::filesystem::path> result;
    for (std::size_t position = 0; position < indexed.size(); ++position) {
        const int expected = position == 0 ? 1 : static_cast<int>(position + 1U);
        if (indexed[position].first != expected) {
            throw std::runtime_error("Runtime DEX numbering must be contiguous");
        }
        const auto bytes = ReadBinaryText(indexed[position].second);
        if (bytes.size() < 8U || bytes.compare(0, 4, "dex\n") != 0) {
            throw std::runtime_error("Runtime file is not a DEX: " + indexed[position].second.u8string());
        }
        const std::vector<std::string> forbidden = {
            "Lcom/lw/web2android/runtime/R;", "Lcom/lw/web2android/runtime/R$",
            "Lcom/lw/web2android/runtime/BuildConfig;"};
        for (const auto& descriptor : forbidden) {
            if (bytes.find(descriptor) != std::string::npos) {
                throw std::runtime_error("Runtime DEX references a generated application class: " + descriptor);
            }
        }
        result.push_back(indexed[position].second);
    }
    return result;
}

}  // namespace

std::vector<std::filesystem::path> ValidateRuntimeBundle(
    const std::filesystem::path& runtimeDirectory,
    const std::string& expectedRuntimeVersion) {
    const auto dexFiles = FindDexFiles(runtimeDirectory);
    const auto metadataFile = runtimeDirectory / "metadata.json";
    if (!std::filesystem::is_regular_file(metadataFile)) {
        throw std::runtime_error(
            "Runtime metadata was not found; replace toolchain/runtime with the current release: " +
            metadataFile.u8string());
    }

    const auto metadata = JsonObject::Parse(ReadBinaryText(metadataFile));
    if (metadata.RequiredInteger("schemaVersion") != 1) {
        throw std::runtime_error("Runtime metadata schemaVersion is unsupported");
    }
    if (metadata.RequiredString("runtimeVersion") != expectedRuntimeVersion) {
        throw std::runtime_error("Runtime version does not match toolchain.lock.json; replace toolchain/runtime with the current release");
    }
    if (!metadata.Contains("configSchemaVersion")) {
        throw std::runtime_error(
            "Runtime is incompatible with this Packer: config schemaVersion " +
            std::to_string(kRuntimeConfigSchemaVersion) +
            " metadata is missing; replace toolchain/runtime with the current release");
    }
    if (metadata.RequiredInteger("configSchemaVersion") != kRuntimeConfigSchemaVersion) {
        throw std::runtime_error(
            "Runtime is incompatible with this Packer: config schemaVersion " +
            std::to_string(kRuntimeConfigSchemaVersion) +
            " is required; replace toolchain/runtime with the current release");
    }
    if (metadata.RequiredString("namespace") != "com.lw.web2android.runtime") {
        throw std::runtime_error("Runtime metadata contains an unexpected namespace");
    }
    if (!metadata.Contains("dexFiles")) {
        throw std::runtime_error(
            "Runtime DEX integrity metadata is missing; replace toolchain/runtime with the current release");
    }

    std::map<std::string, JsonObject> declaredDex;
    for (const auto& entry : metadata.RequiredObjectArray("dexFiles")) {
        const auto name = entry.RequiredString("name");
        if (std::filesystem::path(name).filename().u8string() != name ||
            !declaredDex.emplace(name, entry).second) {
            throw std::runtime_error("Runtime metadata contains an invalid or duplicate DEX name: " + name);
        }
    }
    if (declaredDex.size() != dexFiles.size()) {
        throw std::runtime_error("Runtime DEX file count does not match metadata; the Runtime Bundle may be mixed or damaged");
    }

    for (const auto& dex : dexFiles) {
        const auto name = dex.filename().u8string();
        const auto declared = declaredDex.find(name);
        if (declared == declaredDex.end()) {
            throw std::runtime_error("Runtime metadata does not declare DEX file: " + name);
        }
        const auto expectedSize = declared->second.RequiredInteger("size");
        const auto actualSize = static_cast<std::int64_t>(std::filesystem::file_size(dex));
        if (expectedSize != actualSize) {
            throw std::runtime_error("Runtime DEX size mismatch; the Runtime Bundle may be mixed or damaged: " + name);
        }
        const auto expectedHash = Lower(declared->second.RequiredString("sha256"));
        if (!IsSha256(expectedHash) || Sha256File(dex) != expectedHash) {
            throw std::runtime_error("Runtime DEX SHA-256 mismatch; the Runtime Bundle may be mixed or damaged: " + name);
        }
    }
    return dexFiles;
}

}  // namespace lw::web2android
