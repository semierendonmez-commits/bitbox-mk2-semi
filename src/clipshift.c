/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/*
 * Shift for clip pads (portkit): rotates the clip's audio against its loop by -100 .. +100 ms, so a loop whose first
 * transient sits off the file start lands on the beat. +s: at the downbeat you hear the file s ms in; the last s ms of
 * the loop come from the file start. -s: the downbeat plays the file's last s ms first.
 *
 * Clip voices (class B, 0x080b6bd0) read their sample by absolute frame position through the pad's stream (pad +0x24):
 * 0x080a58a8 (one channel plane) and 0x080a5870 (both planes, streamed files), all from 0x080b66d4; the segment
 * starts prefetch with 0x080a59cc (seek, 0x080b58fa / 0x080b5c74). Each read [pos, pos + n) inside the file becomes
 * [(pos + shift) mod frames, ...), split at the file end; reads past the end stay as they were (silence). The seek gets
 * the same mapping. Slicer pads (pad +0x254 set) are left alone. Positions are file frames; 1 ms = 48 frames (files at
 * other rates shift by a little more or less than the shown ms).
 *
 * Play length for clip pads: Full (default, the whole clip) or 1/4 .. 32 bars in quarter-bar steps. Clip voices play
 * segment by segment (re/symbols.md "Abspiellaenge"); when the next segment index reaches the clip's segment count
 * (pad +0x240 + pad +0x244, compared at 0x080b7056 / 0x080b7242) the clip loops back to segment 0 or stops. With Sync
 * on (pad +0x1f5), a segment is one Sync unit: pad +0x220 = its length in ticks, 960 per beat (Sync list 0x76: 1 bar
 * 3840, 1/2 1920, 1/4 960, 1/8 480, 1/16 240; "Slice" 0xf0000 is the slicer). The count is capped at the play length:
 * quarter bars * 960 / unit, rounded, at least 1, so the length is exact down to the Sync unit (a quarter bar is one
 * beat, 4/4). Sync None or Slice: no cap.
 *
 * Stretch for clip pads: -100 .. +100 %, speed x 1 / (1 + |p| / 100) below 0 and x (1 + p / 100) above (so -100 %
 * is half, +100 % double speed). Clips play their file at its own rate and keep time by starting each Sync unit
 * (segment k) at file position k x segment length (pad +0x1fc), 0x080b5b90 in the segment start 0x080b5a34
 * (re/symbols.md "Stretch"). Stretch scales that position by the speed, live at every segment start (pitch unchanged,
 * same jumping as when the firmware follows the song tempo), and the clip's segment count by 1 / speed: the loop bound
 * (clip_segments, with Play Len) and the "last segment" test 0x080b5adc. (pkJ-pkL scaled the clip length in the setup
 * 0x080b5224 instead; a synced clip does not re-run that setup on the device.)
 */
#include <stdint.h>

#define FN(addr) ((addr) | 1u)

typedef int (*read1_fn)(void *stream, float *dst, int n, int pos, int ch);
typedef int (*read2_fn)(void *stream, float *left, float *right, int n, int pos);
typedef void (*seek_fn)(void *stream, int pos);
typedef int (*frames_fn)(void *stream);
#define fw_read1  ((read1_fn)FN(0x080a58a8))
#define fw_read2  ((read2_fn)FN(0x080a5870))
#define fw_seek   ((seek_fn)FN(0x080a59cc))
#define fw_frames ((frames_fn)FN(0x080a5954))   /* frame count; 0x10000000 if the source does not know it */

#define P_SHIFT        0x1c3
#define P_PLAYLEN      0x1c4      /* list index: 0 = Full, q = q quarter bars (1 .. 128) */
#define PAD_SEGMENTS   0x240      /* + 0x244: segment count */
#define P_SMODE        0x1c7      /* Stretch Mode (src/stretch.c): Auto / Jump / Smooth */
#define P_STRETCH      0x1c6      /* -100 .. 100 %; not 0x1c5: the parameter command 0x0807f7bc takes that id itself (0x0807f7e2) */
#define PAD_SYNCED     0x1f5
#define PAD_UNIT_TICKS 0x220      /* int64 */
#define TICKS_PER_BEAT 960
#define SLICE_TICKS    0xf0000
#define PAD_SLICED     0x254
#define FRAMES_PER_MS  48
#define UNKNOWN_FRAMES 0x10000000

unsigned pad_setting(const uint8_t *pad, unsigned param);     /* filter.c: the pad's value from its app record */


/* the pad's shift in frames, 0 if none applies; *frames = the file's frame count */
static int shift_of(const uint8_t *pad, void *stream, int *frames)
{
    if (!pad || pad[PAD_SLICED])
        return 0;
    int ms = (int)pad_setting(pad, P_SHIFT);
    if (ms == 0 || ms < -100 || ms > 100)
        return 0;
    int total = fw_frames(stream);
    if (total <= 0 || total >= UNKNOWN_FRAMES)
        return 0;
    *frames = total;
    int s = (ms * FRAMES_PER_MS) % total;
    return s < 0 ? s + total : s;                       /* 0 <= s < total */
}

/* next run: from stock position pos, at most n frames; *at = where to read it. Returns the run length. */
static int run(int pos, int n, int s, int total, int *at)
{
    if (pos < 0 || pos >= total) {                      /* outside the file: as stock (zero-filled) */
        *at = pos;
        if (pos < 0)
            return -pos < n ? -pos : n;
        return n;
    }
    int p = pos + s;
    if (p >= total)
        p -= total;
    int m = total - pos;                                /* stay inside the file in stock terms */
    if (total - p < m)
        m = total - p;                                  /* and in shifted terms (wrap) */
    *at = p;
    return m < n ? m : n;
}

/* --- Loop Fade (0x183, ms) ------------------------------------------------------------------------------------- */
/* A recorded loop has no material beyond its ends, so a true crossfade is not possible; Loop Fade is a short dip
   instead: the last F frames before a seam fade out, the first F after it fade in (quarter sine, F = ms x 48). Seams:
   the loop start (logical 0), the loop end (synced: segments x segment length x Stretch speed, as the stock wraps,
   at most the file's end; else the file's end) and, with Shift, the file's own seam at logical total - shift. Applied
   to the frames as read (logical position pos + i), so the stock read, its resampling and the WSOLA grains all get it. */
#define P_LFADE 0x183
#define PAD_SEGLEN 0x1fc            /* float: file frames per segment */
int clip_segments(const uint8_t *pad);
static float clip_speed_of(const uint8_t *pad);

static inline __attribute__((always_inline)) float qsin(float t)  /* sin(pi / 2 t), 0 <= t <= 1 */
{
    float t2 = t * t;
    return t * (1.5707963f + t2 * (-0.6459641f + t2 * 0.0751678f));   /* coefficients sum to 1: ends at exactly 1 */
}

static void fade_zone(float *l, float *r, int n, int pos, int a, int b, int out, float inv)
{
    /* frames with logical position in [a, b): out ? fading out towards b : fading in from a */
    int lo = a > pos ? a - pos : 0, hi = b < pos + n ? b - pos : n;
    for (int i = lo; i < hi; i++) {
        int d = out ? b - 1 - (pos + i) : pos + i - a;
        float g = qsin((float)d * inv);
        l[i] *= g;
        if (r && r != l)
            r[i] *= g;
    }
}

static void loop_fade(const uint8_t *pad, void *stream, float *l, float *r, int n, int pos)
{
    if (!pad || n <= 0)
        return;
    int ms = (int)pad_setting(pad, P_LFADE);
    if (ms <= 0 || ms > 50)
        return;
    int total = fw_frames(stream);
    if (total <= 0 || total >= UNKNOWN_FRAMES)
        return;
    int end = total;
    if (pad[PAD_SYNCED] && !pad[PAD_SLICED]) {
        float e = (float)clip_segments(pad) * *(const float *)(pad + PAD_SEGLEN) * clip_speed_of(pad);
        if (e > 1.0f && e < (float)total)
            end = (int)e;
    }
    int f = ms * FRAMES_PER_MS;
    if (f > end / 4)
        f = end / 4;
    if (f < 2)
        return;
    float inv = 1.0f / (float)f;
    if (pos < f)
        fade_zone(l, r, n, pos, 0, f, 0, inv);
    if (pos + n > end - f)
        fade_zone(l, r, n, pos, end - f, end, 1, inv);
    int sh = shift_of(pad, stream, &total);
    if (sh) {
        int seam = total - sh;                          /* logical position of the file's seam */
        if (seam > f && seam < end - f) {
            fade_zone(l, r, n, pos, seam - f, seam, 1, inv);
            fade_zone(l, r, n, pos, seam, seam + f, 0, inv);
        }
    }
}

/* thunks in clipshift_thunk.S pass the pad (r4 of 0x080b66d4 / 0x080b576c / 0x080b5a34) as the last argument */
static int clip_read1_raw(void *stream, float *dst, int n, int pos, int ch, const uint8_t *pad)
{
    int total, s = shift_of(pad, stream, &total);
    if (!s)
        return fw_read1(stream, dst, n, pos, ch);
    int ok = 0;
    while (n > 0) {
        int at, m = run(pos, n, s, total, &at);
        ok |= fw_read1(stream, dst, m, at, ch);
        dst += m;
        pos += m;
        n -= m;
    }
    return ok;
}

static int clip_read2_raw(void *stream, float *left, float *right, int n, int pos, const uint8_t *pad)
{
    int total, s = shift_of(pad, stream, &total);
    if (!s)
        return fw_read2(stream, left, right, n, pos);
    int ok = 0;
    while (n > 0) {
        int at, m = run(pos, n, s, total, &at);
        ok |= fw_read2(stream, left, right, m, at);
        left += m;
        if (right)
            right += m;
        pos += m;
        n -= m;
    }
    return ok;
}

int clip_read1(void *stream, float *dst, int n, int pos, int ch, const uint8_t *pad)
{
    int ok = clip_read1_raw(stream, dst, n, pos, ch, pad);
    loop_fade(pad, stream, dst, 0, n, pos);
    return ok;
}

int clip_read2(void *stream, float *left, float *right, int n, int pos, const uint8_t *pad)
{
    int ok = clip_read2_raw(stream, left, right, n, pos, pad);
    loop_fade(pad, stream, left, right, n, pos);
    return ok;
}

void clip_seek(void *stream, int pos, const uint8_t *pad)
{
    int total, s = shift_of(pad, stream, &total);
    if (s && pos >= 0 && pos < total) {
        pos += s;
        if (pos >= total)
            pos -= total;
    }
    fw_seek(stream, pos);
}

static int stretched_count(const uint8_t *pad, int count);

/* --- play length ------------------------------------------------------------------------------------------------- */
const char *const playlen_names[129] = {
    "Full", "1/4", "1/2", "3/4", "1", "1 1/4", "1 1/2", "1 3/4", "2", "2 1/4",
    "2 1/2", "2 3/4", "3", "3 1/4", "3 1/2", "3 3/4", "4", "4 1/4", "4 1/2", "4 3/4",
    "5", "5 1/4", "5 1/2", "5 3/4", "6", "6 1/4", "6 1/2", "6 3/4", "7", "7 1/4",
    "7 1/2", "7 3/4", "8", "8 1/4", "8 1/2", "8 3/4", "9", "9 1/4", "9 1/2", "9 3/4",
    "10", "10 1/4", "10 1/2", "10 3/4", "11", "11 1/4", "11 1/2", "11 3/4", "12", "12 1/4",
    "12 1/2", "12 3/4", "13", "13 1/4", "13 1/2", "13 3/4", "14", "14 1/4", "14 1/2", "14 3/4",
    "15", "15 1/4", "15 1/2", "15 3/4", "16", "16 1/4", "16 1/2", "16 3/4", "17", "17 1/4",
    "17 1/2", "17 3/4", "18", "18 1/4", "18 1/2", "18 3/4", "19", "19 1/4", "19 1/2", "19 3/4",
    "20", "20 1/4", "20 1/2", "20 3/4", "21", "21 1/4", "21 1/2", "21 3/4", "22", "22 1/4",
    "22 1/2", "22 3/4", "23", "23 1/4", "23 1/2", "23 3/4", "24", "24 1/4", "24 1/2", "24 3/4",
    "25", "25 1/4", "25 1/2", "25 3/4", "26", "26 1/4", "26 1/2", "26 3/4", "27", "27 1/4",
    "27 1/2", "27 3/4", "28", "28 1/4", "28 1/2", "28 3/4", "29", "29 1/4", "29 1/2", "29 3/4",
    "30", "30 1/4", "30 1/2", "30 3/4", "31", "31 1/4", "31 1/2", "31 3/4", "32"
};

/* thunk (clip_segments_thunk): replaces `ldrd r1, r2, [r6, #0x240]` before `add r2, r1`; returns the segment count to
   use (pad = r6) */
int clip_segments(const uint8_t *pad)
{
    int count = *(const int32_t *)(pad + PAD_SEGMENTS) + *(const int32_t *)(pad + PAD_SEGMENTS + 4);
    if (pad[PAD_SLICED])
        return count;
    if (!pad[PAD_SYNCED] || count < 1)
        return count;
    count = stretched_count(pad, count);
    int q = (int)pad_setting(pad, P_PLAYLEN);
    int32_t unit = *(const int32_t *)(pad + PAD_UNIT_TICKS);
    if (q <= 0 || q > 128 || unit <= 0 || unit >= SLICE_TICKS || *(const int32_t *)(pad + PAD_UNIT_TICKS + 4))
        return count;
    int limit = (q * TICKS_PER_BEAT + unit / 2) / unit;
    if (limit < 1)
        limit = 1;
    return limit < count ? limit : count;
}

/* --- stretch -------------------------------------------------------------------------------------------------------- */
/* the pad's Stretch speed (1 = none) */
static float clip_speed_of(const uint8_t *pad)
{
    if (pad[PAD_SLICED])
        return 1.0f;
    int p = (int)pad_setting(pad, P_STRETCH);
    if (p == 0 || p < -100 || p > 100)
        return 1.0f;
    return p > 0 ? 1.0f + (float)p * 0.01f : 1.0f / (1.0f - (float)p * 0.01f);
}

/* segments at this speed, rounded, at least 1 */
static int stretched_count(const uint8_t *pad, int count)
{
    float n = (float)count / clip_speed_of(pad) + 0.5f;
    int c = (int)n;
    return c < 1 ? 1 : c;
}

/* stretch.c: the speed for the smooth (WSOLA) read */
float clip_stretch_speed(const uint8_t *pad)
{
    return clip_speed_of(pad);
}

/* thunk (clip_pos_thunk): `vmul.f32 s16, s15, s16` (segment index x segment length) at 0x080b5b90 */
float clip_speed(const uint8_t *pad)
{
    return clip_speed_of(pad);
}

/* thunk (clip_last_thunk): `ldr.w r3, [r4, #0x240]` at 0x080b5adc (r3 - 1 == index: last segment) */
int clip_last_count(const uint8_t *pad)
{
    int count = *(const int32_t *)(pad + PAD_SEGMENTS);
    return count < 1 ? count : stretched_count(pad, count);
}


/* --- Sync 1/32 ------------------------------------------------------------------------------------------------------ */
/* Sync (0x76) list: the stock 7 entries (pointers into the stock strings, list at 0x080c58c0) + "1/32", appended so the
   stored indexes of old presets keep their meaning (6 = None). Its beats table (0x080c60cc, read only by the clip param
   handler at 0x080b84ae through the literal 0x080b860c, x 960 ticks) gets the same eighth entry: 1/32 = 0.125 beat,
   120 ticks. The preset reader's legacy conversion (0x080830da) keeps searching the stock 7. */
typedef void (*register_list_fn)(void *table, int param, const char *label, const char *const *names, int count,
                                 const char *xml);
#define fw_register_list ((register_list_fn)FN(0x0806a27c))

const char *const sync_names[8] = {
    (const char *)0x080c45d0u, (const char *)0x080c4528u, (const char *)0x080c4530u, (const char *)0x080c4534u,
    (const char *)0x080c4538u, (const char *)0x080c453cu, (const char *)0x080c3348u, "1/32",
};
const float sync_beats[8] = {1024.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.0f, 0.125f};

void sync_register(void *table, int param, const char *label, const char *const *names, int count, const char *xml)
{
    (void)names;
    (void)count;
    fw_register_list(table, param, label, sync_names, 8, xml);
}

/* --- hide Play Len / Stretch where they do nothing ------------------------------------------------------------------- */
/* Both need the clip synced: Quant Size (0x2a) not None (8) and Sync (0x76) neither Slice (0) nor None (6) (pad +0x1f5,
   0x080b80fc). The page builder 0x08078ec8(app, msg, list, ..) fills a parameter page; the stock hides entries the same
   way (0x080884c8, Delay vs. Beat Sync at 0x0807902e). */
typedef int (*page_fn)(void *app, const void *msg, void *list, int arg);
typedef void *(*record_fn)(void *app, const void *msg);
typedef void (*list_remove_fn)(void *list, unsigned param);
typedef int (*store_get_fn)(const void *store, unsigned param);
#define fw_page        ((page_fn)FN(0x08078ec8))
#define fw_record      ((record_fn)FN(0x08078e90))
#define fw_list_remove ((list_remove_fn)FN(0x080884c8))
int fast_get(const void *store, unsigned param);       /* fastget.c: 0x08089388 with a cache */
#define fw_store_get fast_get
#define P_QUANT        0x2a
#define P_SYNC         0x76
#define QUANT_NONE     8
#define SYNC_SLICE     0
#define SYNC_NONE      6

static int list_has(const uint8_t *list, unsigned param)
{
    unsigned n = *(const uint32_t *)list;
    for (unsigned i = 0; i < n && i < 0x30; i++)
        if (*(const uint16_t *)(list + 4 + 12 * i) == param)
            return 1;
    return 0;
}

int fx_page(void *app, const void *msg, uint8_t *list);   /* fx.c */
#define P_RVB_TYPE 0x178
#define APP_FX     0x2200u                             /* app + 0x2200 + row x 0x18: FX cell records */

int master_page(void *app, const void *msg, uint8_t *list);
void gen_page(const uint8_t *rec, const uint8_t *app);

int clip_page(void *app, const void *msg, void *list, int arg)
{
    int r = fw_page(app, msg, list, arg);
    if (master_page(app, msg, list))                   /* master.c: Compressor / Distortion on the settings page */
        return r;
    /* FX cells (layer 3): reverb / delay type pages (fx.c) */
    if (fx_page(app, msg, list))
        return r;
    gen_page(fw_record(app, msg), app);                 /* gen.c: the pad's tab 3 is "Gen" or "Gran" */
    if (!list_has(list, P_PLAYLEN) && !list_has(list, P_STRETCH) && !list_has(list, P_SMODE))
        return r;
    const void *rec = fw_record(app, msg);
    int quant = fw_store_get(rec, P_QUANT), sync = fw_store_get(rec, P_SYNC);
    if (quant == QUANT_NONE || sync == SYNC_SLICE || sync == SYNC_NONE) {
        fw_list_remove(list, P_PLAYLEN);
        fw_list_remove(list, P_STRETCH);
        fw_list_remove(list, P_SMODE);
    }
    return r;
}

