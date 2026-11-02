import dev.slinky.shaded.asm.ClassReader;
import dev.slinky.shaded.asm.ClassWriter;
import dev.slinky.shaded.asm.Opcodes;
import dev.slinky.shaded.asm.tree.AbstractInsnNode;
import dev.slinky.shaded.asm.tree.ClassNode;
import dev.slinky.shaded.asm.tree.FieldInsnNode;
import dev.slinky.shaded.asm.tree.InsnList;
import dev.slinky.shaded.asm.tree.InsnNode;
import dev.slinky.shaded.asm.tree.IntInsnNode;
import dev.slinky.shaded.asm.tree.JumpInsnNode;
import dev.slinky.shaded.asm.tree.LabelNode;
import dev.slinky.shaded.asm.tree.LdcInsnNode;
import dev.slinky.shaded.asm.tree.MethodInsnNode;
import dev.slinky.shaded.asm.tree.MethodNode;
import dev.slinky.shaded.asm.tree.TypeInsnNode;
import dev.slinky.shaded.asm.tree.VarInsnNode;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Unified payload patcher for slinkyFX v1.0.0.
 *
 * Applies, in a single pass:
 *  - Notification gate (only show module toggles while in-game)
 *  - ClickGUI mouse-delta consumption (camera no longer drifts behind UI)
 *  - Instant GuiModule unload (close screen, save async, skip redundant focus grab)
 *  - Timer mouse mapping + timer speed clamp
 *
 * Run against the three mapping jars (named, srg, official). The patcher
 * compiles the helper class GuiGate against each jar so it never needs to
 * hard-code Minecraft class names/descriptors.
 */
public final class PatchClient {

    static final String GATE = "dev/slinky/client/GuiGate";
    static final String MANAGER = "dev/slinky/module/ModuleManager";
    static final String CTX = "dev/slinky/client/ClientContext";
    static final String EVT = "dev/slinky/client/event/SemanticEvent";

    static int fallbacks;

    public static void main(String[] args) throws Exception {
        for (String arg : args) {
            patchJar(Paths.get(arg));
        }
    }

    static void patchJar(Path jar) throws Exception {
        byte[] gateBytes = compileGate(jar);

        List<String> names = new ArrayList<>();
        List<byte[]> data = new ArrayList<>();
        boolean patchedNotif = false;
        boolean patchedClickGui = false;
        boolean patchedGuiModule = false;
        boolean patchedTimerModule = false;

        try (ZipInputStream zin = new ZipInputStream(new BufferedInputStream(Files.newInputStream(jar)))) {
            ZipEntry entry;
            while ((entry = zin.getNextEntry()) != null) {
                String name = entry.getName();
                byte[] bytes = readAll(zin);
                if (name.equals("dev/slinky/module/render/NotificationsModule.class")) {
                    bytes = patchNotifications(bytes);
                    patchedNotif = true;
                } else if (name.equals("dev/slinky/client/gui/ClickGuiScreen.class")) {
                    bytes = patchClickGuiScreen(bytes);
                    patchedClickGui = true;
                } else if (name.equals("dev/slinky/module/render/GuiModule.class")) {
                    bytes = patchGuiModule(bytes);
                    patchedGuiModule = true;
                } else if (name.equals("dev/slinky/module/utility/TimerModule.class")) {
                    bytes = patchTimerModule(bytes);
                    patchedTimerModule = true;
                }
                names.add(name);
                data.add(bytes);
            }
        }

        names.add(GATE + ".class");
        data.add(gateBytes);

        try (ZipOutputStream zout = new ZipOutputStream(new BufferedOutputStream(Files.newOutputStream(jar)))) {
            for (int i = 0; i < names.size(); i++) {
                ZipEntry outEntry = new ZipEntry(names.get(i));
                zout.putNextEntry(outEntry);
                zout.write(data.get(i));
                zout.closeEntry();
            }
        }
        System.out.println("patched " + jar
                + " notifications=" + patchedNotif
                + " clickgui=" + patchedClickGui
                + " guimodule=" + patchedGuiModule
                + " timer=" + patchedTimerModule);
    }

    static byte[] compileGate(Path againstJar) throws Exception {
        return compileHelper(againstJar,
                Paths.get("C:/dev/slinkyFX-main/tools/GuiGate.java"),
                "dev/slinky/client/GuiGate.class");
    }

    static byte[] compileHelper(Path againstJar, Path source, String classNameInTmp) throws Exception {
        Path tmp = Paths.get("C:/Users/twink/AppData/Local/Temp/opencode/patchclient_out/"
                + againstJar.getFileName().toString().replace('.', '_'));
        Files.createDirectories(tmp);
        ProcessBuilder pb = new ProcessBuilder(
                "C:/Program Files/Eclipse Adoptium/jdk-8.0.504.1-hotspot/bin/javac.exe",
                "-d", tmp.toString(),
                "-cp", againstJar.toString(),
                source.toString()
        );
        pb.inheritIO();
        Process p = pb.start();
        if (p.waitFor() != 0) {
            throw new RuntimeException("javac failed for " + source);
        }
        return Files.readAllBytes(tmp.resolve(classNameInTmp));
    }

    // ------------------------------------------------------------------
    // NotificationsModule: gate all toggle notifications to in-game only
    // ------------------------------------------------------------------

    static byte[] patchNotifications(byte[] in) {
        ClassNode cn = readClean(in);
        MethodNode mn = method(cn, "stateChanged", "(Ldev/slinky/module/Module;Z)V");
        if (mn == null) throw new AssertionError("NotificationsModule.stateChanged missing");

        // Find the manager field reference used for find("gui") so we know its name.
        String managerField = findManagerField(cn);

        LabelNode notInGame = new LabelNode();
        LabelNode skipClear = new LabelNode();

        InsnList gate = new InsnList();
        // if (!GuiGate.inGame(manager.context())) {
        gate.add(new VarInsnNode(Opcodes.ALOAD, 0));
        gate.add(new FieldInsnNode(Opcodes.GETFIELD, "dev/slinky/module/render/NotificationsModule", managerField, "L" + MANAGER + ";"));
        gate.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, MANAGER, "context", "()L" + CTX + ";", false));
        gate.add(new MethodInsnNode(Opcodes.INVOKESTATIC, GATE, "inGame", "(L" + CTX + ";)Z", false));
        gate.add(new JumpInsnNode(Opcodes.IFNE, notInGame));

        //     if (module == this && !enabled) queue.clearMessages();
        gate.add(new VarInsnNode(Opcodes.ALOAD, 1));
        gate.add(new VarInsnNode(Opcodes.ALOAD, 0));
        LabelNode notSelf = new LabelNode();
        gate.add(new JumpInsnNode(Opcodes.IF_ACMPNE, notSelf));
        gate.add(new VarInsnNode(Opcodes.ILOAD, 2));
        gate.add(new JumpInsnNode(Opcodes.IFNE, skipClear));
        gate.add(new VarInsnNode(Opcodes.ALOAD, 0));
        gate.add(new FieldInsnNode(Opcodes.GETFIELD, "dev/slinky/module/render/NotificationsModule", "queue", "Ldev/slinky/client/render/NotificationQueue;"));
        gate.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, "dev/slinky/client/render/NotificationQueue", "clearMessages", "()V", false));
        gate.add(skipClear);
        gate.add(new InsnNode(Opcodes.RETURN));
        gate.add(notSelf);
        gate.add(new InsnNode(Opcodes.RETURN));
        gate.add(notInGame);

        mn.instructions.insert(gate);
        return write(cn);
    }

    static String findManagerField(ClassNode cn) {
        for (dev.slinky.shaded.asm.tree.FieldNode f : cn.fields) {
            if (f.desc.equals("L" + MANAGER + ";")) return f.name;
        }
        return "manager"; // fallback
    }

    // ------------------------------------------------------------------
    // ClickGuiScreen.handleMouseInput: consume deltas, don't pass to super
    // ------------------------------------------------------------------

    static byte[] patchClickGuiScreen(byte[] in) {
        ClassNode cn = readClean(in);
        // In notch/SRG the override name is obfuscated. Find the ()V method that
        // calls MinecraftInputAdapter.mouse (handleMouseInput equivalent).
        MethodNode mn = null;
        for (MethodNode candidate : cn.methods) {
            if (!"()V".equals(candidate.desc)) continue;
            for (AbstractInsnNode a : candidate.instructions.toArray()) {
                if (a instanceof MethodInsnNode) {
                    MethodInsnNode m = (MethodInsnNode) a;
                    if ("dev/slinky/client/input/MinecraftInputAdapter".equals(m.owner)
                            && "mouse".equals(m.name)
                            && "(Ldev/slinky/module/ModuleManager;)I".equals(m.desc)) {
                        mn = candidate;
                        break;
                    }
                }
            }
            if (mn != null) break;
        }
        if (mn == null) throw new AssertionError("ClickGuiScreen handleMouseInput equivalent not found");

        // Replace the single invokespecial to the parent GuiScreen with
        // GuiGate.consumeMouseDeltas(). The super class name comes from the
        // class metadata because it is obfuscated in notch mappings.
        String superName = cn.superName;
        MethodInsnNode superCall = null;
        for (AbstractInsnNode a : mn.instructions.toArray()) {
            if (a instanceof MethodInsnNode) {
                MethodInsnNode m = (MethodInsnNode) a;
                if (m.getOpcode() == Opcodes.INVOKESPECIAL
                        && m.owner != null && m.owner.equals(superName)
                        && "()V".equals(m.desc)) {
                    superCall = m;
                    break;
                }
            }
        }
        if (superCall == null) throw new AssertionError("ClickGuiScreen super.handleMouseInput not found");

        InsnList repl = new InsnList();
        repl.add(new MethodInsnNode(Opcodes.INVOKESTATIC, GATE, "consumeMouseDeltas", "()V", false));
        mn.instructions.insertBefore(superCall, repl);
        mn.instructions.remove(superCall);
        return write(cn);
    }

    // ------------------------------------------------------------------
    // GuiModule.onDisable: instant close, async save, no redundant focus grab
    // ------------------------------------------------------------------

    static byte[] patchGuiModule(byte[] in) {
        ClassNode cn = readClean(in);
        MethodNode mn = method(cn, "onDisable", "(L" + CTX + ";)V");
        if (mn == null) throw new AssertionError("GuiModule.onDisable missing");

        // Find displayGuiScreen(null) call. Keep everything up to it; replace
        // everything after it (until RETURN) with GuiGate.asyncSave(this); return.
        MethodInsnNode displayGuiScreen = null;
        for (AbstractInsnNode a : mn.instructions.toArray()) {
            if (a instanceof MethodInsnNode) {
                MethodInsnNode m = (MethodInsnNode) a;
                if ("displayGuiScreen".equals(m.name)
                        && "(Lnet/minecraft/client/gui/GuiScreen;)V".equals(m.desc)) {
                    displayGuiScreen = m;
                    break;
                }
            }
        }
        if (displayGuiScreen == null) {
            // In SRG/notch Minecraft method names are obfuscated. Find the
            // virtual call that passes aconst_null to the object returned by
            // ClientContext.minecraft().
            for (AbstractInsnNode a = mn.instructions.getFirst(); a != null; a = a.getNext()) {
                if (a instanceof MethodInsnNode) {
                    MethodInsnNode m = (MethodInsnNode) a;
                    AbstractInsnNode prev = a.getPrevious();
                    AbstractInsnNode prev2 = prev != null ? prev.getPrevious() : null;
                    if (m.getOpcode() == Opcodes.INVOKEVIRTUAL
                            && prev instanceof InsnNode && prev.getOpcode() == Opcodes.ACONST_NULL
                            && prev2 instanceof MethodInsnNode
                            && "minecraft".equals(((MethodInsnNode) prev2).name)
                            && "dev/slinky/client/ClientContext".equals(((MethodInsnNode) prev2).owner)) {
                        displayGuiScreen = m;
                        break;
                    }
                }
            }
        }
        if (displayGuiScreen == null) throw new AssertionError("GuiModule displayGuiScreen call not found");

        // Leave the original tail in place (it is unreachable) and just jump
        // over it to an async save + return. This avoids breaking stack-map
        // frames that reference labels in the removed branch.
        AbstractInsnNode ret = null;
        for (AbstractInsnNode a = mn.instructions.getFirst(); a != null; a = a.getNext()) {
            if (a instanceof InsnNode && a.getOpcode() == Opcodes.RETURN) {
                ret = a;
                break;
            }
        }
        if (ret == null) throw new AssertionError("GuiModule.onDisable return not found");

        LabelNode saveLabel = new LabelNode();
        InsnList jump = new InsnList();
        jump.add(new JumpInsnNode(Opcodes.GOTO, saveLabel));
        mn.instructions.insert(displayGuiScreen, jump);

        InsnList tail = new InsnList();
        tail.add(saveLabel);
        tail.add(new VarInsnNode(Opcodes.ALOAD, 0));
        tail.add(new MethodInsnNode(Opcodes.INVOKESTATIC, GATE, "asyncSave", "(Ldev/slinky/module/render/GuiModule;)V", false));
        mn.instructions.insertBefore(ret, tail);
        return write(cn);
    }

    // ------------------------------------------------------------------
    // TimerModule: fix require_mouse button mapping and clamp timer speed.
    // ------------------------------------------------------------------

    static byte[] patchTimerModule(byte[] in) {
        ClassNode cn = readClean(in);
        MethodNode mn = method(cn, "onEvent", "(L" + CTX + ";L" + EVT + ";)V");
        if (mn == null) throw new AssertionError("TimerModule.onEvent missing");

        // Fix the inverted left/right mouse mapping for require_mouse.
        // The original code maps keyCode -99 (left) to Mouse button 1 (right)
        // and everything else to button 0 (left). Swap the two constants.
        AbstractInsnNode cur = mn.instructions.getFirst();
        boolean swapped = false;
        while (cur != null) {
            if (cur.getOpcode() == Opcodes.BIPUSH && cur instanceof IntInsnNode
                    && ((IntInsnNode) cur).operand == -99) {
                AbstractInsnNode ificmpne = cur.getNext();
                if (ificmpne instanceof JumpInsnNode && ificmpne.getOpcode() == Opcodes.IF_ICMPNE) {
                    AbstractInsnNode c1 = ificmpne.getNext();
                    AbstractInsnNode gotoInsn = c1 != null ? c1.getNext() : null;
                    AbstractInsnNode c2 = gotoInsn != null ? gotoInsn.getNext() : null;
                    while (c2 != null && (c2 instanceof LabelNode || c2.getOpcode() == -1)) c2 = c2.getNext();
                    if (c1 != null && c1.getOpcode() == Opcodes.ICONST_1
                            && gotoInsn != null && gotoInsn.getOpcode() == Opcodes.GOTO
                            && c2 != null && c2.getOpcode() == Opcodes.ICONST_0) {
                        mn.instructions.set(c1, new InsnNode(Opcodes.ICONST_0));
                        mn.instructions.set(c2, new InsnNode(Opcodes.ICONST_1));
                        swapped = true;
                        break;
                    }
                }
            }
            cur = cur.getNext();
        }
        if (!swapped) throw new AssertionError("TimerModule mouse mapping not found");

        // Clamp timer speed after it is computed and before it is written.
        // The Timer field name is obfuscated in SRG/notch, so locate the
        // putfield that follows the MinecraftFields.timer() lookup.
        AbstractInsnNode putTimerSpeed = null;
        AbstractInsnNode timerLookup = null;
        for (AbstractInsnNode a : mn.instructions.toArray()) {
            if (a instanceof MethodInsnNode) {
                MethodInsnNode m = (MethodInsnNode) a;
                if ("dev/slinky/client/MinecraftFields".equals(m.owner)
                        && "timer".equals(m.name)
                        && "(Lnet/minecraft/client/Minecraft;)Lnet/minecraft/util/Timer;".equals(m.desc)) {
                    timerLookup = a;
                }
            }
        }
        if (timerLookup == null) {
            // Fallback for obfuscated mappings where the return type differs.
            for (AbstractInsnNode a : mn.instructions.toArray()) {
                if (a instanceof MethodInsnNode) {
                    MethodInsnNode m = (MethodInsnNode) a;
                    if ("dev/slinky/client/MinecraftFields".equals(m.owner) && "timer".equals(m.name)) {
                        timerLookup = a;
                        break;
                    }
                }
            }
        }
        if (timerLookup != null) {
            for (AbstractInsnNode a = timerLookup.getNext(); a != null; a = a.getNext()) {
                if (a instanceof FieldInsnNode && "F".equals(((FieldInsnNode) a).desc)) {
                    putTimerSpeed = a;
                    break;
                }
            }
        }
        if (putTimerSpeed == null) throw new AssertionError("TimerModule timerSpeed putfield not found");

        InsnList clamp = new InsnList();
        clamp.add(new MethodInsnNode(Opcodes.INVOKESTATIC, GATE, "clampTimerSpeed", "(F)F", false));
        mn.instructions.insert(putTimerSpeed.getPrevious(), clamp);

        return write(cn);
    }

    // ------------------------------------------------------------------

    static byte[] write(ClassNode cn) {
        ClassWriter cw = new ClassWriter(ClassWriter.COMPUTE_FRAMES) {
            @Override
            protected String getCommonSuperClass(String type1, String type2) {
                try {
                    return super.getCommonSuperClass(type1, type2);
                } catch (Throwable t) {
                    fallbacks++;
                    System.out.println("  [fallback] " + type1 + " / " + type2);
                    return "java/lang/Object";
                }
            }
        };
        cn.accept(cw);
        return cw.toByteArray();
    }

    static ClassNode readClean(byte[] data) {
        ClassNode cn = new ClassNode();
        new ClassReader(data).accept(cn, ClassReader.SKIP_DEBUG);
        return cn;
    }

    static ClassNode readExpanded(byte[] data) {
        ClassNode cn = new ClassNode();
        new ClassReader(data).accept(cn, ClassReader.SKIP_DEBUG | ClassReader.EXPAND_FRAMES);
        return cn;
    }

    static MethodNode method(ClassNode cn, String name, String desc) {
        for (MethodNode mn : cn.methods) {
            if (mn.name.equals(name) && mn.desc.equals(desc)) return mn;
        }
        return null;
    }

    static byte[] readAll(ZipInputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }
}
