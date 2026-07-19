package local.felidae.intellij.run;

import com.intellij.execution.ExecutionException;
import com.intellij.execution.configurations.GeneralCommandLine;
import com.intellij.util.execution.ParametersListUtil;
import com.intellij.execution.process.OSProcessHandler;
import com.intellij.execution.process.ProcessHandler;
import com.intellij.execution.process.ProcessTerminatedListener;
import com.intellij.execution.runners.ExecutionEnvironment;
import com.intellij.execution.configurations.CommandLineState;
import local.felidae.intellij.execution.FelidaeExecutableResolver;
import org.jetbrains.annotations.NotNull;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;

public final class FelidaeCommandLineState
        extends CommandLineState {

    private final FelidaeRunConfiguration configuration;

    public FelidaeCommandLineState(
            @NotNull ExecutionEnvironment environment,
            @NotNull FelidaeRunConfiguration configuration
    ) {
        super(environment);
        this.configuration = configuration;
    }

    @Override
    protected @NotNull ProcessHandler startProcess()
            throws ExecutionException {

        Path interpreter = resolveInterpreter();
        Path sourceFile = Path.of(
                configuration.getSourceFile()
        ).toAbsolutePath().normalize();

        GeneralCommandLine commandLine =
                new GeneralCommandLine();

        commandLine.setExePath(interpreter.toString());
        commandLine.setCharset(StandardCharsets.UTF_8);
        commandLine.addParameter(sourceFile.toString());

        String arguments =
                configuration.getProgramArguments();

        if (!arguments.isBlank()) {
            commandLine.addParameters(
                    ParametersListUtil.parse(arguments)
            );
        }

        Path workingDirectory =
                resolveWorkingDirectory(sourceFile);

        commandLine.setWorkDirectory(
                workingDirectory.toFile()
        );

        OSProcessHandler processHandler =
                new OSProcessHandler(commandLine);

        ProcessTerminatedListener.attach(processHandler);

        return processHandler;
    }

    private @NotNull Path resolveInterpreter()
            throws ExecutionException {

        String configured =
                configuration.getInterpreterPath();

        if (!configured.isBlank()) {
            return Path.of(configured)
                    .toAbsolutePath()
                    .normalize();
        }

        Path resolved =
                FelidaeExecutableResolver.resolveInterpreter(
                        configuration.getProject()
                );

        if (resolved == null) {
            throw new ExecutionException(
                    "Felidae interpreter not found. " +
                            "Expected build/felidae.exe or FELIDAE_PATH."
            );
        }

        return resolved;
    }

    private @NotNull Path resolveWorkingDirectory(
            @NotNull Path sourceFile
    ) {
        String configured =
                configuration.getWorkingDirectory();

        if (!configured.isBlank()) {
            return Path.of(configured)
                    .toAbsolutePath()
                    .normalize();
        }

        Path parent = sourceFile.getParent();

        return parent != null
                ? parent
                : Path.of(".");
    }
}