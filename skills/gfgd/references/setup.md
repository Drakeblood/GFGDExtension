# Setting a project up on GFGD

## Installing

Copy the `gfgdextension` folder into the project's `addons/` directory and restart Godot.

There is **no plugin to enable** and no `plugin.cfg`: `gfgd.gdextension` is picked up wherever it
sits (library paths inside it are relative), and the editor integration registers itself from the
extension. Nothing appears under Project Settings → Plugins, and nothing is written to
`project.godot` for the extension itself.

To confirm it actually loaded: `.godot/extension_list.cfg` should list
`res://addons/gfgdextension/gfgd.gdextension`, and `ClassDB.class_exists("World")` should be true.
An empty `extension_list.cfg` means the editor has not rescanned or the binary for this
platform/target slot is missing.

**A project that already has C# classes named `GameInstance`, `GameMode`, `Controller`,
`PlayerController`, `Level`, `LocalPlayer`, `InputComponent`, `SaveGame`, `ProjectStatics`,
`GameplayTag*` or `Assert*` and marked `[GlobalClass]` will collide on ClassDB names.** One of the
two has to go; they cannot both be registered.

## Project settings

In Project Settings → Application → Game Framework:

| Setting | Meaning |
|---|---|
| `application/run/main_loop_type` | Must be `World`. **Required** — nothing else starts the framework. |
| `application/game_framework/game_instance_script` | Script extending `GameInstance`. |
| `application/game_framework/default_game_mode` | A scene whose root is a `GameModeBase`. |
| `application/game_framework/default_port` | Port your menus and helpers default to. Defaults to `7777`. |
| `application/game_framework/gameplay_tag_tables` | `GameplayTagTable` resources, in merge order. Empty is fine to start with. |
| `application/game_framework/save_encryption_key` | Key for encrypted saves. **Ships with a default that is public in the GFGD repository — replace it before release.** |

The extension registers all of these on load, so they appear in the editor UI. Godot does not write
out settings that still hold their default, so a fresh install adds nothing to `project.godot` until
you change something.

`main_loop_type` is an engine setting under `application/run/`, not under
`application/game_framework/` with the rest. It is the one that is easy to miss, and missing it
means the framework simply never starts — with no error.

## What a first level needs

1. **A game mode scene.** Root node is a `GameModeBase` (or your subclass's script on a
   `GameModeBase` root). It has to be a scene, not a resource, because the scenes it names are set
   in the inspector. Point `application/game_framework/default_game_mode` at it.
2. **A level scene.** Root node is a `Level` (or your subclass). Set `game_mode_override` when this
   level wants a different game mode than the project default.
3. **A pawn scene.** Any scene with a `Pawn` node somewhere inside it — the root or any descendant.
   Point the game mode's `default_pawn_scene` at it. In 2D, give its root a `z_index` above what the
   level draws: pawns are parented to `/root/Pawns`, which sits *before* the level in root's child
   list, so the level otherwise paints straight over them (see `gotchas.md`).
4. **`PlayerStart2D` / `PlayerStart3D` nodes** in the level, where pawns should appear.
5. **`run/main_scene`** pointing at a scene containing the level.

A menu level with no player: give it a game mode whose `_init_game` returns `true`. That suppresses
the spawn entirely, so the menu runs with no player rather than with an empty one.

## Addon layout

```
addons/gfgdextension/
  gfgd.gdextension     manifest; library paths are relative to this file
  README.md            prose overview for a human
  VERSION.txt          which build this is (gfgd, commit, godot-cpp, api_version)
  bin/
    windows/  linux/  macos/  android/
  doc_classes/         per-class XML reference   (.gdignore)
  src/                 the C++ sources           (.gdignore)
  skills/              agent skills              (.gdignore)
```

The three `.gdignore`d folders are invisible to Godot: never scanned, never imported, never
exported, and not `load()`able. Read them with ordinary file tools. **The `.gdignore` files live in
those subfolders and never in the addon root** — a root `.gdignore` would hide `gfgd.gdextension`
itself and the extension would not load.

Library filenames are keyed by platform, architecture, float precision and build target — see
`[libraries]` in the manifest for what each slot expects. The editor build provides the tag pickers
and the tag table editor; template builds carry runtime only.

## Migrating from an autoload / plain-SceneTree architecture

| Old shape | GFGD |
|---|---|
| Autoload singleton holding cross-level state | `GameInstance` (created from a project setting, outside the tree) |
| Autoload or level script spawning the player | `GameModeBase` — it spawns and possesses on its own |
| Hard-coded spawn `Marker2D`/`Position2D` | `PlayerStart2D` / `PlayerStart3D` |
| A settings `Resource` naming the player/pawn scenes | The game mode **scene**'s inspector properties |
| Static event-bus class with string channels | `GameplayMessageRouter`, channels keyed by tag name |
| Hand-rolled object pool | `NodePool` |
| `Input.is_action_pressed` in the player script | `InputComponent`, bound in `_setup_input_component` |
| Own touch joystick `Control` | `PlayerVirtualJoystick` / `PlayerTouchButton` |
| JSON save helper | `SaveGame` + `ProjectStatics.save_game` / `load_game` |
| Tag list in an `.ini`/`.cfg` | `GameplayTagTable` resources listed in `gameplay_tag_tables` |

Two things to expect on the way, whatever the old architecture was:

- **A game mode settings `Resource` becomes a scene.** GFGD names the pawn, controller, game state
  and player state scenes from the game mode scene's own inspector, so the old settings resource has
  no replacement — delete it rather than porting it.
- **Every script a scene references must still exist at some path.** Deleting scripts before writing
  their replacements leaves scenes that will not load and `.tres` files whose values are lost on the
  next save. Write a minimal stub at the new path first, extending the right base class and — for a
  `Resource` — **repeating the same `@export` names**, then fill it in.

For a full account of a real port, including the language traps and how to verify it, see
`references/migrating-from-csharp.md`.
