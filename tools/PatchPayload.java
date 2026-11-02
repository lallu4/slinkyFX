import dev.slinky.shaded.asm.ClassReader;
import dev.slinky.shaded.asm.ClassWriter;
import dev.slinky.shaded.asm.Handle;
import dev.slinky.shaded.asm.Opcodes;
import dev.slinky.shaded.asm.Type;
import dev.slinky.shaded.asm.tree.AbstractInsnNode;
import dev.slinky.shaded.asm.tree.ClassNode;
import dev.slinky.shaded.asm.tree.FieldInsnNode;
import dev.slinky.shaded.asm.tree.FrameNode;
import dev.slinky.shaded.asm.tree.InsnList;
import dev.slinky.shaded.asm.tree.InsnNode;
import dev.slinky.shaded.asm.tree.InvokeDynamicInsnNode;
import dev.slinky.shaded.asm.tree.JumpInsnNode;
import dev.slinky.shaded.asm.tree.LabelNode;
import dev.slinky.shaded.asm.tree.LdcInsnNode;
import dev.slinky.shaded.asm.tree.MethodInsnNode;
import dev.slinky.shaded.asm.tree.MethodNode;
import dev.slinky.shaded.asm.tree.TypeInsnNode;
import dev.slinky.shaded.asm.tree.VarInsnNode;

import java.io.ByteArrayOutputStream;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Payload behavior patcher - the source of the prebuilt jars in SlinkyNative/payload.
 *
 * Usage (ALWAYS run against the pristine source jar - never against a
 * previously patched output, the patches assume original bytecode):
 *   javac -cp <pristine-payload.jar> PatchPayload.java
 *   java -cp .<sep><pristine-payload.jar> PatchPayload <in.jar> <out.jar>
 * (<sep> is ';' on Windows, ':' on bash; the shaded ASM tree API comes from
 * the payload jar itself, so this exact source works for the official /
 * srg / named / intermediary jars.)
 *
 * Patches, all on client-owned classes:
 *
 *  ClickGuiScreen:
 *    - all profile actions (load/save/delete/open-folder/create) are instant,
 *      no confirmation prompts (profileAction -> perform(action, name, true))
 *    - the unload tab opens the "Are you sure? / Unload Slinky?" modal on a
 *      single click; confirming only sets the close-requested flag and closes
 *      the modal (lambda$unloadPrompt$N, javac's capturing-lambda shape: a
 *      private synthetic instance method plus an invokedynamic whose
 *      instantiated type is the bare SAM signature "()V" - anything else and
 *      LambdaMetafactory rejects the call site on first execution). No
 *      shutdown runs in the GUI: the earlier frame-end shutdown check misread
 *      the original non-null unloadAction marker and killed the client on the
 *      first GUI frame (the "white flash").
 *  GuiModule:
 *    - onClientTickStart added: performs the module-manager shutdown on the
 *      next client tick after the modal confirmed the unload, outside the
 *      renderer and modal call stacks.
 *  NotificationsModule:
 *    - opening the profile folder now pushes a top-left notification too
 *    - module-state notifications are locked to the original Slinky orange
 *      (RGB 1.0 / 0.55 / 0.2)
 *  CanvasFonts:
 *    - current(int) is forced down the bundled-font branch; the game-font
 *      branch registers a reload listener on the Forge resource manager and
 *      throws AbstractMethodError on Forge 1.8.9 + OptiFine/PolyPatcher/
 *      Redirectionor installs.
 *  SlinkyMod:
 *    - preInitialize re-enables the notifications module after the saved
 *      configuration loads, so an old disabled state cannot suppress the
 *      action notifications.
 *
 * Every constant-pool reference the inserted code needs is copied out of the
 * class being patched, so no mappings are hardcoded.
 */
public class PatchPayload {

    static final String GUI = "dev/slinky/client/gui/ClickGuiScreen";
    static final String GUI_MODULE = "dev/slinky/module/render/GuiModule";
    static final String NOTIF = "dev/slinky/module/render/NotificationsModule";
    static final String FONTS = "dev/slinky/client/render/font/CanvasFonts";
    static final String MOD = "dev/slinky/SlinkyMod";
    static int fallbacks;

    public static void main(String[] args) throws Exception {
        Path in = Paths.get(args[0]);
        Path out = Paths.get(args[1]);
        byte[] buf = new byte[65536];
        try (ZipInputStream zin = new ZipInputStream(new BufferedInputStream(Files.newInputStream(in)));
             ZipOutputStream zout = new ZipOutputStream(new BufferedOutputStream(Files.newOutputStream(out)))) {
            ZipEntry entry;
            while ((entry = zin.getNextEntry()) != null) {
                String name = entry.getName();
                ByteArrayOutputStream bos = new ByteArrayOutputStream();
                int n;
                while ((n = zin.read(buf)) > 0) bos.write(buf, 0, n);
                byte[] data = bos.toByteArray();
                if (name.equals(GUI + ".class")) {
                    data = patchGui(data);
                    System.out.println("patched " + name);
                } else if (name.equals(GUI_MODULE + ".class")) {
                    data = patchGuiModule(data);
                    System.out.println("patched " + name);
                } else if (name.equals(NOTIF + ".class")) {
                    data = patchNotif(data);
                    System.out.println("patched " + name);
                } else if (name.equals(FONTS + ".class")) {
                    data = patchFonts(data);
                    System.out.println("patched " + name);
                } else if (name.equals(MOD + ".class")) {
                    data = patchMod(data);
                    System.out.println("patched " + name);
                }
                ZipEntry outEntry = new ZipEntry(name);
                outEntry.setTime(entry.getTime());
                zout.putNextEntry(outEntry);
                zout.write(data);
                zout.closeEntry();
            }
        }
        System.out.println("common-super fallbacks: " + fallbacks);
        System.out.println("wrote " + out);
    }

    // ------------------------------------------------------------------

    static byte[] patchGui(byte[] data) throws Exception {
        ClassNode cn = readClean(data);

        int maxLambda = -1;
        for (MethodNode mn : cn.methods) {
            if (mn.name.startsWith("lambda$")) {
                try {
                    maxLambda = Math.max(maxLambda, Integer.parseInt(mn.name.substring(mn.name.lastIndexOf('$') + 1)));
                } catch (NumberFormatException ignored) {
                }
            }
        }
        String lambdaName = "lambda$unloadPrompt$" + (maxLambda + 1);

        MethodNode profileAction = method(cn, "profileAction", "(ILjava/lang/String;D)V");
        MethodNode buildFramePlan = methodByName(cn, "buildFramePlan");
        if (profileAction == null || buildFramePlan == null) throw new AssertionError("methods missing");

        // ---- refs harvested from the class itself -------------------
        MethodInsnNode perform = insn(profileAction.instructions, MethodInsnNode.class,
                a -> "perform".equals(a.name), "perform call");
        MethodInsnNode beginPrompt = insnAnyMethod(cn, MethodInsnNode.class, a -> "beginPrompt".equals(a.name)
                && "(Ljava/lang/String;Ljava/lang/Runnable;D)V".equals(a.desc), "beginPrompt call");
        InvokeDynamicInsnNode template = insn(profileAction.instructions, InvokeDynamicInsnNode.class,
                a -> "run".equals(a.name), "lambda call site");

        // ---- 1. profileAction: all actions instant ------------------
        // Replace the entire method body with: this.perform(action, name, true); return;
        InsnList pa = profileAction.instructions;
        pa.clear();

        InsnList fast = new InsnList();
        fast.add(new VarInsnNode(Opcodes.ALOAD, 0));
        fast.add(new VarInsnNode(Opcodes.ILOAD, 1));
        fast.add(new VarInsnNode(Opcodes.ALOAD, 2));
        fast.add(new InsnNode(Opcodes.ICONST_1));
        fast.add(new MethodInsnNode(Opcodes.INVOKESPECIAL, perform.owner, "perform", perform.desc, perform.itf));
        fast.add(new InsnNode(Opcodes.RETURN));
        pa.add(fast);

        // ---- 2. buildFramePlan: unload tab click -> confirm modal ----
        InsnList ins = buildFramePlan.instructions;
        MethodInsnNode update = insn(ins, MethodInsnNode.class,
                a -> "update".equals(a.name) && "(Ljava/lang/Object;ZZZZD)Z".equals(a.desc), "HoldActivation.update");
        MethodInsnNode progress = insn(ins, MethodInsnNode.class,
                a -> "progress".equals(a.name) && "(Ljava/lang/Object;)F".equals(a.desc), "HoldActivation.progress");
        int updateIdx = ins.indexOf(update);
        int progressIdx = ins.indexOf(progress);

        // walk back to the if_icmpne that gates the last tab's branch
        int gateIdx = -1;
        for (int i = updateIdx - 1; i >= 0; i--) {
            AbstractInsnNode a = ins.get(i);
            if (a instanceof JumpInsnNode && a.getOpcode() == Opcodes.IF_ICMPNE) {
                gateIdx = i;
                break;
            }
        }
        if (gateIdx < 0) throw new AssertionError("if_icmpne before HoldActivation.update not found");

        int startIdx = gateIdx + 1;
        while (ins.get(startIdx).getOpcode() == -1) startIdx++; // labels, frames, line numbers
        if (!isALoad0(ins.get(startIdx))) {
            throw new AssertionError("unexpected region start " + ins.get(startIdx));
        }

        int endIdx = -1;
        LabelNode merge = null;
        for (int i = progressIdx + 1; i < ins.size(); i++) {
            AbstractInsnNode a = ins.get(i);
            if (a instanceof JumpInsnNode && a.getOpcode() == Opcodes.GOTO) {
                endIdx = i;
                merge = ((JumpInsnNode) a).label;
                break;
            }
        }
        if (endIdx < 0 || merge == null) throw new AssertionError("merge goto after HoldActivation.progress not found");
        if (!(ins.get(endIdx + 1) instanceof LabelNode)) {
            throw new AssertionError("goto is not followed by the merge label");
        }

        // the rect local (r) and the `now` parameter slot
        int rSlot = -1;
        for (int i = startIdx; i < updateIdx - 1; i++) {
            AbstractInsnNode a = ins.get(i);
            if (isALoad(a) && ins.get(i + 1) instanceof MethodInsnNode
                    && "hover".equals(((MethodInsnNode) ins.get(i + 1)).name)) {
                rSlot = ((VarInsnNode) a).var;
                break;
            }
        }
        int nowSlot = -1;
        for (int i = updateIdx - 1; i > updateIdx - 6 && i >= 0; i--) {
            AbstractInsnNode a = ins.get(i);
            if (a instanceof VarInsnNode && a.getOpcode() == Opcodes.DLOAD) {
                nowSlot = ((VarInsnNode) a).var;
                break;
            }
        }
        if (rSlot < 0 || nowSlot < 0) throw new AssertionError("rect/now slots not found (r=" + rSlot + " now=" + nowSlot + ")");

        // the regular-tab click call, used for the new single-click check
        MethodInsnNode click = null;
        for (int i = endIdx + 1; i < ins.size(); i++) {
            AbstractInsnNode a = ins.get(i);
            if (a instanceof MethodInsnNode && "click".equals(((MethodInsnNode) a).name)
                    && "(Ldev/slinky/client/render/RenderMath$Rect;)Z".equals(((MethodInsnNode) a).desc)) {
                click = (MethodInsnNode) a;
                break;
            }
        }
        if (click == null) throw new AssertionError("click ref not found");

        // field/method refs reused inside the replacement code
        FieldInsnNode stateRef = insn(ins, FieldInsnNode.class, a -> "state".equals(a.name), "state field");
        FieldInsnNode closeRef = insn(ins, FieldInsnNode.class, a -> "closeRequested".equals(a.name), "closeRequested field");
        MethodInsnNode finishModal = insnAnyMethod(cn, MethodInsnNode.class,
                a -> "finishModal".equals(a.name) && "(Z)V".equals(a.desc), "finishModal method");

        // remove the held-activation block (update + shutdown + progress bar)
        List<AbstractInsnNode> doomed = new ArrayList<>();
        for (int i = startIdx; i <= endIdx; i++) doomed.add(ins.get(i));
        for (AbstractInsnNode d : doomed) ins.remove(d);

        // insert before the else-branch label: the gate's fall-through runs
        // this code for the last tab, the gate's jump skips it for the others
        AbstractInsnNode anchor = ins.get(startIdx);
        if (!(anchor instanceof LabelNode)) throw new AssertionError("anchor is not the else-branch label: " + anchor);
        InsnList rep = new InsnList();
        rep.add(new VarInsnNode(Opcodes.ALOAD, 0));
        rep.add(new FieldInsnNode(Opcodes.GETFIELD, stateRef.owner, "state", stateRef.desc));
        rep.add(new VarInsnNode(Opcodes.ALOAD, rSlot));
        rep.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, click.owner, "click", click.desc, click.itf));
        rep.add(new JumpInsnNode(Opcodes.IFEQ, merge));
        rep.add(new VarInsnNode(Opcodes.ALOAD, 0));
        rep.add(new FieldInsnNode(Opcodes.GETFIELD, stateRef.owner, "state", stateRef.desc));
        rep.add(new LdcInsnNode("Unload Slinky?"));
        rep.add(new VarInsnNode(Opcodes.ALOAD, 0));
        rep.add(new InvokeDynamicInsnNode("run", "(L" + GUI + ";)Ljava/lang/Runnable;",
                template.bsm,
                template.bsmArgs[0],
                new Handle(Opcodes.H_INVOKEVIRTUAL, GUI, lambdaName, "()V", false),
                Type.getMethodType("()V")));
        rep.add(new VarInsnNode(Opcodes.DLOAD, nowSlot));
        rep.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, beginPrompt.owner, "beginPrompt", beginPrompt.desc, beginPrompt.itf));
        rep.add(new JumpInsnNode(Opcodes.GOTO, merge));
        ins.insertBefore(anchor, rep);

        // ---- 3. the unload-confirmation lambda body ------------------
        // javac's capturing-lambda shape: a private synthetic INSTANCE method
        // with no parameters (the receiver is captured by the call site), and
        // an invokedynamic whose instantiated type is the bare SAM signature.
        // Only request the unload and close the modal here. GuiModule performs
        // the actual shutdown on the next client tick, completely outside the
        // renderer and modal call stacks.
        MethodNode lambda = new MethodNode(Opcodes.ACC_PRIVATE | Opcodes.ACC_SYNTHETIC,
                lambdaName, "()V", null, null);
        lambda.instructions.add(new VarInsnNode(Opcodes.ALOAD, 0));
        lambda.instructions.add(new FieldInsnNode(Opcodes.GETFIELD, stateRef.owner, "state", stateRef.desc));
        lambda.instructions.add(new InsnNode(Opcodes.ICONST_1));
        lambda.instructions.add(new FieldInsnNode(Opcodes.PUTFIELD, closeRef.owner, "closeRequested", closeRef.desc));
        lambda.instructions.add(new VarInsnNode(Opcodes.ALOAD, 0));
        lambda.instructions.add(new FieldInsnNode(Opcodes.GETFIELD, stateRef.owner, "state", stateRef.desc));
        lambda.instructions.add(new InsnNode(Opcodes.ICONST_0));
        lambda.instructions.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, finishModal.owner, "finishModal", finishModal.desc, finishModal.itf));
        lambda.instructions.add(new InsnNode(Opcodes.RETURN));
        cn.methods.add(lambda);

        System.out.println("  click->prompt: r=" + rSlot + " now=" + nowSlot
                + " region=[" + startIdx + "," + endIdx + "] lambda=" + lambdaName);
        return write(cn);
    }

    // ------------------------------------------------------------------

    static byte[] patchGuiModule(byte[] data) throws Exception {
        ClassNode cn = readClean(data);
        if (method(cn, "onClientTickStart", "(Ldev/slinky/client/ClientContext;)V") != null) {
            throw new AssertionError("GuiModule.onClientTickStart already exists");
        }

        FieldInsnNode runtimeRef = insnAnyMethod(cn, FieldInsnNode.class,
                a -> a.getOpcode() == Opcodes.GETFIELD && "runtime".equals(a.name), "runtime field");
        FieldInsnNode managerRef = insnAnyMethod(cn, FieldInsnNode.class,
                a -> a.getOpcode() == Opcodes.GETFIELD && "manager".equals(a.name), "manager field");
        FieldInsnNode closeRef = insnAnyMethod(cn, FieldInsnNode.class,
                a -> "closeRequested".equals(a.name), "closeRequested field");

        MethodNode tick = new MethodNode(Opcodes.ACC_PUBLIC | Opcodes.ACC_STRICT,
                "onClientTickStart", "(Ldev/slinky/client/ClientContext;)V", null, null);
        LabelNode done = new LabelNode();
        tick.instructions.add(new VarInsnNode(Opcodes.ALOAD, 0));
        tick.instructions.add(new FieldInsnNode(Opcodes.GETFIELD, runtimeRef.owner, "runtime", runtimeRef.desc));
        tick.instructions.add(new FieldInsnNode(Opcodes.GETFIELD, closeRef.owner, "closeRequested", closeRef.desc));
        tick.instructions.add(new JumpInsnNode(Opcodes.IFEQ, done));
        tick.instructions.add(new VarInsnNode(Opcodes.ALOAD, 0));
        tick.instructions.add(new FieldInsnNode(Opcodes.GETFIELD, runtimeRef.owner, "runtime", runtimeRef.desc));
        tick.instructions.add(new InsnNode(Opcodes.ICONST_0));
        tick.instructions.add(new FieldInsnNode(Opcodes.PUTFIELD, closeRef.owner, "closeRequested", closeRef.desc));
        tick.instructions.add(new VarInsnNode(Opcodes.ALOAD, 0));
        tick.instructions.add(new FieldInsnNode(Opcodes.GETFIELD, managerRef.owner, "manager", managerRef.desc));
        tick.instructions.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL,
                "dev/slinky/module/ModuleManager", "shutdown", "()V", false));
        tick.instructions.add(done);
        tick.instructions.add(new InsnNode(Opcodes.RETURN));
        cn.methods.add(tick);

        System.out.println("  onClientTickStart: confirmed unload deferred outside rendering");
        return write(cn);
    }

    // ------------------------------------------------------------------

    static byte[] patchFonts(byte[] data) throws Exception {
        ClassNode cn = readClean(data);
        MethodNode current = method(cn, "current", "(I)Ldev/slinky/client/render/font/CanvasFonts$Engine;");
        if (current == null) throw new AssertionError("CanvasFonts.current(I) missing");

        InsnList ins = current.instructions;
        // current(int) starts with: getstatic guiFace:Z ; ifeq <game-font branch>.
        // The game-font branch registers a reload-listener lambda with the Forge
        // resource manager, and on Forge 1.8.9 installs with resource coremods
        // that registration throws AbstractMethodError (observed crash:
        // SimpleReloadableResourceManager.func_110542_a). The GUI never takes
        // this branch - it renders through the bundled font engine.
        // Force the bundled branch for every caller by replacing the
        // conditional read with iconst_1; the game-font branch becomes
        // unreachable and no resource-manager registration ever happens.
        AbstractInsnNode first = ins.getFirst();
        if (!(first instanceof FieldInsnNode)
                || first.getOpcode() != Opcodes.GETSTATIC
                || !"guiFace".equals(((FieldInsnNode) first).name)) {
            throw new AssertionError("unexpected current(I) entry: " + first);
        }
        ins.set(first, new InsnNode(Opcodes.ICONST_1));

        System.out.println("  current(I): bundled font engine forced, resource-manager listener bypassed");
        return write(cn);
    }

    // ------------------------------------------------------------------

    static byte[] patchMod(byte[] data) throws Exception {
        ClassNode cn = readClean(data);
        MethodNode preInitialize = method(cn, "preInitialize", "()V");
        if (preInitialize == null) throw new AssertionError("preInitialize missing");

        FieldInsnNode modulesRef = insn(preInitialize.instructions, FieldInsnNode.class,
                a -> a.getOpcode() == Opcodes.GETFIELD && "modules".equals(a.name), "modules field");
        MethodInsnNode find = insn(preInitialize.instructions, MethodInsnNode.class,
                a -> "find".equals(a.name) && "(Ljava/lang/String;)Ldev/slinky/module/Module;".equals(a.desc),
                "module find call");

        AbstractInsnNode ret = null;
        for (AbstractInsnNode a : preInitialize.instructions.toArray()) {
            if (a.getOpcode() == Opcodes.RETURN) ret = a;
        }
        if (ret == null) throw new AssertionError("preInitialize return missing");

        // The configuration has been loaded at this point. Keep the original
        // config notification surface available even if an old config saved the
        // Notifications module as disabled.
        InsnList enable = new InsnList();
        enable.add(new VarInsnNode(Opcodes.ALOAD, 0));
        enable.add(new FieldInsnNode(Opcodes.GETFIELD, modulesRef.owner, "modules", modulesRef.desc));
        enable.add(new VarInsnNode(Opcodes.ALOAD, 0));
        enable.add(new FieldInsnNode(Opcodes.GETFIELD, modulesRef.owner, "modules", modulesRef.desc));
        enable.add(new LdcInsnNode("notifications"));
        enable.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, find.owner, "find", find.desc, find.itf));
        enable.add(new InsnNode(Opcodes.ICONST_1));
        enable.add(new InsnNode(Opcodes.ICONST_0));
        enable.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL,
                "dev/slinky/module/ModuleManager", "setEnabled", "(Ldev/slinky/module/Module;ZZ)V", false));
        preInitialize.instructions.insertBefore(ret, enable);

        System.out.println("  preInitialize: Notifications enabled after config load");
        return write(cn);
    }

    // ------------------------------------------------------------------

    static byte[] patchNotif(byte[] data) throws Exception {
        ClassNode cn = readClean(data);
        MethodNode configAction = method(cn, "configAction", "(ILjava/lang/String;)V");
        if (configAction == null) throw new AssertionError("configAction missing");

        MethodInsnNode append = insn(configAction.instructions, MethodInsnNode.class,
                a -> "append".equals(a.name) && "(ILjava/lang/String;)V".equals(a.desc), "queue.append call");
        FieldInsnNode queueField = insn(configAction.instructions, FieldInsnNode.class,
                a -> a.getOpcode() == Opcodes.GETFIELD && "queue".equals(a.name), "queue field");

        LabelNode skip = new LabelNode();
        InsnList pre = new InsnList();
        pre.add(new VarInsnNode(Opcodes.ILOAD, 1));
        pre.add(new InsnNode(Opcodes.ICONST_2));
        pre.add(new JumpInsnNode(Opcodes.IF_ICMPNE, skip));
        pre.add(new VarInsnNode(Opcodes.ALOAD, 0));
        pre.add(new FieldInsnNode(Opcodes.GETFIELD, queueField.owner, "queue", queueField.desc));
        pre.add(new InsnNode(Opcodes.ICONST_0));                       // kind = config action (green)
        pre.add(new LdcInsnNode("Opened profile folder"));
        pre.add(new MethodInsnNode(Opcodes.INVOKEVIRTUAL, append.owner, "append", append.desc, append.itf));
        pre.add(new InsnNode(Opcodes.RETURN));
        pre.add(skip);
        configAction.instructions.insert(pre);

        // Force module-state notifications (kind == 1) to orange instead of the
        // GUI accent color, matching the original Slinky video.
        patchNotifColors(cn);

        System.out.println("  configAction: open-folder notification added, kind-1 color forced to orange");
        return write(cn);
    }

    static void patchNotifColors(ClassNode cn) {
        MethodNode plan = method(cn, "plan", "(Ldev/slinky/client/ClientContext;)Ldev/slinky/client/render/DrawPlan;");
        if (plan == null) throw new AssertionError("plan method missing");

        InsnList ins = plan.instructions;
        // Find the kind == 1 check: getfield kind, iconst_1, if_icmpne.
        int kindCheck = -1;
        for (int i = 0; i < ins.size() - 2; ++i) {
            AbstractInsnNode a = ins.get(i);
            AbstractInsnNode b = ins.get(i + 1);
            AbstractInsnNode c = ins.get(i + 2);
            if (a.getOpcode() == Opcodes.GETFIELD && "kind".equals(((FieldInsnNode)a).name)
                    && b.getOpcode() == Opcodes.ICONST_1
                    && c instanceof JumpInsnNode && c.getOpcode() == Opcodes.IF_ICMPNE) {
                kindCheck = i + 2;
                break;
            }
        }
        if (kindCheck < 0) throw new AssertionError("kind == 1 check not found");

        // The goto that leaves the kind-1 block for the merge after color
        // selection. There are two GOTOs inside this block: the inner one
        // jumps to the shared astore; the outer one jumps to the merge label.
        int gotoIdx = -1;
        int foundGotos = 0;
        for (int i = kindCheck + 1; i < ins.size(); ++i) {
            if (ins.get(i).getOpcode() == Opcodes.GOTO) {
                if (++foundGotos == 2) {
                    gotoIdx = i;
                    break;
                }
            }
        }
        if (gotoIdx < 0) throw new AssertionError("kind-1 block outer goto not found");

        // The astore right before the goto holds the chosen color (there may
        // be a label/line/frame node between them).
        AbstractInsnNode storeNode = null;
        for (int i = gotoIdx - 1; i > kindCheck; --i) {
            AbstractInsnNode a = ins.get(i);
            if (a.getOpcode() == Opcodes.ASTORE) {
                storeNode = a;
                break;
            }
        }
        if (storeNode == null) throw new AssertionError("expected astore before kind-1 goto");
        int colorLocal = ((VarInsnNode)storeNode).var;

        // Remove the old white/accent logic between the if_icmpne and the astore.
        List<AbstractInsnNode> doomed = new ArrayList<>();
        for (int i = kindCheck + 1; i < gotoIdx - 1; ++i) {
            doomed.add(ins.get(i));
        }
        for (AbstractInsnNode d : doomed) ins.remove(d);

        // Insert: new ColorSetting.Value(1.0f, 0.55f, 0.2f)  (original Slinky orange)
        InsnList rep = new InsnList();
        rep.add(new TypeInsnNode(Opcodes.NEW, "dev/slinky/setting/ColorSetting$Value"));
        rep.add(new InsnNode(Opcodes.DUP));
        rep.add(new LdcInsnNode(1.0f));
        rep.add(new LdcInsnNode(0.55f));
        rep.add(new LdcInsnNode(0.2f));
        rep.add(new MethodInsnNode(Opcodes.INVOKESPECIAL, "dev/slinky/setting/ColorSetting$Value", "<init>", "(FFF)V", false));
        ins.insertBefore(storeNode, rep);

        System.out.println("  plan: kind-1 color forced to orange (local=" + colorLocal + ")");
    }

    // ------------------------------------------------------------------

    static byte[] write(ClassNode cn) throws Exception {
        ClassWriter cw = new ClassWriter(ClassWriter.COMPUTE_FRAMES) {
            @Override
            protected String getCommonSuperClass(String type1, String type2) {
                try {
                    return super.getCommonSuperClass(type1, type2);
                } catch (Throwable t) {
                    fallbacks++;
                    System.out.println("  [common-super fallback] " + type1 + " / " + type2);
                    return "java/lang/Object";
                }
            }
        };
        cn.accept(cw);
        return cw.toByteArray();
    }

    // Strip debug info (LineNumber/LocalVariable tables) when reading the
    // original class; they would otherwise survive rewriting and can create
    // duplicate LVT entries after our instruction surgery.
    static ClassNode readClean(byte[] data) {
        ClassNode cn = new ClassNode();
        new ClassReader(data).accept(cn, ClassReader.SKIP_DEBUG);
        return cn;
    }

    static boolean isALoad0(AbstractInsnNode a) {
        return a instanceof VarInsnNode
                && a.getOpcode() == Opcodes.ALOAD
                && ((VarInsnNode) a).var == 0;
    }

    static boolean isALoad(AbstractInsnNode a) {
        return a instanceof VarInsnNode && a.getOpcode() == Opcodes.ALOAD;
    }

    static MethodNode method(ClassNode cn, String name, String desc) {
        for (MethodNode mn : cn.methods) {
            if (mn.name.equals(name) && mn.desc.equals(desc)) return mn;
        }
        return null;
    }

    static MethodNode methodByName(ClassNode cn, String name) {
        for (MethodNode mn : cn.methods) {
            if (mn.name.equals(name)) return mn;
        }
        return null;
    }

    interface Match<T extends AbstractInsnNode> {
        boolean test(T node);
    }

    static <T extends AbstractInsnNode> T insn(InsnList list, Class<T> kind, Match<T> test, String what) {
        for (AbstractInsnNode a : list.toArray()) {
            if (kind.isInstance(a) && test.test(kind.cast(a))) return kind.cast(a);
        }
        throw new AssertionError(what + " not found");
    }

    static <T extends AbstractInsnNode> T insnAnyMethod(ClassNode cn, Class<T> kind, Match<T> test, String what) {
        for (MethodNode mn : cn.methods) {
            for (AbstractInsnNode a : mn.instructions.toArray()) {
                if (kind.isInstance(a) && test.test(kind.cast(a))) return kind.cast(a);
            }
        }
        throw new AssertionError(what + " not found");
    }
}
