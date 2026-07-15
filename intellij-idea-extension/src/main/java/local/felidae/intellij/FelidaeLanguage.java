package local.felidae.intellij;

import com.intellij.lang.Language;
import org.jetbrains.annotations.NotNull;

public final class FelidaeLanguage extends Language {

    public static final FelidaeLanguage INSTANCE =
            new FelidaeLanguage();

    private FelidaeLanguage() {
        super("Felidae");
    }

    @Override
    public @NotNull String getDisplayName() {
        return "Felidae";
    }
}