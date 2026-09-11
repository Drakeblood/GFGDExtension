# Pooling, messaging and saves

## NodePool

Recycles nodes by taking them **out of the tree**, not by disabling them. A node outside the tree
does not exist for the physics server, so a recycled body cannot be hit by accident — which is the
bug every hand-rolled "just hide it" pool eventually produces.

| Property | Type | Meaning |
|---|---|---|
| `scene` | `PackedScene` | What to instantiate. |
| `factory` | `Callable` | Alternative to `scene`, when construction is not a plain instantiate. |
| `prewarm` | `int` | How many to build up front. |
| `container` | `NodePath` | Where acquired nodes are parented. Defaults to the pool itself. |

```gdscript
var node := pool.acquire()      # from the free list, or newly built
pool.release(node)              # back to the free list
pool.release_all()
pool.clear()
pool.get_active() -> Node[]
pool.get_active_count() -> int
pool.get_total_created() -> int
pool.detach_deferred(instance_id: int)
```

Signals: `node_acquired(node)`, `node_released(node)`.

`release()` is **safe from inside a physics callback**, because leaving the tree is deferred to the
end of the frame.

**A pooled node is reset by name, not by interface.** `NodePool` calls `_on_acquired()` and
`_on_released()` if the node defines them, because a pooled node is a `RigidBody2D` one moment and
an `Area2D` the next and there is no shared base to declare them on. **A typo in either name is
silent.**

```gdscript
extends Area2D

func _on_acquired() -> void:
	monitoring = true
	modulate.a = 1.0

func _on_released() -> void:
	monitoring = false
	linear_velocity = Vector2.ZERO
```

**`NodePool` frees everything when it leaves the tree.** Nodes waiting in the free list have no
parent, so nobody else ever would. That also means a pool moved to another parent comes back empty.

One pool handles one scene. For several kinds of spawnable, use one `NodePool` per kind — keyed in
a `Dictionary` on whatever manages them.

## GameplayMessageRouter

A fire-and-forget bus for announcements with no natural owner. Static `get_singleton()`, not an
autoload.

```gdscript
GameplayMessageRouter.get_singleton().register_listener(&"Fx.Impact", _on_impact)

func _on_impact(_channel: StringName, payload: Variant) -> void:
	burst(payload.position, payload.strength)
```

```gdscript
broadcast(channel: StringName, payload: Variant)
register_listener(channel, callback, match := MATCH_EXACT) -> handle
unregister_listener(handle)
has_listeners(channel) -> bool
clear()
```

Channels are **tag names**, so a listener on `Fx` with `MATCH_PARTIAL` hears `Fx.Impact`.
`MATCH_EXACT` (0) and `MATCH_PARTIAL` (1) are the two modes.

**A listener does not have to be unregistered.** One whose object has been freed is dropped on the
next broadcast.

**Listeners on one channel are called in REVERSE registration order — last registered, first
called.** The router walks its listener vector backwards, because that is the only way it can drop
freed listeners while iterating. Two consequences:

- **Never encode a dependency in registration order.** "The scorer registers first so it runs
  first" is exactly backwards, and it fails in the way that is hardest to see: the display is asked
  to show a result the scorer has not written yet, so it reads the *previous* run's value, or zero.
- **If B needs A's output, give it its own channel.** A listens on `Run.Ended`, does the scoring,
  and broadcasts `Run.ResultReady` when the result exists. B listens on that. The order is then
  stated in the code instead of inherited from whichever node happened to be `_ready` first.

```gdscript
# scorer
func _on_run_ended(_channel: StringName, _payload: Variant) -> void:
	_finish_run()                                  # writes the result
	router.broadcast(&"Run.ResultReady", _result)  # only now can anyone read it

# hud — listens on ResultReady, NOT on Ended
```

A partial-match listener is called once per broadcast even if several of its ancestors match,
and all matching callables across every ancestor bucket are collected before any of them runs — so
a listener may safely broadcast again, or unregister itself, from inside its own callback.

**The payload is always passed as `callback(channel, payload)` — two arguments, always**, because a
`MATCH_PARTIAL` listener cannot otherwise tell what it heard.

Delivery is **synchronous** and **local to one peer**. It is not a replication mechanism; broadcast
on the peer that needs to react, or replicate the state that causes the reaction.

Use it for cross-cutting announcements — a hit landed, a run ended, an objective completed — where
the sender should not know who is listening. Use signals for a relationship that is actually
one-to-one.

## Saves

`SaveGame` is a `Resource` you subclass. **By default it serializes every script
variable** — not only the `@export`ed ones, because a plain GDScript member carries no storage flag
and requiring one would write an empty object for every save class written in GDScript. So the
minimum is just fields:

```gdscript
extends SaveGame

var version := 1
var coins := 0
var best_score := 0
```

Override `_to_json` / `_from_json` only when reflection is not what you want — to keep a field out
of the file, or to reshape it:

```gdscript
func _to_json() -> String:
	return JSON.stringify({"v": version, "coins": coins})

func _from_json(data: Variant) -> int:
	coins = int(data.get("coins", 0))
	return OK
```

`to_json()` / `from_json(data)` are the non-virtual entry points; `from_json` returns
`ERR_INVALID_DATA` when handed something that is not a `Dictionary`.

Reading and writing goes through `ProjectStatics`, all static:

```gdscript
ProjectStatics.save_game(slot_name: String, data, encrypt := true) -> ...
ProjectStatics.load_game(slot_name: String, encrypt := true, save_game_script = null) -> ...
```

Encryption uses `application/game_framework/save_encryption_key`. **The default is public in the
GFGD repository**, and GFGD warns once per run while a project is still using it. Change it before
your first release, not after — changing it makes existing saves unreadable.

There is no built-in version field. Put one in your own payload and branch on it when reading;
a save written by an older build is the normal case, not the exception.

### `load_game` returning null does not mean "no save"

`load_game` returns a null `Ref` for four different reasons, and only one of them means the player
has never played:

| `LoadResult` | Cause | What it means |
|---|---|---|
| `LOAD_NOT_FOUND` | No file for this slot | There is no save. Starting from defaults is correct. |
| `LOAD_UNREADABLE` | Could not open or decrypt | There **is** a save. Usually `save_encryption_key` changed; the data may be fine. |
| `LOAD_INVALID_DATA` | JSON did not parse, or `_from_json` returned an error | There is a save. Your own migration code may have rejected it. |
| `LOAD_BAD_SCRIPT` | `save_game_script` does not extend `SaveGame` | There is a save. The caller is wrong; the file was never touched. |

```gdscript
var loaded := ProjectStatics.load_game("slot0", true, MySave)
match ProjectStatics.get_last_load_result():
	ProjectStatics.LOAD_OK:        _save = loaded
	ProjectStatics.LOAD_NOT_FOUND: _save = MySave.new()          # first run
	_:                             _show_save_damaged_screen()   # do NOT start over silently
```

`has_save_game(slot)` answers "is there a file at all" without reading it, and
`get_slot_load_result(slot)` remembers per slot rather than only the last call.

**The framework protects the file even if you ignore all of that.** After a failure other than
`LOAD_NOT_FOUND`, the slot is marked and `save_game` refuses it, returning `ERR_LOCKED` and printing
why. So the tempting shape

```gdscript
# The old data-loss classic. It no longer destroys the file - the write fails loudly instead.
var loaded := ProjectStatics.load_game("slot0", true, MySave)
_save = loaded if loaded is MySave else MySave.new()
```

now produces a game running on defaults whose saves visibly do not stick, rather than a player whose
profile is gone. `save_game` returns an `Error` for every other failure too, so a caller that checks
the return value learns about a full disk or an unwritable `user://` as well.

When the player has been told and has chosen to start again, `clear_load_failure(slot)` lifts the
block deliberately. Do not call it to make an error message go away.

Slot files live at `user://saves/<slot>.sav`.

## ProjectStatics — the rest

Convenience lookups that take the `World` (or `SceneTree`) and save a chain of calls:

```gdscript
ProjectStatics.get_game_instance(world)
ProjectStatics.get_game_mode(world)
ProjectStatics.get_game_state(world)
ProjectStatics.get_level(world)
ProjectStatics.open_level(world, resource_path)
ProjectStatics.get_first_player_controller(world)
ProjectStatics.get_player_controller(world, index)
ProjectStatics.get_player_controller_count(world)
ProjectStatics.get_local_player(world, index)
ProjectStatics.get_local_player_count(world)
```

From a node inside the tree, `get_tree()` is the `World`, so `get_world()` on any framework class
and these statics reach the same objects.

## Assertions

`Assert` is a static helper exposed to GDScript: `is_true`, `is_false`, `are_equal`,
`are_not_equal`, `is_null`, `is_not_null` — each taking a message. `AssertionException` carries
`get_message()`. It is an assertion API, not a test runner.
