package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.XboxCompatLog;
import java.nio.file.AccessMode;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.Set;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(targets = "com.cobblemon.mod.common.battles.runner.graal.GraalShowdownService$createContext$1", remap = false)
public abstract class CobblemonShowdownFileSystemMixin {
    @Inject(method = "checkAccess", at = @At("HEAD"), cancellable = true, require = 0)
    private void banditvault$allowShowdownAccess(Path path, Set<? extends AccessMode> modes, LinkOption[] linkOptions, CallbackInfo ci) {
        XboxCompatLog.log("Bypassing Cobblemon Showdown realpath access check for Xbox sandbox");
        ci.cancel();
    }
}
