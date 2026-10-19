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
// The one source of truth for the phone HUD's geometry and palette. Three places use it and must
// agree:
//   - ControlBarPhoneLayout.cpp re-lays-out the control bar's real GameWindows into it and paints
//     the backdrop (top strip, tile beds) under them,
//   - W3DInGameUI.cpp draws the overlay buttons (menu, control groups, ALL, IDLE) and the tile
//     chrome (frames, costs, lock badges, the name label) over them,
//   - SDL3GameEngine.cpp hit-tests finger taps against the overlay buttons and the HUD's dead zones.
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

inline Rect inflate( const Rect &r, Int by ) { Rect o = { r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by }; return o; }

/// Overlay buttons drawn by W3DInGameUI and hit-tested by the touch layer (not GameWindows).
enum OverlayId CPP_11(: Int)
{
	OVERLAY_MENU = 0,       ///< pause/ESC menu, top-left of the strip
	OVERLAY_GROUP_1,        ///< control groups 1..4: tap = select, long-press = assign
	OVERLAY_GROUP_2,
	OVERLAY_GROUP_3,
	OVERLAY_GROUP_4,
	OVERLAY_SELECT_ALL,     ///< select every unit
	OVERLAY_IDLE_WORKER,    ///< select the next idle worker and centre on it
	OVERLAY_COUNT
};
const Int NUM_CONTROL_GROUPS = 4;

/// The command drawer: 3 columns of 4:3 tiles (the shape of the cameo art), up to 5 rows (the UI
/// never shows more than 14 commands). Visible commands are packed into the first cells.
const Int GRID_COLS  = 3;
const Int GRID_ROWS  = 5;
const Int GRID_CELLS = GRID_COLS * GRID_ROWS;
/// Production queue: 2 rows of 5 in the info row beside the portrait.
const Int QUEUE_COLS = 5;
const Int QUEUE_ROWS = 2;
/// Upgrade / veterancy cameos under the portrait, 3 per row.
const Int CAMEO_COLS = 3;

/// One palette for every HUD surface, in the engine's Color packing (0xAARRGGBB). Dark translucent
/// beds, one muted-gold hairline, warm text; a pressed or active control gets the bright gold line.
namespace Palette
{
	const UnsignedInt StripFill    = 0xAF0A0D12;   ///< top strip band
	const UnsignedInt PanelFill    = 0xB40C1016;   ///< drawer panels
	const UnsignedInt PanelLine    = 0xC8CDB26E;   ///< panel hairline
	const UnsignedInt TileFill     = 0xD2161B23;   ///< bed under a tile / button
	const UnsignedInt TileLine     = 0xA0786946;   ///< tile hairline
	const UnsignedInt TileLineHot  = 0xF0E6C882;   ///< pressed / active tile hairline
	const UnsignedInt PressedFill  = 0xE65A4B28;   ///< pressed overlay button
	const UnsignedInt GhostLine    = 0x80CDB26E;   ///< the "+" slot offered for "hold to assign"
	const UnsignedInt Trough       = 0xC8000000;   ///< empty power meter
	const UnsignedInt Text         = 0xFFF0E4BE;
	const UnsignedInt TextDim      = 0x96AAA087;
	const UnsignedInt TextShadow   = 0xC8000000;
	const UnsignedInt Glyph        = 0xF5E6CD8C;   ///< ☰ bars, "+", lock
	const UnsignedInt GlyphDim     = 0x8CAAA087;
	const UnsignedInt BadgeFill    = 0xEBD6B870;   ///< unit-count badge on a group tile
	const UnsignedInt BadgeText    = 0xFF14110A;
	const UnsignedInt CostFill     = 0xB4000000;   ///< cost pill on a build tile
	const UnsignedInt CostText     = 0xFFF0E4BE;
	const UnsignedInt CostTextPoor = 0xFFFF7A5C;   ///< cost the player cannot pay right now
	const UnsignedInt LockFill     = 0x8C000000;
	const UnsignedInt LabelFill    = 0xDC0C1016;   ///< name label beside the touched tile
}

/// What one overlay button looks like this frame. Filled by ControlBar::phoneOverlays() from game
/// state (which groups exist, whether anything is selected, whether a worker is idle), so the draw
/// side and the touch side agree on both position and presence.
struct OverlayState
{
	Rect rect;
	Bool visible;   ///< drawn and tappable
	Bool ghost;     ///< an empty group slot offered for "hold to assign" while something is selected
	Bool dimmed;
	Int  badge;     ///< units in the group (0 = no badge)
};

/// Resolved layout for one display size, in display pixels.
struct Metrics
{
	Int  displayW, displayH;
	Real pxPerMm;
	Bool multiplayer;       ///< chat and beacon buttons exist
	Int  margin;            ///< screen edge margin
	Int  gap;               ///< gap between tiles
	Int  pad;               ///< panel padding around tiles
	Int  line;              ///< hairline width
	Int  stripH;            ///< top strip content height
	Int  squareTile;        ///< left-column tile (groups, ALL, IDLE)
	Int  tileW, tileH;      ///< command tile, 4:3 like the cameo art it shows
	Int  cameoTile;         ///< upgrade / veterancy cameo under the portrait (CAMEO_COLS per row)
	Int  queueTile;         ///< production queue tile (QUEUE_COLS x QUEUE_ROWS)
	Int  powersTile;        ///< one generals' powers tile

	// top strip, left to right; stripBand is the whole band the backdrop paints
	Rect stripBand, menu, money, power, star, powersRow, chat, beacon, minimap;
	Int  colTop;            ///< top of the left column (below the strip)

	// right drawer: a bottom-anchored grid of command tiles; the info row (portrait panel and,
	// while producing, the queue panel) sits directly above the rows in use
	Rect grid;              ///< the full GRID_COLS x GRID_ROWS footprint, right-aligned
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

inline Metrics computeMetrics( Int displayW, Int displayH, Real pxPerMm, Bool multiplayer )
{
	Metrics m;
	m.displayW    = displayW;
	m.displayH    = displayH;
	m.pxPerMm     = pxPerMm;
	m.multiplayer = multiplayer;
	m.margin      = mmToPx( 0.8f, pxPerMm );
	m.gap         = mmToPx( 0.6f, pxPerMm );
	m.pad         = mmToPx( 0.7f, pxPerMm );
	m.line        = mmToPx( 0.15f, pxPerMm );
	if( m.line < 1 ) m.line = 1;
	m.stripH      = mmToPx( 8.0f, pxPerMm );

	// --- top strip: menu, money, power, star, powers row ... chat, beacon, minimap ---
	const Int y = m.margin;
	Int x = m.margin;
	m.stripBand = { 0, 0, displayW, m.margin * 2 + m.stripH };
	m.menu  = { x, y, mmToPx( 10.0f, pxPerMm ), m.stripH };
	x = m.menu.right() + m.gap * 2;
	m.money = { x, y, mmToPx( 20.0f, pxPerMm ), m.stripH };
	x = m.money.right() + m.gap;
	const Int powerH = mmToPx( 2.6f, pxPerMm );
	m.power = { x, y + (m.stripH - powerH) / 2, mmToPx( 18.0f, pxPerMm ), powerH };
	x = m.power.right() + m.gap * 2;
	m.star  = { x, y, m.stripH, m.stripH };

	const Int mapW = mmToPx( 20.0f, pxPerMm ), mapH = mmToPx( 14.0f, pxPerMm );
	m.minimap = { displayW - m.margin - mapW, y, mapW, mapH };

	Int rowEnd = m.minimap.x - m.gap * 2;
	if( multiplayer )
	{
		m.beacon = { m.minimap.x - m.gap * 2 - m.stripH, y, m.stripH, m.stripH };
		m.chat   = { m.beacon.x - m.gap - m.stripH, y, m.stripH, m.stripH };
		rowEnd   = m.chat.x - m.gap * 2;
	}
	else
	{
		m.chat   = { rowEnd, y, 0, 0 };
		m.beacon = m.chat;
	}

	// --- generals' powers row fills what is left of the strip; tiles 5.5–7 mm ---
	const Int rowX = m.star.right() + m.gap * 2;
	Int rowW = rowEnd - rowX;
	if( rowW < 0 ) rowW = 0;
	m.powersRow = { rowX, y, rowW, m.stripH };
	Int pt = (rowW - 10 * m.gap) / 11;
	const Int ptMin = mmToPx( 5.5f, pxPerMm ), ptMax = mmToPx( 7.0f, pxPerMm );
	if( pt < ptMin ) pt = ptMin;
	if( pt > ptMax ) pt = ptMax;
	m.powersTile = pt;

	// --- left column: 8.5 mm tiles, shrunk only if six of them would not fit ---
	m.colTop = m.stripBand.bottom() + m.gap * 2;
	Int sq = mmToPx( 8.5f, pxPerMm );
	const Int colTile = (displayH - m.colTop - m.margin - 5 * m.gap) / 6;
	if( sq > colTile ) sq = colTile;
	m.squareTile = sq;

	// --- command tiles: 10.5 mm wide, 4:3; shrunk so a full drawer (5 rows + portrait panel with
	//     one cameo row) stays below the minimap ---
	Int tileW = mmToPx( 10.5f, pxPerMm );
	const Int avail = displayH - m.minimap.bottom() - m.gap - m.margin;
	// full height = 6 tileH + cameoTile + 6 gap + 4 pad, with tileH = 3/4 tileW, cameoTile = (tileW - 2 gap) / 3
	const Int tileWBudget = (Int)((avail - 5.34f * m.gap - 4 * m.pad) / 4.834f);
	if( tileW > tileWBudget ) tileW = tileWBudget;
	const Int tileMin = mmToPx( 7.0f, pxPerMm );
	if( tileW < tileMin ) tileW = tileMin;
	m.tileW = tileW;
	m.tileH = tileW * 3 / 4;
	m.cameoTile = (tileW - (CAMEO_COLS - 1) * m.gap) / CAMEO_COLS;

	const Int gridW = GRID_COLS * tileW + (GRID_COLS - 1) * m.gap;
	const Int gridH = GRID_ROWS * m.tileH + (GRID_ROWS - 1) * m.gap;
	m.grid = { displayW - m.margin - m.pad - gridW, displayH - m.margin - m.pad - gridH, gridW, gridH };

	// queue tiles: QUEUE_COLS beside the portrait panel within the drawer's width, QUEUE_ROWS as
	// tall as the portrait; 3.2–4.5 mm
	Int q = (gridW - tileW - 5 * m.gap - 2 * m.pad) / QUEUE_COLS;
	const Int qByHeight = (m.tileH - (QUEUE_ROWS - 1) * m.gap) / QUEUE_ROWS;
	if( q > qByHeight ) q = qByHeight;
	const Int qMin = mmToPx( 3.2f, pxPerMm ), qMax = mmToPx( 4.5f, pxPerMm );
	if( q < qMin ) q = qMin;
	if( q > qMax ) q = qMax;
	m.queueTile = q;

	return m;
}

/// Grid cell i (0-based) counted from the bottom-left, left to right, then up: the first commands
/// sit under the right thumb.
inline Rect commandCellRect( const Metrics &m, Int cell )
{
	const Int rowFromBottom = cell / GRID_COLS;
	const Int col = cell % GRID_COLS;
	Rect r;
	r.w = m.tileW;
	r.h = m.tileH;
	r.x = m.grid.x + col * (m.tileW + m.gap);
	r.y = m.grid.bottom() - (rowFromBottom + 1) * m.tileH - rowFromBottom * m.gap;
	return r;
}

/// Top of the rows in use when `rows` (>= 1) rows of the grid are populated.
inline Int gridTopForRows( const Metrics &m, Int rows )
{
	if( rows < 1 ) rows = 1;
	if( rows > GRID_ROWS ) rows = GRID_ROWS;
	return m.grid.bottom() - rows * m.tileH - (rows - 1) * m.gap;
}

/// The index-th visible slot of the left column, stacked from the top without gaps.
inline Rect leftSlotRect( const Metrics &m, Int index )
{
	Rect r = { m.margin, m.colTop + index * (m.squareTile + m.gap), m.squareTile, m.squareTile };
	return r;
}

} // namespace AndroidHud

#endif // __ANDROID__
