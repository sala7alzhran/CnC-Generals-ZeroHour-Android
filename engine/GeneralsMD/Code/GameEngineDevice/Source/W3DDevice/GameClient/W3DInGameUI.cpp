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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DInGameUI.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Desct:	 In game user interface implementation for W3D
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>

#include "Common/GlobalData.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/ControlBar.h"
#if defined(__ANDROID__)
#include "GameClient/VideoPlayer.h"   // TheVideoPlayer->firstStream() for the Skip button
#include "GameClient/AndroidHud.h"    // shared geometry and palette of the phone HUD
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"      // command names for the drawer's name label
#include "GameClient/Mouse.h"         // the tile under the (synthesized) cursor
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#endif
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DGUICallbacks.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/hanim.h"

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.



#ifdef RTS_DEBUG
#include "W3DDevice/GameClient/HeightMap.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/vertmaterial.h"
class DebugHintObject : public RenderObjClass
{

public:

	DebugHintObject();
	DebugHintObject(const DebugHintObject & src);
	DebugHintObject & operator = (const DebugHintObject &);
	~DebugHintObject();

	virtual RenderObjClass *	Clone() const;
	virtual int						Class_ID() const;
	virtual void					Render(RenderInfoClass & rinfo);
	virtual Bool					Cast_Ray(RayCollisionTestClass & raytest);

	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const;
  virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & aabox) const;

	int updateBlock();
	void freeMapResources();
	void setLocAndColorAndSize(const Coord3D *loc, Int argb, Int size);

protected:

	Coord3D m_myLoc;
	Int m_myColor;	// argb
	Int m_mySize;

	DX8IndexBufferClass				*m_indexBuffer;
	ShaderClass								m_shaderClass; //shader or rendering state for heightmap
	VertexMaterialClass	  	  *m_vertexMaterialClass;
	DX8VertexBufferClass			*m_vertexBufferTile;	//First vertex buffer.

	void initData();
};

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )


DebugHintObject::~DebugHintObject()
{
	freeMapResources();
}

DebugHintObject::DebugHintObject() :
	m_indexBuffer(nullptr),
	m_vertexMaterialClass(nullptr),
	m_vertexBufferTile(nullptr),
	m_myColor(0),
	m_mySize(0)
{
	initData();
}

Bool DebugHintObject::Cast_Ray(RayCollisionTestClass & raytest)
{
	return false;
}

DebugHintObject::DebugHintObject(const DebugHintObject & src)
{
	*this = src;
}

DebugHintObject & DebugHintObject::operator = (const DebugHintObject & that)
{
	DEBUG_CRASH(("oops"));
	return *this;
}

void DebugHintObject::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	Vector3	ObjSpaceCenter((float)1000*0.5f,(float)1000*0.5f,(float)0);
	float length = ObjSpaceCenter.Length();
	sphere.Init(ObjSpaceCenter, length);
}

void DebugHintObject::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	Vector3	minPt(0,0,0);
	Vector3	maxPt((float)1000,(float)1000,(float)1000);
	box.Init(minPt,maxPt);
}

Int DebugHintObject::Class_ID() const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

RenderObjClass * DebugHintObject::Clone() const
{
	DEBUG_CRASH(("oops"));
	return NEW DebugHintObject(*this);
}


void DebugHintObject::freeMapResources()
{
	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexBufferTile);
	REF_PTR_RELEASE(m_vertexMaterialClass);
}

//Allocate a heightmap of x by y vertices.
//data must be an array matching this size.
void DebugHintObject::initData()
{
	freeMapResources();	//free old data and ib/vb

	m_indexBuffer = NEW_REF(DX8IndexBufferClass,(3));

	// Fill up the IB
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		ib[0]=0;
		ib[1]=1;
		ib[2]=2;
	}

	m_vertexBufferTile = NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV1,3,DX8VertexBufferClass::USAGE_DEFAULT));

	//go with a preset material for now.
	m_vertexMaterialClass = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);

	//use a multi-texture shader: (text1*diffuse)*text2.
	m_shaderClass = ShaderClass(SC_ALPHA);
}

void DebugHintObject::setLocAndColorAndSize(const Coord3D *loc, Int argb, Int size)
{
	m_myLoc = *loc;
	m_myColor = argb;
	m_mySize = size;

	if (m_myLoc.z < 0 && TheTerrainRenderObject)
	{
		m_myLoc.z = TheTerrainRenderObject->getHeightMapHeight(m_myLoc.x, m_myLoc.y, nullptr);
	}

	if (m_vertexBufferTile)
	{
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexBufferTile);
		VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();

		Real x1 = m_mySize * 0.866;	// cos(30)
		Real y1 = m_mySize * 0.5;		// sin(30)

		// note, pts must go in a counterclockwise order!
		vb[0].x = 0;
		vb[0].y = m_mySize;
		vb[0].z = 0;
		vb[0].diffuse = m_myColor;
		vb[0].u1 = 0;
		vb[0].v1 = 0;

		vb[1].x = -x1;
		vb[1].y = -y1;
		vb[1].z = 0;
		vb[1].diffuse = m_myColor;
		vb[1].u1 = 0;
		vb[1].v1 = 0;

		vb[2].x = x1;
		vb[2].y = -y1;
		vb[2].z = 0;
		vb[2].diffuse = m_myColor;
		vb[2].u1 = 0;
		vb[2].v1 = 0;
	}
}

void DebugHintObject::Render(RenderInfoClass & rinfo)
{
	SphereClass bounds(Vector3(m_myLoc.x, m_myLoc.y, m_myLoc.z), m_mySize);
	if (!rinfo.Camera.Cull_Sphere(bounds))
	{
		DX8Wrapper::Set_Material(m_vertexMaterialClass);
		DX8Wrapper::Set_Shader(m_shaderClass);
		DX8Wrapper::Set_Texture(0, nullptr);
		DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
		DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile);

		Matrix3D tm(Transform);
		Vector3 vec(m_myLoc.x, m_myLoc.y, m_myLoc.z);
		tm.Set_Translation(vec);
		DX8Wrapper::Set_Transform(D3DTS_WORLD, tm);

		DX8Wrapper::Draw_Triangles(	0, 1, 0, 3);
	}
}
#endif // RTS_DEBUG


///////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::W3DInGameUI()
{
	Int i;

	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{

		m_moveHintRenderObj[ i ] = nullptr;
		m_moveHintAnim[ i ] = nullptr;

	}

	m_buildingPlacementAnchor = nullptr;
	m_buildingPlacementArrow = nullptr;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::~W3DInGameUI()
{
	Int i;

	// remove render objects for hints
	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{

		REF_PTR_RELEASE( m_moveHintRenderObj[ i ] );
		REF_PTR_RELEASE( m_moveHintAnim[ i ] );

	}

	REF_PTR_RELEASE( m_buildingPlacementAnchor );
	REF_PTR_RELEASE( m_buildingPlacementArrow );

}

// loadText ===================================================================
/** Load text from the file */
//=============================================================================
static void loadText( char *filename, GameWindow *listboxText )
{
	if (!listboxText)
		return;
	GadgetListBoxReset(listboxText);

	FILE *fp;

	// open the file
	fp = fopen( filename, "r" );
	if( fp == nullptr )
		return;

	char buffer[ 1024 ];
	UnicodeString line;
	Color color = GameMakeColor(255, 255, 255, 255);
	while( fgets( buffer, 1024, fp ) != nullptr )
	{
		line.translate(buffer);
		line.trim();
		if (line.isEmpty())
			line = L" ";
		GadgetListBoxAddEntryText(listboxText, line, color, -1, -1);
	}

	// close the file
	fclose( fp );

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::init()
{

	// extending functionality
	InGameUI::init();
	// for the beta, they didn't want the help menu showing up, but I left this as a bock
	// comment because we'll probably want to add this back in.
/*
		// create the MOTD
		GameWindow *motd = TheWindowManager->winCreateFromScript( "MOTD.wnd" );
		if( motd )
		{
			NameKeyType listboxTextID = TheNameKeyGenerator->nameToKey( "MOTD.wnd:ListboxMOTD" );
			GameWindow *listboxText = TheWindowManager->winGetWindowFromId(motd, listboxTextID);

			loadText( "HelpScreen.txt", listboxText );

			// hide it for now
			motd->winHide( TRUE );

		}
*/

}

//-------------------------------------------------------------------------------------------------
/** Update in game UI */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::update()
{

	// call base
	InGameUI::update();

}

#if defined(__ANDROID__)
namespace { void releaseAndroidHudText(); }   // defined with the HUD overlay pass below
#endif
//-------------------------------------------------------------------------------------------------
/** Reset the in game ui */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::reset()
{

	// call base
	InGameUI::reset();


#if defined(__ANDROID__)
	// GeneralsX @feature Android smart HUD: the overlay pass's text objects are per game
	releaseAndroidHudText();
#endif
}

#if defined(__ANDROID__)
// GeneralsX @feature Android smart HUD 19/10/2026 The HUD's overlay pass, drawn after the window
// system so it sits on top of the bar's own windows: the overlay buttons that are not GameWindows
// (the ☰ menu and the left-thumb column: control groups with their unit count, one "+" slot to
// assign a selection, ALL, IDLE) and the chrome of the command drawer's tiles (hairline frames, cost
// pills, lock badges, the name of the tile under the finger). Presence and geometry come from
// ControlBar (phoneOverlays / phoneMetrics), the same source the touch layer hit-tests against
// (SDL3GameEngine.cpp), so draw, hit-test and layout cannot drift apart.
extern Int TheAndroidOverlayPressed;   // the overlay the finger is holding down (-1: none); set by the touch layer
namespace {
	typedef AndroidHud::Rect HudRect;   // WWLib has a global HudRect too
	namespace Palette = AndroidHud::Palette;

	// Text objects live for the session and are re-set only when their content changes: nothing
	// here allocates per frame. Released in W3DInGameUI::reset().
	DisplayString *s_overlayLabel[ AndroidHud::OVERLAY_COUNT ] = { nullptr };
	DisplayString *s_smallText[ AndroidHud::NUM_CONTROL_GROUPS ] = { nullptr };   // unit count, or the slot number on the "+" tile
	Int            s_smallValue[ AndroidHud::NUM_CONTROL_GROUPS ] = { 0 };
	DisplayString *s_costText[ AndroidHud::GRID_CELLS ] = { nullptr };
	Int            s_costValue[ AndroidHud::GRID_CELLS ] = { 0 };
	DisplayString *s_nameText = nullptr;
	const CommandButton *s_nameCommand = nullptr;
	GameFont *s_fontBig = nullptr, *s_fontSmall = nullptr, *s_fontLabel = nullptr;
	const WideChar *const kOverlayText[ AndroidHud::OVERLAY_COUNT ] = { L"", L"1", L"2", L"3", L"4", L"ALL", L"IDLE" };

	void ensureFonts()
	{
		if( !TheControlBar )
			return;
		if( !s_fontBig )   s_fontBig   = TheControlBar->getPhoneHudFont( 3.6f );
		if( !s_fontSmall ) s_fontSmall = TheControlBar->getPhoneHudFont( 1.9f );
		if( !s_fontLabel ) s_fontLabel = TheControlBar->getPhoneHudFont( 2.6f );
	}

	DisplayString *makeText( GameFont *font )
	{
		if( !TheDisplayStringManager )
			return nullptr;
		DisplayString *s = TheDisplayStringManager->newDisplayString();
		if( s && font )
			s->setFont( font );
		return s;
	}

	void releaseText( DisplayString *&s )
	{
		if( s && TheDisplayStringManager )
			TheDisplayStringManager->freeDisplayString( s );
		s = nullptr;
	}

	void releaseAndroidHudText()
	{
		for( Int i = 0; i < AndroidHud::OVERLAY_COUNT; ++i ) releaseText( s_overlayLabel[ i ] );
		for( Int i = 0; i < AndroidHud::NUM_CONTROL_GROUPS; ++i ) releaseText( s_smallText[ i ] );
		for( Int i = 0; i < AndroidHud::GRID_CELLS; ++i ) releaseText( s_costText[ i ] );
		releaseText( s_nameText );
		s_nameCommand = nullptr;
	}

	// --- primitives ---
	inline void fill( const HudRect &r, UnsignedInt color )
	{
		if( r.w > 0 && r.h > 0 )
			TheDisplay->drawFillRect( r.x, r.y, r.w, r.h, color );
	}
	inline void frame( const HudRect &r, Int line, UnsignedInt color )
	{
		if( r.w > 0 && r.h > 0 )
			TheDisplay->drawOpenRect( r.x, r.y, r.w, r.h, (Real)line, color );
	}
	inline HudRect windowRect( GameWindow *w )
	{
		ICoord2D pos, size;
		w->winGetScreenPosition( &pos.x, &pos.y );
		w->winGetSize( &size.x, &size.y );
		HudRect r = { pos.x, pos.y, size.x, size.y };
		return r;
	}
	void drawCentered( DisplayString *s, const HudRect &r, UnsignedInt color )
	{
		Int w = 0, h = 0;
		s->getSize( &w, &h );
		s->draw( r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, color, Palette::TextShadow );
	}
	// a small text in a filled pill, anchored by its bottom-right corner
	void drawPill( DisplayString *s, Int right, Int bottom, Int padX, Int padY, UnsignedInt fillColor, UnsignedInt textColor, Int line )
	{
		Int w = 0, h = 0;
		s->getSize( &w, &h );
		HudRect pill = { right - w - 2 * padX, bottom - h - 2 * padY, w + 2 * padX, h + 2 * padY };
		fill( pill, fillColor );
		if( line > 0 )
			frame( pill, line, Palette::TileLine );
		s->draw( pill.x + padX, pill.y + padY, textColor, Palette::TextShadow );
	}

	// An overlay button: bed + hairline (bright while pressed). A ghost is a hairline only.
	void drawOverlayFrame( const HudRect &r, Int line, Bool pressed, Bool ghost )
	{
		if( ghost )
		{
			frame( r, line, pressed ? Palette::TileLineHot : Palette::GhostLine );
			return;
		}
		fill( r, pressed ? Palette::PressedFill : Palette::TileFill );
		frame( r, line, pressed ? Palette::TileLineHot : Palette::TileLine );
	}

	void drawMenuGlyph( const HudRect &r, UnsignedInt color )
	{
		const Int barH = r.h / 12 < 3 ? 3 : r.h / 12;
		const Int barW = r.w * 2 / 5;
		const Int bx = r.x + (r.w - barW) / 2, cy = r.y + r.h / 2, step = barH * 5 / 2;
		for( Int k = -1; k <= 1; ++k )
			TheDisplay->drawFillRect( bx, cy + k * step - barH / 2, barW, barH, color );
	}

	void drawPlusGlyph( const HudRect &r, UnsignedInt color )
	{
		const Int t = r.h / 14 < 2 ? 2 : r.h / 14, len = r.w * 2 / 5;
		const Int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
		TheDisplay->drawFillRect( cx - len / 2, cy - t / 2, len, t, color );
		TheDisplay->drawFillRect( cx - t / 2, cy - len / 2, t, len, color );
	}

	// A padlock in the tile's top-right corner: a dark box, a body, a shackle of three lines.
	void drawLockGlyph( const HudRect &tile, Int line )
	{
		const Int s = tile.h / 3;
		const HudRect box = { tile.right() - line * 2 - s, tile.y + line * 2, s, s };
		fill( box, Palette::LockFill );
		const Int bw = s * 5 / 8, bh = s * 3 / 8;
		const Int bx = box.x + (s - bw) / 2, by = box.y + s - bh - s / 8;
		TheDisplay->drawFillRect( bx, by, bw, bh, Palette::Glyph );
		const Real lw = (Real)(line * 2);
		const Int sw = bw * 3 / 5, sx0 = bx + (bw - sw) / 2, sx1 = sx0 + sw, sy = by - bh * 3 / 4;
		TheDisplay->drawLine( sx0, by, sx0, sy, lw, Palette::Glyph );
		TheDisplay->drawLine( sx1, by, sx1, sy, lw, Palette::Glyph );
		TheDisplay->drawLine( sx0, sy, sx1, sy, lw, Palette::Glyph );
	}

	void drawOverlayLabel( Int id, const HudRect &r, UnsignedInt color )
	{
		DisplayString *&str = s_overlayLabel[ id ];
		if( !str )
		{
			str = makeText( s_fontBig );
			if( !str )
				return;
			str->setText( UnicodeString( kOverlayText[ id ] ) );
		}
		drawCentered( str, r, color );
	}

	// the small number on a group tile: the unit count (bottom-right pill), or the slot number on a
	// "+" tile (top-left, no pill)
	void drawGroupNumber( Int group, Int value, const HudRect &r, Bool ghost, const AndroidHud::Metrics &m )
	{
		DisplayString *&str = s_smallText[ group ];
		if( !str )
		{
			str = makeText( s_fontSmall );
			if( !str )
				return;
			s_smallValue[ group ] = value - 1;   // force the first set
		}
		if( s_smallValue[ group ] != value )
		{
			s_smallValue[ group ] = value;
			UnicodeString text;
			text.format( L"%d", value );
			str->setText( text );
		}
		if( ghost )
			str->draw( r.x + m.gap, r.y + m.gap / 2, Palette::GlyphDim, Palette::TextShadow );
		else
			drawPill( str, r.right() - m.line, r.bottom() - m.line, m.gap / 2 + 1, 1, Palette::BadgeFill, Palette::BadgeText, 0 );
	}

	void drawCostPill( Int cell, const HudRect &tile, Int cost, Bool affordable, const AndroidHud::Metrics &m )
	{
		DisplayString *&str = s_costText[ cell ];
		if( !str )
		{
			str = makeText( s_fontSmall );
			if( !str )
				return;
			s_costValue[ cell ] = cost - 1;   // force the first set
		}
		if( s_costValue[ cell ] != cost )
		{
			s_costValue[ cell ] = cost;
			UnicodeString text;
			text.format( L"$%d", cost );
			str->setText( text );
		}
		drawPill( str, tile.right() - m.line, tile.bottom() - m.line, m.gap / 2 + 1, 1, Palette::CostFill,
							affordable ? Palette::CostText : Palette::CostTextPoor, 0 );
	}

	// The name of the command tile under the cursor (the last touched tile: the synthesized mouse
	// stays where the finger lifted), in a label beside the drawer so it never covers a tile.
	void drawNameLabel( const AndroidHud::Metrics &m )
	{
		if( !TheMouse || !TheWindowManager || !TheGameText )
			return;
		const MouseIO *io = TheMouse->getMouseStatus();
		GameWindow *under = TheWindowManager->getWindowUnderCursor( io->pos.x, io->pos.y );
		GameWindow *tile = nullptr;
		for( Int c = 0; c < AndroidHud::GRID_CELLS && !tile; ++c )
			if( under && TheControlBar->getPhoneCommandWindow( c ) == under )
				tile = under;
		if( !tile || tile->winIsHidden() )
			return;
		const CommandButton *command = TheControlBar->getPhoneCommand( tile );
		if( !command || command->getTextLabel().isEmpty() )
			return;
		if( !s_nameText )
		{
			s_nameText = makeText( s_fontLabel );
			if( !s_nameText )
				return;
			s_nameCommand = nullptr;
		}
		if( command != s_nameCommand )
		{
			s_nameCommand = command;
			s_nameText->setText( TheGameText->fetch( command->getTextLabel() ) );
		}
		AndroidHud::Rect drawer;
		if( !TheControlBar->getPhoneDrawerRect( drawer ) )
			return;
		const HudRect t = windowRect( tile );
		Int w = 0, h = 0;
		s_nameText->getSize( &w, &h );
		HudRect box = { drawer.x - m.gap - (w + 2 * m.pad), t.y + (t.h - (h + 2 * m.gap)) / 2, w + 2 * m.pad, h + 2 * m.gap };
		if( box.x < m.margin )
			box.x = m.margin;
		fill( box, Palette::LabelFill );
		frame( box, m.line, Palette::PanelLine );
		s_nameText->draw( box.x + m.pad, box.y + m.gap, Palette::Text, Palette::TextShadow );
	}

	void drawAndroidHud()
	{
		if( !TheDisplay || !TheControlBar || !TheControlBar->phoneHudVisible() )
			return;
		ensureFonts();
		const AndroidHud::Metrics &m = TheControlBar->phoneMetrics();
		AndroidHud::OverlayState ov[ AndroidHud::OVERLAY_COUNT ];
		TheControlBar->phoneOverlays( ov );

		// ☰ menu: three stacked bars
		if( ov[ AndroidHud::OVERLAY_MENU ].visible )
		{
			const HudRect &r = ov[ AndroidHud::OVERLAY_MENU ].rect;
			const Bool pressed = TheAndroidOverlayPressed == AndroidHud::OVERLAY_MENU;
			drawOverlayFrame( r, m.line, pressed, FALSE );
			drawMenuGlyph( r, Palette::Glyph );
		}

		// left column: the groups that exist (with their unit count), the "+" slot, ALL, IDLE
		for( Int g = 0; g < AndroidHud::NUM_CONTROL_GROUPS; ++g )
		{
			const Int id = AndroidHud::OVERLAY_GROUP_1 + g;
			const AndroidHud::OverlayState &o = ov[ id ];
			if( !o.visible )
				continue;
			const Bool pressed = TheAndroidOverlayPressed == id;
			drawOverlayFrame( o.rect, m.line, pressed, o.ghost );
			if( o.ghost )
			{
				drawPlusGlyph( o.rect, pressed ? Palette::Glyph : Palette::GlyphDim );
				drawGroupNumber( g, g + 1, o.rect, TRUE, m );
			}
			else
			{
				drawOverlayLabel( id, o.rect, Palette::Text );
				if( o.badge > 0 )
					drawGroupNumber( g, o.badge, o.rect, FALSE, m );
			}
		}
		for( Int id = AndroidHud::OVERLAY_SELECT_ALL; id <= AndroidHud::OVERLAY_IDLE_WORKER; ++id )
		{
			const AndroidHud::OverlayState &o = ov[ id ];
			if( !o.visible )
				continue;
			drawOverlayFrame( o.rect, m.line, TheAndroidOverlayPressed == id, FALSE );
			drawOverlayLabel( id, o.rect, Palette::Text );
		}

		// drawer tiles: hairline, price, lock
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		const Int money = player ? (Int)player->getMoney()->countMoney() : 0;
		for( Int c = 0; c < AndroidHud::GRID_CELLS; ++c )
		{
			GameWindow *tile = TheControlBar->getPhoneCommandWindow( c );
			if( !tile || tile->winIsHidden() )
				continue;
			const HudRect t = windowRect( tile );
			frame( AndroidHud::inflate( t, m.line ), m.line, Palette::TileLine );
			const Int cost = TheControlBar->getPhoneCommandCost( TheControlBar->getPhoneCommand( tile ) );
			if( cost > 0 )
				drawCostPill( c, t, cost, money >= cost, m );
			if( !tile->winGetEnabled() && !BitIsSet( tile->winGetStatus(), WIN_STATUS_NOT_READY ) )
				drawLockGlyph( t, m.line );
		}

		drawNameLabel( m );
	}

	// On-screen SKIP button (top-right), shown during any video/cutscene. Tap → ESC (skip),
	// hit-tested in SDL3GameEngine.cpp. KEEP GEOMETRY IN SYNC WITH SKIP_BTN_* THERE
	// (right-anchored: W - 20 - 150, y 18, 150x54). Drawn here (not W3DDisplay) because the
	// loadscreen render path calls TheInGameUI->draw() then early-outs before the display's
	// end-of-frame code, so this is the one draw hook that runs during a mission-intro movie.
	void drawAndroidSkipButton()
	{
		if (!TheDisplay)
			return;
		const Int bw = 150, bh = 54, bx = TheDisplay->getWidth() - 20 - bw, by = 18;
		TheDisplay->drawFillRect(bx, by, bw, bh, GameMakeColor(18, 22, 28, 190));
		TheDisplay->drawOpenRect(bx, by, bw, bh, 2.0f, GameMakeColor(214, 184, 112, 240));
		const UnsignedInt g = GameMakeColor(230, 205, 140, 245);
		const Int cy = by + bh / 2, th = 12, tw = 13;
		const Int tipA = bx + bw / 2 - 2, tipB = tipA + 18;   // two ">>" chevrons = fast-forward/skip
		TheDisplay->drawLine(tipA - tw, cy - th, tipA, cy, 4.0f, g);
		TheDisplay->drawLine(tipA, cy, tipA - tw, cy + th, 4.0f, g);
		TheDisplay->drawLine(tipB - tw, cy - th, tipB, cy, 4.0f, g);
		TheDisplay->drawLine(tipB, cy, tipB - tw, cy + th, 4.0f, g);
	}
}
#endif // __ANDROID__

//-------------------------------------------------------------------------------------------------
/** Draw member for the W3D implementation of the game user interface */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::draw()
{
	TheDisplay->beginBatch();
	preDraw();

	// draw selection region if drag selecting
	if( m_isDragSelecting )
		drawSelectionRegion();

	// for each view draw hints
	/// @todo should the UI be iterating through views like this?
	if( TheDisplay )
	{
		View *view;

		for( view = TheDisplay->getFirstView();
				 view;
				 view = TheDisplay->getNextView( view ) )
		{

			// draw move hints
			drawMoveHints( view );

			// draw attack hints
			drawAttackHints( view );

			// draw placement angle selection if needed
			drawPlaceAngle( view );

		}

	}

	// repaint all our windows

#ifdef EXTENDED_STATS
	if (!DX8Wrapper::stats.m_disableConsole) {
#endif

#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	extern Bool g_UT_startTiming;
	if (!g_UT_startTiming)
#endif

	postDraw();

	TheWindowManager->winRepaint();

	postWindowDraw();

#ifdef EXTENDED_STATS
	}
#endif

#if defined(__ANDROID__)
	// Drawn last so it sits on top of the HUD/video. During a cutscene show the SKIP button
	// (top-right); otherwise, while actually playing, the overlay buttons (menu, control groups).
	if (TheVideoPlayer && TheVideoPlayer->firstStream())
		drawAndroidSkipButton();
	else if (TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !isQuitMenuVisible())
		drawAndroidHud();
#endif

	TheDisplay->endBatch();

}

//-------------------------------------------------------------------------------------------------
/** draw 2d selection region on screen */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawSelectionRegion()
{
	Real width = 2.0f;
	UnsignedInt color = 0x9933FF33;  //0xAARRGGBB

	TheDisplay->drawOpenRect( m_dragSelectRegion.lo.x,
														m_dragSelectRegion.lo.y,
														m_dragSelectRegion.hi.x - m_dragSelectRegion.lo.x,
														m_dragSelectRegion.hi.y - m_dragSelectRegion.lo.y,
														width,
														color );

}

//-------------------------------------------------------------------------------------------------
/** Draw the visual feedback for clicking in the world and telling units
	* to move there */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawMoveHints( View *view )
{
	Int i;
//	Real width = 1.0f;
//	UnsignedInt color = 0x9933FF33;  //0xAARRGGBB

	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;

		if( elapsed <= 40 )
		{
			RectClass rect;

			// if this hint is not in this view ignore it
			/// @todo write this to check if point is visible in view
//			if( view->pointInView( &m_moveHint[ i ].pos == FALSE )
//				continue;

			// create render object and add to scene of needed
			if( m_moveHintRenderObj[ i ] == nullptr )
			{
				RenderObjClass *hint;
				HAnimClass *anim;

				// create hint object
				hint = W3DDisplay::m_assetManager->Create_Render_Obj(TheGlobalData->m_moveHintName.str());

				AsciiString animName;
				animName.format("%s.%s", TheGlobalData->m_moveHintName.str(), TheGlobalData->m_moveHintName.str());
				anim = W3DDisplay::m_assetManager->Get_HAnim(animName.str());

				// sanity
				if( hint == nullptr )
				{

					DEBUG_CRASH(("unable to create hint"));
					return;

				}

				// assign render objects to GUI data
				m_moveHintRenderObj[ i ] = hint;

				// note that 'anim' is returned from Get_HAnim with an AddRef, so we don't need to addref it again.
				// however, we do need to release the contents of moveHintAnim (if any)
				REF_PTR_RELEASE(m_moveHintAnim[i]);
				m_moveHintAnim[i] = anim;

			}

			// show the render object if hidden
			if( m_moveHintRenderObj[ i ]->Is_Hidden() == 1 ) {
				m_moveHintRenderObj[ i ]->Set_Hidden( 0 );
				// add to scene
				W3DDisplay::m_3DScene->Add_Render_Object( m_moveHintRenderObj[ i ] );
				if (m_moveHintAnim[i])
					m_moveHintRenderObj[i]->Set_Animation(m_moveHintAnim[i], 0, RenderObjClass::ANIM_MODE_ONCE);
			}

			// move this hint render object to the position and align with terrain
			Matrix3D transform;
			PathfindLayerEnum layer = TheTerrainLogic->alignOnTerrain( 0, m_moveHint[ i ].pos, true, transform );

			Real waterZ;
			if (layer == LAYER_GROUND && TheTerrainLogic->isUnderwater(m_moveHint[ i ].pos.x, m_moveHint[ i ].pos.y, &waterZ))
			{
				Coord3D tmp = m_moveHint[ i ].pos;
				tmp.z = waterZ;
				Coord3D normal;
				normal.x = 0;
				normal.y = 0;
				normal.z = 1;
				makeAlignToNormalMatrix(0, tmp, normal, transform);
			}

			m_moveHintRenderObj[ i ]->Set_Transform( transform );

#if 0
			// if there is a source then draw line from source to destination
			Object *obj = TheGameLogic->getObject( m_moveHint[ i ].sourceID );
			if( obj )
			{
				Drawable *source = obj->getDrawable();

				if( source )
				{
					Coord3D pos;
					ICoord2D start, end;

					// project start and end point to screen point
					source->getPosition( &pos );
					view->worldToScreen( &pos, &start );
					view->worldToScreen( &hintPos, &end );

					// draw the line
					TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

				}
			}
#endif

		}
		else
		{

			// hide hint marker
			if( m_moveHintRenderObj[ i ] )
				if( m_moveHintRenderObj[ i ]->Is_Hidden() == 0 ) {
					m_moveHintRenderObj[ i ]->Set_Hidden( 1 );
					W3DDisplay::m_3DScene->Remove_Render_Object( m_moveHintRenderObj[ i ] );
				}

		}

	}

}

//-------------------------------------------------------------------------------------------------
/** Draw visual back for clicking to attack a unit in the world */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackHints( View *view )
{

}

//-------------------------------------------------------------------------------------------------
/** Draw the angle selection for placing building if needed */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawPlaceAngle( View *view )
{
//	Coord2D v, p, o;
	//Real size = 15.0f;

	//Create the anchor & arrow if not already created!
	if( !m_buildingPlacementAnchor )
	{
		m_buildingPlacementAnchor = W3DDisplay::m_assetManager->Create_Render_Obj( "Locater01" );

		// sanity
		if( !m_buildingPlacementAnchor )
		{
			DEBUG_CRASH( ("Unable to create BuildingPlacementAnchor (Locator01.w3d) -- cursor for placing buildings") );
			return;
		}
	}
	if( !m_buildingPlacementArrow )
	{
		m_buildingPlacementArrow = W3DDisplay::m_assetManager->Create_Render_Obj( "Locater02" );

		// sanity
		if( !m_buildingPlacementArrow )
		{
			DEBUG_CRASH( ("Unable to create BuildingPlacementArrow (Locator02.w3d) -- cursor for placing buildings") );
			return;
		}
	}

	Bool anchorInScene = m_buildingPlacementAnchor->Peek_Scene() != nullptr;
	Bool arrowInScene	 = m_buildingPlacementArrow->Peek_Scene() != nullptr;

	// get out of here if this display isn't up anyway
	if( isPlacementAnchored() == FALSE )
	{
		if( anchorInScene )
		{
			//If our anchor is in the scene, remove it from the scene but don't delete it.
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementAnchor );
		}
		if( arrowInScene )
		{
			//If our arrow is in the scene, remove it from the scene but don't delete it.
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementArrow );
		}
		return;
	}

	// get the anchor points
	ICoord2D start, end;
	getPlacementPoints( &start, &end );




	Coord3D vector;
	vector.x = end.x - start.x;
	vector.y = end.y - start.y;
	vector.z = 0.0f;
	Real length = vector.length();

	Bool showArrow = length >= 5.0f;

	if( showArrow )
	{
		if( anchorInScene )
		{
			//We're switching to the arrow!
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementAnchor );
		}
		if( !arrowInScene )
		{
			W3DDisplay::m_3DScene->Add_Render_Object( m_buildingPlacementArrow );
			arrowInScene = TRUE;
		}
	}
	else
	{
		if( arrowInScene )
		{
			//We're switching to the anchor!
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementArrow );
		}
		if( !anchorInScene )
		{
			W3DDisplay::m_3DScene->Add_Render_Object( m_buildingPlacementAnchor );
			anchorInScene = TRUE;
		}
	}

	//The proper way to orient the placement arrow is to copy the matrix from the m_placeIcon[0]!
	if( anchorInScene )
	{
		if ( m_placeIcon[ 0 ] )
			m_buildingPlacementAnchor->Set_Transform( *m_placeIcon[ 0 ]->getTransformMatrix() );
	}
	else if( arrowInScene )
	{
		if ( m_placeIcon[ 0 ] )
			m_buildingPlacementArrow->Set_Transform( *m_placeIcon[ 0 ]->getTransformMatrix() );
	}


	//m_buildingPlacementArrow->Set_Transform(

	// draw a little box at the start to show the "anchor" point
	//Real rectSize = 4.0f;
	//TheDisplay->drawFillRect( start.x - rectSize / 2, start.y - rectSize / 2,
	//													rectSize, rectSize, color );

	// compute vector for line
	//v.x = end.x - start.x;
	//v.y = end.y - start.y;
	//v.normalize();

	// compute opposite vector
	//o.x = -v.x;
	//o.y = -v.y;

	// compute perpendicular vector one way
	//p.x = -v.y;
	//p.y = v.x;

	// draw the line
	//start.x = o.x * size + p.x * (size/2.0f) + end.x;
	//start.y = o.y * size + p.y * (size/2.0f) + end.y;
	//TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

	// compute perpendicular vector other way
	//p.x = v.y;
	//p.y = -v.x;

	// draw the line
	//start.x = o.x * size + p.x * (size/2.0f) + end.x;
	//start.y = o.y * size + p.y * (size/2.0f) + end.y;
	//TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

}

