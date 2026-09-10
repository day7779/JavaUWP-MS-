package banditvault.neoforgecontroller;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;

public final class NeoForgeClientApi {
    private NeoForgeClientApi() {
    }

    public static Screen screen(Minecraft client) {
        return client.gui.screen();
    }

    public static void setScreen(Minecraft client, Screen screen) {
        client.gui.setScreen(screen);
    }

    public static boolean isHudHidden(Minecraft client) {
        return client.gui.hud.isHidden();
    }
}
