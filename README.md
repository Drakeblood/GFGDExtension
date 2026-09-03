# GFGD — Game Framework for Godot

A gameplay framework for **Godot 4.6+**, implemented in C++ as a GDExtension: game modes, controllers, possession, per-player input, an ability system — and one code path that runs the same game standalone, on a listen server, on a dedicated server and on a client.

## Architecture

```
root
 ├─ NetDriver              – connections, travel, and mirroring what the server creates
 ├─ GameState              – what every peer may know; exists on clients too
 │   └─ PlayerState…       – per-player name, id, score, ping
 ├─ Players
 │   └─ PlayerController…  – server: all of them; client: only its own
 ├─ Pawns
 │   └─ Pawn<id>           – the node a player drives
 │       ├─ Pawn           – makes the parent possessable, sets up input bindings
 │       ├─ Camera2D/3D    – made current on possession, where the player is sitting
 │       └─ AbilitySystemComponent – abilities, gameplay tags, effects, attributes
 ├─ InputRouter            – sends each raw event to the player owning that device
 ├─ Level                  – scene root node, may override the game mode
 │   └─ PlayerStart2D/3D…  – where restart_player places each pawn
 └─ GameMode               – per-level rules and default scenes; server only

Held by World, outside the scene tree:
 GameInstance             – application-lifetime object (created from a project setting)
  └─ LocalPlayer…         – one per human at this machine; survives a level change
      └─ PlayerInput      – that human's own action state, filtered by device
```

Everything under `root` is a real node; everything under `GameInstance` is a plain `Object` the framework owns and frees itself, deliberately outside the tree so it can outlive a level.

`World` is the main loop and the world: it holds the controller lists — `get_first_player_controller()` and `get_player_controllers()` — and creates the `NetDriver`, `InputRouter`, `GameState` and `GameMode`. The `GameMode` deliberately keeps no player list of its own: it is the server's object and does not exist on a client, while the world's list does. `GameInstance` is what outlives a level, which is why the `LocalPlayer`s live there.

Controllers, pawns and player states hang off fixed parents rather than off the level, because their node path is the address every peer refers to them by — and a level's node name is not something the framework gets to decide.

### Startup flow

1. `World::_initialize` creates the **GameInstance** from `application/game_framework/game_instance_script` and calls its `_on_init`.
2. On the first frame the **NetDriver**, the `Players` and `Pawns` containers and (unless this is a dedicated server) the **InputRouter** are created, and the current **Level** — the main scene root, which must be a `Level` — is found.
3. The **GameMode** scene is resolved: `Level.game_mode_override`, else `application/game_framework/default_game_mode`. It is instantiated on every peer, but only added to the tree on the server; a client reads its default scenes and nothing else.
4. The **GameState** is created from `GameModeBase.game_state_scene` and added to the root, before the game mode runs — so player states have somewhere to attach.
5. `GameMode.init_game` runs, then the game mode enters the tree. Unless `_init_game` returns `true` ("I handled spawning"), `restart_all_players()` logs in every `LocalPlayer`, which creates its `PlayerController` and `PlayerState`, spawns `default_pawn_scene`, places it at a `PlayerStart` and possesses its `Pawn`. A project that never touched local multiplayer has exactly one local player here, created implicitly.
6. On possession the `PlayerController` makes the pawn camera current and calls `Pawn._setup_input_component`, where you bind input actions.

The level is in the tree and started before any of this, so `_init_game` and everything it spawns see a world that is already live. The game mode's own `_ready` runs after `init_game`, which makes it the point where the level, the players and the game state are all up.

All hooks degrade gracefully — missing settings produce warnings and sensible fallbacks, never crashes.

## Project settings

| Setting | Meaning |
|---|---|
| `application/run/main_loop_type` | Must be `World` to enable the framework. |
| `application/game_framework/game_instance_script` | Script extending `GameInstance`. |
| `application/game_framework/default_game_mode` | A scene whose root is a `GameModeBase`. |
| `application/game_framework/default_port` | Port the demo menu and helpers default to. |
| `application/game_framework/gameplay_tag_tables` | `GameplayTagTable` resources declaring the project's gameplay tags. Merged in listed order; on a duplicate the first table wins. Parent tags are registered implicitly. |
| `application/game_framework/save_encryption_key` | Key for encrypted saves. Defaults to a key that ships with this source — replace it before release. |

A minimal `project.godot`:

```ini
[application]

run/main_loop_type="World"
game_framework/game_instance_script="res://scripts/game_instance.gd"
game_framework/default_game_mode="res://game_modes/default_game_mode.tscn"
```

### The game mode is a scene

A game mode declares what everything else is built from, so it is a scene rather than a bare script — that is the only way those choices can be set in the inspector:

| Property | Used for |
|---|---|
| `game_state_scene` | Root must be a `GameStateBase`. |
| `player_state_scene` | Root must be a `PlayerState`. |
| `player_controller_scene` | Root must be a `PlayerController`. |
| `default_pawn_scene` | The pawn every player gets. |
| `pawn_scene_overrides` | Indexed by local player index; falls back to `default_pawn_scene`. |
| `max_local_players`, `allow_press_to_join` | Local multiplayer. |
| `max_players` | How many players the session accepts, across every machine. |

A client never runs a game mode, but it does read these: both sides have to build the same player state and the same pawn, and this is where that agreement is written down. Any scene named here must be saved as its own file — a scene built inline cannot be named to another machine.

### Scene requirements

The pawn scene must contain a **`Pawn`** node — either as the scene root, or anywhere below it (the first one found in a recursive search wins). Without it the pawn spawns but cannot be possessed, and the framework warns instead of failing. A camera and an `AbilitySystemComponent` are optional siblings; the camera is made current on possession, on the machine the player is sitting at — set `Pawn.auto_manage_camera = false` for a game filmed by a fixed camera the level owns, or possession quietly steals the view from it.

## Networking

The same build is every role. Which one it is depends on what you called:

| Call | `get_net_mode()` |
|---|---|
| nothing | `NET_MODE_STANDALONE` |
| `host_game(port)` | `NET_MODE_LISTEN_SERVER` |
| `create_dedicated_server(port)` | `NET_MODE_DEDICATED_SERVER` |
| `join_game(address, port)` | `NET_MODE_CLIENT` |

`has_authority()` is true in every mode but the last, and true with no peer at all — which is what makes a rule written for a server also correct in a game that never touches the network. A dedicated server creates no local player, no input routing and no camera; it waits for machines to connect.

### What exists where

| | Server | All clients | Owning client only |
|---|:---:|:---:|:---:|
| `GameMode` | ✓ | | |
| `GameState`, `PlayerState` | ✓ | ✓ | |
| Pawn | ✓ | ✓ | |
| `PlayerController` | ✓ | | ✓ |
| `GameInstance`, `LocalPlayer`, `PlayerInput`, `InputRouter` | local to each peer | | |

### Joining and travelling

```gdscript
world.host_game(7777)                 # or create_dedicated_server(7777)
world.server_travel("res://levels/arena.tscn")
```

`server_travel` tells every client to load the level, then loads it here. A client that connects later is told the same thing on connect. Either way the client loads, reports back, and only then is it handed the world as it stands and logged in — which is why a late join and a level change are the same case, and why nothing is ever sent to a peer that could not receive it.

`open_level` is the standalone form; on a server it forwards to `server_travel`, and a client is told it cannot change level on its own.

`World` relays the connection signals: `connected_to_server`, `connection_failed`, `server_disconnected`, `peer_connected`, `peer_disconnected`, `net_mode_changed`, `level_loaded`.

### Replication

The framework mirrors what it creates, so a project does not place a single `MultiplayerSpawner` or `MultiplayerSynchronizer`:

- **Spawning** goes through `NetDriver` as explicit remote calls. Player states, pawns and player controllers are named after a server-assigned `player_id`, so the same player is at the same path on every peer.
- **Properties** are kept in step by synchronizers the framework attaches from `_ready`, on every peer at matching paths. `PlayerState` replicates its name, id, score, spectator flag and ping; `GameStateBase` replicates whether play has begun and the server clock; a pawn replicates its root's position and rotation while `replicate_transform` is on.
- **Your own properties**: return their names from `_get_replicated_properties()` on a `PlayerState`, `GameStateBase` or `Pawn` subclass and they join the same synchronizer.

```gdscript
extends PlayerState

var kills := 0

func _get_replicated_properties() -> PackedStringArray:
	return PackedStringArray(["kills"])
```

### Ownership and roles

Ownership is the chain a pawn is reached by: a pawn is owned by its controller, and a player controller by the connection it was created for. `get_owner_peer_id()` walks it. It is what decides whether a remote call is honoured — the server drops input sent for a controller the sender does not own, which is what stops one client from playing another's character.

Roles are what a peer may do with a node, and they are mirror images of each other:

| | `get_local_role()` | `get_remote_role()` |
|---|---|---|
| on the server | `ROLE_AUTHORITY` | `ROLE_AUTONOMOUS_PROXY` for a player's own, else `ROLE_SIMULATED_PROXY` |
| on the owning client | `ROLE_AUTONOMOUS_PROXY` | `ROLE_AUTHORITY` |
| on another client | `ROLE_SIMULATED_PROXY` | `ROLE_AUTHORITY` |

Authority is not ownership: the server has authority over every pawn, including one a client owns.

### Input across the network

A client sends what its player is holding — only the actions that changed, unreliably, because input is a stream of states and a lost packet is corrected by the next one. The server writes it into that controller's `PlayerInput`, and from the `InputComponent` down it runs exactly the code a local player runs. The pawn therefore moves on the server and its position comes back to everyone.

This is the whole of it: **there is no client-side prediction and no rollback.** Movement costs a round trip. That is the honest boundary of a minimal setup; a game that needs a snappier feel has to add prediction on top, and the pieces to do it with — roles, ownership, an authority check per pawn — are already here.

Not implemented: seamless travel, relevancy and priority beyond the visibility filter a synchronizer carries, a spectator pawn, and voice.

## Local multiplayer

Godot's `Input` singleton merges every device into one state, so two people cannot share it. GFGD splits it: a **`LocalPlayer`** is one human at this machine and owns a **`PlayerInput`** holding their own action state, while a single **`InputRouter`** at the scene root hands each raw event to whoever owns the device it came from. `InputComponent` then reads that player's state instead of the global singleton, so a pawn only ever sees its own pad.

**A single player project is unaffected.** The first local player is created holding `PlayerInput.DEVICE_SLOT_ALL`, and in that state `PlayerInput` forwards every query straight to `Input` — same behaviour, same code path.

A device slot is a plain `int`: a joypad index, `DEVICE_SLOT_KEYBOARD_MOUSE`, `DEVICE_SLOT_ALL`, or `DEVICE_SLOT_NONE`. One player may own several. Events are classified by event class rather than by `InputEvent.device`, because a synthesized `InputEventAction` reports device `0` and would collide with joypad 0.

Two pads with the keyboard ignored, using `max_local_players = 2` and `allow_press_to_join = true`:

```gdscript
func _init_game(world: World) -> bool:
	var pads: Array = Input.get_connected_joypads()
	# Narrow player 0 off the DEVICE_SLOT_ALL wildcard, or it swallows the events
	# the second pad needs to join with.
	world.get_local_player(0).device_slots = PackedInt32Array([pads[0]])
	if pads.size() > 1:
		world.create_local_player(pads[1])
	return false

func _can_join(device_slot: int) -> bool:
	return device_slot != PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE
```

### Touch input

`PlayerVirtualJoystick` and `PlayerTouchButton` are `Control`s that write to a **`PlayerInput`** rather than to the global `Input` singleton, so a pawn reads them through its `InputComponent` like any other action.

Godot ships `VirtualJoystick` and `TouchScreenButton`, and for a single player game those are the better choice — they are themed, and a `PlayerInput` in passthrough forwards to `Input` anyway. These two exist for what the engine's cannot do: address one player out of several (`player_index`), and, on the stick, hold an action for as long as a finger is down (`press_action`), so a single gesture can mean both "move" and "hold".

```gdscript
$Joystick.action_left = &"move_left"
$Joystick.action_right = &"move_right"
$Joystick.action_up = &"move_up"
$Joystick.action_down = &"move_down"
$Joystick.press_action = &"grab"      # held while the finger is on the stick
```

Both track their finger by index in `_input` rather than through GUI routing, which hands a second finger to the same `Control` and loses one of the two. Neither marks events handled, so the UI above keeps working; a HUD strip stays clear simply by giving the stick a rect that does not cover it. Hiding either node releases whatever it holds, so a stick hidden mid-drag cannot leave a pawn flying.

Split screen is deliberately left to the project: set `LocalPlayer.viewport_override` and the framework points the right camera at the right viewport, but it does not build the layout.

## Gameplay Ability System

- **GameplayTag / GameplayTagContainer** — hierarchical `StringName` tags; `"A.B.C"` matches the parent query `"A.B"`. Containers serialize as a `PackedStringArray` (`tags`) and resolve through the `GameplayTagsManager` singleton.
- **GameplayTagTable** (Resource) — a `tag name -> description` dictionary; the authoring source for tags. Split them across as many tables as you like (per feature, or shipped with an addon) and list each one in `gameplay_tag_tables`. `GameplayTagsManager` flattens them into a single lookup and tracks where every tag came from: `get_tag_source_kind()` reports `TAG_SOURCE_TABLE` (declared), `TAG_SOURCE_IMPLICIT` (a parent implied by a longer tag) or `TAG_SOURCE_RUNTIME` (never declared — `request_tag()` conjured it, which usually means a typo or a stale asset), with `get_tag_source_path()` naming the declaring table.
- **GameplayAbility** (Resource) — identified by `ability_name`, optionally bound to an input action via `input_action_name`. Tag gating: `activation_required_tags`, `activation_blocked_tags`, `activation_owned_tags`, `block/cancel_abilities_with_tag`. Script hooks: `_on_give_ability`, `_can_activate_ability`, `_activate_ability`, `_end_ability`, `_input_pressed/_released`.
- **GameplayEffect** (Resource) — `INSTANT` (mutates base values), `HAS_DURATION`/`INFINITE` (aggregate onto current values, grant `granted_tags`, optionally periodic via `period`). Carries `AttributeModifier`s (`ADD`, `MULTIPLY`, `OVERRIDE`) plus application tag requirements.
- **AttributeSet** (Resource) — a `name -> base value` dictionary; current values are computed as `(base + Σ add) × Π multiply` (an `OVERRIDE` wins). Hooks `_pre_attribute_change` (clamping) / `_post_attribute_change`, signals `attribute_changed`, `base_value_changed`.
- **AbilitySystemComponent** (Node) — give/activate/cancel abilities, `apply_gameplay_effect_to_self`, owned tag counting, exported `startup_abilities`, `startup_effects` and `attribute_set` (duplicated at runtime). Signals: `ability_activated`, `ability_ended`, `owned_tag_changed`, `gameplay_effect_applied/removed`, `attribute_changed`.

The ability system is not replicated. In a networked game, activate abilities where the pawn is authoritative and let the results — attributes, tags — reach clients through whatever you replicate on the pawn or its player state.

## Messaging

`GameplayMessageRouter` is a fire-and-forget bus for announcements with no natural owner — a hit landed, a wave started, the run ended. Channels are gameplay tag names, so a listener registered on `Fx` with `MATCH_PARTIAL` hears `Fx.Impact` and `Fx.Text` alike.

```gdscript
GameplayMessageRouter.get_singleton().register_listener(&"Fx.Impact", _on_impact)

func _on_impact(_channel: StringName, payload: Variant) -> void:
	burst(payload.position, payload.strength)
```

A listener whose object is freed is dropped on the next broadcast, which is what makes this usable from nodes that come and go with a level. Prefer a plain signal whenever the emitter and the listener can see each other; the router earns its place when they cannot — UI built before the level exists, for one. It is local to one peer.

## Pooling

`NodePool` recycles nodes for anything spawned by the hundred. It pools by taking the node **out of the tree** rather than by disabling it: a node outside the tree does not exist for the physics server, so it cannot collide, costs no broadphase budget, and cannot be hit by accident while "dead".

```gdscript
var bullet: Node = $BulletPool.acquire()   # scene or factory, prewarm, container
...
$BulletPool.release(bullet)                # safe inside a physics callback
```

Pooled nodes get `_on_acquired()` / `_on_released()` called if they define them — by name, because a pooled node is a `RigidBody2D` one moment and an `Area2D` the next.

## Save games

`ProjectStatics.save_game/load_game` serialize a `SaveGame` resource to `user://saves/` (optionally encrypted). By default every script variable is serialized to JSON; override `_to_json` / `_from_json` for custom formats.

Encryption uses `application/game_framework/save_encryption_key`. It defaults to the key compiled into this extension, which is public — a released game should set its own. The framework warns once per run while the default is still in use.

## The demo project

`project/` is a working example of every mode. From the menu: **New Game** (single player, ability demo), **Local Co-op** (two pads), **Host** and **Join**. From the command line, after `--`:

```sh
godot --path project -- --server --port 7777          # dedicated server
godot --path project -- --host                        # listen server, playing along
godot --path project -- --join 127.0.0.1              # client
godot --path project -- --level res://levels/test_level.tscn   # skip the menu
```

`--auto-move` drives a pawn without a keyboard and `--travel-after N` makes the server change level again, both so a run with no window still shows whether the network did its job.

## Building

Requirements: [SCons](https://scons.org/), a C++17 compiler, and the `godot-cpp` submodule:

```sh
git submodule update --init --recursive
```

**One SCons invocation builds exactly one library**, for one `platform` x `arch` x `target`. Both default to the host, so a bare `scons target=template_debug` on Windows gives you Windows x86_64 and nothing else — every other platform has to be asked for by name.

### The three targets

| | `editor` | `template_debug` | `template_release` |
|---|:---:|:---:|:---:|
| `TOOLS_ENABLED` | ✓ | | |
| `DEBUG_ENABLED` | ✓ | ✓ | |
| optimisation | `speed_trace` | `speed_trace` | `speed` |
| class reference compiled in | ✓ | ✓ | |
| loaded by | the editor | an export with debug | a release export |

`TOOLS_ENABLED` is what gates the gameplay tag editor, so a `template_debug` library dropped into an editor runs the game but leaves you no way to edit tags. And the editor binary loads the `editor` slot even when it is only running the game, which is why rebuilding just `template_debug` and pressing play appears to change nothing.

A desktop platform therefore wants all three; Android wants two, because nobody runs an editor there.

### Per platform

```sh
# Windows, on Windows
scons platform=windows arch=x86_64 target=editor
scons platform=windows arch=x86_64 target=template_debug
scons platform=windows arch=x86_64 target=template_release

# Linux, on Linux (add arch=arm64 for ARM machines)
scons platform=linux arch=x86_64 target=editor
scons platform=linux arch=x86_64 target=template_debug
scons platform=linux arch=x86_64 target=template_release

# macOS, on macOS - arch=universal is required, see below
scons platform=macos arch=universal target=editor
scons platform=macos arch=universal target=template_debug
scons platform=macos arch=universal target=template_release

# Android, from any host with the NDK - arm64 for devices, x86_64 for the emulator
scons platform=android arch=arm64  target=template_debug
scons platform=android arch=arm64  target=template_release
scons platform=android arch=x86_64 target=template_debug
scons platform=android arch=x86_64 target=template_release
```

Android needs `ANDROID_HOME` pointing at an SDK with the NDK installed.

macOS **must** be built with `arch=universal`. The build strips `.universal` from the file name, producing `libGFGD.macos.template_debug.dylib` — exactly what the manifest names. Built per-arch it would come out as `...template_debug.arm64.dylib`, which nothing looks for.

Windows, Linux and macOS each need their own host or a CI runner; there is no cross-compilation toolchain set up here. Android is the exception and cross-compiles from anything.

`precision=double` is only for a Godot built in double precision. The manifest has the slots, but a normal build never needs them.

The Godot API the bindings are generated from is pinned in `SConstruct` as `api_version = "4.7"`. Build with `api_version=4.6` to produce a library that also loads in a 4.6 engine — at the cost of whatever 4.7 added — and lower `compatibility_minimum` in `gfgd.gdextension` to match.

### Where it lands

The library is written to `bin/<platform>/` and copied into `project/addons/gfgdextension/bin/<platform>/`, where `project/addons/gfgdextension/gfgd.gdextension` (entry symbol `gfgd_library_init`) picks it up by platform, architecture, precision and target. Nothing is copied by hand. Dropping that one `addons/gfgdextension/` folder into another project is the whole install — so the binaries you want shipped are the ones that need to be in it.

A CMake build is also provided for IDE use and writes to the same place.
