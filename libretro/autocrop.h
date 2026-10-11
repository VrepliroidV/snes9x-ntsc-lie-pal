/*
 * Automatic black-border crop for the Libretro core.
 *
 * Borders are kept in base units: 1/256 of the picture width horizontally and
 * one SNES line vertically. The crop therefore survives switches between 256,
 * 512 and 1024 pixel widths and double-height output.
 *
 * Each frame gives two measurements per side:
 * - raw: how far the dark border really goes. Graphics inside the cropped
 *   area (raw smaller than the crop) restore that side in the same frame.
 * - trusted: the part of the border that may be cropped.
 *
 * Hardware bands, made only of lines drawn with the display off (forced blank,
 * zero brightness or no main-screen layers), are trusted when the picture
 * between them is not mostly dark. Bands that are only dark pixels look the
 * same as the black background of a logo screen, so they are trusted only
 * when the picture is mostly filled, each band is at most a quarter of the
 * picture and the edge next to the band is a solid line of graphics rather
 * than the outline of an object. Before such a band is cropped the first
 * time, it must also stay unchanged while the picture inside moves, or stay
 * unchanged for several seconds on a still picture.
 *
 * Layouts that stay on screen for a while are remembered for the game and
 * cropped in the first frame they appear again. A remembered layout that
 * graphics contradict right after it is applied, twice in a row, is forgotten.
 */
#ifndef SNES9X_LIBRETRO_AUTOCROP_H
#define SNES9X_LIBRETRO_AUTOCROP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(RED_SHIFT_BITS) || !defined(GREEN_SHIFT_BITS)
#error "Include the Snes9x pixel format headers before autocrop.h"
#endif

enum autocrop_mode
{
    AUTOCROP_DISABLED,
    AUTOCROP_FIT,
    AUTOCROP_STRETCH
};

enum
{
    AUTOCROP_TOP,
    AUTOCROP_BOTTOM,
    AUTOCROP_LEFT,
    AUTOCROP_RIGHT,
    AUTOCROP_SIDES
};

/* First-time waits, in frames. Pixel-only borders need moving frames, or a
 * much longer unchanged period on still screens (paused or idle games);
 * logo screens rarely stay that long. */
static const int AUTOCROP_HARDWARE_FRAMES = 2;
static const int AUTOCROP_PIXEL_FRAMES = 90;
static const int AUTOCROP_PIXEL_MOTION_FRAMES = 10;
static const int AUTOCROP_PIXEL_STILL_FRAMES = 480;
/* A layout shown this long is remembered and saved. */
static const int AUTOCROP_ESTABLISH_FRAMES = 300;
/* After graphics restore a side, remembered layouts wait this many frames,
 * so a sprite crossing the border cannot make the picture jump every frame. */
static const int AUTOCROP_MEMORY_COOLDOWN = 30;
/* Graphics restoring a remembered layout this soon count against it. */
static const int AUTOCROP_STRIKE_FRAMES = 60;
static const int AUTOCROP_MAX_STRIKES = 2;
static const int AUTOCROP_MAX_LAYOUTS = 16;
/* Lines of one single colour up to this level (of 31) are borders too:
 * dark grey bars instead of black ones. */
static const int AUTOCROP_DIM_LEVEL = 6;
/* Thinner borders are left alone (base units). */
static const int AUTOCROP_MIN_BAND = 2;
/* Rows or columns next to a band checked for a solid edge (base units). */
static const int AUTOCROP_EDGE_DEPTH = 4;
static const int AUTOCROP_MIN_EDGE_PERCENT = 25;
/* Non-dark share of the picture inside hardware bands. */
static const int AUTOCROP_MIN_HARDWARE_FILL_PERCENT = 25;
/* Non-dark share required to trust pixel-only bands: whole picture and the
 * area left after the crop. */
static const int AUTOCROP_MIN_SCREEN_FILL_PERCENT = 40;
static const int AUTOCROP_MIN_CONTENT_FILL_PERCENT = 25;
/* Motion grid: one sample every 4 base units; a frame moves when this many
 * samples change. */
static const int AUTOCROP_GRID_STEP = 4;
static const int AUTOCROP_GRID_WIDTH = 64;
static const int AUTOCROP_GRID_HEIGHT = 60;
static const int AUTOCROP_MOTION_SAMPLES = 4;
static const char AUTOCROP_FILE_HEADER[] = "snes9x-autocrop 2";

struct autocrop_layout
{
    int band[AUTOCROP_SIDES];
    int strikes;
    bool established;
};

struct autocrop_state
{
    int applied[AUTOCROP_SIDES];
    int pending[AUTOCROP_SIDES];
    int stable[AUTOCROP_SIDES];
    int motion[AUTOCROP_SIDES];
    int since_expand;
    int applied_frames;
    int instant_layout;   /* remembered layout applied at once, or -1 */
    int instant_frames;
    int layout_count;
    autocrop_layout layouts[AUTOCROP_MAX_LAYOUTS];
    bool layouts_changed; /* the saved (established) set changed */
    bool has_grid;
    uint16_t grid[AUTOCROP_GRID_HEIGHT][AUTOCROP_GRID_WIDTH];
};

struct autocrop_frame
{
    const uint16_t *pixels;        /* first output row */
    int pitch;                     /* in pixels */
    int width, height;             /* output size in pixels */
    int hscale, vscale;            /* output pixels per base unit */
    int base_width, base_height;   /* size in base units */
    const uint8_t *hardware_rows;  /* per output row, non-zero for display-off lines */
};

/* Forgets the current crop but keeps the layouts learned for the game. */
static inline void autocrop_reset_tracking(autocrop_state *state)
{
    memset(state->applied, 0, sizeof(state->applied));
    memset(state->pending, 0, sizeof(state->pending));
    memset(state->stable, 0, sizeof(state->stable));
    memset(state->motion, 0, sizeof(state->motion));
    state->since_expand = AUTOCROP_MEMORY_COOLDOWN;
    state->applied_frames = 0;
    state->instant_layout = -1;
    state->instant_frames = 0;
    state->has_grid = false;
}

static inline void autocrop_reset(autocrop_state *state)
{
    memset(state, 0, sizeof(*state));
    autocrop_reset_tracking(state);
}

static inline bool autocrop_dark(uint16_t pixel)
{
    return ((pixel >> RED_SHIFT_BITS) & 0x1f) <= 2 &&
           ((pixel >> GREEN_SHIFT_BITS) & 0x1f) <= 2 &&
           (pixel & 0x1f) <= 2;
}

static inline bool autocrop_dim(uint16_t pixel)
{
    return ((pixel >> RED_SHIFT_BITS) & 0x1f) <= AUTOCROP_DIM_LEVEL &&
           ((pixel >> GREEN_SHIFT_BITS) & 0x1f) <= AUTOCROP_DIM_LEVEL &&
           (pixel & 0x1f) <= AUTOCROP_DIM_LEVEL;
}

/* A border line is all dark, or all one dim colour (a dark grey bar keeps the
 * same colour when the game changes brightness, unlike its dark parts). */
static inline bool autocrop_row_border(const autocrop_frame &frame, int y)
{
    const uint16_t *row = frame.pixels + (ptrdiff_t)y * frame.pitch;
    const uint16_t first = row[0];
    bool dark = true, uniform = autocrop_dim(first);
    for (int x = 0; x < frame.width; x++)
    {
        dark = dark && autocrop_dark(row[x]);
        uniform = uniform && row[x] == first;
        if (!dark && !uniform)
            return false;
    }
    return true;
}

static inline bool autocrop_column_border(const autocrop_frame &frame, int x, int y0, int y1)
{
    const uint16_t first = frame.pixels[(ptrdiff_t)y0 * frame.pitch + x];
    bool dark = true, uniform = autocrop_dim(first);
    for (int y = y0; y < y1; y++)
    {
        const uint16_t pixel = frame.pixels[(ptrdiff_t)y * frame.pitch + x];
        dark = dark && autocrop_dark(pixel);
        uniform = uniform && pixel == first;
        if (!dark && !uniform)
            return false;
    }
    return true;
}

static inline bool autocrop_rows_hardware(const autocrop_frame &frame, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        if (!frame.hardware_rows[y])
            return false;
    return true;
}

/* Percentage of samples in a rectangle of output pixels that are neither dark
 * nor the border colour (ignore, or -1 for none). */
static int autocrop_fill(const autocrop_frame &frame, int x0, int x1, int y0, int y1, int xstep, int ystep, int ignore)
{
    long content = 0, samples = 0;
    for (int y = y0; y < y1; y += ystep)
    {
        const uint16_t *row = frame.pixels + (ptrdiff_t)y * frame.pitch;
        for (int x = x0; x < x1; x += xstep)
        {
            samples++;
            content += !autocrop_dark(row[x]) && row[x] != ignore;
        }
    }
    return samples ? (int)(content * 100 / samples) : 0;
}

static inline int autocrop_min(int a, int b) { return a < b ? a : b; }
static inline int autocrop_max(int a, int b) { return a > b ? a : b; }

/* Measures the dark border of one frame. Returns false when the whole frame is
 * dark (fades, black screens): those frames must not change the crop. */
static bool autocrop_measure(const autocrop_frame &frame, int raw[AUTOCROP_SIDES],
                             int trusted[AUTOCROP_SIDES], bool hardware[AUTOCROP_SIDES])
{
    int top = 0, bottom = 0, left = 0, right = 0;

    while (top < frame.height && autocrop_row_border(frame, top))
        top++;
    if (top == frame.height)
        return false;
    while (autocrop_row_border(frame, frame.height - 1 - bottom))
        bottom++;

    const int y0 = top, y1 = frame.height - bottom;
    while (autocrop_column_border(frame, left, y0, y1))
        left++;
    while (autocrop_column_border(frame, frame.width - 1 - right, y0, y1))
        right++;
    const int x0 = left, x1 = frame.width - right;

    /* A dark grey border colour does not count as picture in the fill checks,
     * so a logo on a grey background is treated like one on black. */
    int ignore = -1;
    {
        const uint16_t *corner = top ? frame.pixels :
                                 bottom ? frame.pixels + (ptrdiff_t)(frame.height - 1) * frame.pitch :
                                 left ? frame.pixels + (ptrdiff_t)y0 * frame.pitch :
                                 right ? frame.pixels + (ptrdiff_t)y0 * frame.pitch + frame.width - 1 : NULL;
        if (corner && !autocrop_dark(*corner) && autocrop_dim(*corner))
            ignore = *corner;
    }

    /* Display-off lines at the outer edge of the top and bottom bands. */
    int hardware_top = 0, hardware_bottom = 0;
    while (hardware_top < top && frame.hardware_rows[hardware_top])
        hardware_top++;
    while (hardware_bottom < bottom && frame.hardware_rows[frame.height - 1 - hardware_bottom])
        hardware_bottom++;

    /* Round toward zero so a partial unit of content is never cropped. */
    raw[AUTOCROP_TOP]    = top / frame.vscale;
    raw[AUTOCROP_BOTTOM] = bottom / frame.vscale;
    raw[AUTOCROP_LEFT]   = left / frame.hscale;
    raw[AUTOCROP_RIGHT]  = right / frame.hscale;
    int hardware_band[AUTOCROP_SIDES] = { hardware_top / frame.vscale, hardware_bottom / frame.vscale, 0, 0 };
    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        if (raw[side] < AUTOCROP_MIN_BAND)
            raw[side] = 0;
        if (hardware_band[side] < AUTOCROP_MIN_BAND)
            hardware_band[side] = 0;
        hardware[side] = false;
        trusted[side] = 0;
    }

    /* The picture between the display-off lines must not be mostly dark (a
     * logo between black bars), and the bars must leave half the height. */
    const int screen_fill = autocrop_fill(frame, 0, frame.width, hardware_top, frame.height - hardware_bottom,
                                          4 * frame.hscale, 4 * frame.vscale, ignore);
    const bool trust_hardware = screen_fill >= AUTOCROP_MIN_HARDWARE_FILL_PERCENT &&
        hardware_band[AUTOCROP_TOP] + hardware_band[AUTOCROP_BOTTOM] <= frame.base_height / 2;

    /* Pixel-only bands: trusted on mostly filled pictures only, each band at
     * most a quarter of the picture. */
    bool any_pixel_band = false, trust_pixels = screen_fill >= AUTOCROP_MIN_SCREEN_FILL_PERCENT;
    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        if (raw[side] <= hardware_band[side])
            continue;
        any_pixel_band = true;
        const int limit = side < AUTOCROP_LEFT ? frame.base_height / 4 : frame.base_width / 4;
        if (raw[side] > limit)
            trust_pixels = false;
    }

    if (any_pixel_band && trust_pixels)
        trust_pixels = autocrop_fill(frame, x0, x1, y0, y1, 4 * frame.hscale, 4 * frame.vscale, ignore) >= AUTOCROP_MIN_CONTENT_FILL_PERCENT;

    /* The edge next to each pixel-only band must be a solid line of graphics,
     * not the outline of a logo floating on black. */
    if (any_pixel_band && trust_pixels)
    {
        const int rows = AUTOCROP_EDGE_DEPTH * frame.vscale, columns = AUTOCROP_EDGE_DEPTH * frame.hscale;
        for (int side = 0; side < AUTOCROP_SIDES && trust_pixels; side++)
        {
            if (raw[side] <= hardware_band[side])
                continue;
            int edge;
            if (side == AUTOCROP_TOP)
                edge = autocrop_fill(frame, x0, x1, y0, autocrop_min(y0 + rows, y1), frame.hscale, frame.vscale, ignore);
            else if (side == AUTOCROP_BOTTOM)
                edge = autocrop_fill(frame, x0, x1, autocrop_max(y1 - rows, y0), y1, frame.hscale, frame.vscale, ignore);
            else if (side == AUTOCROP_LEFT)
                edge = autocrop_fill(frame, x0, autocrop_min(x0 + columns, x1), y0, y1, frame.hscale, frame.vscale, ignore);
            else
                edge = autocrop_fill(frame, autocrop_max(x1 - columns, x0), x1, y0, y1, frame.hscale, frame.vscale, ignore);
            trust_pixels = edge >= AUTOCROP_MIN_EDGE_PERCENT;
        }
    }

    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        const bool display_off = trust_hardware && hardware_band[side] > 0;
        if (raw[side] > hardware_band[side] && trust_pixels)
            trusted[side] = raw[side];
        else if (display_off)
        {
            /* The display-off part, cropped quickly. */
            trusted[side] = hardware_band[side];
            hardware[side] = true;
        }
    }

    return true;
}

/* Compares a coarse sample grid with the previous frame. Returns true when
 * enough of the picture changed to count as a moving frame. */
static bool autocrop_motion(autocrop_state *state, const autocrop_frame &frame)
{
    int changed = 0;
    for (int gy = 0; gy < AUTOCROP_GRID_HEIGHT; gy++)
    {
        const int y = (gy * AUTOCROP_GRID_STEP + AUTOCROP_GRID_STEP / 2) * frame.vscale;
        const bool inside = y < frame.height;
        const uint16_t *row = frame.pixels + (ptrdiff_t)(inside ? y : 0) * frame.pitch;
        for (int gx = 0; gx < AUTOCROP_GRID_WIDTH; gx++)
        {
            const int x = (gx * AUTOCROP_GRID_STEP + AUTOCROP_GRID_STEP / 2) * frame.hscale;
            const uint16_t sample = inside && x < frame.width ? row[x] : 0;
            changed += state->grid[gy][gx] != sample;
            state->grid[gy][gx] = sample;
        }
    }
    const bool had_grid = state->has_grid;
    state->has_grid = true;
    return had_grid && changed >= AUTOCROP_MOTION_SAMPLES;
}

static inline bool autocrop_same(const int a[AUTOCROP_SIDES], const int b[AUTOCROP_SIDES])
{
    return !memcmp(a, b, sizeof(int) * AUTOCROP_SIDES);
}

static int autocrop_find(const autocrop_state *state, const int band[AUTOCROP_SIDES])
{
    for (int i = 0; i < state->layout_count; i++)
        if (autocrop_same(state->layouts[i].band, band))
            return i;
    return -1;
}

static void autocrop_forget(autocrop_state *state, int index)
{
    if (state->layouts[index].established)
        state->layouts_changed = true;
    memmove(&state->layouts[index], &state->layouts[index + 1],
            sizeof(state->layouts[0]) * (state->layout_count - index - 1));
    state->layout_count--;
    if (state->instant_layout == index)
        state->instant_layout = -1;
    else if (state->instant_layout > index)
        state->instant_layout--;
}

/* Moves a layout to the end of the list, the most recently used position. */
static int autocrop_touch(autocrop_state *state, int index)
{
    const autocrop_layout layout = state->layouts[index];
    const bool instant = state->instant_layout == index;
    memmove(&state->layouts[index], &state->layouts[index + 1],
            sizeof(state->layouts[0]) * (state->layout_count - index - 1));
    state->layouts[state->layout_count - 1] = layout;
    if (instant)
        state->instant_layout = state->layout_count - 1;
    else if (state->instant_layout > index)
        state->instant_layout--;
    return state->layout_count - 1;
}

/* Adds a layout seen for the first time. It is saved once established. */
static void autocrop_remember(autocrop_state *state, const int band[AUTOCROP_SIDES], bool established)
{
    bool any = false;
    for (int side = 0; side < AUTOCROP_SIDES; side++)
        any |= band[side] != 0;
    if (!any)
        return;
    const int found = autocrop_find(state, band);
    if (found >= 0)
    {
        if (established && !state->layouts[found].established)
        {
            state->layouts[found].established = true;
            state->layouts_changed = true;
        }
        return;
    }
    if (state->layout_count == AUTOCROP_MAX_LAYOUTS)
    {
        /* Make room: the least recently used unconfirmed layout, else the oldest. */
        int victim = 0;
        for (int i = 0; i < state->layout_count; i++)
            if (!state->layouts[i].established)
            {
                victim = i;
                break;
            }
        autocrop_forget(state, victim);
    }
    autocrop_layout &layout = state->layouts[state->layout_count++];
    memcpy(layout.band, band, sizeof(layout.band));
    layout.strikes = 0;
    layout.established = established;
    if (established)
        state->layouts_changed = true;
}

static void autocrop_apply(autocrop_state *state, const int band[AUTOCROP_SIDES])
{
    memcpy(state->applied, band, sizeof(state->applied));
    memcpy(state->pending, band, sizeof(state->pending));
    memset(state->stable, 0, sizeof(state->stable));
    memset(state->motion, 0, sizeof(state->motion));
    state->applied_frames = 0;
}

/* Feeds one measured frame. Returns true when the applied crop changed. */
static bool autocrop_update(autocrop_state *state, const int raw[AUTOCROP_SIDES],
                            const int trusted[AUTOCROP_SIDES], const bool hardware[AUTOCROP_SIDES],
                            bool moving)
{
    bool changed = false;

    if (state->since_expand < AUTOCROP_MEMORY_COOLDOWN)
        state->since_expand++;
    if (state->instant_layout >= 0 && ++state->instant_frames > AUTOCROP_STRIKE_FRAMES)
    {
        state->layouts[state->instant_layout].strikes = 0; /* it held: keep trusting it */
        state->instant_layout = -1;
    }

    /* Graphics inside the cropped area: show them right away. */
    for (int side = 0; side < AUTOCROP_SIDES; side++)
        if (raw[side] < state->applied[side])
        {
            state->applied[side] = raw[side];
            state->pending[side] = raw[side];
            state->stable[side] = 0;
            state->motion[side] = 0;
            state->since_expand = 0;
            state->applied_frames = 0;
            changed = true;
        }
    if (changed && state->instant_layout >= 0)
    {
        /* A remembered layout was wrong right after it was applied. */
        const int index = state->instant_layout;
        state->instant_layout = -1;
        if (++state->layouts[index].strikes >= AUTOCROP_MAX_STRIKES)
            autocrop_forget(state, index);
    }

    /* A remembered layout is back: crop it in this same frame. */
    if (state->since_expand >= AUTOCROP_MEMORY_COOLDOWN && !autocrop_same(trusted, state->applied))
    {
        bool grows = true;
        for (int side = 0; side < AUTOCROP_SIDES; side++)
            grows &= trusted[side] >= state->applied[side];
        const int found = grows ? autocrop_find(state, trusted) : -1;
        if (found >= 0 && state->layouts[found].established)
        {
            autocrop_apply(state, trusted);
            state->instant_layout = autocrop_touch(state, found);
            state->instant_frames = 0;
            return true;
        }
    }

    /* New borders: crop them once they stay unchanged. */
    bool confirmed = false;
    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        if (trusted[side] <= state->applied[side])
        {
            state->pending[side] = state->applied[side];
            state->stable[side] = 0;
            state->motion[side] = 0;
            continue;
        }
        if (trusted[side] != state->pending[side])
        {
            state->pending[side] = trusted[side];
            state->stable[side] = 0;
            state->motion[side] = 0;
        }
        state->stable[side]++;
        state->motion[side] += moving;
        const bool ready = hardware[side] ?
            state->stable[side] >= AUTOCROP_HARDWARE_FRAMES :
            (state->stable[side] >= AUTOCROP_PIXEL_FRAMES && state->motion[side] >= AUTOCROP_PIXEL_MOTION_FRAMES) ||
            state->stable[side] >= AUTOCROP_PIXEL_STILL_FRAMES;
        if (ready)
        {
            state->applied[side] = trusted[side];
            state->stable[side] = 0;
            state->motion[side] = 0;
            changed = confirmed = true;
        }
    }
    if (changed)
        state->applied_frames = 0;
    if (confirmed)
        autocrop_remember(state, state->applied, false);

    /* A layout that stays on screen long enough is saved for the game. */
    if (!changed && ++state->applied_frames == AUTOCROP_ESTABLISH_FRAMES)
    {
        const int found = autocrop_find(state, state->applied);
        if (found >= 0)
        {
            autocrop_remember(state, state->applied, true);
            autocrop_touch(state, found);
        }
    }

    return changed;
}

/* Layout memory file: a header line, then "top bottom left right" for each
 * established layout, least recently used first. */
static void autocrop_load_layouts(autocrop_state *state, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    char header[64];
    if (fgets(header, sizeof(header), file) && !strncmp(header, AUTOCROP_FILE_HEADER, sizeof(AUTOCROP_FILE_HEADER) - 1))
    {
        int band[AUTOCROP_SIDES];
        while (state->layout_count < AUTOCROP_MAX_LAYOUTS &&
               fscanf(file, "%d %d %d %d", &band[0], &band[1], &band[2], &band[3]) == AUTOCROP_SIDES)
        {
            bool valid = true;
            for (int side = 0; side < AUTOCROP_SIDES; side++)
                valid &= band[side] >= 0 && band[side] <= 128;
            if (valid)
                autocrop_remember(state, band, true);
        }
    }
    fclose(file);
    state->layouts_changed = false;
}

static void autocrop_save_layouts(autocrop_state *state, const char *path)
{
    state->layouts_changed = false;
    bool any = false;
    for (int i = 0; i < state->layout_count; i++)
        any |= state->layouts[i].established;
    if (!any)
    {
        remove(path);
        return;
    }
    FILE *file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file, "%s\n", AUTOCROP_FILE_HEADER);
    for (int i = 0; i < state->layout_count; i++)
        if (state->layouts[i].established)
            fprintf(file, "%d %d %d %d\n", state->layouts[i].band[0], state->layouts[i].band[1],
                    state->layouts[i].band[2], state->layouts[i].band[3]);
    fclose(file);
}

#endif
