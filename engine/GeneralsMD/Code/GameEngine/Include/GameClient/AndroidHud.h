/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: AndroidHud.h /////////////////////////////////////////////////////////////////////////////
// GeneralsX @feature Android smart HUD 19/10/2026 (docs/MOBILE-UX-SPEC.md §6)
//
// The one source of truth for the phone HUD geometry. Three places use it and must agree:
//   - ControlBarPhoneLayout.cpp re-lays-out the control bar's real GameWindows into it,
//   - W3DInGameUI.cpp draws the overlay buttons (menu, control groups, ALL, IDLE),
//   - SDL3GameEngine.cpp hit-tests finger taps against those overlay buttons.
// Every size is derived from the display's physical density so it holds on any phone.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#if defined(__ANDROID__)

#include "Lib/BaseType.h"

/// Physical pixels per millimetre of the display. Published by the Android touch layer at
/// start-up (it asks the Java side over JNI); 0 until then, callers fall back to a phone-sized guess.
extern Real TheAndroidPixelsPerMm;

namespace AndroidHud
{

struct Rect
{
	Int x, y, w, h;
	Int right() const { return x + w; }
	Int bottom() const { return y + h; }
	Bool contains( Int px, Int py ) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

/// Overlay buttons drawn by W3DInGameUI and hit-tested by the touch layer (not GameWindows).
enum OverlayId CPP_11(: Int)
{
	OVERLAY_MENU = 0,       ///< pause/ESC menu, top-left
	OVERLAY_GROUP_1,        ///< control groups 1..4: tap = select, long-press = assign
	OVERLAY_GROUP_2,
	OVERLAY_GROUP_3,
	OVERLAY_GROUP_4,
	OVERLAY_SELECT_ALL,     ///< select every unit
	OVERLAY_IDLE_WORKER,    ///< select the next idle worker and centre on it
	OVERLAY_COUNT
};
const Int NUM_CONTROL_GROUPS = 4;

/// Resolved layout for one display size, in display pixels.
struct Metrics
{
	Int  displayW, displayH;
	Real pxPerMm;
	Int  margin;            ///< screen edge margin
	Int  gap;               ///< gap between tiles
	Int  pad;               ///< panel padding around tiles
	Int  tile;              ///< command / group tile (target 9.5 mm)
	Int  stripH;            ///< top strip height

	Rect overlay[ OVERLAY_COUNT ];

	// top strip, left to right
	Rect money, power, star, chat, beacon;
	Rect powersRow;         ///< generals' powers shortcut row (between the strip buttons and the minimap)
	Int  powersTile;        ///< one powers tile (fits up to 11 in the row)
	Rect minimap;           ///< radar, top-right

	// right drawer: a bottom-anchored grid of command tiles with an info area (portrait or
	// production queue) sitting directly above the rows in use
	Rect grid;              ///< the full grid footprint (gridRows rows)
	Int  gridCols, gridRows;
	Int  infoH;             ///< info area height
	Int  queueTile;         ///< production queue tile (2 rows x 5)
};

inline Int mmToPx( Real mm, Real pxPerMm ) { return (Int)(mm * pxPerMm + 0.5f); }

/// px/mm to use for a display: the published density, or "a phone's short side is about 68 mm".
inline Real pixelsPerMmFor( Int displayW, Int displayH )
{
	if( TheAndroidPixelsPerMm > 2.0f && TheAndroidPixelsPerMm < 60.0f )
		return TheAndroidPixelsPerMm;
	const Int shortSide = displayW < displayH ? displayW : displayH;
	return shortSide > 0 ? (Real)shortSide / 68.0f : 15.0f;
}

inline Metrics computeMetrics( Int displayW, Int displayH, Real pxPerMm )
{
	Metrics m;
	m.displayW = displayW;
	m.displayH = displayH;
	m.pxPerMm  = pxPerMm;
	m.margin   = mmToPx( 0.8f, pxPerMm );
	m.gap      = mmToPx( 0.6f, pxPerMm );
	m.pad      = mmToPx( 0.8f, pxPerMm );
	m.stripH   = mmToPx( 8.0f, pxPerMm );

	// --- top strip ---
	Int x = m.margin;
	const Int y = m.margin;
	m.overlay[ OVERLAY_MENU ] = { x, y, mmToPx( 11.0f, pxPerMm ), m.stripH };
	x = m.overlay[ OVERLAY_MENU ].right() + m.gap * 2;
	m.money = { x, y, mmToPx( 17.0f, pxPerMm ), m.stripH };
	x = m.money.right() + m.gap;
	const Int powerH = mmToPx( 2.6f, pxPerMm );
	m.power = { x, y + (m.stripH - powerH) / 2, mmToPx( 22.0f, pxPerMm ), powerH };
	x = m.power.right() + m.gap * 2;
	m.star = { x, y, m.stripH, m.stripH };
	x = m.star.right() + m.gap;
	m.chat = { x, y, m.stripH, m.stripH };
	x = m.chat.right() + m.gap;
	m.beacon = { x, y, m.stripH, m.stripH };

	// --- minimap, top-right ---
	const Int mapW = mmToPx( 20.0f, pxPerMm ), mapH = mmToPx( 14.0f, pxPerMm );
	m.minimap = { displayW - m.margin - mapW, y, mapW, mapH };

	// --- powers row fills what is left of the strip ---
	const Int rowX = m.beacon.right() + m.gap * 2;
	Int rowW = m.minimap.x - m.gap * 2 - rowX;
	if( rowW < 0 ) rowW = 0;
	m.powersRow = { rowX, y, rowW, m.stripH };
	Int pt = (rowW - 10 * m.gap) / 11;
	const Int ptMin = mmToPx( 5.5f, pxPerMm );
	if( pt < ptMin ) pt = ptMin;
	if( pt > m.stripH ) pt = m.stripH;
	m.powersTile = pt;

	// --- tile size: 9.5 mm, shrunk only if the left column or the drawer would not fit ---
	Int tile = mmToPx( 9.5f, pxPerMm );
	const Int colTop = m.margin + m.stripH + m.gap * 2;
	const Int colAvail = displayH - colTop - m.margin;
	const Int colTile = (colAvail - 5 * m.gap) / 6;                  // 4 groups + ALL + IDLE
	if( tile > colTile ) tile = colTile;
	m.queueTile = mmToPx( 5.5f, pxPerMm );
	Int infoH = 2 * m.queueTile + m.gap;
	const Int portraitH = mmToPx( 11.0f, pxPerMm );
	if( infoH < portraitH ) infoH = portraitH;
	m.infoH = infoH;
	m.gridCols = 4;
	m.gridRows = 4;
	// the drawer at full height must stay below the minimap
	const Int drawerAvail = displayH - m.minimap.bottom() - m.gap * 2 - m.pad * 2 - m.margin - infoH - m.gap;
	const Int drawerTile = (drawerAvail - (m.gridRows - 1) * m.gap) / m.gridRows;
	if( tile > drawerTile ) tile = drawerTile;
	const Int tileMin = mmToPx( 7.0f, pxPerMm );
	if( tile < tileMin ) tile = tileMin;
	m.tile = tile;

	// --- left column ---
	for( Int i = 0; i < NUM_CONTROL_GROUPS + 2; ++i )
	{
		m.overlay[ OVERLAY_GROUP_1 + i ] = { m.margin, colTop + i * (tile + m.gap), tile, tile };
	}

	// --- right drawer grid, bottom-anchored ---
	const Int gridW = m.gridCols * tile + (m.gridCols - 1) * m.gap;
	const Int gridH = m.gridRows * tile + (m.gridRows - 1) * m.gap;
	m.grid = { displayW - m.margin - m.pad - gridW, displayH - m.margin - m.pad - gridH, gridW, gridH };

	return m;
}

/// Command slot i (0-based) fills the grid from the bottom row up, left to right, so the first
/// commands sit under the right thumb.
inline Rect commandTileRect( const Metrics &m, Int slot )
{
	const Int rowFromBottom = slot / m.gridCols;
	const Int col = slot % m.gridCols;
	Rect r;
	r.w = r.h = m.tile;
	r.x = m.grid.x + col * (m.tile + m.gap);
	r.y = m.grid.bottom() - (rowFromBottom + 1) * m.tile - rowFromBottom * m.gap;
	return r;
}

/// Top of the rows in use when `visibleRows` rows (>= 1) of the grid are populated.
inline Int gridTopForRows( const Metrics &m, Int visibleRows )
{
	if( visibleRows < 1 ) visibleRows = 1;
	if( visibleRows > m.gridRows ) visibleRows = m.gridRows;
	return m.grid.bottom() - visibleRows * m.tile - (visibleRows - 1) * m.gap;
}

} // namespace AndroidHud

#endif // __ANDROID__
