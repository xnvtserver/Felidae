#pragma once

// Every bundled native module exports the same, bounded manifest entry point.
// The runtime validates this document before accepting a DLL.  Keeping the
// declaration in the library (rather than a runtime module-name table) makes
// a package's capabilities reviewable alongside its binary implementation.

#include <string>

#if defined(_WIN32)
#define FELIDAE_NATIVE_MANIFEST_EXPORT __declspec(dllexport)
#else
#define FELIDAE_NATIVE_MANIFEST_EXPORT __attribute__((visibility("default")))
#endif

#ifndef FELIDAE_NATIVE_MANIFEST_MODULE
#error "Native targets must define FELIDAE_NATIVE_MANIFEST_MODULE"
#endif

namespace FelidaeNativeManifest {

inline const char* enabled(bool value) { return value ? "true" : "false"; }

inline std::string document() {
    bool pure = false;
    bool threadSafe = false;
    bool supportsBatch = false;
    bool acceptsSelections = false;
    bool needsFactProjection = false;
    const char* requestedTypes = "[]";
    const char* requestedFields = "[]";
    std::size_t maximumProjectedRows = 0;
    const char* functionContracts = "{}";
    const char* argumentConstraints = "[]";

#if defined(FELIDAE_NATIVE_MANIFEST_SET) || defined(FELIDAE_NATIVE_MANIFEST_GROUP)
    pure = true;
    threadSafe = true;
    supportsBatch = true;
    acceptsSelections = true;
#elif defined(FELIDAE_NATIVE_MANIFEST_FACT_ANALYSIS)
    pure = true;
    threadSafe = true;
    acceptsSelections = true;
#elif defined(FELIDAE_NATIVE_MANIFEST_WORDNET)
    pure = true;
    threadSafe = true;
    needsFactProjection = true;
    requestedTypes = "[\"Synset\",\"Lemma\",\"Sense\",\"Gloss\",\"Example\",\"Hypernym\",\"SimilarTo\",\"Antonym\",\"MorphException\",\"ConceptFrequency\"]";
    requestedFields = "[\"id\",\"pos\",\"text\",\"language\",\"lemma\",\"synset\",\"number\",\"frequency\",\"child\",\"parent\",\"left\",\"right\",\"surface\",\"count\"]";
    maximumProjectedRows = 1000000;
#elif defined(FELIDAE_NATIVE_MANIFEST_FACT)
    pure = true;
    threadSafe = true;
    // These are the only fact operations that require a runtime hierarchy or
    // record projection. The policy travels with the fact DLL, not the host.
    functionContracts =
        "{"
        "\"common_ancestor\":{\"needs_fact_hierarchy\":true},"
        "\"direct_relation\":{\"needs_fact_hierarchy\":true},"
        "\"is_ancestor\":{\"needs_fact_hierarchy\":true},"
        "\"is_descendant\":{\"needs_fact_hierarchy\":true},"
        "\"ancestor_closure\":{\"needs_fact_hierarchy\":true},"
        "\"descendant_closure\":{\"needs_fact_hierarchy\":true},"
        "\"shortest_path\":{\"needs_fact_hierarchy\":true},"
        "\"path_similarity\":{\"needs_fact_hierarchy\":true},"
        "\"wu_palmer_similarity\":{\"needs_fact_hierarchy\":true},"
        "\"resnik_similarity\":{\"needs_fact_hierarchy\":true},"
        "\"lin_similarity\":{\"needs_fact_hierarchy\":true},"
        "\"frequency_statistics\":{}"
        "}";
    argumentConstraints =
        "["
        "{\"name\":\"algorithm\",\"index\":2,\"kind\":\"string_option\",\"values\":[\"exact_recursive\",\"structural\",\"semantic_recursive\",\"semantic_pattern\",\"relationship_aware\"]},"
        "{\"name\":\"lexical_algorithm\",\"index\":3,\"kind\":\"string_option\",\"values\":[\"path\",\"wup\",\"wu_palmer\",\"Wu-Palmer\",\"Wu Palmer\",\"resnik\",\"jiang_conrath\",\"Jiang-Conrath\",\"Jiang Conrath\",\"lin\",\"edit\",\"Leacock-Chodorow\",\"Leacock Chodorow\",\"leacock_chodorow\",\"lch\"]},"
        "{\"name\":\"field_alignment\",\"index\":4,\"kind\":\"string_option\",\"values\":[\"exact\",\"semantic\"]},"
        "{\"name\":\"collection_mode\",\"index\":5,\"kind\":\"string_option\",\"values\":[\"auto\",\"ordered\",\"unordered\"]},"
        "{\"name\":\"missing_field_policy\",\"index\":6,\"kind\":\"string_option\",\"values\":[\"penalize\",\"ignore\"]},"
        "{\"name\":\"mode\",\"index\":2,\"kind\":\"string_option\",\"values\":[\"exact\",\"semantic\"]},"
        "{\"name\":\"threshold\",\"index\":7,\"kind\":\"unit_interval\"},"
        "{\"name\":\"maximum_depth\",\"index\":8,\"kind\":\"positive_finite\"},"
        "{\"name\":\"maximum_fields\",\"index\":9,\"kind\":\"positive_finite\"},"
        "{\"name\":\"explain\",\"index\":10,\"kind\":\"boolean_text\"}"
        "]";
#endif

    return std::string("{\"schema_version\":1,\"module\":\"") +
        FELIDAE_NATIVE_MANIFEST_MODULE +
        "\",\"abi_version\":1,\"capabilities\":{\"pure\":" + enabled(pure) +
        ",\"thread_safe\":" + enabled(threadSafe) +
        ",\"supports_batch\":" + enabled(supportsBatch) +
        ",\"accepts_fact_selections\":" + enabled(acceptsSelections) +
        ",\"needs_fact_projection\":" + enabled(needsFactProjection) +
        ",\"requested_fact_types\":" + requestedTypes +
        ",\"requested_fact_fields\":" + requestedFields +
        ",\"maximum_projected_rows\":" + std::to_string(maximumProjectedRows) +
        ",\"argument_constraints\":" + argumentConstraints +
        "},\"functions\":" + functionContracts + "}";
}

} // namespace FelidaeNativeManifest

extern "C" FELIDAE_NATIVE_MANIFEST_EXPORT const char* felidae_native_manifest_v1() {
    static const std::string manifest = FelidaeNativeManifest::document();
    return manifest.c_str();
}
