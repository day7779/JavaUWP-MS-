package banditvault.forgecontroller;

import banditvault.controllercore.ControllerLog;

public final class ForgeControllerLog {
    private static final String TAG = "forge_controller";

    private ForgeControllerLog() {
    }

    public static void log(String message) {
        ControllerLog.log(TAG, message);
    }

    public static void logException(String message, Throwable throwable) {
        ControllerLog.logException(TAG, message, throwable);
    }
}
