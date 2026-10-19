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

// FILE: PlaceEventTranslator.h ///////////////////////////////////////////////////////////
// Author: Steven Johnson, Dec 2001

#pragma once

#include "GameClient/InGameUI.h"

//-----------------------------------------------------------------------------
class PlaceEventTranslator : public GameMessageTranslator
{
private:
	UnsignedInt m_frameOfUpButton;

public:
	PlaceEventTranslator();
	virtual ~PlaceEventTranslator() override;
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg) override;
};

#if defined(__ANDROID__)
/// GeneralsX @feature Android touch placement 19/10/2026 A finger tap while a structure is pending:
/// anchor it at the tapped screen point and place it, the two steps the PC does on mouse-down and
/// on the click message. The touch layer calls it directly. TRUE when a construct order was sent.
Bool AndroidPlacementTap( const ICoord2D &screen );
#endif
