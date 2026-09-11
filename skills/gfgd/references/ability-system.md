# The ability system — abilities, effects, attributes

A lightweight GAS: `AbilitySystemComponent` is the hub, and abilities, effects and attribute sets
are authorable `Resource`s.

**The ability system is not replicated.** Activate abilities where the pawn is authoritative and let
the results reach clients through what you replicate on the pawn or its player state.

## AbilitySystemComponent

A `Node`, normally placed under the pawn root alongside the `Pawn` marker.

| Property | Type | Meaning |
|---|---|---|
| `attribute_set` | `AttributeSet` | Template. **Duplicated at runtime** — play never mutates the resource on disk. |
| `startup_abilities` | `Array` | Granted on ready. |
| `startup_effects` | `Array` | Applied on ready. |

Abilities:

```gdscript
give_ability(ability_template: GameplayAbility, source_object: Variant = null) -> void
clear_ability(ability: GameplayAbility) -> void
try_activate_ability(ability_name: StringName) -> bool
cancel_abilities_with_tags(tag_container: GameplayTagContainer) -> void
get_activatable_abilities() -> Array
ability_local_input_pressed(action_name: StringName) -> void
ability_local_input_released(action_name: StringName) -> void
```

Effects:

```gdscript
apply_gameplay_effect_to_self(effect: GameplayEffect) -> int      # returns an active_id
remove_active_gameplay_effect(active_id: int) -> bool
remove_active_effects_with_tags(tag_container: GameplayTagContainer) -> int
get_active_effects() -> Array                                     # of ActiveGameplayEffect
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
`register_gameplay_tag_event(tag: GameplayTag, tag_delegate: Callable)`.

Signals: `ability_activated(ability)`, `ability_ended(ability, was_canceled)`,
`attribute_changed(attribute_name, old_value, new_value)`,
`gameplay_effect_applied(effect, active_id)`, `gameplay_effect_removed(effect, active_id)`,
`owned_tag_changed(tag_name, new_count)`.

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

`ActiveGameplayEffect` (a `RefCounted`) is a live instance: `get_effect()`, `get_active_id()`,
`get_remaining_time()`.

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

```gdscript
extends GameplayAbility

func _can_activate_ability() -> bool:
	return get_ability_system_component().get_attribute_value(&"stamina") >= 10.0

func _activate_ability() -> void:
	apply_effect_to_owner(preload("res://effects/stamina_cost.tres"))
	# ... do the thing ...
	end_ability(false)

func _end_ability(was_canceled: bool) -> void:
	pass

func _input_pressed() -> void: pass
func _input_released() -> void: pass
func _on_give_ability() -> void: pass
```

Non-virtual surface: `activate_ability()`, `can_activate_ability()`,
`end_ability(was_canceled := false)`, `apply_effect_to_owner(effect) -> int`,
`get_ability_system_component()`, `get_source_object()`, `get_ability_id()`, `get_is_active()`,
`get_is_input_pressed()` / `set_is_input_pressed()`, `setup_ability(asc, source)`,
`input_pressed()`, `input_released()`. Signal: `ability_ended`.

**Virtual hooks are overrides, not signals** — never `connect()` to `_activate_ability` and friends.

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
