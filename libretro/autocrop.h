/*
 * Automatic black-border crop for the Libretro core.
 *
 * Borders are kept in base units: 1/256 of the picture width horizontally and
 * one SNES line vertically. The crop therefore survives switches between 256,
 * 512 and 1024 pixel widths and double-height output.
 *
 * A border band is a "hardware" band when every line in it was drawn with the
 * display off (forced blank, zero brightness or no main-screen layers). Those
 * are accepted quickly. Bands that are only dark pixels can be ordinary dark
 * artwork, so they must stay unchanged for longer and are ignored on screens
 * where too much of the picture is dark.
 *
 * Content reaching a cropped area always restores that side in the same frame,
 * so a wrong guess never hides game graphics for more than the frames needed
 * to see them.
 */
#ifndef SNES9X_LIBRETRO_AUTOCROP_H
#define SNES9X_LIBRETRO_AUTOCROP_H

#include <stddef.h>
#include <stdint.h>
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

/* Frames a new border must stay unchanged before it is cropped. */
static const int AUTOCROP_HARDWARE_FRAMES = 6;
static const int AUTOCROP_PIXEL_FRAMES = 60;
/* Thinner borders are left alone (base units). */
static const int AUTOCROP_MIN_BAND = 2;
/* Pixel-only bands are trusted only on screens at least this full (percent). */
static const int AUTOCROP_MIN_FILL_PERCENT = 25;

struct autocrop_state
{
    int applied[AUTOCROP_SIDES];
    int pending[AUTOCROP_SIDES];
    int stable[AUTOCROP_SIDES];
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

static inline void autocrop_reset(autocrop_state *state)
{
    memset(state, 0, sizeof(*state));
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
static bool autocrop_measure(const autocrop_frame &frame, int band[AUTOCROP_SIDES], bool hardware[AUTOCROP_SIDES])
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
    band[AUTOCROP_TOP]    = top / frame.vscale;
    band[AUTOCROP_BOTTOM] = bottom / frame.vscale;
    band[AUTOCROP_LEFT]   = left / frame.hscale;
    band[AUTOCROP_RIGHT]  = right / frame.hscale;
    hardware[AUTOCROP_TOP]    = autocrop_rows_hardware(frame, 0, top);
    hardware[AUTOCROP_BOTTOM] = autocrop_rows_hardware(frame, y1, frame.height);
    hardware[AUTOCROP_LEFT]   = false;
    hardware[AUTOCROP_RIGHT]  = false;

    for (int side = 0; side < AUTOCROP_SIDES; side++)
        if (band[side] < AUTOCROP_MIN_BAND)
            band[side] = 0;

    /* Hardware bands that leave less than half the picture are not borders. */
    if (band[AUTOCROP_TOP] + band[AUTOCROP_BOTTOM] > frame.base_height / 2)
    {
        if (hardware[AUTOCROP_TOP])
            band[AUTOCROP_TOP] = 0;
        if (hardware[AUTOCROP_BOTTOM])
            band[AUTOCROP_BOTTOM] = 0;
    }

    /* A pixel-only band wider than a quarter of the picture means a mostly
     * dark screen (text on black, dark scenery): trust no pixel-only band. */
    bool trust_pixels = true;
    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        const int limit = side < AUTOCROP_LEFT ? frame.base_height / 4 : frame.base_width / 4;
        if (!hardware[side] && band[side] > limit)
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
                band[side] = 0;

    return true;
}

/* Feeds one measured frame. Returns true when the applied crop changed. */
static bool autocrop_update(autocrop_state *state, const int band[AUTOCROP_SIDES], const bool hardware[AUTOCROP_SIDES])
{
    bool changed = false;

    for (int side = 0; side < AUTOCROP_SIDES; side++)
    {
        if (band[side] < state->applied[side])
        {
            /* Content entered the cropped area: show it right away. */
            state->applied[side] = band[side];
            state->pending[side] = band[side];
            state->stable[side] = 0;
            changed = true;
        }
        else if (band[side] == state->applied[side])
        {
            state->pending[side] = band[side];
            state->stable[side] = 0;
        }
        else
        {
            if (band[side] != state->pending[side])
            {
                state->pending[side] = band[side];
                state->stable[side] = 0;
            }
            const int needed = hardware[side] ? AUTOCROP_HARDWARE_FRAMES : AUTOCROP_PIXEL_FRAMES;
            if (++state->stable[side] >= needed)
            {
                state->applied[side] = band[side];
                state->stable[side] = 0;
                changed = true;
            }
        }
    }

    return changed;
}

#endif
