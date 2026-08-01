package local.felidae.intellij.actions;

import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.Messages;
import com.intellij.openapi.vfs.VirtualFile;
import local.felidae.intellij.execution.FelidaeExecutableResolver;
import local.felidae.intellij.execution.FelidaeProcessRunner;
import local.felidae.intellij.ui.FelidaeConsoleService;
import org.jetbrains.annotations.NotNull;

import java.nio.file.Path;

public final class FelidaeRunQueryAction
        extends AnAction
        implements DumbAware {

    private static final String DEFAULT_QUERY = "? Engineer(name: name)";

    @Override
    public void update(
            @NotNull AnActionEvent event
    ) {
        FelidaeActionSupport.updateAvailability(event);
    }

    @Override
    public void actionPerformed(
            @NotNull AnActionEvent event
    ) {
        Project project =
                FelidaeActionSupport.getProject(event);

        VirtualFile virtualFile =
                FelidaeActionSupport.getSelectedFile(event);

        if (project == null ||
                virtualFile == null ||
                !FelidaeActionSupport.isFelidaeFile(virtualFile)) {
            return;
        }

        String query = Messages.showInputDialog(
                project,
                "Felidae query to run against this program:",
                "Run Felidae Query",
                null,
                DEFAULT_QUERY,
                null
        );

        if (query == null || query.isBlank()) {
            return;
        }

        Path interpreter =
                FelidaeExecutableResolver.resolveInterpreter(project);

        if (interpreter == null) {
            FelidaeConsoleService console =
                    FelidaeConsoleService.getInstance(project);

            console.show();
            console.clear();
            console.printError(
                    "Felidae interpreter was not found.\n\n" +
                            "Expected location:\n" +
                            "  <project>/build/felidae.exe\n\n" +
                            "Alternatively, configure the FELIDAE_PATH " +
                            "environment variable.\n"
            );

            return;
        }

        Path sourceFile =
                FelidaeActionSupport.prepareSourceFile(virtualFile);

        if (sourceFile == null) {
            FelidaeConsoleService console =
                    FelidaeConsoleService.getInstance(project);

            console.show();
            console.printError(
                    "Cannot resolve source-file path: " +
                            virtualFile.getPath() +
                            "\n"
            );

            return;
        }

        FelidaeProcessRunner.runQuery(
                project,
                interpreter,
                sourceFile,
                query.trim()
        );
    }
}
