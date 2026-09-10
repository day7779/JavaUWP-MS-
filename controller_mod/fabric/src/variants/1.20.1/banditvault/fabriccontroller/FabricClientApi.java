package banditvault.fabriccontroller;

import net.minecraft.class_310;
import net.minecraft.class_437;

public final class FabricClientApi {
    private FabricClientApi() {
    }

    public static class_437 screen(class_310 client) {
        return client.field_1755;
    }

    public static void setScreen(class_310 client, class_437 screen) {
        client.method_1507(screen);
    }

    public static boolean isHudHidden(class_310 client) {
        return client.field_1690.field_1842;
    }
}
