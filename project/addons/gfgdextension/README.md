# GFGD

A game framework for **Godot 4.6+**, built as a GDExtension: game modes, controllers, possession, per-player input, networking, and a lightweight gameplay ability system — tags, abilities, effects, attributes.

`VERSION.txt` next to this file says which build you have: the GFGD commit, the godot-cpp it was built against, the API version and the minimum Godot version.

This file is the orientation document: what the pieces are, how they fit, and the contracts that are not visible from the API. Per-class reference lives in Godot's own help — press <kbd>F1</kbd> and search for any class below, or <kbd>Ctrl</kbd>+click a class name in the script editor. The same reference ships as XML in `doc_classes/`, which is the fast way to look a signature up from outside the editor.

## Install

Copy this `gfgdextension` folder into your project's `addons/` directory and restart Godot. There is no plugin to enable in Project Settings and no `plugin.cfg`: `gfgd.gdextension` is picked up wherever it sits, and the editor integration registers itself from the extension.

To confirm it actually loaded, `.godot/extension_list.cfg` should name it and `ClassDB.class_exists("World")` should be true. An empty `extension_list.cfg` means the editor has not rescanned, or there is no binary for this platform and target slot.

**A project that already has C# classes of its own named `GameInstance`, `GameMode`, `Controller`, `PlayerController`, `Level`, `LocalPlayer`, `InputComponent`, `SaveGame`, `ProjectStatics`, `GameplayTag*` or `Assert*` and marked `[GlobalClass]` will collide with these on ClassDB names.** One of the two has to go — they cannot both be registered.

Then, in Project Settings → Application → Game Framework:

| Setting | Meaning |
|---|---|
| `application/run/main_loop_type` | Must be `World`. **Required** — nothing else starts the framework. |
| `game_framework/game_instance_script` | Script extending `GameInstance`. |
| `game_framework/default_game_mode` | A scene whose root is a `GameModeBase`. |
| `game_framework/default_port` | Port your menus and helpers default to. |
| `game_framework/gameplay_tag_tables` | `GameplayTagTable` resources, in merge order. Empty is fine to start with. |
| `game_framework/save_encryption_key` | Key for encrypted saves. Ships with a default that is in this repository — replace it before release. |

The extension registers all of these on load, so they appear in the editor UI. Godot does not write out settings that still hold their default, so a fresh install adds nothing to `project.godot` until you change something.

## Class map

| Class | Role |
|---|---|
| `World` | Custom `SceneTree`, the entry point. Owns startup order, the level and the session. |
| `NetDriver` | Connections, travel, and mirroring what the server creates. |
| `GameInstance` | Application-lifetime object. State that survives a level change. |
| `Level` | Playable scene root. May override the game mode. |
| `GameModeBase` | Per-level rules and the default scenes everything is built from. Server only. |
| `GameStateBase` | What every peer is allowed to know, including the player list. Exists on clients too. |
| `PlayerController` / `Controller` / `AIController` | Possesses a pawn, owns its input. Dies with the level. |
| `PlayerState` | Per-player data everyone sees: name, id, score, ping. Lives under the game state. |
| `Pawn` | Makes a scene possessable. Where input bindings are declared. |
| `PlayerStart2D` / `PlayerStart3D` | Where the game mode places a pawn. |
| `LocalPlayer` | One human at this machine. Owned by the `GameInstance`, survives a level change. |
| `PlayerInput` | That human's own action state, filtered by the devices they own. |
| `InputRouter` | Sends each raw event to the player who owns the device it came from. |
| `InputComponent` | Binds input actions to callables, reading the owning player's `PlayerInput`. |
| `CharacterMovementComponent` | Walking, falling and jumping for a 3D pawn. Unreal's property names, metric values. |
| `MovementMode` | One way of moving, as a `Resource`. `WalkingMode`, `FallingMode`, or your own in GDScript. |
| `MovementState` / `MovementInput` / `ProposedMove` | The simulation state, the input for one tick, and what a mode asks for. |
| `MovementBackend` | Decides when the simulation runs and how often. The seam client prediction would plug into. |
| `AbilitySystemComponent` | Abilities, owned tags, effects, attributes. The ability hub. |
| `GameplayAbility` / `GameplayEffect` | Authorable ability and effect resources. |
| `AttributeSet` / `AttributeModifier` | Named numeric attributes and the changes applied to them. |
| `GameplayTag` / `GameplayTagContainer` | A hierarchical tag, and a set of them. |
| `GameplayTagTable` / `GameplayTagsManager` | Tag declarations, and the merged runtime lookup. |
| `SaveGame` / `ProjectStatics` | Save state and the helpers that read and write it. |
| `NodePool` | Recycles nodes for anything spawned by the hundred. |
| `GameplayMessageRouter` | Fire-and-forget bus, keyed by tag name, for announcements with no owner. |
| `PlayerVirtualJoystick` / `PlayerTouchButton` | Touch controls that drive one player's `PlayerInput`. |

And how they sit at runtime:

```
root
 ├─ NetDriver              – connections, travel, mirroring
 ├─ GameState              – what every peer may know; exists on clients too
 │   └─ PlayerState…       – per-player name, id, score, ping
 ├─ Players
 │   └─ PlayerController…  – server: all of them; client: only its own
 ├─ Pawns
 │   └─ Pawn<id>           – the node a player drives
 │       ├─ Pawn           – makes the parent possessable, declares input bindings
 │       ├─ Camera2D/3D    – made current on possession, where the player is sitting
 │       └─ AbilitySystemComponent
 ├─ InputRouter            – sends each raw event to the player owning that device
 ├─ Level                  – scene root node, may override the game mode
 │   └─ PlayerStart2D/3D…  – where a pawn is placed
 └─ GameMode               – per-level rules and default scenes; server only

Held by World, outside the scene tree:
 GameInstance             – application-lifetime object (created from a project setting)
  └─ LocalPlayer…         – one per human at this machine; survives a level change
      └─ PlayerInput      – that human's own action state, filtered by device
```

Everything under `root` is a real node; everything under `GameInstance` is a plain `Object` that `World` owns and frees itself, deliberately outside the tree so it can outlive a level. `NetDriver`, `InputRouter`, `Players` and `Pawns` are built once and survive a level change; `Level`, `GameState` and `GameMode` are rebuilt with each level.

Controllers, player states and pawns hang off fixed parents rather than off the level, and are named after a server-assigned `player_id`, because their node path is the address every peer refers to them by.

## The game mode is a scene

A game mode declares what everything else is built from, so it is a scene whose root is a `GameModeBase` — that is the only way those choices can be set in the inspector:

| Property | Used for |
|---|---|
| `game_state_scene` | Root must be a `GameStateBase`. |
| `player_state_scene` | Root must be a `PlayerState`. |
| `player_controller_scene` | Root must be a `PlayerController`. |
| `default_pawn_scene` | The pawn every player gets. |
| `pawn_scene_overrides` | Indexed by local player index; falls back to `default_pawn_scene`. |
| `max_local_players`, `allow_press_to_join` | Local multiplayer. |
| `max_players` | How many players the session accepts, across every machine. |

Point `Level.game_mode_override` at it, or leave it to `game_framework/default_game_mode`.

A client never runs a game mode, but it does read these: both sides have to build the same player state and the same pawn, and this is where that agreement is written down.

## Startup ordering

```
[Level] _enter_tree / _ready          <- the level AND EVERYTHING IN IT is up first
GameState created and added
GameMode._init_game                   <- prepare; spawns and possesses the players
GameMode._ready                       <- everything live
Level._init_level                     <- the first hook that can see all of it
```

Read the first line carefully, because it is the single most expensive thing in this document. **Everything sitting in the level scene — a HUD, a camera, a spawner, a warning overlay, a touch control — has its `_ready` run before there is a game mode to ask, before a pawn exists, and before any controller has possessed anything.** `World.get_game_mode()` returns null there, the usual `if x != null` guard swallows it, and the node simply never finds what it was looking for, for the whole session, in silence.

Either hand the references out from `Level._init_level`, which runs after everything is built, or — usually better — have each node resolve what it needs the first time it needs it, which also survives the pawn being respawned mid-level.

Two hooks, one rule: **prepare in `_init_game`, play in `_ready`.** The framework logs the local players in and possesses their pawns on its own; your game mode overrides `_init_game` only if it needs to prepare something first, and reads the player off the world from `_ready`. Returning `true` from `_init_game` suppresses the spawn entirely — that is how a menu level runs with no player at all rather than with an empty one.

The player list lives on `World`, not on the `GameMode` — `get_first_player_controller()` and `get_player_controllers()`. The game mode is the server's object and does not exist on a client; the world does, so that is where "who is player one" is answered.

## Minimal integration

A game mode that reads an attribute off the spawned player:

```gdscript
extends GameModeBase

func _ready() -> void:
	var pawn: Pawn = get_world().get_first_player_controller().get_pawn()
	var asc: AbilitySystemComponent = pawn.get_pawn_root().get_node("AbilitySystemComponent")

	asc.attribute_changed.connect(func(name: StringName, old: float, new: float) -> void:
		print("%s %s -> %s" % [name, old, new]))

	print(asc.get_attribute_value(&"health"))
	asc.try_activate_ability(&"TestAbility")
```

A pawn that binds input. Note this runs **on possession**, not on `_ready`:

```gdscript
extends Pawn

func _setup_input_component(input_component: InputComponent) -> void:
	var asc: AbilitySystemComponent = get_pawn_root().get_node("AbilitySystemComponent")
	input_component.bind_action(&"attack", InputComponent.STARTED,
		func() -> void: asc.ability_local_input_pressed(&"attack"))
	input_component.bind_action(&"attack", InputComponent.COMPLETED,
		func() -> void: asc.ability_local_input_released(&"attack"))
```

And moving it, in a way that is correct offline and online alike:

```gdscript
func _process(delta: float) -> void:
	if not has_authority():
		return    # the server decides where this pawn is; here it is only shown

	var move := _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, 0.0, move.y))
	get_pawn_root().position += consume_movement_input_vector() * SPEED * delta
```

## Character movement

`CharacterMovementComponent` gives a pawn walking, falling and jumping. Attach it beside the `Pawn`, on a root that is a `PhysicsBody3D` with a capsule shape:

```
CharacterBody3D
 ├─ CollisionShape3D      # a CapsuleShape3D
 ├─ Pawn
 └─ CharacterMovementComponent
```

`updated_body_path` defaults to the parent and the walking and falling modes register themselves, so nothing else is required. Feed it with the `add_movement_input` the pawn already had — guarded by `has_authority()` — and **stop moving the root yourself**: the component writes the transform once per tick and would overwrite you.

Property names follow Unreal's `CharacterMovementComponent`, but **the values are metric**. Godot works in metres where Unreal works in centimetres, so `max_walk_speed` defaults to `6.0`, not `600`. Divide anything lifted from an Unreal project by 100.

`ground_friction` and `braking_deceleration_walking` are not the same knob and should not be tuned as one: friction is how sharply the character can **change direction** while being pushed, braking is how hard it **stops** when input ends.

How a character moves is a named `MovementMode` in a dictionary rather than a value in an enum, so a project can add one — a `Resource`, subclassable in GDScript — without this extension changing. Each mode proposes motion in `_generate_move` and carries it out in `_simulation_tick`; the split is what will let a dash be layered on a walk without the walking code knowing about dashes.

Movement runs on the authority, as everything else does. There is still no client prediction; `MovementBackend` is the seam it would plug into, and it exists now because it cannot be added afterwards.

What a client receives is the transform plus **`movement_mode`** — a position says where a pawn is, not whether it is walking or falling, and the second is what animation needs. `movement_mode_changed` fires on every peer; set `replicate_movement_mode = false` for a pawn whose mode nothing outside the simulation cares about.

Not in yet: step-up (`max_step_height` is carried but not acted on), crouching, swimming, flying, moving platforms, root motion, layered moves, and 2D.

## Networking

The same build is every role, decided by what you called:

| Call | `World.get_net_mode()` |
|---|---|
| nothing | `NET_MODE_STANDALONE` |
| `host_game(port)` | `NET_MODE_LISTEN_SERVER` |
| `create_dedicated_server(port)` | `NET_MODE_DEDICATED_SERVER` |
| `join_game(address, port)` | `NET_MODE_CLIENT` |

`has_authority()` is true in every mode but the last, and true with no peer at all — which is what makes a rule written for a server also correct in a game that never touches the network.

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

### Replication

The framework mirrors what it creates, so a project places no `MultiplayerSpawner` and no `MultiplayerSynchronizer` of its own. Player states, pawns and player controllers are spawned through `NetDriver`; properties are kept in step by synchronizers the framework attaches from `_ready` at matching paths on every peer.

For your own state, return the property names from `_get_replicated_properties()` on a `PlayerState`, `GameStateBase` or `Pawn` subclass:

```gdscript
extends PlayerState

var kills := 0

func _get_replicated_properties() -> PackedStringArray:
	return PackedStringArray(["kills"])
```

### Ownership, roles and input

Ownership is the chain a pawn is reached by: a pawn is owned by its controller, and a player controller by the connection it was created for. It decides whether a remote call is honoured — input sent for a controller the sender does not own is dropped.

Roles say what a peer may do with a node, and are mirror images of each other: on the server everything is `ROLE_AUTHORITY`; on the client that owns it, `ROLE_AUTONOMOUS_PROXY`; on any other client, `ROLE_SIMULATED_PROXY`. Authority is not ownership — the server has authority over every pawn, including one a client owns.

A client sends what its player is holding and the server writes it into that controller's `PlayerInput`, so from the input component down the server runs exactly the code a local player runs. **There is no client-side prediction and no rollback**: movement costs a round trip. A game that needs a snappier feel adds prediction on top, and the pieces to do it with are already here.

## Local multiplayer

Godot's `Input` singleton merges every device into one state, which is why two people cannot share it. GFGD splits it: a **`LocalPlayer`** is one human at this machine, it owns a **`PlayerInput`** holding that person's own action state, and a central **`InputRouter`** hands each raw event to whoever owns the device it came from.

The split is deliberate about lifetime. A `PlayerController` belongs to a level and dies with it; a `LocalPlayer` is owned by the `GameInstance` and carries the device assignment across a level change.

**A project that does nothing sees no change.** The first local player is created holding `PlayerInput.DEVICE_SLOT_ALL`, a wildcard that accepts every device, and in that state `PlayerInput` forwards every query straight to `Input`. Same behaviour, same code path, down to synthetic `Input.action_press()`.

### Device slots

A device slot is a plain `int`, so it round-trips through GDScript and can key a `Dictionary`:

| Value | Meaning |
|---|---|
| `0`, `1`, `2`, … | Joypad index |
| `PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE` | Keyboard and mouse, counted as one device |
| `PlayerInput.DEVICE_SLOT_ALL` | Every device. The single player default |
| `PlayerInput.DEVICE_SLOT_NONE` | No device yet; waiting for a press-to-join |

A player may own several at once — keyboard and mouse *and* a pad is the normal setup for player 1 in a co-op game.

Events are classified by class, not by `InputEvent.device`: keyboard and mouse have reserved ids, but a synthesized `InputEventAction` reports `0`, which would collide with joypad 0.

### Two pads, keyboard ignored

Set `max_local_players = 2` and `allow_press_to_join = true` on the game mode, then:

```gdscript
extends GameModeBase

func _init_game(world: World) -> bool:
	var pads: Array = Input.get_connected_joypads()

	# Narrow player 0 off the DEVICE_SLOT_ALL wildcard first, or it swallows
	# every event the second pad needs to join with.
	world.get_local_player(0).device_slots = PackedInt32Array([pads[0]])

	if pads.size() > 1:
		world.create_local_player(pads[1])

	return false    # the framework logs everyone in and spawns them

func _can_join(device_slot: int) -> bool:
	return device_slot != PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE
```

`_can_join` is the gate for press-to-join, and it is where "keyboard is not a player" belongs. Any button on an unowned device offers its slot to `GameModeBase.try_join()`; a player who is already in the game but has no device claims it first, so the same path doubles as "press a button to pick your pad back up" after a disconnect.

One `InputMap` serves everyone. You do not duplicate actions per player.

### Touch controls

Godot ships `VirtualJoystick` and `TouchScreenButton`, and for a single player game those are the better choice — they are themed, and `PlayerInput` in passthrough forwards to `Input` anyway. `PlayerVirtualJoystick` and `PlayerTouchButton` exist for the two things the engine's cannot do: address one player out of several (`player_index`), and hold an action for as long as a finger is on the stick (`press_action`), so one gesture can mean both "move" and "hold".

```gdscript
$Joystick.action_left = &"move_left"
$Joystick.action_right = &"move_right"
$Joystick.action_up = &"move_up"
$Joystick.action_down = &"move_down"
$Joystick.press_action = &"grab"      # held while the finger is down
```

Both are `Control`s, so they anchor with the rest of the HUD, and both read raw touches in `_input` tracked by finger index. Neither marks events handled, so buttons above keep working — a stick keeps clear of the HUD simply by having a rect that does not cover it.

### Split screen

Out of scope by design: the framework records *which* viewport belongs to whom and leaves the layout to you. Set `LocalPlayer.viewport_override` to a `SubViewport` and `PlayerController.set_pawn_camera_node_as_current()` targets it. A `Camera2D` needs nothing else — its `custom_viewport` is set for you. A `Camera3D` has no such property, so it has to physically live inside the `SubViewport`; `LocalPlayer.adopt_camera()` moves it there, and the framework warns rather than reparenting behind your back.

## Gameplay tags

Tags are declared in `GameplayTagTable` resources — a `tag name -> description` dictionary — and listed in `gameplay_tag_tables`. Several tables merge in listed order; on a duplicate declaration the first one wins and the later one warns.

Parents are implicit: declaring `Ability.Combat.Melee` also registers `Ability` and `Ability.Combat` as usable tags, though they hold no description until some table declares them outright.

Selecting a table in the FileSystem dock opens a tree editor. Double-click a row (or press Rename) to edit a name or a description; renaming a tag rewrites everything beneath it as a single undo step. `GameplayTag` and `GameplayTagContainer` properties get a hierarchical picker in the inspector.

```gdscript
var manager := GameplayTagsManager.get_singleton()
var tag := manager.request_tag(&"Status.Debuff.Burning")

if asc.get_owned_gameplay_tags().has_tag(tag):
	pass    # has_tag is hierarchical: "Status.Debuff" would match too
```

## Pooling and messaging

`NodePool` recycles nodes by taking them **out of the tree**, not by disabling them: a node outside the tree does not exist for the physics server, so a recycled body cannot be hit by accident. Give it a `scene` or a `factory`, then `acquire()` and `release()`; `release()` is safe from inside a physics callback, because leaving the tree is deferred to the end of the frame.

`GameplayMessageRouter` is a bus for announcements with no natural owner. Channels are tag names, so a listener on `Fx` with `MATCH_PARTIAL` hears `Fx.Impact`:

```gdscript
GameplayMessageRouter.get_singleton().register_listener(&"Fx.Impact", _on_impact)

func _on_impact(_channel: StringName, payload: Variant) -> void:
	burst(payload.position, payload.strength)
```

## Porting an existing project onto GFGD

The framework has replacements for most of what a hand-rolled one does, so the bulk of a port is
deletion:

| What you have | What replaces it |
|---|---|
| Autoload holding cross-level state | `GameInstance` — created from a project setting, outside the tree |
| Autoload or level script spawning the player | `GameModeBase`, which spawns and possesses on its own |
| Hard-coded `Marker2D` spawn point | `PlayerStart2D` / `PlayerStart3D` |
| A settings `Resource` naming the pawn and controller scenes | The game mode **scene**'s own inspector properties |
| Static event bus with string channels | `GameplayMessageRouter`, channels keyed by tag name |
| Hand-rolled object pool | `NodePool` |
| `Input.is_action_pressed` in the player script | `InputComponent`, bound in `_setup_input_component` |
| Own touch joystick `Control` | `PlayerVirtualJoystick` / `PlayerTouchButton` |
| JSON save helper | `SaveGame` + `ProjectStatics.save_game` / `load_game` |
| Tag list in an `.ini` | `GameplayTagTable` resources in `gameplay_tag_tables` |

Three things worth knowing before you start:

- **From C#, the port cannot be gradual.** C# cannot inherit a GDExtension class, so every class
  that exists to override something has to become GDScript; and a C# framework of your own whose
  `[GlobalClass]` names match GFGD's cannot be installed alongside it. Decide up front how far the
  port goes.
- **Get the project *booting* before porting any gameplay.** Switch `main_loop_type`, write the game
  instance, one game mode scene, one level and the tag table, and replace everything else with
  three-line stubs at the paths the scenes already reference. A stub for a `Resource` must repeat the
  same `@export` names, or its `.tres` loses every value on the next save.
- **A parse check proves nothing.** The mistakes this framework makes possible — a `_ready` that runs
  too early, a pawn drawing behind the level, a `.tres` that loaded without its script — all produce
  a running game with clean output. Boot after every step and check the actual numbers.

`skills/gfgd/references/migrating-from-csharp.md` has the long version, written from a real port.

## Gotchas — what the API does not tell you

Signatures are introspectable (`ClassDB.class_get_method_list()`, or <kbd>F1</kbd>). These are the contracts that are not, and each one is a mistake that is easy to make.

- **C# cannot inherit GDExtension classes.** `class MyGameMode : GameModeBase` will not work — this is a Godot limitation, not a GFGD one. From C#, use the framework compositionally: place the nodes, author the resources, call the methods, connect to the signals. Anything requiring an override (`_init_game`, `_activate_ability`, `_setup_input_component`, …) has to be GDScript.
- **Nothing crashes.** Missing settings, a missing `Pawn` node, an unloadable tag table — all warn and fall back. A clean Output panel does not mean the project is configured; check for warnings.
- **Virtual hooks are overrides, not signals.** Methods starting with `_` (`_init_game`, `_can_activate_ability`, `_pre_attribute_change`) are overridden in a subclass. Do not try to `connect()` to them.
- **Every scene a game mode names has to be its own file.** A scene built inline cannot be named to another machine, so a client would have nothing to build. The framework warns when it finds one while networked.
- **A pawn built by hand is not mirrored.** Choose the scene through `default_pawn_scene`, `pawn_scene_overrides` or `_get_pawn_scene_for`, and let the framework spawn it; a node a script instantiates itself exists only where it was made.
- **`GameMode` is null on a client, on purpose.** Use `World.get_game_state()` for anything a client has to read, and keep rules behind `has_authority()`.
- **Tags are referenced by name, never by object.** Containers serialize a `PackedStringArray` and resolve through `GameplayTagsManager` on load. Never author an inline `GameplayTag` sub-resource in a `.tres` — it compares equal by name but is not the canonical instance, and it breaks when the tag is renamed.
- **Tag container properties start out null.** `ability_tags`, `granted_tags` and the rest default to `null`, not to an empty container, because ClassDB warns about any live object used as a property default. Every framework method that takes a container accepts null and reads it as empty, and the inspector creates one on the first edit — but your own GDScript has to check `is_valid()` before calling into one.
- **`request_tag()` turns a typo into a real tag.** It never returns null for a well-formed name; it registers the tag, warns, and carries on. `get_tag_source_kind()` returning `TAG_SOURCE_RUNTIME` is how you find these — the editor pickers show them in red.
- **The `Pawn` node is found recursively.** It may be the pawn scene root or any descendant; the first match wins. Two of them in one scene means the other is silently ignored.
- **Player starts are claimed once each per level.** With more players than starts the framework warns and reuses them. It cannot test whether a start is physically occupied.
- **Input binding happens on possession.** `_setup_input_component` is called by the `PlayerController` when it possesses the pawn — binding from `_ready` is too early and will not fire.
- **Never read `Input` from a pawn script.** It merges every device, so with two local players it moves both pawns at once, and on a server it is nobody's input at all. Go through the `InputComponent`, which resolves to the owning player's `PlayerInput`.
- **Raising `max_local_players` without narrowing player 0 leaves nothing for anyone else.** Player 0 starts on `DEVICE_SLOT_ALL` and would claim every event, including the ones a second pad needs to join with. The framework warns about exactly this combination.
- **`PlayerInput` in filtered mode does not see `Input.action_press()`.** Synthetic actions pushed into the engine singleton belong to nobody. Use `PlayerInput.action_press()` on the player you mean.
- **The `InputRouter` listens on `_input`, not `_unhandled_input`.** It has to: if a `Control` swallowed a press but not the release, the action would stay held forever. It never marks input handled, so the GUI is unaffected — but gameplay actions do fire while a `LineEdit` has focus. Disable the `InputComponent` to gate that.
- **A `Controller` subclass overriding `_enter_tree` must call the base implementation.** Godot binds only the most derived override, and the base is what registers the controller with `World.get_controllers()`. Forgetting it removes the controller from every world list, silently.
- **A `LocalPlayer` is not a node.** It has no name path and does not appear in the scene tree; hold the reference or ask the `GameInstance` for it by index. It also outlives the level, which is the whole reason it is not one.
- **The attribute formula is `(base + Σ ADD) × Π MULTIPLY`,** and a single `OVERRIDE` beats all of it. `AttributeSet` is duplicated at runtime, so the resource on disk is a template and play never mutates it.
- **Owned tags are reference counted.** Two sources granting the same tag both have to release it. One ability ending does not strip a tag another still grants.
- **The ability system is not replicated.** Activate abilities where the pawn is authoritative and let the results reach clients through what you replicate on the pawn or its player state.
- **Tag tables load before anything else.** `World::_initialize` builds `GameplayTagsManager` before creating the `GameInstance`, so tags are always resolvable from `_on_init` onward.
- **A pooled node is reset by name, not by interface.** `NodePool` calls `_on_acquired()` and `_on_released()` if the node defines them, because a pooled node is a `RigidBody2D` one moment and an `Area2D` the next and there is no shared base to declare them on. A typo in either name is silent.
- **`NodePool` frees everything when it leaves the tree.** Nodes waiting in the free list have no parent, so nobody else ever would. That also means a pool moved to another parent comes back empty.
- **A message router listener does not have to be unregistered.** One whose object has been freed is dropped on the next broadcast. Delivery is synchronous and the payload is passed as `callback(channel, payload)` — two arguments, always, because a `MATCH_PARTIAL` listener cannot otherwise tell what it heard. It is local to one peer.
- **Message router listeners on one channel run in reverse registration order — last registered, first called.** The router walks its listener list backwards so it can drop freed listeners while iterating. Never encode a dependency in registration order; if one listener needs what another produced, the producer should broadcast a second channel once the result exists.
- **Possession makes the pawn camera current, where the player is sitting.** For a game filmed by a camera the level owns, set `Pawn.auto_manage_camera = false`, or the pawn quietly steals the view — and without a camera on the pawn you pay for a recursive search that finds nothing.
- **The default save encryption key is in this repository.** `application/game_framework/save_encryption_key` defaults to it so existing saves keep working; changing it makes them unreadable, so change it before your first release rather than after.
- **`ProjectStatics.load_game` returns null for four different reasons** — file missing, decryption failed, `_from_json` rejected it, wrong script — and only the first means "no save yet". Ask `get_last_load_result()` rather than inferring a first run from the null; `has_save_game(slot)` answers "is there a file" without reading it. After any failure but `LOAD_NOT_FOUND` the framework refuses `save_game` on that slot and returns `ERR_LOCKED`, so the classic "null means first run, save over it" no longer destroys a profile — it fails loudly on the write. `clear_load_failure(slot)` lifts the block once the player has chosen to start over.
- **A `.tres` whose script extends a native GFGD type must name that type in its header** — `[gd_resource type="AttributeSet" script_class="MyAttributes" …]`, not `type="Resource"`. Godot refuses to attach a script whose base is a GDExtension class to a resource declared as something else; the file then loads with no script and every value at its default. The editor writes the right header itself, so this only bites a hand-written or hand-edited file.
- **A level's children are ready before the game mode exists.** See *Startup ordering* above. Anything in the level scene that reaches for the game mode, a pawn or a controller from `_ready` gets null. Within a single scene, Godot's own rule that a child's `_ready` runs before its parent's has the same shape: a child reading an attribute its parent defines in `_ready` gets `0.0`, so read attributes at the point of use and pick a fallback that means "not configured" rather than one that means "do nothing".
- **In 2D, a pawn draws behind the level.** `Pawns` is built before any level is loaded, so it sits earlier in root's child list, and root's 2D children draw in tree order — the level's background paints straight over the pawn, which is then invisible with nothing in the Output to explain it. Give the pawn scene root a `z_index` above what the level draws.
- **The editor binary loads the `.editor` library even when running a game.** `godot --path <project>` from an editor build matches `windows.x86_64.single.debug.editor` in the manifest, not `template_debug`. Rebuilding only the template target and testing that way will appear to change nothing — a real template build needs an export.

## Layout

```
gfgdextension/
  README.md
  gfgd.gdextension     manifest; library paths are relative to this file
  VERSION.txt          which build this is
  bin/
    windows/  linux/  macos/  android/
  doc_classes/         per-class XML reference   (.gdignore)
  src/                 the C++ sources           (.gdignore)
  skills/              agent skills              (.gdignore)
```

The last three ship as plain text and carry an empty `.gdignore`, so Godot never scans, imports or
exports them. Godot does not export non-resource files by default, but it does scan them into the
FileSystem dock; the marker makes both guarantees unconditional and costs nothing. It follows that
they cannot be `load()`ed either — read them with ordinary file tools.

**The markers live in those subfolders and never in the addon root.** A `.gdignore` at the top would
hide `gfgd.gdextension` itself, and the extension would not load at all.

`skills/` holds two agent skills: `gfgd` for working in a game built on the framework, and
`gfgd-dev` for working on the framework itself. Copy the folder you want into your client's skills
directory, e.g. `.claude/skills/`.

Library filenames are keyed by platform, architecture, float precision and build target — see `[libraries]` in the manifest for what each slot expects. The editor build provides the tag pickers and the tag table editor; template builds carry runtime only.

## Building

From the repository root, not from here. The short way, which builds and then assembles this folder:

```powershell
./pack_addon.ps1 -Build windows,android
```

One platform there is one full set of targets. Add `-Destination <project>/addons/gfgdextension -InstallSkills` to land the result in a game in the same command, and drop `-Build` to repack without compiling anything.

The long way: **one SCons invocation builds exactly one library**, for one `platform` x `arch` x `target`; both default to the host, so every other platform has to be named.

| | `editor` | `template_debug` | `template_release` |
|---|:---:|:---:|:---:|
| `TOOLS_ENABLED` (the tag editor) | ✓ | | |
| `DEBUG_ENABLED` | ✓ | ✓ | |
| optimisation | `speed_trace` | `speed_trace` | `speed` |
| class reference compiled in | ✓ | ✓ | |
| debug symbols (CMake presets) | ✓ | | |
| loaded by | the editor | an export with debug | a release export |

`GODOTCPP_TARGET` and `CMAKE_BUILD_TYPE` are separate axes, and confusing them is expensive. The target decides `DEBUG_ENABLED`, `TOOLS_ENABLED` and optimisation; the build type decides only whether debug symbols are emitted. `template_debug` does **not** have to mean compiled with `-g` — under MinGW the DWARF lands inside the library rather than beside it, which is worth 60 MB on Windows and 30 MB per Android ABI. The presets build it as `Release`; only the editor target keeps its symbols. SCons emits none by default for any target, which is why a SCons build has always come out smaller than a CMake one.

```sh
# a desktop platform wants all three
scons platform=windows arch=x86_64   target=editor
scons platform=windows arch=x86_64   target=template_debug
scons platform=windows arch=x86_64   target=template_release

scons platform=linux   arch=x86_64   target=editor            # and arch=arm64 if needed
scons platform=macos   arch=universal target=editor           # universal is required on macOS

# Android wants two, twice - nobody runs an editor there
scons platform=android arch=arm64  target=template_debug
scons platform=android arch=arm64  target=template_release
scons platform=android arch=x86_64 target=template_debug
scons platform=android arch=x86_64 target=template_release
```

Android needs `ANDROID_HOME` pointing at an SDK with the NDK installed. macOS must use `arch=universal` — the build strips `.universal` from the file name, which is what the manifest expects; a per-arch build comes out named something nothing looks for.

Each build copies its library into `bin/<platform>/` above; only the shared library is copied, not the import libraries that MSVC emits beside it. A CMake build is also provided for IDE use and writes to the same place.

Class reference in `doc_classes/*.xml` at the repository root is compiled into the binary, which is what makes it show up under <kbd>F1</kbd>. Regenerate the skeletons after changing the API with:

```
godot --headless --path <project> --doctool <abs-path-to-repo-root> --gdextension-docs
```

It merges with the existing files, so prose already written is kept.
