package local.felidae.intellij;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Loads the shared stdlib hover-doc content bundled at /builtin-docs.json,
 * generated at build time from the repo-root docs/builtin-docs.json (the
 * same source of truth the VS Code extension uses). Hand-rolled parsing
 * avoids adding a JSON library dependency for one well-controlled, two-level
 * string-only shape, matching the precedent already set by
 * FelidaeStdlibIndex.parseJsonStringArray for the same reason.
 */
public final class FelidaeBuiltinDocs {

    public record Entry(@NotNull String heading, @NotNull String description, @NotNull String example) {
    }

    private static final Map<String, Entry> ENTRIES = load();

    private FelidaeBuiltinDocs() {
        throw new AssertionError("FelidaeBuiltinDocs cannot be instantiated.");
    }

    public static @Nullable Entry get(@NotNull String key) {
        return ENTRIES.get(key);
    }

    public static boolean contains(@NotNull String key) {
        return ENTRIES.containsKey(key);
    }

    public static @NotNull Iterable<Map.Entry<String, Entry>> entries() {
        return ENTRIES.entrySet();
    }

    private static Map<String, Entry> load() {
        try (InputStream stream = FelidaeBuiltinDocs.class.getResourceAsStream("/builtin-docs.json")) {
            if (stream == null) {
                return Map.of();
            }
            String json = new String(stream.readAllBytes(), StandardCharsets.UTF_8);
            return parse(json);
        } catch (IOException | RuntimeException exception) {
            return Map.of();
        }
    }

    private record Parsed(String value, int end) {
    }

    private static Map<String, Entry> parse(@NotNull String json) {
        Map<String, Entry> result = new LinkedHashMap<>();
        int i = skipWhitespace(json, 0);
        if (i >= json.length() || json.charAt(i) != '{') {
            return result;
        }
        i++;

        while (true) {
            i = skipWhitespace(json, i);
            if (i >= json.length() || json.charAt(i) == '}') break;

            Parsed key = parseString(json, i);
            if (key == null) break;
            i = skipWhitespace(json, key.end());
            if (i >= json.length() || json.charAt(i) != ':') break;
            i++;
            i = skipWhitespace(json, i);
            if (i >= json.length() || json.charAt(i) != '{') break;
            i++;

            String heading = null;
            String description = null;
            String example = null;

            objectFields:
            while (true) {
                i = skipWhitespace(json, i);
                if (i >= json.length()) break;
                if (json.charAt(i) == '}') {
                    i++;
                    break;
                }
                Parsed fieldKey = parseString(json, i);
                if (fieldKey == null) break;
                i = skipWhitespace(json, fieldKey.end());
                if (i >= json.length() || json.charAt(i) != ':') break;
                i++;
                i = skipWhitespace(json, i);
                Parsed fieldValue = parseString(json, i);
                if (fieldValue == null) break;
                i = fieldValue.end();

                switch (fieldKey.value()) {
                    case "heading" -> heading = fieldValue.value();
                    case "description" -> description = fieldValue.value();
                    case "example" -> example = fieldValue.value();
                    default -> { }
                }

                i = skipWhitespace(json, i);
                if (i < json.length() && json.charAt(i) == ',') {
                    i++;
                    continue;
                }
                if (i < json.length() && json.charAt(i) == '}') {
                    i++;
                    break objectFields;
                }
                break;
            }

            if (heading != null && description != null && example != null) {
                result.put(key.value(), new Entry(heading, description, example));
            }

            i = skipWhitespace(json, i);
            if (i < json.length() && json.charAt(i) == ',') {
                i++;
                continue;
            }
            break;
        }

        return result;
    }

    private static @Nullable Parsed parseString(@NotNull String json, int start) {
        if (start >= json.length() || json.charAt(start) != '"') {
            return null;
        }
        StringBuilder builder = new StringBuilder();
        int i = start + 1;
        while (i < json.length()) {
            char ch = json.charAt(i);
            if (ch == '"') {
                return new Parsed(builder.toString(), i + 1);
            }
            if (ch == '\\' && i + 1 < json.length()) {
                char escaped = json.charAt(i + 1);
                switch (escaped) {
                    case 'n' -> builder.append('\n');
                    case 'r' -> builder.append('\r');
                    case 't' -> builder.append('\t');
                    case 'b' -> builder.append('\b');
                    case 'f' -> builder.append('\f');
                    case '"' -> builder.append('"');
                    case '\\' -> builder.append('\\');
                    case '/' -> builder.append('/');
                    case 'u' -> {
                        if (i + 5 < json.length()) {
                            builder.append((char) Integer.parseInt(json.substring(i + 2, i + 6), 16));
                            i += 4;
                        }
                    }
                    default -> builder.append(escaped);
                }
                i += 2;
                continue;
            }
            builder.append(ch);
            i++;
        }
        return null;
    }

    private static int skipWhitespace(@NotNull String json, int start) {
        int i = start;
        while (i < json.length() && Character.isWhitespace(json.charAt(i))) i++;
        return i;
    }
}
