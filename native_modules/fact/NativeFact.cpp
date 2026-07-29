#include "fact.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Json {
    enum Kind { Null, Number, String, Array, Object } kind = Null;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
};

void skipWs(const std::string& s, size_t& p) {
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
}

bool parseValue(const std::string& s, size_t& p, Json& out);

bool parseString(const std::string& s, size_t& p, std::string& out) {
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    while (p < s.size()) {
        char c = s[p++];
        if (c == '"') return true;
        if (c == '\\' && p < s.size()) {
            char e = s[p++];
            if (e == 'n') out.push_back('\n');
            else if (e == 't') out.push_back('\t');
            else out.push_back(e);
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool parseArray(const std::string& s, size_t& p, Json& out) {
    if (p >= s.size() || s[p] != '[') return false;
    ++p;
    out.kind = Json::Array;
    skipWs(s, p);
    if (p < s.size() && s[p] == ']') { ++p; return true; }
    while (p < s.size()) {
        Json item;
        if (!parseValue(s, p, item)) return false;
        out.items.push_back(std::move(item));
        skipWs(s, p);
        if (p < s.size() && s[p] == ',') { ++p; continue; }
        if (p < s.size() && s[p] == ']') { ++p; return true; }
        return false;
    }
    return false;
}

bool parseObject(const std::string& s, size_t& p, Json& out) {
    if (p >= s.size() || s[p] != '{') return false;
    ++p;
    out.kind = Json::Object;
    skipWs(s, p);
    if (p < s.size() && s[p] == '}') { ++p; return true; }
    while (p < s.size()) {
        std::string key;
        if (!parseString(s, p, key)) return false;
        skipWs(s, p);
        if (p >= s.size() || s[p] != ':') return false;
        ++p;
        Json value;
        if (!parseValue(s, p, value)) return false;
        out.fields[std::move(key)] = std::move(value);
        skipWs(s, p);
        if (p < s.size() && s[p] == ',') { ++p; skipWs(s, p); continue; }
        if (p < s.size() && s[p] == '}') { ++p; return true; }
        return false;
    }
    return false;
}

bool parseNumber(const std::string& s, size_t& p, Json& out) {
    size_t start = p;
    if (p < s.size() && s[p] == '-') ++p;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
    if (p < s.size() && s[p] == '.') {
        ++p;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
    }
    if (start == p) return false;
    out.kind = Json::Number;
    out.number = std::strtod(s.c_str() + start, nullptr);
    return true;
}

bool parseValue(const std::string& s, size_t& p, Json& out) {
    skipWs(s, p);
    if (p >= s.size()) return false;
    if (s[p] == '{') return parseObject(s, p, out);
    if (s[p] == '[') return parseArray(s, p, out);
    if (s[p] == '"') {
        out.kind = Json::String;
        return parseString(s, p, out.text);
    }
    if (s.compare(p, 4, "null") == 0) { p += 4; out.kind = Json::Null; return true; }
    if (s.compare(p, 4, "true") == 0) { p += 4; out.kind = Json::String; out.text = "true"; return true; }
    if (s.compare(p, 5, "false") == 0) { p += 5; out.kind = Json::String; out.text = "false"; return true; }
    return parseNumber(s, p, out);
}

Json parseJson(const std::string& text) {
    Json out;
    size_t p = 0;
    if (!parseValue(text, p, out)) throw std::runtime_error("invalid native JSON payload");
    skipWs(text, p);
    if (p != text.size()) throw std::runtime_error("invalid native JSON payload: trailing data");
    return out;
}

const Json* field(const Json& object, const std::string& key) {
    if (object.kind != Json::Object) return nullptr;
    auto it = object.fields.find(key);
    return it == object.fields.end() ? nullptr : &it->second;
}

std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

std::string q(const std::string& s) { return "\"" + esc(s) + "\""; }

std::string jsonText(const Json& value) {
    switch (value.kind) {
        case Json::Null:
            return "null";
        case Json::Number: {
            std::ostringstream out;
            out << value.number;
            return out.str();
        }
        case Json::String:
            return q(value.text);
        case Json::Array: {
            std::ostringstream out;
            out << "[";
            for (size_t i = 0; i < value.items.size(); ++i) {
                if (i) out << ",";
                out << jsonText(value.items[i]);
            }
            out << "]";
            return out.str();
        }
        case Json::Object: {
            std::ostringstream out;
            out << "{";
            bool first = true;
            for (const auto& entry : value.fields) {
                if (!first) out << ",";
                first = false;
                out << q(entry.first) << ":" << jsonText(entry.second);
            }
            out << "}";
            return out.str();
        }
    }
    return "null";
}

char* copyResponse(const std::string& response) {
    char* buffer = static_cast<char*>(std::malloc(response.size() + 1));
    if (!buffer) return nullptr;
    std::memcpy(buffer, response.c_str(), response.size() + 1);
    return buffer;
}

std::string kindName(const Json& v) {
    switch (v.kind) {
        case Json::Null: return "nil";
        case Json::Number: return std::floor(v.number) == v.number ? "integer" : "decimal";
        case Json::String: return (v.text == "true" || v.text == "false") ? "boolean" : "string";
        case Json::Array: return "collection";
        case Json::Object: return "fact";
    }
    return "unknown";
}

std::string scalarText(const Json& v) {
    if (v.kind == Json::String) return v.text;
    if (v.kind == Json::Number) {
        std::ostringstream out;
        out << v.number;
        return out.str();
    }
    if (v.kind == Json::Null) return "nil";
    return kindName(v);
}

std::string normalized(std::string s) {
    std::string out;
    bool previousSpace = false;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            previousSpace = false;
        } else if (!previousSpace && !out.empty()) {
            out.push_back(' ');
            previousSpace = true;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

double clamp01(double value) {
    if (std::isnan(value) || value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

double editSimilarity(const std::string& left, const std::string& right) {
    const std::string a = normalized(left);
    const std::string b = normalized(right);
    if (a == b) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    std::vector<size_t> prev(b.size() + 1), curr(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        curr[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    const double denom = static_cast<double>(std::max(a.size(), b.size()));
    return clamp01(1.0 - static_cast<double>(prev[b.size()]) / denom);
}

struct Config {
    std::string algorithm = "semantic_recursive";
    std::string lexicalAlgorithm = "wu_palmer";
    std::string fieldAlignment = "semantic";
    std::string collectionMode = "auto";
    std::string missingFieldPolicy = "penalize";
    double threshold = 0.80;
    size_t maximumDepth = 32;
    size_t maximumFields = 256;
    bool explain = false;
};

double asNumber(const Json* value, double fallback = 0.0) {
    if (!value) return fallback;
    if (value->kind == Json::Number) return value->number;
    return fallback;
}

std::string stringField(const Json& value, const std::string& name) {
    const Json* item = field(value, name);
    return item && item->kind == Json::String ? item->text : "";
}

std::string factTypeOf(const Json& value) {
    return stringField(value, "__type");
}

std::string parentTypeOf(const Json& value) {
    return stringField(value, "__parent");
}

bool hasStringValue(const std::set<std::string>& allowed, const std::string& value) {
    return allowed.find(value) != allowed.end();
}

std::string requireStringOption(const Json& args,
                                const std::string& name,
                                const std::string& fallback,
                                const std::set<std::string>& allowed) {
    const Json* value = field(args, name);
    if (!value) return fallback;
    if (value->kind != Json::String) throw std::runtime_error("fact config '" + name + "' expects a string");
    if (!hasStringValue(allowed, value->text)) throw std::runtime_error("Unsupported fact config '" + name + "': " + value->text);
    return value->text;
}

std::string canonicalLexicalAlgorithm(const std::string& value) {
    if (value == "Leacock-Chodorow" || value == "Leacock–Chodorow" || value == "Leacock Chodorow" || value == "leacock_chodorow" || value == "lch") return "leacock_chodorow";
    if (value == "Wu-Palmer" || value == "Wu Palmer" || value == "wup") return "wu_palmer";
    if (value == "Jiang-Conrath" || value == "Jiang Conrath") return "jiang_conrath";
    return value;
}

double requireFiniteNumberOption(const Json& args, const std::string& name, double fallback) {
    const Json* value = field(args, name);
    if (!value) return fallback;
    if (value->kind != Json::Number || !std::isfinite(value->number)) {
        throw std::runtime_error("fact config '" + name + "' expects a finite number");
    }
    return value->number;
}

size_t boundedSizeOption(const Json& args, const std::string& name, size_t fallback, size_t upperBound) {
    const double value = requireFiniteNumberOption(args, name, static_cast<double>(fallback));
    if (value < 1.0) throw std::runtime_error("fact config '" + name + "' must be at least 1");
    if (value > static_cast<double>(upperBound)) throw std::runtime_error("fact config '" + name + "' exceeds the supported limit");
    return static_cast<size_t>(value);
}

Config readConfig(const Json& args) {
    if (args.kind != Json::Object) throw std::runtime_error("fact native call expects an object payload");
    Config c;
    c.algorithm = requireStringOption(args, "algorithm", c.algorithm, {
        "exact_recursive", "structural", "semantic_recursive", "semantic_pattern", "relationship_aware"
    });
    c.lexicalAlgorithm = canonicalLexicalAlgorithm(requireStringOption(args, "lexical_algorithm", c.lexicalAlgorithm, {
        "path", "wup", "wu_palmer", "Wu-Palmer", "Wu Palmer", "resnik", "jiang_conrath", "Jiang-Conrath", "Jiang Conrath", "lin", "edit", "Leacock-Chodorow", "Leacock–Chodorow", "Leacock Chodorow", "leacock_chodorow", "lch"
    }));
    c.fieldAlignment = requireStringOption(args, "field_alignment", c.fieldAlignment, {"exact", "semantic"});
    c.collectionMode = requireStringOption(args, "collection_mode", c.collectionMode, {"auto", "ordered", "unordered"});
    c.missingFieldPolicy = requireStringOption(args, "missing_field_policy", c.missingFieldPolicy, {"penalize", "ignore"});
    c.threshold = requireFiniteNumberOption(args, "threshold", c.threshold);
    if (c.threshold < 0.0 || c.threshold > 1.0) throw std::runtime_error("fact config 'threshold' must be between 0 and 1");
    c.maximumDepth = boundedSizeOption(args, "maximum_depth", c.maximumDepth, 512);
    c.maximumFields = boundedSizeOption(args, "maximum_fields", c.maximumFields, 100000);
    const Json* explainValue = field(args, "explain");
    if (explainValue) {
        if (explainValue->kind != Json::String || (explainValue->text != "true" && explainValue->text != "false")) {
            throw std::runtime_error("fact config 'explain' expects \"true\" or \"false\"");
        }
        c.explain = explainValue->text == "true";
    }
    return c;
}

struct Feature {
    std::string path;
    std::string name;
    std::string kind;
    std::string value;
    size_t depth = 0;
};

void collectFeatures(const Json& value,
                     const std::string& path,
                     size_t depth,
                     const Config& config,
                     std::vector<Feature>& out,
                     bool& truncated) {
    if (depth > config.maximumDepth || out.size() >= config.maximumFields) {
        truncated = true;
        return;
    }
    std::string name = path;
    size_t dot = name.find_last_of(".[]");
    if (dot != std::string::npos && dot + 1 < name.size()) name = name.substr(dot + 1);
    out.push_back(Feature{path.empty() ? "$" : path, name.empty() ? "$" : name, kindName(value), scalarText(value), depth});
    if (value.kind == Json::Object) {
        for (const auto& entry : value.fields) {
            const std::string childPath = path.empty() ? entry.first : path + "." + entry.first;
            collectFeatures(entry.second, childPath, depth + 1, config, out, truncated);
        }
    } else if (value.kind == Json::Array) {
        for (size_t i = 0; i < value.items.size(); ++i) {
            collectFeatures(value.items[i], path + "[" + std::to_string(i) + "]", depth + 1, config, out, truncated);
        }
    }
}

std::string featuresJson(const std::vector<Feature>& features) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < features.size(); ++i) {
        if (i) out << ",";
        out << "{\"path\":" << q(features[i].path)
            << ",\"field\":" << q(features[i].name)
            << ",\"kind\":" << q(features[i].kind)
            << ",\"value\":" << q(features[i].value)
            << ",\"depth\":" << features[i].depth << "}";
    }
    out << "]";
    return out.str();
}

std::map<std::string, Feature> leafMap(const std::vector<Feature>& features) {
    std::map<std::string, Feature> out;
    for (const auto& feature : features) {
        if (feature.kind != "fact" && feature.kind != "collection") out[feature.path] = feature;
    }
    return out;
}

double scalarSimilarity(const Json& left, const Json& right) {
    if (left.kind == Json::Null || right.kind == Json::Null) return left.kind == right.kind ? 1.0 : 0.0;
    if (left.kind == Json::Number && right.kind == Json::Number) {
        const double denom = std::max({std::fabs(left.number), std::fabs(right.number), 1e-9});
        return clamp01(1.0 - std::fabs(left.number - right.number) / denom);
    }
    if (left.kind == Json::String && right.kind == Json::String) {
        if (left.text == right.text) return 1.0;
        if (normalized(left.text) == normalized(right.text)) return 0.95;
        return editSimilarity(left.text, right.text) * 0.65;
    }
    return 0.0;
}

struct CompareResult {
    double score = 0.0;
    double structure = 0.0;
    double values = 0.0;
    double fields = 0.0;
    std::vector<std::string> matches;
    std::vector<std::string> differences;
    std::vector<std::string> warnings;
};

double fieldNameSimilarity(const std::string& left, const std::string& right) {
    if (left == right) return 1.0;
    if (normalized(left) == normalized(right)) return 0.95;
    return editSimilarity(left, right);
}

CompareResult compareValues(const Json& left, const Json& right, const Config& config, size_t depth = 0) {
    CompareResult result;
    if (depth > config.maximumDepth) {
        result.warnings.push_back("maximum_depth_truncated");
        return result;
    }
    if (left.kind == Json::Object && right.kind == Json::Object) {
        size_t matched = 0;
        double valueScore = 0.0;
        const bool exactFields = config.fieldAlignment == "exact" || config.algorithm == "exact_recursive";
        std::set<std::string> rightUsed;
        std::set<std::string> leftSeen;
        const size_t fieldUniverse = std::max(left.fields.size(), right.fields.size());
        if (fieldUniverse > config.maximumFields) result.warnings.push_back("maximum_fields_truncated");
        size_t compared = 0;
        for (const auto& leftEntry : left.fields) {
            if (compared++ >= config.maximumFields) break;
            leftSeen.insert(leftEntry.first);
            const std::pair<const std::string, Json>* rightEntry = nullptr;
            auto exact = right.fields.find(leftEntry.first);
            if (exact != right.fields.end()) {
                rightEntry = &(*exact);
            } else if (!exactFields) {
                double bestScore = 0.0;
                for (const auto& candidate : right.fields) {
                    if (rightUsed.count(candidate.first)) continue;
                    double candidateScore = fieldNameSimilarity(leftEntry.first, candidate.first);
                    if (kindName(leftEntry.second) == kindName(candidate.second)) candidateScore = (candidateScore * 0.75) + 0.25;
                    if (candidateScore > bestScore) {
                        bestScore = candidateScore;
                        rightEntry = &candidate;
                    }
                }
                if (bestScore < 0.35) rightEntry = nullptr;
            }
            if (!rightEntry) {
                result.differences.push_back("removed:" + leftEntry.first);
                continue;
            }
            rightUsed.insert(rightEntry->first);
            ++matched;
            CompareResult child = compareValues(leftEntry.second, rightEntry->second, config, depth + 1);
            if (leftEntry.first != rightEntry->first) child.score = (child.score * 0.80) + (fieldNameSimilarity(leftEntry.first, rightEntry->first) * 0.20);
            valueScore += child.score;
            for (const auto& item : child.differences) result.differences.push_back(leftEntry.first + "." + item);
            for (const auto& item : child.warnings) result.warnings.push_back(item);
            if (child.score >= config.threshold) result.matches.push_back(leftEntry.first == rightEntry->first ? leftEntry.first : leftEntry.first + "->" + rightEntry->first);
        }
        for (const auto& rightEntry : right.fields) {
            if (!rightUsed.count(rightEntry.first) && !leftSeen.count(rightEntry.first)) {
                result.differences.push_back("added:" + rightEntry.first);
            }
        }
        result.fields = fieldUniverse == 0 ? 1.0 : static_cast<double>(matched) / static_cast<double>(fieldUniverse);
        result.structure = result.fields;
        result.values = matched == 0 ? (fieldUniverse == 0 ? 1.0 : 0.0) : valueScore / static_cast<double>(matched);
        result.score = clamp01((result.structure * 0.15) + (result.fields * 0.10) + (result.values * 0.75));
        return result;
    }
    if (left.kind == Json::Array && right.kind == Json::Array) {
        const size_t maxCount = std::max(left.items.size(), right.items.size());
        const size_t minCount = std::min(left.items.size(), right.items.size());
        double sum = 0.0;
        for (size_t i = 0; i < minCount && i < config.maximumFields; ++i) {
            sum += compareValues(left.items[i], right.items[i], config, depth + 1).score;
        }
        result.structure = maxCount == 0 ? 1.0 : static_cast<double>(minCount) / static_cast<double>(maxCount);
        result.values = minCount == 0 ? result.structure : sum / static_cast<double>(minCount);
        result.score = clamp01((result.structure * 0.45) + (result.values * 0.55));
        if (left.items.size() != right.items.size()) result.differences.push_back("collection_size");
        if (left.items.size() > config.maximumFields || right.items.size() > config.maximumFields) result.warnings.push_back("maximum_fields_truncated");
        return result;
    }
    result.structure = left.kind == right.kind ? 1.0 : 0.0;
    result.values = scalarSimilarity(left, right);
    result.fields = result.structure;
    result.score = clamp01((result.structure * 0.25) + (result.values * 0.75));
    if (result.score < 1.0) result.differences.push_back("changed_value");
    return result;
}

std::string warningsJson(const std::vector<std::string>& warnings, bool addWordNetPending) {
    std::set<std::string> unique(warnings.begin(), warnings.end());
    if (addWordNetPending) unique.insert("wordnet_internal_service_pending_for_phase_2");
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& warning : unique) {
        if (!first) out << ",";
        first = false;
        out << q(warning);
    }
    out << "]";
    return out.str();
}

std::string similarityResponse(const Json& args, bool scalarOnly = false) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.check_similarity expects fact1 and fact2\"}";
    Config config = readConfig(args);
    CompareResult compared = compareValues(*left, *right, config);
    if (config.algorithm == "exact_recursive") compared.score = compared.differences.empty() && compared.score == 1.0 ? 1.0 : 0.0;
    if (config.algorithm == "structural") compared.score = compared.structure;
    if (scalarOnly) {
        std::ostringstream score;
        score << "{\"score\":" << compared.score << "}";
        return score.str();
    }
    std::ostringstream out;
    out << "{\"score\":" << compared.score
        << ",\"algorithm\":" << q(config.algorithm)
        << ",\"lexical_algorithm\":" << q(config.lexicalAlgorithm)
        << ",\"type_similarity\":1"
        << ",\"structural_similarity\":" << compared.structure
        << ",\"field_alignment_similarity\":" << compared.fields
        << ",\"lexical_value_similarity\":" << compared.values
        << ",\"numeric_similarity\":" << compared.values
        << ",\"relationship_similarity\":1"
        << ",\"collection_similarity\":" << compared.structure
        << ",\"matched_fields\":[";
    for (size_t i = 0; i < compared.matches.size(); ++i) {
        if (i) out << ",";
        out << q(compared.matches[i]);
    }
    out << "],\"unmatched_fields\":[],\"changed_values\":[";
    for (size_t i = 0; i < compared.differences.size(); ++i) {
        if (i) out << ",";
        out << q(compared.differences[i]);
    }
    out << "],\"semantic_paths\":[],\"metadata\":{"
        << "\"field_alignment\":" << q(config.fieldAlignment)
        << ",\"collection_mode\":" << q(config.collectionMode)
        << ",\"missing_field_policy\":" << q(config.missingFieldPolicy)
        << ",\"phase\":\"phase_1_recursive_baseline\"}"
        << ",\"warnings\":" << warningsJson(compared.warnings, config.algorithm.find("semantic") != std::string::npos)
        << "}";
    return out.str();
}

std::string extractSemantics(const Json& args) {
    const Json* input = field(args, "input");
    if (!input) return "{\"error\":\"fact.extract_semantics expects input\"}";
    Config config = readConfig(args);
    bool truncated = false;
    std::vector<Feature> features;
    collectFeatures(*input, "", 0, config, features, truncated);
    std::ostringstream out;
    out << "{\"kind\":" << q(kindName(*input))
        << ",\"node_count\":" << features.size()
        << ",\"truncated\":" << q(truncated ? "true" : "false")
        << ",\"features\":" << featuresJson(features)
        << ",\"warnings\":" << warningsJson({}, true)
        << "}";
    return out.str();
}

std::string differenceResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.check_difference expects fact1 and fact2\"}";
    Config config = readConfig(args);
    CompareResult compared = compareValues(*left, *right, config);
    std::ostringstream out;
    out << "{\"score\":" << (1.0 - compared.score)
        << ",\"similarity\":" << compared.score
        << ",\"differences\":[";
    for (size_t i = 0; i < compared.differences.size(); ++i) {
        if (i) out << ",";
        out << "{\"path\":" << q(compared.differences[i]) << ",\"change\":\"recursive_baseline\"}";
    }
    out << "],\"warnings\":" << warningsJson(compared.warnings, true)
        << ",\"metadata\":{\"phase\":\"phase_1_recursive_baseline\"}}";
    return out.str();
}

std::string alignFieldsResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.align_fields expects fact1 and fact2\"}";
    Config config = readConfig(args);
    const std::string mode = requireStringOption(args, "mode", config.fieldAlignment, {"exact", "semantic"});
    std::vector<Feature> lf;
    std::vector<Feature> rf;
    bool lt = false;
    bool rt = false;
    collectFeatures(*left, "", 0, config, lf, lt);
    collectFeatures(*right, "", 0, config, rf, rt);
    auto lm = leafMap(lf);
    auto rm = leafMap(rf);
    std::ostringstream out;
    out << "{\"mode\":" << q(mode) << ",\"matches\":[";
    bool first = true;
    const bool exactMode = mode == "exact";
    std::set<std::string> used;
    for (const auto& l : lm) {
        std::string rightPath;
        double score = 0.0;
        auto exact = rm.find(l.first);
        if (exact != rm.end()) {
            rightPath = exact->first;
            score = 1.0;
        } else if (!exactMode) {
            for (const auto& r : rm) {
                if (used.count(r.first)) continue;
                double candidateScore = fieldNameSimilarity(l.second.name, r.second.name);
                if (l.second.kind == r.second.kind) candidateScore = (candidateScore * 0.75) + 0.25;
                if (candidateScore > score) {
                    score = candidateScore;
                    rightPath = r.first;
                }
            }
        }
        if (rightPath.empty() || score < 0.35) continue;
        used.insert(rightPath);
        if (!first) out << ",";
        first = false;
        out << "{\"left\":" << q(l.first) << ",\"right\":" << q(rightPath) << ",\"score\":" << score << "}";
    }
    out << "],\"warnings\":" << warningsJson({}, true) << "}";
    return out.str();
}

std::string patternResponse(const Json& args, bool unify) {
    const Json* factValue = field(args, unify ? "candidate" : "fact");
    const Json* pattern = field(args, "pattern");
    if (!factValue || !pattern) return "{\"error\":\"fact semantic pattern operation expects pattern and candidate/fact\"}";
    Config config = readConfig(args);
    if (!unify) requireStringOption(args, "mode", "semantic", {"exact", "semantic"});
    CompareResult compared = compareValues(*pattern, *factValue, config);
    const bool matched = compared.score >= config.threshold;
    std::ostringstream out;
    out << "{" << (unify ? "\"unified\":" : "\"matched\":") << q(matched ? "true" : "false")
        << ",\"score\":" << compared.score
        << ",\"threshold\":" << config.threshold
        << ",\"bindings\":[]"
        << ",\"exact_bindings\":[]"
        << ",\"approximate_bindings\":[]"
        << ",\"field_mappings\":[]"
        << ",\"failed_constraints\":[";
    if (!matched) out << q("threshold_not_met");
    out << "],\"warnings\":" << warningsJson(compared.warnings, true)
        << ",\"metadata\":{\"phase\":\"phase_1_recursive_baseline\"}}";
    return out.str();
}

std::string nearResponse(const Json& args) {
    Config config = readConfig(args);
    std::string raw = similarityResponse(args, false);
    Json parsed = parseJson(raw);
    if (field(parsed, "error")) return raw;
    const double score = asNumber(field(parsed, "score"), 0.0);
    std::ostringstream out;
    out << "{\"matched\":" << q(score >= config.threshold ? "true" : "false")
        << ",\"score\":" << score
        << ",\"threshold\":" << config.threshold
        << ",\"comparison\":" << raw << "}";
    return out.str();
}

bool jsonEqual(const Json& left, const Json& right) {
    return jsonText(left) == jsonText(right);
}

std::string relationPropertiesResponse(const Json& args) {
    const Json* pairs = field(args, "pairs");
    if (!pairs || pairs->kind != Json::Array) {
        return "{\"error\":\"Relation.properties expects pairs as an array of [left, right] values\"}";
    }
    if (pairs->items.size() > 2048) {
        return "{\"error\":\"Relation.properties supports at most 2048 pairs\"}";
    }
    struct Edge { std::string left; std::string right; };
    std::vector<Edge> edges;
    std::set<std::string> nodes;
    std::set<std::string> edgeKeys;
    const auto key = [](const std::string& left, const std::string& right) {
        return std::to_string(left.size()) + ":" + left + std::to_string(right.size()) + ":" + right;
    };
    for (const auto& pair : pairs->items) {
        if (pair.kind != Json::Array || pair.items.size() != 2) {
            return "{\"error\":\"Relation.properties requires every pair to contain exactly two values\"}";
        }
        const std::string left = jsonText(pair.items[0]);
        const std::string right = jsonText(pair.items[1]);
        nodes.insert(left);
        nodes.insert(right);
        if (edgeKeys.insert(key(left, right)).second) edges.push_back({left, right});
    }
    bool reflexive = true;
    for (const auto& node : nodes) {
        if (!edgeKeys.count(key(node, node))) { reflexive = false; break; }
    }
    bool symmetric = true;
    bool asymmetric = true;
    for (const auto& edge : edges) {
        const bool reversed = edgeKeys.count(key(edge.right, edge.left)) != 0;
        if (!reversed) symmetric = false;
        if (edge.left == edge.right || reversed) asymmetric = false;
    }
    bool transitive = true;
    for (const auto& first : edges) {
        for (const auto& second : edges) {
            if (first.right != second.left) continue;
            if (!edgeKeys.count(key(first.left, second.right))) { transitive = false; break; }
        }
        if (!transitive) break;
    }
    std::ostringstream out;
    out << "{\"pair_count\":" << edges.size()
        << ",\"node_count\":" << nodes.size()
        << ",\"reflexive\":" << (reflexive ? "true" : "false")
        << ",\"symmetric\":" << (symmetric ? "true" : "false")
        << ",\"asymmetric\":" << (asymmetric ? "true" : "false")
        << ",\"transitive\":" << (transitive ? "true" : "false") << "}";
    return out.str();
}

std::string nearestSubfactsResponse(const Json& args) {
    const Json* input = field(args, "input");
    if (!input) return "{\"error\":\"fact.nearest_subfacts expects input\"}";
    const double configuredDepth = requireFiniteNumberOption(args, "maximum_depth", 1.0);
    if (configuredDepth < 1.0 || configuredDepth > 64.0) {
        return "{\"error\":\"fact.nearest_subfacts maximum_depth must be between 1 and 64\"}";
    }
    const size_t maximumDepth = static_cast<size_t>(configuredDepth);
    struct Pending { const Json* value; size_t depth; std::vector<std::string> path; };
    std::vector<Pending> pending{{input, 0, {}}};
    std::vector<Pending> matches;
    size_t cursor = 0;
    while (cursor < pending.size()) {
        Pending current = std::move(pending[cursor++]);
        if (current.depth > 0 && factTypeOf(*current.value).size() > 0) {
            matches.push_back(current);
            if (matches.size() >= 1024) break;
        }
        if (current.depth == maximumDepth) continue;
        if (current.value->kind == Json::Object) {
            for (const auto& entry : current.value->fields) {
                auto path = current.path;
                path.push_back(entry.first);
                pending.push_back({&entry.second, current.depth + 1, std::move(path)});
            }
        } else if (current.value->kind == Json::Array) {
            for (size_t index = 0; index < current.value->items.size(); ++index) {
                auto path = current.path;
                path.push_back("[" + std::to_string(index) + "]");
                pending.push_back({&current.value->items[index], current.depth + 1, std::move(path)});
            }
        }
    }
    std::ostringstream out;
    out << "{\"count\":" << matches.size() << ",\"neighbors\":[";
    for (size_t index = 0; index < matches.size(); ++index) {
        if (index) out << ",";
        out << "{\"value\":" << jsonText(*matches[index].value)
            << ",\"depth\":" << matches[index].depth << ",\"path\":[";
        for (size_t pathIndex = 0; pathIndex < matches[index].path.size(); ++pathIndex) {
            if (pathIndex) out << ",";
            out << q(matches[index].path[pathIndex]);
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

// Finds a structurally equal value in a fact-shaped value without assuming a
// schema.  The root is depth zero; object fields and array elements increase
// the depth by one.  std::map gives object fields a deterministic order and
// arrays retain source order, so the first matching path is stable.
std::string containsSubfactResponse(const Json& args) {
    const Json* input = field(args, "input");
    const Json* candidate = field(args, "candidate");
    if (!input || !candidate) {
        return "{\"error\":\"fact.contains_subfact expects input and candidate\"}";
    }

    const double configuredDepth = requireFiniteNumberOption(args, "maximum_depth", 32.0);
    if (configuredDepth < 0.0 || configuredDepth > 256.0) {
        return "{\"error\":\"fact.contains_subfact maximum_depth must be between 0 and 256\"}";
    }
    const size_t maximumDepth = static_cast<size_t>(configuredDepth);

    struct PendingValue {
        const Json* value;
        size_t depth;
        std::vector<std::string> path;
    };
    std::vector<PendingValue> pending;
    pending.push_back({input, 0, {}});
    size_t cursor = 0;
    while (cursor < pending.size()) {
        PendingValue current = std::move(pending[cursor++]);
        if (jsonEqual(*current.value, *candidate)) {
            std::ostringstream out;
            out << "{\"found\":true,\"depth\":" << current.depth << ",\"path\":[";
            for (size_t i = 0; i < current.path.size(); ++i) {
                if (i) out << ",";
                out << q(current.path[i]);
            }
            out << "]}";
            return out.str();
        }
        if (current.depth == maximumDepth) continue;
        if (current.value->kind == Json::Object) {
            for (const auto& entry : current.value->fields) {
                auto childPath = current.path;
                childPath.push_back(entry.first);
                pending.push_back({&entry.second, current.depth + 1, std::move(childPath)});
            }
        } else if (current.value->kind == Json::Array) {
            for (size_t i = 0; i < current.value->items.size(); ++i) {
                auto childPath = current.path;
                childPath.push_back("[" + std::to_string(i) + "]");
                pending.push_back({&current.value->items[i], current.depth + 1, std::move(childPath)});
            }
        }
    }
    return "{\"found\":false,\"depth\":null,\"path\":[]}";
}

std::string propertyCompareResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.compare_properties expects fact1 and fact2\"}";
    Config config;
    config.algorithm = "exact_recursive";
    config.fieldAlignment = "exact";
    CompareResult compared = compareValues(*left, *right, config);
    std::ostringstream out;
    out << "{\"score\":" << compared.score
        << ",\"mode\":\"property_exact\""
        << ",\"matched\":\"" << (compared.score >= 1.0 ? "true" : "false") << "\""
        << ",\"field_match_score\":" << compared.fields
        << ",\"value_match_score\":" << compared.values
        << ",\"matched_fields\":[";
    for (size_t i = 0; i < compared.matches.size(); ++i) {
        if (i) out << ",";
        out << q(compared.matches[i]);
    }
    out << "],\"differences\":[";
    for (size_t i = 0; i < compared.differences.size(); ++i) {
        if (i) out << ",";
        out << q(compared.differences[i]);
    }
    out << "],\"warnings\":" << warningsJson(compared.warnings, false) << "}";
    return out.str();
}

std::string propertyDifferenceResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.property_difference expects fact1 and fact2\"}";
    Config config;
    config.algorithm = "exact_recursive";
    config.fieldAlignment = "exact";
    CompareResult compared = compareValues(*left, *right, config);
    std::ostringstream out;
    out << "{\"mode\":\"property_exact\",\"changed\":\"" << (compared.differences.empty() ? "false" : "true")
        << "\",\"differences\":[";
    for (size_t i = 0; i < compared.differences.size(); ++i) {
        if (i) out << ",";
        out << "{\"path\":" << q(compared.differences[i]) << ",\"change\":\"property\"}";
    }
    out << "],\"similarity\":" << compared.score << "}";
    return out.str();
}

Json withoutFactIdentityFields(const Json& value) {
    if (value.kind != Json::Object) return value;
    Json copy = value;
    copy.fields.erase("__type");
    copy.fields.erase("__parent");
    copy.fields.erase("name");
    copy.fields.erase("id");
    return copy;
}

std::string commonAncestorResponse(const Json& args);

std::string factCompareResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.compare_facts expects fact1 and fact2\"}";
    if (left->kind != Json::Object || right->kind != Json::Object) {
        return "{\"error\":\"fact.compare_facts expects fact-shaped object values\"}";
    }

    Config config;
    config.algorithm = "exact_recursive";
    config.fieldAlignment = "exact";
    Json leftProperties = withoutFactIdentityFields(*left);
    Json rightProperties = withoutFactIdentityFields(*right);
    CompareResult properties = compareValues(leftProperties, rightProperties, config);

    Json ancestorArgs;
    ancestorArgs.kind = Json::Object;
    ancestorArgs.fields["fact1"] = *left;
    ancestorArgs.fields["fact2"] = *right;
    if (const Json* facts = field(args, "facts")) ancestorArgs.fields["facts"] = *facts;
    Json ancestor = parseJson(commonAncestorResponse(ancestorArgs));
    const std::string ancestorType = stringField(ancestor, "ancestor_type");
    const bool hasDeclaredAncestor = !ancestorType.empty() && ancestorType != "CommonFact";
    const double ancestryScore = hasDeclaredAncestor ? 1.0 : 0.0;
    const double score = clamp01((properties.score * 0.75) + (ancestryScore * 0.25));

    std::ostringstream out;
    out << "{\"score\":" << score
        << ",\"method\":\"fact_property_ancestor\""
        << ",\"property_score\":" << properties.score
        << ",\"ancestry_score\":" << ancestryScore
        << ",\"common_ancestor\":" << q(ancestorType.empty() ? "CommonFact" : ancestorType)
        << ",\"generalized_fact\":" << (field(ancestor, "generalized_fact") ? jsonText(*field(ancestor, "generalized_fact")) : "{}")
        << ",\"matched_fields\":[";
    for (size_t i = 0; i < properties.matches.size(); ++i) {
        if (i) out << ",";
        out << q(properties.matches[i]);
    }
    out << "],\"differences\":[";
    for (size_t i = 0; i < properties.differences.size(); ++i) {
        if (i) out << ",";
        out << q(properties.differences[i]);
    }
    out << "],\"ignored_identity_fields\":[\"__type\",\"__parent\",\"name\",\"id\"]"
        << ",\"warnings\":[]}";
    return out.str();
}

using ParentGraph = std::map<std::string, std::vector<std::string>>;

void addParentEdge(ParentGraph& parents, const std::string& child, const std::string& parent) {
    if (child.empty() || parent.empty()) return;
    auto& values = parents[child];
    if (std::find(values.begin(), values.end(), parent) == values.end()) values.push_back(parent);
}

ParentGraph parentMapFromFacts(const Json& args) {
    ParentGraph parents;
    // The interpreter supplies the compact declared hierarchy for runtime
    // graph operations.  Prefer an explicit caller corpus when present so
    // standalone/native usage retains its existing deterministic behavior.
    const Json* facts = field(args, "facts");
    if (!facts) facts = field(args, "__facts");
    if (facts && facts->kind == Json::Array) {
        for (const auto& item : facts->items) {
            const std::string type = factTypeOf(item);
            const std::string parent = parentTypeOf(item);
            addParentEdge(parents, type, parent);
        }
        return parents;
    }
    const Json* hierarchy = field(args, "__parents");
    if (hierarchy && hierarchy->kind == Json::Object) {
        for (const auto& entry : hierarchy->fields) {
            if (entry.second.kind == Json::String) {
                addParentEdge(parents, entry.first, entry.second.text);
            } else if (entry.second.kind == Json::Array) {
                for (const auto& parent : entry.second.items) {
                    if (parent.kind == Json::String) addParentEdge(parents, entry.first, parent.text);
                }
            }
        }
    }
    return parents;
}

std::vector<std::string> ancestorChain(const std::string& type,
                                       const std::string& directParent,
                                       const ParentGraph& parents) {
    std::vector<std::string> chain;
    std::set<std::string> seen;
    std::vector<std::string> pending;
    if (!type.empty()) pending.push_back(type);
    for (size_t index = 0; index < pending.size(); ++index) {
        const std::string current = pending[index];
        if (!seen.insert(current).second) continue;
        chain.push_back(current);
        const auto next = parents.find(current);
        if (next != parents.end()) {
            pending.insert(pending.end(), next->second.begin(), next->second.end());
        } else if (current == type && !directParent.empty()) {
            pending.push_back(directParent);
        }
    }
    return chain;
}

bool isAncestorType(const std::string& ancestor,
                    const std::string& descendant,
                    const std::string& descendantParent,
                    const ParentGraph& parents) {
    if (ancestor.empty() || descendant.empty()) return false;
    auto chain = ancestorChain(descendant, descendantParent, parents);
    return std::find(chain.begin(), chain.end(), ancestor) != chain.end();
}

std::map<std::string, std::vector<std::string>> childMapFromParents(const ParentGraph& parents) {
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& item : parents) {
        for (const auto& parent : item.second) children[parent].push_back(item.first);
    }
    for (auto& item : children) std::sort(item.second.begin(), item.second.end());
    return children;
}

std::string factTypeArgument(const Json& args, const std::string& name) {
    const Json* value = field(args, name);
    if (!value) return "";
    if (value->kind == Json::String) return value->text;
    return factTypeOf(*value);
}

std::vector<std::string> descendantClosure(const std::string& type,
                                           const std::map<std::string, std::vector<std::string>>& children) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::vector<std::string> pending;
    auto first = children.find(type);
    if (first != children.end()) pending = first->second;
    while (!pending.empty()) {
        const std::string current = pending.back();
        pending.pop_back();
        if (seen.count(current)) continue;
        seen.insert(current);
        out.push_back(current);
        auto next = children.find(current);
        if (next != children.end()) {
            for (auto it = next->second.rbegin(); it != next->second.rend(); ++it) pending.push_back(*it);
        }
    }
    return out;
}

size_t depthOfType(const std::string& type,
                   const ParentGraph& parents) {
    auto chain = ancestorChain(type, "", parents);
    return chain.empty() ? 0 : chain.size();
}

std::string nearestCommonType(const std::vector<std::string>& leftChain,
                              const std::vector<std::string>& rightChain) {
    for (const auto& item : leftChain) {
        if (std::find(rightChain.begin(), rightChain.end(), item) != rightChain.end()) return item;
    }
    return "";
}

std::map<std::string, size_t> factTypeFrequency(const Json& args,
                                                const ParentGraph& parents,
                                                size_t& total) {
    std::map<std::string, size_t> counts;
    total = 0;
    const Json* facts = field(args, "facts");
    if (!facts) facts = field(args, "__facts");
    if (!facts || facts->kind != Json::Array) return counts;
    for (const auto& item : facts->items) {
        const std::string type = factTypeOf(item);
        if (type.empty()) continue;
        ++total;
        auto chain = ancestorChain(type, parentTypeOf(item), parents);
        for (const auto& ancestor : chain) counts[ancestor]++;
    }
    return counts;
}

double informationContentForType(const std::string& type,
                                 const std::map<std::string, size_t>& frequencies,
                                 size_t total) {
    if (type.empty() || total == 0) return 0.0;
    auto found = frequencies.find(type);
    const double count = found == frequencies.end() ? 1.0 : static_cast<double>(std::max<size_t>(1, found->second));
    return -std::log(count / static_cast<double>(total));
}

std::string normalizedFactToken(std::string value) {
    std::string out;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!out.empty() && out.back() != '_') out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 3 && out.substr(out.size() - 3) == "ies") out = out.substr(0, out.size() - 3) + "y";
    else if (out.size() > 3 && out.substr(out.size() - 3) == "ing") out = out.substr(0, out.size() - 3);
    else if (out.size() > 2 && out.substr(out.size() - 2) == "ed") out = out.substr(0, out.size() - 2);
    else if (out.size() > 1 && out.back() == 's') out.pop_back();
    return out;
}

std::string ancestryResponse(const Json& args, bool descendantMode) {
    const Json* ancestor = field(args, descendantMode ? "ancestor" : "ancestor");
    const Json* descendant = field(args, descendantMode ? "descendant" : "descendant");
    if (!ancestor || !descendant) return "{\"error\":\"fact ancestry check expects ancestor and descendant\"}";
    const std::string ancestorType = factTypeOf(*ancestor);
    const std::string descendantType = factTypeOf(*descendant);
    const auto parents = parentMapFromFacts(args);
    const bool matched = isAncestorType(ancestorType, descendantType, parentTypeOf(*descendant), parents);
    std::ostringstream out;
    out << "{\"matched\":" << q(matched ? "true" : "false")
        << ",\"ancestor\":" << q(ancestorType)
        << ",\"descendant\":" << q(descendantType)
        << ",\"mode\":\"declared_extend\"}";
    return out.str();
}

std::string directRelationResponse(const Json& args) {
    const Json* parent = field(args, "parent");
    const Json* child = field(args, "child");
    if (!parent || !child) return "{\"error\":\"fact.direct_relation expects parent and child\"}";
    const std::string parentType = factTypeOf(*parent);
    const std::string childType = factTypeOf(*child);
    const auto parents = parentMapFromFacts(args);
    std::vector<std::string> directParents;
    const auto found = parents.find(childType);
    if (found != parents.end()) directParents = found->second;
    else if (!parentTypeOf(*child).empty()) directParents.push_back(parentTypeOf(*child));
    const bool matched = !parentType.empty() && !childType.empty() &&
        std::find(directParents.begin(), directParents.end(), parentType) != directParents.end();
    std::ostringstream out;
    out << "{\"matched\":" << q(matched ? "true" : "false")
        << ",\"parent\":" << q(parentType)
        << ",\"child\":" << q(childType)
        << ",\"child_parent\":" << q(directParents.empty() ? "" : directParents.front())
        << ",\"mode\":\"direct_parent_child\"}";
    return out.str();
}

std::string closureResponse(const Json& args, bool descendants) {
    const std::string type = factTypeArgument(args, "fact");
    if (type.empty()) return "{\"error\":\"fact closure expects fact or type string\"}";
    const auto parents = parentMapFromFacts(args);
    std::vector<std::string> values;
    if (descendants) {
        values = descendantClosure(type, childMapFromParents(parents));
    } else {
        const Json* factValue = field(args, "fact");
        const std::string directParent = factValue && factValue->kind == Json::Object ? parentTypeOf(*factValue) : "";
        auto chain = ancestorChain(type, directParent, parents);
        if (!chain.empty()) values.assign(chain.begin() + 1, chain.end());
    }
    std::ostringstream out;
    out << "{\"fact\":" << q(type)
        << ",\"mode\":" << q(descendants ? "descendant_closure" : "ancestor_closure")
        << ",\"count\":" << values.size()
        << ",\"facts\":[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << q(values[i]);
    }
    out << "]}";
    return out.str();
}

std::string commonAncestorResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.common_ancestor expects fact1 and fact2\"}";
    const auto parents = parentMapFromFacts(args);
    const auto leftChain = ancestorChain(factTypeOf(*left), parentTypeOf(*left), parents);
    const auto rightChain = ancestorChain(factTypeOf(*right), parentTypeOf(*right), parents);
    std::string declaredAncestor;
    for (const auto& item : leftChain) {
        if (std::find(rightChain.begin(), rightChain.end(), item) != rightChain.end()) {
            declaredAncestor = item;
            break;
        }
    }

    std::ostringstream generalized;
    generalized << "{\"__type\":\"CommonFact\"";
    if (!declaredAncestor.empty()) generalized << ",\"__parent\":" << q(declaredAncestor);
    if (left->kind == Json::Object && right->kind == Json::Object) {
        for (const auto& l : left->fields) {
            if (l.first == "__type" || l.first == "__parent") continue;
            auto r = right->fields.find(l.first);
            if (r == right->fields.end()) continue;
            if (jsonEqual(l.second, r->second)) {
                generalized << "," << q(l.first) << ":" << jsonText(l.second);
            } else if (l.second.kind == Json::Number && r->second.kind == Json::Number) {
                const double minValue = std::min(l.second.number, r->second.number);
                const double maxValue = std::max(l.second.number, r->second.number);
                generalized << "," << q(l.first) << ":{\"__type\":\"Range\",\"min\":" << minValue << ",\"max\":" << maxValue << "}";
            } else if (l.second.kind == Json::Object && r->second.kind == Json::Object) {
                Json nestedArgs;
                nestedArgs.kind = Json::Object;
                nestedArgs.fields["fact1"] = l.second;
                nestedArgs.fields["fact2"] = r->second;
                Json nested = parseJson(commonAncestorResponse(nestedArgs));
                const Json* nestedFact = field(nested, "generalized_fact");
                if (nestedFact) generalized << "," << q(l.first) << ":" << jsonText(*nestedFact);
            }
        }
    }
    generalized << "}";

    std::ostringstream out;
    out << "{\"ancestor_type\":" << q(declaredAncestor.empty() ? "CommonFact" : declaredAncestor)
        << ",\"source\":\"" << (declaredAncestor.empty() ? "synthesized_property_overlap" : "declared_extend") << "\""
        << ",\"generalized_fact\":" << generalized.str()
        << ",\"left_chain\":[";
    for (size_t i = 0; i < leftChain.size(); ++i) {
        if (i) out << ",";
        out << q(leftChain[i]);
    }
    out << "],\"right_chain\":[";
    for (size_t i = 0; i < rightChain.size(); ++i) {
        if (i) out << ",";
        out << q(rightChain[i]);
    }
    out << "]}";
    return out.str();
}

std::string shortestPathResponse(const Json& args) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact.shortest_path expects fact1 and fact2\"}";
    const auto parents = parentMapFromFacts(args);
    const auto leftChain = ancestorChain(factTypeOf(*left), parentTypeOf(*left), parents);
    const auto rightChain = ancestorChain(factTypeOf(*right), parentTypeOf(*right), parents);
    size_t li = leftChain.size();
    size_t ri = rightChain.size();
    for (size_t i = 0; i < leftChain.size(); ++i) {
        auto found = std::find(rightChain.begin(), rightChain.end(), leftChain[i]);
        if (found != rightChain.end()) {
            li = i;
            ri = static_cast<size_t>(std::distance(rightChain.begin(), found));
            break;
        }
    }
    if (li == leftChain.size()) return "{\"reachable\":\"false\",\"distance\":-1,\"path\":[],\"edge_types\":[],\"total_cost\":0}";
    std::vector<std::string> path;
    for (size_t i = 0; i <= li; ++i) path.push_back(leftChain[i]);
    for (size_t i = ri; i > 0; --i) path.push_back(rightChain[i - 1]);
    std::ostringstream out;
    out << "{\"reachable\":\"true\",\"distance\":" << (path.empty() ? 0 : path.size() - 1)
        << ",\"path\":[";
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) out << ",";
        out << q(path[i]);
    }
    out << "],\"edge_types\":[";
    for (size_t i = 1; i < path.size(); ++i) {
        if (i > 1) out << ",";
        out << q("extends");
    }
    out << "],\"total_cost\":" << (path.empty() ? 0 : path.size() - 1)
        << ",\"explanation\":\"declared extend ancestry path\"}";
    return out.str();
}

std::string graphSimilarityResponse(const Json& args, const std::string& algorithm) {
    const Json* left = field(args, "fact1");
    const Json* right = field(args, "fact2");
    if (!left || !right) return "{\"error\":\"fact graph similarity expects fact1 and fact2\"}";
    const auto parents = parentMapFromFacts(args);
    const std::string leftType = factTypeOf(*left);
    const std::string rightType = factTypeOf(*right);
    if (leftType.empty() || rightType.empty()) return "{\"error\":\"fact graph similarity expects fact-shaped objects with __type\"}";
    const auto leftChain = ancestorChain(leftType, parentTypeOf(*left), parents);
    const auto rightChain = ancestorChain(rightType, parentTypeOf(*right), parents);
    const std::string lca = nearestCommonType(leftChain, rightChain);
    double score = 0.0;
    size_t distance = 0;
    if (!lca.empty()) {
        const size_t leftDistance = static_cast<size_t>(std::distance(leftChain.begin(), std::find(leftChain.begin(), leftChain.end(), lca)));
        const size_t rightDistance = static_cast<size_t>(std::distance(rightChain.begin(), std::find(rightChain.begin(), rightChain.end(), lca)));
        distance = leftDistance + rightDistance;
        if (algorithm == "path") {
            score = 1.0 / (static_cast<double>(distance) + 1.0);
        } else if (algorithm == "wu_palmer") {
            const double lcaDepth = static_cast<double>(depthOfType(lca, parents));
            const double leftDepth = static_cast<double>(std::max<size_t>(1, leftChain.size()));
            const double rightDepth = static_cast<double>(std::max<size_t>(1, rightChain.size()));
            score = (2.0 * lcaDepth) / (leftDepth + rightDepth);
        } else {
            size_t total = 0;
            const auto frequencies = factTypeFrequency(args, parents, total);
            const double lcaIc = informationContentForType(lca, frequencies, total);
            const double leftIc = informationContentForType(leftType, frequencies, total);
            const double rightIc = informationContentForType(rightType, frequencies, total);
            if (algorithm == "resnik") score = lcaIc;
            else if (algorithm == "lin") score = (leftIc + rightIc) <= 0.0 ? 0.0 : (2.0 * lcaIc) / (leftIc + rightIc);
        }
    }
    std::ostringstream out;
    out << "{\"score\":" << clamp01(score)
        << ",\"raw_score\":" << score
        << ",\"algorithm\":" << q(algorithm)
        << ",\"scope\":\"fact_graph\""
        << ",\"left\":" << q(leftType)
        << ",\"right\":" << q(rightType)
        << ",\"lowest_common_ancestor\":" << q(lca.empty() ? "CommonFact" : lca)
        << ",\"distance\":" << distance
        << ",\"source\":\"declared_extend_fact_graph\"}";
    return out.str();
}

std::string frequencyStatsResponse(const Json& args) {
    const auto parents = parentMapFromFacts(args);
    size_t total = 0;
    const auto frequencies = factTypeFrequency(args, parents, total);
    std::ostringstream out;
    out << "{\"method\":\"fact_frequency_statistics\",\"total_facts\":" << total << ",\"types\":{";
    bool first = true;
    for (const auto& item : frequencies) {
        if (!first) out << ",";
        first = false;
        const double ic = informationContentForType(item.first, frequencies, total);
        out << q(item.first) << ":{\"count\":" << item.second << ",\"information_content\":" << ic << "}";
    }
    out << "}}";
    return out.str();
}

std::string normalizeResponse(const Json& args) {
    const Json* text = field(args, "text");
    if (!text || text->kind != Json::String) return "{\"error\":\"fact.normalize expects text string\"}";
    std::ostringstream out;
    out << "{\"input\":" << q(text->text)
        << ",\"normalized\":" << q(normalizedFactToken(text->text))
        << ",\"method\":\"morphy_like_fact_token_normalization\"}";
    return out.str();
}

std::vector<std::string> stringArrayField(const Json& args, const std::string& name, std::string& error) {
    std::vector<std::string> values;
    const Json* array = field(args, name);
    if (!array) return values;
    if (array->kind != Json::Array) {
        error = name + " expects an array of field names";
        return values;
    }
    for (const auto& item : array->items) {
        if (item.kind != Json::String || item.text.empty()) {
            error = name + " expects non-empty string field names";
            values.clear();
            return values;
        }
        values.push_back(item.text);
    }
    return values;
}

bool requiredFieldsMatch(const Json& input,
                         const Json& candidate,
                         const std::vector<std::string>& requiredFields,
                         std::vector<std::string>& failedConstraints) {
    if (requiredFields.empty()) return true;
    if (input.kind != Json::Object || candidate.kind != Json::Object) {
        failedConstraints.push_back("required_fields.need_object_facts");
        return false;
    }
    bool matched = true;
    for (const auto& key : requiredFields) {
        const Json* left = field(input, key);
        const Json* right = field(candidate, key);
        if (!left || !right) {
            failedConstraints.push_back("required:" + key + ".missing");
            matched = false;
            continue;
        }
        if (!jsonEqual(*left, *right)) {
            failedConstraints.push_back("required:" + key + ".mismatch");
            matched = false;
        }
    }
    return matched;
}

std::string stringArrayJson(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << q(values[i]);
    }
    out << "]";
    return out.str();
}

std::string findNearestResponse(const Json& args) {
    const Json* input = field(args, "input");
    const Json* candidates = field(args, "candidates");
    if (!input || !candidates || candidates->kind != Json::Array) return "{\"error\":\"fact.find_nearest expects input and array candidates\"}";
    const size_t wanted = static_cast<size_t>(std::max(1.0, asNumber(field(args, "count"), 1.0)));
    std::string requiredFieldsError;
    const auto requiredFields = stringArrayField(args, "required_fields", requiredFieldsError);
    if (!requiredFieldsError.empty()) {
        return "{\"error\":\"fact.find_nearest " + esc(requiredFieldsError) + "\"}";
    }
    Config config;
    config.algorithm = "exact_recursive";
    config.fieldAlignment = "exact";
    struct Ranked { size_t index; double score; std::string differences; };
    std::vector<Ranked> ranked;
    size_t rejected = 0;
    for (size_t i = 0; i < candidates->items.size(); ++i) {
        std::vector<std::string> failedConstraints;
        if (!requiredFieldsMatch(*input, candidates->items[i], requiredFields, failedConstraints)) {
            ++rejected;
            continue;
        }
        CompareResult compared = compareValues(*input, candidates->items[i], config);
        std::ostringstream diffs;
        diffs << "[";
        for (size_t j = 0; j < compared.differences.size(); ++j) {
            if (j) diffs << ",";
            diffs << q(compared.differences[j]);
        }
        diffs << "]";
        ranked.push_back(Ranked{i, compared.score, diffs.str()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.index < b.index;
    });
    std::ostringstream out;
    out << "{\"count\":" << std::min(wanted, ranked.size())
        << ",\"candidate_count\":" << candidates->items.size()
        << ",\"matched_candidate_count\":" << ranked.size()
        << ",\"rejected_count\":" << rejected;
    if (!requiredFields.empty()) out << ",\"required_fields\":" << stringArrayJson(requiredFields);
    out << ",\"nearest\":[";
    for (size_t i = 0; i < ranked.size() && i < wanted; ++i) {
        if (i) out << ",";
        out << "{\"index\":" << ranked[i].index
            << ",\"similarity\":" << ranked[i].score
            << ",\"distance\":" << (1.0 - ranked[i].score)
            << ",\"fact\":" << jsonText(candidates->items[ranked[i].index])
            << ",\"important_differences\":" << ranked[i].differences
            << ",\"reason\":\"" << (requiredFields.empty() ? "highest property similarity" : "required fields matched before property ranking") << "\"}";
    }
    out << "]}";
    return out.str();
}

Json predictValue(const std::vector<Json>& values, double& confidence, std::string& algorithm) {
    if (values.empty()) {
        confidence = 0.0;
        algorithm = "unsupported";
        return Json{};
    }
    const Json& last = values.back();
    if (last.kind == Json::Number) {
        algorithm = values.size() >= 2 ? "linear_trend" : "last_numeric_value";
        double predicted = last.number;
        if (values.size() >= 2 && values[values.size() - 2].kind == Json::Number) {
            predicted = last.number + (last.number - values[values.size() - 2].number);
        }
        Json out;
        out.kind = Json::Number;
        out.number = predicted;
        confidence = values.size() >= 2 ? 0.75 : 0.55;
        return out;
    }
    if (last.kind == Json::String || last.kind == Json::Null) {
        algorithm = "last_value";
        confidence = 0.60;
        return last;
    }
    if (last.kind == Json::Object) {
        algorithm = "recursive_nested_prediction";
        confidence = 0.65;
        return last;
    }
    if (last.kind == Json::Array) {
        algorithm = "last_collection_value";
        confidence = 0.50;
        return last;
    }
    confidence = 0.0;
    algorithm = "unsupported";
    return Json{};
}

std::string predictNextResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"fact.predict_next expects non-empty facts array\"}";
    const std::string factType = stringField(args, "factType").empty() ? "PredictedFact" : stringField(args, "factType");
    std::set<std::string> keys;
    for (const auto& item : facts->items) {
        if (item.kind != Json::Object) continue;
        for (const auto& entry : item.fields) if (entry.first != "__type" && entry.first != "__parent") keys.insert(entry.first);
    }
    Json predicted;
    predicted.kind = Json::Object;
    predicted.fields["__type"] = Json{Json::String, 0.0, factType, {}, {}};
    std::ostringstream fieldsOut;
    fieldsOut << "[";
    bool firstField = true;
    double totalConfidence = 0.0;
    size_t predictedFields = 0;
    for (const auto& key : keys) {
        std::vector<Json> values;
        for (const auto& item : facts->items) {
            const Json* value = field(item, key);
            if (value) values.push_back(*value);
        }
        double confidence = 0.0;
        std::string algorithm;
        Json next = predictValue(values, confidence, algorithm);
        predicted.fields[key] = next;
        totalConfidence += confidence;
        ++predictedFields;
        if (!firstField) fieldsOut << ",";
        firstField = false;
        fieldsOut << "{\"field\":" << q(key)
                  << ",\"value\":" << jsonText(next)
                  << ",\"confidence\":" << confidence
                  << ",\"algorithm\":" << q(algorithm) << "}";
    }
    fieldsOut << "]";
    const double overall = predictedFields == 0 ? 0.0 : totalConfidence / static_cast<double>(predictedFields);
    std::ostringstream out;
    out << "{\"prediction\":" << jsonText(predicted)
        << ",\"confidence\":" << overall
        << ",\"method\":\"field_wise_sequence_prediction\""
        << ",\"field_predictions\":" << fieldsOut.str()
        << ",\"source_count\":" << facts->items.size()
        << ",\"warnings\":[]}";
    return out.str();
}

std::string fxEscape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

std::string majorityLabel(const std::map<std::string, size_t>& counts, size_t& count) {
    std::string label;
    count = 0;
    for (const auto& item : counts) {
        if (item.second > count) {
            label = item.first;
            count = item.second;
        }
    }
    return label;
}

double giniImpurity(const std::map<std::string, size_t>& counts, size_t total) {
    if (total == 0) return 0.0;
    double impurity = 1.0;
    for (const auto& item : counts) {
        const double probability = static_cast<double>(item.second) / static_cast<double>(total);
        impurity -= probability * probability;
    }
    return impurity;
}

std::string trainDecisionTreeResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    const Json* features = field(args, "features");
    const std::string target = stringField(args, "target");
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"fact_analysis.train_decision_tree expects non-empty facts array\"}";
    if (target.empty()) return "{\"error\":\"fact_analysis.train_decision_tree expects target\"}";
    if (!features || features->kind != Json::Array) return "{\"error\":\"fact_analysis.train_decision_tree expects features array\"}";

    std::map<std::string, size_t> classCounts;
    for (const auto& item : facts->items) {
        const Json* targetValue = field(item, target);
        if (!targetValue) continue;
        classCounts[scalarText(*targetValue)]++;
    }
    size_t majorityCount = 0;
    const std::string majority = majorityLabel(classCounts, majorityCount);
    if (majority.empty()) return "{\"error\":\"fact_analysis.train_decision_tree target did not match any training facts\"}";
    const double baselineConfidence = static_cast<double>(majorityCount) / static_cast<double>(facts->items.size());

    std::string splitFeature;
    double splitThreshold = 0.0;
    std::string leftPrediction = majority;
    std::string rightPrediction = majority;
    size_t leftCount = 0;
    size_t rightCount = 0;
    double bestImpurity = std::numeric_limits<double>::infinity();
    for (const auto& feature : features->items) {
        const std::string featureName = scalarText(feature);
        if (featureName.empty()) continue;
        std::vector<double> values;
        for (const auto& item : facts->items) {
            const Json* value = field(item, featureName);
            const Json* targetValue = field(item, target);
            if (value && value->kind == Json::Number && targetValue) values.push_back(value->number);
        }
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        if (values.size() < 2) continue;
        for (size_t i = 1; i < values.size(); ++i) {
            const double threshold = (values[i - 1] + values[i]) / 2.0;
            std::map<std::string, size_t> leftCounts;
            std::map<std::string, size_t> rightCounts;
            size_t leftTotal = 0;
            size_t rightTotal = 0;
            for (const auto& item : facts->items) {
                const Json* value = field(item, featureName);
                const Json* targetValue = field(item, target);
                if (!value || value->kind != Json::Number || !targetValue) continue;
                if (value->number <= threshold) {
                    leftCounts[scalarText(*targetValue)]++;
                    ++leftTotal;
                } else {
                    rightCounts[scalarText(*targetValue)]++;
                    ++rightTotal;
                }
            }
            const size_t splitTotal = leftTotal + rightTotal;
            if (leftTotal == 0 || rightTotal == 0 || splitTotal == 0) continue;
            const double weightedImpurity =
                (static_cast<double>(leftTotal) * giniImpurity(leftCounts, leftTotal) +
                 static_cast<double>(rightTotal) * giniImpurity(rightCounts, rightTotal)) /
                static_cast<double>(splitTotal);
            if (weightedImpurity < bestImpurity) {
                size_t leftMajorityCount = 0;
                size_t rightMajorityCount = 0;
                splitFeature = featureName;
                splitThreshold = threshold;
                leftPrediction = majorityLabel(leftCounts, leftMajorityCount);
                rightPrediction = majorityLabel(rightCounts, rightMajorityCount);
                leftCount = leftTotal;
                rightCount = rightTotal;
                bestImpurity = weightedImpurity;
            }
        }
    }
    const bool hasSplit = !splitFeature.empty();
    const double confidence = hasSplit ? std::max(0.0, 1.0 - bestImpurity) : baselineConfidence;
    const std::string method = hasSplit ? "numeric_gini_stump" : "majority_class_baseline";

    std::ostringstream featureList;
    featureList << "[";
    for (size_t i = 0; i < features->items.size(); ++i) {
        if (i) featureList << ", ";
        featureList << scalarText(features->items[i]);
    }
    featureList << "]";

    std::ostringstream modelFx;
    modelFx << "DecisionTreeModel(\n"
            << "    name: \"GeneratedDecisionTree\",\n"
            << "    target: \"" << fxEscape(target) << "\",\n"
            << "    prediction: \"" << fxEscape(majority) << "\",\n"
            << "    confidence: " << confidence << ",\n"
            << "    method: \"" << method << "\",\n"
            << "    features: \"" << fxEscape(featureList.str()) << "\"";
    if (hasSplit) {
        modelFx << ",\n"
                << "    split_feature: \"" << fxEscape(splitFeature) << "\",\n"
                << "    split_threshold: " << splitThreshold << ",\n"
                << "    left_prediction: \"" << fxEscape(leftPrediction) << "\",\n"
                << "    right_prediction: \"" << fxEscape(rightPrediction) << "\"\n";
    } else {
        modelFx << "\n";
    }
    modelFx
            << ").\n\n"
            << "GeneratedDecisionTreePredict(input: any) =>\n"
            << "    return (prediction: \"" << fxEscape(majority) << "\", confidence: " << confidence
            << ", route: [\"" << method << "\"]).\n";

    std::ostringstream counts;
    counts << "{";
    bool first = true;
    for (const auto& item : classCounts) {
        if (!first) counts << ",";
        first = false;
        counts << q(item.first) << ":" << item.second;
    }
    counts << "}";

    std::ostringstream out;
    out << "{\"model_type\":\"decision_tree\""
        << ",\"method\":" << q(method)
        << ",\"target\":" << q(target)
        << ",\"prediction\":" << q(majority)
        << ",\"confidence\":" << confidence
        << ",\"class_counts\":" << counts.str();
    if (hasSplit) {
        out << ",\"split\":{\"feature\":" << q(splitFeature)
            << ",\"threshold\":" << splitThreshold
            << ",\"left_prediction\":" << q(leftPrediction)
            << ",\"right_prediction\":" << q(rightPrediction)
            << ",\"left_count\":" << leftCount
            << ",\"right_count\":" << rightCount
            << ",\"gini\":" << bestImpurity << "}";
    }
    out << ",\"model_fx\":" << q(modelFx.str())
        << ",\"warnings\":" << (hasSplit ? "[]" : "[\"no_numeric_split_found_used_majority_baseline\"]") << "}";
    return out.str();
}

std::string predictWithModelResponse(const Json& args) {
    const Json* model = field(args, "model");
    if (!model || model->kind != Json::Object) return "{\"error\":\"fact_analysis.predict expects model object\"}";
    if (stringField(*model, "model_type") == "linear_regression") {
        const std::string featureName = stringField(*model, "feature");
        const Json* slope = field(*model, "slope");
        const Json* intercept = field(*model, "intercept");
        const Json* input = field(args, "input");
        const Json* inputValue = input ? field(*input, featureName) : nullptr;
        if (featureName.empty() || !slope || slope->kind != Json::Number || !intercept || intercept->kind != Json::Number) {
            return "{\"error\":\"fact_analysis.predict linear regression model expects feature, slope, and intercept\"}";
        }
        if (!inputValue || inputValue->kind != Json::Number) {
            return "{\"error\":\"fact_analysis.predict linear regression input is missing numeric feature\"}";
        }
        const double predicted = intercept->number + slope->number * inputValue->number;
        std::ostringstream out;
        out << "{\"prediction\":" << predicted
            << ",\"confidence\":" << (field(*model, "r_squared") ? jsonText(*field(*model, "r_squared")) : "0")
            << ",\"model_type\":\"linear_regression\""
            << ",\"decision_route\":[\"linear_regression\"]"
            << ",\"explanation\":\"applied inspectable Felidae regression model fact\"}";
        return out.str();
    }
    const Json* prediction = field(*model, "prediction");
    std::string route = "model_prediction_field";
    if (const Json* split = field(*model, "split")) {
        const std::string featureName = stringField(*split, "feature");
        const Json* threshold = field(*split, "threshold");
        const Json* input = field(args, "input");
        const Json* inputValue = input ? field(*input, featureName) : nullptr;
        if (!featureName.empty() && threshold && threshold->kind == Json::Number && inputValue && inputValue->kind == Json::Number) {
            prediction = inputValue->number <= threshold->number ? field(*split, "left_prediction") : field(*split, "right_prediction");
            route = inputValue->number <= threshold->number ? "numeric_split_left" : "numeric_split_right";
        }
    }
    const Json* confidence = field(*model, "confidence");
    std::ostringstream out;
    out << "{\"prediction\":" << (prediction ? jsonText(*prediction) : "null")
        << ",\"confidence\":" << (confidence ? jsonText(*confidence) : "0")
        << ",\"model_type\":" << q(stringField(*model, "model_type").empty() ? "decision_tree" : stringField(*model, "model_type"))
        << ",\"decision_route\":[" << q(route) << "]"
        << ",\"explanation\":\"applied inspectable Felidae model fact\"}";
    return out.str();
}

std::string evaluateModelResponse(const Json& args) {
    const Json* model = field(args, "model");
    const Json* facts = field(args, "facts");
    const std::string target = stringField(args, "target");
    if (!model || model->kind != Json::Object) return "{\"error\":\"fact_analysis.evaluate_model expects model object\"}";
    if (!facts || facts->kind != Json::Array) return "{\"error\":\"fact_analysis.evaluate_model expects facts array\"}";
    if (target.empty()) return "{\"error\":\"fact_analysis.evaluate_model expects target\"}";
    const Json* prediction = field(*model, "prediction");
    if (!prediction) return "{\"error\":\"fact_analysis.evaluate_model model does not contain prediction\"}";
    auto predictionFor = [&](const Json& input) -> const Json* {
        if (const Json* split = field(*model, "split")) {
            const std::string featureName = stringField(*split, "feature");
            const Json* threshold = field(*split, "threshold");
            const Json* inputValue = field(input, featureName);
            if (!featureName.empty() && threshold && threshold->kind == Json::Number && inputValue && inputValue->kind == Json::Number) {
                return inputValue->number <= threshold->number ? field(*split, "left_prediction") : field(*split, "right_prediction");
            }
        }
        return prediction;
    };
    size_t tested = 0;
    size_t correct = 0;
    for (const auto& item : facts->items) {
        const Json* actual = field(item, target);
        if (!actual) continue;
        ++tested;
        const Json* predicted = predictionFor(item);
        if (predicted && jsonEqual(*predicted, *actual)) ++correct;
    }
    const double accuracy = tested == 0 ? 0.0 : static_cast<double>(correct) / static_cast<double>(tested);
    std::ostringstream out;
    out << "{\"model_type\":" << q(stringField(*model, "model_type").empty() ? "decision_tree" : stringField(*model, "model_type"))
        << ",\"target\":" << q(target)
        << ",\"tested\":" << tested
        << ",\"correct\":" << correct
        << ",\"accuracy\":" << accuracy
        << ",\"method\":\"decision_tree_model_evaluation\"}";
    return out.str();
}

std::string featureListText(const Json& features) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < features.items.size(); ++i) {
        if (i) out << ", ";
        out << scalarText(features.items[i]);
    }
    out << "]";
    return out.str();
}

bool numericVectorForFact(const Json& fact, const Json& features, std::vector<double>& out) {
    if (fact.kind != Json::Object || features.kind != Json::Array) return false;
    out.clear();
    for (const auto& feature : features.items) {
        const std::string key = scalarText(feature);
        const Json* value = field(fact, key);
        if (!value || value->kind != Json::Number || !std::isfinite(value->number)) return false;
        out.push_back(value->number);
    }
    return !out.empty();
}

double squaredDistance(const std::vector<double>& left, const std::vector<double>& right) {
    if (left.size() != right.size() || left.empty()) return std::numeric_limits<double>::infinity();
    double total = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        const double delta = left[i] - right[i];
        total += delta * delta;
    }
    return total;
}

std::string clusterFactsResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    const Json* features = field(args, "features");
    const size_t wantedClusters = static_cast<size_t>(std::max(1.0, asNumber(field(args, "clusters"), 2.0)));
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"ml.cluster_facts expects non-empty facts array\"}";
    if (!features || features->kind != Json::Array || features->items.empty()) return "{\"error\":\"ml.cluster_facts expects non-empty features array\"}";

    std::vector<size_t> sourceIndexes;
    std::vector<std::vector<double>> vectors;
    for (size_t i = 0; i < facts->items.size(); ++i) {
        std::vector<double> vector;
        if (numericVectorForFact(facts->items[i], *features, vector)) {
            sourceIndexes.push_back(i);
            vectors.push_back(std::move(vector));
        }
    }
    if (vectors.empty()) return "{\"error\":\"ml.cluster_facts found no facts with all requested numeric features\"}";

    std::vector<double> means(vectors[0].size(), 0.0);
    std::vector<double> stddevs(vectors[0].size(), 0.0);
    for (const auto& vector : vectors) {
        for (size_t d = 0; d < vector.size(); ++d) means[d] += vector[d];
    }
    for (double& mean : means) mean /= static_cast<double>(vectors.size());
    for (const auto& vector : vectors) {
        for (size_t d = 0; d < vector.size(); ++d) {
            const double delta = vector[d] - means[d];
            stddevs[d] += delta * delta;
        }
    }
    for (double& stddev : stddevs) {
        stddev = std::sqrt(stddev / static_cast<double>(vectors.size()));
        if (stddev <= 1e-12 || !std::isfinite(stddev)) stddev = 1.0;
    }
    std::vector<std::vector<double>> scaledVectors = vectors;
    for (auto& vector : scaledVectors) {
        for (size_t d = 0; d < vector.size(); ++d) vector[d] = (vector[d] - means[d]) / stddevs[d];
    }

    const size_t k = std::min(wantedClusters, vectors.size());
    std::vector<std::vector<double>> centroids;
    for (size_t i = 0; i < k; ++i) centroids.push_back(scaledVectors[i]);
    std::vector<size_t> assignment(scaledVectors.size(), 0);
    for (size_t iteration = 0; iteration < 12; ++iteration) {
        bool changed = false;
        for (size_t i = 0; i < scaledVectors.size(); ++i) {
            size_t best = 0;
            double bestDistance = squaredDistance(scaledVectors[i], centroids[0]);
            for (size_t c = 1; c < k; ++c) {
                const double distance = squaredDistance(scaledVectors[i], centroids[c]);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = c;
                }
            }
            if (assignment[i] != best) {
                assignment[i] = best;
                changed = true;
            }
        }
        std::vector<std::vector<double>> next(k, std::vector<double>(scaledVectors[0].size(), 0.0));
        std::vector<size_t> counts(k, 0);
        for (size_t i = 0; i < scaledVectors.size(); ++i) {
            ++counts[assignment[i]];
            for (size_t d = 0; d < scaledVectors[i].size(); ++d) next[assignment[i]][d] += scaledVectors[i][d];
        }
        for (size_t c = 0; c < k; ++c) {
            if (counts[c] == 0) continue;
            for (double& value : next[c]) value /= static_cast<double>(counts[c]);
            centroids[c] = next[c];
        }
        if (!changed) break;
    }

    std::ostringstream clustersOut;
    clustersOut << "[";
    for (size_t c = 0; c < k; ++c) {
        if (c) clustersOut << ",";
        clustersOut << "{\"id\":" << c << ",\"centroid\":{";
        for (size_t f = 0; f < features->items.size(); ++f) {
            if (f) clustersOut << ",";
            clustersOut << q(scalarText(features->items[f])) << ":" << ((centroids[c][f] * stddevs[f]) + means[f]);
        }
        clustersOut << "},\"members\":[";
        bool firstMember = true;
        for (size_t i = 0; i < scaledVectors.size(); ++i) {
            if (assignment[i] != c) continue;
            if (!firstMember) clustersOut << ",";
            firstMember = false;
            clustersOut << "{\"index\":" << sourceIndexes[i]
                        << ",\"distance\":" << std::sqrt(squaredDistance(scaledVectors[i], centroids[c]))
                        << ",\"fact\":" << jsonText(facts->items[sourceIndexes[i]]) << "}";
        }
        clustersOut << "]}";
    }
    clustersOut << "]";

    std::ostringstream modelFx;
    modelFx << "KMeansFactModel(\n"
            << "    name: \"GeneratedKMeansFactModel\",\n"
            << "    clusters: " << k << ",\n"
            << "    features: \"" << fxEscape(featureListText(*features)) << "\",\n"
            << "    method: \"k_means_numeric_facts\"\n"
            << ").\n";

    std::ostringstream out;
    out << "{\"model_type\":\"k_means\""
        << ",\"method\":\"k_means_numeric_facts\""
        << ",\"features\":" << jsonText(*features)
        << ",\"clusters\":" << clustersOut.str()
        << ",\"trained_count\":" << vectors.size()
        << ",\"ignored_count\":" << (facts->items.size() - vectors.size())
        << ",\"scaling\":\"standardized\""
        << ",\"model_fx\":" << q(modelFx.str())
        << ",\"warnings\":[]}";
    return out.str();
}

std::string discoverAssociationsResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    const double minSupport = clamp01(asNumber(field(args, "min_support"), 0.2));
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"ml.discover_associations expects non-empty facts array\"}";

    std::map<std::string, size_t> itemCounts;
    std::map<std::string, std::map<std::string, size_t>> pairCounts;
    size_t usable = 0;
    for (const auto& fact : facts->items) {
        if (fact.kind != Json::Object) continue;
        std::vector<std::string> items;
        for (const auto& entry : fact.fields) {
            if (entry.first == "__type" || entry.first == "__parent") continue;
            if (entry.second.kind != Json::String && entry.second.kind != Json::Number) continue;
            items.push_back(entry.first + "=" + scalarText(entry.second));
        }
        if (items.empty()) continue;
        ++usable;
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        for (const auto& item : items) itemCounts[item]++;
        for (size_t i = 0; i < items.size(); ++i) {
            for (size_t j = 0; j < items.size(); ++j) {
                if (i == j) continue;
                pairCounts[items[i]][items[j]]++;
            }
        }
    }
    if (usable == 0) return "{\"error\":\"ml.discover_associations found no scalar fact properties\"}";

    std::ostringstream associations;
    associations << "[";
    bool first = true;
    for (const auto& left : pairCounts) {
        const auto countIt = itemCounts.find(left.first);
        if (countIt == itemCounts.end() || countIt->second == 0) continue;
        for (const auto& right : left.second) {
            const double support = static_cast<double>(right.second) / static_cast<double>(usable);
            if (support < minSupport) continue;
            const double confidence = static_cast<double>(right.second) / static_cast<double>(countIt->second);
            if (!first) associations << ",";
            first = false;
            associations << "{\"when\":" << q(left.first)
                         << ",\"then\":" << q(right.first)
                         << ",\"support\":" << support
                         << ",\"confidence\":" << confidence
                         << ",\"evidence_count\":" << right.second << "}";
        }
    }
    associations << "]";

    std::ostringstream out;
    out << "{\"method\":\"key_value_association_mining\""
        << ",\"fact_count\":" << facts->items.size()
        << ",\"usable_count\":" << usable
        << ",\"min_support\":" << minSupport
        << ",\"associations\":" << associations.str()
        << ",\"warnings\":[]}";
    return out.str();
}

std::string profileFactsResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    const Json* features = field(args, "features");
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"ml.profile_facts expects non-empty facts array\"}";
    if (!features || features->kind != Json::Array || features->items.empty()) return "{\"error\":\"ml.profile_facts expects non-empty features array\"}";

    std::ostringstream stats;
    stats << "{";
    bool firstFeature = true;
    for (const auto& feature : features->items) {
        const std::string key = scalarText(feature);
        std::vector<double> values;
        for (const auto& fact : facts->items) {
            const Json* value = field(fact, key);
            if (value && value->kind == Json::Number && std::isfinite(value->number)) values.push_back(value->number);
        }
        if (!firstFeature) stats << ",";
        firstFeature = false;
        stats << q(key) << ":{";
        if (values.empty()) {
            stats << "\"count\":0,\"missing\":" << facts->items.size() << "}";
            continue;
        }
        const double sum = std::accumulate(values.begin(), values.end(), 0.0);
        const double mean = sum / static_cast<double>(values.size());
        double variance = 0.0;
        for (double value : values) {
            const double delta = value - mean;
            variance += delta * delta;
        }
        variance /= static_cast<double>(values.size());
        const auto minmax = std::minmax_element(values.begin(), values.end());
        stats << "\"count\":" << values.size()
              << ",\"missing\":" << (facts->items.size() - values.size())
              << ",\"min\":" << *minmax.first
              << ",\"max\":" << *minmax.second
              << ",\"mean\":" << mean
              << ",\"variance\":" << variance
              << ",\"stddev\":" << std::sqrt(variance) << "}";
    }
    stats << "}";
    std::ostringstream out;
    out << "{\"method\":\"numeric_fact_profile\""
        << ",\"fact_count\":" << facts->items.size()
        << ",\"features\":" << jsonText(*features)
        << ",\"stats\":" << stats.str()
        << ",\"warnings\":[]}";
    return out.str();
}

bool pairedNumericValues(const Json& facts, const std::string& leftKey, const std::string& rightKey, std::vector<double>& left, std::vector<double>& right) {
    if (facts.kind != Json::Array) return false;
    for (const auto& fact : facts.items) {
        const Json* l = field(fact, leftKey);
        const Json* r = field(fact, rightKey);
        if (l && r && l->kind == Json::Number && r->kind == Json::Number && std::isfinite(l->number) && std::isfinite(r->number)) {
            left.push_back(l->number);
            right.push_back(r->number);
        }
    }
    return !left.empty() && left.size() == right.size();
}

std::string correlateFactsResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    const std::string leftKey = stringField(args, "left");
    const std::string rightKey = stringField(args, "right");
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"ml.correlate_facts expects non-empty facts array\"}";
    if (leftKey.empty() || rightKey.empty()) return "{\"error\":\"ml.correlate_facts expects left and right feature names\"}";
    std::vector<double> left;
    std::vector<double> right;
    if (!pairedNumericValues(*facts, leftKey, rightKey, left, right)) return "{\"error\":\"ml.correlate_facts found no paired numeric values\"}";
    const double meanLeft = std::accumulate(left.begin(), left.end(), 0.0) / static_cast<double>(left.size());
    const double meanRight = std::accumulate(right.begin(), right.end(), 0.0) / static_cast<double>(right.size());
    double numerator = 0.0;
    double leftVariance = 0.0;
    double rightVariance = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        const double dl = left[i] - meanLeft;
        const double dr = right[i] - meanRight;
        numerator += dl * dr;
        leftVariance += dl * dl;
        rightVariance += dr * dr;
    }
    const double denom = std::sqrt(leftVariance * rightVariance);
    const double correlation = denom == 0.0 ? 0.0 : numerator / denom;
    std::ostringstream out;
    out << "{\"method\":\"pearson_fact_correlation\""
        << ",\"left\":" << q(leftKey)
        << ",\"right\":" << q(rightKey)
        << ",\"pairs\":" << left.size()
        << ",\"correlation\":" << correlation
        << ",\"strength\":" << q(std::abs(correlation) >= 0.75 ? "strong" : (std::abs(correlation) >= 0.4 ? "moderate" : "weak"))
        << ",\"warnings\":[]}";
    return out.str();
}

std::string trainLinearRegressionResponse(const Json& args) {
    const Json* facts = field(args, "facts");
    const std::string target = stringField(args, "target");
    const std::string featureName = stringField(args, "feature");
    if (!facts || facts->kind != Json::Array || facts->items.empty()) return "{\"error\":\"ml.train_linear_regression expects non-empty facts array\"}";
    if (target.empty() || featureName.empty()) return "{\"error\":\"ml.train_linear_regression expects target and feature\"}";
    std::vector<double> x;
    std::vector<double> y;
    if (!pairedNumericValues(*facts, featureName, target, x, y)) return "{\"error\":\"ml.train_linear_regression found no paired numeric feature and target values\"}";
    const double meanX = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
    const double meanY = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(y.size());
    double numerator = 0.0;
    double denominator = 0.0;
    double totalVariance = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        numerator += (x[i] - meanX) * (y[i] - meanY);
        denominator += (x[i] - meanX) * (x[i] - meanX);
        totalVariance += (y[i] - meanY) * (y[i] - meanY);
    }
    if (denominator == 0.0) return "{\"error\":\"ml.train_linear_regression feature has zero variance\"}";
    const double slope = numerator / denominator;
    const double intercept = meanY - slope * meanX;
    double residual = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double predicted = intercept + slope * x[i];
        const double delta = y[i] - predicted;
        residual += delta * delta;
    }
    const double rSquared = totalVariance == 0.0 ? 1.0 : clamp01(1.0 - residual / totalVariance);

    std::ostringstream modelFx;
    modelFx << "LinearRegressionModel(\n"
            << "    name: \"GeneratedLinearRegressionModel\",\n"
            << "    target: \"" << fxEscape(target) << "\",\n"
            << "    feature: \"" << fxEscape(featureName) << "\",\n"
            << "    interceptMagnitude: " << std::abs(intercept) << ",\n"
            << "    interceptSign: \"" << (intercept < 0.0 ? "negative" : "positive") << "\",\n"
            << "    slopeMagnitude: " << std::abs(slope) << ",\n"
            << "    slopeSign: \"" << (slope < 0.0 ? "negative" : "positive") << "\",\n"
            << "    rSquared: " << rSquared << ",\n"
            << "    method: \"ordinary_least_squares_single_feature\"\n"
            << ").\n";

    std::ostringstream out;
    out << "{\"model_type\":\"linear_regression\""
        << ",\"method\":\"ordinary_least_squares_single_feature\""
        << ",\"target\":" << q(target)
        << ",\"feature\":" << q(featureName)
        << ",\"intercept\":" << intercept
        << ",\"slope\":" << slope
        << ",\"r_squared\":" << rSquared
        << ",\"trained_count\":" << x.size()
        << ",\"model_fx\":" << q(modelFx.str())
        << ",\"warnings\":[]}";
    return out.str();
}

std::string dispatch(const std::string& function, const Json& args) {
    std::string call = function;
    const std::string prefix = "system:flibrary:fact:";
    if (call.rfind(prefix, 0) == 0) call = "fact:" + call.substr(prefix.size());
    const std::string analysisPrefix = "system:flibrary:fact_analysis:";
    if (call.rfind(analysisPrefix, 0) == 0) call = "fact_analysis:" + call.substr(analysisPrefix.size());
    if (call == "fact:extract_semantics") return extractSemantics(args);
    if (call == "fact:check_similarity" || call == "fact:compare") return similarityResponse(args);
    if (call == "fact:compare_facts") return factCompareResponse(args);
    if (call == "fact:similarity_score") return similarityResponse(args, true);
    if (call == "fact:check_difference") return differenceResponse(args);
    if (call == "fact:align_fields") return alignFieldsResponse(args);
    if (call == "fact:match_pattern") return patternResponse(args, false);
    if (call == "fact:semantic_unify") return patternResponse(args, true);
    if (call == "fact:near") return nearResponse(args);
    if (call == "fact:compare_properties") return propertyCompareResponse(args);
    if (call == "fact:property_difference") return propertyDifferenceResponse(args);
    if (call == "fact:contains_subfact") return containsSubfactResponse(args);
    if (call == "fact:relation_properties") return relationPropertiesResponse(args);
    if (call == "fact:nearest_subfacts") return nearestSubfactsResponse(args);
    if (call == "fact:common_ancestor") return commonAncestorResponse(args);
    if (call == "fact:direct_relation") return directRelationResponse(args);
    if (call == "fact:is_ancestor") return ancestryResponse(args, false);
    if (call == "fact:is_descendant") return ancestryResponse(args, true);
    if (call == "fact:ancestor_closure") return closureResponse(args, false);
    if (call == "fact:descendant_closure") return closureResponse(args, true);
    if (call == "fact:shortest_path") return shortestPathResponse(args);
    if (call == "fact:path_similarity") return graphSimilarityResponse(args, "path");
    if (call == "fact:wu_palmer_similarity") return graphSimilarityResponse(args, "wu_palmer");
    if (call == "fact:resnik_similarity") return graphSimilarityResponse(args, "resnik");
    if (call == "fact:lin_similarity") return graphSimilarityResponse(args, "lin");
    if (call == "fact:frequency_statistics") return frequencyStatsResponse(args);
    if (call == "fact:normalize") return normalizeResponse(args);
    if (call == "fact_analysis:find_nearest" || call == "fact_analysis:find_nearest_where") return findNearestResponse(args);
    if (call == "fact_analysis:predict_next") return predictNextResponse(args);
    if (call == "fact_analysis:train_decision_tree") return trainDecisionTreeResponse(args);
    if (call == "fact_analysis:predict" || call == "fact_analysis:apply_model") return predictWithModelResponse(args);
    if (call == "fact_analysis:evaluate_model") return evaluateModelResponse(args);
    if (call == "fact_analysis:cluster_facts") return clusterFactsResponse(args);
    if (call == "fact_analysis:discover_associations") return discoverAssociationsResponse(args);
    if (call == "fact_analysis:profile_facts") return profileFactsResponse(args);
    if (call == "fact_analysis:correlate_facts") return correlateFactsResponse(args);
    if (call == "fact_analysis:train_linear_regression") return trainLinearRegressionResponse(args);
    return "{\"error\":\"Unsupported fact native function\"}";
}

} // namespace

extern "C" FELIDAE_FACT_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson) {
    try {
        Json args = parseJson(argsJson ? argsJson : "{}");
        return copyResponse(dispatch(functionName ? functionName : "", args));
    } catch (const std::exception& ex) {
        return copyResponse(std::string("{\"error\":\"") + esc(ex.what()) + "\"}");
    } catch (...) {
        return copyResponse("{\"error\":\"Unknown native fact module failure\"}");
    }
}

extern "C" FELIDAE_FACT_EXPORT void felidae_native_free(char* ptr) {
    std::free(ptr);
}
