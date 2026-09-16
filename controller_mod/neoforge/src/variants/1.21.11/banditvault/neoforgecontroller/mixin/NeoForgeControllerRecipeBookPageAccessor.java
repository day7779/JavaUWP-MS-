package banditvault.neoforgecontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.ImageButton;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookPage;
import net.minecraft.client.gui.screens.recipebook.RecipeButton;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(value = RecipeBookPage.class, remap = false)
public interface NeoForgeControllerRecipeBookPageAccessor {
    @Accessor("buttons")
    List<RecipeButton> banditvault$buttons();

    @Accessor("forwardButton")
    ImageButton banditvault$forwardButton();

    @Accessor("backButton")
    ImageButton banditvault$backButton();

    @Accessor("overlay")
    OverlayRecipeComponent banditvault$overlay();
}
