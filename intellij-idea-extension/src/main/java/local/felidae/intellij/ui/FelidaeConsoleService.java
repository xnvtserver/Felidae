package local.felidae.intellij.ui;

import com.intellij.execution.ui.ConsoleView;
import com.intellij.execution.ui.ConsoleViewContentType;
import com.intellij.execution.impl.ConsoleViewImpl;
import com.intellij.openapi.Disposable;
import com.intellij.openapi.components.Service;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.Disposer;
import com.intellij.openapi.wm.ToolWindow;
import com.intellij.openapi.wm.ToolWindowManager;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

@Service(Service.Level.PROJECT)
public final class FelidaeConsoleService implements Disposable {

    public static final String TOOL_WINDOW_ID = "Felidae";

    private final Project project;

    private @Nullable ConsoleView consoleView;

    public FelidaeConsoleService(@NotNull Project project) {
        this.project = project;
    }

    public static @NotNull FelidaeConsoleService getInstance(
            @NotNull Project project
    ) {
        return project.getService(FelidaeConsoleService.class);
    }

    public synchronized void attachConsole(
            @NotNull ConsoleView console
    ) {
        if (consoleView != null && consoleView != console) {
            Disposer.dispose(consoleView);
        }

        consoleView = console;
    }

    public synchronized @Nullable ConsoleView getConsoleView() {
        return consoleView;
    }

    public synchronized @NotNull ConsoleView getOrCreateConsole() {
        if (consoleView == null) {
            consoleView = new ConsoleViewImpl(project, false);
        }

        return consoleView;
    }

    public void show() {
        ToolWindow toolWindow = ToolWindowManager
                .getInstance(project)
                .getToolWindow(TOOL_WINDOW_ID);

        if (toolWindow != null) {
            toolWindow.show();
        }
    }

    public void clear() {
        getOrCreateConsole().clear();
    }

    public void printNormal(@NotNull String text) {
        getOrCreateConsole().print(
                text,
                ConsoleViewContentType.NORMAL_OUTPUT
        );
    }

    public void printSystem(@NotNull String text) {
        getOrCreateConsole().print(
                text,
                ConsoleViewContentType.SYSTEM_OUTPUT
        );
    }

    public void printError(@NotNull String text) {
        getOrCreateConsole().print(
                text,
                ConsoleViewContentType.ERROR_OUTPUT
        );
    }

    @Override
    public synchronized void dispose() {
        if (consoleView != null) {
            Disposer.dispose(consoleView);
            consoleView = null;
        }
    }
}