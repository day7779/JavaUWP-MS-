package banditvault.controllercore;

import java.io.File;
import java.util.Properties;

public class ControllerSettingsStore {
    public boolean toggleCrouch = false;
    public boolean toggleSprint = false;
    public boolean invertY = false;
    public float lookSpeed = 135.0f;
    public double cursorSpeed = 18.0;
    public double scrollAmount = 1.0;
    public float moveDeadzone = 0.35f;
    public float lookDeadzone = 0.12f;
    public float cursorDeadzone = 0.12f;
    public float triggerDeadzone = 0.25f;

    protected ControllerSettingsStore() {
    }

    public static File configFile() {
        return new File(new File(System.getProperty("user.dir", "."), "config"), "bandit-controller.properties");
    }

    public void readFrom(Properties props) {
        toggleCrouch = bool(props, "toggleCrouch", toggleCrouch);
        toggleSprint = bool(props, "toggleSprint", toggleSprint);
        invertY = bool(props, "invertY", invertY);
        lookSpeed = (float) range(number(props, "lookSpeed", lookSpeed), 30.0, 300.0);
        cursorSpeed = range(number(props, "cursorSpeed", cursorSpeed), 4.0, 60.0);
        scrollAmount = range(number(props, "scrollAmount", scrollAmount), 0.25, 4.0);
        moveDeadzone = (float) range(number(props, "moveDeadzone", moveDeadzone), 0.0, 0.75);
        lookDeadzone = (float) range(number(props, "lookDeadzone", lookDeadzone), 0.0, 0.75);
        cursorDeadzone = (float) range(number(props, "cursorDeadzone", cursorDeadzone), 0.0, 0.75);
        triggerDeadzone = (float) range(number(props, "triggerDeadzone", triggerDeadzone), 0.0, 0.95);
    }

    public void writeTo(Properties props) {
        props.setProperty("toggleCrouch", Boolean.toString(toggleCrouch));
        props.setProperty("toggleSprint", Boolean.toString(toggleSprint));
        props.setProperty("invertY", Boolean.toString(invertY));
        props.setProperty("lookSpeed", Float.toString(lookSpeed));
        props.setProperty("cursorSpeed", Double.toString(cursorSpeed));
        props.setProperty("scrollAmount", Double.toString(scrollAmount));
        props.setProperty("moveDeadzone", Float.toString(moveDeadzone));
        props.setProperty("lookDeadzone", Float.toString(lookDeadzone));
        props.setProperty("cursorDeadzone", Float.toString(cursorDeadzone));
        props.setProperty("triggerDeadzone", Float.toString(triggerDeadzone));
    }

    private static boolean bool(Properties props, String key, boolean fallback) {
        String value = props.getProperty(key);
        if (value == null) {
            return fallback;
        }
        return "true".equalsIgnoreCase(value)
            || "1".equals(value)
            || "yes".equalsIgnoreCase(value)
            || "on".equalsIgnoreCase(value);
    }

    private static double number(Properties props, String key, double fallback) {
        String value = props.getProperty(key);
        if (value == null) {
            return fallback;
        }
        try {
            return Double.parseDouble(value.trim());
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    private static double range(double value, double min, double max) {
        if (value < min) {
            return min;
        }
        if (value > max) {
            return max;
        }
        return value;
    }
}
