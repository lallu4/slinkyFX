// Instant Stop - descriptor +0x148: when the forward key is released on the
// ground, briefly press back (plus left or right when that cancels the motion
// better) to kill the remaining horizontal velocity, then restore the keys.
//   state 0 = idle, 1 = forward held, 2 = counter-keys pressed this tick.
#include "slinky/engine.h"

struct Vec3 { double x, y, z; };
struct Rot { float yaw, pitch; };

extern "C" int instant_stop_state;               // state
extern "C" const double divisor_1_3;     // sprint speed divisor
extern "C" const float minus_one_in_instant_stop;      // full input (1.0)
extern "C" const float one_in_instant_stop;      // opposite strafe input (-1.0)

FN(unsigned char, is_in_game, (void *jvm, void *object));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));
FN(unsigned char, is_key_down, (void *jvm, void *k));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(float, java_get_entity_living_base_land_movement_factor, (void *jvm, void *entity_living_base));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(Vec3, horizontal_motion, (Rot rotation, float fwd, float strafe, float speed));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));

extern "C" void instant_stop_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    int next = 0;
    if (is_in_game(ctx->jvm, ctx->minecraft)) {
        void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
        void *fwd = java_get_game_settings_forward_key(ctx->jvm, obj);
        void *back = java_get_game_settings_back_key(ctx->jvm, obj);
        void *left = java_get_game_settings_left_key(ctx->jvm, obj);
        void *right = java_get_game_settings_right_key(ctx->jvm, obj);
        int st = instant_stop_state;
        if (st == 2) {
            java_set_key_binding_pressed(ctx->jvm, back, java_get_key_binding_key_code(ctx->jvm, back));
            java_set_key_binding_pressed(ctx->jvm, left, java_get_key_binding_key_code(ctx->jvm, left));
            java_set_key_binding_pressed(ctx->jvm, right, java_get_key_binding_key_code(ctx->jvm, right));
        } else if (st == 1) {
            if (is_key_down(ctx->jvm, fwd))
                return;
            if (!java_get_entity_on_ground(ctx->jvm, ctx->player))
                return;
            Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
            Rot rot = get_entity_rotation(ctx->jvm, ctx->player);
            float speed = java_get_entity_living_base_land_movement_factor(ctx->jvm, ctx->player);
            if (java_call_entity_is_sprinting(ctx->jvm, ctx->player))
                speed = (float)((double)speed / divisor_1_3);
            double vx = motion.x, vz = motion.z;
            float k = minus_one_in_instant_stop;
            Vec3 t = horizontal_motion(rot, k, k, speed);
            t = vec3_add(motion, t);
            double s1 = t.x * t.x + t.z * t.z;
            double s0 = vx * vx + vz * vz;
            int dir = s0 > s1 ? -1 : 999;
            double best = s1 < s0 ? s1 : s0;
            t = horizontal_motion(rot, k, 0.0f, speed);
            t = vec3_add(motion, t);
            double s2 = t.x * t.x + t.z * t.z;
            if (best > s2)
                dir = 0;
            best = s2 < best ? s2 : best;
            t = horizontal_motion(rot, k, one_in_instant_stop, speed);
            t = vec3_add(motion, t);
            double s3 = t.x * t.x + t.z * t.z;
            dir = best > s3 ? 1 : dir;
            if (dir != 999) {
                java_set_key_binding_pressed(ctx->jvm, back, 1);
                java_set_key_binding_pressed(ctx->jvm, left, best > s3);
                java_set_key_binding_pressed(ctx->jvm, right, dir == -1);
                next = 2;
            }
        } else {
            if (!is_key_down(ctx->jvm, fwd))
                return;
            next = 1;
        }
    }
    instant_stop_state = next;
}
