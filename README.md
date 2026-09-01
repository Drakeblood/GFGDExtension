# GFGD — Game Framework for Godot

An [Unreal Engine](https://docs.unrealengine.com/)-inspired gameplay framework for **Godot 4.6+**, implemented in C++ as a GDExtension.

## Architecture

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

Everything under `root` is a real node; everything under `GameInstance` is a plain `Object` the scene tree owns and frees itself, deliberately outside the tree so it can outlive a level.

`GFGDSceneTree` is the world: it holds the controller lists — `get_first_player_controller()` and `get_player_controllers()` — and creates the `InputRouter`, `GameState` and `GameMode`. The `GameMode` deliberately keeps no player controller of its own: it is the server's object and does not exist on a client, while the world's list does. `GameInstance` is what outlives a level, which is why the `LocalPlayer`s live there.

### Startup flow

1. `GFGDSceneTree::_initialize` creates the **GameInstance** from `application/game_framework/game_instance_script` and calls its `_on_init`.
2. The current **Level** (main scene root of type `Level`) is located.
3. The **GameState** is created from `GameModeSettings.game_state_scene` and added to the root, before the game mode runs — so player states have somewhere to attach.
4. The **GameMode** is created from `GameModeSettings` (the level's override or `application/game_framework/default_game_mode_settings`) and `_init_game` is called. Unless the script returns `true` ("I handled spawning"), `restart_all_players()` runs: every `LocalPlayer` is logged in, which creates its `PlayerController` and `PlayerState`, instantiates `pawn_scene`, aligns the pawn to a `PlayerStart` node and possesses its `PawnHandler`. A project that never touched local multiplayer has exactly one local player here, created implicitly.
5. On possession the `PlayerController` makes the pawn camera current and calls `PawnHandler._setup_input_component`, where you bind input actions.

All hooks degrade gracefully — missing settings produce warnings and sensible fallbacks, never crashes.

## Project settings

| Setting | Meaning |
|---|---|
| `application/run/main_loop_type` | Must be `GFGDSceneTree` to enable the framework. |
| `application/game_framework/game_instance_script` | Script extending `GameInstance`. |
| `application/game_framework/default_game_mode_settings` | A `GameModeSettings` resource (`game_mode_script`, `pawn_scene`, `player_controller_scene`, `max_local_players`, …). |
| `application/game_framework/gameplay_tag_tables` | `GameplayTagTable` resources declaring the project's gameplay tags. Merged in listed order; on a duplicate the first table wins. Parent tags are registered implicitly. |

A minimal `project.godot`:

```ini
[application]

run/main_loop_type="GFGDSceneTree"
game_framework/game_instance_script="res://scripts/game_instance.gd"
game_framework/default_game_mode_settings="res://resources/default_game_mode_settings.tres"
game_framework/gameplay_tag_tables=PackedStringArray("res://tags/default_tags.tres")
```

### Scene requirements

The pawn scene named by `GameModeSettings.pawn_scene` must contain a **`PawnHandler`** node — either as the scene root, or anywhere below it (the first one found in a recursive search wins). Without it the pawn spawns but cannot be possessed, and the framework warns instead of failing. A camera and an `AbilitySystemComponent` are optional siblings; the camera is made current on possession.

## Local multiplayer

Godot's `Input` singleton merges every device into one state, so two people cannot share it. GFGD splits it: a **`LocalPlayer`** is one human at this machine and owns a **`PlayerInput`** holding their own action state, while a single **`InputRouter`** at the scene root hands each raw event to whoever owns the device it came from. `InputComponent` then reads that player's state instead of the global singleton, so a pawn only ever sees its own pad.

**Existing single player projects are unaffected.** The first local player is created holding `PlayerInput.DEVICE_SLOT_ALL`, and in that state `PlayerInput` forwards every query straight to `Input` — same behaviour, same code path.

A device slot is a plain `int`: a joypad index, `DEVICE_SLOT_KEYBOARD_MOUSE`, `DEVICE_SLOT_ALL`, or `DEVICE_SLOT_NONE`. One player may own several. Events are classified by event class rather than by `InputEvent.device`, because a synthesized `InputEventAction` reports device `0` and would collide with joypad 0.

Two pads with the keyboard ignored, using `max_local_players = 2` and `allow_press_to_join = true`:

```gdscript
func _init_game(scene_tree: GFGDSceneTree) -> bool:
	var pads: Array = Input.get_connected_joypads()
	# Narrow player 0 off the DEVICE_SLOT_ALL wildcard, or it swallows the events
	# the second pad needs to join with.
	scene_tree.get_local_player(0).device_slots = PackedInt32Array([pads[0]])
	if pads.size() > 1:
		scene_tree.create_local_player(pads[1])
	return false

func _can_join(device_slot: int) -> bool:
	return device_slot != PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE
```

Split screen is deliberately left to the project: set `LocalPlayer.viewport_override` and the framework points the right camera at the right viewport, but it does not build the layout.

Networking is not implemented, but the shape is in place for it. `GameMode` is the server's object and refuses to `login()` without `GFGDSceneTree.has_authority()`; `GameState` and `PlayerState` are the ones that must exist on every peer, which is why the scene tree creates the game state rather than the game mode, and why player states hang off it rather than off their controllers.

## Gameplay Ability System

- **GameplayTag / GameplayTagContainer** — hierarchical `StringName` tags; `"A.B.C"` matches the parent query `"A.B"`. Containers serialize as a `PackedStringArray` (`tags`) and resolve through the `GameplayTagsManager` singleton.
- **GameplayTagTable** (Resource) — a `tag name -> description` dictionary; the authoring source for tags. Split them across as many tables as you like (per feature, or shipped with an addon) and list each one in `gameplay_tag_tables`. `GameplayTagsManager` flattens them into a single lookup and tracks where every tag came from: `get_tag_source_kind()` reports `TAG_SOURCE_TABLE` (declared), `TAG_SOURCE_IMPLICIT` (a parent implied by a longer tag) or `TAG_SOURCE_RUNTIME` (never declared — `request_tag()` conjured it, which usually means a typo or a stale asset), with `get_tag_source_path()` naming the declaring table.
- **GameplayAbility** (Resource) — identified by `ability_name`, optionally bound to an input action via `input_action_name`. Tag gating: `activation_required_tags`, `activation_blocked_tags`, `activation_owned_tags`, `block/cancel_abilities_with_tag`. Script hooks: `_on_give_ability`, `_can_activate_ability`, `_activate_ability`, `_end_ability`, `_input_pressed/_released`.
- **GameplayEffect** (Resource) — `INSTANT` (mutates base values), `HAS_DURATION`/`INFINITE` (aggregate onto current values, grant `granted_tags`, optionally periodic via `period`). Carries `AttributeModifier`s (`ADD`, `MULTIPLY`, `OVERRIDE`) plus application tag requirements.
- **AttributeSet** (Resource) — a `name -> base value` dictionary; current values are computed as `(base + Σ add) × Π multiply` (an `OVERRIDE` wins). Hooks `_pre_attribute_change` (clamping) / `_post_attribute_change`, signals `attribute_changed`, `base_value_changed`.
- **AbilitySystemComponent** (Node) — give/activate/cancel abilities, `apply_gameplay_effect_to_self`, owned tag counting, exported `startup_abilities`, `startup_effects` and `attribute_set` (duplicated at runtime). Signals: `ability_activated`, `ability_ended`, `owned_tag_changed`, `gameplay_effect_applied/removed`, `attribute_changed`.

## Save games

`ProjectStatics.save_game/load_game` serialize a `SaveGame` resource to `user://saves/` (optionally encrypted). By default every script variable is serialized to JSON; override `_to_json` / `_from_json` for custom formats.

## Building

Requirements: [SCons](https://scons.org/), a C++17 compiler, and the `godot-cpp` submodule:

```sh
git submodule update --init --recursive
scons target=template_debug          # or target=template_release / editor
```

The library is written to `bin/<platform>/` and copied into `project/addons/gfgdextension/bin/<platform>/`, where `project/addons/gfgdextension/gfgd.gdextension` (entry symbol `gfgd_library_init`) picks it up. Dropping that one `addons/gfgdextension/` folder into another project is the whole install. A CMake build is also provided for IDE use.
