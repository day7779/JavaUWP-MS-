package banditvault.fabriccontroller;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;

public final class FabricClientApi {
    private FabricClientApi() {
    }

    public static Screen screen(Minecraft client) {
        return client.screen;
    }

    public static void setScreen(Minecraft client, Screen screen) {
        client.setScreen(screen);
    }

    public static boolean isHudHidden(Minecraft client) {
        return client.options.hideGui;
    }
}
