package local.felidae.intellij;

import com.intellij.openapi.fileTypes.LanguageFileType;
import com.intellij.openapi.util.NlsContexts;
import org.jetbrains.annotations.NonNls;
import org.jetbrains.annotations.NotNull;

import javax.swing.Icon;

public final class FelidaeFileType extends LanguageFileType {

    public static final FelidaeFileType INSTANCE =
            new FelidaeFileType();

    public static final @NonNls String NAME =
            "Felidae";

    public static final @NlsContexts.Label String DESCRIPTION =
            "Felidae source file";

    public static final @NonNls String DEFAULT_EXTENSION =
            "fx";

    private FelidaeFileType() {
        super(FelidaeLanguage.INSTANCE);
    }

    @Override
    public @NonNls @NotNull String getName() {
        return NAME;
    }

    @Override
    public @NlsContexts.Label @NotNull String getDescription() {
        return DESCRIPTION;
    }

    @Override
    public @NonNls @NotNull String getDefaultExtension() {
        return DEFAULT_EXTENSION;
    }

    @Override
    public @NotNull Icon getIcon() {
        return FelidaeIcons.FILE;
    }
}