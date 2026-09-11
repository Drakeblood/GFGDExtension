# Character movement

`CharacterMovementComponent` gives a pawn walking, falling and jumping, with the property names
Unreal's `CharacterMovementComponent` uses.

**Values are metric.** Godot works in metres where Unreal works in centimetres, so `max_walk_speed`
defaults to `6.0`, not `600`. Divide any number lifted from an Unreal project by 100.

**There is a map to try all of it on.** The demo project's *New Game* button, or
`--level res://levels/test_level.tscn`. Eight stations along +X — walking, steps, slopes, a gap, a
crouch tunnel, moving platforms, water, an updraft that switches to flying — with a HUD showing the
mode, velocity, floor and immersion live. WASD moves, Space jumps, C crouches, F dashes. Where a
station has something you cannot get past, it sits on one side of the walkway so the tour never dead
ends. The geometry is a table at the top of `project/scripts/demo_level_3d.gd`, so moving a platform
is editing a row; `project/tests/test_playground_3d.gd` holds every station to its numbers.

## Setting one up

Three nodes, and the component is a sibling of the `Pawn`:

```
CharacterBody3D              # the pawn root
 ├─ CollisionShape3D         # a CapsuleShape3D
 ├─ Pawn
 ├─ CharacterMovementComponent
 └─ Camera3D
```

Nothing else is required. `updated_body_path` defaults to `..`, which is the pawn root, and the
walking and falling modes are registered for you.

Drive it the way you would drive any pawn — with `add_movement_input`, in world space:

```gdscript
extends Pawn

var _input: InputComponent
var _movement: CharacterMovementComponent

func _setup_input_component(input_component: InputComponent) -> void:
	_input = input_component
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent")
	_input.bind_action(&"jump", InputComponent.TriggerEvent.STARTED, _on_jump)

func _gather_movement_input(_delta: float) -> void:
	if _input == null or not has_authority():
		return
	var move := _input.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, 0.0, move.y))

func _on_jump() -> void:
	if has_authority():
		_movement.jump()
```

**Gather input in `_gather_movement_input`, not in `_process`.** It is called on the physics tick,
immediately before the movement component takes the vector out — exactly once per simulated step.
Reading input from `_process` still works, but it accumulates once per *rendered* frame while
movement consumes once per *physics* frame, so the vector's length becomes however many frames
happened to fit. The component clamps it to survive that, which keeps walking speed independent of
frame rate but throws away the magnitude: a stick pushed half way arrives looking like a full push.
Gathering per tick keeps it.

The component takes the accumulated vector out on the physics tick and applies it. **Do not also
move the body yourself** — the component writes the transform once per tick and would overwrite you.

The guard is `has_authority()`, not `is_locally_controlled()`. Input for a remote player is read on
the **server**, out of the action state that player's machine sent — and there
`is_locally_controlled()` is false, so the pawn would never accumulate anything. This is the same
guard the demo pawns use, and it is what makes the code identical in every net mode.

### The body may be any `PhysicsBody3D`

Nothing here calls `move_and_slide()`, so `CharacterBody3D` is a sensible default rather than a
requirement — it is simply the kinematic one. `move_and_slide` is avoided on purpose: it reads
`velocity` off the body, derives its step from the engine's own physics delta, and writes back to the
node. All three would make it impossible to run the same tick twice from two starting states, which
is what a resimulation is, and therefore what client prediction needs.

## Where it runs

**Only where the pawn has authority.** On a client, movement still arrives through the transform
replication the `Pawn` sets up, exactly as it did before — see `references/networking.md`. There is
still no client-side prediction; the architecture is shaped so it can be added, not so that it is.

Two things reach a client, not one. The transform, through the `Pawn`'s synchronizer, and
**`movement_mode`**, through the component's own. The second one matters more than it looks: a
position tells you where a pawn is, not whether it is walking or falling, and that is exactly the
question an animation graph asks first. Without it a client has to guess the mode back out of
vertical velocity, which is wrong at the top of a jump and wrong on a ramp.

Unreal replicates the same thing as `ACharacter::ReplicatedMovementMode`, but under
`COND_SimulatedOnly` — an autonomous proxy there predicts its own mode, so sending it one would
fight the prediction. Nothing here predicts yet, so the owning client is a receiver like everybody
else and gets it too. That condition is what has to be added alongside prediction, not before it.
Set `replicate_movement_mode = false` for a pawn whose mode nothing outside the simulation cares
about.

React to it with the `movement_mode_changed` signal, which fires on every peer:

```gdscript
func _ready() -> void:
	var movement: CharacterMovementComponent = get_parent().get_node(^"CharacterMovementComponent")
	movement.movement_mode_changed.connect(_on_mode_changed)

func _on_mode_changed(_from: StringName, to: StringName) -> void:
	$AnimationTree.set(&"parameters/conditions/airborne", to == &"Falling")
```

With no `Pawn` beside it, the component moves whatever it is attached to and reads no input. That is
the shape for a scripted mover or a test scene.

## Modes

How a character moves is decided by a named `MovementMode` in the `modes` dictionary:

| Name | Class | Enters when |
|---|---|---|
| `Walking` | `WalkingMode` | there is walkable ground below |
| `Falling` | `FallingMode` | the floor is gone, or a jump was taken |
| `Flying` | `FlyingMode` | nothing puts it there on its own — see transitions |
| `Swimming` | `SwimmingMode` | the middle of the capsule is inside a `WaterVolume` |
| `Null` | `NullMovementMode` | nothing else is set; moves nothing |

`FlyingMode` is registered but never entered by itself: it has no gravity and no floor test, so a
flying character that finds ground under it has not landed. Deciding that is a transition's job.

Modes hand over to each other; you do not switch them by hand during play. `queue_next_mode()` asks
for a change, taken up at the next mode resolution inside the tick — never in the middle of a
half-finished move, which would leave the state describing a position the body is not in.
`set_movement_mode()` changes it immediately and is for setting a character up.

A mode is a `Resource`, so it can be authored inline in the inspector, shared between pawns, and
**subclassed in GDScript**. Putting your own under a name in `modes` replaces the built-in one; the
defaults only fill names a project left unset.

### The two halves of a mode

```gdscript
extends MovementMode

func _generate_move(params: MovementTickParams) -> void:
	# Propose motion. Side effect free - do not move anything here.
	params.get_proposed_move().linear_velocity = Vector3(0.0, 0.0, -10.0)

func _simulation_tick(params: MovementTickParams) -> void:
	# Carry it out and write params.get_out_state().
	pass
```

The split is the reason a dash on top of a walk will be possible without the walking code ever
hearing about dashes: a proposal is side effect free, so proposals can be blended. Unreal's
`CharacterMovementComponent` fuses the two inside each of its `Phys*` functions, which is precisely
why layering anything on top of it is painful.

`out_state` arrives holding a copy of the starting state, so a mode that returns without writing is
correct rather than undefined.

### The solver, from GDScript

A mode is only a real extension point if it can ask the same questions the built-in ones ask, so the
solver is callable from a script through the component:

| Method | What it answers |
|---|---|
| `sweep_from(from, motion)` | one shape cast — where did it get to, what did it hit |
| `slide_move_from(from, motion, velocity)` | collide and slide; what `FallingMode` uses |
| `move_along_floor_from(from, motion, velocity)` | the ground move, steps included; what `WalkingMode` uses |
| `find_floor_at(from)` | is there ground here, and may it be stood on |
| `adjust_floor_height_at(from, floor)` | snap into the band a standing character is kept in |
| `compute_velocity(...)` | the acceleration/friction/braking model |

Every one of them takes the transform to work from rather than reading the body — the same rule the
C++ side follows, and for the same reason: a tick has to be replayable.

```gdscript
extends MovementMode

func _generate_move(params: MovementTickParams) -> void:
	params.get_proposed_move().linear_velocity = Vector3(0.0, 0.0, -20.0)

func _simulation_tick(params: MovementTickParams) -> void:
	var cmc: CharacterMovementComponent = params.get_component()
	var start: MovementState = params.get_start_state()
	var out: MovementState = params.get_out_state()

	var from := Transform3D(start.rotation, start.position)
	var velocity: Vector3 = params.get_proposed_move().linear_velocity
	var result: Dictionary = cmc.move_along_floor_from(
		from, velocity * params.get_delta(), velocity)

	var moved: Transform3D = result["transform"]
	out.position = moved.origin
	out.velocity = result["velocity"]
```

Register it by putting it in `modes` under the name you want to refer to it by, then
`queue_next_mode(&"Dash")`.

### Layered moves

A dash, a knockback, a gust of wind. `LayeredMove` is motion laid over whatever mode is running, and
it only ever **proposes** — it never sweeps, never writes state, and never knows which mode is
active. The mode executes the mixed result.

This is where the `_generate_move` / `_simulation_tick` split pays for itself. Mixing happens between
the two, which is the only point at which motion is still a value rather than a position. Unreal's
`CharacterMovementComponent` fuses proposal and execution inside each `Phys*` function, and that is
exactly why layering anything on it is painful.

```gdscript
var dash := LinearVelocityLayeredMove.new()
dash.velocity = Vector3(0.0, 0.0, -20.0)
dash.duration = 0.5
dash.mix_mode = ProposedMove.OVERRIDE_VELOCITY
dash.finish_velocity_mode = LayeredMove.SET_VELOCITY
dash.finish_velocity = Vector3.ZERO
movement.queue_layered_move(dash)
```

`duration` of zero is a single tick — an impulse. Negative runs until something cancels it.
`priority` decides who wins: moves are applied lowest first, so the highest priority overwrites last.

**`OVERRIDE_ALL_EXCEPT_VERTICAL` is the one to reach for mid-air.** It takes over sideways movement
and leaves the up axis alone, so an air dash steers a falling character without cancelling gravity.
`OVERRIDE_VELOCITY` would freeze them in place instead.

`finish_velocity_mode` decides what is left behind: `KEEP_VELOCITY` lets a dash that ends mid-air
carry its speed, `SET_VELOCITY` stops it dead, `CLAMP_VELOCITY` keeps the direction but caps the
speed so a launch cannot hand the character a number the rest of the simulation was never tuned for.

### Root motion

`RootMotionLayeredMove` is the whole of root motion — and that it is a layered move rather than new
machinery is the point the propose/execute split was making. The animation proposes a velocity; the
active mode still does the sweeping, the collision and the floor. **A root-motion attack cannot walk
through a wall or off a ledge**, and nothing special had to be written to stop it.

```gdscript
var motion := RootMotionLayeredMove.new()
motion.animation_mixer_path = NodePath("AnimationPlayer")
movement.queue_layered_move(motion)
$AnimationPlayer.play("attack")
```

It defaults to `OVERRIDE_ALL_EXCEPT_VERTICAL` and an unlimited duration: the animation drives
horizontal movement, gravity still applies, and whoever started the animation cancels the move when
it ends.

Two things on the mixer have to be right or it silently proposes nothing, and both are warned about:

- `callback_mode_process` must be **Physics**. Root motion is consumed per simulated tick, and a
  mixer advancing on the render frame hands out deltas that do not line up with it.
- `root_motion_track` is the **track's own path**. For a `Node3D` position track that is just the
  node — `"Root"`, not `"Root:position"`. A skeleton track is `"Skeleton3D:Hips"`.

Rotation is not consumed yet: the mixer offers it, but nothing applies `angular_velocity_deg`.

**In 2D it works, but the animation needs a `Node3D` carrier.** Godot accumulates root motion only
from `TYPE_POSITION_3D` tracks — a 2D position track contributes nothing at all, and
`get_root_motion_position()` just returns zero. So hang a plain `Node3D` under the 2D pawn for the
track to address:

```
CharacterBody2D
├── CollisionShape2D
├── RootMotion          # a plain Node3D, never moved, never drawn
├── AnimationPlayer     # root_motion_track = "RootMotion"
└── CharacterMovementComponent2D
```

The node is never actually moved — a root motion track is excluded from being applied, in either
dimension — and the x and y of the delta are pixels. The delta is taken as authored rather than
turned by the character's facing, because a 2D `MovementState` carries no rotation: facing there is a
sprite flip, not a transform the solver knows about. An animation that has to run both ways either
flips sign in the track or is authored twice.

`LinearVelocityLayeredMove` is the other concrete one that ships, because it is the shape most of them
are. Anything else is a subclass, in C++ or GDScript:

```gdscript
extends LayeredMove

func _generate_move(params: MovementTickParams, out: ProposedMove) -> void:
	var elapsed: float = get_elapsed(params.get_component().get_sim_time_ms())
	out.linear_velocity = Vector3(0.0, 0.0, -elapsed * 20.0)
	out.mix_mode = mix_mode
```

One thing to know before relying on this over the network: **active layered moves live on the
component, not in `MovementState`.** It is the one place the "no simulation state on the node" rule
is bent, because deep-copying a list of instanced Resources twice per substep is not free and nothing
replays a tick yet. Client prediction will have to move them into the state; that is a known cost of
that work rather than an oversight.

### Transitions

Modes hand over to each other, but a mode that decides all its own exits accumulates every idea a
game ever has. `MovementModeTransition` is that decision as a separate object: authored beside a mode
in its `transitions`, or globally in the component's, and the mode it leaves never hears about it.

```gdscript
extends MovementModeTransition

func _evaluate(params: MovementTickParams) -> StringName:
	if params.get_start_state().velocity.y < -5.0:
		return &"Flying"
	return &""
```

Evaluated after the mode has run: the active mode's own list first, then the global one, and the
first that answers with a name wins — so order is priority. A rule is skipped entirely when the mode
itself already queued a change, because a mode that acted knew more about the move it just made than
any rule watching from outside.

## Tuning

The numbers most worth reaching for first:

| Property | Default | What it does |
|---|---|---|
| `max_walk_speed` | 6.0 | the speed walking accelerates towards, m/s |
| `max_acceleration` | 20.48 | how hard it gets there, m/s² |
| `ground_friction` | 8.0 | how sharply it can **change direction** while being pushed |
| `braking_deceleration_walking` | 20.48 | how hard it **stops** when input ends |
| `jump_velocity` | 4.2 | ≈0.9 m of jump at default gravity |
| `air_control` | 0.05 | fraction of `max_acceleration` usable in the air |
| `walkable_floor_angle` | 45° | steeper than this is a wall, not a floor |
| `max_step_height` | 0.45 | the tallest thing that is climbed rather than blocked by |
| `max_walk_speed_crouched` | 3.0 | the cap while crouched |

A partly pushed stick walks at a proportional speed, not at full speed reached more slowly —
`min_analog_walk_speed` sets the floor that scaling is allowed to reach, and defaults to zero.

### Moving platforms

A character standing on something remembers it as its **base**, and each tick is moved by however
much that base moved. `move_with_base` and `rotate_with_base` are both on by default; rotation is
carried about the up axis only, so a tilting platform moves a character without tipping it over.

What is applied is the base's *delta*, not a position rebuilt from a stored offset. The difference is
not academic: rebuilding overwrites every correction the solver made during the tick — the
floor-height adjustment above all — and the two then fight each other a little more on every frame.
The relative offset is still kept in `MovementState`, because that is what a client would be sent to
place a character on the right platform, but it is not what drives the motion.

Expect roughly one physics frame of lag between platform and rider; Unreal has the same, for the same
reason.

`impart_base_velocity` hands the platform's velocity back when the character leaves it, so a jump
from a moving lift keeps the lift's momentum. **Use an `AnimatableBody3D` for a moving platform** —
it is the body Godot tracks the motion of, and therefore the only one that reports a velocity to
take. A `StaticBody3D` pushed around by a script carries the character fine but hands back nothing.

### Swimming

Drop a `WaterVolume` — an `Area3D` — into the level and characters swim in it. `WaterMovementTransition`
is registered as a global transition by default, so nothing else is needed.

Water is found with a **point query every tick**, not with the area's `entered`/`exited` signals.
Signals would be state accumulated outside the simulation, and a replayed tick would see whatever the
last one happened to leave behind rather than what was true at that tick. The cost is one query per
tick per character; a game with no water anywhere can clear `transitions` to stop paying it.

`buoyancy` defaults to 1.0, which is neutral: a submerged character neither sinks nor rises, and one
at the waterline sinks until it is under and then hangs there. Above 1 it floats up. `water_velocity`
is a current, added to whatever the character is doing, so a river carries a still character.

**Entering is decided at the character's middle, leaving at its feet.** Testing the same place in
both directions makes someone treading water cross it twice a second — swimming, falling, swimming —
because the middle of the capsule sits almost exactly on the waterline. Wading is never swimming,
because entering needs the middle submerged in the first place.

A rise near the surface is damped by how much water is left holding the character up, and the lift
half of a stroke is scaled by that same immersion, because there is nothing to push against above the
waterline. Between them, holding *up* makes a character tread water at the surface instead of
swimming clean out of the pool, arcing through the air and dropping back in.

**Getting out of the water is the jump's job.** Taking it adds `out_of_water_jump_velocity` and
switches to `Falling` in the same tick. That is not a shortcut — it is what Unreal does, applying its
`OutofWaterZ` from `PhysicsVolumeChanged` once the character is already falling, because the swim
speed cap and the surface damping would otherwise brake the impulse straight back down and the
character would never leave the pool.

### Crouching

`crouch()` and `un_crouch()` ask; the component decides on the next tick. `can_crouch` defaults to
**on**, which is a deliberate divergence from Unreal — a crouch request that silently does nothing is
a worse first experience than a character that crouches when asked.

The capsule shrinks to `crouched_half_height` and the body drops by the difference, so the **feet
stay where they were** rather than the centre. Standing up is a request rather than an order: under a
low ceiling the character stays down and keeps trying until there is room. `crouch_changed` fires
when it actually happens, which may be many ticks after it was asked for.

Switching `can_crouch` on makes the component take ownership of the body's `CapsuleShape3D`. A shape
authored once and shared between every pawn of a kind would otherwise crouch all of them at once.

### Ledges

`can_walk_off_ledges` is on by default, and a character walks off an edge the moment the floor test
stops finding ground under it. Turn it off and the character stops instead — the blocked move is
given back whole, which is blunter than Unreal working out how far it could still have gone, but it
keeps the promise the flag makes.

Held back at a ledge, a character still settles about 3 cm below its flat-ground rest height. Two
things used to make that much worse and are handled now: the sweep's normal at an overhang is blended
towards the drop and was being followed downhill as if it were a ramp, so the true surface normal is
read back with a short ray; and the floor-height adjustment was pulling the character *down* onto a
contact off to one side, walking it off the drop a few centimetres a tick, so a downward settle onto
an off-axis contact is now refused. What is left is a single tick of settle before the floor test
gives up, and it does not accumulate.

`perch_radius_threshold` is a **restriction**, not a licence, and is off (zero) by default. When a
ground contact sits nearer the capsule's rim than this, the floor is re-tested with a narrower probe;
if that probe finds nothing the character falls even though the full-width test was happy. Raising it
therefore makes a character let go of a ledge *sooner*, which is what Unreal's wording says and the
opposite of what the name suggests:

| `perch_radius_threshold` | probe radius | walks this far past the edge before falling |
|---|---|---|
| 0 (off) | 0.5 | 0.47 |
| 0.2 | 0.3 | 0.27 |
| 0.35 | 0.15 | 0.07 |

Roughly, the character keeps its footing until its centre is a probe-radius past the edge.

It does cut the other way too, in one narrow case: when the full-width sweep finds only something too
steep to stand on **and** a ray straight down the capsule's axis finds nothing either, the narrow
probe's answer is adopted instead. Standing over a floor slot thinner than the probe is the shape of
it — the wide capsule catches the kerb beside the slot, the ray drops through the slot, and the narrow
probe rests on its lips. The result is recorded as a line trace, because the distance that has to
drive the floor-height adjustment is still the wide capsule's.

That half was dead code until it was measured. The narrow probe read its contact normal raw, and a
contact on a lip's corner — which is every contact this test is asked about — reports a normal blended
between the surface and the drop. On flat ground it came back at 47°, two degrees over the 45° limit,
so the probe called good floor too steep and perching could only ever take standing away. It now
reads the surface back with the same short ray the main floor test uses. `project/tests/test_perch.gd`
holds both halves to their numbers.

### Steps

Something too steep to walk on might still be short enough to walk *over*, and that is checked before
the move is turned into a slide — otherwise a character slides along the front of every stair. The
climb is three sweeps: up by `max_step_height`, forward, then back down, and the result is kept only
when the landing is somewhere that could have been walked to. The step *down* is what makes it
honest; without it a character climbs anything by teleporting to the top of it.

Two Godot-specific wrinkles are handled inside that check.

A shape sweep landing on the lip of a step reports a normal blended between the top face and the
front one, and that blend reads as unwalkable even when the surface underneath is flat. Unreal keeps
a separate `ImpactNormal` for exactly this case; Godot has only the one, so the surface is asked
directly with a short ray before the climb is refused. That ray is **nudged into the surface along
the direction of travel** first: the contact a sweep reports sits one collision margin in front of
what it touched, and a ray from the point itself passes cleanly in front of the step and finds
nothing.

And a climb is only a step if it **gained height**. Against a tall face all three sweeps can succeed
and still put the character back on the floor it started on; treating that as a step leaves the
velocity unslid, so the character stands still while reporting full speed — the figure animation
reads. Height rather than forward progress, because a real step is entered half a pixel at a time:
the character is blocked and re-accelerating from a standstill on every tick.

**Friction and braking are not the same number and should not be tuned as one.** Friction is
steering; braking is stopping. If the character feels sluggish to turn, lower `ground_friction`; if it
slides too far after the stick is released, raise `braking_deceleration_walking` or
`braking_friction_factor`.

`use_separate_braking_friction` splits them completely, at the cost of a second number to keep in
step.

### Slopes

Horizontal speed is maintained going up a ramp, which is Unreal's behaviour: a character climbing a
30° slope covers ground as fast as one on the flat, and rises as well. `get_velocity()` therefore
reports the flat speed, not the speed along the slope.

## 2D

`CharacterMovementComponent2D` is the 3D component's twin, on a `PhysicsBody2D`. Deliberately a twin
rather than a shared base: Godot keeps 2D and 3D in separate hierarchies all the way down, and GFGD
already follows that with `PlayerStart2D` and `PlayerStart3D`.

What *is* shared is everything with no dimension in it — `MovementState`, `MovementInput`,
`ProposedMove`, `FloorResult` and `MovementMode` are the same classes. A 2D state keeps its position
in the x and y of a `Vector3` and leaves z alone, so there is one set of types, one mode contract and
one prediction story rather than two. A GDScript mode reads `params.get_component_2d()` where a 3D
one reads `get_component()`.

**Units are pixels**, and the defaults are Unreal's centimetre values unchanged — which is what the
usual "a metre is a hundred pixels" convention makes them. `max_walk_speed` is 600, `gravity` 980,
`max_step_height` 45. The 3D component divides the same numbers by a hundred.

**Up is negative Y**, because Godot's 2D screen axis points down. `up_direction` defaults to
`(0, -1)` and every sign in the solver depends on it.

Everything the 3D component does, the 2D one now does too: walking, falling, jumping, steps, slopes,
walls, **crouching**, **flying**, **swimming**, **moving platforms**, **layered moves** and
**transitions**. The settings carry the same names with pixel values, the signals are the same
(`movement_mode_changed`, `crouch_changed`, `layered_move_started`, `layered_move_finished`), and a
`LayeredMove` written for one dimension works unchanged in the other — it reads its clock from
`MovementTickParams.get_sim_time_ms()` rather than from a component. `RootMotionLayeredMove` works
in both too, with one catch — see below.

Water is `WaterVolume2D` (an `Area2D`) with `WaterMovementTransition2D` registered by default, the
same as in 3D. The modes are `WalkingMode2D`, `FallingMode2D`, `FlyingMode2D` and `SwimmingMode2D`.

**There is a map to try all of it on.** The demo project's main menu has a *2D Playground* button, or
`--level res://levels/test_level_2d.tscn`. It is eight stations left to right - walking, steps,
slopes, a gap, a crouch tunnel, moving platforms, water, an updraft that switches to flying - with a
HUD showing the mode, velocity, floor and immersion live. The geometry is a table at the top of
`project/scripts/demo_level_2d.gd`, so moving a platform is editing four numbers.

### Top-down 2D

The component works top-down, but **not through `Walking`**. A top-down view has no gravity axis on
the screen, so there is no floor for the walking solver to find — and `FlyingMode2D` is exactly a
top-down mover already: no gravity, no floor test, input not flattened, plain collide-and-slide.

```gdscript
movement.gravity = 0.0
movement.starting_mode = &"Flying"
movement.can_crouch = false
movement.transitions = []           # only if there is no water; see below
```

Measured in a top-down room with those settings: 600 px/s in all eight directions, diagonals
normalised so there is no √2 speed boost, a wall stops the capsule at exactly its radius, pushing
diagonally into a wall slides along it, and corners hold still instead of jittering.

**There is a map for this one too** — the demo's *Top-Down Playground* button, or
`--level res://levels/test_level_topdown.tscn`. Pillars, a corridor, a 45° wedge to slide off and a
mud pool, with a HUD showing that `is_on_ground()` and `is_falling()` are permanently false.
`project/tests/test_topdown.gd` holds every claim below to its number.

**Setting gravity to 0 and leaving the mode as `Walking` is the trap.** It looks like it should work.
What happens is that `Walking` finds no floor, hands over to `Falling`, and `Falling` multiplies the
input by `air_control` — 0.05 by default. Acceleration drops from 2048 to 102 px/s², and a second of
holding a direction covers 52 px instead of 517. The character moves, so it does not read as broken;
it just feels like treacle. Left at the defaults it is worse still: gravity drags the character to
the "south" wall, whose north face *is* a walkable floor, and it stands there.

Three things to know:

- **Turn off `can_crouch`.** Crouching shrinks the capsule and shifts the body along `up_direction` so
  the feet stay put. Top-down there are no feet, so it is a 16 px lurch across the screen for nothing.
- **Layered moves must use `OVERRIDE_VELOCITY`, not `OVERRIDE_ALL_EXCEPT_VERTICAL`.** "Vertical" means
  the screen's y, which top-down is a real direction of travel. With the wrong mix mode an eastward
  dash covers its full 600 px and a southward one covers **zero**. This is the one that costs an
  afternoon.
- **`is_on_ground()`, `is_falling()` and `FloorResult` are all meaningless** — permanently false and
  empty. Anything keying off them needs another signal.

What you give up is everything that is about a floor: steps, slopes, ledges and perching, and
moving-platform base tracking, since the base is read off the floor result. `max_fly_speed` and
`braking_deceleration_flying` are the tuning knobs, not `max_walk_speed`.

What you gain is one unexpected thing. With gravity at 0, `SwimmingMode2D` stops being a swim — its
buoyancy term is `gravity * (1 - immersion * buoyancy)`, which is zero — and becomes **flying with a
current and its own speed cap**. So a `WaterVolume2D` is a ready-made mud patch, conveyor or stream:
set `water_velocity` and characters drift. That is why clearing `transitions` is worth it only when
there really is no water; otherwise it costs one point query per tick per character.

Two things are deliberately absent. There is no `rotate_with_base`: the 2D component writes only a
position back to the body, so there is no character rotation for a platform to turn. And there is no
perch — `perch_radius_threshold` and friends are a 3D capsule's answer to standing half off a corner,
and a 2D capsule has no corners to be off.

## Not in yet

Client prediction. `MovementBackend` is the seam it plugs into, and
`skills/gfgd-dev/references/client-prediction.md` is the plan for it.

A simulated proxy also does not extrapolate: it follows the replicated transform and nothing else.
Unreal's proxies run `SimulateMovement()` between updates — applying velocity, gravity and floor
checks — with the replicated mode telling them which physics to run, and smooth the visible mesh
separately from the snapping capsule. That is the other reason the mode is worth having on a client,
and it is what would make the difference visible under packet loss.
