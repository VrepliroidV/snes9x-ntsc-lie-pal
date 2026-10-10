/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
     Licensed under the Snes9x License. See LICENSE.
\*****************************************************************************/
#ifndef SNES9X_TILE_MODE7_HD_H
#define SNES9X_TILE_MODE7_HD_H
namespace TileImpl {
// HD samples use the same window, priority and colour math pipeline as native M7.
template<class MATH> struct Mode7HDPixel
{
    enum { Pitch = 1 };
    static void DrawColor(uint32 pos, uint16 color, uint8 priority)
    {
        if (priority > GFX.DB[pos])
        {
            GFX.S[pos] = MATH::Calc(GFX.ClipColors ? 0 : color, GFX.SubScreen[pos], GFX.SubZBuffer[pos]);
            GFX.DB[pos] = priority;
        }
    }
};

template<class PIXEL, class OP> struct DrawMode7HD
{
    typedef void (*call_t)(uint32, uint32, int);
    static uint8 Sample(int x, int y)
    {
        if (!PPU.Mode7Repeat) { x &= 1023; y &= 1023; }
        else if ((x | y) & ~1023)
        {
            if (PPU.Mode7Repeat != 3) return 0;
            return Memory.VRAM[1 + ((y & 7) << 4) + ((x & 7) << 1)];
        }
        uint32 tile = Memory.VRAM[((y & ~7) << 5) + ((x >> 2) & ~1)];
        return Memory.VRAM[1 + (tile << 7) + ((y & 7) << 4) + ((x & 7) << 1)];
    }
    static uint16 Filter(uint16 a, uint16 b, uint16 c, uint16 d, unsigned fx, unsigned fy)
    {
        unsigned w[4] = {(256-fx)*(256-fy), fx*(256-fy), (256-fx)*fy, fx*fy};
        uint16 colors[4] = {a,b,c,d};
        unsigned red=0, green=0, blue=0;
        for (int i=0; i<4; i++)
        {
            red += ((colors[i] >> 11) & 31) * w[i];
            green += ((colors[i] >> 5) & 63) * w[i];
            blue += (colors[i] & 31) * w[i];
        }
        return (uint16)(((red >> 16) << 11) | ((green >> 16) << 5) | (blue >> 16));
    }
    static void Draw(uint32 left, uint32 right, int depth)
    {
        uint16 *palette = OP::DCMODE() ? DirectColourMaps[0] : IPPU.ScreenColors;
        int scale = IPPU.QuadWidthPixels ? 4 : (IPPU.DoubleWidthPixels ? 2 : 1);
        int sample_scale = Settings.Mode7Hires ? Settings.Mode7Hires : 1;
        // A previously promoted frame may stay at 4x after the option changes.
        if (sample_scale > scale) sample_scale = scale;
        for (uint32 line=GFX.StartY; line<=GFX.EndY; line++)
        {
            const SLineMatrixData &m = LineMatrixData[line];
            int ho = (int16)(m.M7HOFS << 3) >> 3;
            int vo = (int16)(m.M7VOFS << 3) >> 3;
            int cx = (int16)(m.CentreX << 3) >> 3;
            int cy = (int16)(m.CentreY << 3) >> 3;
            int y = PPU.Mode7VFlip ? 255 - (int)(line+1) : (int)(line+1);
            int xx = CLIP_10_BIT_SIGNED(ho-cx), yy = CLIP_10_BIT_SIGNED(vo-cy);
            int bb = ((m.MatrixB*y)&~63) + ((m.MatrixB*yy)&~63) + cx*256;
            int dd = ((m.MatrixD*y)&~63) + ((m.MatrixD*yy)&~63) + cy*256;
            for (uint32 x=left; x<right; x++)
                for (int sub=0; sub<sample_scale; sub++)
                {
                    int sx = (PPU.Mode7HFlip ? (int)(right-1-(x-left)) : (int)x);
                    int direction = PPU.Mode7HFlip ? -1 : 1;
                    // Keep fractional matrix steps before division, including negative slopes.
                    int xf = m.MatrixA*sx + ((m.MatrixA*xx)&~63) + bb + direction*m.MatrixA*sub/sample_scale;
                    int yf = m.MatrixC*sx + ((m.MatrixC*xx)&~63) + dd + direction*m.MatrixC*sub/sample_scale;
                    uint8 b = Sample(xf >> 8, yf >> 8);
                    if (!(b & OP::MASK)) continue;
                    uint16 color = palette[b & OP::MASK];
                    if (Settings.Mode7HiresBilinear)
                    {
                        // Stable centres suppress the half-texel shimmer; smooth uses full fractions.
                        if (Settings.Mode7HiresBilinear == 1) { xf -= 128; yf -= 128; }
                        int tx=xf >> 8, ty=yf >> 8;
                        uint8 p[4] = {Sample(tx,ty), Sample(tx+1,ty), Sample(tx,ty+1), Sample(tx+1,ty+1)};
                        // Preserve transparent and EXTBG priority boundaries; do not bleed across them.
                        bool same = true;
                        for (int i=0; i<4; i++)
                            if (!(p[i]&OP::MASK) || OP::Z1(depth,p[i]) != OP::Z1(depth,b)) same=false;
                        if (same) color=Filter(palette[p[0]&OP::MASK],palette[p[1]&OP::MASK],palette[p[2]&OP::MASK],palette[p[3]&OP::MASK],xf&255,yf&255);
                    }
                    int repeat=scale/sample_scale;
                    for (int i=0; i<repeat; i++)
                        PIXEL::DrawColor(line*GFX.PPL+x*scale+sub*repeat+i,color,OP::Z1(depth,b));
                }
        }
    }
};
template<class PIXEL> struct DrawMode7HD1 : DrawMode7HD<PIXEL,DrawMode7BG1_OP> {};
template<class PIXEL> struct DrawMode7HD2 : DrawMode7HD<PIXEL,DrawMode7BG2_OP> {};
}
#endif
