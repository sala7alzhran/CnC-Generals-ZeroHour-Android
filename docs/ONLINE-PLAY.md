# Playing together: plan

Every player imports their own copy of Zero Hour, so this is only about networking. The engine
is lockstep. Every player simulates the whole game and only orders travel, so every player must
run the same build on the same data.

## Where the code stands

The engine is GeneralsX as of early July 2026.

- **LAN lobby.** It uses UDP port 8086 (`LANAPI.cpp`). Hosts announce themselves to each
  interface's broadcast address every 10 s and are dropped after 20 s of silence.
- **In-game traffic** is unicast on port 8088, bound to the chosen local IP.
- **Packets** carry the player's IP inside them, so there is no NAT tolerance. The address each
  player picks must be one the others can reach.
- **Direct Connect** joins a host by IP, using the same unicast path.
- **GameSpy** online is dead (the master servers are gone).

## Problems on phones, and the fixes

| Problem | Effect | Fix | Status |
|---|---|---|---|
| The lobby socket is bound to one IP. On Android and Linux it then never receives broadcasts. | Phones never see each other's games. | Bind to `INADDR_ANY` on POSIX (GeneralsX #201). | Step 1 |
| The local IP is "the lowest number". Phones have Wi-Fi (`wlan0` 192.168.x), cellular (`rmnet*` 10.x / 100.64.x), sometimes a hotspot (`ap0`/`swlan0`) and VPNs (`tun*`). | The test phone picked its cellular 10.x address for LAN and saved it to Options.ini. Joining fails. | Prefer Wi-Fi/hotspot, then VPN, then Ethernet. Never pick cellular when Wi-Fi exists. Override a saved non-Wi-Fi IP. | Step 1 |
| Wi-Fi power saving | Lag spikes | `WIFI_MODE_FULL_LOW_LATENCY` lock while the game is in front | Step 1 |
| Wi-Fi without internet (a router with no uplink, or a hotspot with mobile data off) | Android may route over cellular instead | Bind the sockets to the Wi-Fi network (`android_setsocknetwork`). Until then, test with mobile data off. | Later |
| Going to the background pauses the game | Lockstep stalls for everyone and may disconnect | Warn the player, or keep the network loop alive | Later |
| A player with modified data | Mismatch mid-game | Turn the INI checksum check on join back on | Later |

## Determinism

- **Phone vs phone, same APK:** same binary and same CPU architecture, so it should stay in sync.
- **Phone vs PC (GeneralsX):** only from the same source commit with the same math options.
  Upstream has fixed several cross-platform divergences (GameMath/fdlibm, GLM vs D3DX, Bezier),
  but there is no arm64-Android data point yet. Expect mismatches until tested.
  - Android-specific risks: plain `char` is unsigned on arm64 Android, and INI floats are parsed
    via `strtod`.
- **Phone vs retail PC:** no.

## Internet play options

1. **Virtual LAN apps** (ZeroTier, Tailscale) plus **Direct Connect** by IP.
   - Works with no server of ours once the game picks the VPN address (step 1 allows `tun*`).
   - The lobby list stays empty: Tailscale has no broadcast, and ZeroTier on Android currently
     drops broadcasts sent from the phone.
   - Fiddly for non-technical players. This is the stop-gap for enthusiasts.
2. **Our own relay**, recommended.
   - A small UDP server gives each player in a "room" a virtual 10.99.0.x address and forwards
     the game's own LAN packets: unicast to one player, broadcast to the whole room.
   - Phones only send outbound UDP, so it works behind mobile carriers' NAT.
   - Players type a short room code. There is no extra app and no account.
   - Effort: about 300 lines of server and 500–800 lines in `udp.cpp`/`Transport`/IP enumeration.
     Roughly 1–2 weeks with testing.
   - Cost: a few KB/s per player on a $4–6/month VPS near the players.
3. **Generals Online protocol (NGMP)**, merged in GeneralsX as #260.
   - It has a lobby, P2P through Valve's GameNetworkingSockets, and STUN plus TURN relays.
   - This is the long-term path for cross-play with PCs, but it needs this port rebased onto
     current GeneralsX, GameNetworkingSockets, OpenSSL and curl for arm64, and the operators'
     agreement.

## Steps

1. **Same Wi-Fi.** Lobby discovery, Wi-Fi IP selection and the Wi-Fi lock (step 1 above).
   - Test with one phone plus a PC listener (`lan_listen.py`): the phone's announcements must
     arrive from its Wi-Fi address every 10 s.
   - Then test with two phones on a router, on a phone hotspot, and with mobile data on and off.
2. **Robustness.**
   - Bind to the Wi-Fi network.
   - Handle the background/pause case.
   - Turn the checksum check back on.
   - Fix the default player name, which reads "localhost".
3. **Internet.** A room-code relay (option 2). Document ZeroTier/Tailscale plus Direct Connect
   meanwhile.
4. **Cross-play with PCs.** Rebase onto current GeneralsX, prove sync phone↔PC, then NGMP.
