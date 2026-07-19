package local.felidae.intellij.actions;

import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import local.felidae.intellij.execution.FelidaeExecutableResolver;
import local.felidae.intellij.execution.FelidaeProcessRunner;
import local.felidae.intellij.ui.FelidaeConsoleService;
import org.jetbrains.annotations.NotNull;

import java.nio.file.Path;

public final class FelidaeCheckAction
        extends AnAction
        implements DumbAware {

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

        Path debugger =
                FelidaeExecutableResolver.resolveDebugger(project);

        if (debugger == null) {
            FelidaeConsoleService console =
                    FelidaeConsoleService.getInstance(project);

            console.show();
            console.clear();
            console.printError(
                    "Celidae debugger executable was not found.\n\n" +
                            "Expected location:\n" +
                            "  <project>/build/celidae.exe\n\n" +
                            "Legacy fallback:\n" +
                            "  <project>/build/felidae_debug.exe\n\n" +
                            "Alternatively, configure the CELIDAE_PATH " +
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

        FelidaeProcessRunner.runChecker(
                project,
                debugger,
                sourceFile
        );
    }
}
