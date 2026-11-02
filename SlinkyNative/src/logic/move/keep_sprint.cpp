// Keep Sprint - descriptor +0x128 / +0x138 / +0x158.
// +0x128: on the attack tick, roll "chance" and remember the player's motion.
// +0x138: if the roll succeeded and the player is still moving the same way,
//         re-apply the saved motion scaled by "motion_mult".
// +0x158: afterwards re-assert sprint on the player.
#include "slinky/engine.h"

struct Vec3 { double x, y, z; };
struct PhaseHook { char padding_00[8]; Ctx *ctx; unsigned char phase; };   // +0x10: phase flag

extern "C" float keep_sprint_chance_value;           // "chance"
extern "C" float keep_sprint_motion_mult;           // "motion_mult"
extern "C" unsigned char keep_sprint_require_on_ground_value;   // "require_on_ground"
extern "C" unsigned char keep_sprint_allow_reduce_value;   // "allow_reduce"
extern "C" Vec3 keep_sprint_saved_motion;            // saved motion
extern "C" unsigned char keep_sprint_roll_succeeded;   // roll succeeded
extern "C" unsigned char keep_sprint_motion_pending_re_apply;   // motion pending re-apply

FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(unsigned char, random_chance, (float chance));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(void, java_set_entity_motion_x, (void *jvm, void *entity, Vec3 value));
FN(void, java_call_entity_set_sprinting, (void *jvm, void *entity, bool on));

extern "C" void keep_sprint_on_attack(PhaseHook *hook) {
    if (hook->phase == 1) {
        Ctx *ctx = hook->ctx;
        unsigned char ok = 0;
        if (ctx->in_game == 1 && java_call_entity_is_sprinting(ctx->jvm, ctx->player) && random_chance(keep_sprint_chance_value)) {
            keep_sprint_saved_motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
            ok = 1;
        }
        keep_sprint_roll_succeeded = ok;
    }
}

extern "C" void keep_sprint_on_attack_post(PhaseHook *hook) {
    if (keep_sprint_roll_succeeded != 1 || hook->phase != 1)
        return;
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    if (keep_sprint_require_on_ground_value == 1 && !java_get_entity_on_ground(ctx->jvm, ctx->player))
        return;
    if (java_get_entity_ticks_existed(ctx->jvm, ctx->player) != java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player))
        return;
    if (keep_sprint_allow_reduce_value == 1 && ((CombatTarget *)ctx->target)->valid)
        return;
    double m = keep_sprint_motion_mult;
    keep_sprint_saved_motion.x *= m;
    keep_sprint_saved_motion.z *= m;
    void *jvm = ctx->jvm, *player = ctx->player;
    asm volatile("" ::: "memory");   // original reloads the saved motion from memory here
    java_set_entity_motion_x(jvm, player, keep_sprint_saved_motion);
    keep_sprint_motion_pending_re_apply = 1;
}

extern "C" void keep_sprint_on_player_update(Hook *hook) {
    if (keep_sprint_motion_pending_re_apply == 1) {
        keep_sprint_motion_pending_re_apply = 0;
        Ctx *ctx = hook->ctx;
        if (ctx->player) {
            java_call_entity_set_sprinting(ctx->jvm, ctx->player, true);
            return;
        }
    }
    EPILOG_NOP();
}
