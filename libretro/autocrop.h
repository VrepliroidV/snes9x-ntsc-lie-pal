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
 * - trusted: the part of the border that may be cropped. A "hardware" band,
 *   made only of lines drawn with the display off (forced blank, zero
 *   brightness or no main-screen layers), is trusted at once. Bands that are
 *   only dark pixels can be dark artwork, so they are ignored on screens
 *   where too much of the picture is dark.
 *
 * A new trusted border is cropped once it stays unchanged for a few frames.
 * Every confirmed layout is remembered for the game, and when the same layout
 * appears again it is applied in the first frame, so the border is not seen.
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

/* Frames a new border must stay unchanged before it is cropped the first time. */
static const int AUTOCROP_HARDWARE_FRAMES = 2;
static const int AUTOCROP_PIXEL_FRAMES = 30;
/* After graphics restore a side, remembered layouts wait this many frames,
 * so a sprite crossing the border cannot make the picture jump every frame. */
static const int AUTOCROP_MEMORY_COOLDOWN = 30;
static const int AUTOCROP_MAX_LAYOUTS = 16;
/* Thinner borders are left alone (base units). */
static const int AUTOCROP_MIN_BAND = 2;
/* Pixel-only bands are trusted only on screens at least this full (percent). */
static const int AUTOCROP_MIN_FILL_PERCENT = 25;
static const char AUTOCROP_FILE_HEADER[] = "snes9x-autocrop 1";

struct autocrop_state
{
    int applied[AUTOCROP_SIDES];
    int pending[AUTOCROP_SIDES];
    int stable[AUTOCROP_SIDES];
    int since_expand;
    int layout_count;
    int layouts[AUTOCROP_MAX_LAYOUTS][AUTOCROP_SIDES];
    bool layouts_changed;
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
    state->since_expand = AUTOCROP_MEMORY_COOLDOWN;
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

static inline bool autocrop_row_dark(const autocrop_frame &frame, int y)
{
    const uint16_t *row = frame.pixels + (ptrdiff_t)y * frame.pitch;
    for (int x = 0; x < frame.width; x++)
        if (!autocrop_dark(row[x]))
            return false;
    return true;
}

static inline bool autocrop_column_dark(const autocrop_frame &frame, int x, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        if (!autocrop_dark(frame.pixels[(ptrdiff_t)y * frame.pitch + x]))
            return false;
    return true;
}

static inline bool autocrop_rows_hardware(const autocrop_frame &frame, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        if (!frame.hardware_rows[y])
            return false;
    return true;
}

/* Measures the dark border of one frame. Returns false when the whole frame is
 * dark (fades, black screens): those frames must not change the crop. */
static bool autocrop_measure(const autocrop_frame &frame, int raw[AUTOCROP_SIDES],
                             int trusted[AUTOCROP_SIDES], bool hardware[AUTOCROP_SIDES])
{
    int top = 0, bottom = 0, left = 0, right = 0;

    while (top < frame.height && autocrop_row_dark(frame, top))
        top++;
    if (top == frame.height)
        return false;
    while (autocrop_row_dark(frame, frame.height - 1 - bottom))
        bottom++;

    const int y0 = top, y1 = frame.height - bottom;
    while (autocrop_column_dark(frame, left, y0, y1))
        left++;
    while (autocrop_column_dark(frame, frame.width - 1 - right, y0, y1))
        right++;

    /* Round toward zero so a partial unit of content is never cropped. */
    raw[AUTOCROP_TOP]    = top / frame.vscale;
    raw[AUTOCROP_BOTTOM] = bottom / frame.vscale;
    raw[AUTOCROP_LEFT]   = left / frame.hscale;
    raw[AUTOCROP_RIGHT]  = right / frame.hscale;
    hardware[AUTOCROP_TOP]    = autocrop_rows_hardware(frame, 0, top);
    hardware[AUTOCROP_BOTTOM] = autocrop_rows_hardware(frame, y1, frame.height);
    hardware[AUTOCROP_LEFT]   = false;
    hardware[AUTOCROP_RIGHT]  = false;

    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        if (raw[side] < AUTOCROP_MIN_BAND)
            raw[side] = 0;
        trusted[side] = raw[side];
    }

    /* Hardware bands that leave less than half the picture are not borders. */
    if (raw[AUTOCROP_TOP] + raw[AUTOCROP_BOTTOM] > frame.base_height / 2)
    {
        if (hardware[AUTOCROP_TOP])
            trusted[AUTOCROP_TOP] = 0;
        if (hardware[AUTOCROP_BOTTOM])
            trusted[AUTOCROP_BOTTOM] = 0;
    }

    /* A pixel-only band wider than a quarter of the picture means a mostly
     * dark screen (text on black, dark scenery): trust no pixel-only band. */
    bool trust_pixels = true;
    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        const int limit = side < AUTOCROP_LEFT ? frame.base_height / 4 : frame.base_width / 4;
        if (!hardware[side] && raw[side] > limit)
            trust_pixels = false;
    }

    if (trust_pixels)
    {
        /* Sparse screens (stars, small text) are not bordered gameplay. */
        const int x0 = left, x1 = frame.width - right;
        long content = 0, samples = 0;
        for (int y = y0; y < y1; y += 2)
        {
            const uint16_t *row = frame.pixels + (ptrdiff_t)y * frame.pitch;
            for (int x = x0; x < x1; x += 2)
            {
                samples++;
                content += !autocrop_dark(row[x]);
            }
        }
        trust_pixels = samples && content * 100 >= samples * AUTOCROP_MIN_FILL_PERCENT;
    }

    if (!trust_pixels)
        for (int side = 0; side < AUTOCROP_SIDES; side++)
            if (!hardware[side])
                trusted[side] = 0;

    return true;
}

static inline bool autocrop_same(const int a[AUTOCROP_SIDES], const int b[AUTOCROP_SIDES])
{
    return !memcmp(a, b, sizeof(int) * AUTOCROP_SIDES);
}

/* Adds a layout to the game's memory; the oldest one makes room when full. */
static void autocrop_remember(autocrop_state *state, const int layout[AUTOCROP_SIDES])
{
    bool any = false;
    for (int side = 0; side < AUTOCROP_SIDES; side++)
        any |= layout[side] != 0;
    if (!any)
        return;
    for (int i = 0; i < state->layout_count; i++)
        if (autocrop_same(state->layouts[i], layout))
            return;
    if (state->layout_count == AUTOCROP_MAX_LAYOUTS)
    {
        memmove(state->layouts[0], state->layouts[1], sizeof(state->layouts[0]) * (AUTOCROP_MAX_LAYOUTS - 1));
        state->layout_count--;
    }
    memcpy(state->layouts[state->layout_count++], layout, sizeof(state->layouts[0]));
    state->layouts_changed = true;
}

static void autocrop_apply(autocrop_state *state, const int layout[AUTOCROP_SIDES])
{
    memcpy(state->applied, layout, sizeof(state->applied));
    memcpy(state->pending, layout, sizeof(state->pending));
    memset(state->stable, 0, sizeof(state->stable));
}

/* Feeds one measured frame. Returns true when the applied crop changed. */
static bool autocrop_update(autocrop_state *state, const int raw[AUTOCROP_SIDES],
                            const int trusted[AUTOCROP_SIDES], const bool hardware[AUTOCROP_SIDES])
{
    bool changed = false;

    if (state->since_expand < AUTOCROP_MEMORY_COOLDOWN)
        state->since_expand++;

    /* Graphics inside the cropped area: show them right away. */
    for (int side = 0; side < AUTOCROP_SIDES; side++)
        if (raw[side] < state->applied[side])
        {
            state->applied[side] = raw[side];
            state->pending[side] = raw[side];
            state->stable[side] = 0;
            state->since_expand = 0;
            changed = true;
        }

    /* A layout confirmed before is back: crop it in this same frame. */
    if (state->since_expand >= AUTOCROP_MEMORY_COOLDOWN && !autocrop_same(trusted, state->applied))
    {
        bool grows = true;
        for (int side = 0; side < AUTOCROP_SIDES; side++)
            grows &= trusted[side] >= state->applied[side];
        for (int i = 0; grows && i < state->layout_count; i++)
            if (autocrop_same(state->layouts[i], trusted))
            {
                autocrop_apply(state, trusted);
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
            continue;
        }
        if (trusted[side] != state->pending[side])
        {
            state->pending[side] = trusted[side];
            state->stable[side] = 0;
        }
        const int needed = hardware[side] ? AUTOCROP_HARDWARE_FRAMES : AUTOCROP_PIXEL_FRAMES;
        if (++state->stable[side] >= needed)
        {
            state->applied[side] = trusted[side];
            state->stable[side] = 0;
            changed = confirmed = true;
        }
    }
    if (confirmed)
        autocrop_remember(state, state->applied);

    return changed;
}

/* Layout memory file: a header line, then "top bottom left right" per line. */
static void autocrop_load_layouts(autocrop_state *state, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    char header[64];
    if (fgets(header, sizeof(header), file) && !strncmp(header, AUTOCROP_FILE_HEADER, sizeof(AUTOCROP_FILE_HEADER) - 1))
    {
        int layout[AUTOCROP_SIDES];
        while (state->layout_count < AUTOCROP_MAX_LAYOUTS &&
               fscanf(file, "%d %d %d %d", &layout[0], &layout[1], &layout[2], &layout[3]) == AUTOCROP_SIDES)
        {
            bool valid = true;
            for (int side = 0; side < AUTOCROP_SIDES; side++)
                valid &= layout[side] >= 0 && layout[side] <= 128;
            if (valid)
                autocrop_remember(state, layout);
        }
    }
    fclose(file);
    state->layouts_changed = false;
}

static void autocrop_save_layouts(autocrop_state *state, const char *path)
{
    state->layouts_changed = false;
    FILE *file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file, "%s\n", AUTOCROP_FILE_HEADER);
    for (int i = 0; i < state->layout_count; i++)
        fprintf(file, "%d %d %d %d\n", state->layouts[i][0], state->layouts[i][1], state->layouts[i][2], state->layouts[i][3]);
    fclose(file);
}

#endif
