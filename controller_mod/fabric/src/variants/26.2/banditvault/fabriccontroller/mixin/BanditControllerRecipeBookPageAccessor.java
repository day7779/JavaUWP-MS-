package banditvault.fabriccontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.ImageButton;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookPage;
import net.minecraft.client.gui.screens.recipebook.RecipeButton;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(RecipeBookPage.class)
public interface BanditControllerRecipeBookPageAccessor {
    @Accessor("buttons")
    List<RecipeButton> banditvault$buttons();

    @Accessor("forwardButton")
    ImageButton banditvault$backButton();

    @Accessor("backButton")
    ImageButton banditvault$forwardButton();

    @Accessor("overlay")
    OverlayRecipeComponent banditvault$overlay();
}
