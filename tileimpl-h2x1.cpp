/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#define _TILEIMPL_CPP_
#include "tileimpl.h"

namespace TileImpl {

	template<class MATH, class BPSTART>
	void HiresBase<MATH, BPSTART>::Draw(int N, int M, uint32 Offset, uint32 OffsetInLine, uint8 Pix, uint8 Z1, uint8 Z2)
	{
        int repeat = IPPU.QuadWidthPixels ? 2 : 1;
        uint32 pos = Offset + 2 * repeat * N;
        uint32 in_line = OffsetInLine + 2 * repeat * N;
        if (Z1 > GFX.DB[pos] && M)
        {
            uint16 main_color = MATH::Calc(GFX.ScreenColors[Pix],GFX.SubScreen[pos],GFX.SubZBuffer[pos]);
            for (int i=0; i<repeat; i++) GFX.S[pos+repeat+i]=main_color;
            if (in_line != (uint32)(SNES_WIDTH-1)*2*repeat)
            {
                uint16 next = MATH::Calc(GFX.ClipColors ? 0 : GFX.SubScreen[pos+2*repeat],GFX.RealScreenColors[Pix],GFX.SubZBuffer[pos]);
                for (int i=0; i<repeat; i++) GFX.S[pos+2*repeat+i]=next;
            }
            if (in_line == 0 || in_line == GFX.RealPPL)
            {
                uint16 first = MATH::Calc(GFX.ClipColors ? 0 : GFX.SubScreen[pos],GFX.RealScreenColors[Pix],GFX.SubZBuffer[pos]);
                for (int i=0; i<repeat; i++) GFX.S[pos+i]=first;
            }
            for (int i=0; i<2*repeat; i++) GFX.DB[pos+i]=Z2;
        }
	}


	// hires double width
	template struct Renderers<DrawTile16, Hires>;
	template struct Renderers<DrawClippedTile16, Hires>;
	template struct Renderers<DrawMosaicPixel16, Hires>;
	template struct Renderers<DrawBackdrop16, Hires>;
	template struct Renderers<DrawMode7MosaicBG1, Hires>;
	template struct Renderers<DrawMode7BG1, Hires>;
	template struct Renderers<DrawMode7MosaicBG2, Hires>;
	template struct Renderers<DrawMode7BG2, Hires>;

	// hires double width interlace
	template struct Renderers<DrawTile16, HiresInterlace>;
	template struct Renderers<DrawClippedTile16, HiresInterlace>;
	template struct Renderers<DrawMosaicPixel16, HiresInterlace>;
	//template struct Renderers<DrawBackdrop16, Hires>;
	//template struct Renderers<DrawMode7MosaicBG1, Hires>;
	//template struct Renderers<DrawMode7BG1, Hires>;
	//template struct Renderers<DrawMode7MosaicBG2, Hires>;
	//template struct Renderers<DrawMode7BG2, Hires>;

} // namespace TileImpl
