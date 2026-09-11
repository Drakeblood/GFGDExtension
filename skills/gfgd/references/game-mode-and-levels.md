# Game modes, levels, spawning and possession

## The game mode is a scene

A game mode declares what everything else is built from, so it is a **scene whose root is a
`GameModeBase`** — that is the only way those choices can be set in the inspector. A `Resource`
cannot do this job.

| Property | Type | Used for |
|---|---|---|
| `game_state_scene` | `PackedScene` | Root must be a `GameStateBase`. |
| `player_state_scene` | `PackedScene` | Root must be a `PlayerState`. |
| `player_controller_scene` | `PackedScene` | Root must be a `PlayerController`. |
| `default_pawn_scene` | `PackedScene` | The pawn every player gets. |
| `pawn_scene_overrides` | `PackedScene[]` | Indexed by local player index; falls back to `default_pawn_scene`. |
| `max_local_players` | `int` (1) | Local multiplayer. |
| `allow_press_to_join` | `bool` (false) | Local multiplayer. |
| `max_players` | `int` (8) | How many players the session accepts, across every machine. |

Point `Level.game_mode_override` at it, or leave it to
`application/game_framework/default_game_mode`.

A client never runs a game mode, but it does read these: both sides have to build the same player
state and the same pawn, and this is where that agreement is written down. **`World.get_game_mode()`
returns null on a client** — use `World.get_game_state()` for anything a client reads.

## Startup order

```
[Level] _enter_tree / _ready          the level is up first
GameState created and added
GameMode._init_game                   prepare; spawns and possesses the players
GameMode._ready                       everything live
Level._init_level
```

**Prepare in `_init_game`, play in `_ready`.**

`_init_game(world: World) -> bool` — returning `true` **suppresses the player spawn entirely**. That
is how a menu level runs with no player at all rather than with an empty one. Returning `false` lets
the framework log the local players in and possess their pawns on its own.

```gdscript
extends GameModeBase

func _init_game(world: World) -> bool:
	return true    # menu level: no players
```

The player list lives on `World`, not on the game mode:

```gdscript
get_world().get_first_player_controller()
get_world().get_player_controllers()
get_world().get_player_controller_at(index)
get_world().find_player_controller_by_player_id(player_id)
```

## Virtuals worth knowing

| Virtual | Returns | When |
|---|---|---|
| `_init_game(world)` | `bool` | Before the spawn. `true` suppresses it. |
| `_init_game_state()` | `void` | After the game state node exists. |
| `_pre_login(peer_id)` | `String` | A non-empty string **refuses** the login, and is the reason. |
| `_choose_player_start(controller)` | `Node` | Pick a start yourself. |
| `_get_pawn_scene_for(controller)` | `PackedScene` | Per-controller pawn choice, above `pawn_scene_overrides`. |
| `_can_join(device_slot)` | `bool` | Press-to-join gate — see `input.md`. |
| `_on_post_login(pc)` | `void` | A player controller has arrived. |
| `_on_logout(pc)` | `void` | A player controller is leaving. |

Signals for the same events, when a subclass is not what you want: `play_started`,
`player_restarted(player_controller, pawn)`, `player_logged_in(player_controller)`,
`player_logged_out(player_controller)`, `login_refused(peer_id, reason)`.

## Levels

`Level` is the playable scene root.

- `game_mode_override: PackedScene` — this level's game mode, above the project default.
- `_init_level(world)` — called **after** the game mode's `_ready`, so everything is live by then.

**Connecting to `player_restarted` from `_init_level` misses the first pawn.** That pawn is spawned
inside `_init_game`, which has already run by the time a level gets its hook, so the first
`player_restarted` is long gone. Take the pawn that is already there, and keep the signal for
respawns:

```gdscript
func _init_level(world: World) -> void:
	var game_mode := world.get_game_mode()
	if game_mode != null:
		game_mode.player_restarted.connect(_on_player_restarted)

	# The first one is already up.
	var controller := world.get_first_player_controller()
	if controller != null:
		_on_player_restarted(controller, controller.get_pawn())
```

`World.find_level()` locates it; `World.get_level()` returns the current one.

Changing level: `World.open_level(path)` standalone, `World.server_travel(path)` networked. On a
server `open_level` forwards to `server_travel`; a client is told it cannot change level on its own.

## Spawning and possession

The framework does this for you. It only needs to be told *what* to build:

1. `GameModeBase` instantiates `player_controller_scene` (or a plain `PlayerController`).
2. It instantiates the pawn scene — `_get_pawn_scene_for` > `pawn_scene_overrides[player_index]` >
   `default_pawn_scene`.
3. It places it at a `PlayerStart2D` / `PlayerStart3D` (`choose_player_start`, or your
   `_choose_player_start`; `find_player_start(tag)` looks one up by `player_start_tag`).
4. The controller possesses it.

**Player starts are claimed once each per level.** With more players than starts the framework warns
and reuses them; it cannot test whether a start is physically occupied.

To do it manually: `spawn_default_player()`, `restart_player(controller)`,
`restart_player_at(controller, start_spot)`, `restart_all_players()`.

## The Pawn node

`Pawn` is a **marker node placed inside** a pawn scene — it is not the pawn's root. It may be the
scene root or any descendant; **the framework finds it recursively and the first match wins**, so
two of them in one scene means the other is silently ignored.

`get_pawn_root()` returns the node the `Pawn` makes possessable — the actual `RigidBody2D`,
`CharacterBody3D` or whatever the scene is built around. Almost everything gameplay-side wants
`get_pawn_root()`, not the `Pawn` itself.

| Property | Default | Meaning |
|---|---|---|
| `camera_path` | `""` | The camera to make current on possession. Empty means search recursively. |
| `auto_manage_camera` | `true` | Set `false` when the level owns the camera, or the pawn steals the view. |
| `auto_possess_player` | `-1` (`AUTO_POSSESS_DISABLED`) | Auto-possess by local player index. |
| `replicate_transform` | `true` | Mirror the root's transform to other peers. |
| `player_id` | — | Server-assigned; the address other peers use. |

Virtuals: `_possessed(controller)`, `_unpossessed()`, `_setup_input_component(input_component)`,
`_get_replicated_properties() -> PackedStringArray`.
Signals: `possessed(controller)`, `unpossessed`.

Movement helpers, which work the same offline and online:

```gdscript
add_movement_input(world_direction: Vector3, scale: float = 1.0)
consume_movement_input_vector() -> Vector3     # reads and clears
get_pending_movement_input_vector() -> Vector3 # reads without clearing
```

In 2D, pack the axes into the `Vector3` and read back what you need — the API is `Vector3` in both
cases.

**A pawn built by hand is not mirrored.** Let the framework spawn it from a named scene, or it
exists only where it was made.
