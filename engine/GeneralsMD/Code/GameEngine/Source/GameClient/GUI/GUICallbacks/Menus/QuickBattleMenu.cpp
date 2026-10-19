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

// FILE: QuickBattleMenu.cpp //////////////////////////////////////////////////////////////////////
// GeneralsX @feature Android quick battle 19/10/2026 (docs/MOBILE-UX-SPEC.md §7, Phase B-2)
//
// The Skirmish screen is a dense 4:3 form of dropdowns laid out for a mouse. On a phone the main
// menu gets a QUICK BATTLE button that opens one screen of big cards, each with ◀ ▶ arrows:
// general, map, opponents, difficulty, starting cash, and START BATTLE. Everything is built in
// code (no .wnd data is shipped) and drawn from primitives. Start fills TheSkirmishGameInfo
// exactly as the Skirmish screen does (SkirmishGameOptionsMenu.cpp: SkirmishGameOptionsMenuInit
// and reallyDoStart), saves the choice to the skirmish preferences and sends MSG_NEW_GAME, so the
// game that starts is an ordinary skirmish and the full Skirmish screen still shows the same setup.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(__ANDROID__)

#include <vector>

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/SkirmishPreferences.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MapUtil.h"
#include "GameClient/QuickBattleMenu.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

namespace
{

/// Screen rectangle in display pixels.
struct Rect
{
	Int x, y, w, h;
	Int right() const { return x + w; }
	Int bottom() const { return y + h; }
};

/// The screen's palette (0xAARRGGBB): dark translucent cards, a thin gold line, warm text.
namespace Palette
{
	const UnsignedInt PanelFill     = 0xC80A0D12;
	const UnsignedInt PanelLine     = 0xD2C9A961;
	const UnsignedInt ButtonFill    = 0xD4141922;
	const UnsignedInt ButtonFillHot = 0xF05A4B28;
	const UnsignedInt ButtonLine    = 0xB0907C4A;
	const UnsignedInt GoldBright    = 0xFFF0D890;
	const UnsignedInt Text          = 0xFFF2E6C4;
	const UnsignedInt TextDim       = 0xA0B0A58A;
	const UnsignedInt TextShadow    = 0xC8000000;
	const UnsignedInt Glyph         = 0xF5E9D08F;
}

/// Pixels per millimetre: a phone's short side is about 68 mm. Every size below is in mm so the
/// cards are thumb-sized on any display.
inline Real pixelsPerMmFor( Int displayW, Int displayH )
{
	const Int shortSide = displayW < displayH ? displayW : displayH;
	return shortSide > 0 ? (Real)shortSide / 68.0f : 15.0f;
}

enum Row { ROW_GENERAL = 0, ROW_MAP, ROW_OPPONENTS, ROW_DIFFICULTY, ROW_CASH, ROW_COUNT };
enum Kind { KIND_PREV, KIND_NEXT, KIND_BACK, KIND_START, KIND_ENTRY };

/// One selectable value of a card.
struct Choice
{
	Int value;            ///< player template index, cash amount, or unused
	AsciiString key;      ///< map file name
	UnicodeString name;
};

/// A button we created and what it does.
struct ButtonRef
{
	GameWindow *window;
	Kind kind;
	Int row;
};

struct Screen
{
	GameWindow *parent;                 ///< the main menu's root window
	GameWindow *entry;                  ///< the QUICK BATTLE button on the main menu
	GameWindow *root;                   ///< the full-screen panel, hidden until opened
	ButtonRef buttons[ 3 + 2 * ROW_COUNT ];
	Int buttonCount;

	Real pxPerMm;
	Int displayW, displayH, line, pad, gap, arrow;
	Rect title, card[ ROW_COUNT ], startCard, backRect;

	Int selected[ ROW_COUNT ];
	std::vector<Choice> generals, maps, cash;
	Int mapPlayers;                     ///< players the chosen map allows (humans + AI)

	GameFont *fontTitle, *fontLabel, *fontValue, *fontButton;
	DisplayString *titleText, *labelText[ ROW_COUNT ], *valueText[ ROW_COUNT ], *backText, *startText, *entryText;
};

Screen s_screen = { nullptr };

const WideChar *const kLabel[ ROW_COUNT ] = { L"General", L"Map", L"Opponents", L"Difficulty", L"Starting cash" };
const WideChar *const kDifficulty[ 3 ] = { L"Easy", L"Medium", L"Hard" };

inline Int mmToPx( Real mm ) { return (Int)(mm * s_screen.pxPerMm + 0.5f); }

// --- drawing primitives (display coordinates, the windows are placed in them) ---
inline void fill( const Rect &r, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheDisplay->drawFillRect( r.x, r.y, r.w, r.h, color );
}
inline void frame( const Rect &r, UnsignedInt color )
{
	if( r.w > 0 && r.h > 0 )
		TheDisplay->drawOpenRect( r.x, r.y, r.w, r.h, (Real)s_screen.line, color );
}
inline Rect windowRect( GameWindow *w )
{
	ICoord2D pos, size;
	w->winGetScreenPosition( &pos.x, &pos.y );
	w->winGetSize( &size.x, &size.y );
	Rect r = { pos.x, pos.y, size.x, size.y };
	return r;
}
void drawCentered( DisplayString *s, const Rect &r, UnsignedInt color )
{
	if( !s )
		return;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, color, Palette::TextShadow );
}
inline Bool pressed( GameWindow *w )
{
	return BitIsSet( w->winGetInstanceData()->getState(), WIN_STATE_SELECTED );
}

DisplayString *makeText( GameFont *font, const WideChar *text )
{
	if( !TheDisplayStringManager )
		return nullptr;
	DisplayString *s = TheDisplayStringManager->newDisplayString();
	if( !s )
		return nullptr;
	if( font )
		s->setFont( font );
	if( text )
		s->setText( UnicodeString( text ) );
	return s;
}
void freeText( DisplayString *&s )
{
	if( s && TheDisplayStringManager )
		TheDisplayStringManager->freeDisplayString( s );
	s = nullptr;
}

const ButtonRef *refFor( GameWindow *w )
{
	for( Int i = 0; i < s_screen.buttonCount; ++i )
		if( s_screen.buttons[ i ].window == w )
			return &s_screen.buttons[ i ];
	return nullptr;
}

// --- the choices ---
void buildGenerals()
{
	Screen &sc = s_screen;
	sc.generals.clear();
	Choice random = { PLAYERTEMPLATE_RANDOM, AsciiString::TheEmptyString, TheGameText->fetch( "GUI:Random" ) };
	sc.generals.push_back( random );
	if( !ThePlayerTemplateStore )
		return;
	// the same filter the Skirmish screen's army dropdown applies (GUIUtil.cpp)
	std::vector<AsciiString> seenSides;
	const Int count = ThePlayerTemplateStore->getPlayerTemplateCount();
	for( Int c = 0; c < count; ++c )
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate( c );
		if( !fac || fac->getStartingBuilding().isEmpty() )
			continue;
		const GeneralPersona *general = TheChallengeGenerals ? TheChallengeGenerals->getGeneralByTemplateName( fac->getName() ) : nullptr;
		if( general && !general->isStartingEnabled() )
			continue;
		Bool seen = FALSE;
		for( size_t i = 0; i < seenSides.size() && !seen; ++i )
			if( seenSides[ i ] == fac->getSide() )
				seen = TRUE;
		if( seen )
			continue;
		seenSides.push_back( fac->getSide() );
		AsciiString side;
		side.format( "SIDE:%s", fac->getSide().str() );
		Choice choice = { c, AsciiString::TheEmptyString, TheGameText->fetch( side ) };
		sc.generals.push_back( choice );
	}
}

void buildMaps()
{
	Screen &sc = s_screen;
	sc.maps.clear();
	if( !TheMapCache )
		return;
	TheMapCache->updateCache();
	for( MapCache::iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it )
	{
		const MapMetaData &md = it->second;
		if( !md.m_isMultiplayer || md.m_numPlayers < 2 || !md.m_doesExist )
			continue;
		Choice choice = { md.m_numPlayers, it->first, md.m_displayName };
		if( choice.name.isEmpty() )
			choice.name.translate( it->first );
		sc.maps.push_back( choice );
	}
}

void buildCash()
{
	Screen &sc = s_screen;
	sc.cash.clear();
	if( !TheMultiplayerSettings )
		return;
	const MultiplayerStartingMoneyList &list = TheMultiplayerSettings->getStartingMoneyList();
	for( size_t i = 0; i < list.size(); ++i )
	{
		Choice choice = { (Int)list[ i ].countMoney(), AsciiString::TheEmptyString, UnicodeString::TheEmptyString };
		choice.name.format( L"$%d", choice.value );
		sc.cash.push_back( choice );
	}
}

inline Int clampIndex( Int i, Int count ) { return count <= 0 ? 0 : (i < 0 ? 0 : (i >= count ? count - 1 : i)); }

/// Players the chosen map allows; the opponent count stays below it.
void clampOpponents()
{
	Screen &sc = s_screen;
	sc.mapPlayers = sc.maps.empty() ? 2 : sc.maps[ clampIndex( sc.selected[ ROW_MAP ], (Int)sc.maps.size() ) ].value;
	Int maxOpponents = sc.mapPlayers - 1;
	if( maxOpponents > MAX_SLOTS - 1 ) maxOpponents = MAX_SLOTS - 1;
	if( maxOpponents < 1 ) maxOpponents = 1;
	if( sc.selected[ ROW_OPPONENTS ] < 1 ) sc.selected[ ROW_OPPONENTS ] = 1;
	if( sc.selected[ ROW_OPPONENTS ] > maxOpponents ) sc.selected[ ROW_OPPONENTS ] = maxOpponents;
}

/// Re-set the value texts from the selection (only on change: nothing allocates per frame).
void refreshValues()
{
	Screen &sc = s_screen;
	clampOpponents();
	UnicodeString text;
	if( sc.valueText[ ROW_GENERAL ] )
		sc.valueText[ ROW_GENERAL ]->setText( sc.generals.empty() ? UnicodeString( L"-" ) : sc.generals[ clampIndex( sc.selected[ ROW_GENERAL ], (Int)sc.generals.size() ) ].name );
	if( sc.valueText[ ROW_MAP ] )
	{
		if( sc.maps.empty() )
			text = L"No maps found";
		else
		{
			const Choice &map = sc.maps[ clampIndex( sc.selected[ ROW_MAP ], (Int)sc.maps.size() ) ];
			text.format( L"%ls  (%d players)", map.name.str(), map.value );
		}
		sc.valueText[ ROW_MAP ]->setText( text );
	}
	if( sc.valueText[ ROW_OPPONENTS ] )
	{
		text.format( sc.selected[ ROW_OPPONENTS ] == 1 ? L"%d opponent" : L"%d opponents", sc.selected[ ROW_OPPONENTS ] );
		sc.valueText[ ROW_OPPONENTS ]->setText( text );
	}
	if( sc.valueText[ ROW_DIFFICULTY ] )
		sc.valueText[ ROW_DIFFICULTY ]->setText( UnicodeString( kDifficulty[ clampIndex( sc.selected[ ROW_DIFFICULTY ], 3 ) ] ) );
	if( sc.valueText[ ROW_CASH ] )
		sc.valueText[ ROW_CASH ]->setText( sc.cash.empty() ? UnicodeString( L"default" ) : sc.cash[ clampIndex( sc.selected[ ROW_CASH ], (Int)sc.cash.size() ) ].name );
}

Int choiceCount( Int row )
{
	const Screen &sc = s_screen;
	switch( row )
	{
		case ROW_GENERAL:    return (Int)sc.generals.size();
		case ROW_MAP:        return (Int)sc.maps.size();
		case ROW_OPPONENTS:  return sc.mapPlayers - 1 < 1 ? 1 : (sc.mapPlayers - 1 > MAX_SLOTS - 1 ? MAX_SLOTS - 1 : sc.mapPlayers - 1);
		case ROW_DIFFICULTY: return 3;
		case ROW_CASH:       return (Int)sc.cash.size();
		default:             return 0;
	}
}

void step( Int row, Int delta )
{
	Screen &sc = s_screen;
	const Int count = choiceCount( row );
	if( count <= 0 )
		return;
	if( row == ROW_OPPONENTS )
	{
		// 1..count, wrapping
		Int v = sc.selected[ row ] + delta;
		if( v < 1 ) v = count;
		if( v > count ) v = 1;
		sc.selected[ row ] = v;
	}
	else
	{
		Int v = sc.selected[ row ] + delta;
		if( v < 0 ) v = count - 1;
		if( v >= count ) v = 0;
		sc.selected[ row ] = v;
	}
	refreshValues();
}

/// Load the last skirmish setup as the starting point: the Skirmish screen and this one share it.
void loadDefaults()
{
	Screen &sc = s_screen;
	SkirmishPreferences prefs;
	sc.selected[ ROW_GENERAL ] = 0;
	const Int faction = prefs.getPreferredFaction();
	for( size_t i = 0; i < sc.generals.size(); ++i )
		if( sc.generals[ i ].value == faction )
			sc.selected[ ROW_GENERAL ] = (Int)i;
	sc.selected[ ROW_MAP ] = 0;
	AsciiString map = prefs.getPreferredMap();
	map.toLower();
	for( size_t i = 0; i < sc.maps.size(); ++i )
		if( sc.maps[ i ].key == map )
			sc.selected[ ROW_MAP ] = (Int)i;
	const Int money = (Int)prefs.getStartingCash().countMoney();
	sc.selected[ ROW_CASH ] = 0;
	for( size_t i = 0; i < sc.cash.size(); ++i )
		if( sc.cash[ i ].value == money )
			sc.selected[ ROW_CASH ] = (Int)i;
	sc.selected[ ROW_OPPONENTS ] = prefs.getInt( "QuickBattleOpponents", 1 );
	sc.selected[ ROW_DIFFICULTY ] = prefs.getInt( "QuickBattleDifficulty", 1 );
	if( sc.selected[ ROW_OPPONENTS ] < 1 )
		sc.selected[ ROW_OPPONENTS ] = 1;
	if( sc.selected[ ROW_DIFFICULTY ] < 0 || sc.selected[ ROW_DIFFICULTY ] > 2 )
		sc.selected[ ROW_DIFFICULTY ] = 1;
	refreshValues();
}

// --- starting the game: what SkirmishGameOptionsMenuInit and reallyDoStart do ---
void startBattle()
{
	Screen &sc = s_screen;
	if( sc.maps.empty() || !TheGameLogic || !TheMessageStream || !TheMapCache )
		return;
	const Choice &map = sc.maps[ clampIndex( sc.selected[ ROW_MAP ], (Int)sc.maps.size() ) ];
	const Int general = sc.generals.empty() ? PLAYERTEMPLATE_RANDOM : sc.generals[ clampIndex( sc.selected[ ROW_GENERAL ], (Int)sc.generals.size() ) ].value;

	if( !TheSkirmishGameInfo )
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	else if( TheSkirmishGameInfo->isInGame() )
		TheSkirmishGameInfo->endGame();
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP( TheSkirmishGameInfo->getSlot( 0 )->getIP() );
	TheSkirmishGameInfo->enterGame();

	SkirmishPreferences prefs;
	GameSlot human;
	human.setName( prefs.getUserName() );
	human.setState( SLOT_PLAYER, prefs.getUserName() );
	human.setColor( -1 );
	human.setPlayerTemplate( general );
	TheSkirmishGameInfo->setSlot( 0, human );

	clampOpponents();
	const Int difficulty = clampIndex( sc.selected[ ROW_DIFFICULTY ], 3 );
	const SlotState aiState = difficulty == 0 ? SLOT_EASY_AI : (difficulty == 1 ? SLOT_MED_AI : SLOT_BRUTAL_AI);
	for( Int i = 1; i <= sc.selected[ ROW_OPPONENTS ] && i < MAX_SLOTS; ++i )
	{
		GameSlot ai;
		ai.setState( aiState );
		ai.setColor( -1 );
		ai.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
		TheSkirmishGameInfo->setSlot( i, ai );
	}

	TheSkirmishGameInfo->setSeed( GetTickCount() );
	if( !sc.cash.empty() && TheMultiplayerSettings )
	{
		const MultiplayerStartingMoneyList &list = TheMultiplayerSettings->getStartingMoneyList();
		const Int idx = clampIndex( sc.selected[ ROW_CASH ], (Int)list.size() );
		if( idx < (Int)list.size() )
			TheSkirmishGameInfo->setStartingCash( list[ idx ] );
	}
	TheSkirmishGameInfo->setSuperweaponRestriction( 0 );
	TheSkirmishGameInfo->setMap( map.key );
	const MapMetaData *md = TheMapCache->findMap( map.key );
	TheSkirmishGameInfo->setMapCRC( md ? md->m_CRC : 0 );
	TheSkirmishGameInfo->setMapSize( md ? md->m_filesize : 0 );

	// remember the setup (the Skirmish screen reads the same keys). Not SkirmishPreferences::write():
	// that reads the Skirmish screen's game-speed slider, which does not exist here.
	AsciiString tmp;
	tmp.format( "%d", general );
	prefs[ "PlayerTemplate" ] = tmp;
	prefs[ "Map" ] = map.key;
	prefs.setStartingCash( TheSkirmishGameInfo->getStartingCash() );
	prefs.setSuperweaponRestricted( FALSE );
	prefs.setInt( "QuickBattleOpponents", sc.selected[ ROW_OPPONENTS ] );
	prefs.setInt( "QuickBattleDifficulty", difficulty );
	prefs.UserPreferences::write();

	if( TheGameLogic->isInGame() )
		TheGameLogic->clearGameData( FALSE );
	TheWritableGlobalData->m_mapName = map.key;
	TheSkirmishGameInfo->startGame( 0 );
	InitRandom( TheSkirmishGameInfo->getSeed() );

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );   // not used; keeps the argument order of the Skirmish screen
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( TheGlobalData->m_framesPerSecondLimit );

	if( sc.root )
		sc.root->winHide( TRUE );
}

// --- window callbacks ---
WindowMsgHandledType swallowInput( GameWindow *, UnsignedInt msg, WindowMsgData, WindowMsgData )
{
	return (msg == GWM_CHAR || msg == GWM_MOUSE_POS) ? MSG_IGNORED : MSG_HANDLED;
}

WindowMsgHandledType screenSystem( GameWindow *, UnsignedInt msg, WindowMsgData mData1, WindowMsgData )
{
	if( msg != GBM_SELECTED )
		return MSG_IGNORED;
	const ButtonRef *ref = refFor( (GameWindow *)mData1 );
	if( !ref )
		return MSG_IGNORED;
	switch( ref->kind )
	{
		case KIND_PREV:  step( ref->row, -1 ); break;
		case KIND_NEXT:  step( ref->row, +1 ); break;
		case KIND_BACK:  if( s_screen.root ) s_screen.root->winHide( TRUE ); break;
		case KIND_START: startBattle(); break;
		default: break;
	}
	return MSG_HANDLED;
}

void screenDraw( GameWindow *window, WinInstanceData * )
{
	Screen &sc = s_screen;
	const Rect all = windowRect( window );
	fill( all, 0xF00A0D12 );
	if( sc.titleText )
		sc.titleText->draw( sc.title.x, sc.title.y + (sc.title.h - mmToPx( 4.2f )) / 2, Palette::Text, Palette::TextShadow );
	for( Int row = 0; row < ROW_COUNT; ++row )
	{
		const Rect &c = sc.card[ row ];
		fill( c, Palette::PanelFill );
		frame( c, Palette::PanelLine );
		if( sc.labelText[ row ] )
			sc.labelText[ row ]->draw( c.x + sc.pad, c.y + sc.pad, Palette::TextDim, Palette::TextShadow );
		if( sc.valueText[ row ] )
		{
			const Int labelH = mmToPx( 2.4f );
			Rect value = { c.x + sc.pad + sc.arrow + sc.pad, c.y + sc.pad + labelH + sc.gap, c.w - 2 * (sc.pad + sc.arrow + sc.pad), c.h - 2 * sc.pad - labelH - sc.gap };
			drawCentered( sc.valueText[ row ], value, Palette::Text );
		}
	}
}

// ◀ ▶ : a bed, a hairline and a chevron; bright while pressed
void arrowDraw( GameWindow *window, WinInstanceData * )
{
	const ButtonRef *ref = refFor( window );
	const Rect r = windowRect( window );
	const Bool down = pressed( window );
	fill( r, down ? Palette::ButtonFillHot : Palette::ButtonFill );
	frame( r, down ? Palette::GoldBright : Palette::ButtonLine );
	const Int cx = r.x + r.w / 2, cy = r.y + r.h / 2, half = r.h / 5, depth = r.w / 6;
	const Real thick = (Real)(s_screen.line * 3);
	const Int dir = (ref && ref->kind == KIND_NEXT) ? 1 : -1;
	const Int tipX = cx + dir * depth / 2, baseX = cx - dir * depth / 2;
	TheDisplay->drawLine( baseX, cy - half, tipX, cy, thick, Palette::Glyph );
	TheDisplay->drawLine( tipX, cy, baseX, cy + half, thick, Palette::Glyph );
}

// BACK / START BATTLE / QUICK BATTLE: a bed, a hairline and centred text
void textButtonDraw( GameWindow *window, WinInstanceData * )
{
	const ButtonRef *ref = refFor( window );
	const Rect r = windowRect( window );
	const Bool down = pressed( window );
	const Bool primary = ref && (ref->kind == KIND_START || ref->kind == KIND_ENTRY);
	fill( r, down ? Palette::ButtonFillHot : (primary ? 0xE02A2416 : Palette::ButtonFill) );
	frame( r, (down || primary) ? Palette::GoldBright : Palette::ButtonLine );
	DisplayString *text = nullptr;
	if( ref )
		text = ref->kind == KIND_BACK ? s_screen.backText : (ref->kind == KIND_START ? s_screen.startText : s_screen.entryText);
	drawCentered( text, r, Palette::Text );
}

GameWindow *makeButton( GameWindow *parent, const Rect &r, Kind kind, Int row, GameWinDrawFunc draw )
{
	Screen &sc = s_screen;
	if( sc.buttonCount >= (Int)(sizeof( sc.buttons ) / sizeof( sc.buttons[ 0 ] )) )
		return nullptr;
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	GameWindow *b = TheWindowManager->gogoGadgetPushButton( parent, WIN_STATUS_ENABLED, r.x, r.y, r.w, r.h, &inst, nullptr, FALSE );
	if( !b )
		return nullptr;
	b->winSetDrawFunc( draw );
	ButtonRef &ref = sc.buttons[ sc.buttonCount++ ];
	ref.window = b;
	ref.kind = kind;
	ref.row = row;
	return b;
}

void layout()
{
	Screen &sc = s_screen;
	const Int W = sc.displayW, H = sc.displayH;
	const Int margin = mmToPx( 4.0f );
	sc.gap = mmToPx( 2.0f );
	sc.pad = mmToPx( 1.5f );
	sc.line = mmToPx( 0.15f );
	if( sc.line < 1 ) sc.line = 1;
	sc.arrow = mmToPx( 10.0f );
	const Int titleH = mmToPx( 8.0f );
	sc.title = { margin, margin, W - 2 * margin, titleH };
	sc.backRect = { W - margin - mmToPx( 30.0f ), margin, mmToPx( 30.0f ), titleH };
	const Int cardW = (W - 2 * margin - sc.gap) / 2;
	Int cardH = (H - 2 * margin - titleH - 3 * sc.gap) / 3;
	const Int cardMax = mmToPx( 16.0f );
	if( cardH > cardMax ) cardH = cardMax;
	const Int top = sc.title.bottom() + sc.gap;
	const Int rowOf[ ROW_COUNT ] = { 0, 0, 1, 1, 2 };
	const Int colOf[ ROW_COUNT ] = { 0, 1, 0, 1, 0 };
	for( Int row = 0; row < ROW_COUNT; ++row )
		sc.card[ row ] = Rect{ margin + colOf[ row ] * (cardW + sc.gap), top + rowOf[ row ] * (cardH + sc.gap), cardW, cardH };
	sc.startCard = Rect{ margin + cardW + sc.gap, top + 2 * (cardH + sc.gap), cardW, cardH };
}

void destroyScreen()
{
	Screen &sc = s_screen;
	if( TheWindowManager )
	{
		if( sc.root )
			TheWindowManager->winDestroy( sc.root );    // destroys its buttons too
		if( sc.entry )
			TheWindowManager->winDestroy( sc.entry );
	}
	sc.root = nullptr;
	sc.entry = nullptr;
	sc.parent = nullptr;
	sc.buttonCount = 0;
	freeText( sc.titleText );
	freeText( sc.backText );
	freeText( sc.startText );
	freeText( sc.entryText );
	for( Int row = 0; row < ROW_COUNT; ++row )
	{
		freeText( sc.labelText[ row ] );
		freeText( sc.valueText[ row ] );
	}
	sc.generals.clear();
	sc.maps.clear();
	sc.cash.clear();
}

void openScreen()
{
	Screen &sc = s_screen;
	if( !sc.root )
		return;
	buildGenerals();
	buildMaps();
	buildCash();
	loadDefaults();
	for( Int i = 0; i < sc.buttonCount; ++i )
		if( sc.buttons[ i ].kind == KIND_START && sc.buttons[ i ].window )
			sc.buttons[ i ].window->winEnable( !sc.maps.empty() );
	sc.root->winHide( FALSE );
	sc.root->winBringToTop();
}

} // namespace

//-------------------------------------------------------------------------------------------------
void QuickBattleAttachToMainMenu( GameWindow *parentMainMenu )
{
	Screen &sc = s_screen;
	if( !parentMainMenu || !TheWindowManager || !TheDisplay || !TheFontLibrary )
		return;
	destroyScreen();
	sc.parent = parentMainMenu;
	sc.displayW = TheDisplay->getWidth();
	sc.displayH = TheDisplay->getHeight();
	sc.pxPerMm = pixelsPerMmFor( sc.displayW, sc.displayH );
	layout();

	sc.fontTitle  = TheFontLibrary->getFont( AsciiString( "Arial" ), mmToPx( 4.2f ), TRUE );
	sc.fontLabel  = TheFontLibrary->getFont( AsciiString( "Arial" ), mmToPx( 2.4f ), TRUE );
	sc.fontValue  = TheFontLibrary->getFont( AsciiString( "Arial" ), mmToPx( 3.2f ), TRUE );
	sc.fontButton = TheFontLibrary->getFont( AsciiString( "Arial" ), mmToPx( 3.0f ), TRUE );
	sc.titleText = makeText( sc.fontTitle, L"QUICK BATTLE" );
	sc.backText  = makeText( sc.fontButton, L"Back" );
	sc.startText = makeText( sc.fontButton, L"START BATTLE" );
	sc.entryText = makeText( sc.fontButton, L"QUICK BATTLE" );
	for( Int row = 0; row < ROW_COUNT; ++row )
	{
		sc.labelText[ row ] = makeText( sc.fontLabel, kLabel[ row ] );
		sc.valueText[ row ] = makeText( sc.fontValue, L"" );
		if( sc.valueText[ row ] )
			sc.valueText[ row ]->setWordWrap( sc.card[ row ].w - 2 * (sc.pad + sc.arrow + sc.pad) );
	}

	// the QUICK BATTLE button on the main menu, bottom-right, above the update notice's corner
	Rect entry = { sc.displayW - mmToPx( 4.0f ) - mmToPx( 42.0f ), sc.displayH - mmToPx( 4.0f ) - 40 - mmToPx( 11.0f ), mmToPx( 42.0f ), mmToPx( 11.0f ) };
	sc.entry = makeButton( parentMainMenu, entry, KIND_ENTRY, -1, textButtonDraw );

	// the screen: a full-size child of the main menu, created last so it is hit first while shown
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_USER_WINDOW );
	sc.root = TheWindowManager->winCreate( parentMainMenu, WIN_STATUS_ENABLED, 0, 0, sc.displayW, sc.displayH, screenSystem, &inst );
	if( !sc.root )
		return;
	sc.root->winSetDrawFunc( screenDraw );
	sc.root->winSetInputFunc( swallowInput );
	sc.root->winHide( TRUE );

	makeButton( sc.root, sc.backRect, KIND_BACK, -1, textButtonDraw );
	for( Int row = 0; row < ROW_COUNT; ++row )
	{
		const Rect &c = sc.card[ row ];
		Rect prev = { c.x + sc.pad, c.y + (c.h - sc.arrow) / 2, sc.arrow, sc.arrow };
		Rect next = { c.right() - sc.pad - sc.arrow, c.y + (c.h - sc.arrow) / 2, sc.arrow, sc.arrow };
		makeButton( sc.root, prev, KIND_PREV, row, arrowDraw );
		makeButton( sc.root, next, KIND_NEXT, row, arrowDraw );
	}
	makeButton( sc.root, sc.startCard, KIND_START, -1, textButtonDraw );
}

//-------------------------------------------------------------------------------------------------
void QuickBattleDetach()
{
	destroyScreen();
}

//-------------------------------------------------------------------------------------------------
Bool QuickBattleHandleButton( GameWindow *control )
{
	if( !control || control != s_screen.entry )
		return FALSE;
	openScreen();
	return TRUE;
}

#endif // __ANDROID__
