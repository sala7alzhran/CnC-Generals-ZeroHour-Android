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

// IPEnumeration.h ///////////////////////////////////////////////////////////////
// Class for enumerating IP addresses
// Author: Matthew D. Campbell, October 2001

#pragma once

#include "GameNetwork/Transport.h"

// GeneralsX @bugfix Android 19/10/2026 What kind of network interface an address sits on, judged by
// the interface name (POSIX only; Windows enumerates by host name and leaves every address UNKNOWN).
// A phone has several IPv4 interfaces at once (Wi-Fi, cellular, hotspot, VPN), and the old
// "lowest address first" rule picked the cellular one (10.x), which no LAN peer can reach.
enum LANInterfaceClass
{
	LAN_INTERFACE_UNKNOWN = 0,	///< no name (Windows) or a name we do not recognise
	LAN_INTERFACE_WIFI,					///< Wi-Fi station or hotspot: wl*, ap*, swlan*, softap*
	LAN_INTERFACE_VPN,					///< overlay networks such as ZeroTier and Tailscale: tun*, zt*, tailscale*
	LAN_INTERFACE_WIRED,				///< Ethernet: eth*, en*
	LAN_INTERFACE_AVOID,				///< cellular data and virtual links: rmnet*, ccmni*, v4-*, dummy*, p2p*, ifb*
};

/**
 * IP wrapper class.
 */
class EnumeratedIP : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(EnumeratedIP, "EnumeratedIP")
public:
	EnumeratedIP() { m_IPstring = ""; m_next = nullptr; m_IP = 0; m_interfaceClass = LAN_INTERFACE_UNKNOWN; }

	// Access functions
	AsciiString getIPstring() { return m_IPstring; }
	void setIPstring( AsciiString name ) { m_IPstring = name; }
	UnsignedInt getIP() { return m_IP; }
	void setIP( UnsignedInt IP ) { m_IP = IP; }
	EnumeratedIP *getNext() { return m_next; }
	void setNext( EnumeratedIP *next ) { m_next = next; }
	// GeneralsX @bugfix Android 19/10/2026 Interface the address belongs to (empty on Windows).
	AsciiString getInterfaceName() { return m_interfaceName; }
	void setInterfaceName( AsciiString name ) { m_interfaceName = name; }
	Int getInterfaceClass() { return m_interfaceClass; }
	void setInterfaceClass( Int interfaceClass ) { m_interfaceClass = interfaceClass; }

protected:
	AsciiString m_IPstring;
	UnsignedInt m_IP;
	EnumeratedIP *m_next;
	AsciiString m_interfaceName;
	Int m_interfaceClass;
};
EMPTY_DTOR(EnumeratedIP)


/**
 * The IPEnumeration class is used to obtain a list of IP addresses on the
 * local machine.
 */
class IPEnumeration
{
public:

	IPEnumeration();
	~IPEnumeration();

	EnumeratedIP * getAddresses();		///< Return a linked list of local IP addresses
	AsciiString getMachineName();			///< Return the Network Neighborhood machine name

	// GeneralsX @bugfix Android 19/10/2026 The address to use given the one saved in Options.ini:
	// the saved one while it still exists, unless it sits on a cellular/virtual interface while a
	// Wi-Fi, VPN or Ethernet one is available; otherwise the first (most suitable) address.
	// Returns 0 when there is no address at all.
	UnsignedInt getPreferredIP( UnsignedInt savedIP );
	EnumeratedIP * findIP( UnsignedInt ip );	///< The enumerated entry for ip, or nullptr

	// GeneralsX @bugfix Android 19/10/2026 True if ip is assigned to one of this machine's active,
	// non-loopback interfaces (POSIX). The LAN lobby socket listens on every interface there, so
	// its own broadcasts can come back from any of them. Always FALSE on Windows.
	static Bool isLocalInterfaceAddress( UnsignedInt ip );

protected:
	void addNewIP( UnsignedByte a, UnsignedByte b, UnsignedByte c, UnsignedByte d );
	// GeneralsX @bugfix Android 19/10/2026 Add at the end of the list (the POSIX list is built in order of preference).
	void appendNewIP( UnsignedInt ip, const char *interfaceName, Int interfaceClass );

	EnumeratedIP *m_IPlist;
	Bool m_isWinsockInitialized;
};
