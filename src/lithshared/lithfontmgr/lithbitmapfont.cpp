//*********************************************************************************
//*********************************************************************************
// Project:		Lith Tech Font Manager
// Purpose:		A general font method for bitmap and system fonts
//*********************************************************************************
// File:		LithBitmapFont.cpp
//*********************************************************************************
//*********************************************************************************

#include "lithbitmapfont.h"
#ifdef NOLFSRC_MODERN
#include <math.h>
#include <stdio.h>
#include <string.h>
#endif

// NOTE: 18/25 functions are byte-identical to the shipped object. The other
// seven (Init's file helpers, the string measurers and the two _Draw workers)
// are near-matches within a few bytes: same instructions, calls, constants
// and control flow, but SP3-vs-SP6 differences in callee-saved register
// tie-breaking (ebx/ebp swapped), deferred-block emission order, and one
// spilled-vs-registered value (verified instruction-by-instruction).

//*********************************************************************************

#ifdef NOLFSRC_MODERN
// A new version, so a FontData.fnt the scan wrote before the modern build
// fixed HUD_CHOOSER.pcx's shadow colour (M-P4-10) is dropped and rescanned:
// the cache is matched on the name and width alone, which did not change.
#define LTF_OFFSETS_VERSION		1.02f
#else
#define LTF_OFFSETS_VERSION		1.01f
#endif

//*********************************************************************************

LithBitmapFont::LithBitmapFont()
{
	m_pFont = LTNULL;
	m_nSurfWidth = 0;
	m_nSurfHeight = 0;
	m_nGroupFlags = 0;
	m_nAvgWidth = 0;
	m_hTransColor = LTNULL;
	m_bChromaKey = LTFALSE;

	m_pDest = LTNULL;
	m_nDestSurfWidth = 0;
	m_nDestSurfHeight = 0;
	m_szString = "";
	m_lfDD = LTNULL;
	m_lfSD = LTNULL;
	m_lfFD = LTNULL;
	m_dwFlags = 0;
	m_byJustify = 0;
	m_nX = 0;
	m_nY = 0;
	m_nExtraLetterSpace = 0;
	m_nWaitLines = 1;
#ifdef NOLFSRC_MODERN
	m_nScale = 1;
	m_nBaseAvgWidth = 0;
	m_bLayoutMetrics = LTFALSE;
	memset(m_aBaseWidths, 0, sizeof(m_aBaseWidths));
	m_pFontHD = LTNULL;
	m_nHDScale = 0;
	m_nHDSmooth = 0;
	memset(m_aHDX, 0, sizeof(m_aHDX));
	memset(m_aHDY, 0, sizeof(m_aHDY));
#endif
}

//*********************************************************************************

LithBitmapFont::~LithBitmapFont()
{
	Free();
	LithFont::Free();
}

//*********************************************************************************

LTBOOL LithBitmapFont::Init(ILTClient *pClient, LITHFONTCREATESTRUCT *lfCS)
{
	if (LithFont::Init(pClient, lfCS))
	{
		// Create the font surface
		m_pFont = m_pClient->CreateSurfaceFromBitmap(lfCS->szFontBitmap);
		if (m_pFont)
		{
			if (lfCS->nGroupFlags)
			{
				m_pClient->GetSurfaceDims(m_pFont, &m_nSurfWidth, &m_nSurfHeight);
				m_nHeight = m_nSurfHeight;

				m_rRect.Init(0, 0, 0, m_nSurfHeight);

				strcpy(m_lfOD.szRefName, lfCS->szFontBitmap);
				strupr(m_lfOD.szRefName);

				m_nGroupFlags = lfCS->nGroupFlags;

				if (lfCS->bChromaKey)
				{
					m_bChromaKey = LTTRUE;
					m_hTransColor = lfCS->hTransColor;
				}

				if (!LoadOffsets())
				{
					if (!CalcOffsets(lfCS->nFixedPitch))
					{
						Free();
						return LTFALSE;
					}

					SaveOffsets();
				}

				CalcAvgWidth();
#ifdef NOLFSRC_MODERN
				// the metrics at scale 1, after the offsets cache is written
				memcpy(m_aBaseWidths, m_lfOD.pWidths, sizeof(m_aBaseWidths));
				m_nBaseAvgWidth = m_nAvgWidth;
				m_nScale = 1;
				ApplyScale();
#endif

				return LTTRUE;
			}
		}
	}

	return LTFALSE;
}

//*********************************************************************************

void LithBitmapFont::Free()
{
	if (m_pClient)
	{
#ifdef NOLFSRC_MODERN
		FreeHD();
#endif
		if (m_pFont)
		{
			m_pClient->DeleteSurface(m_pFont);
			m_pFont = LTNULL;
		}

		m_temp_lfSD.szPrevString = LTNULL;

		m_pClient = LTNULL;
	}
}

//*********************************************************************************

void LithBitmapFont::Draw(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD)
{
	if (!m_pClient)
		return;

	if (!pDest)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

	SetDrawData(pDest, szString, lfDD, nX, nY, lfSD, -1);

	if (m_dwFlags & LTF_DRAW_TIMED)
	{
		Draw_Method_Timed();
	}
	else
	{
		Draw_Method_Normal();
	}

	ClearDrawData();
}

//*********************************************************************************

void LithBitmapFont::GetTextExtents(char *szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y, int index)
{
	if (!m_pClient)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

	SetDrawData(LTNULL, szString, lfDD, 0, 0, LTNULL, index);

	y = (m_lfDD->nLineSpace + m_nHeight) * m_lfFD->nNumLines;

	x = 0;
	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		if (x < m_lfFD->pWidths[i])
			x = m_lfFD->pWidths[i];
	}
#ifdef NOLFSRC_MODERN

	// a menu font answers in layout units, rounded up
	if (m_bLayoutMetrics && m_nScale > 1)
	{
		x = (x + m_nScale - 1) / m_nScale;
		y = (y + m_nScale - 1) / m_nScale;
	}
#endif

	m_lfSD->byLastState = LTF_STATE_EXTENTS;

	ClearDrawData();
}

//*********************************************************************************

void LithBitmapFont::GetPositionFromIndex(char *szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y)
{
	if (!m_pClient)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

	GetTextExtents(szString, lfDD, x, y, index);

	if (y > 0)
	{
		y -= lfDD->nLineSpace;
#ifdef NOLFSRC_MODERN
		y -= LayoutHeight();
#else
		y -= m_nHeight;
#endif
	}
}

//*********************************************************************************

uint8 LithBitmapFont::GetState(LITHFONTSAVEDATA *lfSD)
{
	if (lfSD)
		return lfSD->byLastState;

	return m_temp_lfSD.byLastState;
}

//*********************************************************************************

void LithBitmapFont::SetDrawData(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD, int index)
{
	if (!m_pClient)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

#ifdef NOLFSRC_MODERN
	ApplyScale();
#endif
	m_pDest = pDest;
	m_szString = szString;

	if (index >= 0)
	{
		if (index < (int)strlen(szString))
			szString[index] = '\0';
	}

	m_lfDD = lfDD;
	m_dwFlags = lfDD->dwFlags;
	m_byJustify = lfDD->byJustify;
	m_nX = nX;
	m_nY = nY;

	m_lfSD = lfSD ? lfSD : &m_temp_lfSD;

	m_nWaitLines = (lfDD->nNumWaitLines > 1) ? lfDD->nNumWaitLines : 1;

	m_nDestSurfWidth = 0;
	m_nDestSurfHeight = 0;
	if (m_pDest)
		m_pClient->GetSurfaceDims(m_pDest, &m_nDestSurfWidth, &m_nDestSurfHeight);

	m_lfFD = &m_lfSD->lfFD;

	if (m_dwFlags & LTF_USE_OFFSETS)
	{
		m_nX += lfDD->nXOffset;
		m_nY += lfDD->nYOffset;
	}

	// Only reformat the string if something has changed
	if (m_lfDD->bForceFormat || !m_lfSD->szPrevString || m_lfSD->szPrevString != szString)
	{
		m_lfSD->szPrevString = szString;

#ifdef NOLFSRC_MODERN
		m_nExtraLetterSpace = m_lfDD->nLetterSpace * m_nScale;
#else
		m_nExtraLetterSpace = m_lfDD->nLetterSpace;
#endif

#ifdef NOLFSRC_MODERN
		// a menu font's wrap width is in layout units
		if (m_dwFlags & LTF_DRAW_FORMATTED)
			SetFormatData(m_lfDD->dwFormatWidth * (m_bLayoutMetrics ? m_nScale : 1));
		else
			SetFormatData(0);
#else
		if (m_dwFlags & LTF_DRAW_FORMATTED)
			SetFormatData(m_lfDD->dwFormatWidth);
		else
			SetFormatData(0);
#endif
	}

	if (m_pFont)
		m_pClient->SetSurfaceAlpha(m_pFont, m_lfDD->fAlpha);
#ifdef NOLFSRC_MODERN
	if (m_pFontHD)
		m_pClient->SetSurfaceAlpha(m_pFontHD, m_lfDD->fAlpha);
#endif
}

//*********************************************************************************

void LithBitmapFont::ClearDrawData()
{
	m_szString = "";

	m_pDest = LTNULL;
	m_lfDD = LTNULL;
	m_lfSD = LTNULL;
	m_lfFD = LTNULL;
}

//*********************************************************************************

// Byte-identical: bFound is set before fopen and returned by the failed-open path
// too (shipped zeroes its slot before the call), so the body sits in if (pFile).
LTBOOL LithBitmapFont::LoadOffsets()
{
	if (!m_pClient)
		return LTFALSE;

	LTBOOL bFound = LTFALSE;

	FILE *pFile = fopen(LTF_OFFSETS_FILENAME, "rb");
	if (pFile)
	{
		// Check the version of the file
		LTFLOAT fVersion;
		fread(&fVersion, sizeof(LTFLOAT), 1, pFile);

		if (fVersion != LTF_OFFSETS_VERSION)
		{
			fclose(pFile);
			remove(LTF_OFFSETS_FILENAME);
			return LTFALSE;
		}

		// Read the number of fonts in the file
		uint32 nNumFonts;
		fread(&nNumFonts, sizeof(uint32), 1, pFile);

		LITHFONTOFFSETDATA *pData = new LITHFONTOFFSETDATA[nNumFonts];

		fread(pData, sizeof(LITHFONTOFFSETDATA), nNumFonts, pFile);

		// Find the font in the list
		for (int i = 0; i < (int)nNumFonts; i++)
		{
			if (_mbscmp((unsigned char*)pData[i].szRefName, (unsigned char*)m_lfOD.szRefName) == 0)
			{
				if (m_nSurfWidth == pData[i].pOffsets[LTF_NUM_TOTAL_CHARS])
				{
					m_lfOD = pData[i];
					bFound = LTTRUE;
				}
			}
		}

		delete[] pData;
		fclose(pFile);
	}

	return bFound;
}

//*********************************************************************************

// Byte-identical to the shipped lib. One function-scope nNumChars serves both the
// fixed-pitch total and each group's char count (shipped keeps both in edi, and
// nGroupFlag = 0 is the xor ebx); the per-char store is ONE ternary (both arms
// share the store, and the strength-reduced pointer is based at [nCurChar-1]).
LTBOOL LithBitmapFont::CalcOffsets(uint8 nPitch)
{
	if (!m_pClient || !m_pFont || !m_nSurfWidth || !m_nSurfHeight)
		return LTFALSE;

	int nCurChar = 1;
	int nLastChar = 0;
	LTBOOL bFixed = LTFALSE;
	uint32 nGroupFlag = 0;
	uint32 nNumChars = 0;

	m_lfOD.pOffsets[0] = 0;

	if (nPitch)
	{
		// Verify that the fixed pitch value is correct
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_1)	nNumChars += LTF_NUM_SYMBOLS_1;
		if (m_nGroupFlags & LTF_INCLUDE_NUMBERS)	nNumChars += LTF_NUM_NUMBERS;
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_2)	nNumChars += LTF_NUM_SYMBOLS_2;
		if (m_nGroupFlags & LTF_INCLUDE_UPPERCASE)	nNumChars += LTF_NUM_LETTERS;
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_3)	nNumChars += LTF_NUM_SYMBOLS_3;
		if (m_nGroupFlags & LTF_INCLUDE_LOWERCASE)	nNumChars += LTF_NUM_LETTERS;
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_4)	nNumChars += LTF_NUM_SYMBOLS_4;

		if (m_nSurfWidth / nNumChars != nPitch)
			m_pClient->CPrint("LithFontMgr! ERROR!! - A fixed pitch calculation was incorrect on font %s", m_szRefName);
		else
			bFixed = LTTRUE;
	}

	for (int i = 0; i < LTF_INCLUDE_TOTAL_GROUPS; i++)
	{
		switch (i)
		{
			case 0:	nGroupFlag = LTF_INCLUDE_SYMBOLS_1;	nNumChars = LTF_NUM_SYMBOLS_1;	break;
			case 1:	nGroupFlag = LTF_INCLUDE_NUMBERS;	nNumChars = LTF_NUM_NUMBERS;		break;
			case 2:	nGroupFlag = LTF_INCLUDE_SYMBOLS_2;	nNumChars = LTF_NUM_SYMBOLS_2;	break;
			case 3:	nGroupFlag = LTF_INCLUDE_UPPERCASE;	nNumChars = LTF_NUM_LETTERS;		break;
			case 4:	nGroupFlag = LTF_INCLUDE_SYMBOLS_3;	nNumChars = LTF_NUM_SYMBOLS_3;	break;
			case 5:	nGroupFlag = LTF_INCLUDE_LOWERCASE;	nNumChars = LTF_NUM_LETTERS;		break;
			case 6:	nGroupFlag = LTF_INCLUDE_SYMBOLS_4;	nNumChars = LTF_NUM_SYMBOLS_4;	break;
		}

		nLastChar += nNumChars;

		if (m_nGroupFlags & nGroupFlag)
		{
			for (; nCurChar <= nLastChar; nCurChar++)
			{
				m_lfOD.pOffsets[nCurChar] = bFixed ? m_lfOD.pOffsets[nCurChar-1] + nPitch : FindOffset(m_lfOD.pOffsets[nCurChar-1]);
			}
		}
		else
		{
			for (; nCurChar <= nLastChar; nCurChar++)
			{
				m_lfOD.pOffsets[nCurChar] = m_lfOD.pOffsets[nCurChar-1];
			}
		}
	}

	// Calculate the widths of each character
	for (int j = 0; j < LTF_NUM_TOTAL_CHARS; j++)
	{
		m_lfOD.pWidths[j] = m_lfOD.pOffsets[j+1] - m_lfOD.pOffsets[j];

		if (m_lfOD.pWidths[j] >= m_nSurfWidth)
			return LTFALSE;
	}

	return LTTRUE;
}

//*********************************************************************************

// NEAR (286 vs 288). Prologue and exits match: the zero locals are declared before
// the colour components, and m_nSurfWidth is returned by a single exit at the end
// (the space-found returns sit inside the loop). Left: shipped sinks the solid-pixel
// bodies after the loop latch (tests, then spaceStart/spaceEnd/found bodies) where
// ours keeps them inline; not moved by solid-first order, if/else vs continue,
// per-arm breaks, nested !bInSpace, a while y loop.
// Also measured (f4): the compare/if graph is identical to shipped; only block layout
// differs. Gotos placing the bodies in shipped order, do/while and while y loops,
// flags threaded out of the loop, and every if/else spelling of the solid arm all
// collapse to the same few layouts. Routing the colour test through a bool (a local
// or an inline helper, with or without GetPixel inside it) flips the latch to
// shipped's `jae exit; jmp top`, but the solid bodies stay inline, so size is still 286.
uint32 LithBitmapFont::FindOffset(uint32 nStartPos)
{
	uint32 nSpaceStart = 0;
	uint32 nSpaceEnd = 0;
	LTBOOL bFoundLetter = LTFALSE;
	LTBOOL bInSpace = LTFALSE;

	uint32 nRed = GETR(m_hTransColor);
	uint32 nGreen = GETG(m_hTransColor);
	uint32 nBlue = GETB(m_hTransColor);

	while (1)
	{
		for (uint32 y = 0; y < m_nSurfHeight; y++)
		{
			HLTCOLOR nColor;
			m_pClient->GetPixel(m_pFont, nStartPos, y, &nColor);

			if (GETR(nColor) == nRed && GETG(nColor) == nGreen && GETB(nColor) == nBlue)
			{
				// This pixel is transparent
				if (bFoundLetter && !bInSpace && y == m_nSurfHeight - 1)
				{
					nSpaceStart = nStartPos;
					bInSpace = LTTRUE;
					break;
				}

				continue;
			}

			// This pixel is solid
			if (bInSpace)
			{
				nSpaceEnd = nStartPos - 1;
				bInSpace = LTFALSE;
			}
			else if (!bFoundLetter)
			{
				bFoundLetter = LTTRUE;
			}

			break;
		}

		nStartPos++;

		if (nSpaceStart && nSpaceEnd)
		{
			if (m_nGroupFlags & LTF_INCLUDE_FIXED_END)
				return nSpaceStart;

			return nSpaceStart + (nSpaceEnd - nSpaceStart) / 2;
		}

		if (nStartPos >= m_nSurfWidth)
			break;
	}

	return m_nSurfWidth;
}

//*********************************************************************************

LTBOOL LithBitmapFont::SaveOffsets()
{
	if (!m_pClient)
		return LTFALSE;

	LTBOOL bFound = LTFALSE;
	LTFLOAT fVersion = LTF_OFFSETS_VERSION;
	uint32 nNumFonts = 0;
	LITHFONTOFFSETDATA *pData = LTNULL;

	FILE *pFile = fopen(LTF_OFFSETS_FILENAME, "rb");
	if (pFile)
	{
		fread(&fVersion, sizeof(LTFLOAT), 1, pFile);
		fread(&nNumFonts, sizeof(uint32), 1, pFile);

		pData = new LITHFONTOFFSETDATA[nNumFonts];

		fread(pData, sizeof(LITHFONTOFFSETDATA), nNumFonts, pFile);

		// Replace the data for this font if it's already in the list
		for (int i = 0; i < (int)nNumFonts; i++)
		{
			if (_mbscmp((unsigned char*)pData[i].szRefName, (unsigned char*)m_lfOD.szRefName) == 0)
			{
				pData[i] = m_lfOD;
				bFound = LTTRUE;
			}
		}

		fclose(pFile);
	}

	fVersion = LTF_OFFSETS_VERSION;
	remove(LTF_OFFSETS_FILENAME);

	pFile = fopen(LTF_OFFSETS_FILENAME, "wb");
	if (pFile)
	{
		if (bFound)
		{
			fseek(pFile, 0, SEEK_SET);
			fwrite(&fVersion, sizeof(LTFLOAT), 1, pFile);
			fwrite(&nNumFonts, sizeof(uint32), 1, pFile);
			fwrite(pData, sizeof(LITHFONTOFFSETDATA), nNumFonts, pFile);
		}
		else
		{
			nNumFonts++;
			fseek(pFile, 0, SEEK_SET);
			fwrite(&fVersion, sizeof(LTFLOAT), 1, pFile);
			fwrite(&nNumFonts, sizeof(uint32), 1, pFile);
			fwrite(pData, sizeof(LITHFONTOFFSETDATA), nNumFonts - 1, pFile);
			fwrite(&m_lfOD, sizeof(LITHFONTOFFSETDATA), 1, pFile);
		}

		fclose(pFile);
	}

	if (pData)
		delete[] pData;

	return LTTRUE;
}

//*********************************************************************************

LTBOOL LithBitmapFont::CalcAvgWidth()
{
	uint32 nTotal = 0;
	uint32 nCount = 0;

	for (int i = 0; i < LTF_NUM_TOTAL_CHARS; i++)
	{
		if (m_lfOD.pWidths[i])
		{
			nTotal += m_lfOD.pWidths[i];
			nCount++;
		}
	}

	m_nAvgWidth = nTotal / nCount;

	return LTTRUE;
}

//*********************************************************************************

// The index is advanced inside each arm (before the add on the space arm,
// straight after nChar on the other): shipped has an `inc` in both arms ahead of
// the shared loop test, and this is also what gives nWidth ebx, m_szString ebp.
uint32 LithBitmapFont::GetStringLength(uint32 nIndex1, uint32 nIndex2, LTBOOL bIgnorLastSpace)
{
	if (!m_szString)
		return 0;

	uint32 nWidth = 0;
	uint32 nLen = strlen(m_szString);

	if (!nLen || nIndex1 > nLen || nIndex2 > nLen)
		return 0;

	if (nIndex1 == 0 && nIndex2 == 0)
		nIndex2 = nLen;

	while (nIndex1 < nIndex2)
	{
		char c = m_szString[nIndex1];

		if (c == ' ' && (!bIgnorLastSpace || nIndex1 != nIndex2 - 1))
		{
			nIndex1++;
			nWidth += m_nExtraLetterSpace + m_nAvgWidth;
		}
		else
		{
			int nChar = c - 33;
			nIndex1++;

			if ((uint32)nChar < LTF_NUM_TOTAL_CHARS)
				nWidth += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
		}
	}

	return nWidth;
}

//*********************************************************************************

void LithBitmapFont::SetFormatData(uint32 nFormatWidth)
{
	if (!m_lfFD)
		return;

	uint32 nIndex = 0;
	uint32 nLastSpace = 0;
	uint32 nWidth = 0;
	uint32 nLastWidth = 0;

	m_lfFD->nNumLines = 0;
	m_lfFD->pIndex[0] = 0;

	while (m_szString[nIndex] != '\0')
	{
		char c = m_szString[nIndex];

		if (c == ' ')
		{
			nLastWidth = nWidth;
			nWidth += m_nExtraLetterSpace + m_nAvgWidth;
			nLastSpace = nIndex + 1;
		}
		else if (c == '\n')
		{
			// Newline... break the line here
			nIndex++;
			nLastWidth = 0;

			m_lfFD->pIndex[m_lfFD->nNumLines+1] = nIndex;
			m_lfFD->pWidths[m_lfFD->nNumLines] = nWidth;
			m_lfFD->nNumLines++;

			nWidth = 0;
			nLastSpace = 0;

			continue;
		}
		else
		{
			int nChar = c - 33;

			if ((uint32)nChar < LTF_NUM_TOTAL_CHARS)
				nWidth += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
		}

		if (nWidth > nFormatWidth && nLastSpace && nFormatWidth)
		{
			// This line is too wide... break the line at the last space
			m_lfFD->pIndex[m_lfFD->nNumLines+1] = nLastSpace;
			m_lfFD->pWidths[m_lfFD->nNumLines] = nLastWidth;
			m_lfFD->nNumLines++;

			nLastWidth = 0;
			nWidth = 0;
			nIndex = nLastSpace;
			nLastSpace = 0;
		}
		else
		{
			nIndex++;
		}
	}

	if (nWidth)
	{
		// Add the last line
		m_lfFD->pIndex[m_lfFD->nNumLines+1] = nIndex;
		m_lfFD->pWidths[m_lfFD->nNumLines] = nWidth;
		m_lfFD->nNumLines++;
	}

	m_lfFD->nNumChars = m_lfFD->pIndex[m_lfFD->nNumLines];

	// Setup the timing data
	if (m_dwFlags & LTF_DRAW_TIMED)
	{
		m_lfSD->fStartDrawTime = m_pClient->GetTime();
		m_lfSD->fTotalDrawTime = 0;

		if (m_dwFlags & LTF_TIMED_LETTERS)
			m_lfSD->fTotalDrawTime += m_lfFD->nNumChars * m_lfDD->fLetterDelay;

		if (m_dwFlags & LTF_TIMED_LINES)
			m_lfSD->fTotalDrawTime += (m_lfFD->nNumLines - 1) * m_lfDD->fLineDelay;

		if (m_dwFlags & LTF_TIMED_SCROLL)
			m_lfSD->fTotalDrawTime += (m_lfFD->nNumLines - m_nWaitLines) * m_lfDD->fLineScrollTime;

		for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
		{
			m_lfFD->pTimes[i] = (m_lfFD->pIndex[i+1] - m_lfFD->pIndex[i]) * m_lfDD->fLetterDelay;
		}
	}
}

//*********************************************************************************

void LithBitmapFont::Draw_Method_Normal()
{
	if (!m_lfFD)
		return;

	int nLineHeight = m_lfDD->nLineSpace + m_nHeight;

	void (LithBitmapFont::*pDrawFn)(int, int, uint32, uint32) = &LithBitmapFont::_Draw;

	if (m_dwFlags & LTF_DRAW_SOLID)
		pDrawFn = &LithBitmapFont::_DrawSolid;

	if (m_dwFlags & LTF_EXTRA_NEWLINE_UP)
		nLineHeight = -nLineHeight;

	if (m_dwFlags & LTF_EXTRA_LOCKLAST)
		m_nY -= m_lfFD->nNumLines * nLineHeight;

	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		uint32 nWidth = m_lfFD->pWidths[i];
		uint32 nStartIndex = m_lfFD->pIndex[i];
		uint32 nEndIndex = m_lfFD->pIndex[i+1];

		int x;
		switch (m_byJustify)
		{
			case LTF_JUSTIFY_LEFT:		x = m_nX;					break;
			case LTF_JUSTIFY_RIGHT:		x = m_nX - nWidth;			break;
			case LTF_JUSTIFY_CENTER:	x = m_nX - nWidth/2;		break;
		}

		m_rRect.top = 0;
		m_rRect.bottom = m_nSurfHeight;

		LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

		if (ClipFont(rcDest))
		{
			(this->*pDrawFn)(x, rcDest.top, nStartIndex, nEndIndex);
		}

		m_nY += nLineHeight;
	}

	m_lfSD->byLastState = LTF_STATE_DRAW;
}

//*********************************************************************************

// NEAR (1309 vs 1301). Measured: the second loop reuses i (shipped keeps i, j and
// the pTimes walker in one slot); one loop-scope x (edi, frame 0x4c not 0x50); a
// GetStringLength call per arm with a constant flag (shipped pushes $1/$1/$0).
// Residual is allocation: shipped gives nStartIndex ebp and spills nEndIndex, we
// do the reverse. Not moved by declaration order (all 6), int types, register,
// a function-scope pair, a named count/percent, the inverted if, a while loop.
// Also measured (f4), none of which moved it: const locals, a pIndex pointer local,
// reading pIndex[i+1] at the clamp, compare or full draw (reloads), a clamp then
// ==/ternary flag, nIndex = Start then +=, a commuted compare, Start or End
// declared per arm, and new loop variables for loops 2/3 (VC6 splits webs anyway).
// An int count local gives 1301 bytes, but only because it drops the uint->float
// high-dword store; the allocation is unchanged.
void LithBitmapFont::Draw_Method_Timed()
{
	if (!m_lfFD)
		return;

	if (!m_lfDD)
		return;

	int nCurLine = m_lfFD->nNumLines;
	int nLineHeight = m_lfDD->nLineSpace + m_nHeight;

	LTFLOAT fElapsed = m_pClient->GetTime() - m_lfSD->fStartDrawTime;
	LTFLOAT fLineTime = 0.0f;

	void (LithBitmapFont::*pDrawFn)(int, int, uint32, uint32) = &LithBitmapFont::_Draw;

	// Figure out which line we're currently drawing
	LTFLOAT fTime = 0.0f;
	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		if (m_dwFlags & LTF_TIMED_LETTERS)
			fTime += m_lfFD->pTimes[i];

		if (m_dwFlags & LTF_TIMED_LINES)
			fTime += m_lfDD->fLineDelay;

		if (m_dwFlags & LTF_TIMED_SCROLL)
		{
			if (i >= m_nWaitLines - 1)
				fTime += m_lfDD->fLineScrollTime;
		}

		if (fTime > fElapsed)
		{
			nCurLine = i + 1;
			break;
		}
	}

	// Figure out the time at which the current line started
	for (i = 1; i < (uint32)nCurLine; i++)
	{
		if (m_dwFlags & LTF_TIMED_LETTERS)
			fLineTime += m_lfFD->pTimes[i-1];

		if (m_dwFlags & LTF_TIMED_LINES)
			fLineTime += m_lfDD->fLineDelay;

		if (m_dwFlags & LTF_TIMED_SCROLL)
		{
			if (i - 1 >= m_nWaitLines - 1)
				fLineTime += m_lfDD->fLineScrollTime;
		}
	}

	if (m_dwFlags & LTF_DRAW_SOLID)
		pDrawFn = &LithBitmapFont::_DrawSolid;

	if (m_dwFlags & LTF_EXTRA_NEWLINE_UP)
		nLineHeight = -nLineHeight;

	if (m_dwFlags & LTF_EXTRA_LOCKLAST)
	{
		if (nCurLine >= m_nWaitLines)
		{
			LTFLOAT fScrollOffset = 0.0f;

			if (m_dwFlags & LTF_TIMED_SCROLL)
			{
				if (m_lfDD->fLineScrollTime > 0.0f && nCurLine != (int)m_lfFD->nNumLines)
				{
					LTFLOAT fScrollTime = fElapsed - fLineTime - m_lfFD->pTimes[nCurLine-1] - m_lfDD->fLineDelay;
					if (fScrollTime > 0.0f)
					{
						LTFLOAT fPercent = fScrollTime / m_lfDD->fLineScrollTime;
						if (fPercent > 1.0f)
							fPercent = 1.0f;

						fScrollOffset = fPercent * nLineHeight;
					}
				}
			}

			m_nY -= (nCurLine - m_nWaitLines) * nLineHeight + (int)fScrollOffset;
		}
	}

	// Draw each line
	for (i = 0; i < (uint32)nCurLine; i++)
	{
		uint32 nEndIndex = m_lfFD->pIndex[i+1];
		uint32 nStartIndex = m_lfFD->pIndex[i];
		uint32 nWidth = m_lfFD->pWidths[i];
		int x;

		if ((m_dwFlags & LTF_TIMED_LETTERS) && i == (uint32)nCurLine - 1 && fElapsed < m_lfSD->fTotalDrawTime)
		{
			// This line is still updating... draw a partial line
			uint32 nIndex = nStartIndex + (uint32)((fElapsed - fLineTime) / m_lfFD->pTimes[i] * (nEndIndex - nStartIndex));

			uint32 nLineWidth;

			if (nIndex > nEndIndex)
			{
				nIndex = nEndIndex;
				nLineWidth = GetStringLength(nStartIndex, nIndex, LTTRUE);
			}
			else if (nIndex == nEndIndex)
			{
				nLineWidth = GetStringLength(nStartIndex, nIndex, LTTRUE);
			}
			else
			{
				nLineWidth = GetStringLength(nStartIndex, nIndex, LTFALSE);
			}

			switch (m_byJustify)
			{
				case LTF_JUSTIFY_LEFT:		x = m_nX;					break;
				case LTF_JUSTIFY_RIGHT:		x = m_nX - nLineWidth;		break;
				case LTF_JUSTIFY_CENTER:	x = m_nX - nLineWidth/2;	break;
			}

			m_rRect.top = 0;
			m_rRect.bottom = m_nSurfHeight;

			LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

			if (ClipFont(rcDest))
			{
				(this->*pDrawFn)(x, rcDest.top, nStartIndex, nIndex);
			}
		}
		else
		{
			switch (m_byJustify)
			{
				case LTF_JUSTIFY_LEFT:		x = m_nX;				break;
				case LTF_JUSTIFY_RIGHT:		x = m_nX - nWidth;		break;
				case LTF_JUSTIFY_CENTER:	x = m_nX - nWidth/2;	break;
			}

			m_rRect.top = 0;
			m_rRect.bottom = m_nSurfHeight;

			LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

			if (ClipFont(rcDest))
			{
				(this->*pDrawFn)(x, rcDest.top, nStartIndex, nEndIndex);
			}
		}

		m_nY += nLineHeight;
	}

	// Set the current state
	if (fElapsed >= m_lfSD->fTotalDrawTime)
	{
		m_lfSD->byLastState = LTF_STATE_DRAW_FINISHED;
		return;
	}

	if (m_lfDD->fLineScrollTime != 0.0f)
	{
		if (fElapsed - fLineTime > m_lfFD->pTimes[nCurLine-1] + m_lfDD->fLineDelay)
		{
			m_lfSD->byLastState = LTF_STATE_DRAW_SCROLLING;
			return;
		}
	}

	if (m_lfDD->fLineDelay != 0.0f)
	{
		if (fElapsed - fLineTime > m_lfFD->pTimes[nCurLine-1])
		{
			m_lfSD->byLastState = LTF_STATE_DRAW_LINE_DELAY;
			return;
		}
	}

	m_lfSD->byLastState = LTF_STATE_DRAW_UPDATING;
}

//*********************************************************************************

// Each of the three arms advances nIndex1 itself (no for-increment), and nChar
// is unsigned (`ja`, not `jg`, on the range test). Together these give shipped's
// allocation: nIndex1 in ebx, nX in ebp.
void LithBitmapFont::_Draw(int nX, int nY, uint32 nIndex1, uint32 nIndex2)
{
	if (!m_pClient)
		return;

	if (!m_pDest)
		return;

	if (!m_pFont)
		return;

	if (!m_szString)
		return;

	if (nIndex1 >= nIndex2)
		return;

	while (nIndex1 < nIndex2)
	{
		char c = m_szString[nIndex1];
		uint32 nChar = c - 33;

		if (c == ' ')
		{
			nX += m_nExtraLetterSpace + m_nAvgWidth;
			nIndex1++;
		}
		else if (nChar <= LTF_NUM_TOTAL_CHARS - 1)
		{
			m_rRect.left = m_lfOD.pOffsets[nChar];
			m_rRect.right = m_lfOD.pOffsets[nChar+1];

#ifdef NOLFSRC_MODERN
			BlitGlyph(nX, nY, nChar, LTFALSE);
#else
			if (m_bChromaKey)
			{
				m_pClient->DrawSurfaceToSurfaceTransparent(m_pDest, m_pFont, &m_rRect, nX, nY, m_hTransColor);
			}
			else
			{
				m_pClient->DrawSurfaceToSurface(m_pDest, m_pFont, &m_rRect, nX, nY);
			}
#endif

			nX += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
			nIndex1++;
		}
		else
			nIndex1++;
	}
}

//*********************************************************************************

// Each of the three arms advances nIndex1 itself (no for-increment), and nChar
// is unsigned (`ja`, not `jg`, on the range test). Together these give shipped's
// allocation: nIndex1 in ebx, nX in ebp.
void LithBitmapFont::_DrawSolid(int nX, int nY, uint32 nIndex1, uint32 nIndex2)
{
	if (!m_pClient)
		return;

	if (!m_pDest)
		return;

	if (!m_pFont)
		return;

	if (!m_szString)
		return;

	if (nIndex1 >= nIndex2)
		return;

	while (nIndex1 < nIndex2)
	{
		char c = m_szString[nIndex1];
		uint32 nChar = c - 33;

		if (c == ' ')
		{
			nX += m_nExtraLetterSpace + m_nAvgWidth;
			nIndex1++;
		}
		else if (nChar <= LTF_NUM_TOTAL_CHARS - 1)
		{
			m_rRect.left = m_lfOD.pOffsets[nChar];
			m_rRect.right = m_lfOD.pOffsets[nChar+1];

#ifdef NOLFSRC_MODERN
			BlitGlyph(nX, nY, nChar, LTTRUE);
#else
			m_pClient->DrawSurfaceSolidColor(m_pDest, m_pFont, &m_rRect, nX, nY,
											 m_hTransColor, m_lfDD->hColor);
#endif

			nX += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
			nIndex1++;
		}
		else
			nIndex1++;
	}
}

//*********************************************************************************

LTBOOL LithBitmapFont::ClipFont(LTRect &rcFont)
{
	if (m_dwFlags & LTF_USE_CLIPRECT_Y)
	{
		if (m_dwFlags & LTF_USE_EXCLUSIVE_CLIP)
		{
			if (rcFont.top < m_lfDD->rcClip.top || rcFont.bottom > m_lfDD->rcClip.bottom)
				return LTFALSE;
		}
		else
		{
			if (rcFont.top > m_lfDD->rcClip.bottom || rcFont.bottom < m_lfDD->rcClip.top)
				return LTFALSE;

			if (rcFont.top < m_lfDD->rcClip.top)
			{
#ifdef NOLFSRC_MODERN
				m_rRect.top = (m_lfDD->rcClip.top - rcFont.top) / m_nScale;
#else
				m_rRect.top = m_lfDD->rcClip.top - rcFont.top;
#endif
				rcFont.top = m_lfDD->rcClip.top;
			}

			if (rcFont.bottom > m_lfDD->rcClip.bottom)
			{
#ifdef NOLFSRC_MODERN
				m_rRect.bottom += (m_lfDD->rcClip.bottom - rcFont.bottom) / m_nScale;
#else
				m_rRect.bottom += m_lfDD->rcClip.bottom - rcFont.bottom;
#endif
			}
		}
	}
	else
	{
		if (rcFont.bottom <= 0 || rcFont.top >= (int)m_nDestSurfHeight)
			return LTFALSE;
	}

	if (m_dwFlags & LTF_USE_CLIPRECT_X)
	{
		if (m_dwFlags & LTF_USE_EXCLUSIVE_CLIP)
		{
			if (rcFont.left < m_lfDD->rcClip.left || rcFont.right > m_lfDD->rcClip.right)
				return LTFALSE;
		}
	}
	else
	{
		if (rcFont.right <= 0 || rcFont.left >= (int)m_nDestSurfWidth)
			return LTFALSE;
	}

	return LTTRUE;
}
#ifdef NOLFSRC_MODERN

//*********************************************************************************
// HD-fonts stage 1 and 2 (see lithbitmapfont.h)

int LithBitmapFont::s_nScale = 1;
int LithBitmapFont::s_nSmooth = 1;

void LithBitmapFont::SetGlobalScale(int nScale)
{
	if (nScale < 1) nScale = 1;
	if (nScale > 8) nScale = 8;
	s_nScale = nScale;
}

void LithBitmapFont::SetGlobalSmooth(int nSmooth)
{
	if (nSmooth < 0) nSmooth = 0;
	if (nSmooth > 3) nSmooth = 3;
	s_nSmooth = nSmooth;
}

// Brings the metrics (glyph advances, average width, height) to the current
// global scale; the offsets into the font image stay at scale 1. With
// smoothing, the smoothed image follows the scale too.
void LithBitmapFont::ApplyScale()
{
	if (m_nScale != s_nScale || m_nHeight != (int)m_nSurfHeight * m_nScale)
	{
		m_nScale = s_nScale;
		for (int i = 0; i < LTF_NUM_TOTAL_CHARS; i++)
			m_lfOD.pWidths[i] = m_aBaseWidths[i] * m_nScale;
		m_nAvgWidth = m_nBaseAvgWidth * m_nScale;
		m_nHeight = (int)m_nSurfHeight * m_nScale;
	}

	int nWant = (s_nSmooth && m_nScale > 1 && m_pFont) ? m_nScale : 0;
	if (nWant != m_nHDScale || (nWant && s_nSmooth != m_nHDSmooth))
	{
		FreeHD();
		if (nWant)
			BuildHD();
	}
}

// One glyph (m_rRect, in the font image) to (nX, nY), m_nScale times larger:
// from the smoothed image when there is one, else stretched.
void LithBitmapFont::BlitGlyph(int nX, int nY, uint32 nChar, LTBOOL bSolid)
{
	if (m_pFontHD && m_nHDScale == m_nScale && nChar < LTF_NUM_TOTAL_CHARS)
	{
		int s = m_nHDScale;
		LTRect rcSrc(m_aHDX[nChar], m_aHDY[nChar] + m_rRect.top * s,
					 m_aHDX[nChar] + (m_rRect.right - m_rRect.left) * s,
					 m_aHDY[nChar] + m_rRect.bottom * s);

		if (bSolid)
			m_pClient->DrawSurfaceSolidColor(m_pDest, m_pFontHD, &rcSrc, nX, nY,
											 m_hTransColor, m_lfDD->hColor);
		else if (m_bChromaKey)
			m_pClient->DrawSurfaceToSurfaceTransparent(m_pDest, m_pFontHD, &rcSrc, nX, nY, m_hTransColor);
		else
			m_pClient->DrawSurfaceToSurface(m_pDest, m_pFontHD, &rcSrc, nX, nY);
		return;
	}

	if (m_nScale <= 1)
	{
		if (bSolid)
			m_pClient->DrawSurfaceSolidColor(m_pDest, m_pFont, &m_rRect, nX, nY,
											 m_hTransColor, m_lfDD->hColor);
		else if (m_bChromaKey)
			m_pClient->DrawSurfaceToSurfaceTransparent(m_pDest, m_pFont, &m_rRect, nX, nY, m_hTransColor);
		else
			m_pClient->DrawSurfaceToSurface(m_pDest, m_pFont, &m_rRect, nX, nY);
		return;
	}

	LTRect rcDest(nX, nY,
				  nX + (m_rRect.right - m_rRect.left) * m_nScale,
				  nY + (m_rRect.bottom - m_rRect.top) * m_nScale);

	if (bSolid)
		m_pClient->ScaleSurfaceToSurfaceSolidColor(m_pDest, m_pFont, &rcDest, &m_rRect,
												   m_hTransColor, m_lfDD->hColor);
	else if (m_bChromaKey)
		m_pClient->ScaleSurfaceToSurfaceTransparent(m_pDest, m_pFont, &rcDest, &m_rRect, m_hTransColor);
	else
		m_pClient->ScaleSurfaceToSurface(m_pDest, m_pFont, &rcDest, &m_rRect);
}

void LithBitmapFont::FreeHD()
{
	if (m_pFontHD && m_pClient)
		m_pClient->DeleteSurface(m_pFontHD);
	m_pFontHD = LTNULL;
	m_nHDScale = 0;
	m_nHDSmooth = 0;
}

//*********************************************************************************
// The smoothed image (TextSmooth 2 soft, 3 sharp, or 1 for a font without
// an HD image, the air font, sharp). Each glyph is enlarged on its own (what lies outside
// its cell counts as background) with a Catmull-Rom cubic. In the sharp mode
// each result is then pushed towards the darkest or brightest of the 4x4
// retail pixels it was made from, along an S-curve: flat areas, colours and
// gradients stay as they were, and an edge becomes a short smooth ramp
// rather than a blur or a staircase. A font blended onto the screen (most of
// them) keeps every shade; a colour-keyed font (only the air font) can only
// have its pixels on or off, so it gets the smoothed outline, unshaded.
// The glyphs are packed in rows no wider than 2048 pixels (the font images
// are up to ~1500 wide, eight times that is too wide a texture).

namespace
{
	const int HD_MAX_ROW = 2048;
	const int HD_GAP = 2;

	void HD_Weights(float t, float *w)
	{
		float t2 = t * t, t3 = t2 * t;
		w[0] = (-t3 + 2.0f * t2 - t) * 0.5f;
		w[1] = (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f;
		w[2] = (-3.0f * t3 + 4.0f * t2 + t) * 0.5f;
		w[3] = (t3 - t2) * 0.5f;
	}

	// One channel of a cell (nW x nH, row-major) sampled at the enlarged
	// pixel (ox, oy) of an nScale enlargement.
	float HD_Sample(const float *pCell, int nW, int nH, int ox, int oy, int nScale, int nSmooth)
	{
		float sx = (ox + 0.5f) / nScale - 0.5f;
		float sy = (oy + 0.5f) / nScale - 0.5f;
		int ix = (int)floor(sx), iy = (int)floor(sy);
		float wx[4], wy[4];
		HD_Weights(sx - ix, wx);
		HD_Weights(sy - iy, wy);

		float fSum = 0.0f, fMin = 1.0f, fMax = 0.0f;
		for (int a = 0; a < 4; a++)
		{
			int y = iy - 1 + a;
			for (int b = 0; b < 4; b++)
			{
				int x = ix - 1 + b;
				float v = (x >= 0 && x < nW && y >= 0 && y < nH) ? pCell[y * nW + x] : 0.0f;
				fSum += v * wy[a] * wx[b];
				if (v < fMin) fMin = v;
				if (v > fMax) fMax = v;
			}
		}

		if (fSum < fMin) fSum = fMin;
		if (fSum > fMax) fSum = fMax;
		if (nSmooth != 1 || fMax - fMin < 1.0f / 512.0f)
			return fSum;

		float t = (fSum - fMin) / (fMax - fMin);
		t = (t - 0.5f) * 1.6f + 0.5f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		t = t * t * (3.0f - 2.0f * t);
		return fMin + t * (fMax - fMin);
	}

	uint8 HD_Byte(float f)
	{
		int n = (int)(f * 255.0f + 0.5f);
		return (uint8)(n < 0 ? 0 : (n > 255 ? 255 : n));
	}

	LTBOOL HD_IsKey(HLTCOLOR c, HLTCOLOR hKey)
	{
		// within a 16-bit screen's rounding of the key
		int dr = (int)GETR(c) - (int)GETR(hKey);
		int dg = (int)GETG(c) - (int)GETG(hKey);
		int db = (int)GETB(c) - (int)GETB(hKey);
		return dr >= -8 && dr <= 8 && dg >= -4 && dg <= 4 && db >= -8 && db <= 8;
	}
}

// The font's HD image for scale s, drawn for this project from a free outline
// font (tools/hdfonts.py): <its directory>\HD\<its name>_<s>.pcx, the retail
// image's layout s times larger, white on black. Not for a colour-keyed font.
LTBOOL LithBitmapFont::LoadHDFile(int s)
{
	if (m_bChromaKey)
		return LTFALSE;

	const char *pName = m_lfOD.szRefName;
	const char *pBase = pName;
	const char *p;
	for (p = pName; *p; p++)
	{
		if (*p == '\\' || *p == '/')
			pBase = p + 1;
	}
	int nDir = (int)(pBase - pName);
	const char *pDot = strrchr(pBase, '.');
	int nBase = pDot ? (int)(pDot - pBase) : (int)strlen(pBase);

	char szPath[LTF_OFFSETS_NAME_SIZE + 32];
	if (nDir + nBase + 16 >= (int)sizeof(szPath))
		return LTFALSE;
	sprintf(szPath, "%.*sHD\\%.*s_%d.pcx", nDir, pName, nBase, pBase, s);

	HSURFACE hSurf = m_pClient->CreateSurfaceFromBitmap(szPath);
	if (!hSurf)
		return LTFALSE;

	uint32 nW = 0, nH = 0;
	m_pClient->GetSurfaceDims(hSurf, &nW, &nH);
	if (nW != m_nSurfWidth * s || nH != m_nSurfHeight * s)
	{
		m_pClient->DeleteSurface(hSurf);
		return LTFALSE;
	}

	m_pFontHD = hSurf;
	for (int i = 0; i < LTF_NUM_TOTAL_CHARS; i++)
	{
		m_aHDX[i] = m_lfOD.pOffsets[i] * s;
		m_aHDY[i] = 0;
	}
	return LTTRUE;
}

void LithBitmapFont::BuildHD()
{
	if (!m_pClient || !m_pFont || m_nScale < 2 || !m_nSurfWidth || !m_nSurfHeight)
		return;

	int s = m_nScale;

	// TextSmooth 1: the HD image drawn for the font, where there is one
	if (s_nSmooth == 1 && LoadHDFile(s))
	{
		m_nHDScale = s;
		m_nHDSmooth = s_nSmooth;
		return;
	}

	int nH = (int)m_nSurfHeight;
	int nCellH = nH * s;
	int nChar;

	// the atlas: glyphs left to right, a new row when one is full
	int nRowW = 0, nAtlasW = 0, nAtlasH = nCellH;
	for (nChar = 0; nChar < LTF_NUM_TOTAL_CHARS; nChar++)
	{
		int nW = (int)(m_lfOD.pOffsets[nChar + 1] - m_lfOD.pOffsets[nChar]) * s;
		if (nW <= 0)
		{
			m_aHDX[nChar] = 0;
			m_aHDY[nChar] = 0;
			continue;
		}
		if (nRowW && nRowW + nW > HD_MAX_ROW)
		{
			nRowW = 0;
			nAtlasH += nCellH + HD_GAP;
		}
		m_aHDX[nChar] = nRowW;
		m_aHDY[nChar] = nAtlasH - nCellH;
		nRowW += nW + HD_GAP;
		if (nRowW > nAtlasW)
			nAtlasW = nRowW;
	}
	if (nAtlasW <= 0)
		return;

	m_pFontHD = m_pClient->CreateSurface(nAtlasW, nAtlasH);
	if (!m_pFontHD)
		return;
	m_nHDScale = s;
	m_nHDSmooth = s_nSmooth;
	// HD_Sample's mode: 1 sharp edges, 2 soft
	int nMode = (s_nSmooth == 2) ? 2 : 1;

	HLTCOLOR hBack = m_bChromaKey ? m_hTransColor : SETRGB(0, 0, 0);
	m_pClient->FillRect(m_pFontHD, LTNULL, hBack);

	// the widest cell, for the work buffers
	int nMaxW = 0;
	for (nChar = 0; nChar < LTF_NUM_TOTAL_CHARS; nChar++)
	{
		int nW = (int)(m_lfOD.pOffsets[nChar + 1] - m_lfOD.pOffsets[nChar]);
		if (nW > nMaxW)
			nMaxW = nW;
	}
	int nCell = nMaxW * nH;
	float *pR = new float[nCell];
	float *pG = new float[nCell];
	float *pB = new float[nCell];
	float *pM = new float[nCell];	// colour-keyed: 1 where the glyph is
	HLTCOLOR *pSrc = new HLTCOLOR[nCell];

	for (nChar = 0; nChar < LTF_NUM_TOTAL_CHARS; nChar++)
	{
		int x0 = (int)m_lfOD.pOffsets[nChar];
		int nW = (int)m_lfOD.pOffsets[nChar + 1] - x0;
		if (nW <= 0)
			continue;

		int x, y;
		for (y = 0; y < nH; y++)
		{
			for (x = 0; x < nW; x++)
			{
				HLTCOLOR c = 0;
				m_pClient->GetPixel(m_pFont, x0 + x, y, &c);
				int i = y * nW + x;
				pSrc[i] = c;
				LTBOOL bKey = m_bChromaKey && HD_IsKey(c, m_hTransColor);
				pM[i] = bKey ? 0.0f : 1.0f;
				pR[i] = bKey ? 0.0f : GETR(c) / 255.0f;
				pG[i] = bKey ? 0.0f : GETG(c) / 255.0f;
				pB[i] = bKey ? 0.0f : GETB(c) / 255.0f;
			}
		}

		int nOX = (int)m_aHDX[nChar], nOY = (int)m_aHDY[nChar];
		for (y = 0; y < nCellH; y++)
		{
			for (x = 0; x < nW * s; x++)
			{
				HLTCOLOR c;
				if (m_bChromaKey)
				{
					if (HD_Sample(pM, nW, nH, x, y, s, nMode) < 0.5f)
						continue;
					// the colour of the retail pixel it lies in, or of a
					// neighbour of it that is part of the glyph
					int sx = x / s, sy = y / s;
					c = pSrc[sy * nW + sx];
					if (HD_IsKey(c, m_hTransColor))
					{
						int dx = ((x % s) * 2 < s) ? -1 : 1;
						int dy = ((y % s) * 2 < s) ? -1 : 1;
						int aTry[3][2] = { { sx + dx, sy }, { sx, sy + dy }, { sx + dx, sy + dy } };
						for (int k = 0; k < 3; k++)
						{
							int tx = aTry[k][0], ty = aTry[k][1];
							if (tx >= 0 && tx < nW && ty >= 0 && ty < nH &&
								!HD_IsKey(pSrc[ty * nW + tx], m_hTransColor))
							{
								c = pSrc[ty * nW + tx];
								break;
							}
						}
						if (HD_IsKey(c, m_hTransColor))
							continue;
					}
				}
				else
				{
					uint8 r = HD_Byte(HD_Sample(pR, nW, nH, x, y, s, nMode));
					uint8 g = HD_Byte(HD_Sample(pG, nW, nH, x, y, s, nMode));
					uint8 b = HD_Byte(HD_Sample(pB, nW, nH, x, y, s, nMode));
					if (!r && !g && !b)
						continue;
					c = SETRGB(r, g, b);
				}
				m_pClient->SetPixel(m_pFontHD, nOX + x, nOY + y, c);
			}
		}
	}

	delete[] pR;
	delete[] pG;
	delete[] pB;
	delete[] pM;
	delete[] pSrc;
}
#endif
