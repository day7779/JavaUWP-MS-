package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerLog;

public final class FabricControllerLog {
    private static final String TAG = "fabric_controller";

    private FabricControllerLog() {
    }

    public static void log(String message) {
        ControllerLog.log(TAG, message);
    }

    public static void logException(String message, Throwable throwable) {
        ControllerLog.logException(TAG, message, throwable);
    }
}
