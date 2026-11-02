/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/*
 * Parameter store lookup with a cache (portkit). Same result as the stock 0x08089388(store, param): stores are
 * +4 entries (8 bytes: id u16, value i32 at +4), +0xa count; a missing id reads 0. The stock function scans linearly
 * (pad records: ~66 entries, settings: ~51), every block for every voice; here the index of each (store, param) is
 * remembered in a direct-mapped table and checked against the store before use (id at that index, index < count),
 * so a rebuilt or reloaded store, a collision or a write from another thread can only cost a rescan, never a wrong
 * value. Table: 256 x 8 bytes at 0x2406b000 (patch RAM, free; 0x24070dc0 and up is the firmware's, see re/symbols.md).
 */
#include <stdint.h>

struct gcache { uint32_t store; uint16_t param, idx; };
#define GC ((volatile struct gcache *)0x2406b000u)

int fast_get(const void *store, unsigned param)
{
    const uint8_t *s = (const uint8_t *)store;
    unsigned cnt = *(const uint16_t *)(s + 0xa);
    const uint8_t *ent = *(const uint8_t *const *)(s + 4);
    volatile struct gcache *c = &GC[(((uint32_t)store >> 3) ^ (param * 0x9e37u)) & 255u];
    unsigned i = c->idx;
    if (c->store == (uint32_t)store && c->param == param && i < cnt && *(const uint16_t *)(ent + 8 * i) == param)
        return *(const int32_t *)(ent + 8 * i + 4);
    for (i = 0; i < cnt; i++)
        if (*(const uint16_t *)(ent + 8 * i) == param) {
            c->idx = (uint16_t)i;
            c->param = (uint16_t)param;
            c->store = (uint32_t)store;
            return *(const int32_t *)(ent + 8 * i + 4);
        }
    return 0;
}
