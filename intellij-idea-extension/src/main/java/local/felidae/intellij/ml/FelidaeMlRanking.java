package local.felidae.intellij.ml;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Scores completion candidates with the ranking models trained offline in the
 * repository's ml/ pipeline.
 *
 * <p>The feature vectors below MUST match ml/lib/features.js and
 * vs-code-extension/src/mlRanking.ts element for element - a mismatch does not
 * fail loudly, it silently scores noise. ml/verify-parity-java.js checks this
 * class against the training pipeline on shared fixtures.
 *
 * <p>When the model resources are absent every scorer reports itself disabled
 * and callers leave their ordering untouched, so the plugin behaves exactly as
 * it did before ranking existed.
 */
public final class FelidaeMlRanking {

    public enum CandidateKind { LOCAL, PARAM, METHOD, FACT, LIBRARY }

    private static final int MAX_NAME_LEN = 32;
    private static final int RECENCY_WINDOW = 60;

    private static final Pattern IDENTIFIER = Pattern.compile("[A-Za-z_][A-Za-z0-9_]*");

    private static final FelidaeGbdt COMPLETION_MODEL =
            FelidaeGbdt.load("/models/completion-rank.json");
    private static final FelidaeGbdt NEXT_PARAM_MODEL =
            FelidaeGbdt.load("/models/next-param.json");
    private static final Map<String, Map<String, Integer>> CORPUS_INDEX =
            FelidaeGbdt.loadCorpusIndex("/models/corpus-index.json");

    private FelidaeMlRanking() {
        throw new AssertionError("FelidaeMlRanking cannot be instantiated.");
    }

    public static boolean isCompletionRankingEnabled() {
        return COMPLETION_MODEL != null;
    }

    public static boolean isNextParamRankingEnabled() {
        return NEXT_PARAM_MODEL != null;
    }

    /** Per-file usage statistics gathered from the text above the caret. */
    public static final class CompletionContext {
        final String prefix;
        final int line;
        final Map<String, Integer> localCounts;
        final Map<String, Integer> lastUseLine;

        CompletionContext(String prefix, int line, Map<String, Integer> counts, Map<String, Integer> lastUse) {
            this.prefix = prefix;
            this.line = line;
            this.localCounts = counts;
            this.lastUseLine = lastUse;
        }
    }

    /**
     * Scans the text above the caret for identifier counts and recency.
     * Strings and `#` comments are masked so they contribute neither, matching
     * ml/lib/corpus.js.
     */
    public static @NotNull CompletionContext buildCompletionContext(
            @NotNull CharSequence textAboveCaret,
            @NotNull String prefix
    ) {
        Map<String, Integer> counts = new HashMap<>();
        Map<String, Integer> lastUse = new HashMap<>();
        String[] lines = textAboveCaret.toString().split("\n", -1);
        for (int lineNumber = 0; lineNumber < lines.length; lineNumber++) {
            Matcher matcher = IDENTIFIER.matcher(maskLine(lines[lineNumber]));
            while (matcher.find()) {
                String token = matcher.group();
                counts.merge(token, 1, Integer::sum);
                lastUse.put(token, lineNumber);
            }
        }
        return new CompletionContext(prefix, Math.max(0, lines.length - 1), counts, lastUse);
    }

    /** Probability that {@code name} is the identifier being reached for. */
    public static double scoreCompletion(
            @NotNull String name,
            @NotNull CandidateKind kind,
            @NotNull CompletionContext context
    ) {
        if (COMPLETION_MODEL == null) return 0;

        String prefix = context.prefix;
        int isPrefixMatch = !prefix.isEmpty() && name.startsWith(prefix) ? 1 : 0;
        int isPrefixMatchCI = !prefix.isEmpty()
                && name.toLowerCase().startsWith(prefix.toLowerCase()) ? 1 : 0;

        int localCount = context.localCounts.getOrDefault(name, 0);
        Integer lastUse = context.lastUseLine.get(name);
        int distance = lastUse == null ? RECENCY_WINDOW : Math.max(0, context.line - lastUse);
        double recency = Math.max(0, 1 - Math.min(distance, RECENCY_WINDOW) / (double) RECENCY_WINDOW);
        int builtinCount = kind == CandidateKind.LIBRARY
                ? CORPUS_INDEX.get("builtinFreq").getOrDefault(name, 0)
                : 0;

        double[] features = {
                Math.min(prefix.length(), MAX_NAME_LEN),
                isPrefixMatch,
                isPrefixMatchCI,
                name.isEmpty() ? 0 : Math.min(prefix.length() / (double) name.length(), 1),
                Math.min(name.length(), MAX_NAME_LEN),
                kind == CandidateKind.LOCAL ? 1 : 0,
                kind == CandidateKind.PARAM ? 1 : 0,
                kind == CandidateKind.METHOD ? 1 : 0,
                kind == CandidateKind.FACT ? 1 : 0,
                kind == CandidateKind.LIBRARY ? 1 : 0,
                Math.log1p(localCount),
                recency,
                Math.log1p(builtinCount)
        };
        return COMPLETION_MODEL.predict(features);
    }

    /** Probability that {@code name} is the next named argument written. */
    public static double scoreNextParam(
            @NotNull String name,
            int declIndex,
            int paramsTotal,
            int suppliedCount,
            int firstUnsuppliedIndex,
            boolean isBuiltinCall
    ) {
        if (NEXT_PARAM_MODEL == null) return 0;
        String slotKey = name + "@" + Math.min(suppliedCount, 8);
        int slotCount = CORPUS_INDEX.get("slotFreq").getOrDefault(slotKey, 0);

        double[] features = {
                declIndex,
                paramsTotal,
                suppliedCount,
                Math.max(0, paramsTotal - suppliedCount),
                declIndex == firstUnsuppliedIndex ? 1 : 0,
                declIndex - suppliedCount,
                Math.min(name.length(), MAX_NAME_LEN),
                isBuiltinCall ? 1 : 0,
                Math.log1p(slotCount)
        };
        return NEXT_PARAM_MODEL.predict(features);
    }

    /** Convenience for callers holding a set of already-supplied keys. */
    public static int firstUnsuppliedIndex(
            @NotNull java.util.List<String> paramNames,
            @NotNull Set<String> supplied
    ) {
        for (int i = 0; i < paramNames.size(); i++) {
            if (!supplied.contains(paramNames.get(i))) return i;
        }
        return -1;
    }

    /** Blanks string bodies and `#` comments, preserving length. */
    private static @NotNull String maskLine(@NotNull String line) {
        StringBuilder masked = new StringBuilder(line.length());
        boolean inString = false;
        int i = 0;
        while (i < line.length()) {
            char ch = line.charAt(i);
            if (inString) {
                if (ch == '\\' && i + 1 < line.length()) { masked.append("xx"); i += 2; continue; }
                if (ch == '"') { inString = false; masked.append('"'); i++; continue; }
                masked.append('x');
                i++;
                continue;
            }
            if (ch == '#') {
                masked.append(" ".repeat(line.length() - i));
                break;
            }
            if (ch == '"') { inString = true; masked.append('"'); i++; continue; }
            masked.append(ch);
            i++;
        }
        return masked.toString();
    }

    /** Test seam for ml/verify-parity-java.js. */
    public static double predictRaw(@NotNull String model, double @NotNull [] features) {
        FelidaeGbdt selected = "next-param".equals(model) ? NEXT_PARAM_MODEL : COMPLETION_MODEL;
        return selected == null ? Double.NaN : selected.predict(features);
    }

    static @Nullable FelidaeGbdt completionModel() {
        return COMPLETION_MODEL;
    }
}
