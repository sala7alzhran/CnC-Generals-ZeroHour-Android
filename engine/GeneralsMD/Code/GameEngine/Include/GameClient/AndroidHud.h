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
// The one source of truth for the phone HUD's geometry and palette. Two places use it and must
// agree:
//   - ControlBarPhoneLayout.cpp re-lays-out the control bar's real GameWindows into it and draws
//     every HUD surface (the round buttons, the resource pill, the minimap frame, the drawer, the
//     tiles' chrome),
//   - SDL3GameEngine.cpp hit-tests finger taps against the overlay buttons and the HUD's dead zones.
// Every size is derived from the display's physical density so it holds on any phone.
//
// Layout (landscape, §6.3): a round menu button and one resource pill top-left; a round left
// column (IDLE, ARMY, "+", the control groups) under them; the minimap in a thin gold frame
// top-right with the rank badge beside it and the generals' powers as round buttons under it; the
// command drawer bottom-right: a chip row (portrait, name, health, tabs) over two rows of five
// tiles, with the production queue in a row above the chip while producing.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#if defined(__ANDROID__)

#include "Lib/BaseType.h"

/// Physical pixels per millimetre of the display. Published by the Android touch layer at
/// start-up (it asks the Java side over JNI); 0 until then, callers fall back to a phone-sized guess.
extern Real TheAndroidPixelsPerMm;

/// The overlay button a finger is holding down (an AndroidHud::OverlayId), -1 if none. Set by the
/// touch layer (SDL3GameEngine.cpp), read by the HUD's draw (ControlBarPhoneLayout.cpp).
extern Int TheAndroidOverlayPressed;

namespace AndroidHud
{

struct Rect
{
	Int x, y, w, h;
	Int right() const { return x + w; }
	Int bottom() const { return y + h; }
	Int centerX() const { return x + w / 2; }
	Int centerY() const { return y + h / 2; }
	Bool contains( Int px, Int py ) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

inline Rect inflate( const Rect &r, Int by ) { Rect o = { r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by }; return o; }

inline void unionRect( Rect &acc, const Rect &r )
{
	if( r.w <= 0 || r.h <= 0 )
		return;
	if( acc.w <= 0 || acc.h <= 0 )
	{
		acc = r;
		return;
	}
	const Int x0 = acc.x < r.x ? acc.x : r.x;
	const Int y0 = acc.y < r.y ? acc.y : r.y;
	const Int x1 = acc.right() > r.right() ? acc.right() : r.right();
	const Int y1 = acc.bottom() > r.bottom() ? acc.bottom() : r.bottom();
	acc.x = x0; acc.y = y0; acc.w = x1 - x0; acc.h = y1 - y0;
}

/// Overlay buttons drawn by the HUD and hit-tested by the touch layer (not GameWindows).
enum OverlayId CPP_11(: Int)
{
	OVERLAY_MENU = 0,       ///< pause/ESC menu, the round button top-left
	OVERLAY_IDLE_WORKER,    ///< left column: select the next idle worker (shown while one is idle)
	OVERLAY_SELECT_ALL,     ///< left column: ARMY, select every unit
	OVERLAY_GROUP_1,        ///< left column: control groups 1..4: tap = select, long-press = assign
	OVERLAY_GROUP_2,
	OVERLAY_GROUP_3,
	OVERLAY_GROUP_4,
	OVERLAY_TAB_0,          ///< drawer tabs (BASE / DEF / SUP, or UNITS / TECH / MORE)
	OVERLAY_TAB_1,
	OVERLAY_TAB_2,
	OVERLAY_COUNT
};
const Int NUM_CONTROL_GROUPS = 4;
const Int NUM_TABS = 3;

/// The command drawer: two rows of five tiles per page. A menu with more commands than one page
/// holds is split into tabs (§6.3); a tab with more than a page cycles its pages on a second tap.
const Int GRID_COLS  = 5;
const Int GRID_ROWS  = 2;
const Int GRID_CELLS = GRID_COLS * GRID_ROWS;
/// Production queue: one row of thumbnails above the chip (the engine has 9 queue buttons).
const Int QUEUE_COLS = 9;
/// Generals' powers: round buttons in rows of this many under the minimap.
const Int POWER_COLS = 5;

/// One palette for every HUD surface, in the engine's Color packing (0xAARRGGBB): dark translucent
/// panels, one thin gold line, warm text with a shadow; bright gold for pressed or available, muted
/// for locked.
namespace Palette
{
	const UnsignedInt PanelFill     = 0xC80A0D12;   ///< the drawer bed and the resource pill
	const UnsignedInt PanelLine     = 0xD2C9A961;   ///< thin gold outline of panels and frames
	const UnsignedInt ButtonFill    = 0xD4141922;   ///< bed of a round button or a tile
	const UnsignedInt ButtonFillHot = 0xF05A4B28;   ///< pressed round button
	const UnsignedInt ButtonLine    = 0xB0907C4A;   ///< muted outline (locked, inactive)
	const UnsignedInt Gold          = 0xFFD9B968;   ///< available outline, rings, active tab
	const UnsignedInt GoldBright    = 0xFFF0D890;   ///< pressed outline, a ready power's ring
	const UnsignedInt GhostLine     = 0x80C9A961;   ///< the "+" slot offered for "hold to assign"
	const UnsignedInt PressedWash   = 0x50E6C882;   ///< over a pressed tile
	const UnsignedInt Text          = 0xFFF2E6C4;
	const UnsignedInt TextDim       = 0xA0B0A58A;
	const UnsignedInt TextShadow    = 0xC8000000;
	const UnsignedInt Glyph         = 0xF5E9D08F;   ///< ☰ bars, "+", the ARMY figures, the lock
	const UnsignedInt GlyphDim      = 0x90AAA087;
	const UnsignedInt BadgeFill     = 0xF0D6B870;   ///< count badges
	const UnsignedInt BadgeText     = 0xFF14110A;
	const UnsignedInt CostFill      = 0xB4000000;   ///< price pill on a tile
	const UnsignedInt CostText      = 0xFFF0E4BE;
	const UnsignedInt CostTextPoor  = 0xFFFF7A5C;   ///< a price the player cannot pay right now
	const UnsignedInt LockFill      = 0x8C000000;
	const UnsignedInt Trough        = 0xC8000000;   ///< empty part of a bar or ring
	const UnsignedInt Good          = 0xFF5FD35A;   ///< power covered, health high
	const UnsignedInt Warn          = 0xFFE0C040;   ///< health middling
	const UnsignedInt Bad           = 0xFFE05040;   ///< power short, health low
	const UnsignedInt Coin          = 0xFFE2B84A;   ///< the $ coin in the resource pill
	const UnsignedInt CoinText      = 0xFF3A2A08;
}

/// What one overlay button looks like this frame. Filled by ControlBar::phoneOverlays() from game
/// state, so the draw side and the touch side agree on both position and presence.
struct OverlayState
{
	Rect rect;
	Bool visible;   ///< drawn and tappable
	Bool ghost;     ///< an empty group slot offered for "hold to assign" while something is selected
	Bool active;    ///< the drawer tab currently shown
	Int  badge;     ///< units in the group / idle workers (0 = no badge)
};

/// Resolved layout for one display size, in display pixels.
struct Metrics
{
	Int  displayW, displayH;
	Real pxPerMm;
	Bool multiplayer;       ///< chat and beacon buttons exist
	Int  powerCount;        ///< generals' power buttons the layout made room for
	Int  margin;            ///< screen edge margin
	Int  gap;               ///< gap between controls
	Int  pad;               ///< panel padding
	Int  line;              ///< outline width
	Int  radius;            ///< corner radius of panels and tiles
	Int  roundBtn;          ///< diameter of the menu button
	Int  colBtn;            ///< diameter of a left-column button: 10 mm, shrunk so six fit (7.5 mm floor)
	Int  powerBtn;          ///< diameter of a generals' power button
	Int  rankBtn;           ///< diameter of the rank badge
	Int  chipH;             ///< height of the drawer's chip row (portrait, name, health, tabs)
	Int  tileW, tileH;      ///< command tile: a 4:3 cameo over the name block
	Int  tileNameH;         ///< the name block under the cameo (two lines)
	Int  queueTile;         ///< production queue thumbnail
	Int  cameoTile;         ///< upgrade / veterancy cameo in the chip
	Int  tabW;              ///< one drawer tab

	// top-left: the round menu and the resource pill (coin, money, power bar)
	Rect menu, pill, coin, moneyText, powerBar;
	Int  colTop;            ///< top of the first left-column slot

	// top-right: minimap in its frame, rank badge beside it, powers under it, timers under those
	Rect minimap, minimapFrame, rank, powersArea, superweaponTimers, mapBlock;
	Rect chat, beacon;      ///< multiplayer only, bottom-left
	Rect namedTimers;       ///< mission timers, bottom-left

	// bottom-right drawer: the full GRID_COLS x GRID_ROWS tile footprint; the rows in use hug its
	// bottom, the chip row sits above them (chipRect / commandCellRect take the row count)
	Rect grid;
	Rect drawerPanel;       ///< the largest drawer: two tile rows + chip row + padding
};

inline Int mmToPx( Real mm, Real pxPerMm ) { return (Int)(mm * pxPerMm + 0.5f); }

/// The top-right block for given minimap and power-button sizes: the framed minimap, the rank badge
/// beside it, the powers in rows of POWER_COLS under it (right-aligned), the superweapon countdown
/// line under those, and the block's footprint (a dead zone for the touch layer).
inline void placeTopRight( Metrics &m, Int mapW, Int mapH, Int powerBtn )
{
	m.powerBtn = powerBtn;
	m.minimap = { m.displayW - m.margin - m.gap - mapW, m.margin + m.gap, mapW, mapH };
	m.minimapFrame = inflate( m.minimap, m.gap );
	m.rank = { m.minimapFrame.x - m.gap * 2 - m.rankBtn, m.minimapFrame.y + (m.minimapFrame.h - m.rankBtn) / 2, m.rankBtn, m.rankBtn };
	const Int powerRows = (m.powerCount + POWER_COLS - 1) / POWER_COLS;
	const Int powerCols = m.powerCount < POWER_COLS ? m.powerCount : POWER_COLS;
	m.powersArea = { m.minimapFrame.right() - (powerCols * m.powerBtn + (powerCols > 0 ? powerCols - 1 : 0) * m.gap),
									 m.minimapFrame.bottom() + m.gap * 2,
									 powerCols * m.powerBtn + (powerCols > 0 ? powerCols - 1 : 0) * m.gap,
									 powerRows * m.powerBtn + (powerRows > 0 ? powerRows - 1 : 0) * m.gap };
	Int blockBottom = powerRows > 0 ? m.powersArea.bottom() : m.minimapFrame.bottom();
	const Int timerH = mmToPx( 3.0f, m.pxPerMm ), timerW = mmToPx( 34.0f, m.pxPerMm );
	m.superweaponTimers = { m.displayW - m.margin - timerW, blockBottom + m.gap, timerW, timerH };
	if( m.powerCount > 0 )
		blockBottom = m.superweaponTimers.bottom();   // a superweapon's countdown comes with its power button
	Int blockLeft = m.rank.x;
	if( powerCols > 0 && m.powersArea.x < blockLeft )
		blockLeft = m.powersArea.x;                  // a full row of powers is wider than map + badge
	m.mapBlock = { blockLeft, 0, m.displayW - blockLeft, blockBottom + m.gap };
}

/// The lowest edge of the top-right block the drawer must clear: the powers, or the minimap frame.
inline Int topRightHardBottom( const Metrics &m )
{
	return (m.powerCount > 0 ? m.powersArea.bottom() : m.minimapFrame.bottom()) + m.gap;
}

/// px/mm to use for a display: the published density, or "a phone's short side is about 68 mm".
inline Real pixelsPerMmFor( Int displayW, Int displayH )
{
	if( TheAndroidPixelsPerMm > 2.0f && TheAndroidPixelsPerMm < 60.0f )
		return TheAndroidPixelsPerMm;
	const Int shortSide = displayW < displayH ? displayW : displayH;
	return shortSide > 0 ? (Real)shortSide / 68.0f : 15.0f;
}

inline Metrics computeMetrics( Int displayW, Int displayH, Real pxPerMm, Bool multiplayer, Int powerCount )
{
	Metrics m;
	m.displayW    = displayW;
	m.displayH    = displayH;
	m.pxPerMm     = pxPerMm;
	m.multiplayer = multiplayer;
	m.powerCount  = powerCount < 0 ? 0 : powerCount;
	m.margin      = mmToPx( 0.8f, pxPerMm );
	m.gap         = mmToPx( 0.6f, pxPerMm );
	m.pad         = mmToPx( 0.7f, pxPerMm );
	m.line        = mmToPx( 0.15f, pxPerMm );
	if( m.line < 1 ) m.line = 1;
	m.radius      = mmToPx( 1.2f, pxPerMm );
	m.roundBtn    = mmToPx( 10.0f, pxPerMm );
	m.powerBtn    = mmToPx( 8.0f, pxPerMm );
	m.rankBtn     = mmToPx( 9.0f, pxPerMm );
	m.chipH       = mmToPx( 7.5f, pxPerMm );
	m.queueTile   = mmToPx( 3.4f, pxPerMm );
	m.cameoTile   = mmToPx( 2.4f, pxPerMm );
	m.tabW        = mmToPx( 8.0f, pxPerMm );
	m.tileNameH   = mmToPx( 3.9f, pxPerMm );

	// --- top-left: round menu, then the resource pill ---
	m.menu = { m.margin, m.margin, m.roundBtn, m.roundBtn };
	const Int pillH = mmToPx( 8.0f, pxPerMm );
	m.pill = { m.menu.right() + m.gap * 2, m.margin + (m.roundBtn - pillH) / 2, mmToPx( 38.0f, pxPerMm ), pillH };
	const Int coinD = mmToPx( 5.2f, pxPerMm );
	m.coin = { m.pill.x + m.pad, m.pill.y + (pillH - coinD) / 2, coinD, coinD };
	const Int barW = mmToPx( 12.0f, pxPerMm ), barH = mmToPx( 2.0f, pxPerMm );
	m.powerBar = { m.pill.right() - m.pad - barW, m.pill.y + (pillH - barH) / 2, barW, barH };
	const Int boltW = mmToPx( 2.6f, pxPerMm );
	m.moneyText = { m.coin.right() + m.gap, m.pill.y, m.powerBar.x - boltW - m.gap * 2 - (m.coin.right() + m.gap), pillH };

	// --- left column: IDLE, ARMY, "+" and the groups; at most six at once (there is no "+" once all
	//     four groups exist), shrunk so six fit above the bottom margin ---
	m.colTop = m.menu.bottom() + m.gap * 2;
	m.colBtn = m.roundBtn;
	const Int colFit = (displayH - m.colTop - m.margin - 5 * m.gap) / 6;
	if( m.colBtn > colFit ) m.colBtn = colFit;
	const Int colMin = mmToPx( 7.5f, pxPerMm );
	if( m.colBtn < colMin ) m.colBtn = colMin;

	// --- top-right: minimap (20 x 14 mm), rank badge, powers (8 mm), timers. A two-row drawer at its
	//     7 mm tile floor must still clear the powers; on a short phone with two rows of powers the
	//     power buttons shrink to 7 mm first, then the minimap (down to 11 mm tall) ---
	Int mapW = mmToPx( 20.0f, pxPerMm ), mapH = mmToPx( 14.0f, pxPerMm );
	const Int tileMin = mmToPx( 7.0f, pxPerMm );
	const Int drawerNeed = 2 * m.pad + m.chipH + m.gap + GRID_ROWS * (tileMin * 3 / 4 + m.tileNameH) + (GRID_ROWS - 1) * m.gap + m.margin;
	placeTopRight( m, mapW, mapH, m.powerBtn );
	if( displayH - topRightHardBottom( m ) < drawerNeed )
		placeTopRight( m, mapW, mapH, mmToPx( 7.0f, pxPerMm ) );
	if( displayH - topRightHardBottom( m ) < drawerNeed )
	{
		const Int deficit = drawerNeed - (displayH - topRightHardBottom( m ));
		const Int mapMinH = mmToPx( 11.0f, pxPerMm );
		mapH = mapH - deficit < mapMinH ? mapMinH : mapH - deficit;
		mapW = mapH * 20 / 14;
		placeTopRight( m, mapW, mapH, m.powerBtn );
	}
	const Int timerH = mmToPx( 3.0f, pxPerMm );

	// --- bottom-left: chat and beacon in multiplayer, mission timers ---
	const Int smallBtn = mmToPx( 8.0f, pxPerMm );
	if( multiplayer )
	{
		m.chat   = { m.margin, displayH - m.margin - smallBtn, smallBtn, smallBtn };
		m.beacon = { m.margin, m.chat.y - m.gap - smallBtn, smallBtn, smallBtn };
	}
	else
	{
		m.chat   = { m.margin, displayH - m.margin, 0, 0 };
		m.beacon = m.chat;
	}
	m.namedTimers = { m.margin, (multiplayer ? m.beacon.y : displayH - m.margin) - m.gap - timerH * 3, mmToPx( 40.0f, pxPerMm ), timerH * 3 };

	// --- bottom-right drawer: tiles 9 mm wide, shrunk so chip row + two tile rows fit under the
	//     minimap block (its superweapon countdown line included), never below 7 mm ---
	Int tileW = mmToPx( 9.0f, pxPerMm );
	const Int avail = displayH - m.margin - m.mapBlock.bottom() - m.gap;
	const Int tileHBudget = (avail - 2 * m.pad - m.chipH - m.gap - (GRID_ROWS - 1) * m.gap) / GRID_ROWS;
	const Int tileWBudget = (tileHBudget - m.tileNameH) * 4 / 3;
	if( tileW > tileWBudget ) tileW = tileWBudget;
	if( tileW < tileMin ) tileW = tileMin;
	m.tileW = tileW;
	m.tileH = tileW * 3 / 4 + m.tileNameH;
	const Int gridW = GRID_COLS * tileW + (GRID_COLS - 1) * m.gap;
	const Int gridH = GRID_ROWS * m.tileH + (GRID_ROWS - 1) * m.gap;
	m.grid = { displayW - m.margin - m.pad - gridW, displayH - m.margin - m.pad - gridH, gridW, gridH };
	const Int chipTop = m.grid.y - m.gap - m.chipH;
	m.drawerPanel = { m.grid.x - m.pad, chipTop - m.pad, gridW + 2 * m.pad, m.grid.bottom() + m.pad - (chipTop - m.pad) };
	return m;
}

/// Top of the tile rows when `rows` (0..GRID_ROWS) of them are in use: they hug the grid's bottom.
inline Int gridTopForRows( const Metrics &m, Int rows )
{
	if( rows < 0 ) rows = 0;
	if( rows > GRID_ROWS ) rows = GRID_ROWS;
	return m.grid.bottom() - rows * m.tileH - (rows > 0 ? rows - 1 : -1) * m.gap;
}

/// Grid cell i (0-based) of a page with `rows` rows, row-major from the top-left: the reading order
/// of the menu.
inline Rect commandCellRect( const Metrics &m, Int cell, Int rows )
{
	const Int row = cell / GRID_COLS, col = cell % GRID_COLS;
	Rect r = { m.grid.x + col * (m.tileW + m.gap), gridTopForRows( m, rows ) + row * (m.tileH + m.gap), m.tileW, m.tileH };
	return r;
}

/// The chip row (portrait, name, health, tabs) directly above the tile rows in use.
inline Rect chipRect( const Metrics &m, Int rows )
{
	Rect r = { m.grid.x, gridTopForRows( m, rows ) - m.gap - m.chipH, m.grid.w, m.chipH };
	return r;
}

/// The 4:3 portrait at the left of the chip row.
inline Rect portraitRect( const Metrics &m, Int rows )
{
	const Rect chip = chipRect( m, rows );
	Rect r = { chip.x, chip.y, m.chipH * 4 / 3, m.chipH };
	return r;
}

/// Drawer tab t (0..NUM_TABS-1), right-aligned in the chip row.
inline Rect tabRect( const Metrics &m, Int rows, Int t )
{
	const Rect chip = chipRect( m, rows );
	Rect r = { chip.right() - (NUM_TABS - t) * m.tabW - (NUM_TABS - 1 - t) * m.gap, chip.y, m.tabW, m.chipH };
	return r;
}

/// The production queue row above the chip row.
inline Rect queueRowRect( const Metrics &m, Int rows )
{
	const Rect chip = chipRect( m, rows );
	Rect r = { chip.x, chip.y - m.gap - m.queueTile, chip.w, m.queueTile };
	return r;
}

/// The index-th production queue thumbnail, left to right in the queue row.
inline Rect queueCellRect( const Metrics &m, Int rows, Int index )
{
	const Rect row = queueRowRect( m, rows );
	Rect r = { row.x + index * (m.queueTile + m.gap), row.y, m.queueTile, m.queueTile };
	return r;
}

/// The 4:3 cameo box at the top of a tile.
inline Rect tileCameoRect( const Rect &tile, const Metrics &m )
{
	Rect r = { tile.x, tile.y, tile.w, tile.h - m.tileNameH };
	return r;
}

/// The index-th slot of the left column, stacked from the top.
inline Rect leftSlotRect( const Metrics &m, Int index )
{
	Rect r = { m.margin, m.colTop + index * (m.colBtn + m.gap), m.colBtn, m.colBtn };
	return r;
}

/// The index-th generals' power button: rows of POWER_COLS under the minimap, right-aligned.
inline Rect powerButtonRect( const Metrics &m, Int index )
{
	const Int row = index / POWER_COLS, col = index % POWER_COLS;
	const Int inRow = (m.powerCount - row * POWER_COLS) < POWER_COLS ? (m.powerCount - row * POWER_COLS) : POWER_COLS;
	const Int rowW = inRow * m.powerBtn + (inRow - 1) * m.gap;
	Rect r = { m.minimapFrame.right() - rowW + col * (m.powerBtn + m.gap), m.powersArea.y + row * (m.powerBtn + m.gap), m.powerBtn, m.powerBtn };
	return r;
}

} // namespace AndroidHud

#endif // __ANDROID__
