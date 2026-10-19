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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/networkutil.h"
#include "GameClient/ClientInstance.h"

#ifndef _WIN32
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <string.h>
#endif

#ifndef _WIN32
static Bool InterfaceNameStartsWith(const char *name, const char *prefix)
{
	return strncmp(name, prefix, strlen(prefix)) == 0;
}

// GeneralsX @bugfix Android 19/10/2026 From upstream GeneralsX #307 (BenderAI,
// https://github.com/fbraz3/GeneralsX/pull/307): detect Docker user-defined bridges, which are named
// "br-" followed by a 12 character hexadecimal network id (e.g. br-ffd9ee17a8f4).
static Bool IsDockerUserBridgeName(const char *name)
{
	if (!InterfaceNameStartsWith(name, "br-"))
	{
		return FALSE;
	}

	const char *suffix = name + 3;
	Int digits = 0;
	for (; suffix[digits] != '\0'; ++digits)
	{
		const char c = suffix[digits];
		const Bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!isHex)
		{
			return FALSE;
		}
	}

	return digits == 12;
}

// GeneralsX @bugfix Android 19/10/2026 Interfaces that never carry a LAN game: container/VM bridges and
// Apple's private links (upstream GeneralsX #201, https://github.com/fbraz3/GeneralsX/pull/201, and #307).
// They are left out of the list entirely.
static Bool IsIgnoredInterfaceName(const char *name)
{
	return InterfaceNameStartsWith(name, "docker")
		|| InterfaceNameStartsWith(name, "veth")
		|| InterfaceNameStartsWith(name, "virbr")
		|| InterfaceNameStartsWith(name, "awdl")
		|| InterfaceNameStartsWith(name, "llw")
		|| InterfaceNameStartsWith(name, "utun")
		|| IsDockerUserBridgeName(name);
}

// GeneralsX @bugfix Android 19/10/2026 Classify an interface by name. Android names its Wi-Fi wlan0,
// its hotspot wlan1/ap0/swlan0/softap0, cellular data rmnet*/ccmni* (plus a v4-rmnet* IPv4-over-IPv6
// translator), and VPN apps (ZeroTier, Tailscale) get a tun*.
static Int ClassifyInterfaceName(const char *name)
{
	if (InterfaceNameStartsWith(name, "wl") || InterfaceNameStartsWith(name, "ap")
		|| InterfaceNameStartsWith(name, "swlan") || InterfaceNameStartsWith(name, "softap"))
	{
		return LAN_INTERFACE_WIFI;
	}
	if (InterfaceNameStartsWith(name, "tun") || InterfaceNameStartsWith(name, "zt")
		|| InterfaceNameStartsWith(name, "tailscale"))
	{
		return LAN_INTERFACE_VPN;
	}
	if (InterfaceNameStartsWith(name, "eth") || InterfaceNameStartsWith(name, "en"))
	{
		return LAN_INTERFACE_WIRED;
	}
	if (InterfaceNameStartsWith(name, "rmnet") || InterfaceNameStartsWith(name, "ccmni")
		|| InterfaceNameStartsWith(name, "v4-") || InterfaceNameStartsWith(name, "dummy")
		|| InterfaceNameStartsWith(name, "p2p") || InterfaceNameStartsWith(name, "ifb"))
	{
		return LAN_INTERFACE_AVOID;
	}
#if defined(__ANDROID__)
	// bond0 is an idle placeholder on phones; on a Linux server it can be the main NIC, so only here.
	if (InterfaceNameStartsWith(name, "bond"))
	{
		return LAN_INTERFACE_AVOID;
	}
#endif
	return LAN_INTERFACE_UNKNOWN;
}

// GeneralsX @bugfix Android 19/10/2026 Sort key for the enumerated list, lower first. On a phone the
// Wi-Fi/hotspot address is the LAN, then a VPN overlay; desktops keep wired Ethernet first (as
// upstream GeneralsX #346 does) and put overlays after unrecognised names such as a br0 host bridge.
static Int GetInterfaceClassRank(Int interfaceClass)
{
	switch (interfaceClass)
	{
#if defined(__ANDROID__)
		case LAN_INTERFACE_WIFI: return 0;
		case LAN_INTERFACE_VPN: return 1;
		case LAN_INTERFACE_WIRED: return 2;
		case LAN_INTERFACE_UNKNOWN: return 3;
#else
		case LAN_INTERFACE_WIRED: return 0;
		case LAN_INTERFACE_WIFI: return 1;
		case LAN_INTERFACE_UNKNOWN: return 2;
		case LAN_INTERFACE_VPN: return 3;
#endif
		default: return 4; // LAN_INTERFACE_AVOID
	}
}
#endif

// GeneralsX @bugfix Android 19/10/2026 Wi-Fi/hotspot, VPN and Ethernet addresses are the ones a LAN peer can reach.
static Bool IsPreferredInterfaceClass(Int interfaceClass)
{
	return interfaceClass == LAN_INTERFACE_WIFI || interfaceClass == LAN_INTERFACE_VPN || interfaceClass == LAN_INTERFACE_WIRED;
}

IPEnumeration::IPEnumeration()
{
	m_IPlist = nullptr;
	m_isWinsockInitialized = false;
}

IPEnumeration::~IPEnumeration()
{
	if (m_isWinsockInitialized)
	{
		WSACleanup();
		m_isWinsockInitialized = false;
	}

	EnumeratedIP *ip = m_IPlist;
	while (ip)
	{
		ip = ip->getNext();
		deleteInstance(m_IPlist);
		m_IPlist = ip;
	}
}

EnumeratedIP * IPEnumeration::getAddresses()
{
	if (m_IPlist)
		return m_IPlist;

	if (!m_isWinsockInitialized)
	{
		WORD verReq = MAKEWORD(2, 2);
		WSADATA wsadata;

		int err = WSAStartup(verReq, &wsadata);
		if (err != 0) {
			return nullptr;
		}

		if ((LOBYTE(wsadata.wVersion) != 2) || (HIBYTE(wsadata.wVersion) !=2)) {
			WSACleanup();
			return nullptr;
		}
		m_isWinsockInitialized = true;
	}

	// TheSuperHackers @feature Add one unique local host IP address for each multi client instance.
	if (rts::ClientInstance::isMultiInstance())
	{
		const UnsignedInt id = rts::ClientInstance::getInstanceId();
		addNewIP(
			127,
			(UnsignedByte)(id >> 16),
			(UnsignedByte)(id >> 8),
			(UnsignedByte)(id));
	}

#ifndef _WIN32
	// GeneralsX @bugfix BenderAI 31/03/2026 Enumerate active IPv4 interfaces on non-Windows (POSIX) platforms instead of hostname resolution.
	struct ifaddrs *ifaddr = nullptr;
	if (getifaddrs(&ifaddr) == 0)
	{
		// GeneralsX @bugfix Android 19/10/2026 Collect the usable addresses first, then list them in order
		// of interface suitability (see GetInterfaceClassRank) instead of by numeric value, so the first
		// entry, which the LAN lobby and Direct Connect fall back to, is the Wi-Fi/hotspot address on a
		// phone rather than the cellular 10.x one.
		struct Candidate
		{
			UnsignedInt ip;
			Int interfaceClass;
			Int rank;
			char name[IFNAMSIZ];
		};
		Candidate candidates[32];
		Int numCandidates = 0;

		for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == nullptr)
			{
				continue;
			}

			if (ifa->ifa_addr->sa_family != AF_INET)
			{
				continue;
			}

			if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0)
			{
				continue;
			}

			// GeneralsX @bugfix Android 19/10/2026 From upstream GeneralsX #307: require an active carrier (IFF_RUNNING).
			// Idle Docker/VM bridges stay administratively UP without a link.
			if ((ifa->ifa_flags & IFF_RUNNING) == 0)
			{
				continue;
			}

			const char *name = (ifa->ifa_name != nullptr) ? ifa->ifa_name : "";
			if (IsIgnoredInterfaceName(name))
			{
				continue;
			}

			const sockaddr_in *addr = reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr);
			// GeneralsX @bugfix BenderAI 31/03/2026 Use ntohl to convert from network byte order before extracting octets;
			// reading s_addr byte-by-byte on little-endian platforms reverses the IPv4 octets.
			const UnsignedInt hostAddr = ntohl(addr->sin_addr.s_addr);
			// GeneralsX @build GitHubCopilot 11/04/2026 Log POSIX interface candidates used for LAN IP selection.
			DEBUG_LOG(("IPEnumeration::getAddresses - interface=%s flags=0x%X ip=%d.%d.%d.%d",
				name, ifa->ifa_flags, PRINTF_IP_AS_4_INTS(hostAddr)));
			/* 			fprintf(stderr, "[LAN86] iface=%s flags=0x%X ip=%d.%d.%d.%d\n",
				(ifa->ifa_name != nullptr) ? ifa->ifa_name : "<unknown>", ifa->ifa_flags,
				PRINTF_IP_AS_4_INTS(hostAddr)); */
			if (numCandidates >= (Int)ARRAY_SIZE(candidates))
			{
				break;
			}
			Candidate &candidate = candidates[numCandidates++];
			candidate.ip = hostAddr;
			candidate.interfaceClass = ClassifyInterfaceName(name);
			candidate.rank = GetInterfaceClassRank(candidate.interfaceClass);
			strlcpy(candidate.name, name, sizeof(candidate.name));
		}
		freeifaddrs(ifaddr);

		// Insertion sort by (rank, address): a handful of entries, and equal ranks keep the old numeric order.
		for (Int i = 1; i < numCandidates; ++i)
		{
			const Candidate key = candidates[i];
			Int j = i - 1;
			while (j >= 0 && (candidates[j].rank > key.rank || (candidates[j].rank == key.rank && candidates[j].ip > key.ip)))
			{
				candidates[j + 1] = candidates[j];
				--j;
			}
			candidates[j + 1] = key;
		}

		for (Int i = 0; i < numCandidates; ++i)
		{
			appendNewIP(candidates[i].ip, candidates[i].name, candidates[i].interfaceClass);
		}

		if (m_IPlist)
		{
			return m_IPlist;
		}
	}
	else
	{
		DEBUG_LOG(("Failed call to getifaddrs; errno returned %d", errno));
	}
#endif

	// get the local machine's host name
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)))
	{
		DEBUG_LOG(("Failed call to gethostname; WSAGetLastError returned %d", WSAGetLastError()));
		return m_IPlist;
	}
	DEBUG_LOG(("Hostname is '%s'", hostname));

	// get host information from the host name
	hostent* hostEnt = gethostbyname(hostname);
	if (hostEnt == nullptr)
	{
		DEBUG_LOG(("Failed call to gethostbyname; WSAGetLastError returned %d", WSAGetLastError()));
		return m_IPlist;
	}

	// sanity-check the length of the IP adress
	if (hostEnt->h_length != 4)
	{
		DEBUG_LOG(("gethostbyname returns oddly-sized IP addresses!"));
		return m_IPlist;
	}

	// construct a list of addresses
	int numAddresses = 0;
	char *entry;
	while ( (entry = hostEnt->h_addr_list[numAddresses++]) != nullptr )
	{
		addNewIP(
			(UnsignedByte)entry[0],
			(UnsignedByte)entry[1],
			(UnsignedByte)entry[2],
			(UnsignedByte)entry[3]);
	}

	return m_IPlist;
}

void IPEnumeration::addNewIP( UnsignedByte a, UnsignedByte b, UnsignedByte c, UnsignedByte d )
{
	UnsignedInt ip = AssembleIp(a, b, c, d);
	for (EnumeratedIP *current = m_IPlist; current != nullptr; current = current->getNext())
	{
		if (current->getIP() == ip)
		{
			/* 			fprintf(stderr, "[LAN86] addNewIP duplicate-skip %d.%d.%d.%d\n", (int)a, (int)b, (int)c, (int)d); */
			return;
		}
	}

	EnumeratedIP *newIP = newInstance(EnumeratedIP);

	AsciiString str;
	str.format("%d.%d.%d.%d", (int)a, (int)b, (int)c, (int)d);

	newIP->setIPstring(str);
	newIP->setIP(ip);

	DEBUG_LOG(("IP: 0x%8.8X (%s)", ip, str.str()));
	/* 	fprintf(stderr, "[LAN86] addNewIP accepted %s numeric=0x%8.8X\n", str.str(), ip); */

	// Add the IP to the list in ascending order
	if (!m_IPlist)
	{
		m_IPlist = newIP;
		newIP->setNext(nullptr);
	}
	else
	{
		if (newIP->getIP() < m_IPlist->getIP())
		{
			newIP->setNext(m_IPlist);
			m_IPlist = newIP;
		}
		else
		{
			EnumeratedIP *p = m_IPlist;
			while (p->getNext() && p->getNext()->getIP() < newIP->getIP())
			{
				p = p->getNext();
			}
			newIP->setNext(p->getNext());
			p->setNext(newIP);
		}
	}
}

void IPEnumeration::appendNewIP( UnsignedInt ip, const char *interfaceName, Int interfaceClass )
{
	EnumeratedIP *last = nullptr;
	for (EnumeratedIP *current = m_IPlist; current != nullptr; current = current->getNext())
	{
		if (current->getIP() == ip)
		{
			return;
		}
		last = current;
	}

	EnumeratedIP *newIP = newInstance(EnumeratedIP);

	AsciiString str;
	str.format("%d.%d.%d.%d", PRINTF_IP_AS_4_INTS(ip));

	newIP->setIPstring(str);
	newIP->setIP(ip);
	newIP->setInterfaceName(AsciiString(interfaceName != nullptr ? interfaceName : ""));
	newIP->setInterfaceClass(interfaceClass);
	newIP->setNext(nullptr);

	DEBUG_LOG(("IP: 0x%8.8X (%s) on %s", ip, str.str(), interfaceName != nullptr ? interfaceName : "?"));

	if (last == nullptr)
	{
		m_IPlist = newIP;
	}
	else
	{
		last->setNext(newIP);
	}
}

EnumeratedIP * IPEnumeration::findIP( UnsignedInt ip )
{
	for (EnumeratedIP *current = getAddresses(); current != nullptr; current = current->getNext())
	{
		if (current->getIP() == ip)
		{
			return current;
		}
	}
	return nullptr;
}

UnsignedInt IPEnumeration::getPreferredIP( UnsignedInt savedIP )
{
	EnumeratedIP *list = getAddresses();
	if (list == nullptr)
	{
		return 0;
	}

	EnumeratedIP *saved = (savedIP != 0) ? findIP(savedIP) : nullptr;
	if (saved == nullptr)
	{
		// The saved address is gone (cellular addresses change often): the list is in order of preference.
		return list->getIP();
	}

	// A loopback address is only ever saved on purpose (TheSuperHackers multi-instance testing).
	if (IsPreferredInterfaceClass(saved->getInterfaceClass()) || (savedIP >> 24) == 127)
	{
		return savedIP;
	}

#if defined(__ANDROID__)
	// A phone saves whatever address was first when Options was opened, often the cellular one.
	const Bool replaceSaved = TRUE;
#else
	// On a desktop an unrecognised name can be the real LAN (e.g. a br0 host bridge); only replace
	// addresses on interfaces known to be unsuitable.
	const Bool replaceSaved = (saved->getInterfaceClass() == LAN_INTERFACE_AVOID);
#endif
	if (replaceSaved)
	{
		for (EnumeratedIP *current = list; current != nullptr; current = current->getNext())
		{
			if (IsPreferredInterfaceClass(current->getInterfaceClass()))
			{
				return current->getIP();
			}
		}
	}

	return savedIP;
}

Bool IPEnumeration::isLocalInterfaceAddress( UnsignedInt ip )
{
	// Loopback is not treated as "ourselves": TheSuperHackers multi-instance testing gives each
	// game instance its own 127.x address.
	if (ip == 0 || (ip >> 24) == 127)
	{
		return FALSE;
	}

#ifdef _WIN32
	return FALSE;
#else
	// The LAN lobby asks for every received packet; re-read the interface table at most once a second.
	static UnsignedInt s_addresses[32];
	static Int s_numAddresses = 0;
	static UnsignedInt s_lastRefresh = 0;
	static Bool s_haveAddresses = FALSE;

	const UnsignedInt now = timeGetTime();
	if (!s_haveAddresses || now - s_lastRefresh > 1000)
	{
		s_haveAddresses = TRUE;
		s_lastRefresh = now;
		s_numAddresses = 0;

		struct ifaddrs *ifaddr = nullptr;
		if (getifaddrs(&ifaddr) == 0)
		{
			for (struct ifaddrs *ifa = ifaddr; ifa != nullptr && s_numAddresses < (Int)ARRAY_SIZE(s_addresses); ifa = ifa->ifa_next)
			{
				if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
				{
					continue;
				}
				if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0)
				{
					continue;
				}
				const sockaddr_in *addr = reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr);
				s_addresses[s_numAddresses++] = ntohl(addr->sin_addr.s_addr);
			}
			freeifaddrs(ifaddr);
		}
	}

	for (Int i = 0; i < s_numAddresses; ++i)
	{
		if (s_addresses[i] == ip)
		{
			return TRUE;
		}
	}
	return FALSE;
#endif
}

AsciiString IPEnumeration::getMachineName()
{
	if (!m_isWinsockInitialized)
	{
		WORD verReq = MAKEWORD(2, 2);
		WSADATA wsadata;

		int err = WSAStartup(verReq, &wsadata);
		if (err != 0) {
			return "";
		}

		if ((LOBYTE(wsadata.wVersion) != 2) || (HIBYTE(wsadata.wVersion) !=2)) {
			WSACleanup();
			return "";
		}
		m_isWinsockInitialized = true;
	}

#if defined(__ANDROID__)
	// GeneralsX @feature Android 20/10/2026 gethostname() is "localhost" on every phone, which made it
	// the default player name everywhere. ZerohourActivity publishes the phone's own name instead
	// (e.g. "POCO F7 Pro", already cut to the lobby's 12 characters).
	const char *deviceName = getenv("GENERALSX_DEVICE_NAME");
	if (deviceName != nullptr && deviceName[0] != '\0')
	{
		return AsciiString(deviceName);
	}
#endif

	// get the local machine's host name
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)))
	{
		DEBUG_LOG(("Failed call to gethostname; WSAGetLastError returned %d", WSAGetLastError()));
		return "";
	}

	return AsciiString(hostname);
}


