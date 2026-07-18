#include "wordnet.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    return parseNumber(s, p, out);
}

Json parseJson(const std::string& text) {
    Json out;
    size_t p = 0;
    if (!parseValue(text, p, out)) return Json{};
    return out;
}

const Json* field(const Json& object, const std::string& key) {
    if (object.kind != Json::Object) return nullptr;
    auto it = object.fields.find(key);
    return it == object.fields.end() ? nullptr : &it->second;
}

std::string asString(const Json* value, const std::string& fallback = {}) {
    if (!value) return fallback;
    if (value->kind == Json::String) return value->text;
    if (value->kind == Json::Number) {
        std::ostringstream out;
        out << value->number;
        return out.str();
    }
    return fallback;
}

double asNumber(const Json* value, double fallback = 0.0) {
    if (!value) return fallback;
    if (value->kind == Json::Number) return value->number;
    if (value->kind == Json::String) {
        try { return std::stod(value->text); } catch (...) { return fallback; }
    }
    return fallback;
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

char* copyResponse(const std::string& response) {
    char* buffer = static_cast<char*>(std::malloc(response.size() + 1));
    if (!buffer) return nullptr;
    std::memcpy(buffer, response.c_str(), response.size() + 1);
    return buffer;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string norm(const std::string& text) {
    std::string out;
    bool space = false;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            space = false;
        } else if (!space && !out.empty()) {
            out.push_back(' ');
            space = true;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(norm(text));
    std::string word;
    while (in >> word) if (word.size() > 1) out.push_back(word);
    return out;
}

struct Sense {
    std::string id;
    std::string lemma;
    std::string synset;
    double frequency = 0;
};

struct Lemma {
    std::string id;
    std::string text;
    std::string language = "en";
};

struct Edge { std::string type; std::string from; std::string to; };

struct Index {
    std::unordered_map<std::string, std::string> synsetPos;
    std::unordered_map<std::string, Lemma> lemmas;
    std::unordered_map<std::string, Sense> senses;
    std::unordered_map<std::string, std::vector<std::string>> sensesByLemma;
    std::unordered_map<std::string, std::vector<std::string>> sensesBySynset;
    std::unordered_map<std::string, std::string> glosses;
    std::unordered_map<std::string, std::vector<std::string>> examples;
    std::unordered_map<std::string, std::string> morph;
    std::unordered_map<std::string, double> freq;
    std::unordered_map<std::string, std::vector<Edge>> out;
    std::unordered_map<std::string, std::vector<Edge>> in;
};

std::string fstr(const Json& fact, const std::string& key, const std::string& fallback = {}) {
    return asString(field(fact, key), fallback);
}

double fnum(const Json& fact, const std::string& key, double fallback = 0.0) {
    return asNumber(field(fact, key), fallback);
}

void edge(Index& idx, std::string type, std::string from, std::string to) {
    if (from.empty() || to.empty()) return;
    Edge e{std::move(type), std::move(from), std::move(to)};
    idx.out[e.from].push_back(e);
    idx.in[e.to].push_back(e);
}

Index buildIndex(const Json& args) {
    Index idx;
    const Json* facts = field(args, "__facts");
    if (!facts || facts->kind != Json::Array) return idx;
    for (const Json& fact : facts->items) {
        std::string type = fstr(fact, "__type");
        if (type == "Synset") {
            idx.synsetPos[fstr(fact, "id")] = fstr(fact, "pos");
        } else if (type == "Lemma") {
            Lemma l{fstr(fact, "id"), fstr(fact, "text"), fstr(fact, "language", "en")};
            if (l.id.empty()) l.id = l.language + ":" + norm(l.text);
            if (!l.text.empty()) idx.lemmas[l.id] = l;
        } else if (type == "Sense") {
            Sense s{fstr(fact, "id"), fstr(fact, "lemma"), fstr(fact, "synset"), fnum(fact, "frequency")};
            if (s.id.empty()) s.id = s.lemma + "@" + s.synset;
            idx.senses[s.id] = s;
            idx.sensesByLemma[s.lemma].push_back(s.id);
            idx.sensesBySynset[s.synset].push_back(s.id);
        } else if (type == "Gloss") {
            idx.glosses[fstr(fact, "synset")] = fstr(fact, "text");
        } else if (type == "Example") {
            idx.examples[fstr(fact, "synset")].push_back(fstr(fact, "text"));
        } else if (type == "MorphException") {
            idx.morph[norm(fstr(fact, "surface"))] = norm(fstr(fact, "lemma"));
        } else if (type == "ConceptFrequency") {
            idx.freq[fstr(fact, "synset")] += fnum(fact, "count");
        } else if (type == "Hypernym") {
            edge(idx, "hypernym", fstr(fact, "child"), fstr(fact, "parent"));
        } else if (type == "SimilarTo") {
            edge(idx, "similar_to", fstr(fact, "left"), fstr(fact, "right"));
            edge(idx, "similar_to", fstr(fact, "right"), fstr(fact, "left"));
        } else if (type == "Antonym") {
            edge(idx, "antonym", fstr(fact, "left"), fstr(fact, "right"));
            edge(idx, "antonym", fstr(fact, "right"), fstr(fact, "left"));
        } else if (type.find("Meronym") != std::string::npos) {
            edge(idx, lower(type), fstr(fact, "whole"), fstr(fact, "part"));
        } else if (type == "Entails" || type == "Causes" || type == "DerivedFrom") {
            edge(idx, lower(type), fstr(fact, "source", fstr(fact, "left")), fstr(fact, "target", fstr(fact, "right")));
        }
    }
    for (auto& item : idx.sensesByLemma) {
        std::sort(item.second.begin(), item.second.end(), [&](const std::string& a, const std::string& b) {
            return idx.senses[a].frequency > idx.senses[b].frequency;
        });
    }
    return idx;
}

std::vector<std::string> lemmaIds(const Index& idx, const std::string& text, const std::string& lang) {
    std::vector<std::string> ids;
    std::string wanted = norm(text);
    for (const auto& item : idx.lemmas) {
        if (norm(item.second.text) == wanted && (lang.empty() || item.second.language == lang)) ids.push_back(item.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> lemmatizeWords(const Index& idx, const std::string& text, const std::string& lang) {
    std::vector<std::string> out;
    auto push = [&](std::string v) {
        if (!v.empty() && std::find(out.begin(), out.end(), v) == out.end() && !lemmaIds(idx, v, lang).empty()) out.push_back(v);
    };
    std::string base = norm(text);
    auto ex = idx.morph.find(base);
    if (ex != idx.morph.end()) push(ex->second);
    push(base);
    if (base.size() > 3 && base.substr(base.size() - 3) == "ies") push(base.substr(0, base.size() - 3) + "y");
    if (base.size() > 2 && base.back() == 's') push(base.substr(0, base.size() - 1));
    if (out.empty()) out.push_back(base);
    return out;
}

std::vector<std::string> resolveSynsets(const Index& idx, const Json& args, const std::string& prefix) {
    std::vector<std::string> out;
    auto push = [&](const std::string& s) {
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    };
    push(asString(field(args, prefix + "synset")));
    push(asString(field(args, prefix + "synset_id")));
    std::string word = asString(field(args, prefix + "word"), asString(field(args, prefix)));
    const Json* fact = field(args, prefix + "fact");
    if (word.empty() && fact) {
        for (const std::string key : {"synset", "synset_id", "word", "text", "name"}) {
            word = asString(field(*fact, key));
            if (!word.empty()) break;
        }
    }
    if (word.empty()) {
        const Json* direct = field(args, prefix);
        if (direct && direct->kind == Json::Object) {
            for (const std::string key : {"synset", "synset_id", "word", "text", "name"}) {
                word = asString(field(*direct, key));
                if (!word.empty()) break;
            }
        }
    }
    std::string lang = asString(field(args, "language"), "en");
    for (const auto& lemmaText : lemmatizeWords(idx, word, lang)) {
        for (const auto& lemma : lemmaIds(idx, lemmaText, lang)) {
            auto senses = idx.sensesByLemma.find(lemma);
            if (senses == idx.sensesByLemma.end()) continue;
            for (const auto& sense : senses->second) push(idx.senses.at(sense).synset);
        }
    }
    return out;
}

std::vector<Edge> neighbors(const Index& idx, const std::string& synset, const std::string& rel, const std::string& dir) {
    std::vector<Edge> out;
    auto match = [&](const Edge& e) { return rel.empty() || rel == "all" || rel == "related" || e.type == rel; };
    if (dir != "in") {
        auto it = idx.out.find(synset);
        if (it != idx.out.end()) for (const auto& e : it->second) if (match(e)) out.push_back(e);
    }
    if (dir != "out") {
        auto it = idx.in.find(synset);
        if (it != idx.in.end()) for (const auto& e : it->second) if (match(e)) out.push_back(Edge{e.type, e.to, e.from});
    }
    return out;
}

std::string stringArray(const std::vector<std::string>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += q(values[i]);
    }
    return out + "]";
}

std::vector<std::string> pathTo(const std::unordered_map<std::string, std::string>& parent, std::string node) {
    std::vector<std::string> path;
    while (!node.empty()) {
        path.push_back(node);
        auto it = parent.find(node);
        if (it == parent.end()) break;
        node = it->second;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

int shortest(const Index& idx, const std::string& left, const std::string& right, std::vector<std::string>* path = nullptr) {
    if (left == right) { if (path) *path = {left}; return 0; }
    std::deque<std::pair<std::string, int>> qd{{left, 0}};
    std::unordered_set<std::string> seen{left};
    std::unordered_map<std::string, std::string> parent;
    while (!qd.empty() && seen.size() < 4096) {
        auto [cur, depth] = qd.front();
        qd.pop_front();
        for (const auto& e : neighbors(idx, cur, "hypernym", "both")) {
            if (!seen.insert(e.to).second) continue;
            parent[e.to] = cur;
            if (e.to == right) {
                if (path) *path = pathTo(parent, right);
                return depth + 1;
            }
            qd.push_back({e.to, depth + 1});
        }
    }
    return -1;
}

std::vector<std::string> ancestors(const Index& idx, const std::string& synset) {
    std::vector<std::string> out;
    std::deque<std::string> qd{synset};
    std::unordered_set<std::string> seen{synset};
    while (!qd.empty()) {
        std::string cur = qd.front();
        qd.pop_front();
        for (const auto& e : neighbors(idx, cur, "hypernym", "out")) {
            if (seen.insert(e.to).second) {
                out.push_back(e.to);
                qd.push_back(e.to);
            }
        }
    }
    return out;
}

int depth(const Index& idx, const std::string& synset) {
    int best = 1;
    for (const auto& a : ancestors(idx, synset)) {
        int d = shortest(idx, synset, a);
        if (d >= 0) best = std::max(best, d + 1);
    }
    return best;
}

std::vector<std::pair<std::string, int>> commonAncestors(const Index& idx, const std::string& left, const std::string& right) {
    auto la = ancestors(idx, left);
    auto ra = ancestors(idx, right);
    la.push_back(left);
    ra.push_back(right);
    std::unordered_set<std::string> rs(ra.begin(), ra.end());
    std::vector<std::pair<std::string, int>> out;
    for (const auto& a : la) if (rs.count(a)) out.push_back({a, depth(idx, a)});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

double conceptCount(const Index& idx, const std::string& synset, std::unordered_set<std::string>& seen) {
    if (!seen.insert(synset).second) return 0.0;
    double count = idx.freq.count(synset) ? idx.freq.at(synset) : 0.0;
    for (const auto& e : neighbors(idx, synset, "hypernym", "in")) count += conceptCount(idx, e.to, seen);
    return count;
}

double ic(const Index& idx, const std::string& synset) {
    double total = 0.0;
    for (const auto& item : idx.freq) total += item.second;
    if (total <= 0) total = std::max<size_t>(1, idx.synsetPos.size());
    std::unordered_set<std::string> seen;
    double p = (conceptCount(idx, synset, seen) + 1.0) / (total + std::max<size_t>(1, idx.synsetPos.size()));
    return -std::log(std::max(1e-12, p));
}

std::string synsetRow(const Index& idx, const std::string& synset) {
    std::string lemmas = "[";
    bool first = true;
    auto senses = idx.sensesBySynset.find(synset);
    if (senses != idx.sensesBySynset.end()) {
        for (const auto& sid : senses->second) {
            auto lemma = idx.lemmas.find(idx.senses.at(sid).lemma);
            if (lemma == idx.lemmas.end()) continue;
            if (!first) lemmas += ",";
            first = false;
            lemmas += q(lemma->second.text);
        }
    }
    lemmas += "]";
    std::string gloss = idx.glosses.count(synset) ? idx.glosses.at(synset) : "";
    std::string pos = idx.synsetPos.count(synset) ? idx.synsetPos.at(synset) : "";
    return "{\"synset\":" + q(synset) + ",\"pos\":" + q(pos) + ",\"lemmas\":" + lemmas + ",\"gloss\":" + q(gloss) + "}";
}

std::string lookup(const Index& idx, const Json& args) {
    std::string text = asString(field(args, "text"), asString(field(args, "word")));
    std::string lang = asString(field(args, "language"), "en");
    std::string pos = asString(field(args, "pos"));
    std::string out = "[";
    bool first = true;
    for (const auto& lid : lemmaIds(idx, text, lang)) {
        auto senses = idx.sensesByLemma.find(lid);
        if (senses == idx.sensesByLemma.end()) continue;
        for (const auto& sid : senses->second) {
            const Sense& s = idx.senses.at(sid);
            std::string spos = idx.synsetPos.count(s.synset) ? idx.synsetPos.at(s.synset) : "";
            if (!pos.empty() && spos != pos) continue;
            const Lemma& l = idx.lemmas.at(lid);
            if (!first) out += ",";
            first = false;
            out += "{\"lemma\":" + q(l.id) + ",\"text\":" + q(l.text) + ",\"language\":" + q(l.language) +
                   ",\"sense\":" + q(s.id) + ",\"synset\":" + q(s.synset) + ",\"pos\":" + q(spos) +
                   ",\"frequency\":" + std::to_string(s.frequency) + "}";
        }
    }
    return out + "]";
}

std::string lemmatize(const Index& idx, const Json& args) {
    std::string text = asString(field(args, "text"), asString(field(args, "word")));
    std::string lang = asString(field(args, "language"), "en");
    std::string out = "[";
    auto lemmas = lemmatizeWords(idx, text, lang);
    for (size_t i = 0; i < lemmas.size(); ++i) {
        if (i) out += ",";
        out += "{\"lemma\":" + q(lemmas[i]) + ",\"language\":" + q(lang) + ",\"source\":" + q(norm(text) == lemmas[i] ? "identity" : "morphology") + "}";
    }
    return out + "]";
}

std::string traverse(const Index& idx, const Json& args, std::string defaultDir) {
    std::string start = asString(field(args, "synset"));
    if (start.empty()) {
        auto synsets = resolveSynsets(idx, args, "word");
        if (!synsets.empty()) start = synsets.front();
    }
    std::string rel = asString(field(args, "relation"), "hypernym");
    std::string dir = asString(field(args, "direction"), defaultDir);
    int maxDepth = static_cast<int>(asNumber(field(args, "max_depth"), 32));
    std::deque<std::pair<std::string, int>> qd{{start, 0}};
    std::unordered_set<std::string> seen{start};
    std::unordered_map<std::string, std::string> parent;
    std::string out = "[";
    bool first = true;
    while (!qd.empty()) {
        auto [cur, d] = qd.front();
        qd.pop_front();
        if (d > 0) {
            if (!first) out += ",";
            first = false;
            out += "{\"synset\":" + q(cur) + ",\"depth\":" + std::to_string(d) + ",\"path\":" + stringArray(pathTo(parent, cur)) + "}";
        }
        if (d >= maxDepth) continue;
        for (const auto& e : neighbors(idx, cur, rel, dir)) {
            if (seen.insert(e.to).second) {
                parent[e.to] = cur;
                qd.push_back({e.to, d + 1});
            }
        }
    }
    return out + "]";
}

std::string shortestPath(const Index& idx, const Json& args) {
    auto left = resolveSynsets(idx, args, "left_"); if (left.empty()) left = resolveSynsets(idx, args, "left");
    auto right = resolveSynsets(idx, args, "right_"); if (right.empty()) right = resolveSynsets(idx, args, "right");
    std::vector<std::string> path;
    int dist = (!left.empty() && !right.empty()) ? shortest(idx, left.front(), right.front(), &path) : -1;
    return "{\"left\":" + q(left.empty() ? "" : left.front()) + ",\"right\":" + q(right.empty() ? "" : right.front()) +
           ",\"distance\":" + std::to_string(dist) + ",\"path\":" + stringArray(path) + "}";
}

std::string lcs(const Index& idx, const Json& args) {
    auto left = resolveSynsets(idx, args, "left_"); if (left.empty()) left = resolveSynsets(idx, args, "left");
    auto right = resolveSynsets(idx, args, "right_"); if (right.empty()) right = resolveSynsets(idx, args, "right");
    std::string out = "[";
    bool first = true;
    if (!left.empty() && !right.empty()) {
        for (const auto& item : commonAncestors(idx, left.front(), right.front())) {
            if (!first) out += ",";
            first = false;
            out += "{\"synset\":" + q(item.first) + ",\"depth\":" + std::to_string(item.second) + ",\"ic\":" + std::to_string(ic(idx, item.first)) + "}";
        }
    }
    return out + "]";
}

std::string similarity(const Index& idx, const Json& args) {
    auto left = resolveSynsets(idx, args, "left_"); if (left.empty()) left = resolveSynsets(idx, args, "left"); if (left.empty()) left = resolveSynsets(idx, args, "word1"); if (left.empty()) left = resolveSynsets(idx, args, "fact1");
    auto right = resolveSynsets(idx, args, "right_"); if (right.empty()) right = resolveSynsets(idx, args, "right"); if (right.empty()) right = resolveSynsets(idx, args, "word2"); if (right.empty()) right = resolveSynsets(idx, args, "fact2");
    std::string algorithm = lower(asString(field(args, "algorithm"), "path"));
    if (left.empty() || right.empty()) return "{\"score\":0,\"algorithm\":" + q(algorithm) + ",\"reason\":\"unresolved synset\"}";
    int dist = shortest(idx, left.front(), right.front());
    auto common = commonAncestors(idx, left.front(), right.front());
    std::string lcsId = common.empty() ? "" : common.front().first;
    double score = dist < 0 ? 0.0 : 1.0 / (dist + 1.0);
    double lcsIc = lcsId.empty() ? 0.0 : ic(idx, lcsId);
    if (algorithm == "wup" || algorithm == "wu_palmer" || algorithm == "wu-palmer") {
        score = (2.0 * (common.empty() ? 0.0 : common.front().second)) / (depth(idx, left.front()) + depth(idx, right.front()));
    } else if (algorithm == "resnik") {
        score = lcsIc;
    } else if (algorithm == "lin") {
        double denom = ic(idx, left.front()) + ic(idx, right.front());
        score = denom <= 0 ? 0 : (2.0 * lcsIc) / denom;
    } else if (algorithm == "jcn" || algorithm == "jiang_conrath") {
        score = 1.0 / (ic(idx, left.front()) + ic(idx, right.front()) - 2.0 * lcsIc + 1.0);
    }
    return "{\"score\":" + std::to_string(score) + ",\"algorithm\":" + q(algorithm) +
           ",\"details\":{\"left\":" + q(left.front()) + ",\"right\":" + q(right.front()) +
           ",\"distance\":" + std::to_string(dist) + ",\"lcs\":" + q(lcsId) + "}}";
}

std::string synonyms(const Index& idx, const Json& args) {
    std::string lang = asString(field(args, "language"), "en");
    auto synsets = resolveSynsets(idx, args, "word"); if (synsets.empty()) synsets = resolveSynsets(idx, args, "left"); if (synsets.empty()) synsets = resolveSynsets(idx, args, "");
    std::string exclude = norm(asString(field(args, "word"), asString(field(args, "left"))));
    std::string out = "[";
    bool first = true;
    for (const auto& synset : synsets) {
        auto senses = idx.sensesBySynset.find(synset);
        if (senses == idx.sensesBySynset.end()) continue;
        for (const auto& sid : senses->second) {
            auto lemma = idx.lemmas.find(idx.senses.at(sid).lemma);
            if (lemma == idx.lemmas.end() || lemma->second.language != lang || norm(lemma->second.text) == exclude) continue;
            if (!first) out += ",";
            first = false;
            out += "{\"synset\":" + q(synset) + ",\"lemma\":" + q(lemma->second.id) + ",\"text\":" + q(lemma->second.text) + "}";
        }
    }
    return out + "]";
}

std::string disambiguate(const Index& idx, const Json& args) {
    std::string word = asString(field(args, "word"), asString(field(args, "text")));
    std::set<std::string> ctx;
    for (const auto& token : tokenize(asString(field(args, "context")))) ctx.insert(token);
    std::string best;
    int bestScore = -1;
    for (const auto& synset : resolveSynsets(idx, args, "word")) {
        std::set<std::string> sig;
        for (const auto& token : tokenize(idx.glosses.count(synset) ? idx.glosses.at(synset) : "")) sig.insert(token);
        for (const auto& ex : idx.examples.count(synset) ? idx.examples.at(synset) : std::vector<std::string>{}) {
            for (const auto& token : tokenize(ex)) sig.insert(token);
        }
        int score = 0;
        for (const auto& token : ctx) if (sig.count(token)) ++score;
        if (score > bestScore) { bestScore = score; best = synset; }
    }
    if (best.empty()) return "{\"selected\":null,\"candidates\":[]}";
    std::string row = "{\"word\":" + q(word) + ",\"synset\":" + q(best) + ",\"score\":" + std::to_string(bestScore) + ",\"algorithm\":\"lesk\"}";
    return "{\"selected\":" + row + ",\"candidates\":[" + row + "]}";
}

std::string expandQuery(const Index& idx, const Json& args) {
    std::string out = "[";
    bool first = true;
    for (const auto& term : tokenize(asString(field(args, "query"), asString(field(args, "text"))))) {
        Json local; local.kind = Json::Object; Json w; w.kind = Json::String; w.text = term; local.fields["word"] = w;
        for (const auto& synset : resolveSynsets(idx, local, "word")) {
            if (!first) out += ",";
            first = false;
            out += "{\"term\":" + q(term) + ",\"expansion\":" + q(term) + ",\"relation\":\"self\",\"score\":1}";
            for (const auto& e : neighbors(idx, synset, "hypernym", "both")) {
                out += ",{\"term\":" + q(term) + ",\"expansion\":" + synsetRow(idx, e.to) + ",\"relation\":\"semantic\",\"score\":0.5}";
            }
        }
    }
    return out + "]";
}

std::string lexicalChains(const Index& idx, const Json& args) {
    std::string terms = "[";
    bool first = true;
    int count = 0;
    for (const auto& term : tokenize(asString(field(args, "text")))) {
        Json local; local.kind = Json::Object; Json w; w.kind = Json::String; w.text = term; local.fields["word"] = w;
        auto synsets = resolveSynsets(idx, local, "word");
        if (synsets.empty()) continue;
        if (!first) terms += ",";
        first = false;
        ++count;
        terms += "{\"term\":" + q(term) + ",\"synset\":" + q(synsets.front()) + "}";
    }
    terms += "]";
    return "[{\"id\":\"chain_1\",\"terms\":" + terms + ",\"score\":" + std::to_string(count) + "}]";
}

std::string translate(const Index& idx, const Json& args) {
    std::string source = asString(field(args, "source_language"), "en");
    std::string target = asString(field(args, "target_language"), asString(field(args, "language"), "en"));
    std::string out = "[";
    bool first = true;
    for (const auto& synset : resolveSynsets(idx, args, "word")) {
        auto senses = idx.sensesBySynset.find(synset);
        if (senses == idx.sensesBySynset.end()) continue;
        for (const auto& sid : senses->second) {
            auto lemma = idx.lemmas.find(idx.senses.at(sid).lemma);
            if (lemma == idx.lemmas.end() || lemma->second.language != target) continue;
            if (!first) out += ",";
            first = false;
            out += "{\"source_language\":" + q(source) + ",\"target_language\":" + q(target) + ",\"synset\":" + q(synset) + ",\"text\":" + q(lemma->second.text) + ",\"lemma\":" + q(lemma->second.id) + "}";
        }
    }
    return out + "]";
}

std::string dispatch(const std::string& fn, const Json& args) {
    std::string call = fn;
    const std::string prefix = "system:flibrary:wordnet:";
    if (call.rfind(prefix, 0) == 0) call = "wordnet:" + call.substr(prefix.size());
    Index idx = buildIndex(args);
    if (call == "wordnet:lookup") return lookup(idx, args);
    if (call == "wordnet:lemmatize") return lemmatize(idx, args);
    if (call == "wordnet:traverse" || call == "wordnet:closure") return traverse(idx, args, "out");
    if (call == "wordnet:ancestors") return traverse(idx, args, "out");
    if (call == "wordnet:descendants") return traverse(idx, args, "in");
    if (call == "wordnet:shortest_path") return shortestPath(idx, args);
    if (call == "wordnet:common_ancestors" || call == "wordnet:lowest_common_subsumer") return lcs(idx, args);
    if (call == "wordnet:similarity" || call == "wordnet:check_similarity") return similarity(idx, args);
    if (call == "wordnet:disambiguate" || call == "wordnet:lesk" || call == "wordnet:personalized_pagerank") return disambiguate(idx, args);
    if (call == "wordnet:expand_query") return expandQuery(idx, args);
    if (call == "wordnet:lexical_chains") return lexicalChains(idx, args);
    if (call == "wordnet:translate") return translate(idx, args);
    if (call == "wordnet:synonyms") return synonyms(idx, args);
    return "{\"error\":\"Unsupported WordNet function\"}";
}

} // namespace

extern "C" FELIDAE_WORDNET_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson) {
    Json args = parseJson(argsJson ? argsJson : "{}");
    return copyResponse(dispatch(functionName ? functionName : "", args));
}

extern "C" FELIDAE_WORDNET_EXPORT void felidae_native_free(char* ptr) {
    std::free(ptr);
}
