package banditvault.neoforgecontroller;

import net.minecraft.client.Options;
import net.minecraft.client.KeyMapping;

final class NeoForgeControllerKeys {
    final KeyMapping forward;
    final KeyMapping back;
    final KeyMapping left;
    final KeyMapping right;
    final KeyMapping jump;
    final KeyMapping sneak;
    final KeyMapping sprint;
    final KeyMapping attack;
    final KeyMapping use;
    final KeyMapping inventory;
    final KeyMapping drop;
    final KeyMapping swapOffhand;
    final KeyMapping pickItem;

    NeoForgeControllerKeys(Options options) {
        forward = options.keyUp;
        back = options.keyDown;
        left = options.keyLeft;
        right = options.keyRight;
        jump = options.keyJump;
        sneak = options.keyShift;
        sprint = options.keySprint;
        attack = options.keyAttack;
        use = options.keyUse;
        inventory = options.keyInventory;
        drop = options.keyDrop;
        swapOffhand = options.keySwapOffhand;
        pickItem = options.keyPickItem;
    }
}
