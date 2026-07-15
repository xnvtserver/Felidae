package local.felidae.intellij.actions;

import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import local.felidae.intellij.FelidaeFileType;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;

final class FelidaeActionSupport {

    private FelidaeActionSupport() {
    }

    static boolean isFelidaeFile(
            @Nullable VirtualFile file
    ) {
        if (file == null || file.isDirectory()) {
            return false;
        }

        String extension = file.getExtension();

        return extension != null &&
                FelidaeFileType.DEFAULT_EXTENSION
                        .equalsIgnoreCase(extension);
    }

    static @Nullable Project getProject(
            @NotNull AnActionEvent event
    ) {
        return event.getProject();
    }

    static @Nullable VirtualFile getSelectedFile(
            @NotNull AnActionEvent event
    ) {
        return event.getData(CommonDataKeys.VIRTUAL_FILE);
    }

    static @Nullable Path prepareSourceFile(
            @NotNull VirtualFile virtualFile
    ) {
        Document document = FileDocumentManager
                .getInstance()
                .getDocument(virtualFile);

        if (document != null) {
            FileDocumentManager
                    .getInstance()
                    .saveDocument(document);
        }

        try {
            return Path.of(virtualFile.getPath())
                    .toAbsolutePath()
                    .normalize();
        } catch (InvalidPathException exception) {
            return null;
        }
    }

    static void updateAvailability(
            @NotNull AnActionEvent event
    ) {
        Project project = getProject(event);
        VirtualFile file = getSelectedFile(event);

        boolean enabled =
                project != null &&
                        !project.isDisposed() &&
                        isFelidaeFile(file);

        event.getPresentation().setEnabledAndVisible(enabled);
    }
}