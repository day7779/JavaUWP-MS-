package banditvault.micrelay;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.LocalTime;

final class MicRelayLog {
    private MicRelayLog() {
    }

    static synchronized void log(String message) {
        write(message);
    }

    static synchronized void log(String message, Throwable t) {
        if (t == null) {
            write(message);
            return;
        }
        StringWriter buffer = new StringWriter();
        PrintWriter writer = new PrintWriter(buffer);
        writer.println(message);
        t.printStackTrace(writer);
        writer.flush();
        write(buffer.toString().trim());
    }

    static void debug(String message) {
        if (RelayConfig.debug()) {
            log(message);
        }
    }

    private static void write(String message) {
        try {
            Path logFile = Path.of(System.getProperty("user.dir", "."), "mic_relay.log");
            String line = "[" + LocalTime.now() + "] [mic_relay] " + message + System.lineSeparator();
            Files.writeString(logFile, line, StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.WRITE, StandardOpenOption.APPEND);
        } catch (Throwable ignored) {
        }
    }
}
