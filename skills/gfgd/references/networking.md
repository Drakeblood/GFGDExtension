# Networking

## The same build is every role

Decided entirely by what you called:

| Call | `World.get_net_mode()` |
|---|---|
| nothing | `NET_MODE_STANDALONE` |
| `host_game(port, max_players := 32)` | `NET_MODE_LISTEN_SERVER` |
| `create_dedicated_server(port, max_players := 32)` | `NET_MODE_DEDICATED_SERVER` |
| `join_game(address, port)` | `NET_MODE_CLIENT` |

Each returns an `Error`. `disconnect_from_network()` tears the session down.

Predicates: `is_networked()`, `is_client()`, `is_listen_server()`, `is_dedicated_server()`,
`has_authority()`, `get_local_peer_id()`. `World.SERVER_PEER_ID` is `1`.

**`has_authority()` is true in every mode but client, and true with no peer at all** — which is what
makes a rule written for a server also correct in a game that never touches the network. Write
`if not has_authority(): return` and the single-player build takes the same path.

`World` signals: `connected_to_server`, `connection_failed`, `server_disconnected`,
`peer_connected(peer_id)`, `peer_disconnected(peer_id)`, `net_mode_changed(net_mode)`,
`level_loaded(level)`.

## What exists where

| | Server | All clients | Owning client only |
|---|:---:|:---:|:---:|
| `GameMode` | yes | | |
| `GameState`, `PlayerState` | yes | yes | |
| Pawn | yes | yes | |
| `PlayerController` | yes | | yes |
| `GameInstance`, `LocalPlayer`, `PlayerInput`, `InputRouter` | local to each peer | | |

A client holds only **its own** `PlayerController`. To reach another player from a client, go
through `GameState.get_player_array()` / the `PlayerState`s.

## Joining and travelling

```gdscript
world.host_game(7777)                 # or create_dedicated_server(7777)
world.server_travel("res://levels/arena.tscn")
```

`server_travel` tells every client to load the level, then loads it here. A client that connects
later is told the same thing on connect. Either way the client loads, reports back, and only then is
it handed the world as it stands and logged in — which is why **a late join and a level change are
the same case**, and why nothing is ever sent to a peer that could not receive it.

`open_level` is the standalone form; on a server it forwards to `server_travel`, and a client is
told it cannot change level on its own.

`NetDriver.is_peer_ready(peer_id)` and `get_ready_peers()` report who has finished loading.

## Replication

The framework mirrors what it creates, so **a project places no `MultiplayerSpawner` and no
`MultiplayerSynchronizer` of its own.** Player states, pawns and player controllers are spawned
through `NetDriver`; properties are kept in step by synchronizers the framework attaches from
`_ready` at matching paths on every peer.

For your own state, return the property names from `_get_replicated_properties()` on a
`PlayerState`, `GameStateBase` or `Pawn` subclass:

```gdscript
extends PlayerState

var kills := 0

func _get_replicated_properties() -> PackedStringArray:
	return PackedStringArray(["kills"])
```

**Every scene a game mode names has to be its own file.** A scene built inline cannot be named to
another machine, so a client would have nothing to build. The framework warns when it finds one
while networked.

**A pawn built by hand is not mirrored.** Choose the scene through `default_pawn_scene`,
`pawn_scene_overrides` or `_get_pawn_scene_for`.

## Ownership, roles and input

**Ownership** is the chain a pawn is reached by: a pawn is owned by its controller, and a player
controller by the connection it was created for. It decides whether a remote call is honoured —
input sent for a controller the sender does not own is dropped.
`World.get_net_owner_peer(node)` reports it.

**Roles** say what a peer may do with a node, and are mirror images of each other:

| | On the server | On the owning client | On any other client |
|---|---|---|---|
| `get_local_role_for(node)` | `ROLE_AUTHORITY` | `ROLE_AUTONOMOUS_PROXY` | `ROLE_SIMULATED_PROXY` |

`World.get_remote_role_for(node)` gives the other side's view. `Pawn`, `Controller` and
`PlayerState` each expose `get_local_role()` / `get_remote_role()` / `has_authority()` directly.

**Authority is not ownership** — the server has authority over every pawn, including one a client
owns.

A client sends what its player is holding and the server writes it into that controller's
`PlayerInput`, so from the input component down the server runs exactly the code a local player
runs. `PlayerController.replicated_actions` (a `PackedStringArray`) selects which actions are sent.

**There is no client-side prediction and no rollback**: movement costs a round trip. A game that
needs a snappier feel adds prediction on top, and the pieces to do it with are already here.

## Game state and player state

`GameStateBase` is what every peer may know. Replicated members: `has_begun_play`,
`server_world_time`. Lookups: `get_player_array()`, `get_player_count()`,
`get_player_state_by_player_id(id)`, `get_player_state_by_index(i)`,
`get_player_state_by_unique_id(uid)`. `get_server_world_time_seconds()` is the shared clock.
Virtual `_init_game_state(world)`; signals `player_state_added`, `player_state_removed`,
`begun_play`.

`PlayerState` carries `player_name`, `player_id`, `player_index`, `unique_id`, `local`, `score`,
`spectator`, `a_bot`, `ping`; signals `player_name_changed`, `score_changed`.
