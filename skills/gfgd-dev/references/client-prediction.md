# Client prediction — the plan, and what it is already built on

Not implemented. This is the design, written down while the movement work was fresh, so that
whoever picks it up does not have to re-derive it.

Read this before touching `PlayerController`'s input path or `MovementBackend`.

## What problem it solves, and what it does not

Prediction removes the round trip between pressing a key and seeing **your own** character move. It
does nothing for how *other* players look on your screen — that is interpolation and extrapolation of
simulated proxies, a separate and much cheaper job.

Decide which one is hurting before starting. In a co-op or PvE game, proxy smoothing usually buys
more visible quality per hour of work. In fast PvP, prediction is the only thing that helps and
nothing substitutes for it.

## The hooks that already exist

These were put in during the movement work specifically for this, and they cost nothing while unused:

| Hook | Where | What it is for |
|---|---|---|
| `MovementInput.frame` | `src/movement/movement_types.h` | the tick an input belongs to; the one field a client must send for a server to be able to say "processed up to here" |
| `MovementState.should_reconcile()` | `src/movement/movement_types.cpp` | the decision a predicted client makes; on the state because only the state knows which of its fields matter |
| `MovementBackend` | `src/movement/movement_backend.h` | five methods deciding *when* the simulation runs and how many times. `StandaloneMovementBackend` runs one step per physics frame; a predicted backend runs several during a replay |
| The stateless solver | `src/movement/movement_utils.h` | every function takes the transform to work from rather than reading the node, so a tick can be run twice from two different starting states |
| `MovementState` holds the simulation | `src/movement/movement_types.h` | including layered moves and the movement base, so a rollback takes back everything |

The single rule that keeps all of this true: **nothing in the tick may read the body's current
transform, and nothing that survives a tick may live on the component.** Breaking it does not fail a
test — it silently makes replay wrong.

## The work

### 1. Input transport

`PlayerController::send_input_to_server` currently sends a delta of action states, once per rendered
frame, `TRANSFER_MODE_UNRELIABLE_ORDERED`, with no sequence number. That cannot support prediction.
It needs to become:

- sampled once per **physics** tick (`Pawn._gather_movement_input` is already the place a project
  puts its input, so the sampling point exists)
- stamped with a tick number
- sent with **redundancy** — the last N unacknowledged commands in every packet, because the channel
  is unreliable and a dropped command must not stall the server
- acknowledged: the server replies with the last tick it consumed

Keep the existing action-state path working. A pawn that does not predict should not pay for any of
this.

### 2. Client ring buffer and replay

The client keeps, per unacknowledged tick, the `MovementInput` and the `MovementState` it produced.
On a correction from the server:

1. adopt the authoritative state at the acknowledged tick
2. drop everything at or before it
3. re-run `simulate()` once per remaining buffered input, in order
4. the result is the new predicted present

`MovementState.should_reconcile` decides whether step 1 is needed at all — most corrections are
within tolerance and should be ignored, or the character twitches on every packet.

### 3. Server side

Buffer arriving commands and consume one per tick, with a small jitter buffer. Reply with
`(acked_tick, resulting_state)`. Unreal also has a large time-discrepancy detector as anti-cheat;
skip it in a first version and note that it is missing.

### 4. `PredictedMovementBackend`

The small one. Same five methods, `should_resim()` true, `on_rollback()` restores the state, and
`tick()` calls `simulate()` once per input being replayed instead of once per frame.

### 5. Layered moves during a replay

They already live in `MovementState`, but they are **copied shallowly** — the list is new, the moves
are shared. That is correct while time only moves forward. Replay needs the moves themselves
duplicated, because `start_time_ms` is per-instance. Add a `duplicate()` on `LayeredMove` and use it
in `MovementState::copy_from` when the backend reports `should_resim()`.

### 6. The movement base cannot be rewound

`CharacterMovementComponent::base_last_transform` is a snapshot of the world, not of the character,
and a replay cannot restore where a platform used to be. Unreal has the same limitation and lives
with it. A character replayed while standing on a fast platform will drift; the correction from the
server fixes it on the next packet.

More generally: **a resimulation collides against the present world, not the past one.** Other
characters and platforms have moved on. This is accepted, not solved.

## The trap nobody warns you about

Everything in this repository so far has been verifiable headless, deterministically, in one process,
with an assertion over 150 frames. **Prediction is not.** Its failure modes are "rubber-bands at 2%
packet loss" and "feels like treacle when changing direction at 120 ms", and no printed position
catches either.

Budget for building a test harness as part of the work, not after it:

- Godot's ENet binding exposes no artificial latency or loss. Add a debug delay-and-drop queue in the
  GFGD layer — in `NetDriver` or in the controller's send path — with project settings for latency,
  jitter and loss.
- The mechanism *is* testable: force a divergence, assert that after the correction the client's
  state matches the server's, and that the number of replayed ticks is what it should be.
- Feel is not testable here. It needs a person and a window.

## Rough shape of the effort

Bigger than the walking, steps, platforms, flying, transitions and layered-move work combined. Item 2
is the core; items 1 and 3 are each comparable to the movement-mode replication that already exists;
item 4 is small precisely because the seam was built first.
