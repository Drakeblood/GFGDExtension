# Debug tools — console, cheats, gameplay debugger, DebugDraw (v0.5)

A console with cheats, a gameplay debugger with a free camera, and debug drawing. All of it is on
in **debug builds**; a release build has none of it unless
`application/game_framework/debug/enable_in_release = true`. The World adds the overlays under the
root (not on a dedicated server): `GameConsole`, `GameplayDebuggerHUD`, `AbilitySystemDebugger`.
Each has its own on/off setting in `application/game_framework/debug/`.

## Keys are Input Map actions

| Action | Default | Does |
|---|---|---|
| `gfgd_debug_console` | key left of 1 (physical `` ` ``) | open/close the console |
| `gfgd_debug_gameplay_debugger` | `'` (physical) | show/hide the gameplay debugger |
| `gfgd_debug_gameplay_debugger_lock` | Kp * | lock the debugger's target |
| `gfgd_debug_category_0` … `_9` | Kp 0–9 | toggle category slots |
| `gfgd_debug_free_camera` | Tab | free camera on/off (while the debugger is shown) |
| `gfgd_debug_free_camera_teleport` | T (physical) | move your pawn to the free camera |
| `gfgd_debug_gas_debugger` / `_next` | F3 / F4 | the ability system overlay |

The editor writes them into the project the first time it loads the extension, so they are rebound
in **Project Settings > Input Map** (or by a game's controls menu at runtime, via `InputMap`). A
project that never saved them still gets the defaults at startup. `PlayerController` never
replicates `gfgd_debug_*` actions.

## Console

`ConsoleManager` is an engine singleton. A line is `name arg arg`; `"quoted"` is one argument; `;`
separates commands; names are case-insensitive. Resolution order: registered command → console
variable → the player's `CheatManager` method.

```gdscript
ConsoleManager.register_command("give_wood", func(amount: int = 10) -> void: wood += amount, "Adds wood.")
ConsoleManager.register_variable("show_fps", false, "FPS counter.", ConsoleManager.FLAG_SAVED)
if ConsoleManager.get_variable("show_fps"): ...
ConsoleManager.execute("give_wood 5; echo done")
```

- Arguments are converted to the **declared parameter types** (int, float, bool `1/0/on/off`,
  String, Vector3 `x,y,z`, a Resource by `res://` path, a GameplayTag by name). Untyped (lambda
  without types) → guessed. Extra words fold into a trailing String parameter. `FLAG_RAW_ARGS` → the
  Callable gets one `PackedStringArray`.
- `FLAG_SERVER`: typed on a client, the line goes to the server through the player's
  `DebugReplicator` and runs there; `ConsoleManager.get_executor()` is the sender's
  `PlayerController`; `print_line()` output goes back to that client. **Register server commands on
  the server too** (register them everywhere — it is simplest).
- `FLAG_CHEAT`: only while `are_cheats_allowed()` — always in debug builds, else the server's
  `cheats` variable.
- `FLAG_SAVED` variables persist in `user://gfgd_console.cfg`. History: `user://gfgd_console_history.txt`.
  `user://autoexec.cfg` runs at startup (`exec <file>` runs any file).
- Built-ins: `dumpcommands [filter]` (everything with its description), `echo`, `exec`,
  `slomo <scale>` (local `Engine.time_scale`), `quit`, `clear`, `gd.toggle`, `gd.category [name|slot]`,
  `gd.target self|aim|<path>`, `gd.lock`, `gd.freecam`; variables `cheats`, `console.engine_log`.
- **Write a help text** for every command (`register_command`'s third argument, a cheat's
  `_get_command_help`): the console shows it next to each suggestion as you type. Arguments are read
  off the method; a `FLAG_RAW_ARGS` command sets them with `set_command_usage(name, "<path> [count]")`.
  `get_info(name)` returns `{name, kind, usage, help, tags, value}`.
- While the console is open, `PlayerInput.block_gameplay_input()` is held and no mouse event reaches
  the game (the cursor is left as it was; the wheel scrolls the full console): every `PlayerInput`
  query returns released/0. Code reading the `Input` singleton directly is **not** blocked.
- Console output with no overlay listening (dedicated server) goes to stdout.

### The overlay (GameConsole)

The console key steps through two sizes, then closed (Esc closes from either):

1. **Command line** at the bottom of the screen. As you type, suggestions appear above it: every
   matching command, variable and cheat with its arguments, tags and description; once a space is
   typed, the one command's usage. Up/Down select (or walk the history when the line is empty), Tab
   completes, Enter runs. The list scrolls with the selection, so every match (and the whole history)
   can be reached. A command's output shows above the line until the next keystroke.
2. **Full console** from the top: what was typed and what it printed, the line under it,
   PageUp/PageDown scroll.

The game's own log is **not** in the console — it is in the log file (`user://logs/godot.log` on
desktop, rotated, `debug/file_logging/*`) and in the terminal. `--log` (or `-log`) on the command
line opens a terminal window with the live log on Windows when the game has none (started from
Explorer or a shortcut). `console.engine_log 1` adds engine warnings and errors to the full console,
`2` everything (saved).

## Cheats — CheatManager

`application/game_framework/debug/cheat_manager_script` = a script extending `CheatManager`
(`PlayerController.get_cheat_manager()` instances it on first use). **Every public method is a
command** (no `_` prefix); it **always runs on the server**, on the typing player's own cheat
manager:

```gdscript
extends CheatManager

func _get_command_help(command: String) -> String:
	return {"money": "Adds money."}.get(command, "")

func money(amount: int) -> void:
	get_world().get_game_state().budget += amount
	client_message("Budget: %d" % get_world().get_game_state().budget)
```

Helpers: `get_player_controller()`, `get_pawn()`, `get_pawn_root()`, `get_world()`,
`get_ability_system()`, `client_message(text)`. **Do not name the method `print`** — GDScript's
global `print()` wins; use `client_message()` for console output. Built-in cheats: `teleport x,y,z`,
`where`, `set_movement_mode <mode>`, `gas_add_tag`, `gas_remove_tag`, `gas_apply_effect <res path>`,
`gas_grant_ability <res path>`, `gas_set_attribute <name> <value>`, `gas_cancel_abilities`.

## Gameplay debugger

`'` shows the overlay. It follows the actor in the middle of the screen (ray from the camera,
skipping the local pawn; the hit collider is resolved to the nearest ancestor in group
`gameplay_debugger_target`, else a pawn root, else the nearest scripted node), the local pawn when
nothing is hit. Kp * locks, Kp 0–9 toggle categories.

Data flow: the client sends `(target path, enabled categories)` once on change; the
**server** collects every `gameplay_debugger_refresh_interval` (0.15 s) and sends the result to that
client only (reliable, channel 3). The target is resolved **by node path on the server** — a node
that only exists on the client shows "the server has no node at this path".

Built-in categories (slots 0–3): **Basic** (class, script, position, velocity, authority, owner,
controller; draws server position vs local), **Abilities** (server ASC snapshot + client mirror
differences), **Movement** (CMC mode, floor, base, client-vs-server position error), **Net** (peers'
RTT/loss, server fps; off by default). Game categories follow in the order of
`application/game_framework/debug/gameplay_debugger_categories` (script paths):

```gdscript
extends GameplayDebuggerCategory

func _init() -> void:
	category_name = "Tree"

func _collect_data(target: Node, viewer: Node) -> Dictionary:   # SERVER
	return {"hp": target.hp} if target is ChoppableTree else {}

func _draw_data(target: Node, data: Dictionary) -> String:      # viewer, BBCode
	return "hp %d" % data.hp if data else "not a tree"

func _draw_world(target: Node, data: Dictionary) -> void:        # viewer, every frame
	DebugDraw.text(target.global_position + Vector3.UP * 2, "tree")
```

Return only plain data from `_collect_data` (it is an RPC payload). `target` in `_draw_data` is the
viewer's **local** copy — compare it with `data` to find replication bugs.
`GameplayDebugger.collect(node, names, pc)` runs categories directly (handy in tests).

### Free camera

Tab (while the debugger is shown) or `gd.freecam` detaches the view into a `DebugCamera` under the
root — local to this machine, nothing replicates. W A S D fly along the view, Q / E down / up, mouse
looks, Shift ×4, Ctrl ×0.25, the wheel changes `speed`; the keys are physical. It moves in real time,
so it flies on with `slomo 0.05` or a paused tree. Meanwhile the pawn gets no input
(`PlayerInput.block_gameplay_input()`) and targets are picked from the free camera — the local pawn
included. T moves the pawn there through the `teleport` cheat (server-side) and leaves the free
camera. Turning it off returns to the camera it replaced, or to the pawn's camera when that one is
gone. From code: `GameplayDebuggerHUD.set_free_camera(bool)` (`/root/GameplayDebuggerHUD`).

## DebugDraw

Engine singleton, 3D only: `line`, `arrow`, `sphere`, `box`, `transformed_box`, `point`, `axes`,
`text` — each `(..., color, duration)`. `duration = 0` → one frame (call every frame); `> 0` →
seconds. Unlit, no depth test (visible through walls). `DebugDraw.enabled = false` stops it.
