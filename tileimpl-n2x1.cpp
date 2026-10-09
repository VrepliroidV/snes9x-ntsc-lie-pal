/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#define _TILEIMPL_CPP_
#include "tileimpl.h"

namespace TileImpl {

	template<class MATH, class BPSTART>
	void Normal2x1Base<MATH, BPSTART>::Draw(int N, int M, uint32 Offset, uint32 OffsetInLine, uint8 Pix, uint8 Z1, uint8 Z2)
	{
        (void) OffsetInLine;
        int scale = IPPU.QuadWidthPixels ? 4 : 2;
        uint32 pos = Offset + scale * N;
        if (Z1 > GFX.DB[pos] && M)
        {
            uint16 color = MATH::Calc(GFX.ScreenColors[Pix], GFX.SubScreen[pos], GFX.SubZBuffer[pos]);
            for (int i = 0; i < scale; i++) { GFX.S[pos+i] = color; GFX.DB[pos+i] = Z2; }
        }
	}


	// normal double width
	template struct Renderers<DrawTile16, Normal2x1>;
	template struct Renderers<DrawClippedTile16, Normal2x1>;
	template struct Renderers<DrawMosaicPixel16, Normal2x1>;
	template struct Renderers<DrawBackdrop16, Normal2x1>;
	template struct Renderers<DrawMode7MosaicBG1, Normal2x1>;
	template struct Renderers<DrawMode7BG1, Normal2x1>;
	template struct Renderers<DrawMode7MosaicBG2, Normal2x1>;
	template struct Renderers<DrawMode7BG2, Normal2x1>;

	// normal double width interlace
	template struct Renderers<DrawTile16, Interlace>;
	template struct Renderers<DrawClippedTile16, Interlace>;
	template struct Renderers<DrawMosaicPixel16, Interlace>;
	//template struct Renderers<DrawBackdrop16, Normal2x1>;
	//template struct Renderers<DrawMode7MosaicBG1, Normal2x1>;
	//template struct Renderers<DrawMode7BG1, Normal2x1>;
	//template struct Renderers<DrawMode7MosaicBG2, Normal2x1>;
	//template struct Renderers<DrawMode7BG2, Normal2x1>;

} // namespace TileImpl
