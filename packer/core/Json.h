#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lw::web2android {

struct JsonValue {
    enum class Type { String, Integer, Boolean, Object, Array, Null };

    Type type = Type::Null;
    std::string stringValue;
    std::int64_t integerValue = 0;
    bool booleanValue = false;
    std::map<std::string, JsonValue> objectValue;
    std::vector<JsonValue> arrayValue;
};

class JsonObject {
public:
    static JsonObject Parse(const std::string& json);

    bool Contains(const std::string& key) const;
    std::string RequiredString(const std::string& key) const;
    std::string OptionalString(const std::string& key, const std::string& fallback) const;
    std::int64_t RequiredInteger(const std::string& key) const;
    std::int64_t OptionalInteger(const std::string& key, std::int64_t fallback) const;
    bool OptionalBoolean(const std::string& key, bool fallback) const;
    JsonObject RequiredObject(const std::string& key) const;
    std::vector<std::string> OptionalStringArray(
        const std::string& key, const std::vector<std::string>& fallback = {}) const;

private:
    JsonObject() = default;
    explicit JsonObject(std::map<std::string, JsonValue> values);
    std::map<std::string, JsonValue> values_;
};

std::string EscapeJson(const std::string& value);
std::string EscapeXml(const std::string& value);

}  // namespace lw::web2android
