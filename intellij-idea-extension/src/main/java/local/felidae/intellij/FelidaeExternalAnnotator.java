package local.felidae.intellij;

import com.intellij.lang.annotation.AnnotationHolder;
import com.intellij.lang.annotation.ExternalAnnotator;
import com.intellij.lang.annotation.HighlightSeverity;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.TextRange;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.PsiFile;
import local.felidae.intellij.execution.FelidaeExecutableResolver;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class FelidaeExternalAnnotator extends ExternalAnnotator<
        FelidaeExternalAnnotator.CheckRequest,
        List<FelidaeExternalAnnotator.CheckDiagnostic>
        > {

    private static final String FILE_EXTENSION = "fx";

    private static final Duration CHECK_TIMEOUT =
            Duration.ofSeconds(15);

    private static final Duration OUTPUT_READ_TIMEOUT =
            Duration.ofSeconds(2);

    /*
     * Expected protocol:
     *
     * FELIDAE_DIAGNOSTIC severity=error line=4 column=12 message=Expected ')'
     */
    private static final Pattern PROTOCOL_DIAGNOSTIC = Pattern.compile(
            "^FELIDAE_DIAGNOSTIC\\s+" +
                    "severity=(\\w+)\\s+" +
                    "line=(\\d+)\\s+" +
                    "column=(\\d+)\\s+" +
                    "message=(.*)$"
    );

    private static final Pattern JSON_DIAGNOSTIC = Pattern.compile(
            "\\{[^{}]*\"severity\"\\s*:\\s*\"([^\"]+)\"[^{}]*" +
                    "\"line\"\\s*:\\s*(\\d+)[^{}]*" +
                    "\"column\"\\s*:\\s*(\\d+)[^{}]*" +
                    "\"message\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"[^{}]*}"
    );

    /*
     * Fallback runtime-error location formats:
     *
     * error: Something failed at 4:12
     * Something failed at 4:12
     */
    private static final Pattern LOCATION = Pattern.compile(
            "\\bat\\s+(\\d+):(\\d+)\\b",
            Pattern.CASE_INSENSITIVE
    );

    @Override
    public @Nullable CheckRequest collectInformation(
            @NotNull PsiFile file
    ) {
        VirtualFile virtualFile = file.getVirtualFile();

        if (!isSupportedFile(virtualFile)) {
            return null;
        }

        Path sourceFile;

        try {
            sourceFile = Path.of(virtualFile.getPath())
                    .toAbsolutePath()
                    .normalize();
        } catch (InvalidPathException exception) {
            return null;
        }

        Path debugHost = resolveDebugHost(file.getProject());

        return new CheckRequest(sourceFile, debugHost);
    }

    @Override
    public @NotNull List<CheckDiagnostic> doAnnotate(
            CheckRequest request
    ) {
        List<CheckDiagnostic> diagnostics = new ArrayList<>();

        Path debugHost = request.debugHost();

        if (debugHost == null || !Files.isRegularFile(debugHost)) {
            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Celidae debugger executable was not found. " +
                            "Set " + FelidaeExecutableResolver.DEBUG_ENVIRONMENT_VARIABLE +
                            " or place build/celidae.exe under the project root. " +
                            "Legacy build/felidae_debug.exe is still supported.",
                    1,
                    1
            ));

            return diagnostics;
        }

        if (!Files.isRegularFile(request.sourceFile())) {
            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Felidae source file was not found: " +
                            request.sourceFile(),
                    1,
                    1
            ));

            return diagnostics;
        }

        ProcessBuilder builder = new ProcessBuilder(
                debugHost.toString(),
                request.sourceFile().toString(),
                "--check-json"
        );

        /*
         * Merge stderr into stdout so both normal diagnostics and runtime
         * errors can be read from a single stream.
         */
        builder.redirectErrorStream(true);

        Path workingDirectory = request.sourceFile().getParent();

        if (workingDirectory != null && Files.isDirectory(workingDirectory)) {
            builder.directory(workingDirectory.toFile());
        }

        Process process = null;
        CompletableFuture<String> outputFuture = null;

        try {
            process = builder.start();

            Process runningProcess = process;

            outputFuture = CompletableFuture.supplyAsync(() -> {
                try {
                    return readAll(runningProcess.getInputStream());
                } catch (IOException exception) {
                    throw new OutputReadException(exception);
                }
            });

            boolean finished = process.waitFor(
                    CHECK_TIMEOUT.toMillis(),
                    TimeUnit.MILLISECONDS
            );

            if (!finished) {
                terminateProcess(process);

                diagnostics.add(new CheckDiagnostic(
                        "warning",
                        "Celidae --check-json timed out after " +
                                CHECK_TIMEOUT.toSeconds() + " seconds.",
                        1,
                        1
                ));

                return diagnostics;
            }

            String output = readProcessOutput(outputFuture);

            parseJsonDiagnostics(output, diagnostics);
            if (diagnostics.isEmpty()) {
                parseProtocolDiagnostics(output, diagnostics);
            }

            /*
             * Avoid adding one large duplicate runtime error when the
             * executable already emitted structured diagnostics.
             */
            if (process.exitValue() != 0 && diagnostics.isEmpty()) {
                diagnostics.add(
                        parseRuntimeError(
                                output.isBlank()
                                        ? "Celidae check failed with exit code " +
                                        process.exitValue() + "."
                                        : output
                        )
                );
            }
        } catch (IOException exception) {
            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Cannot run Celidae --check-json: " +
                            safeMessage(exception),
                    1,
                    1
            ));
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();

            if (process != null) {
                terminateProcess(process);
            }

            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Celidae --check-json was interrupted.",
                    1,
                    1
            ));
        } catch (ExecutionException exception) {
            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Cannot collect Celidae diagnostics: " +
                            rootCauseMessage(exception),
                    1,
                    1
            ));
        } catch (TimeoutException exception) {
            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Celidae finished, but its output stream did not " +
                            "close within " + OUTPUT_READ_TIMEOUT.toSeconds() +
                            " seconds.",
                    1,
                    1
            ));
        } catch (RuntimeException exception) {
            diagnostics.add(new CheckDiagnostic(
                    "warning",
                    "Unexpected Celidae checker failure: " +
                            safeMessage(exception),
                    1,
                    1
            ));
        } finally {
            if (outputFuture != null && !outputFuture.isDone()) {
                outputFuture.cancel(true);
            }

            if (process != null && process.isAlive()) {
                terminateProcess(process);
            }
        }

        return diagnostics;
    }

    @Override
    public void apply(
            @NotNull PsiFile file,
            @Nullable List<CheckDiagnostic> diagnostics,
            @NotNull AnnotationHolder holder
    ) {
        if (diagnostics == null || diagnostics.isEmpty()) {
            return;
        }

        String sourceText = file.getText();

        for (CheckDiagnostic diagnostic : diagnostics) {
            if (diagnostic == null) {
                continue;
            }

            String message = diagnostic.message();

            if (message == null || message.isBlank()) {
                message = "Felidae diagnostic";
            }

            TextRange range = rangeFor(
                    sourceText,
                    diagnostic.line(),
                    diagnostic.column()
            );

            HighlightSeverity severity = severityFor(
                    diagnostic.severity()
            );

            holder.newAnnotation(severity, message)
                    .range(range)
                    .create();
        }
    }

    private static boolean isSupportedFile(
            @Nullable VirtualFile virtualFile
    ) {
        if (virtualFile == null || virtualFile.isDirectory()) {
            return false;
        }

        String extension = virtualFile.getExtension();

        return extension != null &&
                FILE_EXTENSION.equalsIgnoreCase(extension);
    }

    private static @Nullable Path resolveDebugHost(
            @NotNull Project project
    ) {
        return FelidaeExecutableResolver.resolveDebugger(project);
    }

    private static String readProcessOutput(
            @NotNull CompletableFuture<String> outputFuture
    ) throws InterruptedException, ExecutionException, TimeoutException {
        return outputFuture.get(
                OUTPUT_READ_TIMEOUT.toMillis(),
                TimeUnit.MILLISECONDS
        );
    }

    private static String readAll(
            @NotNull InputStream stream
    ) throws IOException {
        StringBuilder output = new StringBuilder();

        try (
                BufferedReader reader = new BufferedReader(
                        new InputStreamReader(
                                stream,
                                StandardCharsets.UTF_8
                        )
                )
        ) {
            String line;

            while ((line = reader.readLine()) != null) {
                output.append(line).append('\n');
            }
        }

        return output.toString();
    }

    private static void parseProtocolDiagnostics(
            @NotNull String output,
            @NotNull List<CheckDiagnostic> diagnostics
    ) {
        for (String rawLine : output.split("\\R")) {
            String line = rawLine.strip();

            if (line.isEmpty()) {
                continue;
            }

            Matcher matcher = PROTOCOL_DIAGNOSTIC.matcher(line);

            if (!matcher.matches()) {
                continue;
            }

            String severity = matcher.group(1);
            int lineNumber = parsePositiveInt(
                    matcher.group(2),
                    1
            );
            int columnNumber = parsePositiveInt(
                    matcher.group(3),
                    1
            );
            String message = matcher.group(4).trim();

            diagnostics.add(new CheckDiagnostic(
                    severity,
                    message.isBlank()
                            ? "Felidae diagnostic"
                            : message,
                    lineNumber,
                    columnNumber
            ));
        }
    }

    private static void parseJsonDiagnostics(
            @NotNull String output,
            @NotNull List<CheckDiagnostic> diagnostics
    ) {
        Matcher matcher = JSON_DIAGNOSTIC.matcher(output);

        while (matcher.find()) {
            String severity = matcher.group(1);
            int lineNumber = parsePositiveInt(
                    matcher.group(2),
                    1
            );
            int columnNumber = parsePositiveInt(
                    matcher.group(3),
                    1
            );
            String message = unescapeJsonString(
                    matcher.group(4)
            ).trim();

            diagnostics.add(new CheckDiagnostic(
                    severity,
                    message.isBlank()
                            ? "Celidae diagnostic"
                            : message,
                    lineNumber,
                    columnNumber
            ));
        }
    }

    private static @NotNull String unescapeJsonString(
            @NotNull String text
    ) {
        StringBuilder result = new StringBuilder();

        for (int index = 0; index < text.length(); index++) {
            char current = text.charAt(index);

            if (current != '\\' || index + 1 >= text.length()) {
                result.append(current);
                continue;
            }

            char escaped = text.charAt(++index);

            switch (escaped) {
                case 'n' -> result.append('\n');
                case 'r' -> result.append('\r');
                case 't' -> result.append('\t');
                case 'b' -> result.append('\b');
                case 'f' -> result.append('\f');
                case '"', '\\', '/' -> result.append(escaped);
                default -> result.append(escaped);
            }
        }

        return result.toString();
    }

    private static CheckDiagnostic parseRuntimeError(
            @NotNull String output
    ) {
        String message = output.strip();

        if (message.isBlank()) {
            message = "Celidae check failed.";
        }

        if (message.regionMatches(
                true,
                0,
                "error:",
                0,
                "error:".length()
        )) {
            message = message
                    .substring("error:".length())
                    .strip();
        }

        Matcher locationMatcher = LOCATION.matcher(message);

        int line = 1;
        int column = 1;

        if (locationMatcher.find()) {
            line = parsePositiveInt(
                    locationMatcher.group(1),
                    1
            );

            column = parsePositiveInt(
                    locationMatcher.group(2),
                    1
            );
        }

        return new CheckDiagnostic(
                "error",
                message,
                line,
                column
        );
    }

    private static HighlightSeverity severityFor(
            @Nullable String severity
    ) {
        if (severity == null) {
            return HighlightSeverity.WARNING;
        }

        return switch (severity.toLowerCase(Locale.ROOT)) {
            case "error", "fatal" ->
                    HighlightSeverity.ERROR;

            case "info", "information" ->
                    HighlightSeverity.INFORMATION;

            case "hint" ->
                    HighlightSeverity.WEAK_WARNING;

            case "warning", "warn" ->
                    HighlightSeverity.WARNING;

            default ->
                    HighlightSeverity.WARNING;
        };
    }

    private static int parsePositiveInt(
            @Nullable String value,
            int fallback
    ) {
        if (value == null) {
            return fallback;
        }

        try {
            return Math.max(1, Integer.parseInt(value));
        } catch (NumberFormatException exception) {
            return fallback;
        }
    }

    private static TextRange rangeFor(
            @NotNull String text,
            int oneBasedLine,
            int oneBasedColumn
    ) {
        if (text.isEmpty()) {
            return TextRange.EMPTY_RANGE;
        }

        int targetLine = Math.max(1, oneBasedLine);
        int targetColumn = Math.max(1, oneBasedColumn);

        int currentLine = 1;
        int lineStart = 0;

        for (
                int index = 0;
                index < text.length() && currentLine < targetLine;
                index++
        ) {
            char current = text.charAt(index);

            if (current == '\n') {
                currentLine++;
                lineStart = index + 1;
            }
        }

        int lineEnd = findLineEnd(text, lineStart);

        /*
         * The debugger reports one-based columns. Clamp the requested
         * position to the actual line so a bad diagnostic cannot create
         * an invalid TextRange.
         */
        int requestedOffset = lineStart + targetColumn - 1;
        int offset = Math.max(
                lineStart,
                Math.min(requestedOffset, lineEnd)
        );

        /*
         * Highlight one character when possible. For an error positioned
         * at the end of a line, highlight the closest preceding character.
         */
        if (offset < lineEnd) {
            return TextRange.create(offset, offset + 1);
        }

        if (lineEnd > lineStart) {
            return TextRange.create(lineEnd - 1, lineEnd);
        }

        return TextRange.create(lineStart, lineStart);
    }

    private static int findLineEnd(
            @NotNull String text,
            int lineStart
    ) {
        int index = Math.max(0, Math.min(lineStart, text.length()));

        while (index < text.length()) {
            char current = text.charAt(index);

            if (current == '\n' || current == '\r') {
                break;
            }

            index++;
        }

        return index;
    }

    private static void terminateProcess(
            @NotNull Process process
    ) {
        process.destroy();

        try {
            if (!process.waitFor(250, TimeUnit.MILLISECONDS)) {
                process.destroyForcibly();
                process.waitFor(250, TimeUnit.MILLISECONDS);
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            process.destroyForcibly();
        }
    }

    private static String rootCauseMessage(
            @NotNull Throwable throwable
    ) {
        Throwable current = throwable;

        while (current.getCause() != null) {
            current = current.getCause();
        }

        return safeMessage(current);
    }

    private static String safeMessage(
            @NotNull Throwable throwable
    ) {
        String message = throwable.getMessage();

        if (message == null || message.isBlank()) {
            return throwable.getClass().getSimpleName();
        }

        return message;
    }

    public record CheckRequest(
            @NotNull Path sourceFile,
            @Nullable Path debugHost
    ) {
    }

    public record CheckDiagnostic(
            @NotNull String severity,
            @NotNull String message,
            int line,
            int column
    ) {
        public CheckDiagnostic {
            severity = severity.isBlank()
                    ? "warning"
                    : severity;

            message = message.isBlank()
                    ? "Felidae diagnostic"
                    : message;

            line = Math.max(1, line);
            column = Math.max(1, column);
        }
    }

    private static final class OutputReadException
            extends RuntimeException {

        private OutputReadException(
                @NotNull IOException cause
        ) {
            super(cause);
        }
    }
}
