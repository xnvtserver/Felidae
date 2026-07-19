package local.felidae.intellij;

import com.intellij.openapi.util.IconLoader;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

/**
 * Central registry for Felidae plugin icons.
 *
 * IntelliJ automatically loads the corresponding *_dark.svg resource
 * when the IDE uses a dark theme.
 *
 * Example:
 *   /icons/felidaeFile.svg
 *   /icons/felidaeFile_dark.svg
 */
public final class FelidaeIcons {

    /*
     * File-type icon.
     *
     * Recommended resource size: 16x16
     * Used in the project tree, navigation lists, recent files,
     * editor tabs and file-type settings.
     */
    public static final @NotNull Icon FILE =
            load("/icons/felidaeFile.svg");

    /*
     * Generic toolbar/menu action icon.
     *
     * Recommended resource size: 16x16
     */
    public static final @NotNull Icon ACTION =
            load("/icons/felidaeAction.svg");

    /*
     * Tool-window stripe icon.
     *
     * Recommended resource size: 20x20.
     * Keep this visually simpler than the full file icon.
     */
    public static final @NotNull Icon TOOL_WINDOW =
            load("/icons/felidaeToolWindow.svg");

    /*
     * Medium product/language logo.
     *
     * Recommended resource size: 32x32.
     * Suitable for settings pages, dialogs and empty states.
     */
    public static final @NotNull Icon LOGO =
            load("/icons/felidaeLogo.svg");

    /*
     * Run or execute Felidae source.
     *
     * Recommended resource size: 16x16.
     */
    public static final @NotNull Icon RUN =
            load("/icons/felidaeRun.svg");

    /*
     * Run Celidae using --check-json.
     *
     * Recommended resource size: 16x16.
     */
    public static final @NotNull Icon CHECK =
            load("/icons/felidaeCheck.svg");

    /*
     * Error state used by custom Felidae UI components.
     *
     * Prefer IntelliJ's built-in error icon when no Felidae branding
     * is required.
     */
    public static final @NotNull Icon ERROR =
            load("/icons/felidaeError.svg");

    /*
     * Warning state used by custom Felidae UI components.
     */
    public static final @NotNull Icon WARNING =
            load("/icons/felidaeWarning.svg");

    private FelidaeIcons() {
        throw new AssertionError(
                "FelidaeIcons is a utility class and cannot be instantiated."
        );
    }

    private static @NotNull Icon load(@NotNull String path) {
        return IconLoader.getIcon(path, FelidaeIcons.class);
    }
}
