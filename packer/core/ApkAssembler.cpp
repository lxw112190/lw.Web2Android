#include "core/ApkAssembler.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace lw::web2android {
namespace {

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50U;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50U;
constexpr std::uint32_t kEndSignature = 0x06054b50U;

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Unable to open file: " + file.u8string());
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("Unable to determine file size: " + file.u8string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("Unable to read file: " + file.u8string());
    return bytes;
}

void WriteBytes(const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to create APK: " + file.u8string());
    if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Unable to write APK: " + file.u8string());
}

std::uint16_t Read16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 2U > bytes.size()) throw std::runtime_error("Truncated ZIP structure");
    return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::uint32_t Read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4U > bytes.size()) throw std::runtime_error("Truncated ZIP structure");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void Append16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

struct ZipDirectory {
    std::size_t endOffset = 0;
    std::uint16_t entryCount = 0;
    std::uint32_t centralSize = 0;
    std::uint32_t centralOffset = 0;
    std::vector<std::uint8_t> comment;
    std::vector<std::string> names;
};

ZipDirectory ParseDirectory(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 22U) throw std::runtime_error("Input is not a ZIP archive");
    const std::size_t lowerBound = bytes.size() > 65'557U ? bytes.size() - 65'557U : 0U;
    std::size_t endOffset = std::numeric_limits<std::size_t>::max();
    for (std::size_t offset = bytes.size() - 22U;; --offset) {
        if (Read32(bytes, offset) == kEndSignature) {
            const auto commentLength = Read16(bytes, offset + 20U);
            if (offset + 22U + commentLength == bytes.size()) {
                endOffset = offset;
                break;
            }
        }
        if (offset == lowerBound) break;
    }
    if (endOffset == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("ZIP end-of-central-directory record was not found");
    }
    if (Read16(bytes, endOffset + 4U) != 0U || Read16(bytes, endOffset + 6U) != 0U) {
        throw std::runtime_error("Multi-disk ZIP archives are not supported");
    }
    const auto entriesOnDisk = Read16(bytes, endOffset + 8U);
    const auto totalEntries = Read16(bytes, endOffset + 10U);
    if (entriesOnDisk == 0xffffU || totalEntries == 0xffffU) throw std::runtime_error("ZIP64 archives are not supported");
    if (entriesOnDisk != totalEntries) throw std::runtime_error("Inconsistent ZIP entry counts");

    ZipDirectory directory;
    directory.endOffset = endOffset;
    directory.entryCount = totalEntries;
    directory.centralSize = Read32(bytes, endOffset + 12U);
    directory.centralOffset = Read32(bytes, endOffset + 16U);
    if (static_cast<std::uint64_t>(directory.centralOffset) + directory.centralSize != endOffset) {
        throw std::runtime_error("Unsupported ZIP layout or corrupt central directory");
    }
    directory.comment.assign(bytes.begin() + static_cast<std::ptrdiff_t>(endOffset + 22U), bytes.end());

    std::size_t cursor = directory.centralOffset;
    for (std::uint16_t index = 0; index < directory.entryCount; ++index) {
        if (Read32(bytes, cursor) != kCentralHeaderSignature || cursor + 46U > endOffset) {
            throw std::runtime_error("Corrupt ZIP central directory entry");
        }
        const auto nameLength = Read16(bytes, cursor + 28U);
        const auto extraLength = Read16(bytes, cursor + 30U);
        const auto commentLength = Read16(bytes, cursor + 32U);
        const auto recordSize = 46U + nameLength + extraLength + commentLength;
        if (cursor + recordSize > endOffset) throw std::runtime_error("Truncated ZIP central directory entry");
        directory.names.emplace_back(reinterpret_cast<const char*>(bytes.data() + cursor + 46U), nameLength);
        cursor += recordSize;
    }
    if (cursor != endOffset) throw std::runtime_error("ZIP central directory size does not match its entries");
    return directory;
}

std::uint32_t CalculateCrc32(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

struct NewEntry {
    std::string name;
    std::vector<std::uint8_t> content;
    std::uint32_t crc = 0;
    std::uint32_t localOffset = 0;
};

void AppendLocalEntry(std::vector<std::uint8_t>& output, NewEntry& entry) {
    if (entry.name.size() > std::numeric_limits<std::uint16_t>::max() ||
        entry.content.size() > std::numeric_limits<std::uint32_t>::max() ||
        output.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Runtime entry exceeds classic ZIP limits");
    }
    entry.crc = CalculateCrc32(entry.content);
    entry.localOffset = static_cast<std::uint32_t>(output.size());
    Append32(output, kLocalHeaderSignature);
    Append16(output, 20U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, entry.crc);
    Append32(output, static_cast<std::uint32_t>(entry.content.size()));
    Append32(output, static_cast<std::uint32_t>(entry.content.size()));
    Append16(output, static_cast<std::uint16_t>(entry.name.size()));
    Append16(output, 0U);
    output.insert(output.end(), entry.name.begin(), entry.name.end());
    output.insert(output.end(), entry.content.begin(), entry.content.end());
}

void AppendCentralEntry(std::vector<std::uint8_t>& output, const NewEntry& entry) {
    Append32(output, kCentralHeaderSignature);
    Append16(output, 20U);
    Append16(output, 20U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, entry.crc);
    Append32(output, static_cast<std::uint32_t>(entry.content.size()));
    Append32(output, static_cast<std::uint32_t>(entry.content.size()));
    Append16(output, static_cast<std::uint16_t>(entry.name.size()));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, entry.localOffset);
    output.insert(output.end(), entry.name.begin(), entry.name.end());
}

}  // namespace

void ApkAssembler::InjectFiles(const std::filesystem::path& resourceApk,
                               const std::vector<std::filesystem::path>& files,
                               const std::filesystem::path& outputApk) {
    if (files.empty()) throw std::runtime_error("No Runtime DEX files were supplied");
    const auto source = ReadBytes(resourceApk);
    const auto directory = ParseDirectory(source);
    if (static_cast<std::size_t>(directory.entryCount) + files.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("APK would exceed classic ZIP entry limits");
    }

    std::vector<NewEntry> additions;
    for (const auto& file : files) {
        const auto name = file.filename().u8string();
        if (std::find(directory.names.begin(), directory.names.end(), name) != directory.names.end()) {
            throw std::runtime_error("APK already contains Runtime entry: " + name);
        }
        if (std::any_of(additions.begin(), additions.end(), [&](const NewEntry& item) { return item.name == name; })) {
            throw std::runtime_error("Duplicate Runtime entry: " + name);
        }
        additions.push_back(NewEntry{name, ReadBytes(file)});
    }

    std::vector<std::uint8_t> output;
    output.reserve(source.size() + 1024U);
    output.insert(output.end(), source.begin(), source.begin() + directory.centralOffset);
    for (auto& addition : additions) AppendLocalEntry(output, addition);
    const auto newCentralOffset = output.size();
    output.insert(output.end(), source.begin() + directory.centralOffset, source.begin() + directory.endOffset);
    for (const auto& addition : additions) AppendCentralEntry(output, addition);
    const auto newCentralSize = output.size() - newCentralOffset;
    if (newCentralOffset > std::numeric_limits<std::uint32_t>::max() ||
        newCentralSize > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("APK would exceed classic ZIP size limits");
    }
    Append32(output, kEndSignature);
    Append16(output, 0U);
    Append16(output, 0U);
    const auto newEntryCount = static_cast<std::uint16_t>(directory.entryCount + additions.size());
    Append16(output, newEntryCount);
    Append16(output, newEntryCount);
    Append32(output, static_cast<std::uint32_t>(newCentralSize));
    Append32(output, static_cast<std::uint32_t>(newCentralOffset));
    Append16(output, static_cast<std::uint16_t>(directory.comment.size()));
    output.insert(output.end(), directory.comment.begin(), directory.comment.end());
    WriteBytes(outputApk, output);
}

std::vector<std::string> ApkAssembler::ListEntries(const std::filesystem::path& archive) {
    return ParseDirectory(ReadBytes(archive)).names;
}

}  // namespace lw::web2android
