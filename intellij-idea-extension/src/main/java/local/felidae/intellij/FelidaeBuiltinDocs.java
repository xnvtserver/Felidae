package local.felidae.intellij;

import com.google.gson.Gson;
import com.google.gson.JsonSyntaxException;
import com.google.gson.reflect.TypeToken;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.lang.reflect.Type;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Loads the shared stdlib hover-doc content bundled at /builtin-docs.json,
 * generated at build time from the repo-root docs/builtin-docs.json (the same
 * source of truth the VS Code extension uses).
 *
 * <p>Parsing is Gson's. This class previously carried a hand-written
 * recursive-descent reader to avoid a dependency; it handled only string
 * fields, so when builtin-docs.json gained the generated {@code params} array
 * it stopped at the first entry and silently loaded 1 of 126 entries. A real
 * parser removes that whole class of failure, and the schema can now grow
 * without this file changing.
 */
public final class FelidaeBuiltinDocs {

    /**
     * One named argument a builtin accepts. {@code type} is empty for
     * builtins, whose documented examples carry names only; felidae_debug
     * supplies real types for user-defined declarations instead.
     */
    public record Param(@NotNull String name, @NotNull String type) {
        public Param {
            // Gson leaves absent fields null; normalise so callers never see one.
            name = name == null ? "" : name;
            type = type == null ? "" : type;
        }
    }

    public record Entry(
            @NotNull String heading,
            @NotNull String description,
            @NotNull String example,
            @NotNull List<Param> params) {
        public Entry {
            heading = heading == null ? "" : heading;
            description = description == null ? "" : description;
            example = example == null ? "" : example;
            params = params == null ? List.of() : List.copyOf(params);
        }
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
        Type mapType = new TypeToken<LinkedHashMap<String, Entry>>() { }.getType();
        try (InputStream stream = FelidaeBuiltinDocs.class.getResourceAsStream("/builtin-docs.json")) {
            if (stream == null) {
                return Map.of();
            }
            try (Reader reader = new InputStreamReader(stream, StandardCharsets.UTF_8)) {
                Map<String, Entry> parsed = new Gson().fromJson(reader, mapType);
                // Insertion order is preserved so completion and the docs
                // popup list stdlib entries in the order the file declares.
                return parsed == null ? Map.of() : parsed;
            }
        } catch (Exception exception) {
            // A missing or corrupt bundle must not break the whole plugin;
            // every consumer treats an empty table as "no builtin docs".
            if (exception instanceof JsonSyntaxException) {
                Logger.warn("builtin-docs.json is not valid JSON: " + exception.getMessage());
            }
            return Map.of();
        }
    }

    /** Minimal indirection so this class needs no IntelliJ imports. */
    private static final class Logger {
        static void warn(String message) {
            com.intellij.openapi.diagnostic.Logger
                    .getInstance(FelidaeBuiltinDocs.class)
                    .warn(message);
        }
    }
}
