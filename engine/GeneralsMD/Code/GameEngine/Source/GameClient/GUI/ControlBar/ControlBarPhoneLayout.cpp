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
// On a phone the 4:3 command bar is replaced by re-laying-out its own GameWindows at runtime: the
// battlefield gets the whole screen, money / power / the generals' star / the powers row sit in a
// slim top strip, the radar moves to the top-right, and the command buttons become 4:3 tiles in a
// right-thumb drawer that slides in only while the engine shows one of its context panels. The
// engine keeps populating and processing those buttons exactly as on the PC, so every faction,
// general and mod works unchanged; the .wnd layout files are game data and are not modified.
//
// Hit-testing rules this file relies on (GameWindowManager::winProcessMouseEvent, GameWindow::
// winPointInChild): the first child whose rectangle contains the point wins, descendants are only
// reachable through ancestors whose rectangles contain them, a final NO_INPUT window hands the tap
// to the battlefield, and a message every window in the parent chain ignores also reaches the
// battlefield. So: every piece of bar artwork is parked off-screen at zero size (never hit), the
// panels and containers block input (GameWinBlockInput), and the root keeps NO_INPUT so a tap on
// an empty area is the one intended hole to the world.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(__ANDROID__)

#include <chrono>

#include "Common/GameUtility.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/AndroidHud.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Squad.h"
#include "GameNetwork/GameInfo.h"

Real TheAndroidPixelsPerMm = 0.0f;

namespace
{

using AndroidHud::Metrics;
using AndroidHud::Rect;
namespace Palette = AndroidHud::Palette;

const UnsignedInt DRAWER_SLIDE_MS = 160;   ///< the drawer slides in from the right edge over this long

inline UnsignedInt nowMs()
{
	using namespace std::chrono;
	return (UnsignedInt)duration_cast<milliseconds>( steady_clock::now().time_since_epoch() ).count();
}

inline void fillRect( const Rect &r, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheWindowManager->winFillRect( color, 1.0f, r.x, r.y, r.right(), r.bottom() );
}

inline void lineRect( const Rect &r, Int line, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheWindowManager->winOpenRect( color, (Real)line, r.x, r.y, r.right(), r.bottom() );
}

/// A bed under a button: dark fill (the button art covers it) and a hairline just outside it.
inline void tileBed( const Rect &r, Int line )
{
	fillRect( r, Palette::TileFill );
	lineRect( AndroidHud::inflate( r, line ), line, Palette::TileLine );
}

inline Rect windowRect( GameWindow *w )
{
	ICoord2D pos, size;
	w->winGetScreenPosition( &pos.x, &pos.y );
	w->winGetSize( &size.x, &size.y );
	Rect r = { pos.x, pos.y, size.x, size.y };
	return r;
}

inline Bool shown( GameWindow *w ) { return w != nullptr && !w->winIsHidden(); }

inline void unionRect( Rect &acc, const Rect &r )
{
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

// A drawer panel paints its translucent background before its children. Drawn by the panel window
// itself, so a panel the layout parked at zero size paints nothing.
void phonePanelDraw( GameWindow *window, WinInstanceData * )
{
	const Rect r = windowRect( window );
	if( r.w <= 0 || r.h <= 0 )
		return;
	fillRect( r, Palette::PanelFill );
	lineRect( r, TheControlBar ? TheControlBar->phoneMetrics().line : 1, Palette::PanelLine );
}

// The root window paints the top strip and the beds under the strip's controls, before all of them.
void phoneRootDraw( GameWindow *, WinInstanceData * )
{
	if( TheControlBar )
		TheControlBar->phoneDrawBackdrop();
}

GameWindow *findBarWindow( const char *name )
{
	if( !TheWindowManager )
		return nullptr;
	return TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( name ) );
}

/// Parked: no size and far off-screen, so the window is never drawn and never hit. Children move
/// with it. The status is left alone so a panel can be brought back by the next layout.
void park( GameWindow *win )
{
	if( !win )
		return;
	win->winSetSize( 0, 0 );
	win->winSetPosition( -4096, -4096 );
}

/// Bar artwork, markers and anything else the phone has no use for.
void parkDecor( GameWindow *win )
{
	win->winSetStatus( WIN_STATUS_SEE_THRU );
	park( win );
}

/// Controls the phone replaces: hidden as well, in case something unhides them they stay unreachable.
void hideAndPark( GameWindow *win )
{
	if( !win )
		return;
	win->winHide( TRUE );
	park( win );
}

/// Static texts keep the .wnd's PC-sized font; give them a physical height.
void scaleTextFont( GameWindow *win, Int pixelHeight )
{
	if( !win || !TheFontLibrary )
		return;
	GameFont *font = win->winGetFont();
	if( !font || font->pointSize == pixelHeight )
		return;
	GameFont *sized = TheFontLibrary->getFont( font->nameString, pixelHeight, font->bold );
	if( sized )
		GadgetStaticTextSetFont( win, sized );
}

inline void place( ControlBar *bar, GameWindow *win, const Rect &r, Int dx = 0 )
{
	bar->placePhoneWindow( win, r.x + dx, r.y, r.w, r.h );
}

/// Fixed-size set of window pointers for "which role does this window play?" tests.
struct LeafSet
{
	enum { MAX = 96 };
	GameWindow *win[ MAX ];
	Int count;
	LeafSet() : count( 0 ) {}
	void add( GameWindow *w ) { if( w && count < MAX ) win[ count++ ] = w; }
	Bool has( const GameWindow *w ) const
	{
		for( Int i = 0; i < count; ++i )
			if( win[ i ] == w )
				return TRUE;
		return FALSE;
	}
};

/// Depth-first over a window tree. Leaves keep their own draw and input and are sized by the layout.
/// Panels draw a background and block input; containers are see-through and block input (sized by
/// the layout to cover their children). Everything else is artwork and is parked. A window we did
/// not expect to hold a kept descendant becomes a container covering its parent, so the descendant
/// stays reachable. Observer panels are left exactly as they are. Returns whether the subtree holds
/// anything kept.
Bool restyleTree( GameWindow *parent, const LeafSet &leaves, const LeafSet &panels,
									const LeafSet &containers, const LeafSet &skip )
{
	Bool kept = FALSE;
	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		if( skip.has( child ) )
			continue;
		if( leaves.has( child ) )
		{
			kept = TRUE;
			continue;
		}
		const Bool below = restyleTree( child, leaves, panels, containers, skip );
		if( panels.has( child ) )
		{
			child->winClearStatus( WIN_STATUS_SEE_THRU | WIN_STATUS_NO_INPUT );
			child->winSetDrawFunc( phonePanelDraw );
			child->winSetInputFunc( GameWinBlockInput );
			kept = TRUE;
		}
		else if( containers.has( child ) || below )
		{
			child->winClearStatus( WIN_STATUS_NO_INPUT );
			child->winSetStatus( WIN_STATUS_SEE_THRU );
			child->winSetInputFunc( GameWinBlockInput );
			if( !containers.has( child ) )
			{
				// not one of the containers the layout sizes: cover the parent so the kept
				// descendants stay inside the hit-test chain
				ICoord2D size;
				parent->winGetSize( &size.x, &size.y );
				child->winSetPosition( 0, 0 );
				child->winSetSize( size.x, size.y );
				DEBUG_LOG(( "Android HUD: unexpected container %d holds HUD controls; covering its parent", child->winGetWindowId() ));
			}
			kept = TRUE;
		}
		else
		{
			parkDecor( child );
		}
	}
	return kept;
}

/// The bar's named windows the layout touches, looked up once per layout pass (name keys hash).
struct PhoneWindows
{
	GameWindow *money, *power, *radar, *glow, *star, *chat, *beacon;
	GameWindow *ucDesc, *ucCancel, *oclText, *oclBar, *oclSell;
	GameWindow *beaconEdit, *beaconLabel, *beaconClear, *beaconDel;
	GameWindow *queue[ MAX_BUILD_QUEUE_BUTTONS ];
	Bool valid;
};
PhoneWindows s_win = { nullptr };

Bool isMultiplayerGame()
{
	if( !TheGameLogic || !TheGameInfo )
		return FALSE;
	const GameMode mode = TheGameLogic->getGameMode();
	return (mode == GAME_LAN || mode == GAME_INTERNET) && TheGameInfo->isMultiPlayer();
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
	GameWindow *money = s_win.valid ? s_win.money : findBarWindow( "ControlBar.wnd:MoneyDisplay" );
	GameFont *base = money ? money->winGetFont() : nullptr;
	return TheFontLibrary->getFont( base ? base->nameString : AsciiString( "Arial" ),
																	AndroidHud::mmToPx( heightMm, m_phoneMetrics.pxPerMm ), TRUE );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::phoneHasIdleWorker() const
{
	return m_phoneIdleWorkerButton != nullptr && m_phoneIdleWorkerButton->winGetEnabled();
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::phoneHudVisible() const
{
	GameWindow *master = m_contextParent[ CP_MASTER ];
	return master != nullptr && !master->winIsHidden() && !m_isObserverCommandBar;
}

//-------------------------------------------------------------------------------------------------
/** The overlay buttons that exist this frame, packed from the top of the left column: the control
	* groups that hold units (with their count), one "+" slot for the first empty group while something
	* is selected (hold to assign), ALL, and IDLE only while a worker is idle. The ☰ menu is always in
	* the strip. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneOverlays( AndroidHud::OverlayState *out ) const
{
	using namespace AndroidHud;
	const Metrics &m = m_phoneMetrics;
	for( Int i = 0; i < OVERLAY_COUNT; ++i )
	{
		out[ i ].rect = Rect{ 0, 0, 0, 0 };
		out[ i ].visible = FALSE;
		out[ i ].ghost = FALSE;
		out[ i ].dimmed = FALSE;
		out[ i ].badge = 0;
	}
	if( !phoneHudVisible() )
		return;

	out[ OVERLAY_MENU ].rect = m.menu;
	out[ OVERLAY_MENU ].visible = TRUE;

	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	Int slot = 0, firstEmpty = -1;
	for( Int g = 0; g < NUM_CONTROL_GROUPS; ++g )
	{
		Int count = 0;
		if( player )
		{
			Squad *squad = player->getHotkeySquad( g );
			if( squad )
				count = (Int)squad->getLiveObjects().size();
		}
		if( count > 0 )
		{
			OverlayState &o = out[ OVERLAY_GROUP_1 + g ];
			o.rect = leftSlotRect( m, slot++ );
			o.visible = TRUE;
			o.badge = count;
		}
		else if( firstEmpty < 0 )
		{
			firstEmpty = g;
		}
	}
	if( firstEmpty >= 0 && TheInGameUI && TheInGameUI->getSelectCount() > 0 )
	{
		OverlayState &o = out[ OVERLAY_GROUP_1 + firstEmpty ];
		o.rect = leftSlotRect( m, slot++ );
		o.visible = TRUE;
		o.ghost = TRUE;
	}
	out[ OVERLAY_SELECT_ALL ].rect = leftSlotRect( m, slot++ );
	out[ OVERLAY_SELECT_ALL ].visible = TRUE;
	if( phoneHasIdleWorker() )
	{
		out[ OVERLAY_IDLE_WORKER ].rect = leftSlotRect( m, slot++ );
		out[ OVERLAY_IDLE_WORKER ].visible = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::getPhoneDrawerRect( AndroidHud::Rect &out ) const
{
	if( !m_phoneDrawerShown || m_phoneDrawerRect.w <= 0 || m_phoneDrawerRect.h <= 0 || !phoneHudVisible() )
		return FALSE;
	out = m_phoneDrawerRect;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
GameWindow *ControlBar::getPhoneCommandWindow( Int cell ) const
{
	if( cell < 0 || cell >= AndroidHud::GRID_CELLS )
		return nullptr;
	return m_phoneCellWindow[ cell ];
}

//-------------------------------------------------------------------------------------------------
const CommandButton *ControlBar::getPhoneCommand( GameWindow *button ) const
{
	return button ? (const CommandButton *)GadgetButtonGetData( button ) : nullptr;
}

//-------------------------------------------------------------------------------------------------
/** Money a command costs the local player: structures and units, player and object upgrades. Other
	* commands (abilities, powers, sell, stop...) cost nothing and show no price. */
//-------------------------------------------------------------------------------------------------
Int ControlBar::getPhoneCommandCost( const CommandButton *command ) const
{
	if( !command || !ThePlayerList )
		return 0;
	Player *player = ThePlayerList->getLocalPlayer();
	if( !player )
		return 0;
	switch( command->getCommandType() )
	{
		case GUI_COMMAND_DOZER_CONSTRUCT:
		case GUI_COMMAND_UNIT_BUILD:
		{
			const ThingTemplate *thing = command->getThingTemplate();
			return thing ? thing->calcCostToBuild( player ) : 0;
		}
		case GUI_COMMAND_PLAYER_UPGRADE:
		case GUI_COMMAND_OBJECT_UPGRADE:
		{
			const UpgradeTemplate *upgrade = command->getUpgradeTemplate();
			return upgrade ? upgrade->calcCostToBuild( player ) : 0;
		}
		default:
			return 0;
	}
}

//-------------------------------------------------------------------------------------------------
/** The root window's draw: the strip band and the beds under the controls that sit in it. Runs
	* before the bar's children are drawn, so the buttons' art lands on the beds. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneDrawBackdrop()
{
	if( !TheWindowManager || !phoneHudVisible() || !s_win.valid )
		return;
	const Metrics &m = m_phoneMetrics;

	fillRect( m.stripBand, Palette::StripFill );
	Rect edge = { 0, m.stripBand.bottom() - m.line, m.displayW, m.line };
	fillRect( edge, Palette::PanelLine );

	// money pill, power trough, the generals' star, chat and beacon
	fillRect( m.money, Palette::TileFill );
	lineRect( m.money, m.line, Palette::TileLine );
	const Rect trough = AndroidHud::inflate( m.power, m.line * 2 );
	fillRect( trough, Palette::Trough );
	lineRect( trough, m.line, Palette::TileLine );
	if( shown( s_win.star ) )
		tileBed( m.star, m.line );
	if( m.multiplayer )
	{
		if( shown( s_win.chat ) )
			tileBed( m.chat, m.line );
		if( shown( s_win.beacon ) )
			tileBed( m.beacon, m.line );
	}

	// one bed per generals' power the engine shows
	if( m_specialPowerShortcutParent && shown( m_specialPowerShortcutParent ) )
	{
		for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
		{
			GameWindow *button = m_specialPowerShortcutButtons[ i ];
			GameWindow *holder = m_specialPowerShortcutButtonParents[ i ];
			if( shown( button ) && (!holder || shown( holder )) )
				tileBed( windowRect( button ), m.line );
		}
	}

	// minimap frame, only while the player has a radar
	if( shown( s_win.radar ) )
	{
		fillRect( m.minimap, Palette::TileFill );
		lineRect( AndroidHud::inflate( m.minimap, m.line ), m.line, Palette::PanelLine );
	}
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
	m_phoneMetrics = AndroidHud::computeMetrics( W, H, AndroidHud::pixelsPerMmFor( W, H ), isMultiplayerGame() );
	const Metrics &m = m_phoneMetrics;

	// --- the named windows we lay out ---
	PhoneWindows &w = s_win;
	w.money       = findBarWindow( "ControlBar.wnd:MoneyDisplay" );
	w.power       = findBarWindow( "ControlBar.wnd:PowerWindow" );
	w.radar       = findBarWindow( "ControlBar.wnd:LeftHUD" );
	w.glow        = m_radarAttackGlowWindow;
	w.star        = findBarWindow( "ControlBar.wnd:ButtonGeneral" );
	w.chat        = m_communicatorButton;
	w.beacon      = findBarWindow( "ControlBar.wnd:ButtonPlaceBeacon" );
	w.ucDesc      = findBarWindow( "ControlBar.wnd:UnderConstructionDesc" );
	w.ucCancel    = findBarWindow( "ControlBar.wnd:ButtonCancelConstruction" );
	w.oclText     = findBarWindow( "ControlBar.wnd:OCLTimerStaticText" );
	w.oclBar      = findBarWindow( "ControlBar.wnd:OCLTimerProgressBar" );
	w.oclSell     = findBarWindow( "ControlBar.wnd:OCLTimerSellButton" );
	w.beaconEdit  = findBarWindow( "ControlBar.wnd:EditBeaconText" );
	w.beaconLabel = findBarWindow( "ControlBar.wnd:StaticTextBeaconLabel" );
	w.beaconClear = findBarWindow( "ControlBar.wnd:ButtonClearBeaconText" );
	w.beaconDel   = findBarWindow( "ControlBar.wnd:ButtonDeleteBeacon" );
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
	{
		AsciiString name;
		name.format( "ControlBar.wnd:ButtonQueue%02d", i + 1 );
		w.queue[ i ] = findBarWindow( name.str() );
	}
	w.valid = TRUE;

	LeafSet leaves, panels, containers, skip;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i ) leaves.add( m_commandWindows[ i ] );
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i ) leaves.add( w.queue[ i ] );
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i ) leaves.add( m_rightHUDUpgradeCameos[ i ] );
	leaves.add( m_rightHUDCameoWindow );
	leaves.add( w.money ); leaves.add( w.power ); leaves.add( w.radar ); leaves.add( w.glow );
	leaves.add( w.star ); leaves.add( w.chat ); leaves.add( w.beacon );
	leaves.add( w.ucDesc ); leaves.add( w.ucCancel );
	leaves.add( w.oclText ); leaves.add( w.oclBar ); leaves.add( w.oclSell );
	leaves.add( w.beaconEdit ); leaves.add( w.beaconLabel ); leaves.add( w.beaconClear ); leaves.add( w.beaconDel );

	panels.add( m_contextParent[ CP_COMMAND ] );
	panels.add( m_contextParent[ CP_BUILD_QUEUE ] );
	panels.add( m_contextParent[ CP_UNDER_CONSTRUCTION ] );
	panels.add( m_contextParent[ CP_OCL_TIMER ] );
	panels.add( m_contextParent[ CP_BEACON ] );
	panels.add( m_rightHUDUnitSelectParent );
	containers.add( m_rightHUDWindow );

	skip.add( m_contextParent[ CP_OBSERVER_LIST ] );
	skip.add( m_contextParent[ CP_OBSERVER_INFO ] );

	// 1. the root spans the display, paints the backdrop, and is the one intended hole to the
	//    battlefield: a tap that lands on nothing else reaches the world (NO_INPUT), while a tap a
	//    child ignored is blocked on its way up (the input function sees bubbled messages only)
	master->winSetPosition( 0, 0 );
	master->winSetSize( W, H );
	master->winSetStatus( WIN_STATUS_SEE_THRU | WIN_STATUS_NO_INPUT );
	master->winSetDrawFunc( phoneRootDraw );
	master->winSetInputFunc( GameWinBlockInput );
	restyleTree( master, leaves, panels, containers, skip );

	// 2. controls the phone replaces (menu = the ☰ overlay, idle worker = the left column) or drops.
	//    InGameUI keeps enabling/disabling the idle-worker button with the idle count, so the hidden
	//    window stays the source of truth for the IDLE overlay.
	m_phoneIdleWorkerButton = findBarWindow( "ControlBar.wnd:ButtonIdleWorker" );
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonLarge" ) );
	hideAndPark( m_phoneIdleWorkerButton );
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonOptions" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:GeneralsExp" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:ExpBarForeground" ) );

	// 3. top strip and minimap. Chat and beacon exist in multiplayer only (the same test the engine
	//    applies to the beacon button); the radar and its attack glow follow the player's radar.
	Rect moneyText = { m.money.x + m.pad, m.money.y, m.money.w - 2 * m.pad, m.money.h };
	place( this, w.money, moneyText );
	scaleTextFont( w.money, AndroidHud::mmToPx( 3.0f, m.pxPerMm ) );
	place( this, w.power, m.power );
	place( this, w.star, m.star );
	if( m.multiplayer )
	{
		place( this, w.chat, m.chat );
		place( this, w.beacon, m.beacon );
	}
	else
	{
		hideAndPark( w.chat );
		hideAndPark( w.beacon );
	}
	place( this, w.radar, m.minimap );
	place( this, w.glow, AndroidHud::inflate( m.minimap, m.line * 2 ) );

	// 4. the generals' powers shortcut bar: its own layout, one row of tiles in the strip
	if( m_specialPowerShortcutParent )
	{
		LeafSet powerLeaves, powerHolders, none;
		for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
		{
			powerLeaves.add( m_specialPowerShortcutButtons[ i ] );
			powerHolders.add( m_specialPowerShortcutButtonParents[ i ] );
		}
		m_specialPowerShortcutParent->winClearStatus( WIN_STATUS_NO_INPUT );
		m_specialPowerShortcutParent->winSetStatus( WIN_STATUS_SEE_THRU );
		m_specialPowerShortcutParent->winSetInputFunc( GameWinBlockInput );
		restyleTree( m_specialPowerShortcutParent, powerLeaves, none, powerHolders, none );
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

	// 5. text sizes in the drawer's small panels
	const Int textH = AndroidHud::mmToPx( 2.6f, m.pxPerMm );
	scaleTextFont( w.ucDesc, textH );
	scaleTextFont( w.oclText, textH );
	scaleTextFont( w.beaconLabel, textH );

	// 6. the drawer follows what the engine shows; force its first placement
	m_phoneDrawerSignature = 0xFFFFFFFFu;
	m_phoneDrawerShown = FALSE;
	m_phoneDrawerSlideDx = 0;
	updatePhoneDrawer();
}

//-------------------------------------------------------------------------------------------------
/** Every frame: read what the engine currently shows (which commands, portrait, cameos, queue
	* items, the small panels, the radar) into one signature, and re-lay-out the drawer only when it
	* changed or while the drawer is sliding in. Cheap: a few dozen flag reads per frame. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updatePhoneDrawer()
{
	GameWindow *master = m_contextParent[ CP_MASTER ];
	if( !master || !TheDisplay || !s_win.valid )
		return;
	const Metrics &m = m_phoneMetrics;

	UnsignedInt sig = 0;
	Bool anyCommand = FALSE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		if( shown( m_commandWindows[ i ] ) )
		{
			sig |= 1u << i;
			anyCommand = TRUE;
		}
	}
	const Bool portrait = shown( m_rightHUDUnitSelectParent );
	Int cameos = 0;
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
		if( shown( m_rightHUDUpgradeCameos[ i ] ) )
			++cameos;
	Bool queue = shown( m_contextParent[ CP_BUILD_QUEUE ] );
	if( queue )
	{
		queue = FALSE;
		for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS && !queue; ++i )
			if( s_win.queue[ i ] && s_win.queue[ i ]->winGetEnabled() )
				queue = TRUE;
	}
	const Bool uc = shown( m_contextParent[ CP_UNDER_CONSTRUCTION ] );
	const Bool ocl = shown( m_contextParent[ CP_OCL_TIMER ] );
	const Bool beacon = shown( m_contextParent[ CP_BEACON ] );
	const Bool radar = rts::localPlayerHasRadar();
	const Bool hidden = master->winIsHidden();
	if( portrait ) sig |= 1u << 18;
	sig |= (UnsignedInt)cameos << 19;          // 3 bits
	if( queue )  sig |= 1u << 22;
	if( uc )     sig |= 1u << 23;
	if( ocl )    sig |= 1u << 24;
	if( beacon ) sig |= 1u << 25;
	if( radar )  sig |= 1u << 26;
	if( m_radarAttackGlowOn ) sig |= 1u << 27;
	if( hidden ) sig |= 1u << 28;

	const Bool drawerOpen = !hidden && (anyCommand || portrait || queue || uc || ocl || beacon);
	const UnsignedInt now = nowMs();
	if( drawerOpen && !m_phoneDrawerShown )
		m_phoneSlideStartMs = now;
	m_phoneDrawerShown = drawerOpen;
	Int dx = 0;
	if( drawerOpen )
	{
		const UnsignedInt t = now - m_phoneSlideStartMs;
		if( t < DRAWER_SLIDE_MS )
		{
			const Real u = 1.0f - (Real)t / (Real)DRAWER_SLIDE_MS;   // ease-out cubic from the right edge
			dx = (Int)(u * u * u * (Real)(m.displayW - (m.grid.x - m.pad)));
		}
	}

	if( sig == m_phoneDrawerSignature && dx == m_phoneDrawerSlideDx )
		return;
	m_phoneDrawerSignature = sig;
	m_phoneDrawerSlideDx = dx;

	// the minimap exists only with a radar; the glow only while the base is under attack
	if( s_win.radar )
		s_win.radar->winHide( !radar );
	if( s_win.glow )
		s_win.glow->winHide( !(radar && m_radarAttackGlowOn) );

	layoutPhoneDrawer( dx );
}

//-------------------------------------------------------------------------------------------------
/** Place the drawer for what is visible now. Bottom-right, growing upward: the command grid with
	* the visible commands packed into the first cells (no holes), the small context panels when the
	* engine shows them, then the info row — the portrait panel (portrait + upgrade/veterancy cameos)
	* at the right and, while producing, the queue panel beside it. Panels that have nothing to show
	* are parked at zero size and paint nothing. `slideDx` shifts everything right while sliding in. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutPhoneDrawer( Int slideDx )
{
	using namespace AndroidHud;
	const Metrics &m = m_phoneMetrics;
	Rect footprint = { 0, 0, 0, 0 };
	Int bottom = m.displayH - m.margin;        // the next panel's bottom edge, moving up
	const Int panelX = m.grid.x - m.pad, panelW = m.grid.w + 2 * m.pad;

	// 1. command grid: pack the visible buttons into cells from the bottom-left
	Int n = 0;
	for( Int c = 0; c < GRID_CELLS; ++c )
		m_phoneCellWindow[ c ] = nullptr;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		GameWindow *w = m_commandWindows[ i ];
		if( shown( w ) && n < GRID_CELLS )
			m_phoneCellWindow[ n++ ] = w;
	}
	GameWindow *command = m_contextParent[ CP_COMMAND ];
	if( n > 0 )
	{
		const Int rows = (n + GRID_COLS - 1) / GRID_COLS;
		const Int top = gridTopForRows( m, rows );
		Rect panel = { panelX, top - m.pad, panelW, m.grid.bottom() - top + 2 * m.pad };
		place( this, command, panel, slideDx );
		for( Int c = 0; c < n; ++c )
			place( this, m_phoneCellWindow[ c ], commandCellRect( m, c ), slideDx );
		unionRect( footprint, panel );
		bottom = panel.y - m.gap;
	}
	else
	{
		park( command );
	}

	// 2. the small context panels: one row (two for the beacon) of the grid's width
	const Int textH = mmToPx( 2.6f, m.pxPerMm );
	GameWindow *ucPanel = m_contextParent[ CP_UNDER_CONSTRUCTION ];
	if( shown( ucPanel ) )
	{
		Rect panel = { panelX, bottom - (m.tileH + 2 * m.pad), panelW, m.tileH + 2 * m.pad };
		Rect tile = { m.grid.x, panel.y + m.pad, m.tileW, m.tileH };
		Rect text = { tile.right() + m.gap, tile.y, m.grid.right() - tile.right() - m.gap, m.tileH };
		place( this, ucPanel, panel, slideDx );
		place( this, s_win.ucCancel, tile, slideDx );
		place( this, s_win.ucDesc, text, slideDx );
		unionRect( footprint, panel );
		bottom = panel.y - m.gap;
	}
	else
	{
		park( ucPanel );
	}

	GameWindow *oclPanel = m_contextParent[ CP_OCL_TIMER ];
	if( shown( oclPanel ) )
	{
		Rect panel = { panelX, bottom - (m.tileH + 2 * m.pad), panelW, m.tileH + 2 * m.pad };
		Rect tile = { m.grid.x, panel.y + m.pad, m.tileW, m.tileH };
		const Int textW = m.grid.right() - tile.right() - m.gap;
		Rect text = { tile.right() + m.gap, tile.y, textW, textH + m.gap };
		Rect bar  = { tile.right() + m.gap, tile.bottom() - (m.tileH - textH - 2 * m.gap), textW, m.tileH - textH - 2 * m.gap };
		place( this, oclPanel, panel, slideDx );
		place( this, s_win.oclSell, tile, slideDx );
		place( this, s_win.oclText, text, slideDx );
		place( this, s_win.oclBar, bar, slideDx );
		unionRect( footprint, panel );
		bottom = panel.y - m.gap;
	}
	else
	{
		park( oclPanel );
	}

	GameWindow *beaconPanel = m_contextParent[ CP_BEACON ];
	if( shown( beaconPanel ) )
	{
		const Int innerH = 2 * m.tileH + m.gap;
		Rect panel = { panelX, bottom - (innerH + 2 * m.pad), panelW, innerH + 2 * m.pad };
		Rect upper = { m.grid.x, panel.y + m.pad, m.tileW, m.tileH };
		Rect lower = { m.grid.x, upper.bottom() + m.gap, m.tileW, m.tileH };
		Rect edit  = { upper.right() + m.gap, upper.y + (m.tileH - textH - 2 * m.gap) / 2, m.grid.right() - upper.right() - m.gap, textH + 2 * m.gap };
		Rect del   = { lower.right() + m.gap, lower.y, m.tileW, m.tileH };
		place( this, beaconPanel, panel, slideDx );
		place( this, s_win.beaconLabel, upper, slideDx );
		place( this, s_win.beaconEdit, edit, slideDx );
		place( this, s_win.beaconClear, lower, slideDx );
		place( this, s_win.beaconDel, del, slideDx );
		unionRect( footprint, panel );
		bottom = panel.y - m.gap;
	}
	else
	{
		park( beaconPanel );
	}

	// 3. the info row: portrait panel at the right, queue panel beside it while producing
	Int infoRight = m.grid.right() + m.pad;
	if( shown( m_rightHUDUnitSelectParent ) )
	{
		Int cameos = 0;
		for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
			if( shown( m_rightHUDUpgradeCameos[ i ] ) )
				++cameos;
		const Int cameoRows = (cameos + CAMEO_COLS - 1) / CAMEO_COLS;
		const Int innerH = m.tileH + cameoRows * (m.gap + m.cameoTile);
		Rect panel = { infoRight - (m.tileW + 2 * m.pad), bottom - (innerH + 2 * m.pad), m.tileW + 2 * m.pad, innerH + 2 * m.pad };
		Rect portrait = { panel.x + m.pad, panel.y + m.pad, m.tileW, m.tileH };
		place( this, m_rightHUDWindow, panel, slideDx );            // container (see-through, blocks input)
		place( this, m_rightHUDUnitSelectParent, panel, slideDx );  // panel (paints the background)
		place( this, m_rightHUDCameoWindow, portrait, slideDx );
		Int k = 0;
		for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
		{
			GameWindow *cameo = m_rightHUDUpgradeCameos[ i ];
			if( !shown( cameo ) )
				continue;
			Rect c = { portrait.x + (k % CAMEO_COLS) * (m.cameoTile + m.gap),
								 portrait.bottom() + m.gap + (k / CAMEO_COLS) * (m.cameoTile + m.gap),
								 m.cameoTile, m.cameoTile };
			place( this, cameo, c, slideDx );
			++k;
		}
		unionRect( footprint, panel );
		infoRight = panel.x - m.gap;
	}
	else
	{
		park( m_rightHUDWindow );
	}

	GameWindow *queuePanel = m_contextParent[ CP_BUILD_QUEUE ];
	Bool queueHasItems = FALSE;
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS && !queueHasItems; ++i )
		if( s_win.queue[ i ] && s_win.queue[ i ]->winGetEnabled() )
			queueHasItems = TRUE;
	if( shown( queuePanel ) && queueHasItems )
	{
		const Int innerW = QUEUE_COLS * m.queueTile + (QUEUE_COLS - 1) * m.gap;
		const Int innerH = QUEUE_ROWS * m.queueTile + (QUEUE_ROWS - 1) * m.gap;
		Rect panel = { infoRight - (innerW + 2 * m.pad), bottom - (innerH + 2 * m.pad), innerW + 2 * m.pad, innerH + 2 * m.pad };
		place( this, queuePanel, panel, slideDx );
		for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
		{
			const Int row = i / QUEUE_COLS, col = i % QUEUE_COLS;
			Rect q = { panel.x + m.pad + col * (m.queueTile + m.gap), panel.y + m.pad + row * (m.queueTile + m.gap), m.queueTile, m.queueTile };
			place( this, s_win.queue[ i ], q, slideDx );
		}
		unionRect( footprint, panel );
	}
	else
	{
		park( queuePanel );
	}

	m_phoneDrawerRect = footprint;
}

#endif // __ANDROID__
