/* Garage: the car and part data model (garage.md §4.1 - §4.3, §4.12), from the decompile of
 * 0000:3c5a - 43bf, 49a2, 76a7 - 77b7, 7ee6. The pools and part_alloc / car_alloc are game_flow's
 * (flow.h). Records are u16 DS offsets. */
#include "game/garage.h"

#include "game/flow.h"
#include "platform/platform.h"

#define INIT_WEAR 0x4966
#define CARBS_PER_MANIFOLD 0x5126

static inline s16 init_wear(s16 cat) { return DSS((u16)(INIT_WEAR + 2 * cat)); }

/* 0000:3c5a */
s16 part_value(u16 p)
{
    s16 t = DSS((u16)(p + PT_TYPE));
    if (parts_category(t) == 0 && parts_grade(t) == 0) return 100;
    return DSS((u16)(p + PT_VALUE));
}

/* 0000:3ff0 */
void spare_add(s16 type, s16 value)
{
    u16 p = part_alloc();
    DSW((u16)(p + PT_TYPE)) = (u16)type;
    DSW((u16)(p + PT_WEAR)) = (u16)init_wear(parts_category(type));
    DSW((u16)(p + PT_VALUE)) = (u16)value;
    DSW((u16)(p + PT_NEXT)) = DSW(G_SPARES);
    DSW(G_SPARES) = p;
}

/* One stock part of catalogue type t into the slot (0000:4036). */
static u16 stock_part(u16 reuse, u16 slot, s16 t, s16 wear)
{
    u16 p = reuse == 0 ? part_alloc() : DSW(slot);
    DSW(slot) = p;
    DSW((u16)(p + PT_TYPE)) = (u16)t;
    DSW((u16)(p + PT_VALUE)) = (u16)parts_price(t);
    DSW((u16)(p + PT_WEAR)) = (u16)wear;
    DSW((u16)(p + PT_NEXT)) = 0;
    return p;
}

/* 0000:4036 */
u16 car_new(s16 model, s16 value, u16 reuse)
{
    u16 c = reuse != 0 ? reuse : car_alloc();
    DSW((u16)(c + CR_MODEL)) = (u16)model;
    DSW((u16)(c + CR_VALUE)) = (u16)value;
    DSW((u16)(c + CR_GAS)) = 0x28;
    DSB((u16)(c + CR_CLASS)) = model_class(model);
    s16 col;
    do {
        col = rnd(6);
        if (col != 5) break;
    } while (rnd(2) != 1);
    DSB((u16)(c + CR_COLOUR)) = (u8)col;
    DSB((u16)(c + CR_ENGINE_LINK)) = 1;
    DSB((u16)(c + CR_IGNITION)) = (u8)(rnd(0x0D) - 8);
    u16 spec = model_spec(model);
    u16 tg = (spec >> 6) & 3;
    u16 make = (u16)((spec & 0x0F) >> 1);
    stock_part(reuse, (u16)(c + CR_TRANS), (s16)(make + (tg == 3) + tg * 3 + 9), init_wear(1));
    stock_part(reuse, (u16)(c + CR_ENGINE), (s16)(((spec >> 8) & 7) * 3 + make), init_wear(0));
    stock_part(reuse, (u16)(c + CR_TYRES), (s16)(((spec >> 4) & 3) + 0x28), init_wear(4));
    u16 mp_ = reuse == 0 ? part_alloc() : DSW((u16)(c + CR_MANIFOLD));
    DSW((u16)(c + CR_MANIFOLD)) = mp_;
    for (u16 k = 0; k < 6; k++) DSB((u16)(c + 0x19 + k)) = 0;
    spec = model_spec(model);
    u16 mg = (spec >> 11) & 7;
    s16 t = (s16)(mg * 3 + make + 0x19);
    DSW((u16)(mp_ + PT_TYPE)) = (u16)t;
    DSW((u16)(mp_ + PT_VALUE)) = (u16)parts_price(t);
    DSW((u16)(mp_ + PT_WEAR)) = (u16)init_wear(3);
    DSW((u16)(mp_ + PT_NEXT)) = 0;
    s16 carb = (s16)((spec >> 14) + 0x16);
    s16 n = DSS((u16)(CARBS_PER_MANIFOLD + 2 * mg));
    s16 k = (s16)(n - 1);
    if (k < 2)
        for (s16 j = 2; j > k; j--) DSW((u16)(c + CR_CARB + 2 * j)) = 0;
    for (; k >= 0; k--) {
        stock_part(reuse, (u16)(c + CR_CARB + 2 * k), carb, init_wear(2));
        DSB((u16)(c + 0x19 + 2 * k)) = 3;
        DSB((u16)(c + 0x1A + 2 * k)) = 3;
    }
    DSW((u16)(c + CR_BAY_BOLT)) = 0x0303;
    DSB((u16)(c + CR_BAY_BOLT + 2)) = 3;
    DSW((u16)(c + CR_TRANS_BOLT)) = 0x0303;
    DSB((u16)(c + CR_FLAGS)) = 0;
    DSB((u16)(c + CR_FLAGS + 1)) &= 0xE0;
    DSB((u16)(c + CR_FLAGS + 1)) &= 0xDF;
    DSB((u16)(c + CR_FLAGS + 1)) &= 0xBF;
    DSB((u16)(c + CR_FLAGS + 1)) &= 0x7F;
    if (reuse == 0) {
        if (DSW(G_CUR_CAR) != 0) {
            DSW((u16)(c + CR_NEXT)) = DSW(G_OTHER_CARS);
            DSW(G_OTHER_CARS) = c;
        } else {
            DSW(G_CUR_CAR) = c;
        }
    }
    return c;
}

/* 0000:4386 */
s16 part_release(u16 p, s16 keep)
{
    if (p == 0) return 0;
    s16 v = DSS((u16)(p + PT_VALUE));
    if (keep == 0) part_free(p);
    else {
        DSW((u16)(p + PT_NEXT)) = DSW(G_SPARES);
        DSW(G_SPARES) = p;
    }
    return v;
}

/* 0000:43bf */
s16 car_free(u16 car, s16 keep)
{
    DSW((u16)(car + CR_VALUE)) = (u16)(DSS((u16)(car + CR_VALUE)) - part_release(DSW((u16)(car + CR_TRANS)), keep));
    for (u16 i = 0; i < 5; i++)
        DSW((u16)(car + CR_VALUE)) = (u16)(DSS((u16)(car + CR_VALUE)) -
                                           part_release(DSW((u16)(car + CR_ENGINE + 2 * i)), keep));
    part_release(DSW((u16)(car + CR_TYRES)), 0);
    s16 v = DSS((u16)(car + CR_VALUE));
    if (v < 0) v = 0;
    car_release(car);
    return v;
}

/* 0000:49a2 (the engine must be present) */
u16 car_flags(u16 car)
{
    u16 f = 0, fl = DSW((u16)(car + CR_FLAGS));
    if (fl & 0x2000) f = 4;
    if (fl & 0x4000) f |= 2;
    if (fl & 0x8000) f |= 1;
    if (parts_grade(DSS((u16)(DSW((u16)(car + CR_ENGINE)) + PT_TYPE))) == 2) f |= 8;
    return f;
}

/* 0000:76a7: spares of category cat into nodes[n0+1..], DS:49E0[n0+1..] (text ids), wear[n0+1..]. */
void spares_collect(s16 cat, u16 *nodes, s8 *wear, s16 n0)
{
    s16 n = n0;
    for (u16 p = DSW(G_SPARES); p != 0; p = DSW((u16)(p + PT_NEXT))) {
        s16 t = DSS((u16)(p + PT_TYPE));
        if (parts_category(t) == cat) {
            n++;
            nodes[n] = p;
            DSW((u16)(G_LIST_BUF + 2 * n)) = (u16)parts_text(t);
            s16 w = DSS((u16)(p + PT_WEAR));
            wear[n] = w == -0x80 ? (s8)0x80 : (s8)(w / 100);
        }
    }
    DSW(G_LIST_BUF) = (u16)n;
}

/* 0000:7741: the slot's old part becomes a spare; always adjusts cur_car's value */
void part_install(u16 part, u16 slot)
{
    u16 cur = DSW(G_CUR_CAR);
    u16 old = DSW(slot);
    if (old != 0) {
        DSW((u16)(old + PT_NEXT)) = DSW(G_SPARES);
        DSW(G_SPARES) = old;
        DSW(cur) = (u16)(DSS(cur) - part_value(old));
    }
    DSW(slot) = part;
    DSW((u16)(part + PT_NEXT)) = 0;
    DSW(cur) = (u16)(DSS(cur) + part_value(part));
}

/* 0000:7789 */
void part_uninstall(u16 slot)
{
    u16 cur = DSW(G_CUR_CAR);
    DSW((u16)(DSW(slot) + PT_NEXT)) = DSW(G_SPARES);
    DSW(G_SPARES) = DSW(slot);
    DSW(cur) = (u16)(DSS(cur) - part_value(DSW(G_SPARES)));
    DSW(slot) = 0;
}

/* 0000:77b7 */
u16 spare_unlink(u16 part)
{
    u16 prev = 0, q;
    for (q = DSW(G_SPARES); q != 0 && q != part; q = DSW((u16)(q + PT_NEXT))) prev = q;
    if (prev == 0) DSW(G_SPARES) = DSW((u16)(q + PT_NEXT));
    else DSW((u16)(prev + PT_NEXT)) = DSW((u16)(q + PT_NEXT));
    return q;
}

/* 0000:7ee6 */
s16 car_runnable(u16 car, s16 full)
{
    if (DSW((u16)(car + CR_ENGINE)) == 0 || DSC((u16)(car + CR_ENGINE_LINK)) < 1 ||
        DSW((u16)(car + CR_MANIFOLD)) == 0)
        return 0;
    s16 n = DSS((u16)(CARBS_PER_MANIFOLD + 2 * parts_grade(DSS((u16)(DSW((u16)(car + CR_MANIFOLD)) + PT_TYPE)))));
    for (s16 k = 2; k < n + 2; k++)
        if (DSW((u16)(car + CR_CARB + 2 * (k - 2))) == 0) return 0;
    for (s16 i = (s16)(n * 2 + 3); --i >= 0;)
        if (DSC((u16)(car + CR_BAY_BOLT + i)) != 3) return 0;
    if (full != 0) {
        if (DSW((u16)(car + CR_TRANS)) == 0) return 0;
        if (DSW((u16)(car + CR_TYRES)) == 0) return 0;
        for (s16 i = 2; --i >= 0;)
            if (DSC((u16)(car + CR_TRANS_BOLT + i)) != 3) return 0;
    }
    return 1;
}
