import local.felidae.intellij.ml.FelidaeMlRanking;

/**
 * Prints Java-side predictions for feature vectors supplied on stdin, one
 * vector per line as comma-separated doubles, prefixed by the model name.
 * ml/verify-parity-java.js compares the output against ml/lib/gbdt.js.
 */
public class ParityHarness {
    public static void main(String[] args) throws Exception {
        java.io.BufferedReader in =
                new java.io.BufferedReader(new java.io.InputStreamReader(System.in));
        String line;
        StringBuilder out = new StringBuilder();
        while ((line = in.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            int split = line.indexOf('|');
            String model = line.substring(0, split);
            String[] parts = line.substring(split + 1).split(",");
            double[] features = new double[parts.length];
            for (int i = 0; i < parts.length; i++) features[i] = Double.parseDouble(parts[i]);
            out.append(FelidaeMlRanking.predictRaw(model, features)).append('\n');
        }
        System.out.print(out);
    }
}
