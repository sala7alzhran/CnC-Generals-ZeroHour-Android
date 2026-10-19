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
// On a phone the 4:3 command bar is replaced by re-laying-out its own GameWindows at runtime and
// drawing every HUD surface here, in one visual language: the battlefield gets the whole screen; a
// round menu button and one resource pill sit top-left; the left thumb gets a column of round
// buttons (IDLE with the worker's own picture, ARMY, "+" for a new group, the control groups once
// created); the minimap sits top-right in a thin gold frame with the rank badge beside it and the
// generals' powers as round buttons with cooldown rings under it; the right thumb gets a drawer:
// a chip row (portrait, name, health, tabs) over up to two rows of five tiles, each tile its cameo
// over its name with the price. The engine keeps populating and processing those buttons exactly
// as on the PC, so every faction, general and mod works unchanged; the .wnd layout files are game
// data and are not modified, and no artwork is shipped: every HUD shape is drawn from primitives.
//
// Hit-testing rules this file relies on (GameWindowManager::winProcessMouseEvent, GameWindow::
// winPointInChild): the first child whose rectangle contains the point wins, descendants are only
// reachable through ancestors whose rectangles contain them, a final NO_INPUT window hands the tap
// to the battlefield, and a message every window in the parent chain ignores also reaches the
// battlefield. So: every piece of bar artwork is parked off-screen at zero size (never hit), the
// panels are see-through containers that block input, and the root keeps NO_INPUT so a tap on an
// empty area is the one intended hole to the world. A see-through window is not drawn by the
// window manager (its children are), so the root's draw paints every bed under the bar's windows.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(__ANDROID__)

#include <chrono>
#include <math.h>

#include "Common/Energy.h"
#include "Common/GameUtility.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/AndroidHud.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/Squad.h"
#include "GameNetwork/GameInfo.h"

Real TheAndroidPixelsPerMm = 0.0f;
Int  TheAndroidOverlayPressed = -1;

namespace
{

using AndroidHud::Metrics;
using AndroidHud::Rect;
namespace Palette = AndroidHud::Palette;

const UnsignedInt DRAWER_SLIDE_MS = 160;   ///< the drawer slides in from the right edge over this long
const Real TWO_PI = 6.2831853f;

inline UnsignedInt nowMs()
{
	using namespace std::chrono;
	return (UnsignedInt)duration_cast<milliseconds>( steady_clock::now().time_since_epoch() ).count();
}

// ------------------------------------------------------------------------------- primitives ---
inline void fillRect( const Rect &r, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheDisplay->drawFillRect( r.x, r.y, r.w, r.h, color );
}
inline void fillRound( const Rect &r, Int radius, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheDisplay->drawFillRoundRect( r.x, r.y, r.w, r.h, radius, color );
}
inline void lineRound( const Rect &r, Int radius, Int line, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheDisplay->drawOpenRoundRect( r.x, r.y, r.w, r.h, radius, (Real)line, color );
}
inline void fillCircle( const Rect &r, UnsignedInt color )
{
	TheDisplay->drawFillCircle( r.centerX(), r.centerY(), r.w / 2, color );
}
inline void ring( const Rect &r, Int inset, Int line, Real fromTurn, Real toTurn, UnsignedInt color )
{
	TheDisplay->drawArc( r.centerX(), r.centerY(), r.w / 2 - inset, (Real)line, fromTurn, toTurn, color );
}
inline Rect square( Int cx, Int cy, Int side ) { Rect r = { cx - side / 2, cy - side / 2, side, side }; return r; }
inline UnsignedInt withAlpha( UnsignedInt color, Int alpha )
{
	if( alpha < 0 ) alpha = 0;
	if( alpha > 255 ) alpha = 255;
	return (color & 0x00FFFFFFu) | ((UnsignedInt)alpha << 24);
}
inline UnsignedInt lerpColor( UnsignedInt a, UnsignedInt b, Real t )
{
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;
	UnsignedInt out = 0;
	for( Int shift = 0; shift < 32; shift += 8 )
	{
		const Int ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
		out |= (UnsignedInt)(ca + (Int)((cb - ca) * t)) << shift;
	}
	return out;
}
/// 0..1 slow pulse for "look here" states (promotion points, a ready power).
inline Real pulse()
{
	const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	return 0.5f + 0.5f * sinf( (Real)frame * 0.14f );
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

// ------------------------------------------------------------------------------------ text ---
/// A text object created once and re-set only when its content changes: nothing allocates per
/// frame. `key` / `value` remember what the text was last built from.
struct CachedText
{
	DisplayString *str;
	const void *key;
	Int value;
};

enum PhoneFont CPP_11(: Int) { FONT_MONEY = 0, FONT_CHIP, FONT_SMALL, FONT_TILE, FONT_PRICE, FONT_COIN, FONT_COUNT };
const Real kFontMm[ FONT_COUNT ] = { 3.0f, 2.2f, 1.9f, 1.5f, 1.7f, 2.8f };
GameFont *s_font[ FONT_COUNT ] = { nullptr };

CachedText s_money = { nullptr, nullptr, -1 };
CachedText s_coin = { nullptr, nullptr, -1 };
CachedText s_chipName = { nullptr, nullptr, -1 };
CachedText s_chipCount = { nullptr, nullptr, -1 };
CachedText s_idleCount = { nullptr, nullptr, -1 };
CachedText s_rankPoints = { nullptr, nullptr, -1 };
CachedText s_groupNumber[ AndroidHud::NUM_CONTROL_GROUPS ] = { { nullptr, nullptr, -1 } };
CachedText s_ghostNumber[ AndroidHud::NUM_CONTROL_GROUPS ] = { { nullptr, nullptr, -1 } };
CachedText s_groupCount[ AndroidHud::NUM_CONTROL_GROUPS ] = { { nullptr, nullptr, -1 } };
CachedText s_tabCaption[ AndroidHud::NUM_TABS ] = { { nullptr, nullptr, -1 } };
CachedText s_tileName[ AndroidHud::GRID_CELLS ] = { { nullptr, nullptr, -1 } };
CachedText s_tilePrice[ AndroidHud::GRID_CELLS ] = { { nullptr, nullptr, -1 } };

GameFont *font( PhoneFont which )
{
	if( !s_font[ which ] && TheControlBar )
		s_font[ which ] = TheControlBar->getPhoneHudFont( kFontMm[ which ] );
	return s_font[ which ];
}

/// The text object, created on first use with its font. Returns nullptr while the string manager
/// is not up (never during a game).
DisplayString *ensure( CachedText &t, PhoneFont which )
{
	if( !t.str && TheDisplayStringManager )
	{
		t.str = TheDisplayStringManager->newDisplayString();
		if( t.str && font( which ) )
			t.str->setFont( font( which ) );
		t.key = nullptr;
		t.value = -1;
	}
	return t.str;
}
void release( CachedText &t )
{
	if( t.str && TheDisplayStringManager )
		TheDisplayStringManager->freeDisplayString( t.str );
	t.str = nullptr;
	t.key = nullptr;
	t.value = -1;
}
/// Set the text to a number if it changed.
DisplayString *numberText( CachedText &t, PhoneFont which, Int value )
{
	DisplayString *s = ensure( t, which );
	if( s && t.value != value )
	{
		t.value = value;
		UnicodeString text;
		text.format( L"%d", value );
		s->setText( text );
	}
	return s;
}
/// "12,500": the money with thousands separators, re-set only when the amount changes.
DisplayString *moneyText( CachedText &t, Int value )
{
	DisplayString *s = ensure( t, FONT_MONEY );
	if( s && t.value != value )
	{
		t.value = value;
		WideChar reversed[ 24 ];
		Int n = 0, digits = 0, v = value < 0 ? -value : value;
		do
		{
			if( digits > 0 && digits % 3 == 0 )
				reversed[ n++ ] = L',';
			reversed[ n++ ] = (WideChar)(L'0' + v % 10);
			v /= 10;
			++digits;
		} while( v > 0 && n < 20 );
		if( value < 0 )
			reversed[ n++ ] = L'-';
		UnicodeString text;
		for( Int i = n - 1; i >= 0; --i )
			text.concat( reversed[ i ] );
		s->setText( text );
	}
	return s;
}
/// A game text without the '&' that marks the PC hotkey letter.
UnicodeString withoutHotkeyMarker( const UnicodeString &raw )
{
	UnicodeString clean;
	for( Int i = 0; i < raw.getLength(); ++i )
		if( raw.getCharAt( i ) != L'&' )
			clean.concat( raw.getCharAt( i ) );
	return clean;
}

void drawCentered( DisplayString *s, const Rect &r, UnsignedInt color )
{
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, color, Palette::TextShadow );
}
/// A small number in a round badge anchored at the bottom-right of a control.
void drawBadge( DisplayString *s, const Rect &control, const Metrics &m, UnsignedInt fillColor = Palette::BadgeFill, UnsignedInt textColor = Palette::BadgeText )
{
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	Int d = h + m.gap;
	if( d < w + m.gap )
		d = w + m.gap;
	const Rect badge = { control.right() - d + m.line, control.bottom() - d + m.line, d, d };
	fillCircle( badge, fillColor );
	drawCentered( s, badge, textColor );
}
/// A small text in a dark pill anchored by its bottom-right corner (the price on a tile).
void drawPricePill( DisplayString *s, Int right, Int bottom, const Metrics &m, UnsignedInt textColor )
{
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	const Int padX = m.gap / 2 + 1, padY = 1;
	const Rect pill = { right - w - 2 * padX, bottom - h - 2 * padY, w + 2 * padX, h + 2 * padY };
	fillRound( pill, pill.h / 2, Palette::CostFill );
	s->draw( pill.x + padX, pill.y + padY, textColor, Palette::TextShadow );
}

// ---------------------------------------------------------------------------------- glyphs ---
/// A round button's bed and outline. A ghost (the "+" slot) is an outline only.
void drawRoundBed( const Rect &r, const Metrics &m, Bool pressed, Bool ghost )
{
	if( !ghost )
		fillCircle( r, pressed ? Palette::ButtonFillHot : Palette::ButtonFill );
	ring( r, m.line, m.line, 0.0f, 1.0f, pressed ? Palette::GoldBright : (ghost ? Palette::GhostLine : Palette::ButtonLine) );
}
void drawMenuGlyph( const Rect &r, UnsignedInt color )
{
	const Int barH = r.h / 14 < 2 ? 2 : r.h / 14;
	const Int barW = r.w * 2 / 5;
	const Int bx = r.x + (r.w - barW) / 2, cy = r.centerY(), step = barH * 5 / 2;
	for( Int k = -1; k <= 1; ++k )
		TheDisplay->drawFillRect( bx, cy + k * step - barH / 2, barW, barH, color );
}
void drawPlusGlyph( const Rect &r, UnsignedInt color )
{
	const Int t = r.h / 14 < 2 ? 2 : r.h / 14, len = r.w * 2 / 5;
	const Int cx = r.centerX(), cy = r.centerY();
	TheDisplay->drawFillRect( cx - len / 2, cy - t / 2, len, t, color );
	TheDisplay->drawFillRect( cx - t / 2, cy - len / 2, t, len, color );
}
/// Two figures, head and shoulders: the ARMY button.
void drawArmyGlyph( const Rect &r, UnsignedInt color )
{
	const Int d = r.w;
	const Int cx = r.centerX(), cy = r.centerY();
	const Int headR = d * 7 / 100 < 2 ? 2 : d * 7 / 100;
	const Int bodyW = d * 26 / 100, bodyH = d * 19 / 100;
	for( Int k = -1; k <= 1; k += 2 )
	{
		const Int fx = cx + k * d * 11 / 100;
		TheDisplay->drawFillCircle( fx, cy - d * 12 / 100, headR, color );
		TheDisplay->drawFillRoundRect( fx - bodyW / 2, cy - d * 1 / 100, bodyW, bodyH, bodyW / 2, color );
	}
}
/// A padlock: a dark box, a body, a shackle of three lines. `box` is where it goes.
void drawLockGlyph( const Rect &box, Int line )
{
	fillRound( box, box.h / 5, Palette::LockFill );
	const Int s = box.h;
	const Int bw = s * 5 / 8, bh = s * 3 / 8;
	const Int bx = box.x + (s - bw) / 2, by = box.y + s - bh - s / 8;
	TheDisplay->drawFillRect( bx, by, bw, bh, Palette::Glyph );
	const Real lw = (Real)(line * 2);
	const Int sw = bw * 3 / 5, sx0 = bx + (bw - sw) / 2, sx1 = sx0 + sw, sy = by - bh * 3 / 4;
	TheDisplay->drawLine( sx0, by, sx0, sy, lw, Palette::Glyph );
	TheDisplay->drawLine( sx1, by, sx1, sy, lw, Palette::Glyph );
	TheDisplay->drawLine( sx0, sy, sx1, sy, lw, Palette::Glyph );
}
/// A lightning bolt in `r`: the power glyph of the resource pill.
void drawBoltGlyph( const Rect &r, UnsignedInt color )
{
	const Real t = (Real)(r.w / 5 < 2 ? 2 : r.w / 5);
	TheDisplay->drawLine( r.x + r.w * 65 / 100, r.y, r.x + r.w * 30 / 100, r.y + r.h * 55 / 100, t, color );
	TheDisplay->drawLine( r.x + r.w * 30 / 100, r.y + r.h * 55 / 100, r.x + r.w * 70 / 100, r.y + r.h * 45 / 100, t, color );
	TheDisplay->drawLine( r.x + r.w * 70 / 100, r.y + r.h * 45 / 100, r.x + r.w * 35 / 100, r.y + r.h, t, color );
}
/// A five-point star centred in `r` (a fan from the centre: the centre sees every edge of a star).
void drawStarGlyph( const Rect &r, UnsignedInt color )
{
	const Real cx = (Real)r.centerX(), cy = (Real)r.centerY();
	const Real outer = (Real)r.w * 0.34f, inner = outer * 0.45f;
	Real px = cx, py = cy - outer;
	for( Int i = 1; i <= 10; ++i )
	{
		const Real rad = (i & 1) ? inner : outer;
		const Real a = TWO_PI * (Real)i / 10.0f;
		const Real nx = cx + rad * sinf( a ), ny = cy - rad * cosf( a );
		TheDisplay->drawFillTriangle( (Int)cx, (Int)cy, (Int)px, (Int)py, (Int)nx, (Int)ny, color );
		px = nx;
		py = ny;
	}
}
/// A cameo image inset in a round button: the largest square inside the circle, scaled to 70 %.
void drawInsetImage( const Image *image, const Rect &button, UnsignedInt colorMultiplier )
{
	if( !image )
		return;
	const Int side = button.w * 70 / 100;
	const Rect box = square( button.centerX(), button.centerY(), side );
	TheDisplay->drawImage( image, box.x, box.y, box.right(), box.bottom(), colorMultiplier );
}

// --------------------------------------------------------------------------- window helpers ---
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
	GameFont *f = win->winGetFont();
	if( !f || f->pointSize == pixelHeight )
		return;
	GameFont *sized = TheFontLibrary->getFont( f->nameString, pixelHeight, f->bold );
	if( sized )
		GadgetStaticTextSetFont( win, sized );
}
inline void place( ControlBar *bar, GameWindow *win, const Rect &r, Int dx = 0 )
{
	bar->placePhoneWindow( win, r.x + dx, r.y, r.w, r.h );
}

// The draw functions the layout installs on the bar's windows forward to the ControlBar, which
// owns the layout state they need.
void phoneRootDraw( GameWindow *, WinInstanceData * )
{
	if( TheControlBar )
		TheControlBar->phoneDrawBackdrop();
}
void phoneTileDraw( GameWindow *window, WinInstanceData *inst )
{
	if( TheControlBar )
		TheControlBar->phoneDrawTile( window, inst );
}
void phonePowerDraw( GameWindow *window, WinInstanceData *inst )
{
	if( TheControlBar )
		TheControlBar->phoneDrawPowerButton( window, inst );
}
void phoneRankDraw( GameWindow *window, WinInstanceData *inst )
{
	if( TheControlBar )
		TheControlBar->phoneDrawRankBadge( window, inst );
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
/// Containers are see-through (not drawn; the root paints their beds) and block input, sized by the
/// layout to cover their children. Everything else is artwork and is parked. A window we did not
/// expect to hold a kept descendant becomes a container covering its parent, so the descendant stays
/// reachable. Observer panels are left exactly as they are. Returns whether the subtree holds
/// anything kept.
Bool restyleTree( GameWindow *parent, const LeafSet &leaves, const LeafSet &containers, const LeafSet &skip )
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
		const Bool below = restyleTree( child, leaves, containers, skip );
		if( containers.has( child ) || below )
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

const WideChar *const kBuilderTabs[ AndroidHud::NUM_TABS ] = { L"BASE", L"DEF", L"SUP" };
const WideChar *const kOtherTabs[ AndroidHud::NUM_TABS ]   = { L"UNITS", L"TECH", L"MORE" };

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
/** The HUD's text font at a given physical height: the bar's own text face, bold. */
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
void ControlBar::phoneReleaseText()
{
	release( s_money );
	release( s_coin );
	release( s_chipName );
	release( s_chipCount );
	release( s_idleCount );
	release( s_rankPoints );
	for( Int g = 0; g < AndroidHud::NUM_CONTROL_GROUPS; ++g )
	{
		release( s_groupNumber[ g ] );
		release( s_ghostNumber[ g ] );
		release( s_groupCount[ g ] );
	}
	for( Int t = 0; t < AndroidHud::NUM_TABS; ++t )
		release( s_tabCaption[ t ] );
	for( Int c = 0; c < AndroidHud::GRID_CELLS; ++c )
	{
		release( s_tileName[ c ] );
		release( s_tilePrice[ c ] );
	}
	for( Int f = 0; f < FONT_COUNT; ++f )
		s_font[ f ] = nullptr;   // fonts belong to the library; re-fetched for the next game's density
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::phoneHudVisible() const
{
	GameWindow *master = m_contextParent[ CP_MASTER ];
	return master != nullptr && !master->winIsHidden() && !m_isObserverCommandBar;
}

//-------------------------------------------------------------------------------------------------
/** The overlay buttons that exist this frame. Left column from the top: IDLE while a worker is idle
	* (with the count), ARMY, one "+" slot for the first empty group while something is selected (hold
	* to assign), then the groups that hold units (with their count). The round ☰ menu is always
	* top-left. The drawer's tabs exist while the open menu is split into tabs; the tabs that have
	* commands are packed to the right of the chip row. */
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
		out[ i ].active = FALSE;
		out[ i ].badge = 0;
	}
	if( !phoneHudVisible() )
		return;

	out[ OVERLAY_MENU ].rect = m.menu;
	out[ OVERLAY_MENU ].visible = TRUE;

	Int slot = 0;
	const Int idle = TheInGameUI ? TheInGameUI->androidIdleWorkers( nullptr ) : 0;
	if( idle > 0 )
	{
		out[ OVERLAY_IDLE_WORKER ].rect = leftSlotRect( m, slot++ );
		out[ OVERLAY_IDLE_WORKER ].visible = TRUE;
		out[ OVERLAY_IDLE_WORKER ].badge = idle;
	}
	out[ OVERLAY_SELECT_ALL ].rect = leftSlotRect( m, slot++ );
	out[ OVERLAY_SELECT_ALL ].visible = TRUE;

	Player *player = rts::getObservedOrLocalPlayer();
	Int count[ NUM_CONTROL_GROUPS ];
	Int firstEmpty = -1;
	for( Int g = 0; g < NUM_CONTROL_GROUPS; ++g )
	{
		count[ g ] = 0;
		if( player )
		{
			Squad *squad = player->getHotkeySquad( g );
			if( squad )
				count[ g ] = (Int)squad->getLiveObjects().size();
		}
		if( count[ g ] == 0 && firstEmpty < 0 )
			firstEmpty = g;
	}
	if( firstEmpty >= 0 && TheInGameUI && TheInGameUI->getSelectCount() > 0 )
	{
		OverlayState &o = out[ OVERLAY_GROUP_1 + firstEmpty ];
		o.rect = leftSlotRect( m, slot++ );
		o.visible = TRUE;
		o.ghost = TRUE;
	}
	for( Int g = 0; g < NUM_CONTROL_GROUPS; ++g )
	{
		if( count[ g ] <= 0 )
			continue;
		OverlayState &o = out[ OVERLAY_GROUP_1 + g ];
		o.rect = leftSlotRect( m, slot++ );
		o.visible = TRUE;
		o.badge = count[ g ];
	}

	if( m_phoneDrawerShown && m_phoneMenuHasTabs )
	{
		Int visibleTabs = 0;
		for( Int t = 0; t < NUM_TABS; ++t )
			if( m_phoneTabCount[ t ] > 0 )
				++visibleTabs;
		Int tabSlot = NUM_TABS - visibleTabs;
		for( Int t = 0; t < NUM_TABS; ++t )
		{
			if( m_phoneTabCount[ t ] <= 0 )
				continue;
			OverlayState &o = out[ OVERLAY_TAB_0 + t ];
			o.rect = tabRect( m, m_phoneRows, tabSlot++ );
			o.visible = TRUE;
			o.active = (t == m_phoneTab);
			o.badge = (o.active && m_phonePages > 1) ? m_phonePage + 1 : 0;
		}
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
Int ControlBar::getPhoneCommandCell( const GameWindow *button ) const
{
	for( Int c = 0; c < AndroidHud::GRID_CELLS; ++c )
		if( m_phoneCellWindow[ c ] == button && button != nullptr )
			return c;
	return -1;
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
/** Which drawer tab a command belongs to. A builder's menu is split by the structure's kind: BASE
	* (power, production, supply, airfield, command centre), DEF (base defences), SUP (everything
	* else: tech, superweapon, drop zone, black market, fakes...). Any other large menu: UNITS (what
	* it produces, who it holds), TECH (upgrades, sciences), MORE (abilities, sell, rally, stop...). */
//-------------------------------------------------------------------------------------------------
Int ControlBar::phoneCommandTab( const CommandButton *command, Bool builderMenu ) const
{
	if( !command )
		return 0;
	if( builderMenu )
	{
		if( command->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT )
			return 0;
		const ThingTemplate *thing = command->getThingTemplate();
		if( !thing )
			return 2;
		if( thing->isKindOf( KINDOF_FS_POWER ) || thing->isKindOf( KINDOF_FS_FACTORY ) || thing->isKindOf( KINDOF_FS_WARFACTORY )
				|| thing->isKindOf( KINDOF_FS_BARRACKS ) || thing->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || thing->isKindOf( KINDOF_FS_AIRFIELD )
				|| thing->isKindOf( KINDOF_COMMANDCENTER ) )
			return 0;
		if( thing->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			return 1;
		return 2;
	}
	switch( command->getCommandType() )
	{
		case GUI_COMMAND_UNIT_BUILD:
		case GUI_COMMAND_DOZER_CONSTRUCT:
		case GUI_COMMAND_EXIT_CONTAINER:
		case GUI_COMMAND_EVACUATE:
		case GUI_COMMAND_EXECUTE_RAILED_TRANSPORT:
			return 0;
		case GUI_COMMAND_PLAYER_UPGRADE:
		case GUI_COMMAND_OBJECT_UPGRADE:
		case GUI_COMMAND_PURCHASE_SCIENCE:
			return 1;
		default:
			return 2;
	}
}

//-------------------------------------------------------------------------------------------------
const WideChar *ControlBar::phoneTabLabel( Int tab ) const
{
	if( tab < 0 || tab >= AndroidHud::NUM_TABS )
		return L"";
	return m_phoneBuilderMenu ? kBuilderTabs[ tab ] : kOtherTabs[ tab ];
}

//-------------------------------------------------------------------------------------------------
/** A tap on a drawer tab: show it. The active tab tapped again shows its next page (a tab with more
	* commands than one page holds). The change is picked up by the next drawer pass. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneSelectTab( Int tab )
{
	if( !m_phoneMenuHasTabs || tab < 0 || tab >= AndroidHud::NUM_TABS || m_phoneTabCount[ tab ] <= 0 )
		return;
	if( tab == m_phoneTab )
	{
		if( m_phonePages > 1 )
			m_phonePage = (m_phonePage + 1) % m_phonePages;
	}
	else
	{
		m_phoneTab = tab;
		m_phonePage = 0;
	}
}

//-------------------------------------------------------------------------------------------------
/** Generals' power shortcuts the engine shows right now (their row count shapes the layout). */
//-------------------------------------------------------------------------------------------------
Int ControlBar::phoneShownPowerCount() const
{
	if( !m_specialPowerShortcutParent || m_specialPowerShortcutParent->winIsHidden() )
		return 0;
	Int n = 0;
	for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
	{
		GameWindow *button = m_specialPowerShortcutButtons[ i ];
		GameWindow *holder = m_specialPowerShortcutButtonParents[ i ];
		if( shown( button ) && (!holder || shown( holder )) )
			++n;
	}
	return n;
}

//-------------------------------------------------------------------------------------------------
/** The root window's draw: every HUD surface that is not a bar window, painted before the bar's
	* children so their art lands on top: the round menu, the resource pill, the left column, the
	* minimap frame, the drawer's bed with its chip row and tabs, chat and beacon beds. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneDrawBackdrop()
{
	using namespace AndroidHud;
	if( !TheDisplay || !phoneHudVisible() || !s_win.valid )
		return;
	const Metrics &m = m_phoneMetrics;
	OverlayState ov[ OVERLAY_COUNT ];
	phoneOverlays( ov );
	Player *player = rts::getObservedOrLocalPlayer();

	// --- ☰ menu ---
	{
		const Bool pressed = TheAndroidOverlayPressed == OVERLAY_MENU;
		drawRoundBed( m.menu, m, pressed, FALSE );
		drawMenuGlyph( m.menu, Palette::Glyph );
	}

	// --- resource pill: coin, money, bolt, power bar ---
	fillRound( m.pill, m.pill.h / 2, Palette::PanelFill );
	lineRound( m.pill, m.pill.h / 2, m.line, Palette::PanelLine );
	fillCircle( m.coin, Palette::Coin );
	if( DisplayString *coin = ensure( s_coin, FONT_COIN ) )
	{
		if( s_coin.value != 1 )
		{
			s_coin.value = 1;
			coin->setText( UnicodeString( L"$" ) );
		}
		drawCentered( coin, m.coin, Palette::CoinText );
	}
	if( player )
	{
		if( DisplayString *money = moneyText( s_money, player->getMoney()->countMoney() ) )
		{
			Int w = 0, h = 0;
			money->getSize( &w, &h );
			money->draw( m.moneyText.x, m.moneyText.y + (m.moneyText.h - h) / 2, Palette::Text, Palette::TextShadow );
		}
		const Energy *energy = player->getEnergy();
		const Int production = energy ? energy->getProduction() : 0;
		const Int consumption = energy ? energy->getConsumption() : 0;
		const Bool good = production >= consumption;
		const Rect bolt = { m.powerBar.x - m.gap - mmToPx( 2.6f, m.pxPerMm ), m.pill.y + m.pill.h / 4, mmToPx( 2.6f, m.pxPerMm ), m.pill.h / 2 };
		drawBoltGlyph( bolt, good ? Palette::Good : Palette::Bad );
		fillRound( m.powerBar, m.powerBar.h / 2, Palette::Trough );
		Real fraction = 1.0f;
		if( consumption > 0 )
			fraction = production >= consumption ? 1.0f : (Real)production / (Real)consumption;
		else if( production == 0 )
			fraction = 0.0f;
		Rect fillBar = m.powerBar;
		fillBar.w = (Int)(fraction * (Real)m.powerBar.w);
		fillRound( fillBar, m.powerBar.h / 2, good ? Palette::Good : Palette::Bad );
		if( consumption > 0 && production > consumption )
		{
			// the consumption mark: how much of the supply is in use
			const Int markX = m.powerBar.x + (Int)((Real)consumption / (Real)production * (Real)m.powerBar.w);
			TheDisplay->drawFillRect( markX, m.powerBar.y - m.line, m.line, m.powerBar.h + 2 * m.line, Palette::Text );
		}
	}

	// --- left column: IDLE (worker picture + count), ARMY, "+", groups ---
	if( ov[ OVERLAY_IDLE_WORKER ].visible )
	{
		const OverlayState &o = ov[ OVERLAY_IDLE_WORKER ];
		drawRoundBed( o.rect, m, TheAndroidOverlayPressed == OVERLAY_IDLE_WORKER, FALSE );
		const ThingTemplate *worker = nullptr;
		if( TheInGameUI )
			TheInGameUI->androidIdleWorkers( &worker );
		const Image *picture = worker ? worker->getButtonImage() : nullptr;
		if( !picture && worker )
			picture = worker->getSelectedPortraitImage();
		if( picture )
			drawInsetImage( picture, o.rect, 0xFFFFFFFF );
		else
			drawPlusGlyph( o.rect, Palette::GlyphDim );
		if( DisplayString *n = numberText( s_idleCount, FONT_SMALL, o.badge ) )
			drawBadge( n, o.rect, m );
	}
	if( ov[ OVERLAY_SELECT_ALL ].visible )
	{
		const OverlayState &o = ov[ OVERLAY_SELECT_ALL ];
		drawRoundBed( o.rect, m, TheAndroidOverlayPressed == OVERLAY_SELECT_ALL, FALSE );
		drawArmyGlyph( o.rect, Palette::Glyph );
	}
	for( Int g = 0; g < NUM_CONTROL_GROUPS; ++g )
	{
		const Int id = OVERLAY_GROUP_1 + g;
		const OverlayState &o = ov[ id ];
		if( !o.visible )
			continue;
		const Bool pressed = TheAndroidOverlayPressed == id;
		drawRoundBed( o.rect, m, pressed, o.ghost );
		if( o.ghost )
		{
			drawPlusGlyph( o.rect, pressed ? Palette::Glyph : Palette::GlyphDim );
			if( DisplayString *n = numberText( s_ghostNumber[ g ], FONT_SMALL, g + 1 ) )
				n->draw( o.rect.x + o.rect.w * 2 / 3, o.rect.y + o.rect.h / 10, Palette::GlyphDim, Palette::TextShadow );
		}
		else
		{
			if( DisplayString *n = numberText( s_groupNumber[ g ], FONT_MONEY, g + 1 ) )
				drawCentered( n, o.rect, Palette::Text );
			if( DisplayString *c = numberText( s_groupCount[ g ], FONT_SMALL, o.badge ) )
				drawBadge( c, o.rect, m );
		}
	}

	// --- minimap frame, only while the player has a radar ---
	if( shown( s_win.radar ) )
	{
		fillRound( m.minimapFrame, m.radius, Palette::PanelFill );
		lineRound( m.minimapFrame, m.radius, m.line, Palette::PanelLine );
	}

	// --- chat and beacon beds (multiplayer) ---
	if( m.multiplayer )
	{
		if( shown( s_win.chat ) )
			drawRoundBed( m.chat, m, FALSE, FALSE );
		if( shown( s_win.beacon ) )
			drawRoundBed( m.beacon, m, FALSE, FALSE );
	}

	// --- the drawer: one bed, the chip row (name, health, count), the tabs ---
	if( !m_phoneDrawerShown || m_phoneDrawerRect.w <= 0 )
		return;
	Rect bed = m_phoneDrawerRect;
	bed.x += m_phoneDrawerSlideDx;
	fillRound( bed, m.radius, Palette::PanelFill );
	lineRound( bed, m.radius, m.line, Palette::PanelLine );

	Rect chip = chipRect( m, m_phoneRows );
	chip.x += m_phoneDrawerSlideDx;
	const Rect portrait = { chip.x, chip.y, m.chipH * 4 / 3, m.chipH };
	Int infoRight = chip.right();
	for( Int t = 0; t < NUM_TABS; ++t )
		if( ov[ OVERLAY_TAB_0 + t ].visible && ov[ OVERLAY_TAB_0 + t ].rect.x + m_phoneDrawerSlideDx - m.gap < infoRight )
			infoRight = ov[ OVERLAY_TAB_0 + t ].rect.x + m_phoneDrawerSlideDx - m.gap;
	const Int infoX = (shown( m_rightHUDUnitSelectParent ) ? portrait.right() : chip.x) + m.gap;
	const Int infoW = infoRight - infoX;

	Object *obj = nullptr;
	if( m_currentSelectedDrawable )
		obj = m_currentSelectedDrawable->getObject();
	else if( TheInGameUI && TheInGameUI->getFirstSelectedDrawable() )
		obj = TheInGameUI->getFirstSelectedDrawable()->getObject();
	const Int selected = TheInGameUI ? TheInGameUI->getSelectCount() : 0;
	if( obj && infoW > m.gap )
	{
		const ThingTemplate *thing = obj->getTemplate();
		if( DisplayString *name = ensure( s_chipName, FONT_CHIP ) )
		{
			if( s_chipName.key != thing )
			{
				s_chipName.key = thing;
				name->setText( withoutHotkeyMarker( thing->getDisplayName() ) );
			}
			IRegion2D clip;
			clip.lo.x = infoX; clip.lo.y = chip.y; clip.hi.x = infoRight; clip.hi.y = chip.bottom();
			name->setClipRegion( &clip );
			Int w = 0, h = 0;
			name->getSize( &w, &h );
			const Int nameY = chip.y + m.gap / 2;
			name->draw( infoX, nameY, Palette::Text, Palette::TextShadow );
			// health bar under the name, for a single selection
			if( selected <= 1 )
			{
				const BodyModuleInterface *body = obj->getBodyModule();
				if( body && body->getMaxHealth() > 0.0f )
				{
					const Real ratio = body->getHealth() / body->getMaxHealth();
					const Int barH = mmToPx( 1.0f, m.pxPerMm );
					const Rect trough = { infoX, nameY + h + m.gap / 2, infoW, barH };
					fillRound( trough, barH / 2, Palette::Trough );
					Rect hp = trough;
					hp.w = (Int)(ratio * (Real)trough.w);
					fillRound( hp, barH / 2, ratio > 0.5f ? Palette::Good : (ratio > 0.25f ? Palette::Warn : Palette::Bad) );
				}
			}
		}
	}
	if( selected > 1 )
	{
		if( DisplayString *n = numberText( s_chipCount, FONT_SMALL, selected ) )
			drawBadge( n, portrait, m );
	}

	// tabs: pills right-aligned in the chip row; the active one in gold, a pressed one bright
	for( Int t = 0; t < NUM_TABS; ++t )
	{
		const OverlayState &o = ov[ OVERLAY_TAB_0 + t ];
		if( !o.visible )
			continue;
		Rect r = o.rect;
		r.x += m_phoneDrawerSlideDx;
		const Bool pressed = TheAndroidOverlayPressed == OVERLAY_TAB_0 + t;
		fillRound( r, r.h / 2, o.active ? Palette::ButtonFill : withAlpha( Palette::ButtonFill, 0x60 ) );
		lineRound( r, r.h / 2, m.line, pressed ? Palette::GoldBright : (o.active ? Palette::Gold : Palette::ButtonLine) );
		if( DisplayString *caption = ensure( s_tabCaption[ t ], FONT_SMALL ) )
		{
			const WideChar *label = phoneTabLabel( t );
			const Int pages = o.active ? m_phonePages : 1;
			const Int value = (o.badge << 8) | pages;
			if( s_tabCaption[ t ].key != label || s_tabCaption[ t ].value != value )
			{
				s_tabCaption[ t ].key = label;
				s_tabCaption[ t ].value = value;
				UnicodeString text( label );
				if( pages > 1 )
				{
					UnicodeString page;
					page.format( L" %d/%d", o.badge, pages );
					text.concat( page );
				}
				caption->setText( text );
			}
			drawCentered( caption, r, o.active || pressed ? Palette::Text : Palette::TextDim );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A command tile: a rounded bed, the cameo in the top 4:3 box (dimmed in colour while locked, the
	* engine's veterancy overlay and recharge clock over it), the price in a pill, a padlock while
	* locked, the name in up to two lines under the cameo, and the outline: gold while available, muted
	* while locked, bright while pressed or flashing. The window is the engine's own button, so its
	* input and the command processing are untouched. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneDrawTile( GameWindow *window, WinInstanceData *inst )
{
	using namespace AndroidHud;
	if( !TheDisplay || !window )
		return;
	const Metrics &m = m_phoneMetrics;
	const Rect tile = windowRect( window );
	if( tile.w <= 0 || tile.h <= 0 )
		return;
	const Rect cameo = tileCameoRect( tile, m );
	const UnsignedInt status = window->winGetStatus();
	const Bool enabled  = BitIsSet( status, WIN_STATUS_ENABLED );
	const Bool notReady = BitIsSet( status, WIN_STATUS_NOT_READY );
	const Bool locked   = !enabled && !notReady;
	const Bool pressed  = inst && BitIsSet( inst->getState(), WIN_STATE_SELECTED );
	const Bool flashing = BitIsSet( status, WIN_STATUS_FLASHING );

	fillRound( tile, m.radius, Palette::ButtonFill );

	// the cameo, in colour; a locked one dimmed, an unaffordable one only slightly
	const Image *image = GadgetButtonGetEnabledImage( window );
	if( image )
	{
		UnsignedInt colorMultiplier = 0xFFFFFFFF;
		if( locked )
			colorMultiplier = BitIsSet( status, WIN_STATUS_ALWAYS_COLOR ) ? 0xFFB4B4B4 : 0xFF5C5C5C;
		const Rect box = { cameo.x + m.line, cameo.y + m.line, cameo.w - 2 * m.line, cameo.h - 2 * m.line };
		TheDisplay->drawImage( image, box.x, box.y, box.right(), box.bottom(), colorMultiplier );
	}
	PushButtonData *pData = (PushButtonData *)window->winGetUserData();
	if( pData )
	{
		if( pData->overlayImage )
			TheDisplay->drawImage( pData->overlayImage, cameo.x, cameo.y, cameo.right(), cameo.bottom() );
		if( pData->drawClock )
		{
			if( pData->drawClock == NORMAL_CLOCK )
				TheDisplay->drawRectClock( cameo.x, cameo.y, cameo.w, cameo.h, pData->percentClock, pData->colorClock );
			else if( pData->drawClock == INVERSE_CLOCK )
				TheDisplay->drawRemainingRectClock( cameo.x, cameo.y, cameo.w, cameo.h, pData->percentClock, pData->colorClock );
			pData->drawClock = NO_CLOCK;
		}
	}
	if( pressed )
		fillRound( tile, m.radius, Palette::PressedWash );

	// name and price (cached per cell; re-set when the cell's command changes)
	const Int cell = getPhoneCommandCell( window );
	const CommandButton *command = getPhoneCommand( window );
	if( cell >= 0 && command )
	{
		if( DisplayString *name = ensure( s_tileName[ cell ], FONT_TILE ) )
		{
			if( s_tileName[ cell ].key != command )
			{
				s_tileName[ cell ].key = command;
				name->setWordWrap( tile.w - 2 * m.gap );
				name->setWordWrapCentered( TRUE );
				name->setText( command->getTextLabel().isEmpty() || !TheGameText
					? UnicodeString::TheEmptyString : withoutHotkeyMarker( TheGameText->fetch( command->getTextLabel() ) ) );
			}
			IRegion2D clip;
			clip.lo.x = tile.x; clip.lo.y = cameo.bottom(); clip.hi.x = tile.right(); clip.hi.y = tile.bottom();
			name->setClipRegion( &clip );
			Int w = 0, h = 0;
			name->getSize( &w, &h );
			Int y = cameo.bottom() + (m.tileNameH - h) / 2;
			if( y < cameo.bottom() )
				y = cameo.bottom();
			name->draw( tile.x + (tile.w - w) / 2, y, enabled ? Palette::Text : Palette::TextDim, Palette::TextShadow );
		}
		const Int cost = getPhoneCommandCost( command );
		if( cost > 0 )
		{
			if( DisplayString *price = numberText( s_tilePrice[ cell ], FONT_PRICE, cost ) )
			{
				Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
				const Bool affordable = player && player->getMoney()->countMoney() >= (UnsignedInt)cost;
				drawPricePill( price, cameo.right() - m.line, cameo.bottom() - m.line, m, affordable ? Palette::CostText : Palette::CostTextPoor );
			}
		}
	}
	if( locked )
	{
		const Int s = cameo.h / 3;
		const Rect box = { cameo.right() - m.line * 2 - s, cameo.y + m.line * 2, s, s };
		drawLockGlyph( box, m.line );
	}

	// outline
	UnsignedInt lineColor = enabled ? Palette::Gold : Palette::ButtonLine;
	if( pressed )
		lineColor = Palette::GoldBright;
	else if( flashing )
		lineColor = lerpColor( Palette::Gold, Palette::GoldBright, pulse() );
	lineRound( tile, m.radius, pressed || flashing ? m.line * 2 : m.line, lineColor );
}

//-------------------------------------------------------------------------------------------------
/** A generals' power shortcut as a round button: bed, the cameo inset to 70 %, and the ring: a
	* cooldown arc while recharging (the engine's clock percentage), a bright full ring while ready, a
	* muted ring while the power is not available. The engine's count of ready powers (its button text)
	* becomes a badge. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneDrawPowerButton( GameWindow *window, WinInstanceData *inst )
{
	using namespace AndroidHud;
	if( !TheDisplay || !window )
		return;
	const Metrics &m = m_phoneMetrics;
	const Rect r = windowRect( window );
	if( r.w <= 0 || r.h <= 0 )
		return;
	const UnsignedInt status = window->winGetStatus();
	const Bool enabled  = BitIsSet( status, WIN_STATUS_ENABLED );
	const Bool notReady = BitIsSet( status, WIN_STATUS_NOT_READY );
	const Bool pressed  = inst && BitIsSet( inst->getState(), WIN_STATE_SELECTED );

	fillCircle( r, pressed ? Palette::ButtonFillHot : Palette::ButtonFill );
	const Image *image = GadgetButtonGetEnabledImage( window );
	drawInsetImage( image, r, (!enabled && !notReady) ? 0xFF5C5C5C : 0xFFFFFFFF );

	PushButtonData *pData = (PushButtonData *)window->winGetUserData();
	Bool drewClock = FALSE;
	if( pData && pData->drawClock )
	{
		// recharging: a dark track and the gold arc for the part that is done
		Real done = (Real)pData->percentClock / 100.0f;
		if( pData->drawClock == NORMAL_CLOCK )
			done = 1.0f - done;
		if( done < 0.0f ) done = 0.0f;
		if( done > 1.0f ) done = 1.0f;
		ring( r, m.line, m.line * 2, 0.0f, 1.0f, Palette::Trough );
		if( done > 0.0f )
			ring( r, m.line, m.line * 2, 0.0f, done, Palette::Gold );
		pData->drawClock = NO_CLOCK;
		drewClock = TRUE;
	}
	if( !drewClock )
	{
		if( enabled )
			ring( r, m.line, m.line * 2, 0.0f, 1.0f, pressed ? Palette::GoldBright : lerpColor( Palette::Gold, Palette::GoldBright, pulse() ) );
		else
			ring( r, m.line, m.line, 0.0f, 1.0f, Palette::ButtonLine );
	}
	if( inst && inst->getTextLength() > 0 )
	{
		DisplayString *text = inst->getTextDisplayString();
		if( text )
		{
			if( font( FONT_SMALL ) && text->getFont() != font( FONT_SMALL ) )
				text->setFont( font( FONT_SMALL ) );
			drawBadge( text, r, m );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The rank badge (the engine's ButtonGeneral, so a tap opens the promotion screen as on the PC):
	* a round bed with a star; while promotion points are unspent the star pulses gold and the count
	* sits in a badge. */
//-------------------------------------------------------------------------------------------------
void ControlBar::phoneDrawRankBadge( GameWindow *window, WinInstanceData *inst )
{
	using namespace AndroidHud;
	if( !TheDisplay || !window )
		return;
	const Metrics &m = m_phoneMetrics;
	const Rect r = windowRect( window );
	if( r.w <= 0 || r.h <= 0 )
		return;
	const Bool pressed = inst && BitIsSet( inst->getState(), WIN_STATE_SELECTED );
	Player *player = rts::getObservedOrLocalPlayer();
	const Int points = player ? player->getSciencePurchasePoints() : 0;

	fillCircle( r, pressed ? Palette::ButtonFillHot : Palette::ButtonFill );
	ring( r, m.line, m.line, 0.0f, 1.0f, pressed ? Palette::GoldBright : (points > 0 ? Palette::Gold : Palette::ButtonLine) );
	drawStarGlyph( r, points > 0 ? lerpColor( Palette::Gold, Palette::GoldBright, pulse() ) : Palette::Glyph );
	if( points > 0 )
	{
		if( DisplayString *n = numberText( s_rankPoints, FONT_SMALL, points ) )
			drawBadge( n, r, m );
	}
}

//-------------------------------------------------------------------------------------------------
/** The full layout pass. Runs from update() whenever the bar was (re)built, a faction scheme was
	* applied, the stage changed, or the number of generals' powers on show changed: those are the
	* moments the engine itself repositions windows, or the HUD needs a different amount of room. */
//-------------------------------------------------------------------------------------------------
void ControlBar::applyPhoneLayout()
{
	using namespace AndroidHud;
	m_phoneLayoutDirty = FALSE;
	GameWindow *master = m_contextParent[ CP_MASTER ];
	if( !master || !TheDisplay || !TheWindowManager )
		return;

	const Int W = TheDisplay->getWidth(), H = TheDisplay->getHeight();
	m_phoneMetrics = computeMetrics( W, H, pixelsPerMmFor( W, H ), isMultiplayerGame(), phoneShownPowerCount() );
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

	LeafSet leaves, containers, skip;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i ) leaves.add( m_commandWindows[ i ] );
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i ) leaves.add( w.queue[ i ] );
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i ) leaves.add( m_rightHUDUpgradeCameos[ i ] );
	leaves.add( m_rightHUDCameoWindow );
	leaves.add( w.radar ); leaves.add( w.glow ); leaves.add( w.star ); leaves.add( w.chat ); leaves.add( w.beacon );
	leaves.add( w.ucDesc ); leaves.add( w.ucCancel );
	leaves.add( w.oclText ); leaves.add( w.oclBar ); leaves.add( w.oclSell );
	leaves.add( w.beaconEdit ); leaves.add( w.beaconLabel ); leaves.add( w.beaconClear ); leaves.add( w.beaconDel );

	containers.add( m_contextParent[ CP_COMMAND ] );
	containers.add( m_contextParent[ CP_BUILD_QUEUE ] );
	containers.add( m_contextParent[ CP_UNDER_CONSTRUCTION ] );
	containers.add( m_contextParent[ CP_OCL_TIMER ] );
	containers.add( m_contextParent[ CP_BEACON ] );
	containers.add( m_rightHUDUnitSelectParent );
	containers.add( m_rightHUDWindow );

	skip.add( m_contextParent[ CP_OBSERVER_LIST ] );
	skip.add( m_contextParent[ CP_OBSERVER_INFO ] );

	// 1. the root spans the display, paints every HUD surface, and is the one intended hole to the
	//    battlefield: a tap that lands on nothing else reaches the world (NO_INPUT), while a tap a
	//    child ignored is blocked on its way up (the input function sees bubbled messages only)
	master->winSetPosition( 0, 0 );
	master->winSetSize( W, H );
	master->winSetStatus( WIN_STATUS_SEE_THRU | WIN_STATUS_NO_INPUT );
	master->winSetDrawFunc( phoneRootDraw );
	master->winSetInputFunc( GameWinBlockInput );
	restyleTree( master, leaves, containers, skip );

	// 2. controls the phone replaces: the ESC button (the ☰ overlay), the idle-worker button (the
	//    IDLE overlay, fed by InGameUI's idle list), the bar's up/down and options buttons, the
	//    experience bar (the rank badge), the money and power windows (the resource pill).
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonLarge" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonIdleWorker" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:ButtonOptions" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:GeneralsExp" ) );
	hideAndPark( findBarWindow( "ControlBar.wnd:ExpBarForeground" ) );
	hideAndPark( w.money );
	hideAndPark( w.power );

	// 3. top-right: the rank badge (the engine's button with our draw), the minimap and its glow;
	//    chat and beacon bottom-left in multiplayer only (the same test the engine applies)
	if( w.star )
	{
		place( this, w.star, m.rank );
		w.star->winSetDrawFunc( phoneRankDraw );
	}
	place( this, w.radar, m.minimap );
	place( this, w.glow, m.minimapFrame );
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

	// 4. the generals' powers shortcut bar: its own layout; the buttons get the round draw and are
	//    placed by the drawer pass (which packs the ones on show)
	if( m_specialPowerShortcutParent )
	{
		LeafSet powerLeaves, powerHolders, none;
		for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
		{
			powerLeaves.add( m_specialPowerShortcutButtons[ i ] );
			powerHolders.add( m_specialPowerShortcutButtonParents[ i ] );
			if( m_specialPowerShortcutButtons[ i ] )
				m_specialPowerShortcutButtons[ i ]->winSetDrawFunc( phonePowerDraw );
		}
		m_specialPowerShortcutParent->winClearStatus( WIN_STATUS_NO_INPUT );
		m_specialPowerShortcutParent->winSetStatus( WIN_STATUS_SEE_THRU );
		m_specialPowerShortcutParent->winSetInputFunc( GameWinBlockInput );
		restyleTree( m_specialPowerShortcutParent, powerLeaves, powerHolders, none );
		place( this, m_specialPowerShortcutParent, m.powersArea );
	}

	// 5. the drawer's command tiles and the small panels' buttons draw as tiles; text sizes
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		if( m_commandWindows[ i ] )
			m_commandWindows[ i ]->winSetDrawFunc( phoneTileDraw );
	if( w.ucCancel ) w.ucCancel->winSetDrawFunc( phoneTileDraw );
	if( w.oclSell )  w.oclSell->winSetDrawFunc( phoneTileDraw );
	const Int textH = mmToPx( 2.2f, m.pxPerMm );
	scaleTextFont( w.ucDesc, textH );
	scaleTextFont( w.oclText, textH );
	scaleTextFont( w.beaconLabel, textH );

	// 6. the drawer follows what the engine shows; force its first placement
	m_phoneDrawerSignature = 0xFFFFFFFFu;
	m_phonePowerSignature = 0xFFFFFFFFu;
	m_phoneDrawerShown = FALSE;
	m_phoneDrawerSlideDx = 0;
	updatePhoneDrawer();
}

//-------------------------------------------------------------------------------------------------
/** Every frame: read what the engine currently shows (which commands, portrait, cameos, queue
	* items, the small panels, the radar, the powers, the tab and page) into two signatures, and
	* re-lay-out only when they changed or while the drawer is sliding in. Cheap: a few dozen flag
	* reads per frame. A change in the number of powers on show re-runs the full layout, since the
	* powers block shapes the room the drawer gets. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updatePhoneDrawer()
{
	using namespace AndroidHud;
	GameWindow *master = m_contextParent[ CP_MASTER ];
	if( !master || !TheDisplay || !s_win.valid )
		return;
	const Metrics &m = m_phoneMetrics;

	const Int powers = phoneShownPowerCount();
	if( powers != m.powerCount )
	{
		markPhoneLayoutDirty();   // next frame: a layout with room for that many power rows
		return;
	}

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

	// a different set of commands is a new menu: back to its first tab and page
	if( (sig & 0x3FFFFu) != (m_phoneDrawerSignature & 0x3FFFFu) )
	{
		m_phoneTab = 0;
		m_phonePage = 0;
	}

	UnsignedInt powerSig = 0;
	for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
	{
		GameWindow *button = m_specialPowerShortcutButtons[ i ];
		GameWindow *holder = m_specialPowerShortcutButtonParents[ i ];
		if( shown( button ) && (!holder || shown( holder )) )
			powerSig |= 1u << i;
	}
	powerSig |= (UnsignedInt)(m_phoneTab & 3) << 12;
	powerSig |= (UnsignedInt)(m_phonePage & 15) << 14;

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

	if( sig == m_phoneDrawerSignature && powerSig == m_phonePowerSignature && dx == m_phoneDrawerSlideDx )
		return;
	m_phoneDrawerSignature = sig;
	m_phonePowerSignature = powerSig;
	m_phoneDrawerSlideDx = dx;

	// the minimap exists only with a radar; the glow only while the base is under attack
	if( s_win.radar )
		s_win.radar->winHide( !radar );
	if( s_win.glow )
		s_win.glow->winHide( !(radar && m_radarAttackGlowOn) );

	layoutPhoneDrawer( dx );
}

//-------------------------------------------------------------------------------------------------
/** Place the drawer for what is visible now. The visible commands are classified into tabs when
	* there are more than a page of them; the active tab's current page is laid into the tile rows
	* (reading order, hugging the bottom), the rest is parked off-screen but never hidden, so the
	* engine keeps populating and enabling those buttons. The chip row sits above the tiles with the
	* portrait and the cameos at its left; the queue row above that while producing. The small context
	* panels (under construction, OCL timer, beacon) take the tile rows instead. The generals' powers
	* on show are packed into their rows under the minimap. `slideDx` shifts the drawer right while it
	* slides in. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutPhoneDrawer( Int slideDx )
{
	using namespace AndroidHud;
	const Metrics &m = m_phoneMetrics;

	// 1. the visible commands, their menu kind and their tabs
	GameWindow *shownWin[ MAX_COMMANDS_PER_SET ];
	Int tabOf[ MAX_COMMANDS_PER_SET ];
	Int n = 0;
	Bool allConstruct = TRUE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		GameWindow *w = m_commandWindows[ i ];
		if( !shown( w ) )
			continue;
		const CommandButton *command = getPhoneCommand( w );
		if( !command || command->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT )
			allConstruct = FALSE;
		shownWin[ n++ ] = w;
	}
	m_phoneBuilderMenu = n > 0 && allConstruct;
	m_phoneMenuHasTabs = n > GRID_CELLS;
	for( Int t = 0; t < NUM_TABS; ++t )
		m_phoneTabCount[ t ] = 0;
	if( m_phoneMenuHasTabs )
	{
		for( Int k = 0; k < n; ++k )
		{
			tabOf[ k ] = phoneCommandTab( getPhoneCommand( shownWin[ k ] ), m_phoneBuilderMenu );
			++m_phoneTabCount[ tabOf[ k ] ];
		}
		if( m_phoneTab < 0 || m_phoneTab >= NUM_TABS || m_phoneTabCount[ m_phoneTab ] == 0 )
		{
			m_phoneTab = 0;
			while( m_phoneTab < NUM_TABS - 1 && m_phoneTabCount[ m_phoneTab ] == 0 )
				++m_phoneTab;
			m_phonePage = 0;
		}
	}
	const Int inTab = m_phoneMenuHasTabs ? m_phoneTabCount[ m_phoneTab ] : n;
	m_phonePages = (inTab + GRID_CELLS - 1) / GRID_CELLS;
	if( m_phonePages < 1 )
		m_phonePages = 1;
	if( m_phonePage < 0 || m_phonePage >= m_phonePages )
		m_phonePage = 0;

	// 2. the active page into the cells; everything else parked
	Int cells = 0, local = 0;
	for( Int c = 0; c < GRID_CELLS; ++c )
		m_phoneCellWindow[ c ] = nullptr;
	for( Int k = 0; k < n; ++k )
	{
		if( m_phoneMenuHasTabs && tabOf[ k ] != m_phoneTab )
		{
			park( shownWin[ k ] );
			continue;
		}
		const Int cell = local++ - m_phonePage * GRID_CELLS;
		if( cell < 0 || cell >= GRID_CELLS )
		{
			park( shownWin[ k ] );
			continue;
		}
		m_phoneCellWindow[ cell ] = shownWin[ k ];
		++cells;
	}
	Int rows = (cells + GRID_COLS - 1) / GRID_COLS;

	// the small context panels take the tile rows when there are no command tiles
	GameWindow *ucPanel = m_contextParent[ CP_UNDER_CONSTRUCTION ];
	GameWindow *oclPanel = m_contextParent[ CP_OCL_TIMER ];
	GameWindow *beaconPanel = m_contextParent[ CP_BEACON ];
	const Bool ucShown = cells == 0 && shown( ucPanel );
	const Bool oclShown = cells == 0 && !ucShown && shown( oclPanel );
	const Bool beaconShown = cells == 0 && !ucShown && !oclShown && shown( beaconPanel );
	if( ucShown || oclShown )
		rows = 1;
	else if( beaconShown )
		rows = 2;
	m_phoneRows = rows;

	Rect footprint = { 0, 0, 0, 0 };
	const Int gridTop = gridTopForRows( m, rows );
	const Rect rowsRect = { m.grid.x, gridTop, m.grid.w, m.grid.bottom() - gridTop };

	GameWindow *commandPanel = m_contextParent[ CP_COMMAND ];
	if( cells > 0 )
	{
		place( this, commandPanel, rowsRect, slideDx );
		for( Int c = 0; c < GRID_CELLS; ++c )
			if( m_phoneCellWindow[ c ] )
				place( this, m_phoneCellWindow[ c ], commandCellRect( m, c, rows ), slideDx );
		unionRect( footprint, rowsRect );
	}
	else
	{
		park( commandPanel );
	}

	if( ucShown )
	{
		const Rect tile = commandCellRect( m, 0, 1 );
		const Rect text = { tile.right() + m.gap, tile.y, m.grid.right() - tile.right() - m.gap, tile.h };
		place( this, ucPanel, rowsRect, slideDx );
		place( this, s_win.ucCancel, tile, slideDx );
		place( this, s_win.ucDesc, text, slideDx );
		unionRect( footprint, rowsRect );
	}
	else
	{
		park( ucPanel );
	}
	if( oclShown )
	{
		const Rect tile = commandCellRect( m, 0, 1 );
		const Int textH = mmToPx( 2.2f, m.pxPerMm ) + m.gap;
		const Int textW = m.grid.right() - tile.right() - m.gap;
		const Rect text = { tile.right() + m.gap, tile.y, textW, textH };
		const Rect bar  = { tile.right() + m.gap, tile.y + textH + m.gap, textW, mmToPx( 2.0f, m.pxPerMm ) };
		place( this, oclPanel, rowsRect, slideDx );
		place( this, s_win.oclSell, tile, slideDx );
		place( this, s_win.oclText, text, slideDx );
		place( this, s_win.oclBar, bar, slideDx );
		unionRect( footprint, rowsRect );
	}
	else
	{
		park( oclPanel );
	}
	if( beaconShown )
	{
		const Rect upper = commandCellRect( m, 0, 2 );
		const Rect lower = commandCellRect( m, GRID_COLS, 2 );
		const Int editH = mmToPx( 2.2f, m.pxPerMm ) + 2 * m.gap;
		const Rect edit = { upper.right() + m.gap, upper.y + (upper.h - editH) / 2, m.grid.right() - upper.right() - m.gap, editH };
		const Rect del  = commandCellRect( m, GRID_COLS + 1, 2 );
		place( this, beaconPanel, rowsRect, slideDx );
		place( this, s_win.beaconLabel, upper, slideDx );
		place( this, s_win.beaconEdit, edit, slideDx );
		place( this, s_win.beaconClear, lower, slideDx );
		place( this, s_win.beaconDel, del, slideDx );
		unionRect( footprint, rowsRect );
	}
	else
	{
		park( beaconPanel );
	}

	// 3. the chip row: the portrait container at its left (up to the tabs), cameos under the name
	const Rect chip = chipRect( m, rows );
	unionRect( footprint, chip );
	Int visibleTabs = 0;
	if( m_phoneMenuHasTabs )
		for( Int t = 0; t < NUM_TABS; ++t )
			if( m_phoneTabCount[ t ] > 0 )
				++visibleTabs;
	const Int infoRight = visibleTabs > 0 ? tabRect( m, rows, NUM_TABS - visibleTabs ).x - m.gap : chip.right();
	if( shown( m_rightHUDUnitSelectParent ) )
	{
		const Rect left = { chip.x, chip.y, infoRight - chip.x, chip.h };
		const Rect portrait = portraitRect( m, rows );
		place( this, m_rightHUDWindow, left, slideDx );             // container (see-through, blocks input)
		place( this, m_rightHUDUnitSelectParent, left, slideDx );   // container
		place( this, m_rightHUDCameoWindow, portrait, slideDx );
		Int k = 0;
		for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
		{
			GameWindow *cameo = m_rightHUDUpgradeCameos[ i ];
			if( !shown( cameo ) )
				continue;
			const Rect c = { portrait.right() + m.gap + k * (m.cameoTile + m.gap), chip.bottom() - m.cameoTile, m.cameoTile, m.cameoTile };
			place( this, cameo, c, slideDx );
			++k;
		}
	}
	else
	{
		park( m_rightHUDWindow );
	}

	// 4. the production queue row above the chip while producing
	GameWindow *queuePanel = m_contextParent[ CP_BUILD_QUEUE ];
	Bool queueHasItems = FALSE;
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS && !queueHasItems; ++i )
		if( s_win.queue[ i ] && s_win.queue[ i ]->winGetEnabled() )
			queueHasItems = TRUE;
	if( shown( queuePanel ) && queueHasItems )
	{
		const Rect row = queueRowRect( m, rows );
		place( this, queuePanel, row, slideDx );
		for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
			place( this, s_win.queue[ i ], queueCellRect( m, rows, i ), slideDx );
		unionRect( footprint, row );
	}
	else
	{
		park( queuePanel );
	}

	// 5. the generals' powers on show, packed into their rows under the minimap
	if( m_specialPowerShortcutParent )
	{
		Int k = 0;
		for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
		{
			GameWindow *button = m_specialPowerShortcutButtons[ i ];
			GameWindow *holder = m_specialPowerShortcutButtonParents[ i ];
			if( shown( button ) && (!holder || shown( holder )) && k < m.powerCount )
			{
				const Rect r = powerButtonRect( m, k++ );
				place( this, holder, r );
				place( this, button, r );
			}
			else
			{
				park( holder );
				park( button );
			}
		}
	}

	m_phoneDrawerRect = footprint.w > 0 ? inflate( footprint, m.pad ) : footprint;
}

#endif // __ANDROID__
