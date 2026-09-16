package banditvault.neoforgecontroller.mixin;

import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.inventory.Slot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(value = AbstractContainerScreen.class, remap = false)
public interface NeoForgeControllerContainerAccessor {
    @Accessor("leftPos")
    int banditvault$leftPos();

    @Accessor("topPos")
    int banditvault$topPos();

    @Accessor("hoveredSlot")
    Slot banditvault$getSlotUnderMouse();

    @Invoker("slotClicked")
    void banditvault$slotClicked(Slot slot, int slotId, int button, ContainerInput actionType);
}
