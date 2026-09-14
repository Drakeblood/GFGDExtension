# Client prediction — how it is built, and what it still does not do

Implemented, for `CharacterMovementComponent` and `CharacterMovementComponent2D`, behind
`client_prediction` (off by default). This file started as the plan; it now records what was built
against that plan, where it lives, the rules that keep it correct, and what was left out on purpose.

Read this before touching `PlayerController`'s input path, `Pawn`'s replication, `MovementBackend`,
or anything a tick reads.

## What problem it solves, and what it does not

Prediction removes the round trip between pressing a key and seeing **your own** character move. It
does nothing for how *other* players look on your screen — that is interpolation and extrapolation of
simulated proxies, a separate and much cheaper job, and still not done (see the end).

## The one rule

**A tick is `(MovementState, MovementInput, dt) -> MovementState`, and nothing else.** Nothing in the
tick may read the body's current transform, and nothing that survives a tick may live on the
component. Replay is only exact if this holds, and breaking it does not crash — it produces a steady
drip of small corrections.

It is now a test: `project/tests/test_replay.gd` runs three seconds of walking, jumping, crouching, a
stateful layered move and a wall, in 3D and 2D, records every tick, rolls back to five points and
replays each twice, and demands the same state to 1e-5. Sabotaging `rollback_to_state` so it does not
write the body back, or making the history's layered moves shallow, turns it red. Run it after any
change to a mode, the solver or the component's tick.

Two things on the component *do* survive a tick and are handled explicitly:

- `current_floor` — a cache the walking mode reads at the start of the next tick. `rollback_to_state`
  clears it and runs `find_floor` again from the restored transform.
- `base_last_transform` — a snapshot of the world, not of the character. It cannot be rewound (the
  platform has moved on), so `rollback_to_state` drops the base and the next tick finds it again.

## Where everything is

| Piece | Where | What it does |
|---|---|---|
| `PredictedMovementBackend` | `src/movement/predicted_movement_backend.*` | the whole protocol: role, client history and replay, server queue, wire format, debug network queue |
| role decision | `PredictedMovementBackend::resolve_role` | per tick, from the `Pawn`: local / autonomous proxy / server / none |
| `simulate_with_input` | both components | a tick with an input handed in instead of gathered; latches left alone |
| `rollback_to_state` | both components | state + body + capsule + floor, together; base dropped |
| `server_receive_moves`, `client_receive_move_ack` | both components | the two remote calls; the component is the node at the same path on both peers |
| `MovementInput.custom_flags` | `src/movement/movement_types.*` | 16 game-defined bits that travel with the move |
| `custom_input_flags` signal | both components | fires inside the tick carrying the bits, on every peer and every replay |
| `MovementInput.copy_from` / `duplicate_input` | `movement_types.*` | the history keeps one per tick |
| `MovementState` deep copy | `copy_from(other, true)`, `duplicate_state(true)` | duplicates layered moves too; only the history uses it |
| `LayeredMove::duplicate_move` | `src/movement/layered_move.*` | `Resource::duplicate` + start time + started; virtual, `_duplicate_move` for scripts; `RootMotionLayeredMove` carries its mixer |
| `should_reconcile` | `movement_types.cpp` | position, velocity, mode, and now crouch |
| `Pawn::wants_movement_input` | `src/framework/pawn.*` | where movement input is used; what pawn scripts gate on |
| `Pawn::is_locally_predicted` | `pawn.*` | owner of a predicted pawn, not the server |
| `Pawn.replicated_position/rotation` | `pawn.*` | the transform replication goes through these, and the owner of a predicted pawn drops them |
| `replicated_movement_mode` | `character_movement_component.*` | the same for the mode |
| input bindings on the client | `PlayerController::_process` | pumps the `InputComponent` on a client whose pawn it predicts |
| project settings | `register_types.cpp` | `prediction/input_buffer_ticks`, `prediction/max_moves_per_packet`, `debug/network_*` |

## How each item of the plan came out

### 1. Input transport

Moves are sampled once per physics tick — by the component's own `gather_input`, which already
called `Pawn._gather_movement_input` — stamped with the backend's `sim_frame`, and sent every tick as
`server_receive_moves(first_frame, PackedVector3Array, PackedInt32Array)`: the newest
`max_moves_per_packet` unacknowledged moves, oldest first. The flags word is jump (bit 0), crouch
(bit 1) and the custom flags in the high sixteen. Unreliable-ordered both ways.

The crouch and custom-flag latches were folded into the input in `gather_input` for this. Before, the
crouch latch was read directly in `update_crouch_state`, which made the input not the whole of the
tick.

The existing action-state path is untouched and still runs: `PlayerController` sends it for
everything that is not movement. On a client whose pawn it predicts, it now also pumps the
`InputComponent`, because a jump bound to a button has to reach the simulation it predicts. A pawn
that does not predict pays for none of this: `StandaloneMovementBackend`, no RPCs sent, no history.

The RPCs live on the **movement component**, not on `PlayerController` as the plan suggested. The
component is at the same path on both peers, it is what the moves are for, and it works for a pawn
possessed by anything.

### 2. Client history and replay

`SavedMove { frame, delta, sim_time_ms, input, state }`, one per predicted tick, capped at 256. An
acknowledgement is stored when it arrives (on the network poll) and applied at the start of the next
physics tick, because a replay runs the solver.

On apply: find the saved move for that frame; `should_reconcile` against the server's state; drop
everything up to it. Only if it disagreed: build the corrected state from the **saved** state with
the server's position, rotation, velocity, mode, crouch and `extra` written over it (layered moves
cannot cross the wire and the base's id is a local `ObjectID`, so those stay the client's own), call
`rollback_to_state`, put the clock back to the saved `sim_time_ms`, and replay every remaining move
with `simulate_with_input`, re-saving each resulting state.

An ack for a frame newer than anything held means the server is at or past everything predicted;
the state is compared with the present and adopted if needed. An ack older than the oldest held move
is ignored.

### 3. Server side

Arriving moves go into a queue (ignoring frames already received or consumed; at most 64 queued, the
rest is resent anyway). Consumption waits for `input_buffer_ticks` queued moves, then takes one per
tick, two while the queue is past `2 * buffer + 2`. Running dry starts buffering again.

A hole in the frame sequence (a move lost despite the redundancy) is filled by repeating the last
consumed input with jump and custom flags cleared — up to 30 frames; past that it jumps ahead. This
keeps the server on the client's timeline; with the previous input held, it is almost always right.

After 0.25 s with nothing consumed, the server simulates the character with a neutral input every
tick (keeping the crouch), without claiming a frame — a paused or dead client does not leave its
character floating. No ack goes out for those ticks.

Every tick that consumed something answers `client_receive_move_ack(frame, packed_state)`.

**Missing on purpose:** there is no time-discrepancy detection. A client sending moves faster than
real time is held back only by the queue limit and the two-per-tick drain. There is also no clock
synchronisation (Overwatch-style client speed-up/slow-down to keep the server's buffer at a target);
drift shows up as occasional starvation and re-buffering.

### 4. `PredictedMovementBackend`

Bigger than "the small one" the plan expected, because it also carries the protocol — but the
movement modes still did not change by a line. `tick`/`tick_2d` share one template over the component
type, which is the only place 2D and 3D had to meet.

### 5. Layered moves during a replay

As planned, with one change: the deep copy is an argument (`copy_from(other, true)`,
`duplicate_state(true)`) used by the history, not something `copy_from` switches on by itself when
the backend resims. `copy_from` runs twice per substep, and deep-copying there would also replace the
move objects every tick — a game holding a reference to its dash would find it gone from the list.

The part the plan did not foresee: a move queued **outside** a tick — from a button handler — exists
on one machine only, and a rollback to before it simply drops it. The answer is
`MovementInput.custom_flags` and the `custom_input_flags` signal: the button sets a bit, the move is
queued from the signal, and the signal fires inside that tick on the client, on the server and on
every replay. `project/scripts/online_pawn.gd` dashes this way.

### 6. The movement base cannot be rewound

As predicted. `rollback_to_state` drops the base; a replay collides against the present world.
Characters replayed while standing on a fast platform drift and are corrected on the next ack.

## Replication on the owning client

The plan did not mention this, and it is the part most likely to be "fixed" back into a bug.

Before prediction, the owning client applied the server's transform and movement mode like everybody
else. A predicting client must not — the server's values are a round trip old and would drag the
character back every packet. The obvious fix, a visibility filter on the synchronizers that hides the
owner, **breaks the prediction itself**: Godot treats a node whose synchronizers are all invisible to
a peer as invisible to that peer's RPCs too, so the acknowledgement RPC on the component (and any game
RPC on the pawn root) fails with "Attempt to call an RPC to a peer that cannot see this node".

So the data still goes to the owner and is dropped on arrival: the `Pawn`'s transform synchronizer
now mirrors `Pawn.replicated_position` / `replicated_rotation`, whose setters ignore the value when
`is_locally_predicted()`, and the component's mirrors `replicated_movement_mode` the same way. The
check is made when the value arrives, which also sidesteps the ordering problem: the `Pawn` sets up
its replication in `_ready`, usually before the component that marks it predicted has run its own.

## Testing

`project/tests/test_replay.gd` — the one rule, above. One process, deterministic.

`project/tests/test_prediction.gd` — end to end: a dedicated server and a client in two processes
over ENet on loopback, driven by `project/tests/prediction_probe.gd` (loaded into both through the
demo's `--probe` flag, at `/root/Probe` on each so they can RPC). The client walks, jumps, dashes (as a
custom flag) and runs into a wall, then asks the server to move its character 2 m sideways. It checks
that the character moves on the tick the key goes down, that jump and dash are predicted, that the
scripted stretch produces **zero** corrections and zero disagreement at every ack, that the server's
move is corrected once, by about 2 m, with about a round trip of ticks replayed, and that agreement
resumes after it. It runs on clean loopback and at 50 ms each way, 10 ms jitter, 5 % loss.

It was proved able to fail three ways: letting the owner apply the replicated transform (5 checks
red), dropping the custom flags on the server (2–3 red), and rolling back without replaying (2 red).
It also passed, by hand, at 120 ms each way, 40 ms jitter and 15 % loss.

The debug network queue is in the backend (`send_call` / `flush_delayed_calls`): every prediction
RPC goes through it, in both directions, delayed by latency ± jitter and dropped at the loss rate,
and delivered in time order so jitter reorders like a real network. It only touches prediction
traffic, and it is compiled out of release builds. The demo takes `--net-latency`, `--net-jitter`,
`--net-loss` so a windowed client can be tried by hand — which is still the only way to judge feel.

## Not done

- **Simulated proxy smoothing.** Other players still snap to each replicated transform. Interpolation
  (or Unreal's `SimulateMovement` extrapolation plus mesh smoothing) is the next most visible thing.
- **Smoothing the correction itself.** A correction moves the capsule at once. Unreal smooths the
  visible mesh toward the corrected capsule over a few frames; a game can do the same from
  `prediction_corrected` today.
- **Clock sync and anti-cheat** — see item 3.
- **Moves queued outside the tick** are not predicted; that is what custom flags are for.
- **Root motion** replays against the animation as it is now, not as it was.
- **2D end to end.** The 2D component has the same code path and passes `test_replay.gd`, but there
  is no 2D online level, so the two-process test covers 3D only.
- **Bandwidth.** One ack per consumed tick per predicted pawn, full state each time. Unreal sends a
  tiny "good move" ack and a full adjustment only on disagreement; that is the obvious saving.
