package banditvault.controllercore;

import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.OpenOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.time.LocalTime;

public final class ControllerLog {
    private static final Path LOG_PATH = Paths.get(System.getProperty("user.dir", "."), "xbox_compat.log");
    private static final OpenOption[] OPEN_OPTIONS = new OpenOption[] {
        StandardOpenOption.CREATE,
        StandardOpenOption.WRITE,
        StandardOpenOption.APPEND
    };

    private ControllerLog() {
    }

    // callers are one-shot or throttled, so reopening per line keeps the file consistent
    public static synchronized void log(String tag, String message) {
        String line = "[" + LocalTime.now() + "] [" + tag + "] " + message + System.lineSeparator();
        try (OutputStream out = Files.newOutputStream(LOG_PATH, OPEN_OPTIONS)) {
            out.write(line.getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
    }

    public static void logException(String tag, String message, Throwable throwable) {
        StringWriter buffer = new StringWriter();
        try (PrintWriter writer = new PrintWriter(buffer)) {
            writer.println(message);
            if (throwable != null) {
                throwable.printStackTrace(writer);
            }
        }
        log(tag, trimTrailing(buffer.toString()));
    }

    private static String trimTrailing(String value) {
        int end = value.length();
        while (end > 0 && Character.isWhitespace(value.charAt(end - 1))) {
            end--;
        }
        return value.substring(0, end);
    }
}
