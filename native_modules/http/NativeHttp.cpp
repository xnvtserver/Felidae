#include "NativeHttp.h"
#include "../common/NativeJson.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#if !defined(__EMSCRIPTEN__) && __has_include("../../third_party/cpp-httplib/httplib.h")
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include "../../third_party/cpp-httplib/httplib.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#define FELIDAE_HAS_CPP_HTTPLIB 1
#endif

namespace Felidae::NativeHttp {

namespace fs = std::filesystem;

struct UrlParts {
    std::string scheme;
    std::string origin;
    std::string path;
};


static bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

static std::string lower(std::string text);

static std::string contentTypeForPath(const fs::path& path, const std::string& fallback) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".js") return "application/javascript";
    if (extension == ".wasm") return "application/wasm";
    if (extension == ".json") return "application/json";
    if (extension == ".css") return "text/css";
    if (extension == ".html" || extension == ".htm") return "text/html";
    return fallback.empty() ? "application/octet-stream" : fallback;
}

static bool isStaticAssetRequest(const std::string& requestPath) {
    return startsWith(requestPath, "/wasm/");
}

static bool readStaticAsset(const std::string& requestPath, std::string& body, std::string& contentType) {
    if (!startsWith(requestPath, "/wasm/")) return false;
    fs::path relative = fs::path(requestPath.substr(1)).lexically_normal();
    fs::path target = (fs::current_path() / "docs" / relative).lexically_normal();
    fs::path docsRoot = (fs::current_path() / "docs").lexically_normal();
    const std::string targetText = target.string();
    const std::string docsText = docsRoot.string();
    if (targetText.rfind(docsText, 0) != 0 || !fs::exists(target) || !fs::is_regular_file(target)) return false;
    std::ifstream in(target, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    body = buffer.str();
    contentType = contentTypeForPath(target, "application/octet-stream");
    return true;
}
static std::string lower(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

static UrlParts parseUrl(const std::string& url) {
    const auto schemePos = url.find("://");
    if (schemePos == std::string::npos) throw Error("http.request expects absolute URL");
    UrlParts parts;
    parts.scheme = lower(url.substr(0, schemePos));
    const auto authorityStart = schemePos + 3;
    const auto pathStart = url.find('/', authorityStart);
    parts.origin = pathStart == std::string::npos ? url : url.substr(0, pathStart);
    parts.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    if (parts.scheme != "http" && parts.scheme != "https") {
        throw Error("http.request supports only http and https URLs");
    }
    return parts;
}

std::string request(const std::string& method,
                    const std::string& url,
                    const std::string& body,
                    const std::string& contentType) {
#ifdef FELIDAE_HAS_CPP_HTTPLIB
    const auto parts = parseUrl(url);
    if (parts.scheme == "https") {
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
        throw Error("https requests require cpp-httplib built with OpenSSL support");
#endif
    }
    httplib::Client client(parts.origin);
    client.set_follow_location(true);
    client.set_connection_timeout(10);
    client.set_read_timeout(30);

    const std::string op = lower(method);
    httplib::Result result;
    if (op == "get") result = client.Get(parts.path);
    else if (op == "delete") result = client.Delete(parts.path);
    else if (op == "post") result = client.Post(parts.path, body, contentType.empty() ? "text/plain" : contentType);
    else if (op == "put") result = client.Put(parts.path, body, contentType.empty() ? "text/plain" : contentType);
    else throw Error("Unsupported HTTP method: " + method);

    if (!result) throw Error("http." + op + " failed: " + httplib::to_string(result.error()));
    if (result->status < 200 || result->status >= 300) {
        throw Error("http." + op + " returned status " + std::to_string(result->status));
    }
    return result->body;
#else
    (void)method;
    (void)url;
    (void)body;
    (void)contentType;
    throw Error("http.request requires cpp-httplib. Add third_party/cpp-httplib/httplib.h or build with the HTTP dependency available.");
#endif
}

std::string serveStatic(const std::string& host,
                        int port,
                        const std::string& responseText,
                        const std::string& contentType) {
#ifdef FELIDAE_HAS_CPP_HTTPLIB
    if (port <= 0 || port > 65535) throw Error("http.serveStatic expects a valid TCP port");
    httplib::Server server;
    auto handler = [&](const httplib::Request& request, httplib::Response& response) {
        std::string assetBody;
        std::string assetContentType;
        if (readStaticAsset(request.path, assetBody, assetContentType)) {
            response.set_content(std::move(assetBody), assetContentType);
            return;
        }
        if (isStaticAssetRequest(request.path)) {
            response.status = 404;
            response.set_content("WASM asset not found. Build docs/wasm with ./build.sh --target wasm.", "text/plain");
            return;
        }
        response.set_content(responseText, contentType.empty() ? "text/plain" : contentType);
    };
    server.Get(R"(/.*)", handler);
    server.Post("/", handler);
    server.Put("/", handler);
    server.Delete("/", handler);
    std::cout << "http.serveStatic listening on http://" << host << ":" << port << "/" << std::endl;
    if (!server.listen(host, port)) {
        throw Error("http.serveStatic failed to listen on " + host + ":" + std::to_string(port));
    }
    return "stopped";
#else
    (void)host;
    (void)port;
    (void)responseText;
    (void)contentType;
    throw Error("http.serveStatic requires cpp-httplib. Add third_party/cpp-httplib/httplib.h or build with the HTTP dependency available.");
#endif
}

} // namespace Felidae::NativeHttp

namespace {

std::string httpOperation(const std::string& functionName) {
    const size_t separator = functionName.rfind(':');
    return separator == std::string::npos ? functionName : functionName.substr(separator + 1);
}

std::string optionalHttpString(const Felidae::NativeJson::Value& args,
                               const std::string& name,
                               const std::string& fallback) {
    const auto* value = Felidae::NativeJson::field(args, name);
    if (!value) return fallback;
    if (value->kind != Felidae::NativeJson::Value::Kind::String) {
        throw std::runtime_error("http native call expects string argument '" + name + "'");
    }
    return value->text;
}

Felidae::NativeJson::Value dispatchHttp(const std::string& functionName,
                                        const Felidae::NativeJson::Value& args) {
    using Felidae::NativeJson::Value;
    const std::string operation = httpOperation(functionName);
    if (operation == "get" || operation == "post" ||
        operation == "put" || operation == "delete") {
        const auto& url = Felidae::NativeJson::requireField(
            args, "url", Value::Kind::String, "http." + operation);
        const std::string body = optionalHttpString(args, "body", "");
        const std::string contentType = optionalHttpString(args, "contentType", "text/plain");
        return Felidae::NativeJson::string(
            Felidae::NativeHttp::request(operation, url.text, body, contentType));
    }
    if (operation == "serveStatic") {
        const auto& host = Felidae::NativeJson::requireField(
            args, "host", Value::Kind::String, "http.serveStatic");
        const auto& port = Felidae::NativeJson::requireField(
            args, "port", Value::Kind::Number, "http.serveStatic");
        const auto& response = Felidae::NativeJson::requireField(
            args, "response", Value::Kind::String, "http.serveStatic");
        if (port.number != static_cast<int>(port.number)) {
            throw std::runtime_error("http.serveStatic expects integer argument 'port'");
        }
        return Felidae::NativeJson::string(Felidae::NativeHttp::serveStatic(
            host.text,
            static_cast<int>(port.number),
            response.text,
            optionalHttpString(args, "contentType", "text/plain")));
    }
    throw std::runtime_error("Unsupported HTTP native function '" + functionName + "'");
}

} // namespace

extern "C" FELIDAE_HTTP_EXPORT char* felidae_native_call(const char* functionName,
                                                           const char* argsJson) {
    try {
        const auto args = Felidae::NativeJson::parse(argsJson, "HTTP native module");
        return Felidae::NativeJson::copyResponse(Felidae::NativeJson::stringify(
            dispatchHttp(functionName ? functionName : "", args)));
    } catch (const std::exception& error) {
        return Felidae::NativeJson::copyResponse(Felidae::NativeJson::stringify(
            Felidae::NativeJson::error(error.what())));
    } catch (...) {
        return Felidae::NativeJson::copyResponse(
            "{\"error\":\"Unknown HTTP native module failure\"}");
    }
}

extern "C" FELIDAE_HTTP_EXPORT void felidae_native_free(char* value) {
    std::free(value);
}

