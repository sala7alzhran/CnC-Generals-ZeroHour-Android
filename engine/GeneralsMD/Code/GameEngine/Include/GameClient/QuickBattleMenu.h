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

// FILE: QuickBattleMenu.h ////////////////////////////////////////////////////////////////////////
// GeneralsX @feature Android quick battle 19/10/2026 (docs/MOBILE-UX-SPEC.md §7, Phase B-2)
//
// A phone-first way into a skirmish: one screen of big cards (general, map, opponents, difficulty,
// starting cash) and a Start button, built in code on top of the main menu. It fills the engine's
// skirmish setup (TheSkirmishGameInfo) and starts the game the way the Skirmish screen does.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#if defined(__ANDROID__)

#include "Lib/BaseType.h"

class GameWindow;

void QuickBattleAttachToMainMenu( GameWindow *parentMainMenu );	///< add the QUICK BATTLE button and the (hidden) screen to the main menu
void QuickBattleDetach();																				///< destroy them (main menu shutdown)
Bool QuickBattleHandleButton( GameWindow *control );						///< a GBM_SELECTED the main menu saw: TRUE if the control was ours

#endif // __ANDROID__
