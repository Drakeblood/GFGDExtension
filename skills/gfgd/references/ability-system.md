# The ability system — abilities, effects, attributes

A lightweight GAS: `AbilitySystemComponent` is the hub, and abilities, effects and attribute sets
are authorable `Resource`s.

**It replicates, server-authoritatively** — see *Networking* at the end. Offline nothing about
that costs anything or changes anything.

## AbilitySystemComponent

A `Node`, normally placed under the pawn root alongside the `Pawn` marker.

| Property | Type | Meaning |
|---|---|---|
| `attribute_set` | `AttributeSet` | Template. **Duplicated at runtime** — play never mutates the resource on disk. |
| `startup_abilities` | `Array` | Granted on ready. |
| `startup_effects` | `Array` | Applied on ready. |
| `replication_mode` | `ReplicationMode` | `REPLICATION_NONE`, `MINIMAL`, `MIXED`, `FULL` (default). See *Networking*. |

Abilities:

```gdscript
give_ability(ability_template: GameplayAbility, source_object: Variant = null, level := 1.0) -> int  # handle
clear_ability(ability: GameplayAbility) -> void
clear_all_abilities() -> void
try_activate_ability(ability_name: StringName) -> bool
try_activate_ability_by_handle(handle: int) -> bool
find_ability_by_name(name) / find_ability_by_handle(handle) -> GameplayAbility
cancel_abilities_with_tags(tag_container: GameplayTagContainer) -> void
cancel_all_abilities() -> void
get_activatable_abilities() -> Array
ability_local_input_pressed(action_name: StringName) -> void
ability_local_input_released(action_name: StringName) -> void
```

Effects:

```gdscript
make_outgoing_spec(effect, level := 1.0) -> GameplayEffectSpec
apply_gameplay_effect_to_self(effect: GameplayEffect, level := 1.0) -> int   # -1 blocked, 0 instant, else active_id
apply_gameplay_effect_spec_to_self(spec: GameplayEffectSpec) -> int
apply_gameplay_effect_to_target(effect, target: AbilitySystemComponent, level := 1.0) -> int
apply_gameplay_effect_spec_to_target(spec, target) -> int
remove_active_gameplay_effect(active_id: int, stacks_to_remove := -1) -> bool
remove_active_effects_with_tags(tag_container: GameplayTagContainer) -> int
get_active_effects() -> Array                                     # of ActiveGameplayEffect
get_active_effect(active_id) / get_active_effect_stack_count(active_id) / get_active_effect_remaining_time(active_id)
get_effects_time_remaining_with_granted_tags(tags) -> float       # a cooldown's time left
```

Cues — presentation only, fired on every peer:

```gdscript
execute_gameplay_cue(cue_tag: GameplayTag, parameters := {}) -> void   # once
add_gameplay_cue(cue_tag, parameters := {}) / remove_gameplay_cue(cue_tag)   # while something lasts
is_gameplay_cue_active(cue_tag_name) -> bool
```

Attributes:

```gdscript
get_attribute_set() -> AttributeSet
get_attribute_value(attribute_name: StringName) -> float          # current, after modifiers
get_attribute_base_value(attribute_name: StringName) -> float
set_attribute_base_value(attribute_name: StringName, value: float) -> void
```

Tags: `get_owned_gameplay_tags()`, `get_blocked_ability_tags()`, `update_tag_map(tag, count_delta)`,
`update_blocked_ability_tags(tag, count_delta)`,
`register_gameplay_tag_event(tag: GameplayTag, tag_delegate: Callable)`,
`has_matching_gameplay_tag(tag)`, `has_any_matching_gameplay_tags(c)`, `has_all_matching_gameplay_tags(c)`,
`get_gameplay_tag_count(tag)`, `add_loose_gameplay_tag(tag, count := 1)`, `remove_loose_gameplay_tag(tag, count := 1)`.

Signals: `ability_given(ability)`, `ability_activated(ability)`,
`ability_activation_failed(ability, reason)`, `ability_ended(ability, was_canceled)`,
`attribute_changed(attribute_name, old_value, new_value)`,
`gameplay_effect_applied(effect, active_id)`, `gameplay_effect_removed(effect, active_id)`,
`gameplay_effect_stack_changed(effect, active_id, stack_count)`,
`owned_tag_changed(tag_name, new_count)`, `gameplay_cue(cue_tag, event_type, parameters)`,
`replicated_state_received()`.

**Owned tags are reference counted.** Two sources granting the same tag both have to release it.
One ability ending does not strip a tag another still grants.

## Attributes

`AttributeSet` is a `Resource` holding `attributes: Dictionary[StringName, float]`.

```gdscript
define_attribute(attribute_name: StringName, base_value: float) -> void
has_attribute(name) -> bool
get_base_value(name) -> float          # authored value
get_current_value(name) -> float       # after modifiers
set_base_value(name, value) -> void
set_current_value(name, value) -> void
get_attribute_names() -> PackedStringArray
init_current_from_base() -> void
```

**The formula is `(base + sum of ADD) * product of MULTIPLY`, and a single `OVERRIDE` beats all of
it.**

Two virtuals to override in a subclass — this is where clamping and derived stats belong:

```gdscript
extends AttributeSet

# Return the value to actually store. This is the clamp hook.
func _pre_attribute_change(attribute_name: StringName, new_value: float) -> float:
	if attribute_name == &"health":
		return clampf(new_value, 0.0, get_current_value(&"max_health"))
	return new_value

func _post_attribute_change(attribute_name: StringName, old_value: float, new_value: float) -> void:
	if attribute_name == &"health" and new_value <= 0.0:
		_die()
```

Signals: `attribute_changed(name, old, new)`, `base_value_changed(name, old, new)`.

`AttributeModifier` is a `Resource`: `attribute: StringName`, `magnitude: float`,
`operation: Operation` (`ADD` = 0, `MULTIPLY` = 1, `OVERRIDE` = 2), plus
`apply(input_value: float) -> float`.

Where the number comes from is `magnitude_type`, worked out **once, when the effect is applied**:

| `magnitude_type` | Magnitude |
|---|---|
| `SCALABLE_FLOAT` (default) | `magnitude`, times `level_curve` sampled at the spec's level when a curve is set |
| `ATTRIBUTE_BASED` | `(backing_attribute + pre_multiply_additive) * coefficient + post_multiply_additive`, the attribute read from the spec's `SOURCE` or the `TARGET` |
| `SET_BY_CALLER` | the spec's value under `set_by_caller_name` (`magnitude`, with a warning, if it has none) |

A `Curve` clamps to its value range (0..1 by default): set `max_value` above the largest multiplier,
and `min_domain`/`max_domain` to the levels you use, or level 5 reads as level 1.

```gdscript
# 40% of the attacker's strength, as damage, whatever effect resource this is.
var spec := attacker_asc.make_outgoing_spec(PUNCH)      # PUNCH: ATTRIBUTE_BASED on "strength", coefficient -0.4
attacker_asc.apply_gameplay_effect_spec_to_target(spec, victim_asc)

# A number only the moment knows.
var hit := attacker_asc.make_outgoing_spec(DAMAGE)       # DAMAGE: SET_BY_CALLER "Damage"
hit.set_set_by_caller_magnitude(&"Damage", -rolled)
hit.context = { "position": hit_point }                  # reaches the effect's cues
attacker_asc.apply_gameplay_effect_spec_to_target(hit, victim_asc)
```

### Authoring the set: two things that fail silently

**A `.tres` for an `AttributeSet` subclass must say so in its header.**

```
[gd_resource type="AttributeSet" script_class="AircraftAttributes" load_steps=2 format=3]
```

Not `type="Resource"`. Godot refuses to attach a script whose base class is a GDExtension type to a
resource declared as something else — the file then loads as a bare `Resource` with no script, every
attribute missing, and nothing in the Output panel. The editor writes the correct header when you
create the resource by choosing the GFGD type in the *New Resource* dialog; this bites when a
`.tres` is written or edited by hand.

**Attributes do not exist until the component is ready, and that is later than you think.**
`AbilitySystemComponent` duplicates its `attribute_set` template and calls `init_current_from_base()`
**in its own `_ready`**; whatever applies upgrade or difficulty effects on top usually runs later
still, from the pawn root's `_ready` or from the game mode. In Godot a child's `_ready` runs
**before** its parent's, so a sibling sprite, a HUD, or anything ordered earlier in the tree reads
`0.0` — a valid float, no warning, no error:

```gdscript
# WRONG - runs before the parent defined anything.
func _ready() -> void:
	_throw_multiplier = _asc.get_attribute_value(&"throw_strength")

# RIGHT - read it when it is needed, with a fallback that means "unconfigured".
func _throw_multiplier() -> float:
	var v := _asc.get_attribute_value(&"throw_strength")
	return v if v > 0.0 else 1.0
```

Pick the fallback carefully: for anything multiplicative it is `1.0`. A `0.0` fallback does not
degrade, it disables — the throw that applies no force, the shot that deals no damage — and it looks
exactly like a gameplay bug rather than an ordering one.

## Effects

`GameplayEffect` is a `Resource`.

| Property | Type |
|---|---|
| `duration_policy` | `DurationPolicy` — `INSTANT` (0), `INFINITE` (1), `HAS_DURATION` (2) |
| `duration` | `float` |
| `period` | `float` — re-apply interval; `0` means once |
| `modifiers` | `AttributeModifier[]` |
| `effect_tags` | `GameplayTagContainer` — what this effect *is* |
| `granted_tags` | `GameplayTagContainer` — granted to the owner while active |
| `application_required_tags` | `GameplayTagContainer` — the owner must have all of these |
| `application_blocked_tags` | `GameplayTagContainer` — the owner must have none of these |
| `remove_effects_with_tags` | `GameplayTagContainer` — applying this strips those |
| `execute_period_on_application` | `bool` — a periodic effect ticks at once, not only after its first period |
| `gameplay_cue_tags` | `GameplayTagContainer` — cues: executed for instant effects and period ticks, added/removed with lasting ones |
| `stacking_type` | `STACKING_NONE` (0) or `STACKING_AGGREGATE` (1) — the same effect again adds a stack to the one running |
| `stack_limit` | `int` — 0 is unlimited |
| `stack_duration_refresh` | `bool` — another application restarts the duration |
| `stack_expiration` | `CLEAR_ENTIRE_STACK` (0) or `REMOVE_SINGLE_AND_REFRESH` (1) — one dose at a time |

**Stacks scale every modifier**: an `ADD` counts once per stack, a `MULTIPLY` is raised to the stack
count (1.1 twice is 1.21), an `OVERRIDE` is unchanged. `remove_active_gameplay_effect(id, 1)` takes
one stack off.

`ActiveGameplayEffect` (a `RefCounted`) is a live instance: `get_effect()`, `get_spec()`,
`get_active_id()`, `get_duration()`, `get_remaining_time()`, `get_stack_count()`, `get_level()`,
`is_replicated()` (a client's mirror).

`GameplayEffectSpec` (a `RefCounted`) is one application: `effect`, `level`, `get_source()`,
`set_set_by_caller_magnitude(name, value)`, `context`. Make one with `make_outgoing_spec`.

**Every tag container property defaults to `null`, not to an empty container.** Framework methods
accept null and read it as empty; your own GDScript must check `is_valid()` first.

## Abilities

`GameplayAbility` is a `Resource`, subclassed in GDScript.

| Property | Meaning |
|---|---|
| `ability_name` | The `StringName` `try_activate_ability` looks up |
| `input_action_name` | Bound through `ability_local_input_pressed/released` |
| `ability_tags` | What this ability *is* |
| `cancel_abilities_with_tag` | Cancelled when this activates |
| `block_abilities_with_tag` | Blocked while this is active |
| `activation_owned_tags` | Granted to the owner while active |
| `activation_required_tags` | The owner must have all of these |
| `activation_blocked_tags` | The owner must have none of these |
| `cost_effect` | Instant effect; activation is refused if it cannot be paid |
| `cooldown_effect` | Duration effect; its `granted_tags` are the cooldown |
| `net_execution_policy` | `LOCAL_ONLY`, `LOCAL_PREDICTED` (default), `SERVER_ONLY` — see *Networking* |

```gdscript
extends GameplayAbility

func _activate_ability() -> void:
	# Checks cost_effect and cooldown_effect again and applies both. Call it
	# where the ability really happens; end the ability when it says no.
	if not commit_ability():
		end_ability(true)
		return
	# ... do the thing ...
	apply_effect_to_target(HIT, target_asc)   # at this ability's level, with the owner as source
	end_ability(false)

func _end_ability(was_canceled: bool) -> void:
	pass

func _input_pressed() -> void: pass
func _input_released() -> void: pass
func _on_give_ability() -> void: pass
```

A refused activation says why: `get_last_failure_reason()` and the component's
`ability_activation_failed(ability, reason)` give `&"active"`, `&"blocked"`, `&"tags"`,
`&"cooldown"`, `&"cost"`, `&"script"`, or `&"rejected"` when the server refused a prediction.

Non-virtual surface: `activate_ability()`, `can_activate_ability()`,
`end_ability(was_canceled := false)`, `commit_ability()`, `commit_ability_cost()`,
`commit_ability_cooldown()`, `check_cost()`, `check_cooldown()`, `get_cooldown_time_remaining()`,
`get_cooldown_tags()`, `make_outgoing_spec(effect)`, `apply_effect_to_target(effect, target)`,
`apply_effect_spec_to_owner(spec)`, `get_avatar()`, `get_ability_level()`, `get_handle()`,
`is_predicting()`, `apply_effect_to_owner(effect) -> int`,
`get_ability_system_component()`, `get_source_object()`, `get_ability_id()`, `get_is_active()`,
`get_is_input_pressed()` / `set_is_input_pressed()`, `setup_ability(asc, source)`,
`input_pressed()`, `input_released()`. Signal: `ability_ended`.

**Virtual hooks are overrides, not signals** — never `connect()` to `_activate_ability` and friends.

## Ability tasks — waiting inside an ability

An ability that takes time is a coroutine: `_activate_ability` awaits. What it awaits should be a
task, not a bare timer or signal, because **a task dies with its ability**: when the ability ends —
finished, cancelled, or a prediction the server refused — its tasks are cancelled, and a cancelled
task never emits. The coroutine is released and the code after its `await` never runs.

```gdscript
func _activate_ability() -> void:
	if not commit_ability():
		end_ability(true)
		return

	# Charge: released early, or held to the limit - whichever first.
	var which: int = await wait_any([wait_input_release(), wait_delay(2.0)]).completed
	var charge := minf(get_time_active() / 2.0, 1.0)

	# The client's aim, on both machines.
	var data: Dictionary = await sync_target_data({ "dir": _aim_dir() }).completed
	var dir: Vector3 = data.get("dir", Vector3.FORWARD)   # from a client: check it
	apply_effect_to_target(HIT, _find_target(dir))
	end_ability(false)
```

With a bare `await get_tree().create_timer(2.0).timeout` instead, an ability cancelled during those
two seconds would still wake up and apply its effect.

| Method | Completes with |
|---|---|
| `wait_delay(seconds)` | `null`, after that many seconds of physics ticks |
| `wait_input_press()` / `wait_input_release()` | seconds waited / seconds held |
| `wait_gameplay_tag_added(name)` / `wait_gameplay_tag_removed(name)` | the tag name; next frame if already true |
| `wait_attribute_change(name)` | `{ old_value, new_value }` |
| `wait_gameplay_event(tag)` | the payload of `AbilitySystemComponent.send_gameplay_event(tag, payload)` |
| `wait_any(tasks)` | index of the first to finish; the others are cancelled |
| `wait_all(tasks)` | an `Array` of every result, in order |
| `sync_target_data(data)` | on the predicting client, `data` (sent to the server); on the server's copy, what the client sent; elsewhere, `data` |

`await task.completed` is the result. A task that finishes while it is being created (a tag already
there) emits at the end of the frame, after the `await` began, so it is never missed.

**`sync_target_data` is the network one.** For a `LOCAL_PREDICTED` ability, the client computes
something only it knows — where the player aimed — and the server's copy of the same activation gets
it. Calls are matched in order within an activation, so call it the same number of times on both
sides, and treat what the server receives as untrusted input. Offline, on a host's own pawn, or for
an activation the server started itself, it simply returns its own data.

**Your own task:** extend `AbilityTask`, override `_activate()` (call `finish(result)` there or
later), `set_ticking(true)` for `_tick(delta)` every physics frame, `_on_end(was_cancelled)` to
disconnect anything you connected, and start it with `run_task(MyTask.new())`.

### Gameplay cues — what players see and hear

A cue is a tag for presentation: `Cue.Hit`, `Cue.Burning`. Effects fire the tags in their
`gameplay_cue_tags` (executed for an instant effect and each period tick; added and removed with a
lasting one), and code fires them directly. Every peer that hears a cue emits
`gameplay_cue(tag, event, parameters)` and runs the tag's **handler**:

```
# cue_table.tres - a GameplayCueTable, listed in application/game_framework/gameplay_cue_tables
cues = { &"Cue.Hit": preload("res://cues/hit_spark.tscn"),      # a scene
         &"Cue.Burning": preload("res://cues/burning.tscn"),
         &"Cue.Shout": preload("res://cues/shout.tres") }       # a GameplayCueNotify
```

- **A `GameplayCueNotify` resource** (Unreal's Static notify) is called with the character as target:
  `_on_execute(target, parameters)`, `_on_active`, `_on_removed`. One resource serves every
  character, so it keeps no state.
- **A `PackedScene`** (Unreal's Actor notify) is instanced. For an added cue it goes under the
  character and lives until the cue is removed: its root's `_on_active(parameters)` is called, then
  `_on_removed(parameters)` — after which it must free itself (to fade out) — or, without that
  method, it is freed at once. For an executed cue it calls `_on_execute(parameters)` and must free
  itself; with a `location` parameter it is placed in the world there instead of on the character.
- A tag with no handler uses its **nearest parent's**: `Cue.Hit.Fire` plays `Cue.Hit` until it has its
  own. `GameplayCueManager.get_singleton().add_table(t)` adds a table at runtime.
- Handlers do not run on a dedicated server; it still sends the cues.

Parameters from an effect: `level`, `stack_count`, `magnitude` (the first modifier's, stacks
included), `source_path` (the source's avatar — a path, since nodes do not cross the wire),
`effect_path`, `context`, and `location`/`normal` lifted from the spec's context.

**Inside an ability, fire cues through the ability** — `execute_gameplay_cue(&"Cue.Hit", params)`,
`add_gameplay_cue`, `remove_gameplay_cue`. A predicted ability runs on the client and on the server;
through the component, the client that predicted it would hear its cue twice (once its own, once from
the server). The ability's methods leave that client out of what the server sends. A cue an ability
added is removed when it ends.

### Gameplay events and triggers

`asc.send_gameplay_event(&"Event.Hit", { "damage": 12 })` does two things, as in Unreal: running
abilities waiting in `wait_gameplay_event` hear it, and abilities whose `trigger_event_tags` match it
are **activated** by it (a child tag matches: `Event.Hit.Head` triggers `Event.Hit`), reading the
payload with `get_trigger_event_data()`. `_should_respond_to_event(tag, payload)` can turn one down.

**Events are not replicated** — same as Unreal. The usual pattern is that the event happens on each
machine by itself: the attack animation plays on the server and on the predicting client, and its
hit frame sends the event on both. Where a trigger fires follows Unreal's rule: a `LOCAL_PREDICTED`
or `LOCAL_ONLY` ability triggers on the machine controlling its owner, a `SERVER_ONLY` one on the
server. A predicted activation carries the payload with its request, so the server's copy runs with
the client's data though the server never saw the event — check it before trusting it. The server
refuses an activation claiming an event the ability is not triggered by.

`get_time_active()` is the seconds since activation. A task started while the ability is not active
comes back already cancelled.

## Wiring input to abilities

Bind on the pawn, in `_setup_input_component`, and let the ASC dispatch:

```gdscript
extends Pawn

func _setup_input_component(input_component: InputComponent) -> void:
	var asc: AbilitySystemComponent = get_pawn_root().get_node("AbilitySystemComponent")
	input_component.bind_action(&"attack", InputComponent.STARTED,
		func() -> void: asc.ability_local_input_pressed(&"attack"))
	input_component.bind_action(&"attack", InputComponent.COMPLETED,
		func() -> void: asc.ability_local_input_released(&"attack"))
```

## Using attributes for progression

Upgrades, difficulty scaling and gear bonuses are what `AttributeSet` plus `AttributeModifier` are
for: keep the authored value as the **base**, express every upgrade as an `INFINITE`
`GameplayEffect` carrying `ADD`/`MULTIPLY` modifiers, and read `get_attribute_value()` at the point
of use. Removing the effect removes the bonus, with no bookkeeping of your own — and
`attribute_changed` gives the UI its update for free.

For a shop-style upgrade with an authored curve per level, keep the curve in your own resource and
let the attribute hold only the result. Rebuild one effect whenever a level changes rather than
stacking one effect per purchase:

```gdscript
func _apply_upgrades() -> void:
	if _upgrade_effect_id != 0:
		_asc.remove_active_gameplay_effect(_upgrade_effect_id)
	var effect := GameplayEffect.new()
	effect.duration_policy = GameplayEffect.INFINITE
	for stat in _progressions:                       # your own curve resource
		var mod := AttributeModifier.new()
		mod.attribute = stat.attribute_name
		mod.operation = AttributeModifier.OVERRIDE
		mod.magnitude = stat.value_for_level(_levels[stat.attribute_name])
		effect.modifiers.append(mod)
	_upgrade_effect_id = _asc.apply_gameplay_effect_to_self(effect)
```

**Verify the numbers, not the wiring.** An upgrade path that silently stays at level 1 produces a
game that runs, responds and looks correct while every tuned constant is wrong — the flight feels
"different" with nothing to point at. Print the resolved `get_attribute_value()` for each attribute
once at run start while bringing this up; the difference between level 1 and level 6 is obvious in a
number and invisible on screen.

## Networking

`replication_mode` decides what a client is sent. The server's component is always the truth.

| | attributes, owned tags, cues | active effects | granted abilities |
|---|---|---|---|
| `REPLICATION_NONE` | — | — | — (every peer runs its own component, as before) |
| `REPLICATION_MINIMAL` | everyone | nobody | owner |
| `REPLICATION_MIXED` | everyone | owner | owner |
| `REPLICATION_FULL` (default) | everyone | everyone | owner |

"Owner" is whoever owns the `Pawn` beside the component, or the `PlayerState` above it.

**On a replicated client, the component does not change state by itself.** Effects, grants and base
values are ignored there (with a warning, once); `startup_abilities` and `startup_effects` are applied
on the server only. What the client has is what arrives: a full snapshot when its component enters
the tree (`replicated_state_received`), then every change. Attribute values arrive already clamped —
`_pre_attribute_change` and `_post_attribute_change` do not run on a client, only the change signals
fire — so logic like dying on zero health belongs in the server's hook, and a HUD listens to
`attribute_changed`.

Effects built in code (no resource path) are rebuilt on the client from their timing and tags; a
client's `ActiveGameplayEffect` is a mirror (`is_replicated()`), counting down locally for a HUD and
removed when the server says so.

### Activating over the network

Call `try_activate_ability`, or `ability_local_input_pressed`, **on the owning client**. What
happens then is the ability's `net_execution_policy`:

- **`LOCAL_PREDICTED`** (default) — runs on the client at once, and the server is asked. The
  server runs it too; its cost, cooldown and effects reach the client from there. If the server
  refuses, the client's copy is cancelled and `ability_activation_failed` says `&"rejected"`. On
  the client `commit_ability()` only *checks* cost and cooldown against the last values it was sent.
  Whichever side ends the ability, the other side's copy ends too.
- **`SERVER_ONLY`** — the client only asks. The server runs it; the client is told it is active
  (its script does not run there) and hears its cues.
- **`LOCAL_ONLY`** — runs where it was called and nowhere else.

The server can also activate an ability itself (`try_activate_ability` on the server); a predicted
one then runs on the owning client too.

**Bind ability input on the pawn exactly as offline.** With a replicating component the owning client
runs the pawn's bindings itself (the `PlayerController` sees `Pawn.runs_input_locally()`), and the
server ignores a remote player's presses arriving through the replicated action state — otherwise
every press would activate twice. A predicted ability's script therefore runs on two machines: keep
anything that changes game state inside effects, which only the server applies, and keep sounds and
sparks in cues.

**An ability granted from a template built in code** reaches the owner as a stand-in with its name,
input and policy but no script: it can be listed and requested, not predicted. Grant from a saved
`.tres` for the client to run the real script.

What is not there: predicted *effects* (a client never applies an effect, so a predicted ability's
cost shows up a round trip later), prediction keys for anything but activation, and a relevancy
filter beyond "every ready peer".

