#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define FELIDAE_NATIVE_EXPORT __declspec(dllexport)
#else
#define FELIDAE_NATIVE_EXPORT __attribute__((visibility("default")))
#endif

static std::string jsonEscape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else if (ch == '\n') out += "\\n";
        else out += ch;
    }
    return out;
}

static std::string readJsonStringField(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return {};
    pos++;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        char ch = json[pos++];
        if (ch == '\\' && pos < json.size()) {
            char esc = json[pos++];
            if (esc == 'n') out += '\n';
            else out += esc;
        } else {
            out += ch;
        }
    }
    return out;
}

static char* copyResponse(const std::string& response) {
    char* buffer = static_cast<char*>(std::malloc(response.size() + 1));
    if (!buffer) return nullptr;
    std::memcpy(buffer, response.c_str(), response.size() + 1);
    return buffer;
}

extern "C" FELIDAE_NATIVE_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson) {
    const std::string function = functionName ? functionName : "";
    const std::string args = argsJson ? argsJson : "{}";
    if (function == "smoke:echo") {
        const std::string value = readJsonStringField(args, "value");
        return copyResponse("{\"access\":\"" + jsonEscape(value) + "\"}");
    }
    if (function == "smoke:fail") {
        const std::string message = readJsonStringField(args, "message");
        return copyResponse("{\"error\":\"" + jsonEscape(message.empty() ? "native failure" : message) + "\"}");
    }
    if (function == "smoke:invalidJson") {
        return copyResponse("{not-json");
    }
    return copyResponse("{\"error\":\"Unsupported smoke native function\"}");
}

extern "C" FELIDAE_NATIVE_EXPORT void felidae_native_free(char* ptr) {
    std::free(ptr);
}
