package local.felidae.intellij.run;

import com.intellij.execution.configurations.ConfigurationTypeBase;
import com.intellij.openapi.project.DumbAware;
import local.felidae.intellij.FelidaeIcons;
import org.jetbrains.annotations.NotNull;

public final class FelidaeConfigurationType
        extends ConfigurationTypeBase
        implements DumbAware {

    public static final String ID =
            "FelidaeRunConfiguration";

    public FelidaeConfigurationType() {
        super(
                ID,
                "Felidae",
                "Run a Felidae source file",
                FelidaeIcons.RUN
        );

        addFactory(new FelidaeConfigurationFactory(this));
    }

    public static @NotNull FelidaeConfigurationType getInstance() {
        return CONFIGURATION_TYPE_EP
                .findExtensionOrFail(FelidaeConfigurationType.class);
    }
}