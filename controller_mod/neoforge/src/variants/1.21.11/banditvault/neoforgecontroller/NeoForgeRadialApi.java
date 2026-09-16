package banditvault.neoforgecontroller;

import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;

final class NeoForgeRadialApi {
    private NeoForgeRadialApi() {
    }

    static ItemStack icon(String item) {
        Item resolved = BuiltInRegistries.ITEM.getValue(Identifier.parse("minecraft:" + item));
        return resolved == null ? null : new ItemStack(resolved);
    }
}
