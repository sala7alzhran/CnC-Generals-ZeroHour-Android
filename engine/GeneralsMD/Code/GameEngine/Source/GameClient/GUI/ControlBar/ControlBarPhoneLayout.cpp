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

// FILE: ControlBarPhoneLayout.cpp ////////////////////////////////////////////////////////////////
// GeneralsX @feature Android smart HUD 19/10/2026 (docs/MOBILE-UX-SPEC.md §6)
//
// On a phone the 4:3 command bar is replaced by re-laying-out its own GameWindows at runtime:
// the battlefield gets the whole screen, the money / power / generals star / powers row sit in a
// slim top strip, the radar moves to the top-right, and the command buttons become big tiles in a
// right-thumb drawer that is only visible while the engine shows one of its context panels. The
// engine keeps populating and processing those buttons exactly as on the PC, so every faction,
// general and mod works unchanged; the .wnd layout files are game data and are not modified.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(__ANDROID__)

#include "Common/NameKeyGenerator.h"
#include "GameClient/AndroidHud.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"

Real TheAndroidPixelsPerMm = 0.0f;

namespace
{

using AndroidHud::Metrics;
using AndroidHud::Rect;

// Translucent panel behind a group of tiles (the drawer, the info area above it, the smaller
// under-construction / timer / beacon panels). Drawn by the panel window itself, before its children.
void phonePanelDraw( GameWindow *window, WinInstanceData * )
{
	ICoord2D pos, size;
	window->winGetScreenPosition( &pos.x, &pos.y );
	window->winGetSize( &size.x, &size.y );
	if( size.x <= 0 || size.y <= 0 )
		return;
	const Int x1 = pos.x + size.x, y1 = pos.y + size.y;
	TheWindowManager->winFillRect( GameMakeColor( 14, 18, 24, 185 ), 1.0f, pos.x, pos.y, x1, y1 );
	TheWindowManager->winOpenRect( GameMakeColor( 214, 184, 112, 220 ), 2.0f, pos.x, pos.y, x1, y1 );
}

// A panel swallows the taps that miss a tile, so a unit under the drawer is never hit by accident.
WindowMsgHandledType phonePanelInput( GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData )
{
	return MSG_HANDLED;
}

GameWindow *findBarWindow( const char *name )
{
	if( !TheWindowManager )
		return nullptr;
	return TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( name ) );
}

// Everything in the bar's tree that is neither a functional leaf nor a panel stops drawing and
// stops taking input: the bar artwork, markers and container windows. Neither flag affects children,
// and a no-input window lets the tap fall through to the battlefield.
inline void passThrough( GameWindow *win )
{
	win->winSetStatus( WIN_STATUS_SEE_THRU | WIN_STATUS_NO_INPUT );
}

// Controls the phone does not use. Hidden, and parked off-screen in case something unhides them.
void hideAndPark( GameWindow *win )
{
	if( !win )
		return;
	win->winHide( TRUE );
	win->winSetPosition( -4096, -4096 );
}

// Static texts keep the .wnd's PC-sized font; give them a readable height.
void scaleTextFont( GameWindow *win, Int pixelHeight )
{
	if( !win || !TheFontLibrary )
		return;
	GameFont *font = win->winGetFont();
	if( !font || font->pointSize >= pixelHeight )
		return;
	GameFont *bigger = TheFontLibrary->getFont( font->nameString, pixelHeight, font->bold );
	if( bigger )
		GadgetStaticTextSetFont( win, bigger );
}

inline void place( ControlBar *bar, GameWindow *win, const Rect &r )
{
	bar->placePhoneWindow( win, r.x, r.y, r.w, r.h );
}

// Fixed-size set of window pointers for "is this one of the leaves we keep?" tests.
struct LeafSet
{
	enum { MAX = 96 };
	GameWindow *win[ MAX ];
	Int count;
	void add( GameWindow *w ) { if( w && count < MAX ) win[ count++ ] = w; }
	Bool has( const GameWindow *w ) const
	{
		for( Int i = 0; i < count; ++i )
			if( win[ i ] == w )
				return TRUE;
		return FALSE;
	}
};

// Depth-first over the bar's tree. Observer panels are left exactly as they are.
void makePassThrough( GameWindow *parent, const LeafSet &leaves, const LeafSet &panels, const LeafSet &skip )
{
	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		if( skip.has( child ) )
			continue;
		if( panels.has( child ) )
		{
			child->winSetDrawFunc( phonePanelDraw );
			child->winSetInputFunc( phonePanelInput );
		}
		else if( !leaves.has( child ) )
		{
			passThrough( child );
		}
		makePassThrough( child, leaves, panels, skip );
	}
}

} // namespace

//-------------------------------------------------------------------------------------------------
/** Put a window at a screen rectangle whatever its parent is: positions are parent-relative, so the
	* parent must already sit where it belongs. */
//-------------------------------------------------------------------------------------------------
void ControlBar::placePhoneWindow( GameWindow *win, Int screenX, Int screenY, Int width, Int height )
{
	if( !win )
		return;
	Int px = 0, py = 0;
	GameWindow *parent = win->winGetParent();
	if( parent )
		parent->winGetScreenPosition( &px, &py );
	win->winSetPosition( screenX - px, screenY - py );
	win->winSetSize( width, height );
}

//-------------------------------------------------------------------------------------------------
/** The HUD's text font at a given physical height, so overlay labels match the bar's own text. */
//-------------------------------------------------------------------------------------------------
GameFont *ControlBar::getPhoneHudFont( Real heightMm )
{
	if( !TheFontLibrary || !TheDisplay )
		return nullptr;
	GameWindow *money = findBarWindow( "ControlBar.wnd:MoneyDisplay" );
	GameFont *base = money ? money->winGetFont() : nullptr;
	const Real pxPerMm = AndroidHud::pixelsPerMmFor( TheDisplay->getWidth(), TheDisplay->getHeight() );
	return TheFontLibrary->getFont( base ? base->nameString : AsciiString( "Arial" ),
																	AndroidHud::mmToPx( heightMm, pxPerMm ), TRUE );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::phoneHasIdleWorker() const
{
	return m_phoneIdleWorkerButton != nullptr && m_phoneIdleWorkerButton->winGetEnabled();
}

//-------------------------------------------------------------------------------------------------
/** The full layout pass. Runs from update() whenever the bar was (re)built, a faction scheme was
	* applied or the stage changed: those are the moments the engine itself repositions windows. */
//-------------------------------------------------------------------------------------------------
void ControlBar::applyPhoneLayout()
{
	m_phoneLayoutDirty = FALSE;
	GameWindow *master = m_contextParent[ CP_MASTER ];
	if( !master || !TheDisplay || !TheWindowManager )
		return;

	const Int W = TheDisplay->getWidth(), H = TheDisplay->getHeight();
	const Metrics m = AndroidHud::computeMetrics( W, H, AndroidHud::pixelsPerMmFor( W, H ) );

	// --- the named windows we lay out ---
	GameWindow *money       = findBarWindow( "ControlBar.wnd:MoneyDisplay" );
	GameWindow *power       = findBarWindow( "ControlBar.wnd:PowerWindow" );
	GameWindow *radar       = findBarWindow( "ControlBar.wnd:LeftHUD" );
	GameWindow *star        = findBarWindow( "ControlBar.wnd:ButtonGeneral" );
	GameWindow *beacon      = findBarWindow( "ControlBar.wnd:ButtonPlaceBeacon" );
	GameWindow *ucDesc      = findBarWindow( "ControlBar.wnd:UnderConstructionDesc" );
	GameWindow *ucCancel    = findBarWindow( "ControlBar.wnd:ButtonCancelConstruction" );
	GameWindow *oclText     = findBarWindow( "ControlBar.wnd:OCLTimerStaticText" );
	GameWindow *oclBar      = findBarWindow( "ControlBar.wnd:OCLTimerProgressBar" );
	GameWindow *oclSell     = findBarWindow( "ControlBar.wnd:OCLTimerSellButton" );
	GameWindow *beaconEdit  = findBarWindow( "ControlBar.wnd:EditBeaconText" );
	GameWindow *beaconLabel = findBarWindow( "ControlBar.wnd:StaticTextBeaconLabel" );
	GameWindow *beaconClear = findBarWindow( "ControlBar.wnd:ButtonClearBeaconText" );
	GameWindow *beaconDel   = findBarWindow( "ControlBar.wnd:ButtonDeleteBeacon" );

	GameWindow *queue[ MAX_BUILD_QUEUE_BUTTONS ];
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
	{
		AsciiString name;
		name.format( "ControlBar.wnd:ButtonQueue%02d", i + 1 );
		queue[ i ] = findBarWindow( name.str() );
	}

	LeafSet leaves; leaves.count = 0;
	LeafSet panels; panels.count = 0;
	LeafSet skip;   skip.count = 0;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i ) leaves.add( m_commandWindows[ i ] );
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i ) leaves.add( queue[ i ] );
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i ) leaves.add( m_rightHUDUpgradeCameos[ i ] );
	leaves.add( m_rightHUDCameoWindow );
	leaves.add( money ); leaves.add( power ); leaves.add( radar ); leaves.add( m_radarAttackGlowWindow );
	leaves.add( star ); leaves.add( m_communicatorButton ); leaves.add( beacon );
	leaves.add( ucDesc ); leaves.add( ucCancel );
	leaves.add( oclText ); leaves.add( oclBar ); leaves.add( oclSell );
	leaves.add( beaconEdit ); leaves.add( beaconLabel ); leaves.add( beaconClear ); leaves.add( beaconDel );

	panels.add( m_contextParent[ CP_COMMAND ] );
	panels.add( m_contextParent[ CP_BUILD_QUEUE ] );
	panels.add( m_contextParent[ CP_UNDER_CONSTRUCTION ] );
	panels.add( m_contextParent[ CP_OCL_TIMER ] );
	panels.add( m_contextParent[ CP_BEACON ] );
	panels.add( m_rightHUDUnitSelectParent );

	skip.add( m_contextParent[ CP_OBSERVER_LIST ] );
	skip.add( m_contextParent[ CP_OBSERVER_INFO ] );

	// 1. the root spans the display and lets taps through to the battlefield
	master->winSetPosition( 0, 0 );
	master->winSetSize( W, H );
	passThrough( master );
	makePassThrough( master, leaves, panels, skip );

	// 2. controls the phone replaces (menu = the ☰ overlay, idle worker = the left column) or drops.
	//    InGameUI keeps enabling/disabling the idle-worker button with the idle count, so the hidden
	//    window stays the source of truth for the IDLE overlay's dimmed state.
	m_phoneIdleWorkerButton = findBarWindow( "ControlBar.wnd:ButtonIdleWorker" );
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonLarge" ) );
	hideAndPark( m_phoneIdleWorkerButton );
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonOptions" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:GeneralsExp" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:ExpBarForeground" ) );

	// 3. top strip and minimap
	place( this, money, m.money );
	scaleTextFont( money, AndroidHud::mmToPx( 3.4f, m.pxPerMm ) );
	place( this, power, m.power );
	place( this, star, m.star );
	place( this, m_communicatorButton, m.chat );
	place( this, beacon, m.beacon );
	place( this, radar, m.minimap );
	place( this, m_radarAttackGlowWindow, m.minimap );

	// 4. the generals' powers shortcut bar: its own layout, one row of tiles in the strip
	if( m_specialPowerShortcutParent )
	{
		passThrough( m_specialPowerShortcutParent );
		place( this, m_specialPowerShortcutParent, m.powersRow );
		for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
		{
			Rect t;
			t.w = t.h = m.powersTile;
			t.x = m.powersRow.x + i * (m.powersTile + m.gap);
			t.y = m.powersRow.y + (m.powersRow.h - m.powersTile) / 2;
			place( this, m_specialPowerShortcutButtonParents[ i ], t );
			place( this, m_specialPowerShortcutButtons[ i ], t );
		}
	}

	// 5. the small context panels share the drawer's bottom row
	{
		const Rect slot0 = AndroidHud::commandTileRect( m, 0 );
		const Rect slot1 = AndroidHud::commandTileRect( m, 1 );
		const Rect slot4 = AndroidHud::commandTileRect( m, m.gridCols );      // row above, column 0
		Rect oneRow = { m.grid.x - m.pad, slot0.y - m.pad, m.grid.w + 2 * m.pad, m.tile + 2 * m.pad };
		Rect twoRows = { m.grid.x - m.pad, slot4.y - m.pad, m.grid.w + 2 * m.pad, 2 * m.tile + m.gap + 2 * m.pad };
		const Int textH = AndroidHud::mmToPx( 3.0f, m.pxPerMm );
		const Rect rowText = { slot1.x, slot0.y, m.grid.right() - slot1.x, m.tile };

		// under construction: cancel tile + "xx% complete"
		place( this, m_contextParent[ CP_UNDER_CONSTRUCTION ], oneRow );
		place( this, ucCancel, slot0 );
		place( this, ucDesc, rowText );
		scaleTextFont( ucDesc, textH );

		// OCL timer: sell tile + countdown text above a progress bar
		place( this, m_contextParent[ CP_OCL_TIMER ], oneRow );
		place( this, oclSell, slot0 );
		Rect oclTextRect = { rowText.x, rowText.y, rowText.w, m.tile / 2 };
		Rect oclBarRect  = { rowText.x, rowText.y + m.tile / 2 + m.gap, rowText.w, m.tile / 2 - m.gap };
		place( this, oclText, oclTextRect );
		scaleTextFont( oclText, textH );
		place( this, oclBar, oclBarRect );

		// beacon: label + text entry on the upper row, clear / delete tiles on the lower row
		place( this, m_contextParent[ CP_BEACON ], twoRows );
		place( this, beaconClear, slot0 );
		place( this, beaconDel, slot1 );
		Rect labelRect = { slot4.x, slot4.y, m.tile, m.tile };
		Rect editRect  = { slot4.right() + m.gap, slot4.y + m.tile / 4, m.grid.right() - slot4.right() - m.gap, m.tile / 2 };
		place( this, beaconLabel, labelRect );
		scaleTextFont( beaconLabel, textH );
		place( this, beaconEdit, editRect );
	}

	// 6. the drawer itself follows the selection; force its first placement
	m_phoneVisibleRows = -1;
	updatePhoneDrawer();
}

//-------------------------------------------------------------------------------------------------
/** The drawer grows upward from the bottom-right corner with the rows actually in use, and the
	* info area (portrait or production queue) sits directly above those rows. Cheap: a few flag reads
	* per frame, and window moves only when the row count changes. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updatePhoneDrawer()
{
	GameWindow *command = m_contextParent[ CP_COMMAND ];
	if( !command || !TheDisplay )
		return;

	const Int W = TheDisplay->getWidth(), H = TheDisplay->getHeight();
	const Metrics m = AndroidHud::computeMetrics( W, H, AndroidHud::pixelsPerMmFor( W, H ) );
	const Int maxSlots = m.gridCols * m.gridRows;

	Int rows = 1;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET && i < maxSlots; ++i )
	{
		GameWindow *w = m_commandWindows[ i ];
		if( !w || w->winIsHidden() )
			continue;
		const Int r = i / m.gridCols + 1;
		if( r > rows )
			rows = r;
	}
	if( rows == m_phoneVisibleRows )
		return;
	m_phoneVisibleRows = rows;

	const Int top = AndroidHud::gridTopForRows( m, rows );

	// the command grid panel and its tiles
	placePhoneWindow( command, m.grid.x - m.pad, top - m.pad, m.grid.w + 2 * m.pad, m.grid.bottom() - top + 2 * m.pad );
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		if( i < maxSlots )
			place( this, m_commandWindows[ i ], AndroidHud::commandTileRect( m, i ) );
		else
			hideAndPark( m_commandWindows[ i ] );   // script-only slots the UI never shows
	}

	// the info area directly above, ending where the grid panel starts (one seamless panel)
	const Rect infoPanel = { m.grid.x - m.pad, top - m.pad - m.infoH - m.pad, m.grid.w + 2 * m.pad, m.infoH + m.pad };
	const Int infoY = infoPanel.y + m.pad;
	place( this, m_rightHUDWindow, infoPanel );
	place( this, m_rightHUDUnitSelectParent, infoPanel );
	place( this, m_contextParent[ CP_BUILD_QUEUE ], infoPanel );

	// portrait on the left, upgrade / veterancy cameos in a column beside it
	Rect portrait = { m.grid.x, infoY, m.infoH, m.infoH };
	place( this, m_rightHUDCameoWindow, portrait );
	const Int cameo = (m.infoH - (MAX_RIGHT_HUD_UPGRADE_CAMEOS - 1) * m.gap) / MAX_RIGHT_HUD_UPGRADE_CAMEOS;
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
	{
		Rect c = { portrait.right() + m.gap, infoY + i * (cameo + m.gap), cameo, cameo };
		place( this, m_rightHUDUpgradeCameos[ i ], c );
	}

	// production queue: 2 rows x 5 (items 1-5 above, 6-9 below)
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
	{
		AsciiString name;
		name.format( "ControlBar.wnd:ButtonQueue%02d", i + 1 );
		GameWindow *q = findBarWindow( name.str() );
		const Int row = i / 5, col = i % 5;
		Rect r = { m.grid.x + col * (m.queueTile + m.gap), infoY + row * (m.queueTile + m.gap), m.queueTile, m.queueTile };
		place( this, q, r );
	}
}

#endif // __ANDROID__
