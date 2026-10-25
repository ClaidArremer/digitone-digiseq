/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DIGISEQ for Digitone: sequencer types a track can play in place of its own
 * trigs, for the Digitone mk1 (OS 1.43 and 1.44). Every track picks its own
 * TYPE, so T1 can play SNAKE, T2 185 and T3 its own trigs.
 *
 *   SNAKE  a Cartesian sequencer after MDD Snake (and Make Noise's Rene):
 *          eight cells in a 4 x 2 grid that matches knobs A-H, walked by a
 *          path. DIGISNAKE 1.3, unchanged, and its projects load as they were.
 *   185    a stage sequencer after ML-185 (Nico Starke's free Max for Live
 *          device, itself after RYK's M-185): eight stages, each with a pitch,
 *          a velocity, a length in clock pulses (1-8: how long the stage plays
 *          before the next) and a mode: OFF (rests), ONE (one note on its
 *          first pulse), ALL (a note every pulse, or every DIV pulses) or HOLD
 *          (one note held as long as the stage lasts).
 *
 * The page is TRIG's next page: press TRIG again from the TRIG page. Its
 * pages depend on the track's type:
 *   SNAKE  NOTES, GATES, VELOCITY, PATH
 *   185    PITCH, VELOCITY, PULSES, MODE, DIV, SETUP
 * The last page's knob A is TYPE: OFF, SNAKE or 185. OFF keeps the type's
 * settings, so the pages still show (and edit) the type it had.
 *   knobs A-H        the cells / stages of the page shown (the last page: its
 *                    eight settings)
 *   LEFT / RIGHT     the previous / next page
 *   UP / DOWN        NOTES / PITCH: all a semitone (FUNC: an octave);
 *                    GATES, MODE: rotate; VELOCITY: all up or down;
 *                    PULSES, DIV: all one more or less
 *   trig keys 1-8    GATES: toggle that cell; MODE: that stage's next mode
 *   hold TRIG + UP / DOWN  the next / previous TYPE
 *   hold TRIG + YES  randomise the page shown
 *   hold TRIG + NO   back to how it was when TRIG went down (a print too)
 *   hold TRIG + PLAY clear the page shown
 *   hold TRIG + REC / STOP  copy the page shown / paste it (same type)
 *   hold TRIG + FUNC + REC  PRINT: what the track plays into its own trigs,
 *                    notes, velocities (and 185's held lengths), TYPE off
 *   FUNC + YES       randomise the note pages together (SNAKE: NOTES, GATES,
 *                    VELOCITY; 185: PITCH, VELOCITY, PULSES, MODE)
 *   FUNC + NO        back to before the last run of FUNC + YES
 *   FUNC + knob      jump: notes -24/-12/0/+12/+24, velocity 1/32/64/96/127,
 *                    RANGE 1/12/24
 *
 * On OS 1.44 (its port's cflags) the stock drop-down message names what was
 * done, and every change asks the OS to refresh its power-up copy of the
 * project, so the settings survive a power cycle.
 *
 * The sequencer side uses seq-events 0.2: ev_gen_trig decides whether a step
 * fires, ev_gen_notes its note, ev_fire_vel its velocity and ev_fire_len its
 * length (185's HOLD and LEN). The UI uses core-dn1's mod page and project
 * data. No firmware address is named here but the 1.44 port's.
 */
#include "digitone-mk1/core3.h"
#include "digitone-mk1/seq.h"

#ifndef SQ_LEN_MAX
#error "DIGISEQ needs seq-events 0.2 (ev_fire_len)"
#endif

#if defined(DSQ_POPUP_GET) && defined(DSQ_POPUP_PRINTF)
#define POPUP(...) do { uint32_t m_ = ((uint32_t (*)(void))DSQ_POPUP_GET)(); \
                        if (m_) ((void (*)(uint32_t, const char *, ...))DSQ_POPUP_PRINTF)(m_, __VA_ARGS__); \
                   } while (0)
#else
#define POPUP(...) do { } while (0)
#endif

#define TRACKS    8             /* 0-3 the synth tracks, 4-7 the MIDI tracks */
#define CELLS     8
#define NOTE_MAX  24

enum { E_SNAKE = 1, E_185 = 2 };
static const char *const type_names[3] = { "OFF", "SNAKE", "185" };

/* ==== the project's data ================================================ */

/* SNAKE (DIGISNAKE's, so its projects' SNK1 and SNK2 load as they were) */
enum { P_ON, P_SHAPE, P_DIR, P_LEN, P_DIV, P_SCALE, P_RANGE, P_SHIFT, NSET };
struct snake {
    signed char note[CELLS];    /* semitones from NOTE */
    unsigned char vel[CELLS];   /* 1-127 */
    unsigned char set[NSET];    /* the PATH page; set[P_ON] mirrors TYPE SNAKE */
    unsigned char gate;         /* bit c: cell c fires */
    unsigned char pad[7];       /* pad[0]: 2X's speed to put back + 1 */
};                              /* 32 bytes */
struct snake digiseq_snake[TRACKS];

/* 185 and the track's TYPE */
enum { M_OFF, M_ONE, M_ALL, M_HOLD, MODES };
static const char *const mode_names[MODES] = { "OFF", "ONE", "ALL", "HOLD" };
enum { S_TYPE, S_DIR, S_STAGES, S_CLOCK, S_SCALE, S_RANGE, S_NLEN, S_SHIFT, NSETS };
struct stages {
    signed char pitch[CELLS];   /* semitones from NOTE */
    unsigned char vel[CELLS];   /* 1-127 */
    unsigned char pulses[CELLS];/* 1-8 clock pulses */
    unsigned char mode[CELLS];  /* M_ */
    unsigned char div[CELLS];   /* ALL: a note every div pulses, 1-4 */
    unsigned char set[NSETS];   /* SETUP; set[S_TYPE] unused (struct track's) */
};                              /* 48 bytes */
struct track {
    unsigned char on;           /* TYPE is not OFF */
    unsigned char eng;          /* E_SNAKE or E_185: the type (kept while OFF) */
    unsigned char spd;          /* 185 2X: the speed to put back + 1 */
    unsigned char pad0;
    struct stages st;
    unsigned char pad[12];
};                              /* 64 bytes */
struct track digiseq_trk[TRACKS];

/* ==== shared tables ===================================================== */

#define SHAPES 7
static const unsigned char shapes[SHAPES][CELLS] = {
    {0, 1, 2, 3, 4, 5, 6, 7},   /* ROWS   left to right, top then bottom */
    {0, 1, 2, 3, 7, 6, 5, 4},   /* SNAKE  along the top, back along the bottom */
    {0, 4, 1, 5, 2, 6, 3, 7},   /* COLS   down each column */
    {0, 4, 5, 1, 2, 6, 7, 3},   /* ZIGZAG down, across, up, across */
    {0, 5, 2, 7, 4, 1, 6, 3},   /* DIAG   diagonal hops */
    {0, 3, 7, 4, 1, 2, 6, 5},   /* CORNER the corners, then the middle */
    {0, 6, 1, 7, 2, 4, 3, 5},   /* HOP    two across and down */
};
static const char *const shape_names[SHAPES] = {
    "ROWS", "SNAKE", "COLS", "ZIGZAG", "DIAG", "CORNER", "HOP"
};

enum { D_FWD, D_BWD, D_FB, D_BF, D_RND, DIRS };
static const char *const dir_names[DIRS] = { "FWD", "BWD", "F/B", "B/F", "RND" };

#define SCALES 12
static const unsigned short scale_mask[SCALES] = {
    0xfff, 0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x9ad, 0x295, 0x4a9, 0x4e9, 0x555,
};
static const char *const scale_names[SCALES] = {
    "CHR", "MAJ", "MIN", "DOR", "PHR", "LYD", "MIX", "HMIN", "PENT", "mPNT", "BLUE", "WHOL"
};

/* SNAKE's PATH page */
static const unsigned char set_min[NSET] = { 0, 0, 0, 1, 0, 0, 1, 0 };
static const unsigned char set_max[NSET] = { 1, SHAPES - 1, DIRS - 1, CELLS, 8, SCALES - 1, NOTE_MAX, CELLS - 1 };
static const char *const set_labels[NSET] = { "TYPE", "SHAPE", "DIR", "LEN", "DIV", "SCALE", "RANGE", "SHIFT" };

/* 185's SETUP page */
#define NLENS 5
static const unsigned char sset_min[NSETS] = { 0, 0, 1, 0, 0, 1, 0, 0 };
static const unsigned char sset_max[NSETS] = { 0, DIRS - 1, CELLS, 8, SCALES - 1, NOTE_MAX, NLENS - 1, CELLS - 1 };
static const char *const sset_labels[NSETS] = { "TYPE", "DIR", "STAGES", "CLOCK", "SCALE", "RANGE", "LEN", "SHIFT" };
static const char *const nlen_names[NLENS] = { "TRK", "1/32", "1/16", "1/8", "1/4" };

/* DIV (SNAKE) and CLOCK (185): sequencer steps a move (1 = 1X, 2 = 1/2, 4 =
 * 1/4, 8 = 1/8), or 0: 2X, the track itself at twice its speed (per-track
 * SCALE mode) and a move every step. */
#define DIVS 5
static const unsigned char div_list[DIVS] = { 8, 4, 2, 1, 0 };
static const char *div_name(int v)
{
    switch (v) {
    case 0: return "2X";
    case 1: return "1X";
    case 2: return "1/2";
    case 4: return "1/4";
    case 8: return "1/8";
    default: return "1/?";
    }
}
static int div_turn(int v, long d)
{
    int i, best = 3;
    for (i = 0; i < DIVS; i++)
        if (div_list[i] == v)
            best = i;
    if (best == 3 && v != 1) {
        for (i = 0; i < DIVS - 1; i++)
            if (div_list[i] >= v)
                best = i;
    }
    best += (int)d;
    if (best < 0) best = 0;
    if (best > DIVS - 1) best = DIVS - 1;
    return div_list[best];
}
static int steps_of(int div)                /* sequencer steps a move */
{
    return div < 1 ? 1 : div;
}

/* ---- LEN: the Digitone's note lengths, as the TRIG page's LEN index ------
 * Measured on the TRIG page (emulated OS 1.44), in steps: index 14 is 1/16
 * (one step), 30 1/8, 46 1/4, 62 1/2, 78 16 steps, 94 32, 110 64; below each
 * of those the index moves in 1/16, 1/8, 1/4, 1/2, 1 and 2 steps. h: half
 * steps (1-128), the index for that length (or the nearest below). */
static int len_for_halves(int h)
{
    if (h < 1) h = 1;
    if (h > 128) h = 128;
    if (h <= 4)  return 8 * h - 2;          /* to 2 steps: 1/16 of a step an index */
    if (h <= 8)  return 30 + (h - 4) * 4;   /* to 4: 1/8 */
    if (h <= 16) return 46 + (h - 8) * 2;   /* to 8: 1/4 */
    if (h <= 32) return 62 + (h - 16);      /* to 16: 1/2 */
    if (h <= 64) return 78 + (h - 32) / 2;  /* to 32: 1 */
    return 94 + (h - 64) / 4;               /* to 64: 2 */
}

/* ==== defaults and checks ================================================ */

static void snake_defaults(struct snake *s)
{
    static const signed char demo[CELLS] = { 0, 3, 7, 10, 12, 10, 7, 3 };
    int c;
    for (c = 0; c < CELLS; c++) {
        s->note[c] = demo[c];
        s->vel[c] = 100;
    }
    s->gate = 0xff;
    s->set[P_ON] = 0;
    s->set[P_SHAPE] = 1;
    s->set[P_DIR] = D_FWD;
    s->set[P_LEN] = CELLS;
    s->set[P_DIV] = 1;
    s->set[P_SCALE] = 0;
    s->set[P_RANGE] = 12;
    s->set[P_SHIFT] = 0;
    for (c = 0; c < 7; c++)
        s->pad[c] = 0;
}

static void snake_check(struct snake *s)
{
    int c;
    for (c = 0; c < CELLS; c++) {
        if (s->note[c] > NOTE_MAX) s->note[c] = NOTE_MAX;
        else if (s->note[c] < -NOTE_MAX) s->note[c] = -NOTE_MAX;
        if (s->vel[c] < 1) s->vel[c] = 1;
        else if (s->vel[c] > 127) s->vel[c] = 127;
    }
    for (c = 0; c < NSET; c++)
        if (s->set[c] < set_min[c] || s->set[c] > set_max[c])
            s->set[c] = c == P_DIV ? 1 : c == P_RANGE ? 12 : set_min[c];
    if (s->set[P_LEN] < 1)
        s->set[P_LEN] = CELLS;
}

/* A demo line: a rising figure with a held note and a ratchet */
static void stages_defaults(struct stages *g)
{
    static const signed char demo_p[CELLS] = { 0, 7, 12, 10, 3, 5, 7, 0 };
    static const unsigned char demo_l[CELLS] = { 1, 1, 2, 1, 4, 1, 2, 1 };
    static const unsigned char demo_m[CELLS] = { M_ONE, M_ONE, M_ALL, M_ONE, M_HOLD, M_OFF, M_ONE, M_ONE };
    int c;
    for (c = 0; c < CELLS; c++) {
        g->pitch[c] = demo_p[c];
        g->vel[c] = 100;
        g->pulses[c] = demo_l[c];
        g->mode[c] = demo_m[c];
        g->div[c] = 1;
    }
    g->set[S_TYPE] = 0;
    g->set[S_DIR] = D_FWD;
    g->set[S_STAGES] = CELLS;
    g->set[S_CLOCK] = 1;
    g->set[S_SCALE] = 0;
    g->set[S_RANGE] = 12;
    g->set[S_NLEN] = 0;
    g->set[S_SHIFT] = 0;
}

static void stages_check(struct stages *g)
{
    int c;
    for (c = 0; c < CELLS; c++) {
        if (g->pitch[c] > NOTE_MAX) g->pitch[c] = NOTE_MAX;
        else if (g->pitch[c] < -NOTE_MAX) g->pitch[c] = -NOTE_MAX;
        if (g->vel[c] < 1) g->vel[c] = 1;
        else if (g->vel[c] > 127) g->vel[c] = 127;
        if (g->pulses[c] < 1 || g->pulses[c] > 8) g->pulses[c] = 1;
        if (g->mode[c] >= MODES) g->mode[c] = M_ONE;
        if (g->div[c] < 1 || g->div[c] > 4) g->div[c] = 1;
    }
    for (c = 0; c < NSETS; c++)
        if (g->set[c] < sset_min[c] || g->set[c] > sset_max[c])
            g->set[c] = c == S_CLOCK ? 1 : c == S_RANGE ? 12 : c == S_STAGES ? CELLS : sset_min[c];
}

static void track_defaults(struct track *k)
{
    int c;
    k->on = 0;
    k->eng = E_SNAKE;
    k->spd = 0;
    k->pad0 = 0;
    stages_defaults(&k->st);
    for (c = 0; c < 12; c++)
        k->pad[c] = 0;
}

/* ==== state shared by the UI and the fires ================================ */

static unsigned char snk_ready, dsq_ready, data_ready;
static unsigned char dsq_found[TRACKS];
static volatile unsigned char reset_req[TRACKS];
/* Starting from STOP (DIGISNAKE 1.2): the sequencer prepares step 1 while
 * stopped (a fire with the transport at 0): that fire starts the sequence
 * (prep), and the first fire after PLAY is then step 2. Without a prepare
 * (rewind), the first fire after PLAY starts it. */
static volatile unsigned char prep[TRACKS], rewind[TRACKS];

/* DIRECT JUMP's count of pattern starts (a weak import: 0 without it). When it
 * moves, every sequence starts again with the pattern. */
extern volatile uint32_t dj_starts;
static uint32_t dj_seen[TRACKS];

#define SAVE_TICKS 90
static unsigned short save_wait;
static void changed(void)
{
    save_wait = SAVE_TICKS;
}

/* OS 1.44: ask for the power-up copy of the project, as its own change notice
 * does (fw_project's virtual slot 17, its mirror buffer at +32), unless the
 * project has its notices suspended (+36). */
static void mirror_request(void)
{
#ifdef DSQ_MIRROR
    uint8_t *p = fw_project;
    uint32_t buf;
    if (!p)
        return;
    buf = *(volatile uint32_t *)(p + 32);
    if (!buf || *(volatile uint8_t *)(p + 36))
        return;
    ((void (*)(void *, uint32_t, uint32_t))(*(uint32_t **)p)[17])(p, buf, 0);
#endif
}

/* A track's whole state, for TRIG + NO, FUNC + NO and PRINT's undo */
struct whole {
    struct snake s;
    struct track k;
};
static struct whole hold_copy[TRACKS], run_copy[TRACKS];
static unsigned char hold_valid[TRACKS], run_valid[TRACKS], run_active[TRACKS];

/* a byte copy: struct assignments would call memcpy, which a mod has not */
static void copy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
}

static void get_whole(int t, struct whole *w)
{
    copy(&w->s, &digiseq_snake[t], sizeof w->s);
    copy(&w->k, &digiseq_trk[t], sizeof w->k);
}
static void put_whole(int t, const struct whole *w)
{
    copy(&digiseq_snake[t], &w->s, sizeof w->s);
    copy(&digiseq_trk[t], &w->k, sizeof w->k);
    digiseq_snake[t].set[P_ON] = (unsigned char)(w->k.on && w->k.eng == E_SNAKE);
}

static void snk_loaded(int t0, int found)
{
    int t;
    for (t = t0; t < t0 + 4; t++) {
        if (found)
            snake_check(&digiseq_snake[t]);
        else
            snake_defaults(&digiseq_snake[t]);
        reset_req[t] = 1;
        hold_valid[t] = run_valid[t] = run_active[t] = 0;
    }
    snk_ready |= (unsigned char)(t0 ? 2 : 1);
    data_ready = 0;                         /* the tick puts TYPE together */
}
static void dsq_loaded(int t0, int found)
{
    int t;
    for (t = t0; t < t0 + 4; t++) {
        dsq_found[t] = (unsigned char)found;
        if (found) {
            struct track *k = &digiseq_trk[t];
            if (k->eng != E_SNAKE && k->eng != E_185)
                k->eng = E_SNAKE;
            k->on = k->on ? 1 : 0;
            stages_check(&k->st);
        } else {
            track_defaults(&digiseq_trk[t]);
        }
        reset_req[t] = 1;
    }
    dsq_ready |= (unsigned char)(t0 ? 2 : 1);
    data_ready = 0;
}
void digiseq_snk1(int found) { snk_loaded(0, found); }
void digiseq_snk2(int found) { snk_loaded(4, found); }
void digiseq_dsq1(int found) { dsq_loaded(0, found); }
void digiseq_dsq2(int found) { dsq_loaded(4, found); }

const struct core_projdata digiseq_projdata_snk1 = {
    0x534e4b31u, 4 * sizeof(struct snake), &digiseq_snake[0], digiseq_snk1      /* "SNK1" */
};
const struct core_projdata digiseq_projdata_snk2 = {
    0x534e4b32u, 4 * sizeof(struct snake), &digiseq_snake[4], digiseq_snk2      /* "SNK2" */
};
const struct core_projdata digiseq_projdata_dsq1 = {
    0x44535131u, 4 * sizeof(struct track), &digiseq_trk[0], digiseq_dsq1        /* "DSQ1" */
};
const struct core_projdata digiseq_projdata_dsq2 = {
    0x44535132u, 4 * sizeof(struct track), &digiseq_trk[4], digiseq_dsq2        /* "DSQ2" */
};

/* After a load (UI task): TYPE from DSQ, or for a DIGISNAKE project (no DSQ),
 * SNAKE wherever its SNAKE was on. */
static void put_together(void)
{
    int t;
    for (t = 0; t < TRACKS; t++) {
        struct track *k = &digiseq_trk[t];
        if (!(snk_ready & (t < 4 ? 1 : 2)))
            snake_defaults(&digiseq_snake[t]);
        if (!(dsq_ready & (t < 4 ? 1 : 2)) || !dsq_found[t]) {
            if (!(dsq_ready & (t < 4 ? 1 : 2)))
                track_defaults(k);
            k->eng = E_SNAKE;
            k->on = digiseq_snake[t].set[P_ON] ? 1 : 0;
        }
        digiseq_snake[t].set[P_ON] = (unsigned char)(k->on && k->eng == E_SNAKE);
        reset_req[t] = 1;
    }
    data_ready = 1;
}

/* ==== playback (interrupt level, in the sequencer's step fire) ============ */

static unsigned long irq_rand = 0x2545f491UL;
static unsigned long xorshift(unsigned long *s)
{
    unsigned long x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x2545f491UL;
    return *s;
}

static int eng_on(int32_t track, int eng)
{
    return data_ready && (uint32_t)track < TRACKS && digiseq_trk[track].on && digiseq_trk[track].eng == eng;
}

/* A walk over len places, by a direction: SNAKE's path, 185's stages */
struct walk {
    unsigned char k;            /* the place, 0..len-1 */
    signed char pp;             /* ping-pong's way */
};

static void walk_on(struct walk *w, int dir, int len, unsigned long *rnd)
{
    int k = w->k;
    switch (dir) {
    case D_BWD:
        k = k > 0 ? k - 1 : len - 1;
        break;
    case D_FB:
    case D_BF:
        if (len < 2) {
            k = 0;
            break;
        }
        if (w->pp >= 0) {
            if (k + 1 >= len) { w->pp = -1; k = len - 2; } else k++;
        } else {
            if (k <= 0) { w->pp = 1; k = 1; } else k--;
        }
        break;
    case D_RND:
        k = (int)(xorshift(rnd) % (unsigned long)len);
        break;
    default:
        k = k + 1 < len ? k + 1 : 0;
        break;
    }
    if (k >= len)
        k = 0;
    w->k = (unsigned char)k;
}

static void walk_start(struct walk *w, int dir, int len)
{
    if (dir == D_BWD || dir == D_BF) {
        w->k = (unsigned char)(len - 1);
        w->pp = -1;
    } else {
        w->k = 0;
        w->pp = 1;
    }
}

/* ---- SNAKE ---- */
struct play {
    struct walk w;
    unsigned char count;        /* sequencer steps since the last move */
    unsigned char cell;         /* the cell the step plays */
    unsigned char moved;        /* this step is a snake step (DIV: the others rest) */
};
static struct play play[TRACKS];
volatile unsigned char digiseq_cell[TRACKS];        /* for the screen */

static int snake_len(const struct snake *s)
{
    int len = s->set[P_LEN];
    return len < 1 || len > CELLS ? CELLS : len;
}

static int cell_of(const struct snake *s, int k, int len)
{
    int i = (k + s->set[P_SHIFT]) % len;
    return shapes[s->set[P_SHAPE] % SHAPES][i];
}

static void snake_start(struct play *p, const struct snake *s, int len)
{
    p->count = 0;
    p->moved = 1;
    walk_start(&p->w, s->set[P_DIR], len);
}

static void snake_advance(struct play *p, const struct snake *s, int len, unsigned long *rnd)
{
    if (++p->count >= s->set[P_DIV]) {
        p->count = 0;
        p->moved = 1;
        walk_on(&p->w, s->set[P_DIR], len, rnd);
    } else {
        p->moved = 0;
    }
}

/* ---- 185 ---- */
struct play185 {
    struct walk w;              /* the stage, as the k-th of STAGES */
    unsigned char pulse;        /* the pulse within the stage */
    unsigned char count;        /* sequencer steps since the last pulse */
    unsigned char moved;        /* this step is a clock pulse */
    unsigned char stage;        /* the stage the step plays, 0-7 */
    unsigned char fire;         /* the step fires */
    unsigned char hold;         /* a HOLD's first pulse: its length in steps, else 0 */
};
static struct play185 p185[TRACKS];
volatile unsigned char digiseq_stage[TRACKS], digiseq_pulse[TRACKS];

static int stages_n(const struct stages *g)
{
    int n = g->set[S_STAGES];
    return n < 1 || n > CELLS ? CELLS : n;
}

static int stage_of(const struct stages *g, int k)
{
    return (k + g->set[S_SHIFT]) % CELLS;
}

static void st_decide(struct play185 *p, const struct stages *g)
{
    int s = stage_of(g, p->w.k), m = g->mode[s], d = g->div[s];
    p->stage = (unsigned char)s;
    p->hold = 0;
    if (!p->moved || m == M_OFF)
        p->fire = 0;
    else if (m == M_ALL)
        p->fire = (unsigned char)(p->pulse % (d < 1 ? 1 : d) == 0);
    else
        p->fire = (unsigned char)(p->pulse == 0);
    if (p->fire && m == M_HOLD)
        p->hold = (unsigned char)(g->pulses[s] * steps_of(g->set[S_CLOCK]));
}

static void st_start(struct play185 *p, const struct stages *g)
{
    p->count = 0;
    p->pulse = 0;
    p->moved = 1;
    walk_start(&p->w, g->set[S_DIR], stages_n(g));
    st_decide(p, g);
}

static void st_advance(struct play185 *p, const struct stages *g, unsigned long *rnd)
{
    int n = stages_n(g);
    if (++p->count >= steps_of(g->set[S_CLOCK])) {
        p->count = 0;
        p->moved = 1;
        if (p->w.k >= n)
            p->w.k = 0;
        if (++p->pulse >= g->pulses[stage_of(g, p->w.k)]) {
            p->pulse = 0;
            walk_on(&p->w, g->set[S_DIR], n, rnd);
        }
    } else {
        p->moved = 0;
    }
    if (p->w.k >= n)
        p->w.k = 0;
    st_decide(p, g);
}

/* ---- the step clock: start, catch up, advance (both types) ---- */
static void eng_start(int t)
{
    if (digiseq_trk[t].eng == E_185)
        st_start(&p185[t], &digiseq_trk[t].st);
    else
        snake_start(&play[t], &digiseq_snake[t], snake_len(&digiseq_snake[t]));
}

static void eng_advance(int t, unsigned long *rnd)
{
    if (digiseq_trk[t].eng == E_185)
        st_advance(&p185[t], &digiseq_trk[t].st, rnd);
    else
        snake_advance(&play[t], &digiseq_snake[t], snake_len(&digiseq_snake[t]), rnd);
}

/* Both reads of a step (first 1, then 0) must answer the same: the first
 * moves the sequence, the second reuses where it is. */
void digiseq_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    int fire;
    if (!data_ready || (uint32_t)track >= TRACKS || !digiseq_trk[track].on)
        return;
    if (first) {
        if (fw_transport == 0) {            /* stopped: prepare the first step */
            prep[track] = 1;
            rewind[track] = reset_req[track] = 0;
            dj_seen[track] = dj_starts;
            eng_start(track);
        } else if (prep[track] || rewind[track]) {
            int n;
            prep[track] = rewind[track] = reset_req[track] = 0;
            dj_seen[track] = dj_starts;
            eng_start(track);
            for (n = 0; n < step && n < SQ_STEPS; n++)
                eng_advance(track, &irq_rand);
        } else if (reset_req[track] || dj_starts != dj_seen[track]) {
            reset_req[track] = 0;
            dj_seen[track] = dj_starts;
            eng_start(track);
        } else {
            eng_advance(track, &irq_rand);
        }
        if (digiseq_trk[track].eng == E_185) {
            digiseq_stage[track] = p185[track].stage;
            digiseq_pulse[track] = p185[track].pulse;
        } else {
            struct play *p = &play[track];
            const struct snake *s = &digiseq_snake[track];
            int len = snake_len(s);
            if (p->w.k >= len)
                p->w.k = 0;
            p->cell = (unsigned char)cell_of(s, p->w.k, len);
            digiseq_cell[track] = p->cell;
        }
    }
    if (digiseq_trk[track].eng == E_185) {
        fire = p185[track].fire;
    } else {
        const struct play *p = &play[track];
        fire = p->moved && ((digiseq_snake[track].gate >> p->cell) & 1);
    }
    if (fire)
        *trig |= SQ_TRIG;
    else
        *trig &= ~SQ_TRIG;
}

static int quantise(int note, int root, int scale)
{
    unsigned short m;
    int i, deg;
    if (scale <= 0 || scale >= SCALES)
        return note;
    m = scale_mask[scale];
    for (i = 0; i < 12; i++) {              /* down to the nearest scale tone */
        deg = (note - root - i) % 12;
        if (deg < 0)
            deg += 12;
        if ((m >> deg) & 1)
            return note - i;
    }
    return note;
}

static int clamp_note(int n)
{
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

void digiseq_notes(int32_t track, int32_t step, int32_t generated, int8_t notes[SQ_NOTES])
{
    struct sq_track info;
    int base, root = 0, n, i;
    (void)step;
    (void)generated;
    if (!data_ready || (uint32_t)track >= TRACKS || !digiseq_trk[track].on)
        return;
    if (sq_track_info(track, &info, sizeof info) == 0) {
        base = info.note;
        if (info.root != SQ_ROOT_NONE)
            root = info.root % 12;
    } else {
        base = notes[0] >= 0 ? notes[0] : 60;
    }
    if (digiseq_trk[track].eng == E_185) {
        const struct stages *g = &digiseq_trk[track].st;
        n = quantise(base + g->pitch[p185[track].stage], root, g->set[S_SCALE]);
    } else {
        const struct snake *s = &digiseq_snake[track];
        n = quantise(base + s->note[play[track].cell], root, s->set[P_SCALE]);
    }
    notes[0] = (int8_t)clamp_note(n);
    for (i = 1; i < SQ_NOTES; i++)          /* one note, not the step's chord */
        notes[i] = 0;
}

void digiseq_vel(int32_t track, int32_t step, int32_t *vel)
{
    (void)step;
    if (eng_on(track, E_185))
        *vel = digiseq_trk[track].st.vel[p185[track].stage];
    else if (eng_on(track, E_SNAKE))
        *vel = digiseq_snake[track].vel[play[track].cell];
}

/* 185: a HOLD's note lasts its stage; LEN (SETUP) sets the others' (TRK:
 * the track's LEN, so the steps' own LEN locks from before do not show) */
#define TR_LEN_OFF_TRACK 0x3c2
static const unsigned char nlen_halves[NLENS] = { 0, 1, 2, 4, 8 };
void digiseq_len(int32_t track, int32_t step, int32_t *len)
{
    const struct play185 *p;
    const struct stages *g;
    (void)step;
    if (!eng_on(track, E_185))
        return;
    p = &p185[track];
    g = &digiseq_trk[track].st;
    if (p->hold)
        *len = len_for_halves(2 * p->hold);
    else if (g->set[S_NLEN] > 0 && g->set[S_NLEN] < NLENS)
        *len = len_for_halves(nlen_halves[g->set[S_NLEN]]);
    else if (fw_pattern)                    /* TRK: the track's LEN, not a step's own */
        *len = (signed char)FW_TRACK(fw_pattern, track)[TR_LEN_OFF_TRACK];
}

/* ==== 2X: the track's own speed =========================================
 * The pattern (976 bytes a track) keeps a speed per track at +0x3c7, used
 * when its SCALE mode is per track (pattern +0x16111 == 1): an index into the
 * stock speeds (2X, 3/2X, 1X, 3/4X, 1/2X, 1/4X, 1/8X). 2X halves the track's
 * step; switching it or the type off, or the pattern changing, puts it back.
 * The speed it had + 1 is kept with the type's data (SNAKE: pad[0]; 185:
 * spd), so a power cycle with 2X on still knows what to put back. */
#define PAT_MODE   0x16111
#define TR_SPEED   0x3c7
static uint8_t *mod_pat[TRACKS];
static signed char mod_orig[TRACKS];
static unsigned char mod_eng[TRACKS];

static int per_track_mode(void)
{
    uint8_t *pat = fw_pattern;
    return pat && pat[PAT_MODE] == 1;
}

static int speed_half(int i)
{
    static const signed char h[7] = { 0, 1, 0, 1, 2, 4, 5 };
    return i >= 0 && i < 7 ? h[i] : i;
}

static unsigned char *spd_keep(int t, int eng)
{
    return eng == E_185 ? &digiseq_trk[t].spd : &digiseq_snake[t].pad[0];
}

static void speed_sync(void)
{
    uint8_t *pat = fw_pattern;
    int t, pt = pat && pat[PAT_MODE] == 1;
    for (t = 0; t < TRACKS; t++) {
        const struct track *k = &digiseq_trk[t];
        int eng = k->eng;
        int want = pt && k->on &&
                   (eng == E_185 ? k->st.set[S_CLOCK] == 0 : digiseq_snake[t].set[P_DIV] == 0);
        if (mod_pat[t] && (mod_pat[t] != pat || !want || mod_eng[t] != eng)) {     /* put it back */
            uint8_t *tr = FW_TRACK(mod_pat[t], t);
            if ((signed char)tr[TR_SPEED] == speed_half(mod_orig[t]))
                tr[TR_SPEED] = (uint8_t)mod_orig[t];
            mod_pat[t] = 0;
            *spd_keep(t, mod_eng[t]) = 0;
            changed();
        }
        if (want && !mod_pat[t]) {
            uint8_t *tr = FW_TRACK(pat, t);
            unsigned char *keep = spd_keep(t, eng);
            int o = (signed char)tr[TR_SPEED], h;
            if (*keep && o == speed_half(*keep - 1)) {          /* ours, from before */
                mod_pat[t] = pat;
                mod_orig[t] = (signed char)(*keep - 1);
                mod_eng[t] = (unsigned char)eng;
            } else if ((h = speed_half(o)) != o) {
                mod_orig[t] = (signed char)o;
                tr[TR_SPEED] = (uint8_t)h;
                mod_pat[t] = pat;
                mod_eng[t] = (unsigned char)eng;
                *keep = (unsigned char)(o + 1);
                changed();
            }
        }
    }
}

/* ==== PRINT (TRIG + FUNC + REC) ==========================================
 * Plays the track's type through the track's length, as it would from the
 * start (or, playing, from where it is), and writes each step as a stock
 * trig: the trig bit (lock trigs kept), its note (one; no chord), its
 * velocity and its LEN (185's held and LEN-set notes; the others the
 * track's), then sets TYPE to OFF. A 2X speed stays. */
#define TR_VEL_OFF   0x080
#define TR_LEN_OFF   0x0c0
#define TR_NOTES_OFF 0x1c0
static uint8_t print_bak[976];
static signed char print_t = -1;

static int print_track(int t)
{
    uint8_t *pat = fw_pattern, *trk;
    struct track *k = &digiseq_trk[t];
    struct sq_track info;
    unsigned long rnd = 0x9e3779b9UL ^ fw_timeline;
    int len, base, root = 0, step, i, first, playing, pending;
    uint32_t last;
    struct play qs;
    struct play185 q8;
    const struct snake *s = &digiseq_snake[t];
    const struct stages *g = &k->st;
    int slen = snake_len(s);
    if (!pat || sq_track_info(t, &info, sizeof info) != 0)
        return -1;
    len = info.len;
    if (len < 1 || len > 64)
        return -1;
    base = info.note;
    if (info.root != SQ_ROOT_NONE)
        root = info.root % 12;
    trk = FW_TRACK(pat, t);
    copy(print_bak, trk, 976);
    copy(&qs, &play[t], sizeof qs);
    copy(&q8, &p185[t], sizeof q8);
    print_t = (signed char)t;
    last = sq_clock[t].last;
    playing = fw_transport == 1 && k->on && last < (uint32_t)len;
    pending = !playing || reset_req[t] || prep[t] || rewind[t] || dj_starts != dj_seen[t];
    first = playing ? ((int)last + 1) % len : 0;
    for (i = 0; i < len; i++) {
        int fire, n, vel, l = -1;
        uint16_t w;
        step = (first + i) % len;
        if (k->eng == E_185) {
            if (i == 0 && pending)
                st_start(&q8, g);
            else
                st_advance(&q8, g, &rnd);
            fire = q8.fire;
            n = quantise(base + g->pitch[q8.stage], root, g->set[S_SCALE]);
            vel = g->vel[q8.stage];
            if (q8.hold)
                l = len_for_halves(2 * q8.hold);
            else if (g->set[S_NLEN] > 0 && g->set[S_NLEN] < NLENS)
                l = len_for_halves(nlen_halves[g->set[S_NLEN]]);
        } else {
            int cell;
            if (i == 0 && pending)
                snake_start(&qs, s, slen);
            else
                snake_advance(&qs, s, slen, &rnd);
            if (qs.w.k >= slen)
                qs.w.k = 0;
            cell = cell_of(s, qs.w.k, slen);
            fire = qs.moved && ((s->gate >> cell) & 1);
            n = quantise(base + s->note[cell], root, s->set[P_SCALE]);
            vel = s->vel[cell];
        }
        w = FW_STEP(trk, step);
        if (fire)
            w |= FW_STEP_TRIG;
        else
            w &= (uint16_t)~FW_STEP_TRIG;
        FW_STEP(trk, step) = w;
        trk[TR_NOTES_OFF + 8 * step] = (uint8_t)clamp_note(n);
        for (n = 1; n < 8; n++)
            trk[TR_NOTES_OFF + 8 * step + n] = 0;
        trk[TR_VEL_OFF + step] = (uint8_t)vel;
        if (k->eng == E_185)
            trk[TR_LEN_OFF + step] = (uint8_t)l;    /* -1 (255): the track's LEN */
    }
    if (mod_pat[t]) {                       /* keep the 2X the print was made at */
        mod_pat[t] = 0;
        *spd_keep(t, mod_eng[t]) = 0;
    }
    k->on = 0;
    digiseq_snake[t].set[P_ON] = 0;
    sq_rearm(1u << t);
    return len;
}

static void unprint(int t)
{
    uint8_t *pat = fw_pattern, *trk;
    int i;
    if (print_t != t || !pat)
        return;
    trk = FW_TRACK(pat, t);
    for (i = 0; i < 976; i++)
        trk[i] = print_bak[i];
    print_t = -1;
    sq_rearm(1u << t);
}

/* ==== the page (core-dn1's mod pages): TRIG's next ======================== */
#ifndef TRIG_PAGE
#error "build with -DTRIG_PAGE=<the TRIG page's index> (mod.json cflags)"
#endif
#ifndef TRIG_KIND
#define TRIG_KIND 9
#endif
struct core_page digiseq_page = {
    28, TRIG_PAGE, "SEQ", "DigiSeq",
    { 0, 0, 0, 0, 0, 0, 0, 0 }, TRIG_KIND,
    0, 0
};
#ifndef MIDI_TRIG_PAGE
#define MIDI_TRIG_PAGE 19
#endif
#ifndef MIDI_TRIG_KIND
#define MIDI_TRIG_KIND 0
#endif
struct core_page digiseq_page_midi = {
    29, MIDI_TRIG_PAGE, "SEQ", "DigiSeq",
    { 0, 0, 0, 0, 0, 0, 0, 0 }, MIDI_TRIG_KIND,
    0, 0
};

#define KEY_FUNC  1
#define KEY_REC   10
#define KEY_PLAY  11
#define KEY_STOP  12
#define KEY_YES   13
#define KEY_NO    14
#define KEY_UP    15
#define KEY_DOWN  16
#define KEY_LEFT  17
#define KEY_RIGHT 18
#define KEY_TRIG  20
#define KEY_LFO   25
#define KEY_STEP1 26
#define EV_KEY(ev)   (*(unsigned long *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(unsigned long *)((char *)(ev) + 16))
#define EV_ENC(ev)   (*(unsigned long *)((char *)(ev) + 12))
#define EV_DELTA(ev) (*(long *)((char *)(ev) + 16))
#define F_DOWN    1
#define F_FUNC    2
#define F_REPEAT  8
#define F_UP      0x10

/* pages: SNAKE's and 185's, numbered within the type */
enum { PG_NOTES, PG_GATES, PG_VEL, PG_PATH, SNAKE_PAGES };
enum { PG8_PITCH, PG8_VEL, PG8_PULSES, PG8_MODE, PG8_DIV, PG8_SETUP, PAGES_185 };
static const char *const snake_pages[SNAKE_PAGES] = { "NOTES", "GATES", "VELOCITY", "PATH" };
static const char *const pages185[PAGES_185] = { "PITCH", "VELOCITY", "PULSES", "MODE", "DIV", "SETUP" };
static const char *const snake_what[SNAKE_PAGES] = { "Notes", "Gates", "Velocity", "Path" };
static const char *const what185[PAGES_185] = { "Pitch", "Velocity", "Pulses", "Modes", "Divisions", "Setup" };

unsigned char digiseq_pg_snake, digiseq_pg_185;     /* the page shown, per type */
void *digiseq_brain;
static unsigned char trig_held, trig_used, in_open;
static unsigned char stock_held, stock_used;
static unsigned char func_held;
static unsigned char redraw;
static long enc_acc[8];
#define COUNTS    4
static unsigned long ui_rand = 0x9e3779b9UL;

static int ui_track(void)
{
    long t = fw_active_track;
    return t >= 0 && t < TRACKS ? (int)t : -1;
}

static int eng_of(int t)
{
    return digiseq_trk[t].eng == E_185 ? E_185 : E_SNAKE;
}

static unsigned char *pg_of(int eng)
{
    return eng == E_185 ? &digiseq_pg_185 : &digiseq_pg_snake;
}

static int pages_of(int eng)
{
    return eng == E_185 ? PAGES_185 : SNAKE_PAGES;
}

static int last_page(int eng)
{
    return pages_of(eng) - 1;
}

static struct core_page *page_for(long t)
{
    return t >= 4 ? &digiseq_page_midi : &digiseq_page;
}

static int page_up(void *brain, struct core_page *pg)
{
    return pg->view && core_page_shown(brain, pg);
}

static int shown(void *brain)
{
    return brain && (page_up(brain, &digiseq_page) || page_up(brain, &digiseq_page_midi));
}

static struct core_page *stock_trig_up(void *brain)
{
    static struct core_page probe;
    struct core_page *pg = page_for(fw_active_track);
    if (!brain || !pg->view || pg->pos < 1)
        return 0;
    probe.view = pg->view;
    probe.pos = pg->pos - 1;
    return core_page_shown(brain, &probe) ? pg : 0;
}

static int clampi(long v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : (int)v;
}

/* TYPE: 0 OFF, 1 SNAKE, 2 185 */
static int type_of(int t)
{
    return digiseq_trk[t].on ? eng_of(t) : 0;
}

static void set_type(int t, int ty)
{
    struct track *k = &digiseq_trk[t];
    if (ty == 0) {
        k->on = 0;
    } else {
        if (k->eng != ty)                   /* stay on TYPE: the new type's last page */
            *pg_of(ty) = (unsigned char)last_page(ty);
        k->eng = (unsigned char)ty;
        k->on = 1;
    }
    digiseq_snake[t].set[P_ON] = (unsigned char)(k->on && k->eng == E_SNAKE);
    reset_req[t] = 1;
}

/* ---- SNAKE's page edits ---- */
static void snake_randomise(struct snake *s, int pg)
{
    int c, r = s->set[P_RANGE];
    if (r < 1 || r > NOTE_MAX)
        r = 12;
    for (c = 0; c < CELLS; c++) {
        unsigned long x = xorshift(&ui_rand);
        if (pg == PG_NOTES)
            s->note[c] = (signed char)((long)(x % (unsigned long)(2 * r + 1)) - r);
        else if (pg == PG_VEL)
            s->vel[c] = (unsigned char)(40 + x % 88);
        else if (pg == PG_GATES) {
            if ((x >> 16) % 100 < 65)
                s->gate |= (unsigned char)(1u << c);
            else
                s->gate &= (unsigned char)~(1u << c);
        }
    }
    if (pg == PG_GATES && !s->gate)
        s->gate = (unsigned char)(1u << (xorshift(&ui_rand) % CELLS));
    if (pg == PG_PATH) {                    /* everything but TYPE and SCALE */
        static const unsigned char divs[8] = { 1, 1, 1, 1, 1, 2, 2, 4 };
        s->set[P_SHAPE] = (unsigned char)(xorshift(&ui_rand) % SHAPES);
        s->set[P_DIR] = (unsigned char)(xorshift(&ui_rand) % DIRS);
        s->set[P_LEN] = (unsigned char)(3 + xorshift(&ui_rand) % (CELLS - 2));
        s->set[P_DIV] = divs[xorshift(&ui_rand) & 7];
        s->set[P_RANGE] = (unsigned char)(5 + xorshift(&ui_rand) % (NOTE_MAX - 4));
        s->set[P_SHIFT] = (unsigned char)(xorshift(&ui_rand) % CELLS);
    }
}

static void snake_clear(struct snake *s, int pg)
{
    struct snake d;
    int c;
    snake_defaults(&d);
    for (c = 0; c < CELLS; c++) {
        if (pg == PG_NOTES)
            s->note[c] = 0;
        else if (pg == PG_VEL)
            s->vel[c] = d.vel[c];
    }
    if (pg == PG_GATES)
        s->gate = 0xff;
    if (pg == PG_PATH) {
        unsigned char on = s->set[P_ON];
        for (c = 0; c < NSET; c++)
            s->set[c] = d.set[c];
        s->set[P_ON] = on;
    }
}

static void snake_nudge(struct snake *s, int pg, int d, int func)
{
    int c;
    if (pg == PG_NOTES) {
        for (c = 0; c < CELLS; c++)
            s->note[c] = (signed char)clampi(s->note[c] + d * (func ? 12 : 1), -NOTE_MAX, NOTE_MAX);
    } else if (pg == PG_VEL) {
        for (c = 0; c < CELLS; c++)
            s->vel[c] = (unsigned char)clampi(s->vel[c] + d * 8, 1, 127);
    } else if (pg == PG_GATES) {
        unsigned g = s->gate;
        g = d > 0 ? ((g << 1) | (g >> 7)) : ((g >> 1) | (g << 7));
        s->gate = (unsigned char)g;
    }
}

static void snake_paste(struct snake *s, const struct snake *from, int pg)
{
    int c;
    if (pg == PG_NOTES)
        for (c = 0; c < CELLS; c++) s->note[c] = from->note[c];
    else if (pg == PG_VEL)
        for (c = 0; c < CELLS; c++) s->vel[c] = from->vel[c];
    else if (pg == PG_GATES)
        s->gate = from->gate;
    else {
        unsigned char on = s->set[P_ON];
        for (c = 0; c < NSET; c++) s->set[c] = from->set[c];
        s->set[P_ON] = on;
    }
}

/* ---- 185's page edits ---- */
static void st_randomise(struct stages *g, int pg)
{
    int c, r = g->set[S_RANGE];
    if (r < 1 || r > NOTE_MAX)
        r = 12;
    for (c = 0; c < CELLS; c++) {
        unsigned long x = xorshift(&ui_rand);
        switch (pg) {
        case PG8_PITCH:
            g->pitch[c] = (signed char)((long)(x % (unsigned long)(2 * r + 1)) - r);
            break;
        case PG8_VEL:
            g->vel[c] = (unsigned char)(40 + x % 88);
            break;
        case PG8_PULSES: {                  /* mostly short stages */
            static const unsigned char l[16] = { 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 4, 6, 8, 2 };
            g->pulses[c] = l[(x >> 8) & 15];
            break;
        }
        case PG8_MODE: {                    /* ONE most, then HOLD, ALL, OFF */
            unsigned long v = (x >> 12) % 100;
            g->mode[c] = (unsigned char)(v < 45 ? M_ONE : v < 70 ? M_HOLD : v < 85 ? M_ALL : M_OFF);
            break;
        }
        case PG8_DIV: {
            static const unsigned char dv[8] = { 1, 1, 1, 1, 2, 2, 3, 4 };
            g->div[c] = dv[(x >> 4) & 7];
            break;
        }
        default:
            break;
        }
    }
    if (pg == PG8_MODE) {                   /* never all rests */
        for (c = 0; c < CELLS && g->mode[c] == M_OFF; c++)
            ;
        if (c == CELLS)
            g->mode[xorshift(&ui_rand) % CELLS] = M_ONE;
    }
    if (pg == PG8_SETUP) {                  /* DIR, STAGES, RANGE, SHIFT */
        g->set[S_DIR] = (unsigned char)(xorshift(&ui_rand) % DIRS);
        g->set[S_STAGES] = (unsigned char)(3 + xorshift(&ui_rand) % (CELLS - 2));
        g->set[S_RANGE] = (unsigned char)(5 + xorshift(&ui_rand) % (NOTE_MAX - 4));
        g->set[S_SHIFT] = (unsigned char)(xorshift(&ui_rand) % CELLS);
    }
}

static void st_clear(struct stages *g, int pg)
{
    struct stages d;
    int c;
    stages_defaults(&d);
    for (c = 0; c < CELLS; c++) {
        switch (pg) {
        case PG8_PITCH:  g->pitch[c] = 0; break;
        case PG8_VEL:    g->vel[c] = 100; break;
        case PG8_PULSES: g->pulses[c] = 1; break;
        case PG8_MODE:   g->mode[c] = M_ONE; break;
        case PG8_DIV:    g->div[c] = 1; break;
        default: break;
        }
    }
    if (pg == PG8_SETUP)
        for (c = 0; c < NSETS; c++)
            g->set[c] = d.set[c];
}

static void st_nudge(struct stages *g, int pg, int d, int func)
{
    int c;
    unsigned char tmp;
    switch (pg) {
    case PG8_PITCH:
        for (c = 0; c < CELLS; c++)
            g->pitch[c] = (signed char)clampi(g->pitch[c] + d * (func ? 12 : 1), -NOTE_MAX, NOTE_MAX);
        break;
    case PG8_VEL:
        for (c = 0; c < CELLS; c++)
            g->vel[c] = (unsigned char)clampi(g->vel[c] + d * 8, 1, 127);
        break;
    case PG8_PULSES:
        for (c = 0; c < CELLS; c++)
            g->pulses[c] = (unsigned char)clampi(g->pulses[c] + d, 1, 8);
        break;
    case PG8_DIV:
        for (c = 0; c < CELLS; c++)
            g->div[c] = (unsigned char)clampi(g->div[c] + d, 1, 4);
        break;
    case PG8_MODE:                          /* rotate the stages' modes */
        if (d > 0) {
            tmp = g->mode[CELLS - 1];
            for (c = CELLS - 1; c > 0; c--) g->mode[c] = g->mode[c - 1];
            g->mode[0] = tmp;
        } else {
            tmp = g->mode[0];
            for (c = 0; c < CELLS - 1; c++) g->mode[c] = g->mode[c + 1];
            g->mode[CELLS - 1] = tmp;
        }
        break;
    default:
        break;
    }
}

static void st_paste(struct stages *g, const struct stages *from, int pg)
{
    int c;
    for (c = 0; c < CELLS; c++) {
        switch (pg) {
        case PG8_PITCH:  g->pitch[c] = from->pitch[c]; break;
        case PG8_VEL:    g->vel[c] = from->vel[c]; break;
        case PG8_PULSES: g->pulses[c] = from->pulses[c]; break;
        case PG8_MODE:   g->mode[c] = from->mode[c]; break;
        case PG8_DIV:    g->div[c] = from->div[c]; break;
        default: break;
        }
    }
    if (pg == PG8_SETUP)
        for (c = 0; c < NSETS; c++)
            g->set[c] = from->set[c];
}

/* ---- shared page actions ---- */
static void page_randomise(int t, int pg)
{
    ui_rand ^= fw_timeline;
    if (eng_of(t) == E_185)
        st_randomise(&digiseq_trk[t].st, pg);
    else
        snake_randomise(&digiseq_snake[t], pg);
}

static void randomise_all(int t)
{
    ui_rand ^= fw_timeline;
    if (eng_of(t) == E_185) {
        st_randomise(&digiseq_trk[t].st, PG8_PITCH);
        st_randomise(&digiseq_trk[t].st, PG8_VEL);
        st_randomise(&digiseq_trk[t].st, PG8_PULSES);
        st_randomise(&digiseq_trk[t].st, PG8_MODE);
    } else {
        snake_randomise(&digiseq_snake[t], PG_NOTES);
        snake_randomise(&digiseq_snake[t], PG_GATES);
        snake_randomise(&digiseq_snake[t], PG_VEL);
    }
}

static const char *what_of(int eng, int pg)
{
    return eng == E_185 ? what185[pg % PAGES_185] : snake_what[pg % SNAKE_PAGES];
}

/* COPY / PASTE: one page, from any track to any other of the same type */
static struct whole clip;
static signed char clip_pg = -1;
static unsigned char clip_eng;

static void edited(int t)
{
    run_active[t] = 0;
    changed();
}

static void reload(int t, const struct whole *from, int valid)
{
    if (!valid)
        return;
    put_whole(t, from);
    reset_req[t] = 1;
    changed();
    POPUP("Sequence reloaded");
}

static int next_stop(int v, long d, const signed char *stops, int n)
{
    int i;
    if (d > 0) {
        for (i = 0; i < n; i++)
            if (stops[i] > v)
                return stops[i];
        return stops[n - 1];
    }
    for (i = n - 1; i >= 0; i--)
        if (stops[i] < v)
            return stops[i];
    return stops[0];
}
static const signed char note_stops[5] = { -24, -12, 0, 12, 24 };
static const signed char vel_stops[5] = { 1, 32, 64, 96, 127 };
static const signed char range_stops[3] = { 1, 12, 24 };

static void next_trig_page(void *brain, void *ev)
{
    static struct core_page to;
    struct core_page *pg = page_up(brain, &digiseq_page_midi) ? &digiseq_page_midi
                         : page_up(brain, &digiseq_page) ? &digiseq_page
                         : page_for(fw_active_track);
    unsigned char *view = pg->view;
    long n;
    if (!view)
        return;
    to.idx = pg->idx;
    to.view = view;
    n = (*(long *)(view + 128) - *(long *)(view + 124)) / 4;
    if (n < 2)
        return;
    to.pos = (pg->pos + 1) % n;
    in_open = 1;
    core_page_open(brain, ev, KEY_TRIG, &to);
    in_open = 0;
}

int digiseq_key(void *brain, void *ev)
{
    unsigned long key = EV_KEY(ev), flags = EV_FLAGS(ev);
    int t, eng, pg, down = (flags & F_DOWN) && !(flags & F_REPEAT);
    unsigned char *pgp;
    digiseq_brain = brain;
    if (key == KEY_FUNC) {
        if (!(flags & F_REPEAT))
            func_held = (unsigned char)(flags & F_DOWN);
    } else if (flags & F_FUNC)
        func_held = 1;
    if (!in_open && stock_held) {
        if (key == KEY_TRIG) {
            if ((flags & F_UP) || !(flags & F_DOWN)) {
                struct core_page *p = page_for(fw_active_track);
                stock_held = 0;
                if (!stock_used && p->view) {
                    in_open = 1;
                    core_page_open(brain, ev, KEY_TRIG, p);
                    in_open = 0;
                    redraw = 1;
                }
            }
            return 1;
        }
        if (flags & F_DOWN)
            stock_used = 1;
    } else if (!in_open && key == KEY_TRIG && down && !trig_held && stock_trig_up(brain)) {
        stock_held = 1;
        stock_used = 0;
        return 1;
    }
    if (in_open || (!trig_held && !shown(brain)))
        return 0;
    t = ui_track();
    if (t < 0)
        return 0;
    eng = eng_of(t);
    pgp = pg_of(eng);
    if (*pgp >= pages_of(eng))
        *pgp = 0;
    pg = *pgp;
    redraw = 1;

    if (key == KEY_TRIG) {
        if ((flags & F_UP) || !(flags & F_DOWN)) {
            if (trig_held && !trig_used) {
                trig_held = 0;
                next_trig_page(brain, ev);
            }
            trig_held = 0;
            return 1;
        }
        if (down) {
            trig_held = 1;
            trig_used = 0;
            print_t = -1;
            get_whole(t, &hold_copy[t]);
            hold_valid[t] = 1;
        }
        return 1;
    }
    if (key >= KEY_TRIG && key <= KEY_LFO) {
        trig_held = 0;
        return 0;
    }
    if (!down)
        return key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN;

    if (key == KEY_YES && (flags & F_FUNC)) {
        trig_used = 1;
        if (!run_active[t]) {
            get_whole(t, &run_copy[t]);
            run_valid[t] = run_active[t] = 1;
        }
        randomise_all(t);
        changed();
        POPUP("%s randomised", eng == E_185 ? "Stages" : "Snake");
        return 1;
    }
    if (key == KEY_NO && trig_held) {
        trig_used = 1;
        unprint(t);
        reload(t, &hold_copy[t], hold_valid[t]);
        run_active[t] = 0;
        return 1;
    }
    if (key == KEY_NO && (flags & F_FUNC)) {
        trig_used = 1;
        reload(t, &run_copy[t], run_valid[t]);
        run_active[t] = 0;
        return 1;
    }
    if ((key == KEY_UP || key == KEY_DOWN) && trig_held) {     /* TYPE */
        int ty = (type_of(t) + (key == KEY_UP ? 1 : 2)) % 3;
        trig_used = 1;
        set_type(t, ty);
        edited(t);
        POPUP("Type=%s", type_names[ty]);
        return 1;
    }
    if (key == KEY_YES && trig_held) {
        trig_used = 1;
        page_randomise(t, pg);
        if (pg == last_page(eng))
            reset_req[t] = 1;
        edited(t);
        POPUP("%s randomised", what_of(eng, pg));
        return 1;
    }
    if (key == KEY_REC && trig_held && ((flags & F_FUNC) || func_held)) {    /* PRINT */
        int n;
        trig_used = 1;
        n = print_track(t);
        if (n < 0) {
            POPUP("No pattern to print to");
        } else {
            reset_req[t] = 1;
            edited(t);
            POPUP("Printed %d steps", n);
        }
        return 1;
    }
    if (key == KEY_REC && trig_held) {                                       /* COPY */
        trig_used = 1;
        get_whole(t, &clip);
        clip_pg = (signed char)pg;
        clip_eng = (unsigned char)eng;
        POPUP("%s copied", what_of(eng, pg));
        return 1;
    }
    if (key == KEY_STOP && trig_held) {                                      /* PASTE */
        trig_used = 1;
        if (clip_pg < 0) {
            POPUP("Nothing to paste");
            return 1;
        }
        if (clip_eng != eng) {
            POPUP("The copy is %s's", type_names[clip_eng]);
            return 1;
        }
        *pgp = (unsigned char)clip_pg;
        if (eng == E_185)
            st_paste(&digiseq_trk[t].st, &clip.k.st, clip_pg);
        else
            snake_paste(&digiseq_snake[t], &clip.s, clip_pg);
        reset_req[t] = 1;
        edited(t);
        POPUP("%s pasted", what_of(eng, clip_pg));
        return 1;
    }
    if (key == KEY_PLAY && trig_held) {                                      /* CLEAR */
        trig_used = 1;
        if (eng == E_185)
            st_clear(&digiseq_trk[t].st, pg);
        else
            snake_clear(&digiseq_snake[t], pg);
        reset_req[t] = 1;
        edited(t);
        POPUP("%s cleared", what_of(eng, pg));
        return 1;
    }
    if (key == KEY_LEFT) {
        *pgp = (unsigned char)(pg ? pg - 1 : pages_of(eng) - 1);
        return 1;
    }
    if (key == KEY_RIGHT) {
        *pgp = (unsigned char)((pg + 1) % pages_of(eng));
        return 1;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        if (eng == E_185)
            st_nudge(&digiseq_trk[t].st, pg, key == KEY_UP ? 1 : -1, flags & F_FUNC);
        else
            snake_nudge(&digiseq_snake[t], pg, key == KEY_UP ? 1 : -1, flags & F_FUNC);
        edited(t);
        return 1;
    }
    if (key >= KEY_STEP1 && key < KEY_STEP1 + CELLS) {
        int c = (int)(key - KEY_STEP1);
        if (eng == E_SNAKE && pg == PG_GATES) {
            digiseq_snake[t].gate ^= (unsigned char)(1u << c);
            edited(t);
            return 1;
        }
        if (eng == E_185 && pg == PG8_MODE) {
            struct stages *g = &digiseq_trk[t].st;
            g->mode[c] = (unsigned char)((g->mode[c] + 1) % MODES);
            edited(t);
            POPUP("Stage %d=%s", c + 1, mode_names[g->mode[c]]);
            return 1;
        }
    }
    return 0;
}

static void snake_enc(int t, struct snake *s, int pg, int c, long d)
{
    switch (pg) {
    case PG_NOTES:
        if (func_held) {
            s->note[c] = (signed char)next_stop(s->note[c], d, note_stops, 5);
            POPUP("Note %c=%c%d", 'A' + c, s->note[c] < 0 ? '-' : '+',
                  s->note[c] < 0 ? -s->note[c] : s->note[c]);
        } else {
            s->note[c] = (signed char)clampi(s->note[c] + d, -NOTE_MAX, NOTE_MAX);
        }
        break;
    case PG_GATES:
        if (d > 0)
            s->gate |= (unsigned char)(1u << c);
        else
            s->gate &= (unsigned char)~(1u << c);
        break;
    case PG_VEL:
        if (func_held) {
            s->vel[c] = (unsigned char)next_stop(s->vel[c], d, vel_stops, 5);
            POPUP("Velocity %c=%d", 'A' + c, s->vel[c]);
        } else {
            s->vel[c] = (unsigned char)clampi(s->vel[c] + d, 1, 127);
        }
        break;
    default:
        if (c == P_ON) {
            int ty = clampi(type_of(t) + d, 0, 2);
            set_type(t, ty);
            POPUP("Type=%s", type_names[ty]);
        } else if (func_held && c == P_RANGE) {
            s->set[c] = (unsigned char)next_stop(s->set[c], d, range_stops, 3);
            POPUP("Range=+-%d", s->set[c]);
        } else if (c == P_DIV) {
            s->set[c] = (unsigned char)div_turn(s->set[c], d);
            if (s->set[c] == 0 && !per_track_mode())
                POPUP("2X needs SCALE per track");
            else
                POPUP("Division=%s", div_name(s->set[c]));
        } else {
            s->set[c] = (unsigned char)clampi(s->set[c] + d, set_min[c], set_max[c]);
        }
        if (c == P_DIR || c == P_LEN || c == P_SHAPE)
            reset_req[t] = 1;
        break;
    }
}

static void st_enc(int t, struct stages *g, int pg, int c, long d)
{
    switch (pg) {
    case PG8_PITCH:
        if (func_held) {
            g->pitch[c] = (signed char)next_stop(g->pitch[c], d, note_stops, 5);
            POPUP("Pitch %d=%c%d", c + 1, g->pitch[c] < 0 ? '-' : '+',
                  g->pitch[c] < 0 ? -g->pitch[c] : g->pitch[c]);
        } else {
            g->pitch[c] = (signed char)clampi(g->pitch[c] + d, -NOTE_MAX, NOTE_MAX);
        }
        break;
    case PG8_VEL:
        if (func_held) {
            g->vel[c] = (unsigned char)next_stop(g->vel[c], d, vel_stops, 5);
            POPUP("Velocity %d=%d", c + 1, g->vel[c]);
        } else {
            g->vel[c] = (unsigned char)clampi(g->vel[c] + d, 1, 127);
        }
        break;
    case PG8_PULSES:
        g->pulses[c] = (unsigned char)clampi(g->pulses[c] + d, 1, 8);
        break;
    case PG8_MODE:
        g->mode[c] = (unsigned char)clampi(g->mode[c] + d, 0, MODES - 1);
        break;
    case PG8_DIV:
        g->div[c] = (unsigned char)clampi(g->div[c] + d, 1, 4);
        break;
    default:
        if (c == S_TYPE) {
            int ty = clampi(type_of(t) + d, 0, 2);
            set_type(t, ty);
            POPUP("Type=%s", type_names[ty]);
        } else if (func_held && c == S_RANGE) {
            g->set[c] = (unsigned char)next_stop(g->set[c], d, range_stops, 3);
            POPUP("Range=+-%d", g->set[c]);
        } else if (c == S_CLOCK) {
            g->set[c] = (unsigned char)div_turn(g->set[c], d);
            if (g->set[c] == 0 && !per_track_mode())
                POPUP("2X needs SCALE per track");
            else
                POPUP("Clock=%s", div_name(g->set[c]));
        } else {
            g->set[c] = (unsigned char)clampi(g->set[c] + d, sset_min[c], sset_max[c]);
        }
        if (c == S_DIR || c == S_STAGES || c == S_SHIFT)
            reset_req[t] = 1;
        break;
    }
}

int digiseq_enc(void *brain, void *ev)
{
    unsigned long e = EV_ENC(ev);
    long d;
    int t, eng, pg;
    digiseq_brain = brain;
    if (e < 1 || e > 8 || !shown(brain))
        return 0;
    t = ui_track();
    if (t < 0)
        return 0;
    enc_acc[e - 1] += EV_DELTA(ev);
    d = enc_acc[e - 1] / COUNTS;
    enc_acc[e - 1] -= d * COUNTS;
    if (d == 0)
        return 1;
    eng = eng_of(t);
    pg = *pg_of(eng);
    if (pg >= pages_of(eng))
        pg = 0;
    redraw = 1;
    edited(t);
    if (eng == E_185)
        st_enc(t, &digiseq_trk[t].st, pg, (int)e - 1, d);
    else
        snake_enc(t, &digiseq_snake[t], pg, (int)e - 1, d);
    return 1;
}

/* ==== drawing (ev_tick, ev_draw): 128 x 64, y = 0 the bottom row ========= */
typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef int (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);
#define FILLRECT  ((rect_fn)fw_fillrect)
#define FRAMERECT ((rect_fn)fw_framerect)
#define TEXTF     ((text_fn)fw_textf)
#define FONT5     ((const void *)fw_font5)

static const char *const note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

void digiseq_tick(void *ctrl)
{
    int t;
    if (!data_ready)
        put_together();
    if (fw_transport == 0)
        for (t = 0; t < TRACKS; t++)
            rewind[t] = 1;
    if (save_wait && !--save_wait)
        mirror_request();
    speed_sync();
    if (shown(digiseq_brain)) {
        static int last_pos = -1, last_play = -1, last_t = -1;
        int ut = ui_track(), pos = -1;
        int playing = (int)fw_transport;
        if (ut >= 0)
            pos = eng_of(ut) == E_185 ? digiseq_stage[ut] * 8 + digiseq_pulse[ut] : digiseq_cell[ut];
        if (redraw || pos != last_pos || playing != last_play || ut != last_t) {
            redraw = 0;
            last_pos = pos;
            last_play = playing;
            last_t = ut;
            *((unsigned char *)ctrl + 0x20) = 1;
        }
    }
}

static int path_order(const struct snake *s, int cell)
{
    int len = s->set[P_LEN], k;
    for (k = 0; k < len; k++)
        if (cell_of(s, k, len) == cell)
            return k + 1;
    return 0;
}

static void draw_note(void *bmp, int x0, int y0, int n, int off)
{
    n = clamp_note(n);
    TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "%s%d", note_names[n % 12], n / 12);
    TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%c%d", off < 0 ? '-' : '+', off < 0 ? -off : off);
}

static void draw_vel(void *bmp, int x0, int y0, int v)
{
    int h = (v * 14) / 127;
    FILLRECT(bmp, x0 + 3, y0 + 4, x0 + 6, y0 + 4 + h, 1);
    TEXTF(bmp, FONT5, x0 + 10, y0 + 8, -1, "%d", v);
}

/* 185: a stage's pulses as a row of ticks under a mode: ONE a tick on the
 * first, ALL on every DIV-th, HOLD a bar across, OFF dots */
static void draw_pulses(void *bmp, int x0, int y0, const struct stages *g, int s, int cur)
{
    int n = g->pulses[s], m = g->mode[s], dv = g->div[s], i;
    int w = 3, x = x0 + 3;                  /* a pulse a slot: the stage's length shows */
    for (i = 0; i < n; i++) {
        int xl = x + i * w, xr = xl + 1;
        int hit = m == M_ALL ? (i % dv == 0) : (m == M_ONE || m == M_HOLD) ? i == 0 : 0;
        if (i == cur)
            FILLRECT(bmp, xl, y0 + 2, xr, y0 + 2, 1);       /* the playing pulse */
        if (m == M_HOLD) {
            FILLRECT(bmp, xl, y0 + 5, xr + (i < n - 1 ? 2 : 0), y0 + 7, 1);
            if (i == 0)
                FILLRECT(bmp, xl, y0 + 5, xl, y0 + 10, 1);
        } else if (hit) {
            FILLRECT(bmp, xl, y0 + 4, xr, y0 + 10, 1);
        } else {
            FILLRECT(bmp, xl, y0 + 5, xl, y0 + 5, 1);
        }
    }
}

static void draw_snake(void *bmp, int t, int base, int root)
{
    const struct snake *s = &digiseq_snake[t];
    int pg = digiseq_pg_snake % SNAKE_PAGES, c;
    int playing = digiseq_trk[t].on && fw_transport == 1;
    for (c = 0; c < CELLS; c++) {
        int x0 = 32 * (c & 3), y0 = (c < 4) ? 30 : 7;
        int x1 = x0 + 30, y1 = y0 + 21, order;
        FRAMERECT(bmp, x0, y0, x1, y1, 1);
        if (pg == PG_PATH) {
            int v = s->set[c];
            TEXTF(bmp, FONT5, x0 + 3, y0 + 14, -1, "%s", set_labels[c]);
            switch (c) {
            case P_ON:    TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", type_names[type_of(t)]); break;
            case P_SHAPE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", shape_names[v % SHAPES]); break;
            case P_DIR:   TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", dir_names[v % DIRS]); break;
            case P_SCALE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", scale_names[v % SCALES]); break;
            case P_RANGE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "+-%d", v); break;
            case P_DIV:   TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", div_name(v)); break;
            default:      TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%d", v); break;
            }
            continue;
        }
        order = path_order(s, c);
        if (order)
            TEXTF(bmp, FONT5, x1 - 6, y1 - 6, -1, "%d", order);
        if (playing && digiseq_cell[t] == c) {
            FRAMERECT(bmp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1);
            FRAMERECT(bmp, x0 + 2, y0 + 2, x1 - 2, y1 - 2, 1);
        }
        if (pg == PG_NOTES) {
            draw_note(bmp, x0, y0, quantise(base + s->note[c], root, s->set[P_SCALE]), s->note[c]);
        } else if (pg == PG_GATES) {
            if ((s->gate >> c) & 1)
                FILLRECT(bmp, x0 + 8, y0 + 5, x1 - 10, y1 - 5, 1);
            else
                FRAMERECT(bmp, x0 + 8, y0 + 5, x1 - 10, y1 - 5, 1);
        } else {
            draw_vel(bmp, x0, y0, s->vel[c]);
        }
    }
    TEXTF(bmp, FONT5, 72, 0, -1, "%s %d/%d", snake_pages[pg], pg + 1, SNAKE_PAGES);
}

static int in_sequence(const struct stages *g, int s)
{
    int k;
    for (k = 0; k < stages_n(g); k++)
        if (stage_of(g, k) == s)
            return 1;
    return 0;
}

static int total_steps(const struct stages *g)
{
    int k, n = 0;
    for (k = 0; k < stages_n(g); k++)
        n += g->pulses[stage_of(g, k)];
    return n * steps_of(g->set[S_CLOCK]);
}

static void draw_185(void *bmp, int t, int base, int root)
{
    const struct stages *g = &digiseq_trk[t].st;
    int pg = digiseq_pg_185 % PAGES_185, c;
    int playing = digiseq_trk[t].on && fw_transport == 1;
    for (c = 0; c < CELLS; c++) {
        int x0 = 32 * (c & 3), y0 = (c < 4) ? 30 : 7;
        int x1 = x0 + 30, y1 = y0 + 21;
        int cur = playing && digiseq_stage[t] == c;
        if (pg == PG8_SETUP) {
            int v = g->set[c];
            FRAMERECT(bmp, x0, y0, x1, y1, 1);
            TEXTF(bmp, FONT5, x0 + 3, y0 + 14, -1, "%s", sset_labels[c]);
            switch (c) {
            case S_TYPE:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", type_names[type_of(t)]); break;
            case S_DIR:   TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", dir_names[v % DIRS]); break;
            case S_CLOCK: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", div_name(v)); break;
            case S_SCALE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", scale_names[v % SCALES]); break;
            case S_RANGE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "+-%d", v); break;
            case S_NLEN:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", nlen_names[v % NLENS]); break;
            default:      TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%d", v); break;
            }
            continue;
        }
        if (in_sequence(g, c)) {
            FRAMERECT(bmp, x0, y0, x1, y1, 1);
            TEXTF(bmp, FONT5, x1 - 6, y1 - 6, -1, "%d", c + 1);
        } else {                            /* past STAGES: corners only */
            FILLRECT(bmp, x0, y0, x0 + 2, y0, 1);
            FILLRECT(bmp, x1 - 2, y0, x1, y0, 1);
            FILLRECT(bmp, x0, y1, x0 + 2, y1, 1);
            FILLRECT(bmp, x1 - 2, y1, x1, y1, 1);
        }
        if (cur) {
            FRAMERECT(bmp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1);
            FRAMERECT(bmp, x0 + 2, y0 + 2, x1 - 2, y1 - 2, 1);
        }
        switch (pg) {
        case PG8_PITCH:
            draw_note(bmp, x0, y0, quantise(base + g->pitch[c], root, g->set[S_SCALE]), g->pitch[c]);
            break;
        case PG8_VEL:
            draw_vel(bmp, x0, y0, g->vel[c]);
            break;
        case PG8_PULSES:
            TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "%d", g->pulses[c]);
            draw_pulses(bmp, x0, y0, g, c, cur ? digiseq_pulse[t] : -1);
            break;
        case PG8_MODE:
            TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "%s", mode_names[g->mode[c] % MODES]);
            draw_pulses(bmp, x0, y0, g, c, cur ? digiseq_pulse[t] : -1);
            break;
        default:                            /* DIV */
            if (g->mode[c] == M_ALL)
                TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "1/%d", g->div[c]);
            else
                TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "(1/%d)", g->div[c]);
            draw_pulses(bmp, x0, y0, g, c, cur ? digiseq_pulse[t] : -1);
            break;
        }
    }
    TEXTF(bmp, FONT5, 72, 0, -1, "%s %d/%d", pages185[pg], pg + 1, PAGES_185);
}

void digiseq_draw(void *bmp, void *ctrl)
{
    int t, base = 60, root = 0;
    struct sq_track info;
    (void)ctrl;
    if (!shown(digiseq_brain))
        return;
    t = ui_track();
    /* Rows 53-63 stay the Digitone's: its title bar and drop-down messages. */
    FILLRECT(bmp, 0, 0, 127, 52, 0);
    if (t < 0) {
        TEXTF(bmp, FONT5, 1, 30, -1, "DIGISEQ: NO TRACK");
        return;
    }
    if (sq_track_info(t, &info, sizeof info) == 0) {
        base = info.note;
        if (info.root != SQ_ROOT_NONE)
            root = info.root % 12;
    }
    if (eng_of(t) == E_185) {
        draw_185(bmp, t, base, root);
        TEXTF(bmp, FONT5, 1, 0, -1, "%s%d 185 %s %dST", t < 4 ? "T" : "M", (t & 3) + 1,
              digiseq_trk[t].on ? "ON" : "OFF", total_steps(&digiseq_trk[t].st));
    } else {
        draw_snake(bmp, t, base, root);
        TEXTF(bmp, FONT5, 1, 0, -1, "%s%d SNAKE %s", t < 4 ? "T" : "MIDI ", (t & 3) + 1,
              digiseq_trk[t].on ? "ON" : "OFF");
    }
}
