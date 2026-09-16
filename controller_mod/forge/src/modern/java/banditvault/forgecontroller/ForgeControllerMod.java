package banditvault.forgecontroller;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import banditvault.neoforgecontroller.NeoForgeControllerSettings;
import net.minecraftforge.fml.common.Mod;

@Mod(ForgeControllerMod.MOD_ID)
public final class ForgeControllerMod {
    public static final String MOD_ID = "banditvault_forge_controller";

    public ForgeControllerMod() {
        NeoForgeControllerSettings.load();
        NeoForgeControllerCompat.ensureInitialized();
    }
}
