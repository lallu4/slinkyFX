// Item Use Fix - descriptor +0x128 / +0x138.
// The first hook records whether the held item is usable (item_use_fix_item_use_flag); the
// second re-applies that state on the following tick.
#include "slinky/engine.h"

extern "C" unsigned char item_use_fix_item_use_flag;  // item-use flag

struct UseState { char padding_00[8]; Ctx *ctx; unsigned char skip; };  // skip @ +0x10

FN(void *, java_get_minecraft_player_controller, (void *jvm, void *minecraft));
FN(unsigned char, java_get_player_controller_mp_is_hitting_block, (void *jvm, void *player_controller_mp));
FN(void, java_set_player_controller_mp_is_hitting_block, (void *jvm, void *player_controller_mp, int on));

extern "C" void item_use_fix_clear_hitting_block(UseState *s) {
    if (__builtin_expect(s->skip != 0, 0))
        goto out;
    {
        Ctx *ctx = s->ctx;
        void *item = java_get_minecraft_player_controller(ctx->jvm, ctx->minecraft);
        unsigned char usable = java_get_player_controller_mp_is_hitting_block(ctx->jvm, item);
        item_use_fix_item_use_flag = usable != 0;
        if (usable) {
            java_set_player_controller_mp_is_hitting_block(ctx->jvm, item, 0);
            return;
        }
    }
out:

}

extern "C" void item_use_fix_restore_hitting_block(UseState *s) {
    if (item_use_fix_item_use_flag == 1 && s->skip == 0) {
        Ctx *ctx = s->ctx;
        void *item = java_get_minecraft_player_controller(ctx->jvm, ctx->minecraft);
        void *jvm = ctx->jvm;
        unsigned char on = item_use_fix_item_use_flag;
        java_set_player_controller_mp_is_hitting_block(jvm, item, on);
    }
}
