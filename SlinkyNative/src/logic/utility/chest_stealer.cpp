// Chest Stealer - descriptor +0x1d8: when a container screen is open, (re)arm
// the steal delay with a random factor in [1.0, 1.5].
#include "slinky/engine.h"

S(str_container_chest);  // "container.chest"
S(str_container_chestdouble);  // "container.chestDouble"

extern "C" float chest_stealer_delay_after_open_value;                 // "delay_after_open" current value
extern "C" unsigned char chest_stealer_require_vanilla_title_value;         // "require_vanilla_title"
extern "C" float chest_stealer_steal_delay;                 // steal delay (ms)

FN(void *, get_current_screen, (void *jvm, void *object));
FN(unsigned char, java_jni_is_instance_of_gui_chest, (void *jvm, void *scr));
FN(void *, java_get_gui_chest_lower_chest_inventory, (void *jvm, void *scr));
FN(void *, java_jni_is_instance_of_inventory_basic, (void *jvm, void *candidate));
FN(void *, java_get_inventory_basic, (void *jvm, void *inventory_basic));
FN(unsigned char, java_string_equal_translation_key, (void *jvm, void *title, const char *name));
FN(void *, rot_make_floats, (float lo, float hi));
FN(float, random_rotation, (void *rng));
FN(void, slot_index, (int event));

extern "C" void chest_stealer_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    void *scr = get_current_screen(ctx->jvm, ctx->minecraft);
    if (scr && java_jni_is_instance_of_gui_chest(ctx->jvm, scr)) {
        if (chest_stealer_require_vanilla_title_value != 1)
            return;
        void *jvm = ctx->jvm;
        void *inventory_basic = java_jni_is_instance_of_inventory_basic(jvm, java_get_gui_chest_lower_chest_inventory(jvm, scr));
        if (inventory_basic) {
            void *title = java_get_inventory_basic(ctx->jvm, inventory_basic);
            if (java_string_equal_translation_key(ctx->jvm, title, str_container_chest))
                return;
            if (java_string_equal_translation_key(ctx->jvm, title, str_container_chestdouble))
                return;
        }
    }
    chest_stealer_steal_delay = chest_stealer_delay_after_open_value;
    chest_stealer_steal_delay = random_rotation(rot_make_floats(1.0f, 1.5f)) * chest_stealer_steal_delay;
    slot_index(13);
}
