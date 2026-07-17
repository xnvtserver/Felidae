#include "BuiltinRegistry.h"

#include <unordered_map>

namespace Felidae {

namespace {

const std::unordered_map<std::string, BuiltinEffect>& builtinEffects() {
    static const std::unordered_map<std::string, BuiltinEffect> effects = {
        {"count", BuiltinEffect::Pure},
        {"sum", BuiltinEffect::Pure},
        {"average", BuiltinEffect::Pure},
        {"min", BuiltinEffect::Pure},
        {"max", BuiltinEffect::Pure},
        {"sort", BuiltinEffect::Pure},
        {"search", BuiltinEffect::Pure},
        {"contains", BuiltinEffect::Pure},
        {"lower", BuiltinEffect::Pure},
        {"upper", BuiltinEffect::Pure},
        {"length", BuiltinEffect::Pure},
        {"ParseDoc", BuiltinEffect::Pure},
        {"str:len", BuiltinEffect::Pure},
        {"str:contains", BuiltinEffect::Pure},
        {"str:concat", BuiltinEffect::Pure},
        {"str:join", BuiltinEffect::Pure},
        {"str:lower", BuiltinEffect::Pure},
        {"str:upper", BuiltinEffect::Pure},
        {"str:trim", BuiltinEffect::Pure},
        {"str:split", BuiltinEffect::Pure},
        {"str:replace", BuiltinEffect::Pure},
        {"str:startsWith", BuiltinEffect::Pure},
        {"str:endsWith", BuiltinEffect::Pure},
        {"console:readLine", BuiltinEffect::ReadsExternalState},
        {"console:writeLine", BuiltinEffect::WritesExternalState},
        {"console:write", BuiltinEffect::WritesExternalState},
        {"system:print", BuiltinEffect::WritesExternalState},
        {"file:readFile", BuiltinEffect::ReadsExternalState},
        {"file:readLines", BuiltinEffect::ReadsExternalState},
        {"file:readLine", BuiltinEffect::ReadsExternalState},
        {"file:writeFile", BuiltinEffect::WritesExternalState},
        {"file:writeLines", BuiltinEffect::WritesExternalState},
        {"file:appendFile", BuiltinEffect::WritesExternalState},
        {"file:exists", BuiltinEffect::ReadsExternalState},
        {"file:deleteFile", BuiltinEffect::WritesExternalState},
        {"csv:parse", BuiltinEffect::Pure},
        {"csv:toFacts", BuiltinEffect::Pure},
        {"csv:toText", BuiltinEffect::Pure},
        {"csv:toFelidaeFacts", BuiltinEffect::Pure},
        {"csv:addRow", BuiltinEffect::Pure},
        {"csv:findRows", BuiltinEffect::Pure},
        {"csv:updateRows", BuiltinEffect::Pure},
        {"csv:deleteRows", BuiltinEffect::Pure},
        {"db:all", BuiltinEffect::ReadsExternalState},
        {"db:find", BuiltinEffect::ReadsExternalState},
        {"db:count", BuiltinEffect::ReadsExternalState},
        {"db:first", BuiltinEffect::ReadsExternalState},
        {"db:types", BuiltinEffect::ReadsExternalState},
        {"db:fields", BuiltinEffect::ReadsExternalState},
        {"json:parse", BuiltinEffect::Pure},
        {"json:get", BuiltinEffect::Pure},
        {"json:has", BuiltinEffect::Pure},
        {"json:keys", BuiltinEffect::Pure},
        {"json:set", BuiltinEffect::Pure},
        {"json:remove", BuiltinEffect::Pure},
        {"json:toText", BuiltinEffect::Pure},
        {"visualize:dataJson", BuiltinEffect::ReadsExternalState},
        {"visualize:dataHtml", BuiltinEffect::ReadsExternalState},
        {"visualize:graphJson", BuiltinEffect::ReadsExternalState},
        {"thread:createThread", BuiltinEffect::WritesExternalState},
        {"thread:start", BuiltinEffect::WritesExternalState},
        {"thread:pause", BuiltinEffect::WritesExternalState},
        {"thread:stop", BuiltinEffect::WritesExternalState},
        {"thread:status", BuiltinEffect::ReadsExternalState},
        {"thread:result", BuiltinEffect::ReadsExternalState},
        {"http:get", BuiltinEffect::ReadsExternalState},
        {"http:post", BuiltinEffect::WritesExternalState},
        {"http:put", BuiltinEffect::WritesExternalState},
        {"http:delete", BuiltinEffect::WritesExternalState},
        {"http:serveStatic", BuiltinEffect::WritesExternalState},
        {"process:platform", BuiltinEffect::ReadsExternalState},
        {"process:exec", BuiltinEffect::WritesExternalState},
        {"process:sleep", BuiltinEffect::Volatile},
        {"math:pi", BuiltinEffect::Pure},
        {"math:e", BuiltinEffect::Pure},
        {"math:random", BuiltinEffect::Volatile},
        {"math:pow", BuiltinEffect::Pure},
        {"math:atan2", BuiltinEffect::Pure},
        {"math:sqrt", BuiltinEffect::Pure},
        {"math:sin", BuiltinEffect::Pure},
        {"math:cos", BuiltinEffect::Pure},
        {"math:tan", BuiltinEffect::Pure},
        {"math:asin", BuiltinEffect::Pure},
        {"math:acos", BuiltinEffect::Pure},
        {"math:atan", BuiltinEffect::Pure},
        {"math:log", BuiltinEffect::Pure},
        {"math:log10", BuiltinEffect::Pure},
        {"math:exp", BuiltinEffect::Pure},
        {"math:abs", BuiltinEffect::Pure},
        {"math:floor", BuiltinEffect::Pure},
        {"math:ceil", BuiltinEffect::Pure},
        {"math:round", BuiltinEffect::Pure},
        {"probability:mean", BuiltinEffect::Pure},
        {"probability:variance", BuiltinEffect::Pure},
        {"probability:stddev", BuiltinEffect::Pure},
        {"probability:normalize", BuiltinEffect::Pure},
        {"probability:entropy", BuiltinEffect::Pure},
        {"probability:covariance", BuiltinEffect::Pure},
        {"probability:correlation", BuiltinEffect::Pure},
        {"probability:bernoulli", BuiltinEffect::Volatile},
        {"probability:binomialPmf", BuiltinEffect::Pure},
        {"probability:binomialCdf", BuiltinEffect::Pure},
        {"probability:poissonPmf", BuiltinEffect::Pure},
        {"probability:poissonCdf", BuiltinEffect::Pure},
        {"probability:normalPdf", BuiltinEffect::Pure},
        {"probability:normalCdf", BuiltinEffect::Pure},
        {"probability:uniformPdf", BuiltinEffect::Pure},
        {"probability:uniformCdf", BuiltinEffect::Pure},
        {"probability:sample", BuiltinEffect::Volatile},
        {"probability:weightedChoice", BuiltinEffect::Volatile},
        {"ml:sigmoid", BuiltinEffect::Pure},
        {"ml:relu", BuiltinEffect::Pure},
        {"ml:dot", BuiltinEffect::Pure},
        {"ml:meanSquaredError", BuiltinEffect::Pure}
    };
    return effects;
}

} // namespace

bool isBuiltinFunctionName(const std::string& name) {
    return builtinEffects().count(name) > 0;
}

BuiltinEffect builtinEffect(const std::string& name) {
    auto found = builtinEffects().find(name);
    return found == builtinEffects().end() ? BuiltinEffect::Volatile : found->second;
}

bool isBuiltinPure(const std::string& name) {
    return builtinEffect(name) == BuiltinEffect::Pure;
}

} // namespace Felidae
