package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.DeltaTracker;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Gui.class)
public abstract class BanditControllerHudMixin {
    @Inject(method = "extractRenderState", at = @At("TAIL"))
    private void banditvault$renderControllerGuide(GuiGraphicsExtractor context, DeltaTracker tickCounter, CallbackInfo ci) {
        BanditControllerCompat.renderGameplayGuide(context);
    }
}
