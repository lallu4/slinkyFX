// Null Move - descriptor +0x148: per tick, drive four movement key states
// (forward/back/left/right style pairs) so that opposite keys cancel out.
// Each of the four states in 0x3049d0..0x3049dc follows the same pattern.
#include "slinky/engine.h"

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };

extern "C" char null_move_module_descriptor[];
extern "C" int null_move_key_state_0;
extern "C" int null_move_key_state_1;
extern "C" int null_move_key_state_2;
extern "C" int null_move_key_state_3;

FN(unsigned char, is_in_game, (void *jvm, void *object));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));
FN(unsigned char, is_key_down, (void *jvm, void *k));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));

// one key pair: `own` = key held, `opp` = opposite key held, `tgt` = key to release
#define PAIR(state, own, opp, tgt, ptype, zinit, cond)             \
    {                                                               \
        int s = state;                                              \
        unsigned char o = is_key_down(ctx->jvm, own);                      \
        ptype p = is_key_down(ctx->jvm, opp);                              \
        int n;                                                      \
        if (o) {                                                    \
            zinit                                                   \
            n = mode;                                               \
            if (cond)                                               \
                java_set_key_binding_pressed(ctx->jvm, tgt, 0);                             \
        } else if (s == 0) {                                        \
            n = 0;                                                  \
        } else {                                                    \
            unsigned char key_binding_key_code = java_get_key_binding_key_code(ctx->jvm, tgt);                  \
            n = 0;                                                  \
            if (key_binding_key_code)                                                  \
                java_set_key_binding_pressed(ctx->jvm, tgt, 1);                             \
        }                                                           \
        state = n;                                                  \
    }

extern "C" void null_move_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (!is_in_game(ctx->jvm, ctx->minecraft))
        return;
    // ticksExisted is read straight off the player; the world-update dispatch site leaves it NULL
    if (!ctx->player)
        return;
    int mode = java_get_entity_ticks_existed(ctx->jvm, ctx->player);
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *game_settings_key_bindings = java_get_game_settings_forward_key(ctx->jvm, obj);
    void *key_binding = java_get_game_settings_left_key(ctx->jvm, obj);
    void *k2 = java_get_game_settings_back_key(ctx->jvm, obj);
    void *k3 = java_get_game_settings_right_key(ctx->jvm, obj);
    PAIR(null_move_key_state_0, game_settings_key_bindings, k2, k2, bool, bool z = s == 0;, z & p)
    PAIR(null_move_key_state_1, key_binding, k3, k3, unsigned char, unsigned char z = s == 0;, (z & p) == 1)
    PAIR(null_move_key_state_2, k2, game_settings_key_bindings, game_settings_key_bindings, unsigned char, unsigned char z = s == 0;, (z & p) == 1)
    PAIR(null_move_key_state_3, k3, key_binding, key_binding, unsigned char, unsigned char z = s == 0;, (z & p) == 1)
}

// the four bindings this module re-writes, put back in step with the real keyboard
static void resync_move_keys(Ctx *ctx) {
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *keys[4] = { java_get_game_settings_forward_key(ctx->jvm, obj),
                      java_get_game_settings_left_key(ctx->jvm, obj),
                      java_get_game_settings_back_key(ctx->jvm, obj),
                      java_get_game_settings_right_key(ctx->jvm, obj) };
    for (int i = 0; i < 4; i++)
        java_set_key_binding_pressed(ctx->jvm, keys[i], java_get_key_binding_key_code(ctx->jvm, keys[i]));
}

extern "C" void null_move_on_toggle(EvHook *hook) {
    if (hook->on || hook->ev != null_move_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    // disabling unregisters the tick handler, so a key this module forced down (or up) would keep
    // that state until the user physically tapped it - you could not walk backwards or strafe
    resync_move_keys(ctx);
    null_move_key_state_0 = 0;
    null_move_key_state_1 = 0;
    null_move_key_state_2 = 0;
    null_move_key_state_3 = 0;
}
