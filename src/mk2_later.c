/*
 * bitbox MK2: stand-ins for the parts of portkit not ported yet (src/master.c: compressor / distortion pages and
 * their parameters; FX3 and the recording monitor come with it). The micro reaches those pages with its arrow keys,
 * which the MK2 does not have (two keys, HOME and INFO); the MK2 placement is still to be designed (e.g. extra Tools
 * tabs next to Main / Rec / MIDI). Until then nothing is registered and no page is changed.
 */
#include <stdint.h>

void master_register(void *table)
{
    (void)table;
}

int master_page(void *app, const void *msg, void *list)
{
    (void)app;
    (void)msg;
    (void)list;
    return 0;
}
