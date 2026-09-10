package banditvault.neoforgecontroller;

import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerInput;
import java.util.Locale;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

public final class NeoForgeControllerSettingsScreen extends Screen {
    private final Screen parent;
    private Tab tab = Tab.CONTROLS;
    private int page;

    public NeoForgeControllerSettingsScreen(Screen parent) {
        super(Component.literal("Bandit Controller"));
        this.parent = parent;
    }

    @Override
    protected void init() {
        rebuildButtons();
    }

    @Override
    public void onClose() {
        close();
    }

    public void close() {
        NeoForgeControllerSettings.save();
        if (this.minecraft != null) {
            NeoForgeClientApi.setScreen(this.minecraft, parent);
        }
    }

    private void rebuildButtons() {
        clearWidgets();
        int width = Math.min(520, Math.max(280, this.width - 24));
        int left = this.width / 2 - width / 2;
        int tabWidth = width / Tab.values().length;
        int top = this.height < 260 ? 16 : 28;

        for (Tab value : Tab.values()) {
            int x = left + value.ordinal() * tabWidth;
            String label = (value == tab ? "[" : "") + value.label + (value == tab ? "]" : "");
            addButton(x, top, tabWidth - 2, label, () -> switchTab(value));
        }

        int listTop = top + 26;
        int footerTop = Math.max(listTop + 50, this.height - 50);
        int rows = Math.max(2, Math.min(7, (footerTop - listTop) / 23));
        int count = itemCount();
        int pages = Math.max(1, (count + rows - 1) / rows);
        page = Math.max(0, Math.min(page, pages - 1));
        int start = page * rows;
        int end = Math.min(count, start + rows);

        for (int index = start; index < end; index++) {
            int y = listTop + (index - start) * 23;
            addRow(left, y, width, index);
        }

        int small = 76;
        addButton(left, footerTop, small, "Reset page", this::resetPage);
        if (pages > 1) {
            addButton(this.width / 2 - 82, footerTop, 36, "<", () -> changePage(-1));
            Button pageLabel = NeoForgeControllerCompat.createButton(this.width / 2 - 42, footerTop, 84, 20, (page + 1) + " / " + pages, button -> {});
            pageLabel.active = false;
            addRenderableWidget(pageLabel);
            addButton(this.width / 2 + 46, footerTop, 36, ">", () -> changePage(1));
        }
        addButton(left + width - small, footerTop, small, "Done", this::close);
    }

    private void addRow(int x, int y, int width, int index) {
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        switch (tab) {
            case CONTROLS:
                ControllerAction action = ControllerAction.values()[index];
                addButton(x, y, width, action.label + "  " + settings.binding(action).label, () -> {
                    settings.rebindController(action, nextInput(settings.binding(action)));
                    saveAndRebuild();
                });
                break;
            case SETTINGS:
                addSettingRow(x, y, width, index, settings);
                break;
            case JAVA:
                KeyMapping key = javaKeys()[index];
                ControllerInput input = settings.javaBinding(key.getName());
                addButton(x, y, width, shorten(Component.translatable(key.getName()).getString(), 38) + "  " + input.label, () -> {
                    settings.rebindJava(key.getName(), nextInput(input));
                    saveAndRebuild();
                });
                break;
            case RADIAL:
                String keyId = settings.radialSlot(index);
                addButton(x, y, width, "Slot " + (index + 1) + "  " + shorten(NeoForgeControllerCompat.radialKeyLabel(keyId), 42), () -> {
                    settings.setRadialSlot(index, nextRadialKey(keyId));
                    saveAndRebuild();
                });
                break;
        }
    }

    private void addSettingRow(int x, int y, int width, int index, NeoForgeControllerSettings settings) {
        switch (index) {
            case 0:
                addButton(x, y, width, "Crouch  " + (settings.toggleCrouch ? "Toggle" : "Hold"), () -> {
                    settings.toggleCrouch = !settings.toggleCrouch;
                    saveAndRebuild();
                });
                break;
            case 1:
                addButton(x, y, width, "Sprint  " + (settings.toggleSprint ? "Toggle" : "Hold"), () -> {
                    settings.toggleSprint = !settings.toggleSprint;
                    saveAndRebuild();
                });
                break;
            case 2:
                addButton(x, y, width, "Invert Y  " + (settings.invertY ? "On" : "Off"), () -> {
                    settings.invertY = !settings.invertY;
                    saveAndRebuild();
                });
                break;
            case 3: addNumber(x, y, width, "Look", settings.lookSpeed, 15, 30, 300, value -> settings.lookSpeed = (float)value); break;
            case 4: addNumber(x, y, width, "Cursor", settings.cursorSpeed, 2, 4, 40, value -> settings.cursorSpeed = value); break;
            case 5: addNumber(x, y, width, "Scroll", settings.scrollAmount, 0.25, 0.25, 4, value -> settings.scrollAmount = value); break;
            case 6: addNumber(x, y, width, "Move deadzone", settings.moveDeadzone, 0.05, 0, 0.75, value -> settings.moveDeadzone = (float)value); break;
            case 7: addNumber(x, y, width, "Look deadzone", settings.lookDeadzone, 0.05, 0, 0.75, value -> settings.lookDeadzone = (float)value); break;
            case 8: addNumber(x, y, width, "Cursor deadzone", settings.cursorDeadzone, 0.05, 0, 0.75, value -> settings.cursorDeadzone = (float)value); break;
            case 9: addNumber(x, y, width, "Trigger deadzone", settings.triggerDeadzone, 0.05, 0, 0.95, value -> settings.triggerDeadzone = (float)value); break;
        }
    }

    private void addNumber(int x, int y, int width, String label, double value, double step, double min, double max, Setter setter) {
        int side = 38;
        addButton(x, y, side, "-", () -> setNumber(value - step, min, max, setter));
        Button valueLabel = NeoForgeControllerCompat.createButton(x + side + 3, y, width - side * 2 - 6, 20, label + "  " + format(value), button -> {});
        valueLabel.active = false;
        addRenderableWidget(valueLabel);
        addButton(x + width - side, y, side, "+", () -> setNumber(value + step, min, max, setter));
    }

    private void setNumber(double value, double min, double max, Setter setter) {
        setter.set(Math.max(min, Math.min(max, value)));
        saveAndRebuild();
    }

    private void switchTab(Tab value) {
        tab = value;
        page = 0;
        rebuildButtons();
    }

    private void changePage(int amount) {
        page += amount;
        rebuildButtons();
    }

    private int itemCount() {
        switch (tab) {
            case CONTROLS: return ControllerAction.values().length;
            case SETTINGS: return 10;
            case JAVA: return javaKeys().length;
            case RADIAL: return 8;
            default: return 0;
        }
    }

    private KeyMapping[] javaKeys() {
        return this.minecraft == null || this.minecraft.options == null ? new KeyMapping[0] : this.minecraft.options.keyMappings;
    }

    private ControllerInput nextInput(ControllerInput input) {
        ControllerInput[] values = ControllerInput.values();
        return values[(input.ordinal() + 1) % values.length];
    }

    private String nextRadialKey(String current) {
        String[] ids = NeoForgeControllerCompat.radialKeyIds();
        for (int i = 0; i < ids.length; i++) {
            if (ids[i].equals(current)) return ids[(i + 1) % ids.length];
        }
        return ids.length == 0 ? "" : ids[0];
    }

    private void resetPage() {
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        switch (tab) {
            case CONTROLS: settings.resetBindings(); break;
            case SETTINGS:
                settings.toggleCrouch = false;
                settings.toggleSprint = false;
                settings.invertY = false;
                settings.lookSpeed = 135.0f;
                settings.cursorSpeed = 14.0;
                settings.scrollAmount = 1.0;
                settings.moveDeadzone = 0.35f;
                settings.lookDeadzone = 0.12f;
                settings.cursorDeadzone = 0.12f;
                settings.triggerDeadzone = 0.25f;
                break;
            case JAVA: settings.resetBindings(); break;
            case RADIAL: settings.resetRadialSlots(); break;
        }
        saveAndRebuild();
    }

    private void saveAndRebuild() {
        NeoForgeControllerSettings.save();
        rebuildButtons();
    }

    private void addButton(int x, int y, int width, String label, Runnable action) {
        addRenderableWidget(NeoForgeControllerCompat.createButton(x, y, width, 20, label, button -> action.run()));
    }

    private String format(double value) {
        if (Math.abs(value - Math.round(value)) < 0.001) return Long.toString(Math.round(value));
        return String.format(Locale.ROOT, "%.2f", value);
    }

    private String shorten(String value, int length) {
        return value.length() <= length ? value : value.substring(0, length - 3) + "...";
    }

    private enum Tab {
        CONTROLS("Controls"),
        SETTINGS("Settings"),
        JAVA("Java keys"),
        RADIAL("Radial");

        private final String label;

        Tab(String label) {
            this.label = label;
        }
    }

    private interface Setter {
        void set(double value);
    }
}
