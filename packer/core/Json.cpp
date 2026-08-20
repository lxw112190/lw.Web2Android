#include "core/Json.h"

#include <charconv>
#include <stdexcept>

namespace lw::web2android {
namespace {

void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

class Parser {
public:
    explicit Parser(const std::string& source) : source_(source) {}

    std::map<std::string, JsonValue> ParseObject() {
        SkipWhitespace();
        Expect('{');
        SkipWhitespace();
        std::map<std::string, JsonValue> result;
        if (Consume('}')) {
            EnsureEnd();
            return result;
        }

        while (true) {
            const auto key = ParseString();
            SkipWhitespace();
            Expect(':');
            SkipWhitespace();
            if (!result.emplace(key, ParseValue()).second) {
                Fail("duplicate key: " + key);
            }
            SkipWhitespace();
            if (Consume('}')) {
                EnsureEnd();
                return result;
            }
            Expect(',');
            SkipWhitespace();
        }
    }

private:
    JsonValue ParseValue() {
        if (Peek() == '"') {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.stringValue = ParseString();
            return value;
        }
        if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9')) {
            return ParseInteger();
        }
        if (ConsumeLiteral("true")) {
            JsonValue value;
            value.type = JsonValue::Type::Boolean;
            value.booleanValue = true;
            return value;
        }
        if (ConsumeLiteral("false")) {
            JsonValue value;
            value.type = JsonValue::Type::Boolean;
            value.booleanValue = false;
            return value;
        }
        if (ConsumeLiteral("null")) {
            return JsonValue{};
        }
        Fail("expected a string, integer, boolean, or null");
    }

    JsonValue ParseInteger() {
        const auto begin = position_;
        if (Peek() == '-') {
            ++position_;
        }
        if (Peek() < '0' || Peek() > '9') {
            Fail("invalid integer");
        }
        while (Peek() >= '0' && Peek() <= '9') {
            ++position_;
        }
        std::int64_t parsed = 0;
        const auto result = std::from_chars(source_.data() + begin, source_.data() + position_, parsed);
        if (result.ec != std::errc{}) {
            Fail("integer is outside the supported range");
        }
        JsonValue value;
        value.type = JsonValue::Type::Integer;
        value.integerValue = parsed;
        return value;
    }

    std::string ParseString() {
        Expect('"');
        std::string result;
        while (position_ < source_.size()) {
            const char current = source_[position_++];
            if (current == '"') {
                return result;
            }
            if (static_cast<unsigned char>(current) < 0x20U) {
                Fail("unescaped control character in string");
            }
            if (current != '\\') {
                result.push_back(current);
                continue;
            }
            if (position_ >= source_.size()) {
                Fail("unfinished escape sequence");
            }
            const char escaped = source_[position_++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': AppendUtf8(result, ParseUnicodeCodePoint()); break;
                default: Fail("unsupported escape sequence");
            }
        }
        Fail("unterminated string");
    }

    std::uint32_t ParseUnicodeCodePoint() {
        const auto first = ParseHexQuad();
        if (first < 0xd800U || first > 0xdbffU) {
            if (first >= 0xdc00U && first <= 0xdfffU) {
                Fail("unexpected low surrogate");
            }
            return first;
        }
        if (!Consume('\\') || !Consume('u')) {
            Fail("high surrogate is missing a low surrogate");
        }
        const auto second = ParseHexQuad();
        if (second < 0xdc00U || second > 0xdfffU) {
            Fail("invalid low surrogate");
        }
        return 0x10000U + ((first - 0xd800U) << 10U) + (second - 0xdc00U);
    }

    std::uint32_t ParseHexQuad() {
        if (position_ + 4U > source_.size()) {
            Fail("incomplete unicode escape");
        }
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = source_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9') value |= static_cast<std::uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f') value |= static_cast<std::uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') value |= static_cast<std::uint32_t>(character - 'A' + 10);
            else Fail("invalid unicode escape");
        }
        return value;
    }

    void SkipWhitespace() {
        while (position_ < source_.size()) {
            const char current = source_[position_];
            if (current != ' ' && current != '\t' && current != '\r' && current != '\n') break;
            ++position_;
        }
    }

    char Peek() const {
        return position_ < source_.size() ? source_[position_] : '\0';
    }

    bool Consume(char expected) {
        if (Peek() != expected) return false;
        ++position_;
        return true;
    }

    bool ConsumeLiteral(const char* literal) {
        const std::string text(literal);
        if (source_.compare(position_, text.size(), text) != 0) return false;
        position_ += text.size();
        return true;
    }

    void Expect(char expected) {
        if (!Consume(expected)) {
            Fail(std::string("expected '") + expected + "'");
        }
    }

    void EnsureEnd() {
        SkipWhitespace();
        if (position_ != source_.size()) {
            Fail("unexpected trailing content");
        }
    }

    [[noreturn]] void Fail(const std::string& message) const {
        throw std::runtime_error("Invalid JSON at byte " + std::to_string(position_) + ": " + message);
    }

    const std::string& source_;
    std::size_t position_ = 0;
};

const JsonValue& RequireValue(const std::map<std::string, JsonValue>& values, const std::string& key) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        throw std::runtime_error("Missing required JSON property: " + key);
    }
    return iterator->second;
}

}  // namespace

JsonObject JsonObject::Parse(const std::string& json) {
    JsonObject object;
    object.values_ = Parser(json).ParseObject();
    return object;
}

bool JsonObject::Contains(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::string JsonObject::RequiredString(const std::string& key) const {
    const auto& value = RequireValue(values_, key);
    if (value.type != JsonValue::Type::String) throw std::runtime_error("JSON property must be a string: " + key);
    return value.stringValue;
}

std::string JsonObject::OptionalString(const std::string& key, const std::string& fallback) const {
    if (!Contains(key)) return fallback;
    return RequiredString(key);
}

std::int64_t JsonObject::RequiredInteger(const std::string& key) const {
    const auto& value = RequireValue(values_, key);
    if (value.type != JsonValue::Type::Integer) throw std::runtime_error("JSON property must be an integer: " + key);
    return value.integerValue;
}

std::int64_t JsonObject::OptionalInteger(const std::string& key, std::int64_t fallback) const {
    if (!Contains(key)) return fallback;
    return RequiredInteger(key);
}

bool JsonObject::OptionalBoolean(const std::string& key, bool fallback) const {
    if (!Contains(key)) return fallback;
    const auto& value = RequireValue(values_, key);
    if (value.type != JsonValue::Type::Boolean) throw std::runtime_error("JSON property must be a boolean: " + key);
    return value.booleanValue;
}

std::string EscapeJson(const std::string& value) {
    std::string output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20U) {
                    const char hex[] = "0123456789abcdef";
                    output += "\\u00";
                    output.push_back(hex[(character >> 4U) & 0x0fU]);
                    output.push_back(hex[character & 0x0fU]);
                } else {
                    output.push_back(static_cast<char>(character));
                }
        }
    }
    return output;
}

std::string EscapeXml(const std::string& value) {
    std::string output;
    for (const char character : value) {
        switch (character) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default: output.push_back(character);
        }
    }
    return output;
}

}  // namespace lw::web2android
