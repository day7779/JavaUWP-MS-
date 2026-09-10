package banditvault.neoforgecontroller.mixin;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = AbstractContainerScreen.class, remap = false)
public abstract class NeoForgeControllerHandledScreenMixin {
    @Inject(method = "extractRenderState", at = @At("HEAD"), remap = false)
    private void banditvault$updateControllerCursor(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        NeoForgeControllerCompat.updateScreenCursorBeforeRender((Screen) (Object) this, mouseX, mouseY);
    }

    @ModifyVariable(method = "extractRenderState", at = @At("HEAD"), ordinal = 0, argsOnly = true, remap = false)
    private int banditvault$useControllerMouseX(int mouseX) {
        return NeoForgeControllerCompat.screenMouseX((Screen) (Object) this, mouseX);
    }

    @ModifyVariable(method = "extractRenderState", at = @At("HEAD"), ordinal = 1, argsOnly = true, remap = false)
    private int banditvault$useControllerMouseY(int mouseY) {
        return NeoForgeControllerCompat.screenMouseY((Screen) (Object) this, mouseY);
    }

    @Inject(method = "extractRenderState", at = @At("TAIL"), remap = false)
    private void banditvault$renderControllerCursor(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        NeoForgeControllerCompat.renderCursor((Screen) (Object) this, graphics);
    }
}
