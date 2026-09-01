# GFGD

An Unreal Engine inspired game framework for **Godot 4.6+**, built as a GDExtension. It ports the core of Unreal's Game Framework — GameInstance / GameMode / Level / PlayerController / Pawn possession — and a lightweight Gameplay Ability System: gameplay tags, abilities, effects, attributes.

This file is the orientation document: what the pieces are, how they fit, and the contracts that are not visible from the API. Per-class reference lives in Godot's own help — press <kbd>F1</kbd> and search for any class below, or <kbd>Ctrl</kbd>+click a class name in the script editor.

## Install

Copy this `gfgdextension` folder into your project's `addons/` directory and restart Godot. There is no plugin to enable in Project Settings and no `plugin.cfg`: `gfgd.gdextension` is picked up wherever it sits, and the editor integration registers itself from the extension.

Then, in Project Settings → Application → Game Framework:

| Setting | Meaning |
|---|---|
| `application/run/main_loop_type` | Must be `GFGDSceneTree`. **Required** — nothing else starts the framework. |
| `game_framework/game_instance_script` | Script extending `GameInstance`. |
| `game_framework/default_game_mode_settings` | A `GameModeSettings` resource. |
| `game_framework/gameplay_tag_tables` | `GameplayTagTable` resources, in merge order. Empty is fine to start with. |

The extension registers all of these on load, so they appear in the editor UI. Godot does not write out settings that still hold their default, so a fresh install adds nothing to `project.godot` until you change something.

### Startup ordering

`GameMode._init_game` is the hook for preparing a level before anything in it runs — Unreal's `InitGame`, which fires before any actor's `BeginPlay`. Whether you actually get that ordering depends on how the level was opened.

Levels opened with `GFGDSceneTree.open_level()` always give it: the scene is instantiated, the game mode is created and `_init_game` called while the level is still **outside** the scene tree, and only then is it added. Every node is reachable and none has run yet.

```
GameMode._init_game        <- level, its nodes and the spawned player reachable, none started
  [Level] _enter_tree
  [player + nodes] _enter_tree
  [player + nodes] _ready
  [Level] _ready
GameMode._ready            <- everything live
Level._init_level
```

Two hooks, one rule: **prepare in `_init_game`, play in `_ready`.** The framework spawns and possesses the pawn from `GameModeSettings` on its own, before the level enters the tree — which is exactly what makes the player reachable from every node's `_ready`, the level's own nodes included. Your game mode does not call `spawn_default_player()`; it overrides `_init_game` only if it needs to prepare something first, and reads the player off the world from `_ready`.

The player list lives on `GFGDSceneTree`, not on the `GameMode` — `get_first_player_controller()` and `get_player_controllers()`. The game mode is the server's object and does not exist on a client; the world does, so that is where "who is player one" is answered.

Returning `true` from `_init_game` suppresses the spawn entirely. That is how a menu level runs with no player at all rather than with an empty one.

The first level is the exception to the ordering, and cannot be otherwise: it is the main scene, which Godot adds before the framework gets control, so its nodes have already run `_ready` when `_init_game` fires. The `_ready` half is unaffected — the game mode still enters the tree last. The way around it is to not need `_init_game` there — make the main scene a menu or a loading level, and reach the levels that care through `open_level()`. The bundled demo does exactly that.

## Class map

| Class | Role |
|---|---|
| `GFGDSceneTree` | Custom `SceneTree`, the entry point. Owns startup order. |
| `GameInstance` | Application-lifetime object. State that survives a level change. |
| `Level` | Playable scene root. May override the game mode. |
| `GameMode` | Per-level rules. Logs players in, spawns and possesses them. Server-only in principle. |
| `GameState` | What every peer is allowed to know, including the player list. Exists on clients too. |
| `GameModeSettings` | Which game mode script, pawn scene and controller scene to use, and how many local players. |
| `PlayerController` / `Controller` | Possesses a pawn, owns its input. Dies with the level. |
| `PlayerState` | Per-player data everyone sees: name, index. Lives under the `GameState`. |
| `LocalPlayer` | One human at this machine. Owned by the `GameInstance`, survives a level change. |
| `PlayerInput` | That human's own action state, filtered by the devices they own. |
| `InputRouter` | Sends each raw event to the player who owns the device it came from. |
| `PawnHandler` | Makes a scene possessable. Where input bindings are declared. |
| `InputComponent` | Binds input actions to callables, reading the owning player's `PlayerInput`. |
| `AbilitySystemComponent` | Abilities, owned tags, effects, attributes. The GAS hub. |
| `GameplayAbility` / `GameplayEffect` | Authorable ability and effect resources. |
| `AttributeSet` / `AttributeModifier` | Named numeric attributes and the changes applied to them. |
| `GameplayTag` / `GameplayTagContainer` | A hierarchical tag, and a set of them. |
| `GameplayTagTable` / `GameplayTagsManager` | Tag declarations, and the merged runtime lookup. |
| `SaveGame` / `ProjectStatics` | Save state and the helpers that read and write it. |

And how they sit at runtime:

```
root
 ├─ Level                 – scene root node, may override GameModeSettings
 │   ├─ PlayerStart…      – where restart_player places each pawn
 │   └─ Pawn…             – spawned here, one per player
 │       ├─ PawnHandler   – makes the pawn possessable, sets up input bindings
 │       ├─ Camera2D/3D   – made current on possession
 │       └─ AbilitySystemComponent – abilities, gameplay tags, effects, attributes
 ├─ InputRouter           – sends each raw event to the player owning that device
 ├─ GameState             – what every peer may know; exists on clients too
 │   └─ PlayerState…      – per-player name, index, score
 └─ GameMode              – per-level rules, logs players in (from GameModeSettings)
     └─ PlayerController… – possesses a PawnHandler
         └─ InputComponent – binds input actions to callables

Held by GFGDSceneTree, outside the scene tree:
 GameInstance             – application-lifetime object (created from a project setting)
  └─ LocalPlayer…         – one per human at this machine; survives a level change
      └─ PlayerInput      – that human's own action state, filtered by device
```

Everything under `root` is a real node; everything under `GameInstance` is a plain `Object` that `GFGDSceneTree` owns and frees itself, deliberately outside the tree so it can outlive a level. `Level`, `InputRouter`, `GameState` and `GameMode` are all direct children of the root — only the router survives `open_level()`.

## Minimal integration

A game mode that reads an attribute off the spawned player:

```gdscript
extends GameMode

func _ready() -> void:
	var pawn_handler: PawnHandler = get_gfgd_scene_tree().get_first_player_controller().get_pawn_handler()
	var asc: AbilitySystemComponent = pawn_handler.get_pawn_root().get_node("AbilitySystemComponent")

	asc.attribute_changed.connect(func(name: StringName, old: float, new: float) -> void:
		print("%s %s -> %s" % [name, old, new]))

	print(asc.get_attribute_value(&"health"))
	asc.try_activate_ability(&"TestAbility")
```

Nothing spawns the player here — the framework already did, and by `_ready` it is fully started. Override `_init_game` on top of this only to prepare the level before it runs, or to return `true` and suppress the spawn.

A pawn handler that binds input. Note this runs **on possession**, not on `_ready`:

```gdscript
extends PawnHandler

func _setup_input_component(input_component: InputComponent) -> void:
	var asc: AbilitySystemComponent = get_pawn_root().get_node("AbilitySystemComponent")
	input_component.bind_action(&"attack", InputComponent.STARTED,
		func() -> void: asc.ability_local_input_pressed(&"attack"))
	input_component.bind_action(&"attack", InputComponent.COMPLETED,
		func() -> void: asc.ability_local_input_released(&"attack"))
```

Working with tags at runtime:

```gdscript
var manager := GameplayTagsManager.get_singleton()
var tag := manager.request_tag(&"Status.Debuff.Burning")

if asc.get_owned_gameplay_tags().has_tag(tag):
	pass    # has_tag is hierarchical: "Status.Debuff" would match too
```

## Local multiplayer

Godot's `Input` singleton merges every device into one state, which is why two people cannot share it. GFGD splits it: a **`LocalPlayer`** is one human at this machine, it owns a **`PlayerInput`** holding that person's own action state, and a central **`InputRouter`** hands each raw event to whoever owns the device it came from.

The split is deliberate about lifetime. A `PlayerController` belongs to a level and dies with it; a `LocalPlayer` is owned by the `GameInstance` and carries the device assignment across `open_level()`. `GameMode.login()` is what marries the two.

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

Set `max_local_players = 2` and `allow_press_to_join = true` on the `GameModeSettings`, then:

```gdscript
extends GameMode

func _init_game(scene_tree: GFGDSceneTree) -> bool:
	var pads: Array = Input.get_connected_joypads()

	# Narrow player 0 off the DEVICE_SLOT_ALL wildcard first, or it swallows
	# every event the second pad needs to join with.
	var first: LocalPlayer = scene_tree.get_local_player(0)
	first.device_slots = PackedInt32Array([pads[0]])

	if pads.size() > 1:
		scene_tree.create_local_player(pads[1])

	return false    # the framework logs everyone in and spawns them

func _can_join(device_slot: int) -> bool:
	return device_slot != PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE
```

`_can_join` is the gate for press-to-join, and it is where "keyboard is not a player" belongs. Any button on an unowned device offers its slot to `GameMode.try_join()`; a player who is already in the game but has no device claims it first, so the same path doubles as "press a button to pick your pad back up" after a disconnect.

Each pawn then reads its own stick through the `InputComponent`, never through `Input`:

```gdscript
func _process(delta: float) -> void:
	var move := _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	get_pawn_root().global_position += Vector3(move.x, 0.0, move.y) * SPEED * delta
```

One `InputMap` serves everyone. You do not duplicate actions per player.

### Split screen

Out of scope by design: the framework records *which* viewport belongs to whom and leaves the layout to you. Set `LocalPlayer.viewport_override` to a `SubViewport` and `PlayerController.set_pawn_camera_node_as_current()` targets it. A `Camera2D` needs nothing else — its `custom_viewport` is set for you. A `Camera3D` has no such property, so it has to physically live inside the `SubViewport`; `LocalPlayer.adopt_camera()` moves it there, and the framework warns rather than reparenting behind your back.

### Networking

There is none yet, and the architecture is the point. `GameMode` is the server's object and refuses to `login()` without `GFGDSceneTree.has_authority()`; `GameState` and `PlayerState` are the ones that have to exist everywhere, which is why `GFGDSceneTree` creates the game state rather than the game mode, and why player states hang off it rather than off their controllers. Adding replication later means adding a `MultiplayerSpawner` to a `game_state_scene`, not moving nodes around.

## Gameplay tags

Tags are declared in `GameplayTagTable` resources — a `tag name -> description` dictionary — and listed in `gameplay_tag_tables`. Several tables merge in listed order; on a duplicate declaration the first one wins and the later one warns.

Parents are implicit: declaring `Ability.Combat.Melee` also registers `Ability` and `Ability.Combat` as usable tags, though they hold no description until some table declares them outright.

Selecting a table in the FileSystem dock opens a tree editor. Double-click a row (or press Rename) to edit a name or a description; renaming a tag rewrites everything beneath it as a single undo step. `GameplayTag` and `GameplayTagContainer` properties get a hierarchical picker in the inspector.

## Gotchas — what the API does not tell you

Signatures are introspectable (`ClassDB.class_get_method_list()`, or <kbd>F1</kbd>). These are the contracts that are not, and each one is a mistake that is easy to make.

- **C# cannot inherit GDExtension classes.** `class MyGameMode : GameMode` will not work — this is a Godot limitation, not a GFGD one. From C#, use the framework compositionally: place the nodes, author the resources, call the methods, connect to the signals. Anything requiring an override (`_init_game`, `_activate_ability`, `_setup_input_component`, …) has to be GDScript.
- **Nothing crashes.** Missing settings, a missing `PawnHandler`, an unloadable tag table — all warn and fall back. A clean Output panel does not mean the project is configured; check for warnings.
- **Virtual hooks are overrides, not signals.** Methods starting with `_` (`_init_game`, `_can_activate_ability`, `_pre_attribute_change`) are overridden in a subclass. Do not try to `connect()` to them.
- **Tags are referenced by name, never by object.** Containers serialize a `PackedStringArray` and resolve through `GameplayTagsManager` on load. Never author an inline `GameplayTag` sub-resource in a `.tres` — it compares equal by name but is not the canonical instance, and it breaks when the tag is renamed.
- **Tag container properties start out null.** `ability_tags`, `granted_tags` and the rest default to `null`, not to an empty container, because ClassDB warns about any live object used as a property default. Every framework method that takes a container accepts null and reads it as empty, and the inspector creates one on the first edit - but your own GDScript has to check `is_valid()` before calling into one.
- **`request_tag()` turns a typo into a real tag.** It never returns null for a well-formed name; it registers the tag, warns, and carries on. `get_tag_source_kind()` returning `TAG_SOURCE_RUNTIME` is how you find these — the editor pickers show them in red.
- **`PawnHandler` is found recursively.** It may be the pawn scene root or any descendant; the first match wins. Two handlers in one scene means the other is silently ignored.
- **Player starts are matched by name glob, not by class.** Any node named `PlayerStart…` counts, so `PlayerStart2` and `PlayerStart_Blue` are picked up without any extra setup. Each is claimed once per level; with more players than starts the framework warns and reuses them by player index. It cannot test whether a start is physically occupied — the level is deliberately still outside the tree when the first players spawn, and physics does not exist there.
- **Input binding happens on possession.** `_setup_input_component` is called by the `PlayerController` when it possesses the pawn — binding from `_ready` is too early and will not fire.
- **Never read `Input` from a pawn script.** It merges every device, so with two local players it moves both pawns at once. Go through the `InputComponent`, which resolves to the owning player's `PlayerInput`. The fallback to `Input` is only there for a component with no owning controller.
- **Raising `max_local_players` without narrowing player 0 leaves nothing for anyone else.** Player 0 starts on `DEVICE_SLOT_ALL` and would claim every event, including the ones a second pad needs to join with. The framework warns about exactly this combination.
- **`PlayerInput` in filtered mode does not see `Input.action_press()`.** Synthetic actions pushed into the engine singleton belong to nobody. Use `PlayerInput.action_press()` on the player you mean. In passthrough mode this does not arise, because every query goes to `Input` anyway.
- **The `InputRouter` listens on `_input`, not `_unhandled_input`.** It has to: if a `Control` swallowed a press but not the release, the action would stay held forever. It never marks input handled, so the GUI is unaffected — but gameplay actions do fire while a `LineEdit` has focus. Disable the `InputComponent` to gate that.
- **A `Controller` subclass overriding `_enter_tree` must call the base implementation.** Godot binds only the most derived override, and the base is what registers the controller with `GFGDSceneTree.get_controllers()`. Forgetting it removes the controller from every world list, silently.
- **A `LocalPlayer` is not a node.** It has no name path and does not appear in the scene tree; hold the reference or ask the `GameInstance` for it by index. It also outlives the level, which is the whole reason it is not one.
- **The attribute formula is `(base + Σ ADD) × Π MULTIPLY`,** and a single `OVERRIDE` beats all of it. `AttributeSet` is duplicated at runtime, so the resource on disk is a template and play never mutates it.
- **Owned tags are reference counted.** Two sources granting the same tag both have to release it. One ability ending does not strip a tag another still grants.
- **`_init_game` runs before the level's nodes only if the framework opened the level.** That is always true for `open_level()` and never true for the main scene, which Godot has already added and whose nodes have run `_ready` first — so the same `_init_game` code behaves differently on the first map than on later ones. Keep the main scene a menu, not a level that depends on the ordering.
- **Do not touch the player from `_init_game`.** The whole point of that hook is that the level has not entered the tree, and the player is spawned into that level - so it has not run `_ready` either. Possession and input binding work, but `AbilitySystemComponent` duplicates its attribute set and grants its startup abilities in `_ready`, so reading an attribute or activating an ability there silently does nothing. Use the game mode's `_ready` instead; it runs after the level is in.
- **Tag tables load before anything else.** `GFGDSceneTree::_initialize` builds `GameplayTagsManager` before creating the `GameInstance`, so tags are always resolvable from `_on_init` onward.
- **The editor binary loads the `.editor` library even when running a game.** `godot --path <project>` from an editor build matches `windows.x86_64.single.debug.editor` in the manifest, not `template_debug`. Rebuilding only the template target and testing that way will appear to change nothing — a real template build needs an export.

## Layout

```
gfgdextension/
  README.md
  gfgd.gdextension     manifest; library paths are relative to this file
  bin/
    windows/  linux/  macos/  android/
```

Library filenames are keyed by platform, architecture, float precision and build target — see `[libraries]` in the manifest for what each slot expects. The editor build provides the tag pickers and the tag table editor; template builds carry runtime only.

## Building

From the repository root, not from here:

```
scons target=editor          # editor build, includes the inspector tooling
scons target=template_debug
scons target=template_release
```

Each build copies its library into `bin/<platform>/` above; only the shared library is copied, not the import libraries that MSVC emits beside it. A CMake build is also provided for IDE use and writes to the same place.

Class reference in `doc_classes/*.xml` at the repository root is compiled into the binary, which is what makes it show up under <kbd>F1</kbd>. Regenerate the skeletons after changing the API with:

```
godot --headless --path <project> --doctool <abs-path-to-repo-root> --gdextension-docs
```

It merges with the existing files, so prose already written is kept.
