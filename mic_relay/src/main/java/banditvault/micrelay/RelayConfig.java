package banditvault.micrelay;

final class RelayConfig {
    private static final String PREFIX = "banditvault.micrelay.";
    private static final String DEFAULT_NAME = "Bandit Relay Microphone";

    private RelayConfig() {
    }

    static boolean enabled() {
        return !"false".equalsIgnoreCase(property("enabled", "true").trim());
    }

    static int port() {
        return intProperty("port", 7340, 1, 65535);
    }

    static String name() {
        String name = property("name", DEFAULT_NAME).trim();
        return name.isEmpty() ? DEFAULT_NAME : name;
    }

    static int bufferMs() {
        return intProperty("bufferMs", 200, 40, 2000);
    }

    static int underrunMs() {
        return intProperty("underrunMs", 40, 0, 250);
    }

    static boolean debug() {
        return "true".equalsIgnoreCase(property("debug", "false").trim());
    }

    static int bufferBytes() {
        int bytes = bufferMs() * 96;
        return Math.max(bytes & ~1, 4096);
    }

    private static String property(String key, String defaultValue) {
        try {
            String value = System.getProperty(PREFIX + key);
            return value == null ? defaultValue : value;
        } catch (RuntimeException e) {
            return defaultValue;
        }
    }

    private static int intProperty(String key, int defaultValue, int min, int max) {
        try {
            int value = Integer.parseInt(property(key, "").trim());
            return (value < min || value > max) ? defaultValue : value;
        } catch (RuntimeException e) {
            return defaultValue;
        }
    }
}
