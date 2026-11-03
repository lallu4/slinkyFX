// JNI bridge 0x22000
// cflags: /Ob1 /clang:-fno-slp-vectorize
#include "slinky/engine.h"
#include "slinky/jcall.h"
struct Rot { float yaw, pitch; };
struct Vec3 { double x, y, z; };

extern "C" jobject get_minecraft(Jvm *jvm) {
    return JCALL(jvm, GetStaticObjectField, ids->Minecraft, ids->Minecraft_theMinecraft);
}

extern "C" jobject get_local_player(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_thePlayer);
}

extern "C" jobject get_world(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_theWorld);
}

extern "C" jobject get_timer(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_timer);
}

extern "C" jobject get_game_settings(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_gameSettings);
}

extern "C" jobject get_minecraft_render_global(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderGlobal);
}

extern "C" jobject java_get_minecraft(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_entityRenderer);
}

extern "C" jobject java_get_minecraft_render_item(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderItem);
}

extern "C" jobject java_get_minecraft_render_engine(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderEngine);
}

extern "C" jobject java_get_minecraft_player_controller(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_playerController);
}

extern "C" jobject java_get_minecraft_object_mouse_over(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_objectMouseOver);
}

extern "C" void java_set_minecraft(Jvm *jvm, void *minecraft, void *value) {
    JCALL(jvm, SetObjectField, minecraft, ids->Minecraft_objectMouseOver, value);
}

// The ClickGUI hands the cursor over to itself by clearing Minecraft.inGameHasFocus, which is the flag
// vanilla gates its mouse-look on (see logic/visual/gui.cpp). The flag means "the game loop owns the mouse"
// to vanilla but "a world is loaded" to the modules, so while the menu is up they keep reading the value the
// flag had before it opened and nothing else in the engine changes behaviour.
extern "C" {
unsigned char gui_owns_cursor;         // the ClickGUI has the cursor and the focus flag is ours to hold
unsigned char gui_focus_before_open;   // the value it had when the menu opened
}

extern "C" bool is_in_game(Jvm *jvm, void *minecraft) {
    if (gui_owns_cursor) return gui_focus_before_open != 0;
    if (!minecraft) return false;
    return JCALLN(jvm, GetBooleanField, minecraft, ids->Minecraft_inGameHasFocus) != 0;
}

extern "C" void java_set_minecraft_in_game_focus(Jvm *jvm, void *minecraft, int value) {
    if (!minecraft) return;
    JCALL(jvm, SetBooleanField, minecraft, ids->Minecraft_inGameHasFocus, (jboolean)(value != 0));
}

extern "C" jint java_get_minecraft_left_click_counter(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_leftClickCounter);
}

extern "C" void java_set_minecraft_left_click_counter(Jvm *jvm, void *minecraft, int value) {
    JCALL(jvm, SetIntField, minecraft, ids->Minecraft_leftClickCounter, value);
}

extern "C" jobject get_render_view_entity(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderViewEntity);
}

extern "C" jobject java_call_minecraft_get_net_handler(Jvm *jvm, void *minecraft) {
    return JCALLN(jvm, CallObjectMethod, minecraft, ids->Minecraft_getNetHandler);
}

extern "C" void java_call_minecraft_click_mouse(Jvm *jvm, void *minecraft) {
    JCALLN(jvm, CallVoidMethod, minecraft, ids->Minecraft_clickMouse);
}

extern "C" void java_call_minecraft_right_click_mouse(Jvm *jvm, void *minecraft) {
    JCALLN(jvm, CallVoidMethod, minecraft, ids->Minecraft_rightClickMouse);
}

// ---- hand-written
FN(jobject, java_get_render_manager_instance, (Jvm *jvm, jobject render_manager));

extern "C" jobject get_current_screen(Jvm *jvm, jobject object) {
    jobject x = JCALLN(jvm, GetObjectField, object, (jfieldID)ids->Minecraft_currentScreen);
    return JCALLN(jvm, IsInstanceOf, x, (jclass)ids->GuiContainer) ? x : nullptr;
}
extern "C" jobject java_get_minecraft_render_manager(Jvm *jvm, jobject minecraft) {
    if (jvm->ids->variant != 0)
        return JCALL(jvm, GetObjectField, minecraft, (jfieldID)ids->Minecraft_renderManager);
    return java_get_render_manager_instance(jvm, minecraft);
}

// ---- ClassFileLoadHook: rewrite the classes listed in the hook table, injecting "SlinkyHooks13"
struct MethodKey { char b[32]; };
struct MethodPat { long long a, b; };
struct HookTab { char padding_00[0x18]; MethodPat *pats; unsigned long long n; };
struct HookCls { jobject cls; void *data; int len; char padding_14[0x14]; };   // 0x28 bytes each

extern "C" volatile int classfile_hooks_first_error;          // first error (sticky)
extern "C" volatile long classfile_hooks_sites_rewritten = 0; // instructions replaced by the class currently being rewritten (diagnostics)
extern "C" unsigned char classfile_hooks_1_while_hooks_are_installed;         // 1 while the hooks are installed
extern "C" void slinky_logf(const char *fmt, ...);
static unsigned char g_disarming;             // 1 while remove_class_hooks restores the original bytecode
extern "C" HookCls hooked_class_minecraft, hooked_class_entity_renderer, hooked_class_render_manager, hooked_class_renderer_living_entity, hooked_class_render_player,
    hooked_class_net_handler_play_client, hooked_class_world_client, hooked_class_entity, hooked_class_entity_living_base;
extern "C" const char str_slinkyhooks13[];         // "SlinkyHooks13"
FN(bool, compare_member_name, (MethodKey left, MethodPat right));
struct HookTable; struct CP; struct RBuf22; struct WBuf22;
FN(int, rewrite_instruction, (HookTable *t, CP *constant_pool, RBuf22 *in, WBuf22 *out));   // instruction patcher (below)
FN(int, rewrite_class_file, (const unsigned char *data, int len, unsigned char **out, int *outLen, const char *hookCls,
                void *hookData, int hookLen, void *match, HookCls *h));
FN(void, libc_free, (void *ptr));                  // free
FN(void *, libc_memcpy_alt, (void *dest, const void *src, long long n));   // memcpy

struct JvmtiEnv_ { void *fn[64]; };
typedef int (*JvmtiAllocate)(JvmtiEnv_ **, long long, unsigned char **);

// method matcher passed to the class rewriter: (-1,-1) entries match everything
extern "C" int match_method(HookTab *t, MethodKey *k, void **outFn, MethodPat **outPat) {
    static int pat_logs;
    for (unsigned long long i = 0; i < t->n; i++) {
        MethodPat *p = &t->pats[i * 2];
        if (p->a == -1 && p->b == -1) {
            *outFn = (void *)rewrite_instruction;
            *outPat = p;
            break;
        }
        unsigned long long na = (unsigned long long)p->a, da = (unsigned long long)p->b;
        if (pat_logs < 12) { pat_logs++; slinky_logf("patcmp: i=%llu name=%p desc=%p", i, p->a, p->b); }
        if (na < 0x10000 || da < 0x10000) {
            slinky_logf("patcmp: bad pattern name=%llx desc=%llx (skipped)", na, da);
            continue;
        }
        if (compare_member_name(*k, *p)) {
            *outFn = (void *)rewrite_instruction;
            *outPat = p;
            break;
        }
    }
    return 0;
}

extern "C" void class_file_load_hook(JvmtiEnv_ **jvmti, JNIEnv *env, jclass redefined, jobject loader, const char *name,
                         jobject pd, int len, const unsigned char *data, int *newLen, unsigned char **newData) {
    if (classfile_hooks_first_error != 0) return;
    // remove_class_hooks sets g_disarming before it retransforms: during that call the JVM may replay this hook
    // for the classes being restored, and re-patching them would silently undo the disarm. Handing the
    // bytes back untouched makes the retransform a true restore. (The install also runs with the hook
    // active and g_disarming clear, so it still patches.)
    if (g_disarming) return;
    HookCls *pctx;
#define TRY(x) if ((*env)->IsSameObject(env, x.cls, redefined)) { pctx = &x; goto found; }
    TRY(hooked_class_minecraft) TRY(hooked_class_entity_renderer) TRY(hooked_class_render_manager) TRY(hooked_class_renderer_living_entity) TRY(hooked_class_render_player)
    TRY(hooked_class_net_handler_play_client) TRY(hooked_class_world_client) TRY(hooked_class_entity) TRY(hooked_class_entity_living_base)
#undef TRY
    return;
found:
    slinky_logf("classhook-seen: %s", name ? name : "?");
    unsigned char *buf = nullptr;
    int outLen;
    __atomic_store_n(&classfile_hooks_sites_rewritten, 0, __ATOMIC_RELAXED);
    int err = rewrite_class_file(data, len, &buf, &outLen, str_slinkyhooks13, pctx->data, pctx->len, (void *)match_method, pctx);
    long sites = __atomic_load_n(&classfile_hooks_sites_rewritten, __ATOMIC_RELAXED);
    if (err == 0)
        slinky_logf("classhook: %s sites=%ld %d->%d", name ? name : "?", sites, len, outLen);
    else
        slinky_logf("classhook: %s sites=%ld err=%d", name ? name : "?", sites, err);
    if (err == 0) {
        unsigned char *mem = nullptr;
        JvmtiAllocate alloc = (JvmtiAllocate)(*jvmti)->fn[45];
        int alloc_error = alloc(jvmti, outLen, &mem);
        if (alloc_error != 0 || !mem) {
            err = (alloc_error ? alloc_error : 100) + 12000;   // JVMTI_ERROR_NULL_POINTER
            libc_free(buf);
            __sync_val_compare_and_swap(&classfile_hooks_first_error, 0, err);
            return;
        }
        libc_memcpy_alt(mem, buf, outLen);
        *newData = mem;
        *newLen = outLen;
        libc_free(buf);
        return;
    }
    __sync_val_compare_and_swap(&classfile_hooks_first_error, 0, err);
}

// ---- JNI bridge functions found by the gap scan (no caller outside the virtualised init) ----
extern "C" jint java_get_minecraft_right_click_delay_timer(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_rightClickDelayTimer);
}

extern "C" jobject java_call_minecraft_add_scheduled_task(Jvm *jvm, void *minecraft, void *value) {
    return JCALLN(jvm, CallObjectMethod, minecraft, ids->Minecraft_addScheduledTask, value);
}

// ---- JVMTI: switch the class hooks off again (undo of the install) ----
// Disables the ClassFileLoadHook event, retransforms the 9 hooked classes so the game gets its original bytecode
// back, drops the transformer's callbacks, natives and capabilities and finally its global ref. Errors are JVMTI error + 12000.
struct JvmtiTable { void *fn[160]; };
typedef int (*JvmtiSetEventMode)(JvmtiTable **, int mode, int event, void *thread, ...);
typedef int (*JvmtiRetransform)(JvmtiTable **, int count, jclass *classes);
typedef int (*JvmtiSetCallbacks)(JvmtiTable **, const void *callbacks, int size);
typedef int (*JvmtiRelinquish)(JvmtiTable **, const void *caps);
#define JVMTI(env, idx, T) ((T)(*(env))->fn[idx])
extern "C" unsigned char classfile_hooks_1_while_hooks_are_installed;       // 1 while the hooks are installed
extern "C" jobject classfile_hooks_global_ref_transformer_class;              // global ref of the transformer class
extern "C" const char ptr_to_hook_installer_class_file_slinkyhooks13[];        // JVMTI capabilities that were added
// One call per step: the previous form evaluated `call` twice, so every check retransformed the classes again.
#define JVMTI_CHECK(call)                        \
    do {                                         \
        int e_ = (call);                         \
        if (e_) { err = e_ + 0x2ee0; goto out; } \
    } while (0)
extern "C" int remove_class_hooks(JNIEnv *env, JvmtiTable **jvmti) {
    int err = 0;
    // Not armed = the hooks were never installed or another pass already consumed the flag (a failed
    // pass left the game's patched calls live). Reporting success here made the unload sequence tear
    // everything down while the JVM was still dispatching natives into this DLL: hs_err pid6708
    // (java_set_render_manager_player_view_y via setRenderY) and pid2260 (serialize_settings via a second hook_update_display pass) look exactly like that.
    if (!__sync_bool_compare_and_swap(&classfile_hooks_1_while_hooks_are_installed, 1, 0)) {
        // A failed install may still have defined SlinkyHooks13 and kept its global ref.
        if (env && classfile_hooks_global_ref_transformer_class) {
            (*env)->DeleteGlobalRef(env, classfile_hooks_global_ref_transformer_class);
            classfile_hooks_global_ref_transformer_class = 0;
        }
        return 12000 + 49 /* JVMTI_ERROR_NOT_AVAILABLE */;
    }
    g_disarming = 1;
    JVMTI_CHECK(JVMTI(jvmti, 1, JvmtiSetEventMode)(jvmti, 0 /* JVMTI_DISABLE */, 0x36 /* CLASS_FILE_LOAD_HOOK */, 0));
    {
        jclass classes[9] = {hooked_class_minecraft.cls, hooked_class_entity_renderer.cls, hooked_class_render_manager.cls, hooked_class_renderer_living_entity.cls, hooked_class_render_player.cls,
                             hooked_class_net_handler_play_client.cls, hooked_class_world_client.cls, hooked_class_entity.cls, hooked_class_entity_living_base.cls};
        JVMTI_CHECK(JVMTI(jvmti, 151, JvmtiRetransform)(jvmti, 9, classes));
    }
    g_disarming = 0;
    // Nothing of ours may stay reachable from the JVM once the DLL is gone: the transformer's callbacks are
    // a copy the VM holds, and its native entry points are function pointers into this module.
    {
        char no_callbacks[0x138] = {};
        int cb = JVMTI(jvmti, 121, JvmtiSetCallbacks)(jvmti, no_callbacks, sizeof no_callbacks);
        if (cb) { err = cb + 0x2ee0; goto out; }
    }
    if (env && classfile_hooks_global_ref_transformer_class &&
        (*env)->UnregisterNatives(env, classfile_hooks_global_ref_transformer_class) != 0)
        (*env)->ExceptionClear(env);
    {
        int rel = JVMTI(jvmti, 142, JvmtiRelinquish)(jvmti, ptr_to_hook_installer_class_file_slinkyhooks13);
        if (rel) err = rel + 0x2ee0;
    }
out:
    g_disarming = 0;
    if (classfile_hooks_global_ref_transformer_class) {
        (*env)->DeleteGlobalRef(env, classfile_hooks_global_ref_transformer_class);
        classfile_hooks_global_ref_transformer_class = 0;
    }
    // A failed step leaves the client resident (the unload sequence cancels on errors): re-arm the flag
    // so the next click gets a whole new pass. Retransform is idempotent, so resuming is safe.
    slinky_logf("disarm: err=%d", err);
    if (err) __sync_bool_compare_and_swap(&classfile_hooks_1_while_hooks_are_installed, 0, 1);
    return err;
}

// ---- class-file patcher: rewrites a call/field instruction that matches a hook entry into `invokestatic hook` ----
// Instruction callback of the class transformer (rewrite_span/copy_methods_table_constant_pool `pick`). `t` lists the hooks; each has the
// owner class, member name + descriptor to catch, the kind of instruction and where its new Methodref index is.
#include "slinky/classfile.h"
struct RBuf22 { const unsigned char *p, *end; };
struct WBuf22 { unsigned char *p, *end; };
struct HookEnt { const char *cls; const char *name, *desc; int kind; int padding_1c; unsigned short *ref; };   // 0x28
struct HookTable { char padding_00[0x10]; HookEnt *e; unsigned long long n; };
struct MemberName { const char *name, *desc; };
FN(int, classfile_read_u1, (RBuf22 *r, unsigned char *out));
FN(int, classfile_read_u2, (RBuf22 *r, unsigned short *out));
FN(int, classfile_write_u1, (WBuf22 *w, unsigned char b));
FN(int, classfile_write_u2, (WBuf22 *w, unsigned short v));
FN(int, constant_pool_member_ref, (CP *constant_pool, unsigned short idx, MemberRef *out));
FN(int, constant_pool_member_ref2, (CP *constant_pool, unsigned short idx, MemberRef *out));
extern "C" int compare_utf8_local(Utf8 left, const char *right) __asm__("compare_utf8");
extern "C" bool compare_member_name_local(NameType left, MemberName right) __asm__("compare_member_name");
extern "C" int rewrite_instruction(HookTable *t, CP *cp, RBuf22 *in, WBuf22 *out) {
    unsigned char op;
    int error_code = classfile_read_u1(in, &op);
    if (error_code) return error_code;
    int kind;
    bool field = true;
    switch (op) {
    case 0xb2: kind = 4; break;               // getstatic
    case 0xb3: kind = 6; break;               // putstatic
    case 0xb4: kind = 3; break;               // getfield
    case 0xb5: kind = 5; break;               // putfield
    case 0xb6: case 0xb7: kind = 1; field = false; break;   // invokevirtual / invokespecial
    case 0xb8: kind = 2; field = false; break;               // invokestatic
    default: return 0;
    }
    unsigned short idx;
    error_code = classfile_read_u2(in, &idx);
    if (error_code) return error_code;
    MemberRef ref;
    error_code = field ? constant_pool_member_ref(cp, idx, &ref) : constant_pool_member_ref2(cp, idx, &ref);
    if (error_code) return error_code;
    for (unsigned long long i = 0; i < t->n; i++) {
        HookEnt *h = &t->e[i];
        if (h->kind != kind) continue;
        if (compare_utf8_local(ref.cls, h->cls) != 0) continue;
        unsigned long long np = (unsigned long long)h->name, dp = (unsigned long long)h->desc;
        static int site_logs;
        if (site_logs < 30) { site_logs++; slinky_logf("sitecmp: cls=%s kind=%d name=%p desc=%p", h->cls, h->kind, h->name, h->desc); }
        if (np < 0x10000 || dp < 0x10000) {
            slinky_logf("sitecmp: bad site name=%llx desc=%llx (skipped)", np, dp);
            continue;
        }
        if (!compare_member_name_local(ref.nt, MemberName{h->name, h->desc})) continue;
        error_code = classfile_write_u1(out, 0xb8);   // invokestatic
        if (error_code) return error_code;
        error_code = classfile_write_u2(out, *h->ref);
        if (error_code == 0) __atomic_add_fetch(&classfile_hooks_sites_rewritten, 1, __ATOMIC_RELAXED);
        return error_code;
    }
    return 0;
}

// ---- functions the first gap scan missed (padding / tail-jump neighbours) ----
extern "C" void java_set_minecraft_right_click_delay_timer(Jvm *jvm, void *minecraft, int value) {
    JCALL(jvm, SetIntField, minecraft, ids->Minecraft_rightClickDelayTimer, value);
}

extern "C" jint java_get_minecraft_display_width(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_displayWidth);
}

extern "C" jint java_get_minecraft_display_height(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_displayHeight);
}