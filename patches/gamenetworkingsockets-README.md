# Exclusive Windows UDP binding

`gamenetworkingsockets-exclusive-bind.patch` extends pinned GameNetworkingSockets
commit `505c697d0abef5da2ff3be35aa4ea3687597c3e9` (v1.4.1). Its raw UDP socket
setup does not request exclusive Windows binding. A second listener can otherwise
overlap an existing listener through the IPv6/IPv4 fallback, rather than report
the occupied port to the game.

The patch sets `SO_EXCLUSIVEADDRUSE` before `bind` on Windows. Failure closes the
socket and follows the library's existing initialization error path. Linux socket
behavior is unchanged. The setting applies to the actual transport socket, so
it protects port ownership across processes as well as embedded server threads.
See [Microsoft's socket option documentation](https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse).

CMake applies the patch after populating the pinned sources, including cached
and offline builds. `tools/patch_gamenetworkingsockets.py` checks for an already
applied patch before applying it; an incompatible upstream source fails
configuration. A preinstalled transport library must provide equivalent binding
behavior. Package licensing continues to include GameNetworkingSockets' upstream
license.

`multiplayer_game_sessions` checks same-process rejection, continued operation
of the original host and reuse after shutdown. `multiplayer_game_modes` also
checks an occupied port from a second game process. Both run in Windows CI.
