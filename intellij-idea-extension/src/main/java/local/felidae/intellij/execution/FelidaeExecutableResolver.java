package local.felidae.intellij.execution;

import com.intellij.openapi.project.Project;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.List;

public final class FelidaeExecutableResolver {

    public static final String DEBUG_ENVIRONMENT_VARIABLE =
            "CELIDAE_PATH";

    public static final String LEGACY_DEBUG_ENVIRONMENT_VARIABLE =
            "FELIDAE_DEBUG_PATH";

    public static final String INTERPRETER_ENVIRONMENT_VARIABLE =
            "FELIDAE_PATH";

    private static final String WINDOWS_DEBUG_EXECUTABLE =
            "celidae.exe";

    private static final String UNIX_DEBUG_EXECUTABLE =
            "celidae";

    private static final String WINDOWS_LEGACY_DEBUG_EXECUTABLE =
            "felidae_debug.exe";

    private static final String UNIX_LEGACY_DEBUG_EXECUTABLE =
            "felidae_debug";

    private static final String WINDOWS_INTERPRETER_EXECUTABLE =
            "felidae.exe";

    private static final String UNIX_INTERPRETER_EXECUTABLE =
            "felidae";

    private FelidaeExecutableResolver() {
        throw new AssertionError(
                "FelidaeExecutableResolver cannot be instantiated."
        );
    }

    public static @Nullable Path resolveDebugger(
            @NotNull Project project
    ) {
        Path celidae = resolve(
                project,
                DEBUG_ENVIRONMENT_VARIABLE,
                WINDOWS_DEBUG_EXECUTABLE,
                UNIX_DEBUG_EXECUTABLE
        );
        if (celidae != null) {
            return celidae;
        }
        return resolve(
                project,
                LEGACY_DEBUG_ENVIRONMENT_VARIABLE,
                WINDOWS_LEGACY_DEBUG_EXECUTABLE,
                UNIX_LEGACY_DEBUG_EXECUTABLE
        );
    }

    public static @Nullable Path resolveInterpreter(
            @NotNull Project project
    ) {
        return resolve(
                project,
                INTERPRETER_ENVIRONMENT_VARIABLE,
                WINDOWS_INTERPRETER_EXECUTABLE,
                UNIX_INTERPRETER_EXECUTABLE
        );
    }

    private static @Nullable Path resolve(
            @NotNull Project project,
            @NotNull String environmentVariable,
            @NotNull String windowsExecutable,
            @NotNull String unixExecutable
    ) {
        Path configured = resolveFromEnvironment(environmentVariable);

        if (configured != null) {
            return configured;
        }

        String basePath = project.getBasePath();

        if (basePath == null || basePath.isBlank()) {
            return null;
        }

        final Path projectRoot;

        try {
            projectRoot = Path.of(basePath)
                    .toAbsolutePath()
                    .normalize();
        } catch (InvalidPathException exception) {
            return null;
        }

        List<Path> candidates = List.of(
                projectRoot.resolve("build")
                        .resolve(windowsExecutable),

                projectRoot.resolve("build")
                        .resolve(unixExecutable),

                projectRoot.resolve("bin")
                        .resolve(windowsExecutable),

                projectRoot.resolve("bin")
                        .resolve(unixExecutable),

                projectRoot.resolve(windowsExecutable),

                projectRoot.resolve(unixExecutable)
        );

        for (Path candidate : candidates) {
            Path normalized = candidate
                    .toAbsolutePath()
                    .normalize();

            if (Files.isRegularFile(normalized)) {
                return normalized;
            }
        }

        return null;
    }

    private static @Nullable Path resolveFromEnvironment(
            @NotNull String variableName
    ) {
        String configured = System.getenv(variableName);

        if (configured == null || configured.isBlank()) {
            return null;
        }

        try {
            Path path = Path.of(configured)
                    .toAbsolutePath()
                    .normalize();

            return Files.isRegularFile(path)
                    ? path
                    : null;
        } catch (InvalidPathException exception) {
            return null;
        }
    }
}
