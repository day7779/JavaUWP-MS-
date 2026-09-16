package banditvault.neoforgecontroller.mixin;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiGraphics;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = Gui.class, remap = false)
public abstract class NeoForgeControllerHudMixin {
    @Inject(method = "render", at = @At("TAIL"), remap = false)
    private void banditvault$renderControllerGuide(GuiGraphics context, DeltaTracker tickCounter, CallbackInfo ci) {
        NeoForgeControllerCompat.renderGameplayGuide(context);
    }
}
