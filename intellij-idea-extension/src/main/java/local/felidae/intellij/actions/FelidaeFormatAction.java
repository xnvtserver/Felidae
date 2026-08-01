package local.felidae.intellij.actions;

import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.command.WriteCommandAction;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.project.Project;
import local.felidae.intellij.formatting.FelidaeFormatter;
import org.jetbrains.annotations.NotNull;

/**
 * Reformats the current Felidae document in place. There is no PSI grammar
 * for .fx files (every one is a PsiPlainTextFile - see FelidaeFileType), so
 * this can't hook into IntelliJ's native FormattingModelBuilder / "Reformat
 * Code" - it directly rewrites the Document text via FelidaeFormatter,
 * which mirrors vs-code-extension's DocumentFormattingEditProvider.
 */
public final class FelidaeFormatAction
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
        Project project = FelidaeActionSupport.getProject(event);
        Editor editor = event.getData(CommonDataKeys.EDITOR);

        if (project == null || editor == null) {
            return;
        }

        Document document = editor.getDocument();
        String newText = FelidaeFormatter.formatSource(document.getText());

        if (newText.equals(document.getText())) {
            return;
        }

        WriteCommandAction.runWriteCommandAction(
                project,
                "Format Felidae File",
                null,
                () -> document.setText(newText)
        );
    }
}
