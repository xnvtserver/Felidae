package local.felidae.intellij.settings;

import com.intellij.openapi.components.PersistentStateComponent;
import com.intellij.openapi.components.Service;
import com.intellij.openapi.components.State;
import com.intellij.openapi.components.Storage;
import com.intellij.openapi.project.Project;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;

/**
 * Persists the per-project felidae/felidae_debug/celidae executable path
 * overrides configured on the Felidae settings page. Checked first by
 * FelidaeExecutableResolver, ahead of environment variables and
 * auto-detection, mirroring the VS Code extension's
 * felidae.interpreterPath/debugInterpreterPath/celidaePath settings.
 */
@Service(Service.Level.PROJECT)
@State(name = "FelidaeSettings", storages = @Storage("felidae.xml"))
public final class FelidaeSettingsState implements PersistentStateComponent<FelidaeSettingsState.SettingsData> {

    public static final class SettingsData {
        public String interpreterPath = "";
        public String debuggerPath = "";
        public String celidaePath = "";
    }

    private SettingsData data = new SettingsData();

    public static @NotNull FelidaeSettingsState getInstance(@NotNull Project project) {
        return project.getService(FelidaeSettingsState.class);
    }

    @Override
    public @NotNull SettingsData getState() {
        return data;
    }

    @Override
    public void loadState(@NotNull SettingsData state) {
        this.data = state;
    }

    public @NotNull String getInterpreterPath() {
        return data.interpreterPath;
    }

    public void setInterpreterPath(@NotNull String value) {
        data.interpreterPath = value;
    }

    public @NotNull String getDebuggerPath() {
        return data.debuggerPath;
    }

    public void setDebuggerPath(@NotNull String value) {
        data.debuggerPath = value;
    }

    public @NotNull String getCelidaePath() {
        return data.celidaePath;
    }

    public void setCelidaePath(@NotNull String value) {
        data.celidaePath = value;
    }

    public @Nullable Path resolvedInterpreterPath() {
        return resolved(data.interpreterPath);
    }

    public @Nullable Path resolvedDebuggerPath() {
        return resolved(data.debuggerPath);
    }

    public @Nullable Path resolvedCelidaePath() {
        return resolved(data.celidaePath);
    }

    private static @Nullable Path resolved(@Nullable String raw) {
        if (raw == null || raw.isBlank()) {
            return null;
        }
        try {
            Path path = Path.of(raw).toAbsolutePath().normalize();
            return Files.isRegularFile(path) ? path : null;
        } catch (InvalidPathException exception) {
            return null;
        }
    }
}
