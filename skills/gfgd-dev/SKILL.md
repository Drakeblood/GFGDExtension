---
name: gfgd-dev
description: Use when working inside the GFGD GDExtension repository itself - the C++ sources under src/, SConstruct, CMakeLists.txt, CMakePresets.json, build_all.ps1, pack_addon.ps1, doc_classes/*.xml, or register_types.cpp. Covers building the editor/template_debug/template_release targets, the godot-cpp submodule, adding or changing a registered class and its _bind_methods, adding a project setting, regenerating the class reference with --doctool --gdextension-docs, and packaging the addon for a game project. This is the extension-authoring skill; for using GFGD from a game, use the "gfgd" skill instead.
---

# Working on GFGD itself

An Unreal-inspired game framework for Godot 4.6+, built as a GDExtension. Library name `GFGD`,
entry symbol `gfgd_library_init`, `api_version` 4.7, `compatibility_minimum` 4.6.

```
SConstruct  CMakeLists.txt  CMakePresets.json  build_all.ps1  pack_addon.ps1  methods.py
doc_classes/       37 XML class-reference files, compiled into the binary
godot-cpp/         submodule
src/
  register_types.cpp/.h   entry point: class registration + project settings
  core/                   assert, assertion_exception, assertion_messages, project_statics
  movement/               movement_types, movement_utils, movement_mode(+_transition),
                          layered_move, movement_backend, character_movement_component,
                          modes/{walking,falling,flying}_mode
  framework/              world, game_instance, local_player, player_input, input_router,
                          input_component, game_state_base, player_state, game_mode_base,
                          level, controller, player_controller, ai_controller, pawn,
                          net_driver, net_replication, player_start_2d/3d, node_pool,
                          player_virtual_joystick, player_touch_button,
                          gameplay_message_router, save_game
  gameplay_tags/          gameplay_tag(+_container, _count_container, _table), _manager
  ability_system/         attribute_set, attribute_modifier, gameplay_effect,
                          active_gameplay_effect, gameplay_ability, ability_system_component
  editor/                 #ifdef TOOLS_ENABLED only
  gen/                    doc_data.gen.cpp (generated, gitignored)
project/           the demo Godot project; builds install the library into its addon
bin/               root output; the addon copy under project/ is what ships
```

## The three targets

**One SCons invocation builds exactly one library**, for one `platform` x `arch` x `target`.

| | `editor` | `template_debug` | `template_release` |
|---|:---:|:---:|:---:|
| `TOOLS_ENABLED` (the tag editor, pickers, inspector) | yes | | |
| `DEBUG_ENABLED` | yes | yes | |
| optimisation | `speed_trace` | `speed_trace` | `speed` |
| class reference compiled in | yes | yes | |
| debug symbols (CMake presets) | yes | **no** | no |
| loaded by | the editor | an export with debug | a release export |

### `GODOTCPP_TARGET` and `CMAKE_BUILD_TYPE` are different axes

Confusing them is expensive. `GODOTCPP_TARGET` decides `DEBUG_ENABLED`, `TOOLS_ENABLED` and the
optimisation level — what the library *is*. `CMAKE_BUILD_TYPE` decides one thing only: **whether
debug symbols are emitted.** godot-cpp ties them to the build type in
`cmake/common_compiler_flags.cmake`:

```cmake
set(DEBUG_SYMBOLS "$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>")
```

`template_debug` does **not** have to mean "compiled with `-g`". Under MinGW the DWARF lands
*inside* the `.dll` rather than beside it, so a `Debug` build type took that target to 60.9 MB on
Windows and 30 MB per Android ABI — for symbols nobody attaches a debugger to. The presets now
build `t-template_debug` as `Release`; only `t-editor` keeps its symbols, because the editor build
is the one you actually step through.

Note that `Release` also defines `NDEBUG`, so `assert()` is compiled out. GFGD does not use it —
`src/` asserts through `ERR_FAIL_COND_MSG` and the `Assert` class — but keep it in mind before
adding one.

**SCons emits no symbols at all by default**, for any target: `tools/godotcpp.py` sets
`env["debug_symbols"]` from `env.dev_build`, which is false unless you pass `dev_build=yes` or
`debug_symbols=yes`. That is why a SCons build has always come out much smaller than a CMake one
here — same sources, different default.

**A desktop platform wants all three.** Android wants two, twice — nobody runs an editor there.

```sh
scons platform=windows arch=x86_64    target=editor
scons platform=windows arch=x86_64    target=template_debug
scons platform=windows arch=x86_64    target=template_release
scons platform=linux   arch=x86_64    target=editor          # and arch=arm64 if needed
scons platform=macos   arch=universal target=editor          # universal is REQUIRED on macOS
scons platform=android arch=arm64     target=template_debug
scons platform=android arch=arm64     target=template_release
scons platform=android arch=x86_64    target=template_debug
scons platform=android arch=x86_64    target=template_release
```

Or through CMake, which is what `build_all.ps1` drives:

```powershell
./build_all.ps1              # every preset for the host platform
./build_all.ps1 android
./build_all.ps1 windows -Targets windows-editor
./build_all.ps1 macos -List
```

Presets are `<platform>-<target>` — `windows-editor`, `windows-template_debug`,
`windows-template_release`, the same for `linux` and `macos`, and
`android-{arm64,x86_64}-template_{debug,release}`. Ninja generator, so on Windows run from a
**Developer PowerShell for VS** or a Rider terminal — Ninja does not locate MSVC on its own. Android
needs `ANDROID_HOME`/`NDK_ROOT` pointing at an SDK with the NDK.

Cross-compiling only works for Android. Linux and macOS have to be built on their own hosts.

## Build gotchas already fixed — do not undo them

These are in `SConstruct` and `CMakeLists.txt` with comments; each one cost real debugging time.

- **`SHOBJSUFFIX` is prefixed with `env["suffix"]`.** godot-cpp keys object files through
  `OBJSUFFIX`, but the shared-object suffix is a separate variable that only MSVC derives from it.
  Under MinGW or the Android NDK every target would otherwise write to the same `world.o`, so
  building the editor after a template silently invalidates everything and recompiles the world.
- **`SHLIBPREFIX` is forced** — `""` on Windows, `"lib"` everywhere else. SCons takes it from the
  *host*, so cross-compiling from Windows drops the `lib` that Android, Linux, macOS and web expect
  and that `gfgd.gdextension` names. Android is the one that bites: its exporter packages a
  zero-byte entry for a library it cannot find and reports nothing, so the APK installs and then
  fails to load the extension at runtime.
- **`.dev` and `.universal` are stripped from the filename.** `.universal` in particular is what the
  manifest expects to be absent; a per-arch macOS build comes out named something nothing looks for.
- **CMake must set `TOOLS_ENABLED` itself** when `GODOTCPP_TARGET STREQUAL "editor"`. SCons gets it
  from godot-cpp; CMake does not, and without it an "editor" build is a `template_debug` build under
  another name — it will load, and the tag editor simply will not be there.
- **Only the shared library is installed into the addon.** MSVC also emits an import library and an
  export file next to it; those are link-time artifacts and must not ship.

## Adding or changing a class

1. Header and implementation under the right `src/<domain>/` folder. Includes are written relative
   to `src/`, which is on `CPPPATH`.
2. Declare the GDScript-visible surface in `_bind_methods()` — **if it is not in `_bind_methods`,
   GDScript cannot call it.** Method names, argument names and defaults, signals, properties with
   their setter/getter pairs, and enum constants all live there.
3. `#include` it and add `GDREGISTER_CLASS(...)` in `src/register_types.cpp`, in the
   `MODULE_INITIALIZATION_LEVEL_SCENE` block. Editor-only classes go in the
   `MODULE_INITIALIZATION_LEVEL_EDITOR` block under `#ifdef TOOLS_ENABLED`, use
   `GDREGISTER_INTERNAL_CLASS`, and an `EditorPlugin` also needs
   `EditorPlugins::add_by_type<>` there plus `remove_by_type<>` in `uninitialize_gdextension_types`.
4. Sources are globbed recursively from `src/` (skipping `src/gen`), so no build file lists them —
   a new file is picked up automatically.
5. Add a `doc_classes/<Class>.xml` (see below).
6. A C++ static singleton exposed as a static ClassDB method — `GameplayTagsManager`,
   `GameplayMessageRouter` — must be torn down in `uninitialize_gdextension_types`. GFGD registers
   **no Godot autoloads and no `Engine::register_singleton()`**; keep it that way.

## Adding a project setting

`register_gfgd_settings()` in `src/register_types.cpp`, via the local `register_gfgd_setting`
helper, which sets the default, the property info (type, hint, hint string) and marks it basic.
Everything lives under `application/game_framework/` — except `application/run/main_loop_type`,
which is an engine setting the project sets to `World` by hand.

Godot does not write out a setting still holding its default, so a fresh install adds nothing to
`project.godot` until the user changes something. Document any new setting in the addon README's
install table and in the `gfgd` skill's `references/setup.md`.

## The class reference

`doc_classes/*.xml` is compiled into the `editor` and `template_debug` binaries by
`GodotCPPDocData`, which is what makes classes show up under F1 — and, since `pack_addon.ps1` copies
the same files into the addon, what an agent reads for exact signatures.

Regenerate the skeletons after changing the API:

```sh
godot --headless --path <project> --doctool <abs-path-to-repo-root> --gdextension-docs
```

It **merges** with the existing files, so prose already written is kept. Write real
`<description>` text for methods, signals, properties and constants — an empty one is a promise the
README makes and does not keep.

## When a project using GFGD finds a gotcha

Framework knowledge lives in four places on purpose, and a fact that lands in only one of them is
half-documented. Write it into all four that apply:

| Where | What belongs there |
|---|---|
| `doc_classes/<Class>.xml` | The authoritative statement, on the method or class it is about. This is what F1 and every agent read first. |
| `skills/gfgd/references/*.md` | The worked version — why it happens, the code shape that avoids it. |
| `skills/gfgd/SKILL.md` | Only if it is one of the handful that cost the most time; the numbered list is a triage list, not an index. |
| `project/addons/gfgdextension/README.md` | The *Gotchas* section, for a human reading before they start. |

Two rules for writing them: **describe the symptom, not just the cause** — most of these produce a
running game with clean output, and the reader is searching by what they see. And **check whether an
existing entry is now wrong** rather than adding a second one next to it; a description that says
delivery order is "unspecified" when the code guarantees reverse order is worse than no description.

Prefer fixing the framework where it is cheap. Documenting a sharp edge is the fallback, not the
goal.

## Packaging

```powershell
# repack only - no compiler runs. After editing doc_classes, skills or the README.
./pack_addon.ps1

# build first, then pack. One platform is one full set of targets.
./pack_addon.ps1 -Build windows,android

# the whole cycle, straight into a game project
./pack_addon.ps1 -Build windows,android `
    -Destination "D:\Godot Projects\SomeGame\addons\gfgdextension" -InstallSkills
```

`-Build` hands each platform to `build_all.ps1` before anything is assembled, so a failed compile
stops the script rather than shipping an addon around stale libraries. The builds drop their output
straight into the addon's `bin/`, which is why packing has nothing to copy afterwards.

It assembles `project/addons/gfgdextension/` from the repo: `doc_classes/`, `src/` (sources only —
never `.o`/`.os` or `src/gen`), `skills/`, a `VERSION.txt` stamp, and an empty **`.gdignore` in each
of those three subfolders**.

**The `.gdignore` goes in the subfolders and never in the addon root** — a root `.gdignore` would
hide `gfgd.gdextension` itself and the extension would not load at all. Godot does not export
non-resource files by default, but it does scan them into the FileSystem dock; the `.gdignore` makes
both guarantees unconditional and costs nothing.

Binaries are gitignored in both `bin/` trees (folder structure kept via `.gitkeep`), so the addon in
git is text only and the libraries are build output.

## Design notes kept for later

`references/client-prediction.md` — the plan for client-side prediction, written down while the
movement work was fresh. Read it before touching `PlayerController`'s input path or
`MovementBackend`; several hooks in `src/movement/` exist only to serve it, and the one rule that
keeps them valid (nothing in a tick reads the body's transform, nothing that survives a tick lives on
the component) fails silently rather than loudly when broken.

## Testing a change

There is no test runner. `Assert`/`AssertionException`/`AssertionMessages` are an assertion API
exposed to GDScript, not a harness.

What there is, is `project/tests/` — self-checking `SceneTree` scripts, each run on its own and each
printing `PASS`/`FAIL` per check and exiting non-zero if anything failed:

```
godot --headless --path project --script res://tests/test_perch.gd
```

`test_topdown.gd` pins the top-down playground and, with it, every claim the movement reference's
"Top-down 2D" section makes: the same distance in all eight directions with nothing pulling sideways,
walls that stop at exactly the capsule radius and deflect rather than grab, a `WaterVolume2D` acting
as a pure current once gravity is 0, and the two settings a side-on game gets right that this one must
not - `can_crouch`, and a layered move's mix mode.

`test_playground_3d.gd` and `test_playground_2d.gd` pin the demo's two side-on maps: it drops a pawn at each station in turn and checks
that the step over `max_step_height` still blocks and is still clearable by a jump, the tunnel still
needs a crouch, the shuttle still carries, the pool still starts swimming and the updraft still starts
flying. Raising either tunnel's ceiling turns its map's test red, which is the point - a map is only
worth having if it keeps meaning what its signs say.

`test_root_motion.gd` pins root motion in both dimensions: the authored speed drives the character,
facing turns the step in 3D and does not in 2D, and a wall still stops it because the mode is what
sweeps. `test_perch.gd` pins both halves of perching: the furthest past a ledge edge the solver still calls
the floor walkable has to equal the perch footprint (`capsule_radius - perch_radius_threshold`,
floored at 0.11), a walking character with perching on has to leave the ledge sooner than one with it
off, and — over a floor slot thinner than the footprint — the narrow probe's answer has to be adopted
back. That last half was dead code until the test was written for it. **Before trusting a new test here, prove it can fail** — sabotage the code it covers, watch it
go red, revert. All three were written that way: stubbing
`should_compute_perch_result` to `return false` turns three of `test_perch.gd`'s six checks red and
dropping the surface probe from `compute_floor_dist_with_radius` turns a fourth, and
making `RootMotionLayeredMove::on_start` bail when `get_component()` is null - the 3D-only behaviour
it used to have - turns three of `test_root_motion.gd`'s seven red while leaving the 3D four green.

Physics runs in `--headless`, so this is a real check and not a smoke test. Build the scene in
`_initialize()`, drive input from `_physics_process` (a `SceneTree` subclass's runs *before* the node
tree's, so input set there lands on the tick about to run), and assert on printed trajectories. Query
the solver directly — `find_floor_at()`, `sweep_from()`, `slide_move_from()` all take the transform
to work from — when a static answer is sharper than a dynamic one.

`project/` is a working demo: 4 game modes (`demo`, `coop`, `main_menu`, `online`), 4 levels, 3 pawn
scenes, GAS resources and tag tables. CLI flags: `--server --port N`, `--host`, `--join <addr>`,
`--level <path>`, `--auto-move`, `--travel-after N`.

**The editor binary loads the `.editor` library even when running a game.**
`godot --path project` from an editor build matches the `.editor` manifest slot, not
`template_debug`. Rebuilding only the template target and testing that way will appear to change
nothing — a real template build needs an export.
