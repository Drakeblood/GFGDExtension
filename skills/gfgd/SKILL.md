---
name: gfgd
description: Use when working in a Godot project that has the GFGD game framework installed (addons/gfgdextension, an Unreal-inspired GDExtension). Covers World as the main loop, GameModeBase, GameStateBase, PlayerState, Controller/PlayerController/AIController, Pawn and possession, LocalPlayer, PlayerInput, InputRouter, InputComponent, PlayerStart2D/3D, NetDriver and replication, AbilitySystemComponent, GameplayAbility, GameplayEffect, AttributeSet, GameplayTag/Container/Table/Manager, NodePool, GameplayMessageRouter, PlayerVirtualJoystick, PlayerTouchButton, and the application/game_framework/* project settings. Read this before writing or debugging any script that touches those classes, before setting up a level, game mode or pawn, and whenever a GFGD feature silently does nothing.
---

# GFGD

An Unreal-inspired game framework for Godot 4.6+, built as a GDExtension. It replaces the
"autoloads plus a scene tree" pattern with explicit lifetimes: things that belong to a level die
with it, things that belong to the application do not.

**Where the truth lives — check these before guessing:**

| Question | Source |
|---|---|
| Exact signature, enum value, signal parameters | `addons/gfgdextension/doc_classes/<Class>.xml` |
| What a method actually does | `addons/gfgdextension/src/**` (C++ sources ship with the addon) |
| Which build is installed | `addons/gfgdextension/VERSION.txt` |
| Prose overview for a human | `addons/gfgdextension/README.md` |
| Live introspection | `ClassDB.class_get_method_list("World")` via the Godot MCP toolkit's `execute_code` |

`doc_classes/`, `src/` and `skills/` each carry a `.gdignore`, so Godot never scans, imports or
exports them. Read them with ordinary file tools — not through `load()`/`preload()`, which
`.gdignore` deliberately blocks. See `references/api-lookup.md` for the fast paths.

## The runtime tree

```
root
 ├─ NetDriver              connections, travel, mirroring
 ├─ GameState              what every peer may know; exists on clients too
 │   └─ PlayerState...     per-player name, id, score, ping
 ├─ Players
 │   └─ PlayerController...  server: all of them; client: only its own
 ├─ Pawns
 │   └─ Pawn<id>           the node a player drives
 │       ├─ Pawn           makes the parent possessable, declares input bindings
 │       ├─ Camera2D/3D    made current on possession
 │       └─ AbilitySystemComponent
 ├─ InputRouter            sends each raw event to the player owning that device
 ├─ Level                  scene root node, may override the game mode
 │   └─ PlayerStart2D/3D...  where a pawn is placed
 └─ GameMode               per-level rules and default scenes; SERVER ONLY

Held by World, outside the scene tree:
 GameInstance             application-lifetime object (created from a project setting)
  └─ LocalPlayer...       one per human at this machine; survives a level change
      └─ PlayerInput      that human's own action state, filtered by device
```

`NetDriver`, `InputRouter`, `Players` and `Pawns` are built once and survive a level change.
`Level`, `GameState` and `GameMode` are rebuilt with each level. Everything under `GameInstance`
is a plain `Object`, deliberately outside the tree so it can outlive a level.

Controllers, player states and pawns hang off fixed parents rather than off the level, and are
named after a server-assigned `player_id`, because their node path is the address every peer
refers to them by.

## Startup order

```
[Level] _enter_tree / _ready          the level AND EVERYTHING IN IT is up first
GameState created and added
GameMode._init_game                   prepare; spawns and possesses the players
GameMode._ready                       everything live
Level._init_level                     the first hook that can see all of it
```

**Prepare in `_init_game`, play in `_ready`.** The framework logs local players in and possesses
their pawns on its own. Override `_init_game` only to prepare something first; read the player off
the world from `_ready`. Returning `true` from `_init_game` suppresses the spawn entirely — that is
how a menu level runs with no player at all.

**Read that first line carefully: a level's children are ready before the game mode exists.** A HUD,
a camera, a spawner, a touch control — anything sitting in the level scene — has its `_ready` run
before there is a game mode to ask and before any pawn exists. `get_game_mode()` returns null there
and the usual null guard swallows it, so the node simply never finds what it wanted, for the whole
session, in silence. Use `Level._init_level`, or better, resolve the reference on first use so it
also survives a respawn. This is the single most expensive mistake in this document.

The player list lives on `World`, not on `GameMode`: `get_first_player_controller()`,
`get_player_controllers()`. `GameMode` does not exist on a client; `World` does.

## The rules that bite hardest

The full list is in `references/gotchas.md` — read it before debugging anything that "silently does
nothing". These cause the most wasted time:

1. **Nothing crashes.** Missing settings, a missing `Pawn` node, an unloadable tag table — all warn
   and fall back. A clean Output panel does not mean the project is configured. **Check warnings.**
2. **A level's children are ready before the game mode, the pawn or possession exist.** Resolve any
   reference reached through the game mode lazily, on first use — never in `_ready`. Inside one
   scene, a child's `_ready` also runs before its parent's, which is how a child reads an attribute
   its parent has not defined yet and gets `0.0`.
3. **`ProjectStatics.load_game` returning null does not mean "no save".** Only `LOAD_NOT_FOUND`
   does; ask `get_last_load_result()`. The framework blocks `save_game` on a slot that failed to
   load, so the classic "null means first run, save over it" bug now fails loudly (`ERR_LOCKED`)
   instead of destroying a profile — but a game that ignores the result still runs on defaults.
4. **In 2D, a pawn draws behind the level.** `/root/Pawns` precedes the level in root's child list,
   so level art paints over it. Give the pawn scene root a `z_index`.
5. **C# cannot inherit GDExtension classes.** Anything needing an override must be GDScript. This
   is a Godot limitation, not a GFGD one.
6. **Virtual hooks are overrides, not signals.** Methods starting with `_` (`_init_game`,
   `_setup_input_component`, `_can_activate_ability`) are overridden in a subclass. Never
   `connect()` to them.
7. **Input binding happens on possession**, in `_setup_input_component`. Binding from `_ready` is
   too early and will not fire.
8. **Never read `Input` from a pawn script.** It merges every device; with two local players it
   moves both pawns, and on a server it is nobody's input. Go through `InputComponent`.
9. **A `Controller` subclass overriding `_enter_tree` must call the base implementation** — the
   base is what registers the controller with `World`. Forgetting it removes the controller from
   every world list, silently.
10. **`GameMode` is null on a client, on purpose.** Use `World.get_game_state()` for anything a
    client reads; keep rules behind `has_authority()`.
11. **Message router listeners run in reverse registration order.** Never encode a dependency in
    registration order; broadcast a second channel when the result actually exists.
12. **Tag container properties start out `null`, not empty.** Check `is_valid()` before calling into
    one from your own GDScript.
13. **Every scene a game mode names has to be its own file.** An inline-built scene cannot be named
    to another machine.
14. **The editor binary loads the `.editor` library even when running a game.**
    `godot --path <project>` from an editor build matches the `.editor` manifest slot, not
    `template_debug`. Testing a template rebuild that way appears to change nothing.

## Minimal integration

A game mode reading an attribute off the spawned player:

```gdscript
extends GameModeBase

func _ready() -> void:
	var pawn: Pawn = get_world().get_first_player_controller().get_pawn()
	var asc: AbilitySystemComponent = pawn.get_pawn_root().get_node("AbilitySystemComponent")
	print(asc.get_attribute_value(&"health"))
```

A pawn binding input — this runs **on possession**, not on `_ready`:

```gdscript
extends Pawn

func _setup_input_component(input_component: InputComponent) -> void:
	input_component.bind_action(&"attack", InputComponent.STARTED, _on_attack)
```

Movement that is correct offline and online alike:

```gdscript
func _process(delta: float) -> void:
	if not has_authority():
		return    # the server decides where this pawn is; here it is only shown
	var move := _input_component.get_vector(&"left", &"right", &"forward", &"back")
	add_movement_input(Vector3(move.x, 0.0, move.y))
	get_pawn_root().position += consume_movement_input_vector() * SPEED * delta
```

## Where to read next

| Task | File |
|---|---|
| Installing GFGD, project settings, a new project's first level | `references/setup.md` |
| Anything silently doing nothing; before any debugging session | `references/gotchas.md` |
| Game mode scenes, levels, player starts, spawning, possession | `references/game-mode-and-levels.md` |
| Input bindings, local multiplayer, device slots, touch, split screen | `references/input.md` |
| Hosting, joining, travel, replication, authority vs ownership | `references/networking.md` |
| Walking, falling, jumping, movement modes, tuning how a character feels | `references/movement.md` |
| Abilities, effects, attributes | `references/ability-system.md` |
| Tags, tag tables, the tag editor | `references/gameplay-tags.md` |
| `NodePool`, `GameplayMessageRouter`, saves | `references/pooling-and-messaging.md` |
| Porting an existing game (especially from C#) onto GFGD | `references/migrating-from-csharp.md` |
| Getting an exact signature fast | `references/api-lookup.md` |

## Class map

| Class | Role |
|---|---|
| `World` | Custom `SceneTree`, the entry point. Owns startup order, the level and the session. |
| `NetDriver` | Connections, travel, and mirroring what the server creates. |
| `GameInstance` | Application-lifetime object. State that survives a level change. |
| `Level` | Playable scene root. May override the game mode. |
| `GameModeBase` | Per-level rules and the default scenes everything is built from. Server only. |
| `GameStateBase` | What every peer is allowed to know, including the player list. |
| `PlayerController` / `Controller` / `AIController` | Possesses a pawn, owns its input. Dies with the level. |
| `PlayerState` | Per-player data everyone sees: name, id, score, ping. |
| `Pawn` | Makes a scene possessable. Where input bindings are declared. |
| `PlayerStart2D` / `PlayerStart3D` | Where the game mode places a pawn. |
| `LocalPlayer` | One human at this machine. Owned by `GameInstance`, survives a level change. |
| `PlayerInput` | That human's own action state, filtered by the devices they own. |
| `InputRouter` | Sends each raw event to the player who owns the device it came from. |
| `InputComponent` | Binds input actions to callables, reading the owning player's `PlayerInput`. |
| `AbilitySystemComponent` | Abilities, owned tags, effects, attributes. |
| `GameplayAbility` / `GameplayEffect` | Authorable ability and effect resources. |
| `AttributeSet` / `AttributeModifier` | Named numeric attributes and the changes applied to them. |
| `GameplayTag` / `GameplayTagContainer` | A hierarchical tag, and a set of them. |
| `GameplayTagTable` / `GameplayTagsManager` | Tag declarations, and the merged runtime lookup. |
| `SaveGame` / `ProjectStatics` | Save state and the helpers that read and write it. |
| `NodePool` | Recycles nodes for anything spawned by the hundred. |
| `GameplayMessageRouter` | Fire-and-forget bus, keyed by tag name. |
| `PlayerVirtualJoystick` / `PlayerTouchButton` | Touch controls that drive one player's `PlayerInput`. |
| `Assert` / `AssertionException` / `AssertionMessages` | Assertion helpers exposed to GDScript. |
