package banditvault.forgecontroller.mixin;

import banditvault.controllercore.ControllerLog;
import banditvault.forgecontroller.ForgeControllerCompat;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.screens.TitleScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = TitleScreen.class, remap = false)
public abstract class ForgeControllerTitleScreenMixin {
    private static boolean banditvault$playableLogged = false;

    @Inject(method = "m_88315_", at = @At("HEAD"), remap = false)
    private void banditvault$updateControllerCursor(GuiGraphics graphics, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        banditvault$logPlayableOnce();
        ForgeControllerCompat.updateScreenCursorBeforeRender((TitleScreen) (Object) this, mouseX, mouseY);
    }

    private static synchronized void banditvault$logPlayableOnce() {
        if (banditvault$playableLogged) {
            return;
        }
        banditvault$playableLogged = true;
        ControllerLog.log("xbox_compat", "banditvault:playable");
    }

    @ModifyVariable(method = "m_88315_", at = @At("HEAD"), ordinal = 0, argsOnly = true, remap = false)
    private int banditvault$useControllerMouseX(int mouseX) {
        return ForgeControllerCompat.screenMouseX(mouseX);
    }

    @ModifyVariable(method = "m_88315_", at = @At("HEAD"), ordinal = 1, argsOnly = true, remap = false)
    private int banditvault$useControllerMouseY(int mouseY) {
        return ForgeControllerCompat.screenMouseY(mouseY);
    }
}
