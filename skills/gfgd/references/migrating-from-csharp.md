# Porting an existing game onto GFGD

Written from the first real port: a shipped 2D game on a hand-rolled C# game framework, moved to
GFGD as ~5 000 lines of GDScript. Everything here is the part that was not obvious in advance.

## The C# question, answered once

**A C# project cannot adopt GFGD gradually.** Two independent walls:

1. **C# cannot inherit a GDExtension class.** `class MyGameMode : GameModeBase` does not compile
   into anything Godot will accept. So every class that exists *to override something* —
   game instance, game mode, level, save game, pawn, controller, ability — has to become GDScript.
2. **`[GlobalClass]` names collide with `ClassDB`.** A C# framework of your own that registers
   `GameMode`, `Level`, `PlayerState` and friends cannot coexist with GFGD's classes of the same
   name. The project will not open cleanly with both installed.

From C#, GFGD is still usable **compositionally** — place the nodes, author the resources, call the
methods, connect the signals. It is the overrides that are closed. In practice that means a mixed
project keeps C# for leaf gameplay code and moves the framework layer to GDScript, or goes all the
way. Decide before starting, not halfway through.

## An order that works

Dependencies, not feature importance, dictate the order. What worked:

1. **Cutover first, gameplay second.** Switch `main_loop_type` to `World`, write the game instance,
   one game mode scene, one level script and the tag table, and get the project *booting* with
   everything else replaced by minimal stubs. A booting project with no gameplay is a far better
   base than half-ported gameplay that cannot run.
2. **Stub every script a scene references, at the matching path.** Every `.tscn`/`.tres` naming a
   deleted script is a scene that will not load and a resource whose values you are about to lose.
   A three-line stub extending the right base class keeps all of it loadable. **A stub for a
   `Resource` must repeat the same `@export` names**, or the `.tres` silently loses its values on
   the next save.
3. **Then: leaf systems → run state → the player → spawnables → UI → the spawner.** The spawner
   goes last because it needs everything else to exist.

In `.tscn`/`.tres`, rewrite `path="res://X.cs"` to `path="res://x.gd"` and **delete the `uid=`
attribute on that line** — Godot resolves by path and writes a fresh uid on the next save.

## Verify by running, not by parsing

`--check-only --script` proves a file parses. It proves nothing else, and every real bug in this
port was of a kind it cannot see:

- a lambda capturing a variable that was still `null`,
- a `_ready` reading something that did not exist yet,
- a pawn drawing behind the level,
- a `.tres` header that made a resource load without its script,
- an upgrade path stuck at level 1, so the whole game ran on wrong constants.

Every one of these produced a running game with no error output. Boot the project after each step,
and check the actual numbers — print the attribute values, the applied forces, the resolved scene
paths — rather than confirming that a thing "works".

## Language traps that cost the most time

- **GDScript lambdas capture by value, at the moment the lambda is created.** A closure over a
  variable assigned *after* it — the classic "build the job, then give it its callable" shape — sees
  `null` forever, and calling a method on it fails per tick, silently, inside a timer callback:

  ```gdscript
  # WRONG - `job` is null inside the lambda
  var job := Job.new(self, func(): _push(job.scale), 0.3)

  # RIGHT - construct first, assign the callable second
  var job := Job.new(self, Callable(), 0.3)
  job.callable = func() -> void: _push(job.scale)
  ```

- **A child's `_ready` runs before its parent's.** See the ordering entries in `gotchas.md` and
  `ability-system.md`; this one bug appeared in six separate scripts in this port, each time
  presenting as a different gameplay problem.
- **Typed arrays do not accept untyped ones.** `Array[Foo]` cannot be assigned an `Array`, so a
  helper returning a plain `Array` breaks at the assignment, not at the return.
- **Integer division warns.** Where it was intentional in the original, write `floori(a * b / c)`
  and keep the arithmetic in floats.
- **Locals shadow inherited properties.** `material`, `offset`, `position` and friends are members
  of the node you are extending; a local of the same name warns and is easy to misread later.

## What the scene, not the code, has to say

C# `_Ready()` bodies that set up initial state — `GravityScale = 0f`,
`_particles.Emitting = false` — have no equivalent unless you write one. When the port moves that
logic into the framework or removes it, **those values must be authored into the `.tscn`**. Otherwise
the scene keeps Godot's defaults and the symptom is physical: a body that falls the instant it
spawns, an effect that plays from the first frame. Go through every `_Ready`/`_ready` in the original
and ask which lines were compensating for the scene, then move them into the scene.

## Saves survive; seeds do not

**Saves.** Keep reading the old file. That means a version field, and a `_from_json` that migrates
old key names — a C# save written with PascalCase keys will not match GDScript's snake_case members:

```gdscript
func _from_json(data: Variant) -> int:
	if typeof(data) != TYPE_DICTIONARY:
		return ERR_INVALID_DATA
	for key in data:
		var name := String(key).to_snake_case()
		if name in self:
			set(name, data[key])
	version = int(data.get("version", data.get("Version", 1)))
	return OK
```

Before the first run that can write, read the existing save and print it, and branch on
`ProjectStatics.get_last_load_result()` rather than on the returned value being null — a migration
that rejects the old file is `LOAD_INVALID_DATA`, not "no save". See `pooling-and-messaging.md`.

**Seeds are not portable.** Godot's `RandomNumberGenerator` does not reproduce .NET's `Random`, so
the same seed gives a different world than the old build did. Determinism *within* the new build is
intact — same seed, same world, every machine — which is what a daily challenge actually needs. But
old seeds, and any leaderboard tied to them, do not carry over. Say so before someone reports it as
a bug.

## Performance, measured

The fear going in was that GDScript could not carry a procedural spawner that had been C#. It could:
the ported game's own scripts cost ~0,3 ms per frame at a steady 60 fps, with the heaviest system
being the one everyone expected to be the problem.

Two things made the measurement trustworthy, and both were learned the hard way:

- **`Performance.TIME_PROCESS` is whole-frame process time, not your script time.** Reading it as
  "how long my code took" overstates the cost by an order of magnitude.
- **Close every other editor and game instance first.** A first round of readings showed 28–48 fps
  and was pure contention from concurrent Godot processes; the same build measured alone was pinned
  at 60.

Keep per-frame work budgets in `Time.get_ticks_usec()`, not `msec` — a budget expressed in whole
milliseconds cannot express "300 µs" and rounds to either zero work or no budget at all.

## Things worth *not* replacing

A 1:1 port is the right call when the framework's equivalent has different semantics. GFGD's
`PlayerVirtualJoystick` drives InputMap actions; the game's own pad returned a continuous value from
"the left 62% of the screen" and blended it with the keyboard axis. Swapping it changes how the game
feels, which is a design decision and does not belong inside a migration. Reach parity first, then
choose.
