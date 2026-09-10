package banditvault.neoforgecontroller.mixin;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import banditvault.neoforgecontroller.NeoForgeClientApi;
import java.util.Collections;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.options.controls.ControlsScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = ControlsScreen.class, remap = false)
public abstract class NeoForgeControllerControlsScreenMixin {
    @Inject(method = "addOptions", at = @At("TAIL"), remap = false)
    private void banditvault$addControllerSettingsButton(CallbackInfo ci) {
        Screen screen = (Screen) (Object) this;
        Button button = NeoForgeControllerCompat.createButton(0, 0, 150, 20, "Bandit Controller...", ignored ->
            NeoForgeClientApi.setScreen(Minecraft.getInstance(), NeoForgeControllerCompat.createSettingsScreen(screen)));
        ((NeoForgeControllerOptionsSubScreenAccessor) this).banditvault$optionsList()
            .addSmall(Collections.<AbstractWidget>singletonList(button));
    }
}
