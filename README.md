# GFGD — Game Framework for Godot

A gameplay framework for **Godot 4.6+**, implemented in C++ as a GDExtension: game modes, controllers, possession, per-player input, an ability system — and one code path that runs the same game standalone, on a listen server, on a dedicated server and on a client.

It is modelled on Unreal Engine: the Gameplay Framework (`GameMode`, `GameState`, `PlayerController`, `Pawn`, `PlayerState`, `LocalPlayer`, `GameInstance`), the Gameplay Ability System and the `CharacterMovementComponent`. The class names and the vocabulary are Unreal's on purpose, so anyone who knows them knows their way around here — but it is built for Godot rather than ported: scenes and nodes instead of actors, GDScript `await` instead of most ability tasks, metres instead of centimetres.

## Installing

Grab the zip from [Releases](https://github.com/Drakeblood/GFGDExtension/releases) and unpack it into your project, so it ends up at `addons/gfgdextension/`. Or copy [`update_gfgd.ps1`](update_gfgd.ps1) into your project root and run `pwsh ./update_gfgd.ps1` with Godot closed. It downloads the latest release (or the one you pass with `-Tag`) and replaces the addon. Since the addon can be restored that way, add `addons/gfgdextension/` and `addons/gfgdextension.old/` to the game project's `.gitignore` instead of committing it. The addon's own [README](project/addons/gfgdextension/README.md#install) has the rest of the setup.

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
 │       ├─ AbilitySystemComponent – abilities, gameplay tags, effects, attributes
 │       └─ CharacterMovementComponent – walking, falling, jumping (3D)
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

The same fact read from the other side is the one that costs integrators the most time: **everything inside the level scene has its `_ready` run before step 3.** A HUD, a camera, a spawner or a touch control sitting in the level cannot see the game mode, a pawn or a controller from `_ready` — `World::get_game_mode()` returns null, the ordinary null guard swallows it, and the node quietly never finds what it wanted. `Level::_init_level` (step 6, after everything) is the hook for handing references out; resolving them on first use is better still, because it also survives a respawn.

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
- **Properties** are kept in step by a `DeltaReplicator` the framework attaches from `_ready` as a child named `Replication`, on every peer at the same path. `PlayerState` replicates its name, id, index, score, spectator and bot flags and ping; `GameStateBase` whether play has begun and the server clock. These are **push-based**: their setters mark them changed, so nothing compares them every frame. A pawn's root position and rotation (while `replicate_transform` is on) and the movement mode stay on a `MultiplayerSynchronizer`: they change every frame, and client prediction reads them on that path.
- **Your own properties**: return their names from `_get_replicated_properties()` (or `_get_delta_replicated_properties()` — both now feed the same replicator) on a `PlayerState`, `GameStateBase` or `Pawn` subclass.

```gdscript
extends PlayerState

var kills := 0
var inventory_log: Array[Dictionary] = []

func _get_replicated_properties() -> PackedStringArray:
	return PackedStringArray(["kills", "inventory_log:id"])
```

Script properties are compared every physics frame against the last state the replicator recorded, deeply, so in-place edits count; add `|push` to compare one only where `mark_delta_dirty()` marks it. Whatever changed in one frame — a scalar and the list it belongs with — leaves in the same patch and arrives together. There is no per-node size limit any more (the synchronizer path needed all of a node's values in one ~1350-byte packet). Only the change is sent — the changed elements of an array, the changed and removed keys of a dictionary, recursively — and nothing while nothing changes:

- **Numbered states, acknowledged per peer.** Every `check_interval` (every physics frame for the framework's `Replication`, 0.1 s by default for a standalone node) the server compares the values with its last state, deeply, so in-place edits count, and records a new numbered state when something differs. Each client gets the patch from the state *it confirmed last* to the newest, unreliably, and acknowledges what it applied; without an acknowledgement the server re-sends — the patch from the confirmed state to whatever is newest by then, never the lost packet itself. A client that missed something catches up in one step and a bad link never queues stale data. `delivery = DELIVERY_RELIABLE` sends over a reliable RPC instead.
- **Keyed arrays.** Write a name as `"orders:id"` and the array's Dictionary elements are matched by their `id`: removing one from the middle sends its id, a moved one sends the key order, and clients get `element_added`, `element_removed` and `element_changed`. Without a key, elements are matched by index.
- **Push-based properties.** By default the server compares every property each check. Add `|push` (`"orders:id|push"`) and it compares that property only where the game marks a change — `mark_delta_dirty(&"orders")` for the whole property, `mark_delta_dirty(&"orders", order_id)` for one element of a keyed array. Unmarked elements are taken from the previous state without being compared or copied, so a 10 000-element list with one marked change costs one element. Debug builds compare everything once a second anyway and warn about a change nobody marked (and replicate it).
- Patches over `max_unreliable_bytes` (1200) go reliably even in the acknowledged mode — an unreliable packet over the MTU is split and lost whole if any piece is.
- A client asks for the full values when its replicator is ready, so a late joiner starts from the current state. `flush()` sends pending changes at once instead of at the next check.
- Values set on a client are not overwritten until the server's value changes again (the synchronizer used to re-send every frame); a client should not write replicated properties.

`tests/test_delta_patches.gd` checks the patches: one changed field in a list of forty orders is 132 bytes instead of 9.7 KB, the first of forty removed is 164 bytes keyed (5.2 KB by index), and thousands of random nested and keyed changes round-trip exactly.

```gdscript
extends GameStateBase

var orders: Array[Dictionary] = []

func _get_replicated_properties() -> PackedStringArray:
	return PackedStringArray(["orders:id"])   # keyed by each order's "id"
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

By default that is the whole of it, and movement costs a round trip. A pawn whose `CharacterMovementComponent` has **`client_prediction`** on skips it: the owning client simulates its own character on the tick the key goes down, sends the server its moves with tick numbers — each repeated in several packets, so a lost one costs nothing — and when the server's answer for a tick disagrees with what it predicted, it takes the server's state and replays every move since. With the same inputs the two machines agree exactly, so on a normal connection that correction only happens when the server did something the client could not know about. See *Client prediction* below.

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

## Character movement

`CharacterMovementComponent` gives a pawn walking, falling and jumping. Attach it beside the `Pawn`, on a root that is a `PhysicsBody3D` with a capsule; `updated_body_path` defaults to the parent and the modes register themselves.

Property names follow Unreal's `CharacterMovementComponent` — `max_walk_speed`, `ground_friction`, `braking_deceleration_walking`, `jump_velocity`, `air_control`, `walkable_floor_angle` — because that is the vocabulary people already have. **The values are metric**: Godot works in metres where Unreal works in centimetres, so `max_walk_speed` defaults to `6.0`, not `600`. Divide anything lifted from an Unreal project by 100.

Drive it with the `add_movement_input` the pawn already had, guarded by `has_authority()` the way the demo pawns are, and let the component consume it:

```gdscript
func _process(_delta: float) -> void:
	if _input == null or not has_authority():
		return
	var move := _input.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, 0.0, move.y))
```

### Modes, and why they are objects

How a character moves is a named `MovementMode` in a dictionary — `Walking`, `Falling`, `Null` — rather than a value in an enum. A mode is a `Resource`, so it can be authored inline, shared between pawns and subclassed in GDScript; putting your own under one of those names replaces the built-in one.

Each mode has two halves that are deliberately not the same function. `_generate_move` **proposes** motion: it is side effect free and touches nothing, so proposals can be blended. `_simulation_tick` **executes** it: sweeps, resolves collisions, writes the state. Unreal fuses the two inside each of its `Phys*` functions, and that is exactly why layering a dash on top of a walk there is painful.

### It does not use `move_and_slide`

`move_and_slide()` reads `velocity` off the body, derives its step from the engine's physics delta, and writes back to the node. Each of those alone would make it impossible to run the same tick twice from two different starting states — which is what a resimulation is, and therefore what client prediction needs. The solver is built on `PhysicsServer3D.body_test_motion` instead, which sweeps from a `Transform3D` handed to it without moving anything.

That is also why the simulation state lives in a `MovementState` object rather than in fields on the node, and why `MovementBackend` exists: `PredictedMovementBackend` calls `simulate_with_input()` once per unacknowledged input after a correction, without a line changing in any mode. The seam was there before prediction was, because it could not have been added afterwards.

Two things reach a client: the transform, through the `Pawn`'s synchronizer, and **`movement_mode`**, through the component's own. A position says where a pawn is, not whether it is walking or falling, and the second is the question an animation graph asks first — guessing it back out of vertical velocity is wrong at the top of a jump and wrong on a ramp. Unreal replicates the same field as `ACharacter::ReplicatedMovementMode`, but only to simulated proxies, because an autonomous proxy predicts its own; here the owner of a predicted character receives both and ignores them. `movement_mode_changed` fires on every peer.

### Client prediction

Set `client_prediction = true` on the component, gate movement input in the pawn script with `Pawn.wants_movement_input()` instead of `has_authority()`, and the owning client stops waiting a round trip to see its own character move. Anything else that changes movement has to travel with the input so both machines do it on the same tick: `jump()` and `crouch()` already do, and a game's own actions — a dash, a sprint — go in `MovementInput.custom_flags` through `add_custom_input_flags()`, and are acted on from the `custom_input_flags` signal, which fires inside the tick on the client, on the server and on every replay. The demo's online pawn predicts, dash included.

`project/tests/test_prediction.gd` runs a real server and client over ENet — once on a clean loopback, once at 50 ms each way with jitter and 5 % loss through a debug delay-and-drop queue — and checks that a scripted stretch of walking, jumping, dashing and hitting a wall produces no correction at all, and that a change the server makes is corrected once, by the right amount. `test_replay.gd` checks the rule under all of it: rolled back to any tick, the same inputs reproduce the same states exactly.

### Not in yet

Smoothing for simulated proxies (other players snap to each replicated transform), smoothing of a correction on the predicting client, clock synchronisation and speed-hack detection for predicted moves.

## Gameplay Ability System

- **GameplayTag / GameplayTagContainer** — hierarchical `StringName` tags; `"A.B.C"` matches the parent query `"A.B"`. Containers serialize as a `PackedStringArray` (`tags`) and resolve through the `GameplayTagsManager` singleton.
- **GameplayTagTable** (Resource) — a `tag name -> description` dictionary; the authoring source for tags. Split them across as many tables as you like (per feature, or shipped with an addon) and list each one in `gameplay_tag_tables`. `GameplayTagsManager` flattens them into a single lookup and tracks where every tag came from: `get_tag_source_kind()` reports `TAG_SOURCE_TABLE` (declared), `TAG_SOURCE_IMPLICIT` (a parent implied by a longer tag) or `TAG_SOURCE_RUNTIME` (never declared — `request_tag()` conjured it, which usually means a typo or a stale asset), with `get_tag_source_path()` naming the declaring table.
- **GameplayAbility** (Resource) — identified by `ability_name`, optionally bound to an input action via `input_action_name`. Tag gating: `activation_required_tags`, `activation_blocked_tags`, `activation_owned_tags`, `block/cancel_abilities_with_tag`. A `cost_effect` and a `cooldown_effect`, checked before activation and applied by `commit_ability()`; a level, carried into every spec it makes; a `net_execution_policy` — `LOCAL_ONLY`, `LOCAL_PREDICTED`, `SERVER_ONLY`. A refused activation says why (`get_last_failure_reason()`, `ability_activation_failed`). Script hooks: `_on_give_ability`, `_can_activate_ability`, `_activate_ability`, `_end_ability`, `_input_pressed/_released`, `_on_remove_ability`.
- **GameplayEffect** (Resource) — `INSTANT` (mutates base values), `HAS_DURATION`/`INFINITE` (aggregate onto current values, grant `granted_tags`, optionally periodic via `period`, ticking at once with `execute_period_on_application`). Carries `AttributeModifier`s (`ADD`, `MULTIPLY`, `OVERRIDE`) plus application tag requirements, aggregate stacking with a limit and two expiry rules, and `gameplay_cue_tags`.
- **Gameplay cues** — `GameplayCueTable` maps a cue tag to a `GameplayCueNotify` resource (called with the character) or a scene (instanced on the character while the cue lasts, or in the world at a hit's location); a tag without a handler uses its parent's. Cues fire on every peer; an ability's own `execute_gameplay_cue` keeps the predicting client from hearing its cue twice.
- **AbilityTask** (RefCounted) — what an ability `await`s: `wait_delay`, `wait_input_press/release`, `wait_gameplay_tag_added/removed`, `wait_attribute_change`, `wait_gameplay_event`, `wait_any`, `wait_all`, and `sync_target_data`, which carries the predicting client's value to the server's copy of the activation. Tasks die with their ability: the code after an `await` on one never runs once the ability has been cancelled. Subclass it in GDScript for your own.
- **AbilityAsync** (RefCounted) — the same waits from outside any ability, for a HUD or a quest: `AbilityAsync.wait_gameplay_tag_added/removed`, `wait_attribute_change`, `wait_gameplay_event`, `wait_gameplay_effect_applied`, each taking the target component and an optional owner node. The target keeps it alive; it ends when the target is freed, the owner leaves the tree, `end_action()` is called, or after one trigger with `only_trigger_once`. Subclass it in GDScript for your own.
- **GameplayEffectSpec** (RefCounted) — one application of an effect: its level, its source, set-by-caller values, free-form context. Applied to self or, with `apply_gameplay_effect_spec_to_target`, to someone else.
- **AttributeModifier** magnitudes — a flat value scaled by a level `Curve`, a snapshot of the source's or target's attribute, or a value the caller sets on the spec.
- **AttributeSet** (Resource) — a `name -> base value` dictionary; current values are computed as `(base + Σ add) × Π multiply` (an `OVERRIDE` wins). Hooks `_pre_attribute_change` (clamping) / `_post_attribute_change`, signals `attribute_changed`, `base_value_changed`.
- **AbilitySystemComponent** (Node) — give/activate/cancel abilities, apply effects to itself or a target, owned tag counting and loose tags, gameplay cues (`execute`/`add`/`remove`, fired on every peer), exported `startup_abilities`, `startup_effects`, `attribute_set` (duplicated at runtime) and `replication_mode`. Signals: `ability_given`, `ability_activated`, `ability_activation_failed`, `ability_ended`, `owned_tag_changed`, `gameplay_effect_applied/removed/stack_changed`, `attribute_changed`, `gameplay_cue`, `replicated_state_received`.

**It replicates.** The server's component is the truth and each client's a mirror: attributes, owned tags and gameplay cues reach every peer, active effects reach the owner or everyone (`replication_mode` `MIXED` or `FULL`), and the granted abilities reach the owner. The owner asks the server to activate an ability; a `LOCAL_PREDICTED` one it runs at once — its tags are there on the frame the button goes down — and the server runs it too, pays its cost, starts its cooldown and confirms, or refuses and the client's copy is cancelled. A `SERVER_ONLY` one runs on the server alone. A joining client is handed the whole state once. `tests/test_gas_replication.gd` checks all of it with a server, an owner and an observer in three processes; `tests/test_gas.gd` checks the rules on one machine.

## Inventory and equipment

Items a player carries, a quick bar of them, and what the active one puts on the character. Every piece is a separate component, so a game takes only what it needs: a chest is just an `InventoryManagerComponent`, armour is an `EquipmentManagerComponent.equip_item()` call with no quick bar involved.

- **InventoryItemDefinition** (Resource) — what an item is: `display_name`, `max_stack_size` and a list of **fragments**. Behaviour comes from the fragments, not from subclasses: `InventoryFragmentEquippableItem` (an `EquipmentDefinition` to put on), `InventoryFragmentQuickBarIcon` (icon, tint, the stat shown as a number), `InventoryFragmentSetStats` (starting stat tags), `InventoryFragmentPickup` (the item's world scene). A game's own fragment extends `InventoryItemFragment` in a script (`_on_instance_created` sets up new instances); `find_fragment_by_class()` takes a native class name, a `class_name` or the script.
- **InventoryItemInstance** (RefCounted) — one item somebody has: its definition, an `item_id` that is the same on the server and its clients and survives moving between inventories, stat tags (named counters — ammo, charges, durability) and free-form `data`.
- **InventoryManagerComponent** (Node) — a list of stacks. `add_item_definition` makes a new entry; `add_items` tops up existing stacks first; `consume_items` is all-or-nothing; `get_total_item_count_by_definition` counts units, not entries; `max_entries` caps it. Signals: `item_added`, `item_removed`, `stack_changed`, `changed`.
- **QuickBarComponent** (Node) — `num_slots` slots pointing into the inventory beside it, one active. The active item is put on through the pawn's equipment manager; selecting another slot takes it off first. `auto_slot_new_items` puts an incoming item with an icon or equipment into the first free slot; an item leaving the inventory leaves its slot. Under a `PlayerController` it outlives the pawn and puts the active item back on whenever a new pawn is possessed.
- **EquipmentDefinition** (Resource) — what putting something on does: `abilities_to_grant`, `effects_to_apply`, `granted_tags` (loose tags), `actors_to_spawn` (`EquipmentActorToSpawn`: a scene, `attach_to`, an `attach_bone` found on any `Skeleton3D` under the pawn, a transform) and an optional `instance_script`.
- **EquipmentManagerComponent** (Node, beside the `Pawn`) — `equip_item` / `unequip_item`. On the server it grants and takes back the abilities, effects and tags through the `AbilitySystemComponent` beside it; on every peer it spawns the scenes and runs the `EquipmentInstance` hooks `_on_equipped` / `_on_unequipped`. `EquipmentInstance.set_spawned_visible()` hides the scenes locally (a tool while both hands carry something).

**It replicates**, the same way as the ability system: the server's components are the truth and clients hold read-only mirrors that ask for the whole state from `_ready` and then get changes once per frame. The inventory and the quick bar go to the owner (`InventoryManagerComponent.replication_mode` can send a chest to everyone); equipment goes to everyone, so each peer sees the axe in a friend's hand. On the owning client `QuickBarComponent.set_active_slot_index()` and `swap_slots()` are requests the server checks and answers. Definitions travel as resource paths, so items that reach clients are saved `.tres` files. `tests/test_inventory.gd` checks the rules on one machine.

## Debug tools

A console, cheats, a gameplay debugger and debug drawing; on in debug builds (`application/game_framework/debug/enable_in_release` keeps them in a release build). Every key is an Input Map action (`gfgd_debug_*`), which the editor adds to the project with defaults, so they are rebound in Project Settings > Input Map.

- **Console** (`` ` ``) — `ConsoleManager` singleton: `register_command(name, callable, help, flags)` with arguments converted to the callable's parameter types, `register_variable(name, default, help, flags)` (`FLAG_SAVED` persists), `execute(line)`. `FLAG_SERVER` commands typed on a client run on the server as that player, and their output comes back. Two sizes: a command line at the bottom with suggestions (arguments and description of every match) as you type, then the full console with what was typed and printed. History, `dumpcommands`, `user://autoexec.cfg`. Game input is blocked while it is open. `--log` opens a terminal window with the game's log on Windows.
- **CheatManager** — `application/game_framework/debug/cheat_manager_script`: every public method of the script is a command that runs on the server, on the typing player's controller (`PlayerController.get_cheat_manager()`). Built-in: `teleport`, `set_movement_mode`, `gas_add_tag`, `gas_apply_effect`, `gas_grant_ability`, `gas_set_attribute`...
- **Gameplay debugger** (`'`) — follows the actor under the crosshair (Kp * locks it, Kp 0–9 toggle categories, Tab detaches a free camera, T moves the pawn to it). Categories (`GameplayDebuggerCategory`) collect on the **server** (`_collect_data`) and draw on the viewer (`_draw_data` → BBCode, `_draw_world` → `DebugDraw`), through a `DebugReplicator` under each `PlayerController`. Built-in: Basic, Abilities (with the client mirror's differences), Movement (with the client-server position error), Net. Game categories: `application/game_framework/debug/gameplay_debugger_categories`.
- **DebugDraw** — singleton: `line`, `arrow`, `sphere`, `box`, `point`, `axes`, `text` with a colour and a duration.
- **AbilitySystemDebugger** (console: `gas.toggle`, `gas.next`, `gas.target self|<path>`; no keys by default) — the local ability system state and an event log.

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

`load_game` returns null for four different reasons, and only `LOAD_NOT_FOUND` means the player has never played: `get_last_load_result()` says which, and `has_save_game(slot)` asks whether a file exists without reading it. After any other failure the slot is marked and `save_game` refuses it with `ERR_LOCKED`, so "load, and if that gave me nothing start fresh and save" cannot overwrite a save the game merely failed to decrypt — the usual cause being a changed encryption key. `clear_load_failure(slot)` lifts the block once the player has been told and has chosen to start over.

## The demo project

`project/` is a working example of every mode. From the menu: **New Game** (single player, ability demo), **Local Co-op** (two pads), **Host** and **Join**. Every pawn is a `CharacterBody3D` driven by a `CharacterMovementComponent`: **WASD** walks, **Space** jumps, **E** activates the test ability. The single-player map also has an ability yard to the right of the start - a heal pad, a fire, a haste pickup and a training dummy for the charged strike (**Q**, hold to charge) - and the online arena a heal pad and a fire whose effects the server applies. From the command line, after `--`:

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
