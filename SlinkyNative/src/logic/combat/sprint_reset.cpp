// Sprint Reset - descriptor +0x158 (state machine) and helper 0x3b150.
// After a hit the module waits "delay" (randomised), then releases / blocks the
// sprint (or forward/back) key for "duration" and restores it.
//   state 0 = idle, 1 = waiting for the delay, 2 = key released.
#include "slinky/engine.h"

extern "C" unsigned sprint_reset_mode;         // "mode"
extern "C" float sprint_reset_delay_value;            // "delay"
extern "C" unsigned char sprint_reset_randomize_value;    // "randomize"
extern "C" unsigned char sprint_reset_wait_for_damage_value;    // "wait_for_damage"
extern "C" unsigned char sprint_reset_require_weapon_value;    // "require_weapon"
extern "C" float sprint_reset_duration_value;            // "duration"
extern "C" int sprint_reset_stage;              // state
extern "C" float sprint_reset_countdown;            // countdown
extern "C" double sprint_reset_time_last_own_hit;           // time of last own hit
extern "C" unsigned char sprint_reset_hit_seen;    // hit seen
extern "C" int sprint_reset_ticks_since_hurt;              // ticks since hurt
extern "C" char sprint_reset_rng_states[];           // rng states
extern "C" char sprint_reset_random_walk_1[];
extern "C" char sprint_reset_random_walk_2[];
extern "C" const double threshold_0_5_in_sprint_reset;
extern "C" const float minus_one_in_sprint_reset;
extern "C" const float random_range_bound_1_in_sprint_reset;
extern "C" const double walk_period_30_in_sprint_reset;
extern "C" const float random_range_bound_0_05_in_sprint_reset;
extern "C" const float random_range_bound_0_1_in_sprint_reset;
extern "C" const float random_range_bound_2_in_sprint_reset;
extern "C" const double walk_period_5_in_sprint_reset;

FN(double, time_now, ());
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_sneak_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_key_17_or_8, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(void, java_call_entity_set_sprinting, (void *jvm, void *entity, bool on));
FN(void *, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, void *rng, double p));
FN(unsigned char, walk_should_change, (void *state, void *rng, double p));
FN(float, random_rotation, (void *rng));
extern "C" void sprint_reset_press_key(Ctx *ctx, unsigned char on);

extern "C" void sprint_reset_on_player_update(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game == 1) {
        double now = time_now();
        if (!(now - sprint_reset_time_last_own_hit < threshold_0_5_in_sprint_reset)) {
            if (java_get_entity_ticks_existed(ctx->jvm, ctx->player) == java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player)) {
                sprint_reset_time_last_own_hit = now;
                sprint_reset_hit_seen = 1;
            }
        }
        sprint_reset_ticks_since_hurt += 1;
        if (java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) == 10)
            sprint_reset_ticks_since_hurt = 0;
        float t = sprint_reset_countdown;
        if (t > 0.0f) {
            t += minus_one_in_sprint_reset;
            sprint_reset_countdown = t;
            if (t > 0.0f) {
                if (sprint_reset_stage == 2) {
                    sprint_reset_press_key(ctx, 1);
                    return;
                }
                return;
            }
        }
        if (sprint_reset_stage == 2) {
            sprint_reset_press_key(ctx, 0);
            sprint_reset_stage = 0;
            return;
        }
        if (sprint_reset_stage == 1) {
            sprint_reset_press_key(ctx, 1);
            sprint_reset_stage = 2;
            sprint_reset_hit_seen = 0;
            float d = sprint_reset_duration_value;
            if (sprint_reset_randomize_value == 1) {
                float x = walk_update(sprint_reset_random_walk_1, rot_make_floats(0.0f, random_range_bound_1_in_sprint_reset), walk_period_30_in_sprint_reset);
                float m = d - x;
                d = 0.0f > m ? 0.0f : m;
                if (walk_should_change(sprint_reset_random_walk_2, rot_make_floats(random_range_bound_0_05_in_sprint_reset, random_range_bound_0_1_in_sprint_reset), walk_period_30_in_sprint_reset))
                    d += random_rotation(rot_make_floats(random_range_bound_1_in_sprint_reset, random_range_bound_2_in_sprint_reset));
            }
            sprint_reset_countdown = d + sprint_reset_countdown;
            return;
        }
        float dl = sprint_reset_delay_value;
        if (sprint_reset_randomize_value == 1)
            dl += walk_update(sprint_reset_rng_states, rot_make_floats(0.0f, random_range_bound_2_in_sprint_reset), walk_period_5_in_sprint_reset);
        if (sprint_reset_hit_seen == 1) {
            if (sprint_reset_wait_for_damage_value == 1 && sprint_reset_ticks_since_hurt != 0)
                return;
            if (sprint_reset_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
                return;
            sprint_reset_countdown = dl + sprint_reset_countdown;
            sprint_reset_stage = 1;
        }
        return;
    }
    if (sprint_reset_stage == 2)
        sprint_reset_press_key(ctx, 0);
    sprint_reset_stage = 0;
    sprint_reset_hit_seen = 0;
    *(int *)&sprint_reset_countdown = 0;
}

extern "C" void sprint_reset_press_key(Ctx *c, unsigned char on) {
    void *obj = get_game_settings(c->jvm, c->minecraft);
    void *key;
    if (sprint_reset_mode == 2)
        key = java_get_game_settings_key_17_or_8(c->jvm, obj);
    else if (sprint_reset_mode == 1)
        key = java_get_game_settings_sneak_key(c->jvm, obj);
    else
        key = java_get_game_settings_forward_key(c->jvm, obj);
    if (sprint_reset_mode == 2)
        java_call_entity_set_sprinting(c->jvm, c->player, (on ^ 1) != 0);
    unsigned char key_binding_key_code = on ? (sprint_reset_mode == 1) : java_get_key_binding_key_code(c->jvm, key);
    java_set_key_binding_pressed(c->jvm, key, key_binding_key_code);
}
