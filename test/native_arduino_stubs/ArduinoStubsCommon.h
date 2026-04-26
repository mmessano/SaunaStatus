#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

class String {
public:
    String() = default;
    String(const char *value) : value_(value ? value : "") {}
    String(const std::string &value) : value_(value) {}

    size_t length() const { return value_.size(); }
    bool startsWith(const char *prefix) const {
        if (!prefix) return false;
        size_t prefixLen = std::strlen(prefix);
        return value_.size() >= prefixLen &&
               value_.compare(0, prefixLen, prefix) == 0;
    }
    String substring(size_t start) const {
        if (start >= value_.size()) return String("");
        return String(value_.substr(start));
    }
    long toInt() const { return std::strtol(value_.c_str(), nullptr, 10); }
    float toFloat() const { return std::strtof(value_.c_str(), nullptr); }
    void toCharArray(char *buffer, size_t len) const {
        if (!buffer || len == 0) return;
        std::snprintf(buffer, len, "%s", value_.c_str());
    }
    const char *c_str() const { return value_.c_str(); }
    char operator[](size_t index) const { return value_[index]; }

    String &operator=(const char *value) {
        value_ = value ? value : "";
        return *this;
    }

    bool operator==(const char *rhs) const { return value_ == (rhs ? rhs : ""); }
    bool operator!=(const char *rhs) const { return !(*this == rhs); }
    bool operator==(const String &rhs) const { return value_ == rhs.value_; }
    bool operator!=(const String &rhs) const { return value_ != rhs.value_; }

    String operator+(const char *rhs) const {
        return String(value_ + std::string(rhs ? rhs : ""));
    }

    std::string std() const { return value_; }

private:
    std::string value_;
};

inline String operator+(const char *lhs, const String &rhs) {
    return String((lhs ? lhs : "") + rhs.std());
}

template <typename T>
inline T min(T a, T b) {
    return std::min(a, b);
}

template <typename T>
inline T max(T a, T b) {
    return std::max(a, b);
}

class IPAddress {
public:
    IPAddress() = default;
    explicit IPAddress(uint32_t raw) : raw_(raw) {}

    bool fromString(const String &value) { return fromString(value.c_str()); }
    bool fromString(const char *value) {
        if (!value || !*value) return false;
        unsigned int parts[4] = {0, 0, 0, 0};
        if (std::sscanf(value, "%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3]) != 4) {
            return false;
        }
        for (unsigned int part : parts) {
            if (part > 255U) return false;
        }
        raw_ = (parts[0] << 24U) | (parts[1] << 16U) | (parts[2] << 8U) | parts[3];
        return true;
    }

    String toString() const {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
                      (raw_ >> 24U) & 0xFFU, (raw_ >> 16U) & 0xFFU,
                      (raw_ >> 8U) & 0xFFU, raw_ & 0xFFU);
        return String(buffer);
    }

    operator uint32_t() const { return raw_; }

private:
    uint32_t raw_ = 0;
};

struct StubSerial {
    template <typename... Args>
    void printf(const char *, Args...) {}
    void print(const char *) {}
    void println(const char *) {}
};

extern StubSerial Serial;

unsigned long millis();
inline void delay(unsigned long) {}
inline void esp_restart() {}

class WiFiClient {
public:
    IPAddress remoteIP() const { return remote_ip_; }
    void setRemoteIP(const IPAddress &value) { remote_ip_ = value; }
    int readBytes(uint8_t *, size_t) { return 0; }

private:
    IPAddress remote_ip_{0x7F000001U};
};

class File {
public:
    File() = default;
    explicit File(std::string content) : content_(std::move(content)), valid_(true) {}

    explicit operator bool() const { return valid_; }
    String readString() const { return String(content_); }
    void close() {}

private:
    std::string content_;
    bool valid_ = false;
};

class LittleFSClass {
public:
    bool begin(bool = false) { return mounted_; }
    File open(const char *path, const char *) const {
        auto it = files_.find(path ? path : "");
        if (it == files_.end()) return File();
        return File(it->second);
    }
    void setMounted(bool mounted) { mounted_ = mounted; }
    void setFile(const char *path, const char *content) {
        files_[path ? path : ""] = content ? content : "";
    }

private:
    bool mounted_ = true;
    std::map<std::string, std::string> files_;
};

extern LittleFSClass LittleFS;

class Preferences {
public:
    bool begin(const char *ns, bool) {
        namespace_ = ns ? ns : "";
        return true;
    }
    void end() {}

    bool isKey(const char *key) const {
        const auto &store = kvStore();
        return store.find(makeKey(key)) != store.end();
    }

    void remove(const char *key) { kvStore().erase(makeKey(key)); }

    void putBool(const char *key, bool value) { kvStore()[makeKey(key)] = value ? "1" : "0"; }
    void putUInt(const char *key, unsigned int value) { kvStore()[makeKey(key)] = std::to_string(value); }
    void putFloat(const char *key, float value) { kvStore()[makeKey(key)] = std::to_string(value); }
    void putUShort(const char *key, uint16_t value) { kvStore()[makeKey(key)] = std::to_string(value); }
    void putString(const char *key, const char *value) { kvStore()[makeKey(key)] = value ? value : ""; }

    int getInt(const char *key, int fallback = 0) const {
        return getIntegral<int>(key, fallback);
    }
    unsigned int getUInt(const char *key, unsigned int fallback = 0) const {
        return getIntegral<unsigned int>(key, fallback);
    }
    uint16_t getUShort(const char *key, uint16_t fallback = 0) const {
        return getIntegral<uint16_t>(key, fallback);
    }
    String getString(const char *key, const char *fallback = "") const {
        const auto &store = kvStore();
        auto it = store.find(makeKey(key));
        return String(it == store.end() ? fallback : it->second);
    }
    size_t getString(const char *key, char *buffer, size_t len) const {
        String value = getString(key, "");
        value.toCharArray(buffer, len);
        return value.length();
    }

private:
    template <typename T>
    T getIntegral(const char *key, T fallback) const {
        const auto &store = kvStore();
        auto it = store.find(makeKey(key));
        if (it == store.end()) return fallback;
        return static_cast<T>(std::strtoul(it->second.c_str(), nullptr, 10));
    }

    std::string makeKey(const char *key) const {
        return namespace_ + ":" + (key ? key : "");
    }

    static std::map<std::string, std::string> &kvStore() {
        static std::map<std::string, std::string> store;
        return store;
    }

    std::string namespace_;
};

class HTTPClient {
public:
    void begin(const char *) {}
    void begin(const String &) {}
    void addHeader(const char *, const char *) {}
    void addHeader(const char *, const String &) {}
    void setTimeout(int) {}
    int POST(const char *) { return post_code_; }
    int POST(const String &) { return post_code_; }
    int GET() { return get_code_; }
    String getString() const { return body_; }
    int getSize() const { return size_; }
    WiFiClient *getStreamPtr() { return &stream_; }
    void end() {}

    static void setPostCode(int code) { post_code_ = code; }
    static void setGetCode(int code) { get_code_ = code; }
    static void setBody(const char *body) { body_ = String(body ? body : ""); }
    static void setSize(int size) { size_ = size; }

private:
    WiFiClient stream_;

    static int post_code_;
    static int get_code_;
    static String body_;
    static int size_;
};

struct JsonValue {
    enum class Type { Null, String, Bool, Number };

    Type type = Type::Null;
    std::string string_value;
    bool bool_value = false;
    long long number_value = 0;
};

class JsonValueRef {
public:
    explicit JsonValueRef(JsonValue *value) : value_(value) {}

    JsonValueRef &operator=(const char *value) {
        value_->type = JsonValue::Type::String;
        value_->string_value = value ? value : "";
        return *this;
    }
    JsonValueRef &operator=(const String &value) {
        value_->type = JsonValue::Type::String;
        value_->string_value = value.std();
        return *this;
    }
    JsonValueRef &operator=(bool value) {
        value_->type = JsonValue::Type::Bool;
        value_->bool_value = value;
        return *this;
    }
    JsonValueRef &operator=(int value) {
        value_->type = JsonValue::Type::Number;
        value_->number_value = value;
        return *this;
    }
    JsonValueRef &operator=(unsigned long value) {
        value_->type = JsonValue::Type::Number;
        value_->number_value = static_cast<long long>(value);
        return *this;
    }

    const char *operator|(const char *fallback) const {
        if (value_->type == JsonValue::Type::String) return value_->string_value.c_str();
        return fallback;
    }
    bool operator|(bool fallback) const {
        if (value_->type == JsonValue::Type::Bool) return value_->bool_value;
        return fallback;
    }

private:
    JsonValue *value_;
};

class JsonObject {
public:
    explicit JsonObject(std::map<std::string, JsonValue> *object = nullptr) : object_(object) {}
    JsonValueRef operator[](const char *key) { return JsonValueRef(&(*object_)[key ? key : ""]); }

private:
    std::map<std::string, JsonValue> *object_ = nullptr;
};

class JsonArray {
public:
    explicit JsonArray(std::vector<std::map<std::string, JsonValue>> *items = nullptr) : items_(items) {}

    template <typename T>
    JsonObject add() {
        items_->push_back({});
        return JsonObject(&items_->back());
    }

private:
    std::vector<std::map<std::string, JsonValue>> *items_ = nullptr;
};

class JsonDocument {
public:
    enum class RootType { Object, Array };

    template <typename T>
    JsonArray to() {
        root_type_ = RootType::Array;
        array_values_.clear();
        object_values_.clear();
        return JsonArray(&array_values_);
    }

    JsonValueRef operator[](const char *key) {
        root_type_ = RootType::Object;
        return JsonValueRef(&object_values_[key ? key : ""]);
    }

    RootType rootType() const { return root_type_; }
    const std::map<std::string, JsonValue> &objectValues() const { return object_values_; }
    const std::vector<std::map<std::string, JsonValue>> &arrayValues() const { return array_values_; }
    std::map<std::string, JsonValue> &mutableObjectValues() { return object_values_; }
    void clearObject() {
        root_type_ = RootType::Object;
        object_values_.clear();
        array_values_.clear();
    }

private:
    RootType root_type_ = RootType::Object;
    std::map<std::string, JsonValue> object_values_;
    std::vector<std::map<std::string, JsonValue>> array_values_;
};

struct DeserializationError {
    enum Code { Ok, InvalidInput };

    explicit DeserializationError(Code code = Ok) : code_(code) {}
    bool operator!=(Code rhs) const { return code_ != rhs; }

private:
    Code code_;
};

inline std::string jsonEscape(const std::string &value) {
    std::string out;
    for (char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

inline void serializeJsonObject(const std::map<std::string, JsonValue> &object, std::string &out) {
    out.push_back('{');
    bool first = true;
    for (const auto &entry : object) {
        if (!first) out.push_back(',');
        first = false;
        out += "\"" + jsonEscape(entry.first) + "\":";
        switch (entry.second.type) {
            case JsonValue::Type::String:
                out += "\"" + jsonEscape(entry.second.string_value) + "\"";
                break;
            case JsonValue::Type::Bool:
                out += entry.second.bool_value ? "true" : "false";
                break;
            case JsonValue::Type::Number:
                out += std::to_string(entry.second.number_value);
                break;
            case JsonValue::Type::Null:
            default:
                out += "null";
                break;
        }
    }
    out.push_back('}');
}

inline void serializeJson(const JsonDocument &doc, String &out) {
    std::string buffer;
    if (doc.rootType() == JsonDocument::RootType::Array) {
        buffer.push_back('[');
        bool first = true;
        for (const auto &item : doc.arrayValues()) {
            if (!first) buffer.push_back(',');
            first = false;
            serializeJsonObject(item, buffer);
        }
        buffer.push_back(']');
    } else {
        serializeJsonObject(doc.objectValues(), buffer);
    }
    out = buffer.c_str();
}

inline void skipJsonWhitespace(const char *&p) {
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
}

inline bool parseJsonStringToken(const char *&p, std::string &out) {
    if (*p != '"') return false;
    ++p;
    out.clear();
    while (*p && *p != '"') {
        if (*p == '\\' && p[1] != '\0') ++p;
        out.push_back(*p++);
    }
    if (*p != '"') return false;
    ++p;
    return true;
}

inline DeserializationError deserializeJson(JsonDocument &doc, const char *input) {
    if (!input) return DeserializationError(DeserializationError::InvalidInput);
    const char *p = input;
    skipJsonWhitespace(p);
    if (*p != '{') return DeserializationError(DeserializationError::InvalidInput);
    ++p;
    doc.clearObject();

    while (true) {
        skipJsonWhitespace(p);
        if (*p == '}') {
            ++p;
            return DeserializationError(DeserializationError::Ok);
        }

        std::string key;
        if (!parseJsonStringToken(p, key)) {
            return DeserializationError(DeserializationError::InvalidInput);
        }

        skipJsonWhitespace(p);
        if (*p != ':') return DeserializationError(DeserializationError::InvalidInput);
        ++p;
        skipJsonWhitespace(p);

        JsonValue &value = doc.mutableObjectValues()[key];
        if (*p == '"') {
            value.type = JsonValue::Type::String;
            if (!parseJsonStringToken(p, value.string_value)) {
                return DeserializationError(DeserializationError::InvalidInput);
            }
        } else if (std::strncmp(p, "true", 4) == 0) {
            value.type = JsonValue::Type::Bool;
            value.bool_value = true;
            p += 4;
        } else if (std::strncmp(p, "false", 5) == 0) {
            value.type = JsonValue::Type::Bool;
            value.bool_value = false;
            p += 5;
        } else if (*p == '-' || std::isdigit(static_cast<unsigned char>(*p))) {
            char *end = nullptr;
            value.type = JsonValue::Type::Number;
            value.number_value = std::strtoll(p, &end, 10);
            if (end == p) return DeserializationError(DeserializationError::InvalidInput);
            p = end;
        } else {
            return DeserializationError(DeserializationError::InvalidInput);
        }

        skipJsonWhitespace(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            ++p;
            return DeserializationError(DeserializationError::Ok);
        }
        return DeserializationError(DeserializationError::InvalidInput);
    }
}

inline DeserializationError deserializeJson(JsonDocument &doc, const String &input) {
    return deserializeJson(doc, input.c_str());
}

class WebServer {
public:
    void reset() {
        headers_.clear();
        args_.clear();
        response_headers_.clear();
        status_code_ = 0;
        content_type_ = "";
        body_ = "";
    }

    void setHeaderValue(const char *name, const char *value) { headers_[name ? name : ""] = String(value); }
    void setArgValue(const char *name, const char *value) { args_[name ? name : ""] = String(value); }
    void setRemoteIP(const char *value) {
        IPAddress ip;
        ip.fromString(value);
        client_.setRemoteIP(ip);
    }

    bool hasHeader(const char *name) const { return headers_.count(name ? name : "") > 0; }
    String header(const char *name) const {
        auto it = headers_.find(name ? name : "");
        return it == headers_.end() ? String("") : it->second;
    }

    bool hasArg(const char *name) const { return args_.count(name ? name : "") > 0; }
    String arg(const char *name) const {
        auto it = args_.find(name ? name : "");
        return it == args_.end() ? String("") : it->second;
    }

    void sendHeader(const char *name, const char *value) { response_headers_[name ? name : ""] = String(value); }
    void send(int code, const char *content_type, const char *body) {
        status_code_ = code;
        content_type_ = String(content_type);
        body_ = String(body);
    }
    void send(int code, const char *content_type, const String &body) {
        status_code_ = code;
        content_type_ = String(content_type);
        body_ = body;
    }
    size_t streamFile(const File &file, const char *content_type) {
        status_code_ = 200;
        content_type_ = String(content_type);
        body_ = file.readString();
        return body_.length();
    }

    WiFiClient &client() { return client_; }
    int statusCode() const { return status_code_; }
    String body() const { return body_; }

private:
    std::map<std::string, String> headers_;
    std::map<std::string, String> args_;
    std::map<std::string, String> response_headers_;
    int status_code_ = 0;
    String content_type_;
    String body_;
    WiFiClient client_;
};

class WebSocketsServer {
public:
    void sendTXT(uint8_t, const char *) {}
    void sendTXT(uint8_t, const String &) {}
    void disconnect(uint8_t) {}
};

enum WStype_t {
    WStype_DISCONNECTED,
    WStype_CONNECTED,
    WStype_TEXT,
};

class CheapStepper {
public:
    void newMove(bool, int) {}
    void stop() {}
};

class Adafruit_MAX31865 {};
class DHT {};
class Adafruit_INA260 {};
class PubSubClient {};

class InfluxDBClient {
public:
    const char *getLastErrorMessage() const { return "stub"; }
};

class Point {};

class UpdateClass {
public:
    bool begin(int) { return true; }
    void write(const uint8_t *, int) {}
    void abort() {}
    bool end(bool) { return true; }
    bool isFinished() const { return true; }
    const char *errorString() const { return "stub"; }
};

extern UpdateClass Update;

struct esp_partition_t {
    const char *label;
};

inline const esp_partition_t *esp_ota_get_running_partition() {
    static esp_partition_t partition{"factory"};
    return &partition;
}

inline uint32_t esp_random() {
    return 0x12345678UL;
}

typedef struct {
    uint8_t opaque[32];
} mbedtls_sha256_context;

inline void mbedtls_sha256_init(mbedtls_sha256_context *) {}
inline void mbedtls_sha256_starts(mbedtls_sha256_context *, int) {}
inline void mbedtls_sha256_update(mbedtls_sha256_context *, const uint8_t *, size_t) {}
inline void mbedtls_sha256_finish(mbedtls_sha256_context *, uint8_t *out) {
    if (out) std::memset(out, 0, 32);
}
inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}
