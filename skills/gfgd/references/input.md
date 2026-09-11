# Input, local multiplayer, touch and split screen

## Why GFGD does not use `Input`

Godot's `Input` singleton merges every device into one state, which is why two people cannot share
it. GFGD splits it:

- **`LocalPlayer`** — one human at this machine. Owned by the `GameInstance`, so it survives a level
  change. Not a node.
- **`PlayerInput`** — that person's own action state, filtered by the devices they own.
- **`InputRouter`** — a node at `/root/InputRouter` that hands each raw event to whoever owns the
  device it came from.
- **`InputComponent`** — what gameplay code talks to. Resolves to the owning player's `PlayerInput`.

**A project that does nothing sees no change.** The first local player is created holding
`PlayerInput.DEVICE_SLOT_ALL`, a wildcard that accepts every device, and in that state `PlayerInput`
forwards every query straight to `Input` — same behaviour, same code path.

## Binding actions

Bindings are declared on the **pawn**, in `_setup_input_component`, which the `PlayerController`
calls **when it possesses the pawn**. Binding from `_ready` is too early and will not fire.

```gdscript
extends Pawn

var _input: InputComponent

func _setup_input_component(input_component: InputComponent) -> void:
	_input = input_component
	input_component.bind_action(&"attack", InputComponent.STARTED, _on_attack_pressed)
	input_component.bind_action(&"attack", InputComponent.COMPLETED, _on_attack_released)
```

`InputComponent.TriggerEvent`:

| Constant | Value | Fires |
|---|---|---|
| `TRIGGERED` | 0 | Every frame the action is held |
| `STARTED` | 1 | On press |
| `COMPLETED` | 2 | On release |

Polling, for continuous input — same shape as `Input`, but scoped to the owning player:

```gdscript
_input.is_action_pressed(&"nitro")
_input.is_action_just_pressed(&"jump")
_input.is_action_just_released(&"jump")
_input.get_action_strength(&"throttle")
_input.get_axis(&"left", &"right")
_input.get_vector(&"left", &"right", &"up", &"down", deadzone := -1.0)
```

Other members: `enabled: bool` (set `false` to gate gameplay input while a `LineEdit` has focus),
`get_owning_controller()`, `get_player_input()`, `remove_binding(action, trigger, callable)`,
`remove_all_bindings()`.

**Never read `Input` from a pawn script.** With two local players it moves both pawns at once, and
on a server it is nobody's input at all.

## Device slots

A device slot is a plain `int`, so it round-trips through GDScript and can key a `Dictionary`:

| Value | Meaning |
|---|---|
| `0`, `1`, `2`, ... | Joypad index |
| `PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE` | Keyboard and mouse, counted as one device |
| `PlayerInput.DEVICE_SLOT_ALL` | Every device. The single-player default |
| `PlayerInput.DEVICE_SLOT_NONE` | No device yet; waiting for a press-to-join |

A player may own several at once — keyboard and mouse *and* a pad is the normal setup for player 1
in a co-op game. `LocalPlayer.device_slots` and `PlayerInput.device_slots` are
`PackedInt32Array`s; `add_device_slot`, `remove_device_slot`, `has_device_slot` and
`accepts_device_slot` do the bookkeeping.

Events are classified **by class, not by `InputEvent.device`**: keyboard and mouse have reserved
ids, but a synthesized `InputEventAction` reports `0`, which would collide with joypad 0.
`PlayerInput.device_slot_from_event(event)` is the static that does the classifying.

## Two pads, keyboard ignored

Set `max_local_players = 2` and `allow_press_to_join = true` on the game mode, then:

```gdscript
extends GameModeBase

func _init_game(world: World) -> bool:
	var pads: Array = Input.get_connected_joypads()

	# Narrow player 0 off the DEVICE_SLOT_ALL wildcard first, or it swallows
	# every event the second pad needs to join with.
	world.get_local_player(0).device_slots = PackedInt32Array([pads[0]])

	if pads.size() > 1:
		world.create_local_player(pads[1])

	return false    # the framework logs everyone in and spawns them

func _can_join(device_slot: int) -> bool:
	return device_slot != PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE
```

`_can_join` is the gate for press-to-join, and it is where "keyboard is not a player" belongs. Any
button on an unowned device offers its slot to `GameModeBase.try_join()`; a player already in the
game but holding no device claims it first, so the same path doubles as "press a button to pick your
pad back up" after a disconnect.

**One `InputMap` serves everyone.** You do not duplicate actions per player.

**Raising `max_local_players` without narrowing player 0 leaves nothing for anyone else** — player 0
on `DEVICE_SLOT_ALL` claims every event, including the ones a second pad needs to join with. The
framework warns about exactly this combination.

`InputRouter` signals: `unassigned_device_input`, `joypad_connected`, `joypad_disconnected`.

## Synthetic input

**`PlayerInput` in filtered mode does not see `Input.action_press()`.** Synthetic actions pushed
into the engine singleton belong to nobody. Use `PlayerInput.action_press(name, strength)` /
`action_release(name)` on the player you mean.

## The router listens on `_input`

Not `_unhandled_input`. It has to: if a `Control` swallowed a press but not the release, the action
would stay held forever. It never marks input handled, so the GUI is unaffected — but **gameplay
actions do fire while a `LineEdit` has focus**. Set `InputComponent.enabled = false` to gate that.

## Touch controls

Godot ships `VirtualJoystick` and `TouchScreenButton`, and **for a single-player game those are the
better choice** — they are themed, and `PlayerInput` in passthrough forwards to `Input` anyway.

`PlayerVirtualJoystick` and `PlayerTouchButton` exist for the two things the engine's cannot do:
address one player out of several (`player_index`), and hold an action for as long as a finger is on
the stick (`press_action`), so one gesture can mean both "move" and "hold".

```gdscript
$Joystick.action_left = &"move_left"
$Joystick.action_right = &"move_right"
$Joystick.action_up = &"move_up"
$Joystick.action_down = &"move_down"
$Joystick.press_action = &"grab"      # held while the finger is down
```

`PlayerVirtualJoystick`: `mode` (`DYNAMIC` / `FIXED`), `max_distance`, `deadzone`,
`recenter_on_overshoot`, `draw_default`, `player_index`; `get_value()`, `is_active()`, `reset()`;
signals `drag_started`, `drag_ended`.

`PlayerTouchButton`: `action`, `shape` (`CIRCLE` / `RECTANGLE`), `texture_normal`,
`texture_pressed`, `draw_default`, `player_index`; `is_pressed()`, `reset()`; signals `pressed`,
`released`.

Both are `Control`s, so they anchor with the rest of the HUD, and both read raw touches in `_input`
tracked by finger index. Neither marks events handled, so buttons above keep working — a stick keeps
clear of the HUD simply by having a rect that does not cover it.

## Split screen

Out of scope by design: the framework records *which* viewport belongs to whom and leaves the layout
to you.

Set `LocalPlayer.viewport_override` to a `SubViewport` and
`PlayerController.set_pawn_camera_node_as_current()` targets it. A `Camera2D` needs nothing else —
its `custom_viewport` is set for you. A `Camera3D` has no such property, so it has to physically
live inside the `SubViewport`; `LocalPlayer.adopt_camera(camera)` moves it there, and the framework
warns rather than reparenting behind your back.
