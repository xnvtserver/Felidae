package local.felidae.intellij.ml;

import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Evaluator for the gradient-boosted decision-tree models trained offline in
 * the repository's ml/ pipeline (see ml/README.md).
 *
 * <p>Only the evaluator lives in the plugin: no ML runtime is bundled, and the
 * model is a small JSON tree ensemble read from plugin resources. The VS Code
 * extension embeds the byte-identical arithmetic in src/mlRanking.ts - keep
 * the two in sync, and keep the feature vectors identical to
 * ml/lib/features.js or the shipped model scores noise.
 * ml/verify-parity-java.js checks this class against the training pipeline.
 *
 * <p>Reading is Gson's. This class used to carry a hand-written JSON reader to
 * avoid a dependency, which is the same choice that made
 * {@link local.felidae.intellij.FelidaeBuiltinDocs} silently drop 125 of 126
 * entries when its schema grew. The arithmetic below is the part worth owning;
 * the parsing is not.
 */
public final class FelidaeGbdt {

    /** Internal node when {@code leaf} is false, otherwise a leaf value. */
    private record Node(boolean leaf, double value, int feature, double threshold, Node left, Node right) {
    }

    private final String objective;
    private final double base;
    private final double learningRate;
    private final List<Node> trees;

    private FelidaeGbdt(String objective, double base, double learningRate, List<Node> trees) {
        this.objective = objective;
        this.base = base;
        this.learningRate = learningRate;
        this.trees = trees;
    }

    /** Loads a model from a plugin resource path, or null when unavailable. */
    public static @Nullable FelidaeGbdt load(@NotNull String resourcePath) {
        JsonObject root = readJsonObject(resourcePath);
        if (root == null) return null;

        try {
            List<Node> trees = new ArrayList<>();
            JsonArray array = root.getAsJsonArray("trees");
            if (array != null) {
                for (JsonElement element : array) {
                    Node tree = parseNode(element);
                    if (tree == null) return null;
                    trees.add(tree);
                }
            }
            if (trees.isEmpty()) return null;

            return new FelidaeGbdt(
                    root.has("objective") ? root.get("objective").getAsString() : "logistic",
                    root.has("base") ? root.get("base").getAsDouble() : 0.0,
                    root.has("learningRate") ? root.get("learningRate").getAsDouble() : 1.0,
                    trees);
        } catch (RuntimeException exception) {
            // A malformed model must disable ranking, never break completion.
            return null;
        }
    }

    public double predict(double @NotNull [] features) {
        double score = base;
        for (Node tree : trees) {
            score += learningRate * evaluate(tree, features);
        }
        return "logistic".equals(objective) ? sigmoid(score) : score;
    }

    /**
     * ml/lib/gbdt.js sends a sample left when {@code x[f] < threshold} - a
     * strict less-than. Must match ml/lib/gbdt.js and mlRanking.ts exactly.
     */
    private static double evaluate(Node node, double[] features) {
        Node current = node;
        while (!current.leaf()) {
            int index = current.feature();
            double value = index >= 0 && index < features.length ? features[index] : 0.0;
            current = value < current.threshold() ? current.left() : current.right();
        }
        return current.value();
    }

    private static double sigmoid(double x) {
        if (x >= 0) return 1.0 / (1.0 + Math.exp(-x));
        double z = Math.exp(x);
        return z / (1.0 + z);
    }

    /** Parses {@code {"v":..}} or {@code {"f":..,"t":..,"l":{..},"r":{..}}}. */
    private static @Nullable Node parseNode(@Nullable JsonElement element) {
        if (element == null || !element.isJsonObject()) return null;
        JsonObject object = element.getAsJsonObject();

        if (object.has("v")) {
            return new Node(true, object.get("v").getAsDouble(), -1, 0, null, null);
        }

        Node left = parseNode(object.get("l"));
        Node right = parseNode(object.get("r"));
        if (left == null || right == null) return null;

        return new Node(
                false,
                0,
                object.has("f") ? object.get("f").getAsInt() : 0,
                object.has("t") ? object.get("t").getAsDouble() : 0,
                left,
                right);
    }

    /**
     * Loads {@code {"builtinFreq":{...},"slotFreq":{...}}} into flat maps.
     * Missing or malformed tables simply mean zero frequencies.
     */
    public static @NotNull Map<String, Map<String, Integer>> loadCorpusIndex(@NotNull String resourcePath) {
        Map<String, Map<String, Integer>> tables = new HashMap<>();
        tables.put("builtinFreq", new HashMap<>());
        tables.put("slotFreq", new HashMap<>());

        JsonObject root = readJsonObject(resourcePath);
        if (root == null) return tables;

        try {
            for (Map.Entry<String, Map<String, Integer>> table : tables.entrySet()) {
                JsonElement element = root.get(table.getKey());
                if (element == null || !element.isJsonObject()) continue;
                for (Map.Entry<String, JsonElement> counted : element.getAsJsonObject().entrySet()) {
                    table.getValue().put(counted.getKey(), counted.getValue().getAsInt());
                }
            }
        } catch (RuntimeException exception) {
            // Partially populated tables are still usable: an absent name just
            // scores zero frequency.
        }
        return tables;
    }

    private static @Nullable JsonObject readJsonObject(@NotNull String resourcePath) {
        try (InputStream stream = FelidaeGbdt.class.getResourceAsStream(resourcePath)) {
            if (stream == null) return null;
            try (Reader reader = new InputStreamReader(stream, StandardCharsets.UTF_8)) {
                return new Gson().fromJson(reader, JsonObject.class);
            }
        } catch (Exception exception) {
            return null;
        }
    }
}
