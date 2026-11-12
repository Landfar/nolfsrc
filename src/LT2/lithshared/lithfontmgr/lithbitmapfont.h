//*********************************************************************************
//*********************************************************************************
// Project:		Lith Tech Font Manager
// Purpose:		A general font method for bitmap and system fonts
//*********************************************************************************
// File:		LithBitmapFont.h
// Created:		April 7, 1999
// Updated:		July 15, 1999
// Author:		Andy Mattingly
//*********************************************************************************
//*********************************************************************************

#ifndef		_LITHTECH_BITMAP_FONT_H_
#define		_LITHTECH_BITMAP_FONT_H_

//*********************************************************************************

#include "lithfont.h"

//*********************************************************************************

class LithBitmapFont : public LithFont
{
	public:
		// Constructors and destructors
		LithBitmapFont();
		~LithBitmapFont();

		// Memory allocation and deallocation
		LTBOOL	Init(ILTClient *pClient, LITHFONTCREATESTRUCT *lfCS);
		void	Free();

		// Drawing / rendering
		void	Draw(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX = 0, int nY = 0, LITHFONTSAVEDATA *lfSD = LTNULL);

		// Formatting information
		void	GetTextExtents(char *szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y, int index = -1);
		void	GetPositionFromIndex(char *szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y);

		// State information
		uint8	GetState(LITHFONTSAVEDATA *lfSD = LTNULL);

		LTBOOL	IsChromaKeyed() {return m_bChromaKey;}
		LTBOOL	IsSystemFont()	{return LTFALSE;}

		// Manual drawing setup functions (without drawing)
		void	SetDrawData(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX = 0, int nY = 0, LITHFONTSAVEDATA *lfSD = LTNULL, int index = -1);
		void	ClearDrawData();

	protected:
		// The surface of the font and which groups are included in the graphic
		HSURFACE	m_pFont;				// Bitmap which stores the font
		uint32		m_nSurfWidth;			// Width of the bitmap surface
		uint32		m_nSurfHeight;			// Height of the bitmap surface
		uint32		m_nGroupFlags;			// Flags for which groups are available
		uint32		m_nAvgWidth;			// Average character width

		// Transparency color data
		HLTCOLOR	m_hTransColor;			// Transparency color for the font
		LTBOOL		m_bChromaKey;			// TRUE is the font uses a transparent color
											// FALSEE is the font uses a blending modes

		// Data needed to choose the correct character to draw
		LITHFONTOFFSETDATA	m_lfOD;			// Offset data for the rectangle placement
		LTRect				m_rRect;		// Rectangle to draw from within the font surface

		// Temporary save data used when no save structure is passed in
		LITHFONTSAVEDATA	m_temp_lfSD;

		// Temp Drawing data for the drawing methods
		HSURFACE			m_pDest;
		uint32				m_nDestSurfWidth;
		uint32				m_nDestSurfHeight;
		char				*m_szString;
		LITHFONTDRAWDATA	*m_lfDD;
		LITHFONTSAVEDATA	*m_lfSD;
		LITHFONTFORMATDATA	*m_lfFD;		// Temporary format data for the string
		uint32				m_dwFlags;
		uint8				m_byJustify;
		int					m_nX;
		int					m_nY;
		int					m_nExtraLetterSpace;
		uint32				m_nWaitLines;

	private:
		// Functions to handle the offset data of the font
		LTBOOL	LoadOffsets();
		LTBOOL	CalcOffsets(uint8 nPitch = 0);
		uint32	FindOffset(uint32 nStartPos);
		LTBOOL	SaveOffsets();

		// Function to calculate extra needed values
		LTBOOL	CalcAvgWidth();

	protected:
		// Function to determine whether a line of text should be drawn or not within the given RECT
		LTBOOL	ClipFont(LTRect &rcFont);
	
	// The follow functions are meant for manual drawing...
	// Otherwise, they should only be used internally
	public:
		// Drawing data functions
		uint32	GetStringLength(uint32 nIndex1 = 0, uint32 nIndex2 = 0, LTBOOL bIgnorLastSpace = LTFALSE);
		void	SetFormatData(uint32 nFormatWidth);

		// Drawing method functions
		void	Draw_Method_Normal();
		void	Draw_Method_Timed();

		// Line drawing functions
		void	_Draw(int nX, int nY, uint32 nIndex1 = 0, uint32 nIndex2 = 0);
		void	_DrawSolid(int nX, int nY, uint32 nIndex1 = 0, uint32 nIndex2 = 0);
#ifdef NOLFSRC_MODERN

	// HD-fonts stage 1: every bitmap font draws its glyphs s times larger
	// (nearest-neighbour, an integer s keeps them crisp) and measures in the
	// same enlarged pixels. The game sets s from the screen (TextScale).
	public:
		static void	SetGlobalScale(int nScale);
		static int	GetGlobalScale()	{ return s_nScale; }

		// A menu font measures in the menus' 640x480 layout units (the glyph
		// pixels at scale 1): extents, height and wrap widths are divided by
		// (or, for a wrap width it is given, multiplied by) the scale, while
		// it still draws at the full scale.
		void	SetLayoutMetrics(LTBOOL bLayout)	{ m_bLayoutMetrics = bLayout; }
		int		LayoutScale()		{ ApplyScale(); return m_bLayoutMetrics ? m_nScale : 1; }
		int		LayoutHeight()		{ return m_nHeight / LayoutScale(); }

		// HD fonts, stage 2: a font drawn at s > 1 draws one to one from an
		// image s times larger rather than stretching the retail glyphs'
		// pixels (TextSmooth): 1, the default, its HD image, drawn from a
		// free outline font (LoadHDFile), or where it has none the retail
		// glyphs enlarged with smooth edges (BuildHD); 2 always the latter;
		// 0 neither (stage 1's enlarged pixels).
		static void	SetGlobalSmooth(int nSmooth);

	private:
		void	ApplyScale();
		void	BlitGlyph(int nX, int nY, uint32 nChar, LTBOOL bSolid);
		void	BuildHD();
		LTBOOL	LoadHDFile(int s);
		void	FreeHD();

		static int	s_nScale;
		static int	s_nSmooth;
		HSURFACE	m_pFontHD;						// the glyphs at m_nHDScale, smoothed
		int			m_nHDScale;						// 0: none
		int			m_nHDSmooth;					// the smoothing it was made with
		uint32		m_aHDX[LTF_NUM_TOTAL_CHARS];	// each glyph's top-left in it
		uint32		m_aHDY[LTF_NUM_TOTAL_CHARS];
		int			m_nScale;						// the scale the metrics below are at
		uint32		m_aBaseWidths[LTF_NUM_TOTAL_CHARS];	// m_lfOD.pWidths at scale 1
		uint32		m_nBaseAvgWidth;				// m_nAvgWidth at scale 1
		LTBOOL		m_bLayoutMetrics;				// measures in layout units (menus)
#endif
};

//*********************************************************************************

#endif