package local.felidae.intellij.execution;

import com.intellij.execution.configurations.GeneralCommandLine;
import com.intellij.execution.process.OSProcessHandler;
import com.intellij.execution.process.ProcessAdapter;
import com.intellij.execution.process.ProcessEvent;
import com.intellij.execution.process.ProcessHandler;
import com.intellij.execution.ui.ConsoleView;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.progress.ProgressIndicator;
import com.intellij.openapi.progress.Task;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.Key;
import local.felidae.intellij.ui.FelidaeConsoleService;
import org.jetbrains.annotations.NotNull;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class FelidaeProcessRunner {

    private FelidaeProcessRunner() {
        throw new AssertionError(
                "FelidaeProcessRunner cannot be instantiated."
        );
    }

    public static void runInterpreter(
            @NotNull Project project,
            @NotNull Path interpreter,
            @NotNull Path sourceFile
    ) {
        run(
                project,
                "Run Felidae",
                interpreter,
                sourceFile,
                List.of()
        );
    }

    public static void runQuery(
            @NotNull Project project,
            @NotNull Path interpreter,
            @NotNull Path sourceFile,
            @NotNull String query
    ) {
        run(
                project,
                "Run Felidae Query",
                interpreter,
                sourceFile,
                List.of(query)
        );
    }

    public static void runChecker(
            @NotNull Project project,
            @NotNull Path debugger,
            @NotNull Path sourceFile
    ) {
        run(
                project,
                "Check Felidae",
                debugger,
                sourceFile,
                List.of("--check-json")
        );
    }

    public static void inspectGraph(
            @NotNull Project project,
            @NotNull Path celidae,
            @NotNull Path sourceFile
    ) {
        run(
                project,
                "Visualize Felidae Data",
                celidae,
                sourceFile,
                List.of("--inspect-graph")
        );
    }

    private static void run(
            @NotNull Project project,
            @NotNull String taskTitle,
            @NotNull Path executable,
            @NotNull Path sourceFile,
            @NotNull List<String> trailingArguments
    ) {
        FelidaeConsoleService consoleService =
                FelidaeConsoleService.getInstance(project);

        consoleService.show();
        consoleService.clear();

        if (!Files.isRegularFile(executable)) {
            consoleService.printError(
                    "Executable not found: " + executable + "\n"
            );
            return;
        }

        if (!Files.isRegularFile(sourceFile)) {
            consoleService.printError(
                    "Source file not found: " + sourceFile + "\n"
            );
            return;
        }

        new Task.Backgroundable(
                project,
                taskTitle,
                true
        ) {
            @Override
            public void run(
                    @NotNull ProgressIndicator indicator
            ) {
                indicator.setIndeterminate(true);
                indicator.setText(taskTitle);

                execute(
                        project,
                        executable,
                        sourceFile,
                        trailingArguments,
                        indicator,
                        consoleService
                );
            }
        }.queue();
    }

    private static void execute(
            @NotNull Project project,
            @NotNull Path executable,
            @NotNull Path sourceFile,
            @NotNull List<String> trailingArguments,
            @NotNull ProgressIndicator indicator,
            @NotNull FelidaeConsoleService consoleService
    ) {
        GeneralCommandLine commandLine =
                new GeneralCommandLine();

        commandLine.setExePath(executable.toString());
        commandLine.setCharset(StandardCharsets.UTF_8);

        Path workingDirectory = sourceFile.getParent();

        if (workingDirectory != null) {
            commandLine.setWorkDirectory(
                    workingDirectory.toFile()
            );
        }

        /*
         * Interpreter:
         *   felidae.exe file.fx
         *
         * Felidae AST debugger:
         *   felidae_debug.exe file.fx --check-json
         */
        commandLine.addParameter(sourceFile.toString());
        commandLine.addParameters(trailingArguments);

        ApplicationManager.getApplication().invokeLater(() -> {
            consoleService.printSystem(
                    "$ " + commandLine.getCommandLineString() + "\n\n"
            );
        });

        try {
            OSProcessHandler processHandler =
                    new OSProcessHandler(commandLine);

            ConsoleView consoleView =
                    consoleService.getOrCreateConsole();

            ApplicationManager.getApplication().invokeLater(() -> {
                consoleView.attachToProcess(processHandler);
            });

            registerProcessListener(
                    processHandler,
                    consoleService
            );

            processHandler.startNotify();

            while (!processHandler.isProcessTerminated()) {
                if (indicator.isCanceled()) {
                    processHandler.destroyProcess();

                    ApplicationManager
                            .getApplication()
                            .invokeLater(() ->
                                    consoleService.printSystem(
                                            "\nFelidae process cancelled.\n"
                                    )
                            );

                    return;
                }

                try {
                    Thread.sleep(100);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    processHandler.destroyProcess();
                    return;
                }
            }
        } catch (Exception exception) {
            ApplicationManager.getApplication().invokeLater(() ->
                    consoleService.printError(
                            "Cannot start Felidae process: " +
                                    safeMessage(exception) +
                                    "\n"
                    )
            );
        }
    }

    private static void registerProcessListener(
            @NotNull ProcessHandler processHandler,
            @NotNull FelidaeConsoleService consoleService
    ) {
        processHandler.addProcessListener(
                new ProcessAdapter() {
                    @Override
                    public void processTerminated(
                            @NotNull ProcessEvent event
                    ) {
                        int exitCode = event.getExitCode();

                        ApplicationManager
                                .getApplication()
                                .invokeLater(() -> {
                                    if (exitCode == 0) {
                                        consoleService.printSystem(
                                                "\nProcess finished successfully.\n"
                                        );
                                    } else {
                                        consoleService.printError(
                                                "\nProcess finished with exit code " +
                                                        exitCode +
                                                        ".\n"
                                        );
                                    }
                                });
                    }

                    @Override
                    public void onTextAvailable(
                            @NotNull ProcessEvent event,
                            @NotNull Key outputType
                    ) {
                        /*
                         * ConsoleView.attachToProcess() already receives and
                         * displays output. Do not print event.getText() here,
                         * otherwise every output line appears twice.
                         */
                    }
                }
        );
    }

    private static @NotNull String safeMessage(
            @NotNull Throwable throwable
    ) {
        String message = throwable.getMessage();

        return message == null || message.isBlank()
                ? throwable.getClass().getSimpleName()
                : message;
    }
}
