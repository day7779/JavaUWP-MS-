package banditvault.neoforgecontroller;

import banditvault.controllercore.ControllerLog;

public final class NeoForgeControllerLog {
    private static final String TAG = "neoforge_controller";

    private NeoForgeControllerLog() {
    }

    public static void log(String message) {
        ControllerLog.log(TAG, message);
    }

    public static void logException(String message, Throwable throwable) {
        ControllerLog.logException(TAG, message, throwable);
    }
}
