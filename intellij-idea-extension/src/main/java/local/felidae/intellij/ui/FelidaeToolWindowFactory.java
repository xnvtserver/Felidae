package local.felidae.intellij.ui;

import com.intellij.execution.ui.ConsoleView;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.wm.ToolWindow;
import com.intellij.openapi.wm.ToolWindowFactory;
import com.intellij.ui.content.Content;
import com.intellij.ui.content.ContentFactory;
import org.jetbrains.annotations.NotNull;

public final class FelidaeToolWindowFactory
        implements ToolWindowFactory, DumbAware {

    @Override
    public void createToolWindowContent(
            @NotNull Project project,
            @NotNull ToolWindow toolWindow
    ) {
        FelidaeConsoleService service =
                FelidaeConsoleService.getInstance(project);

        ConsoleView consoleView =
                service.getOrCreateConsole();

        Content content = ContentFactory
                .getInstance()
                .createContent(
                        consoleView.getComponent(),
                        "Console",
                        false
                );

        content.setCloseable(false);

        toolWindow
                .getContentManager()
                .addContent(content);

        service.attachConsole(consoleView);
    }

    @Override
    public boolean shouldBeAvailable(
            @NotNull Project project
    ) {
        return true;
    }
}