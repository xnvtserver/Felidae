package local.felidae.intellij.run;

import com.intellij.execution.ExecutionException;
import com.intellij.execution.Executor;
import com.intellij.execution.configurations.ConfigurationFactory;
import com.intellij.execution.configurations.RunConfigurationOptions;
import com.intellij.execution.configurations.RunProfileState;
import com.intellij.execution.configurations.RunConfigurationBase;
import com.intellij.execution.configurations.RuntimeConfigurationError;
import com.intellij.execution.runners.ExecutionEnvironment;
import com.intellij.openapi.options.SettingsEditor;
import com.intellij.openapi.project.Project;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;

public final class FelidaeRunConfiguration
        extends RunConfigurationBase<FelidaeRunConfigurationOptions> {

    public FelidaeRunConfiguration(
            @NotNull Project project,
            @NotNull ConfigurationFactory factory,
            @NotNull String name
    ) {
        super(project, factory, name);
    }

    @Override
    protected @NotNull Class<? extends RunConfigurationOptions>
    getOptionsClass() {
        return FelidaeRunConfigurationOptions.class;
    }

    private @NotNull FelidaeRunConfigurationOptions felidaeOptions() {
        return (FelidaeRunConfigurationOptions) super.getOptions();
    }

    public @NotNull String getSourceFile() {
        return felidaeOptions().getSourceFile();
    }

    public void setSourceFile(@NotNull String value) {
        felidaeOptions().setSourceFile(value);
    }

    public @NotNull String getInterpreterPath() {
        return felidaeOptions().getInterpreterPath();
    }

    public void setInterpreterPath(@NotNull String value) {
        felidaeOptions().setInterpreterPath(value);
    }

    public @NotNull String getProgramArguments() {
        return felidaeOptions().getProgramArguments();
    }

    public void setProgramArguments(@NotNull String value) {
        felidaeOptions().setProgramArguments(value);
    }

    public @NotNull String getWorkingDirectory() {
        return felidaeOptions().getWorkingDirectory();
    }

    public void setWorkingDirectory(@NotNull String value) {
        felidaeOptions().setWorkingDirectory(value);
    }

    @Override
    public @NotNull SettingsEditor<FelidaeRunConfiguration>
    getConfigurationEditor() {
        return new FelidaeSettingsEditor(getProject());
    }

    @Override
    public void checkConfiguration()
            throws RuntimeConfigurationError {

        Path sourceFile = parseRequiredPath(
                getSourceFile(),
                "Felidae source file"
        );

        if (!Files.isRegularFile(sourceFile)) {
            throw new RuntimeConfigurationError(
                    "Felidae source file does not exist: " + sourceFile
            );
        }

        String fileName = sourceFile
                .getFileName()
                .toString()
                .toLowerCase();

        if (!fileName.endsWith(".fx")) {
            throw new RuntimeConfigurationError(
                    "Felidae source file must use the .fx extension."
            );
        }

        String interpreterValue = getInterpreterPath();

        if (!interpreterValue.isBlank()) {
            Path interpreter = parseRequiredPath(
                    interpreterValue,
                    "Felidae interpreter"
            );

            if (!Files.isRegularFile(interpreter)) {
                throw new RuntimeConfigurationError(
                        "Felidae interpreter does not exist: " +
                                interpreter
                );
            }
        }

        String workingDirectoryValue = getWorkingDirectory();

        if (!workingDirectoryValue.isBlank()) {
            Path workingDirectory = parseRequiredPath(
                    workingDirectoryValue,
                    "Working directory"
            );

            if (!Files.isDirectory(workingDirectory)) {
                throw new RuntimeConfigurationError(
                        "Working directory does not exist: " +
                                workingDirectory
                );
            }
        }
    }

    @Override
    public @Nullable RunProfileState getState(
            @NotNull Executor executor,
            @NotNull ExecutionEnvironment environment
    ) throws ExecutionException {
        return new FelidaeCommandLineState(
                environment,
                this
        );
    }

    private static @NotNull Path parseRequiredPath(
            @NotNull String value,
            @NotNull String label
    ) throws RuntimeConfigurationError {
        if (value.isBlank()) {
            throw new RuntimeConfigurationError(
                    label + " is not configured."
            );
        }

        try {
            return Path.of(value)
                    .toAbsolutePath()
                    .normalize();
        } catch (InvalidPathException exception) {
            throw new RuntimeConfigurationError(
                    label + " contains an invalid path: " + value
            );
        }
    }
}