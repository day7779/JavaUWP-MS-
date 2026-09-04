package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.ZipFsPathResolver;
import java.io.IOException;
import java.net.URI;
import java.nio.file.Path;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;

@Mixin(net.minecraft.class_10619.class)
public abstract class ZipFsBypassMixin {
    /**
     * @author BanditVault
     * @reason ZipFileSystemProvider's URI path calls toRealPath(), which fails in Xbox Dev Mode.
     */
    @Overwrite
    public static Path method_66590(URI uri) throws IOException {
        return ZipFsPathResolver.resolve(uri);
    }
}
