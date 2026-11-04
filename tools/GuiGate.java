package dev.slinky.client;

import dev.slinky.module.render.GuiModule;

/**
 * Helper class injected into the payload by PatchClient.
 *
 * It deliberately avoids compile-time dependencies on LWJGL or Minecraft
 * classes so the same source compiles against all three mapping jars without
 * needing a game/LWJGL classpath.
 *
 * It must also stay a single class file: PatchClient injects only GuiGate.class
 * into the payload jars, so an inner or anonymous class here would compile to
 * GuiGate$1.class, never reach the jar, and throw NoClassDefFoundError as soon
 * as the method using it runs.
 */
public final class GuiGate implements Runnable {
    private final GuiModule module;

    private GuiGate(GuiModule module) {
        this.module = module;
    }

    /** True once a world/client level is loaded. */
    public static boolean inGame(ClientContext ctx) {
        if (ctx == null) return false;
        try {
            Object world = ctx.getClass().getMethod("world").invoke(ctx);
            return world != null;
        } catch (Throwable t) {
            return false;
        }
    }

    /** Drain LWJGL mouse deltas so they cannot leak to the entity renderer. */
    public static void consumeMouseDeltas() {
        try {
            Class<?> mouse = Class.forName("org.lwjgl.input.Mouse");
            mouse.getMethod("getDX").invoke(null);
            mouse.getMethod("getDY").invoke(null);
        } catch (Throwable ignored) {}
    }

    /** Sprint factor: apply the 1.3x boost only while the player is using an item. */
    public static float sprintFactor(boolean usingItem, float factor) {
        return usingItem ? factor * 1.3f : factor;
    }

    /** Clamp timer speed to a safe positive range to avoid freezing/rewinding time. */
    public static float clampTimerSpeed(float speed) {
        if (speed <= 0.0f || Float.isNaN(speed)) return 0.05f;
        if (speed > 10.0f) return 10.0f;
        return speed;
    }

    /** Save config asynchronously; the caller returns instantly. */
    public static void asyncSave(GuiModule module) {
        if (module == null) return;
        new Thread(new GuiGate(module), "slinky-gui-save").start();
    }

    @Override
    public void run() {
        try {
            module.saveOnClose();
        } catch (Throwable ignored) {}
    }
}
