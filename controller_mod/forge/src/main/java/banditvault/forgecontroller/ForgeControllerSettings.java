package banditvault.forgecontroller;

import banditvault.controllercore.ControllerSettingsStore;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Properties;

public final class ForgeControllerSettings extends ControllerSettingsStore {
    private static final ForgeControllerSettings INSTANCE = new ForgeControllerSettings();
    private static volatile boolean loaded;

    private ForgeControllerSettings() {
    }

    public static ForgeControllerSettings get() {
        if (!loaded) {
            load();
        }
        return INSTANCE;
    }

    public static synchronized void load() {
        if (loaded) {
            return;
        }
        loaded = true;
        File file = configFile();
        if (!file.isFile()) {
            save();
            return;
        }

        Properties props = new Properties();
        try (FileInputStream in = new FileInputStream(file)) {
            props.load(in);
        } catch (IOException e) {
            ForgeControllerLog.logException("Forge controller settings failed to load", e);
            return;
        }

        INSTANCE.readFrom(props);
    }

    public static void save() {
        File file = configFile();
        File dir = file.getParentFile();
        if (dir != null) {
            dir.mkdirs();
        }

        Properties props = new Properties();
        INSTANCE.writeTo(props);

        try (FileOutputStream out = new FileOutputStream(file)) {
            props.store(out, "Bandit controller compatibility settings");
        } catch (IOException e) {
            ForgeControllerLog.logException("Forge controller settings failed to save", e);
        }
    }
}
