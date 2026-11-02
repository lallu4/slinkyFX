// Fake Lag - descriptors +0x48 (tick) / +0x78 (packet hook).
// +0x48: manage the lag windows: per-direction counters ("inbound"/"outbound"
//        ms budgets), and decide whether lagging is currently allowed.
// +0x78: spend the budget while a packet is held; release (verdict 1) when it
//        runs out, or let it straight through (verdict 3) for protected packets.
#include "slinky/engine.h"
#define LIKELY(c) __builtin_expect(!!(c), 1)
#define H4(c) __builtin_expect(!!(c), 0)

extern "C" float fake_lag_inbound_value;            // "inbound"
extern "C" float fake_lag_outbound_value;            // "outbound"
extern "C" bool fake_lag_realtime_damage_value;             // "realtime_damage"
extern "C" unsigned fake_lag_mode;         // "mode"
extern "C" unsigned char fake_lag_require_ingame_value;    // "require_ingame"
extern "C" unsigned char fake_lag_require_weapon_value;    // "require_weapon"
extern "C" unsigned char fake_lag_require_attack_value;    // "require_attack"
extern "C" int fake_lag_last_hurt_time;              // last hurt time
extern "C" int fake_lag_cooldown;              // cooldown
extern "C" int fake_lag_inbound_counter;              // inbound counter
extern "C" int fake_lag_outbound_counter;              // outbound counter
extern "C" unsigned char fake_lag_realtime_damage_flag;
extern "C" unsigned char fake_lag_inbound_window_open;             // inbound window open
extern "C" unsigned char fake_lag_outbound_window_open;             // outbound window open
extern "C" volatile unsigned short fake_lag_inbound_budget;   // inbound budget
extern "C" volatile unsigned short fake_lag_outbound_budget;   // outbound budget

struct PktHook { int type; char padding_04[4]; Ctx *ctx; void *ev; long long budget; char padding_20[4]; int verdict; };  // +0x00/+0x08/+0x10/+0x18/+0x24

FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s06_packet_update_health, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s29_packet_sound_effect, (void *jvm, void *event));

extern "C" void fake_lag_on_tick(Hook *hook) {
    if (fake_lag_cooldown > 0)
        fake_lag_cooldown--;
    __atomic_exchange_n(&fake_lag_realtime_damage_flag, fake_lag_realtime_damage_value, __ATOMIC_SEQ_CST);
    if (fake_lag_mode == 1) {
        int n = fake_lag_inbound_counter;
        bool open;
        if (n > 0) {
            fake_lag_inbound_counter = n - 50;

            open = (unsigned)n < 51;
        } else {
            fake_lag_inbound_counter = (int)((float)n + fake_lag_inbound_value);

            open = false;
        }

        __atomic_exchange_n(&fake_lag_inbound_window_open, open, __ATOMIC_SEQ_CST);
        n = fake_lag_outbound_counter;
        if (n > 0) {
            fake_lag_outbound_counter = n - 50;

            open = (unsigned)n < 51;
        } else {
            fake_lag_outbound_counter = (int)((float)n + fake_lag_outbound_value);

            open = false;
        }

        __atomic_exchange_n(&fake_lag_outbound_window_open, open, __ATOMIC_SEQ_CST);
    }
    Ctx *ctx = hook->ctx;
    unsigned char ok;
    int hurt;
    if (!ctx->world) goto off;
    if (fake_lag_require_ingame_value == 1 && !ctx->in_game) goto off;
    hurt = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
    if (fake_lag_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player)) {
        fake_lag_cooldown = 0;
        ok = false;
    } else {
        bool busy = fake_lag_cooldown != 0;
        unsigned char ra = fake_lag_require_attack_value;
        ok = (ra ^ 1) | busy;

    }
    if (hurt != fake_lag_last_hurt_time)
        fake_lag_cooldown = 20;
    fake_lag_last_hurt_time = hurt;
    if (ok) {
        __atomic_exchange_n(&fake_lag_inbound_budget, (unsigned short)(int)fake_lag_inbound_value, __ATOMIC_SEQ_CST);
        __atomic_exchange_n(&fake_lag_outbound_budget, (unsigned short)(int)fake_lag_outbound_value, __ATOMIC_SEQ_CST);
        return;
    }
    goto clear;
off:
    fake_lag_cooldown = 0;
clear:
    __atomic_exchange_n(&fake_lag_outbound_budget, (unsigned short)0, __ATOMIC_SEQ_CST);
    __atomic_exchange_n(&fake_lag_inbound_budget, (unsigned short)0, __ATOMIC_SEQ_CST);
}

extern "C" void fake_lag_on_packet_sent(PktHook *hook) {
    unsigned short v;
    if (hook->type == 2) {
        if (LIKELY(__atomic_load_n(&fake_lag_realtime_damage_flag, __ATOMIC_SEQ_CST) & 1)) {
            if (java_jni_is_instance_of_s19_packet_entity_status(hook->ctx, hook->ev) || java_jni_is_instance_of_s06_packet_update_health(hook->ctx, hook->ev) || java_jni_is_instance_of_s29_packet_sound_effect(hook->ctx, hook->ev)) {
                hook->verdict = 3;
                return;
            }
        }
        if (__atomic_load_n(&fake_lag_inbound_window_open, __ATOMIC_SEQ_CST) & 1)
            return;
        v = fake_lag_inbound_budget;
        asm volatile("");

    } else {
        if (__atomic_load_n(&fake_lag_outbound_window_open, __ATOMIC_SEQ_CST) & 1)
            return;
        v = fake_lag_outbound_budget;
        asm volatile("");

    }
    hook->budget -= v;
    if (hook->budget < 0) {
        hook->budget = 0;
        hook->verdict = 1;
    }
}
