#include "movement/predicted_movement_backend.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "framework/pawn.h"
#include "framework/world.h"
#include "movement/character_movement_component.h"
#include "movement/character_movement_component_2d.h"

using namespace godot;
using namespace GFGD;

// About four seconds at 60Hz. A client this far ahead of its acknowledgements
// has lost the server; the oldest moves go, and the correction that arrives
// once it is back is adopted as the present instead of replayed.
static constexpr int MAX_SAVED_MOVES = 256;

// A client sends at most max_moves_per_packet a tick; anything beyond this in
// one call is not a client, and is not read.
static constexpr int MAX_MOVES_ACCEPTED_PER_CALL = 64;

// How far ahead of the server a client may queue. Past this the newest moves are
// refused, and the client simply sends them again, because they are still
// unacknowledged.
static constexpr int MAX_QUEUED_MOVES = 64;

// A hole in the move stream no longer than this is filled by repeating the last
// move, which keeps the server on the client's timeline. A longer one is jumped
// over instead, because the client has long since stopped predicting it.
static constexpr int64_t MAX_SYNTHESIZED_GAP = 30;

// How long a server waits for a starved client before simulating the character
// on its own with no input, so that a paused or disconnected client does not
// leave a character hanging in mid-air.
static constexpr double FORCED_UPDATE_SECONDS = 0.25;

static const char* SERVER_RECEIVE_MOVES = "server_receive_moves";
static const char* CLIENT_RECEIVE_MOVE_ACK = "client_receive_move_ack";

PredictedMovementBackend::PredictedMovementBackend()
	: role(PREDICTION_ROLE_NONE)
	, sim_frame(0)
	, sim_time_ms(0.0)
	, replaying(false)
	, last_acked_frame(-1)
	, has_pending_ack(false)
	, pending_ack_frame(-1)
	, correction_count(0)
	, ack_count(0)
	, last_replayed_ticks(0)
	, total_replayed_ticks(0)
	, last_correction_error(0.0f)
	, last_ack_error(0.0f)
	, last_received_frame(-1)
	, last_consumed_frame(-1)
	, buffering(true)
	, starved_seconds(0.0)
	, starved_tick_count(0)
	, synthesized_move_count(0)
	, forced_update_count(0)
	, input_buffer_ticks(2)
	, max_moves_per_packet(8)
	, debug_latency_ms(0)
	, debug_jitter_ms(0)
	, debug_packet_loss(0.0f)
{
	ProjectSettings* settings = ProjectSettings::get_singleton();
	if (settings == nullptr)
	{
		return;
	}

	input_buffer_ticks = MAX(0, (int)settings->get_setting("application/game_framework/prediction/input_buffer_ticks", 2));
	max_moves_per_packet = CLAMP((int)settings->get_setting("application/game_framework/prediction/max_moves_per_packet", 8), 1, MAX_MOVES_ACCEPTED_PER_CALL);

#ifdef DEBUG_ENABLED
	// Debug builds only. A release build ignores these, so a setting left on
	// while testing cannot ship as real lag.
	debug_latency_ms = MAX(0, (int)settings->get_setting("application/game_framework/debug/network_latency_ms", 0));
	debug_jitter_ms = MAX(0, (int)settings->get_setting("application/game_framework/debug/network_jitter_ms", 0));
	debug_packet_loss = CLAMP((float)settings->get_setting("application/game_framework/debug/network_packet_loss_percent", 0.0f), 0.0f, 100.0f) * 0.01f;
#endif
}

PredictedMovementBackend::~PredictedMovementBackend()
{
}

// --- Role ---------------------------------------------------------------------

PredictedMovementBackend::PredictionRole PredictedMovementBackend::resolve_role(Pawn* pawn, bool prediction_enabled)
{
	if (!prediction_enabled || pawn == nullptr)
	{
		return PREDICTION_ROLE_LOCAL;
	}

	World* world = pawn->get_world();
	if (world == nullptr || !world->is_networked())
	{
		return PREDICTION_ROLE_LOCAL;
	}

	if (world->has_authority())
	{
		// The host's own pawn and every bot answer to the server, and there is
		// nobody to predict for.
		const int owner_peer = pawn->get_owner_peer_id();
		const bool remote_owner = owner_peer != World::SERVER_PEER_ID && owner_peer != world->get_local_peer_id();
		return remote_owner ? PREDICTION_ROLE_SERVER : PREDICTION_ROLE_LOCAL;
	}

	return pawn->is_locally_controlled() ? PREDICTION_ROLE_AUTONOMOUS_PROXY : PREDICTION_ROLE_NONE;
}

void PredictedMovementBackend::reset_for_role(PredictionRole new_role)
{
	role = new_role;
	replaying = false;

	saved_moves.clear();
	last_acked_frame = -1;
	has_pending_ack = false;
	pending_ack_frame = -1;
	pending_ack_state.unref();

	incoming_moves.clear();
	last_received_frame = -1;
	last_consumed_frame = -1;
	last_consumed_input.unref();
	buffering = true;
	starved_seconds = 0.0;
}

// --- Ticking ------------------------------------------------------------------

void PredictedMovementBackend::tick(CharacterMovementComponent* component, double delta)
{
	tick_impl(component, delta);
}

void PredictedMovementBackend::tick_2d(CharacterMovementComponent2D* component, double delta)
{
	tick_impl(component, delta);
}

template <typename TComponent>
void PredictedMovementBackend::tick_impl(TComponent* component, double delta)
{
	if (component == nullptr || delta <= 0.0)
	{
		return;
	}

	// Asked every tick rather than once: possession, and with it ownership, is
	// decided after the component is in the tree and can change at any time.
	const PredictionRole new_role = resolve_role(component->get_pawn(), component->get_client_prediction());
	if (new_role != role)
	{
		reset_for_role(new_role);
	}

	flush_delayed_calls(component);

	switch (role)
	{
		case PREDICTION_ROLE_AUTONOMOUS_PROXY:
			tick_autonomous(component, delta);
			break;

		case PREDICTION_ROLE_SERVER:
			tick_server(component, delta);
			break;

		case PREDICTION_ROLE_LOCAL:
			sim_frame++;
			sim_time_ms += delta * 1000.0;
			component->simulate(delta);
			break;

		case PREDICTION_ROLE_NONE:
		default:
			break;
	}
}

template <typename TComponent>
void PredictedMovementBackend::tick_autonomous(TComponent* component, double delta)
{
	// Corrections first, so the tick about to be predicted starts from the
	// corrected present rather than being thrown away by the next one.
	apply_pending_ack(component);

	sim_frame++;
	sim_time_ms += delta * 1000.0;

	component->simulate(delta);

	SavedMove move;
	move.frame = sim_frame;
	move.delta = delta;
	move.sim_time_ms = sim_time_ms;
	move.input = component->get_input()->duplicate_input();
	move.state = component->get_state()->duplicate_state(true);
	saved_moves.push_back(move);

	while (saved_moves.size() > MAX_SAVED_MOVES)
	{
		saved_moves.remove_at(0);
	}

	send_moves(component);
}

template <typename TComponent>
void PredictedMovementBackend::apply_pending_ack(TComponent* component)
{
	if (!has_pending_ack)
	{
		return;
	}

	has_pending_ack = false;

	const int64_t frame = pending_ack_frame;
	const Ref<MovementState> authority = pending_ack_state;
	pending_ack_state.unref();

	if (authority.is_null() || frame <= last_acked_frame)
	{
		return;
	}

	ack_count++;

	const float position_tolerance = component->get_prediction_position_tolerance();
	const float velocity_tolerance = component->get_prediction_velocity_tolerance();

	int index = -1;
	for (int i = 0; i < saved_moves.size(); i++)
	{
		if (saved_moves[i].frame == frame)
		{
			index = i;
			break;
		}
	}

	if (index < 0)
	{
		// Older than anything still held: the history was trimmed while the
		// server was silent, and there is nothing left to compare it with. A
		// newer acknowledgement will follow.
		if (!saved_moves.is_empty() && frame < saved_moves[0].frame)
		{
			return;
		}

		// At or past everything predicted - nothing to replay, so the answer is
		// simply the present.
		last_acked_frame = frame;

		const Ref<MovementState> present = component->get_state();
		last_ack_error = present->get_position().distance_to(authority->get_position());
		if (present->should_reconcile(authority, position_tolerance, velocity_tolerance))
		{
			Ref<MovementState> merged = present->duplicate_state(true);
			merged->set_position(authority->get_position());
			merged->set_rotation(authority->get_rotation());
			merged->set_velocity(authority->get_velocity());
			merged->set_movement_mode(authority->get_movement_mode());
			merged->set_is_crouching(authority->get_is_crouching());
			merged->set_extra(authority->get_extra());
			merged->clear_base();

			last_correction_error = present->get_position().distance_to(authority->get_position());
			component->rollback_to_state(merged);

			correction_count++;
			last_replayed_ticks = 0;
			component->emit_signal("prediction_corrected", frame, 0, last_correction_error);
		}

		saved_moves.clear();
		return;
	}

	last_acked_frame = frame;

	const SavedMove acked = saved_moves[index];
	saved_moves = saved_moves.slice(index + 1);

	last_ack_error = acked.state->get_position().distance_to(authority->get_position());

	if (!acked.state->should_reconcile(authority, position_tolerance, velocity_tolerance))
	{
		// The overwhelmingly common answer. Snapping to a state that is within
		// tolerance would make the character twitch on every packet.
		return;
	}

	// The server's word on everything it can speak for, and this client's own
	// history for what it cannot: layered moves are objects and do not cross
	// the wire, and the base's id is an ObjectID that means nothing here.
	Ref<MovementState> merged = acked.state->duplicate_state(true);
	merged->set_position(authority->get_position());
	merged->set_rotation(authority->get_rotation());
	merged->set_velocity(authority->get_velocity());
	merged->set_movement_mode(authority->get_movement_mode());
	merged->set_is_crouching(authority->get_is_crouching());
	merged->set_extra(authority->get_extra());
	merged->clear_base();

	last_correction_error = acked.state->get_position().distance_to(authority->get_position());

	component->rollback_to_state(merged);
	on_rollback(merged, frame);
	sim_time_ms = acked.sim_time_ms;

	// The replay itself: every move not yet acknowledged, in order, from the
	// corrected state. It collides against the world as it is now rather than
	// as it was - other characters and platforms have moved on - which is
	// accepted rather than solved; the next correction squares what is left.
	replaying = true;
	for (int i = 0; i < saved_moves.size(); i++)
	{
		SavedMove& move = saved_moves.write[i];
		sim_frame = move.frame;
		sim_time_ms = move.sim_time_ms;

		component->simulate_with_input(move.input, move.delta);
		move.state = component->get_state()->duplicate_state(true);
	}
	replaying = false;

	correction_count++;
	last_replayed_ticks = saved_moves.size();
	total_replayed_ticks += last_replayed_ticks;

	component->emit_signal("prediction_corrected", frame, last_replayed_ticks, last_correction_error);
}

template <typename TComponent>
void PredictedMovementBackend::tick_server(TComponent* component, double delta)
{
	if (buffering && incoming_moves.size() >= MAX(1, input_buffer_ticks))
	{
		buffering = false;
	}

	int moves_this_tick = 0;
	if (!buffering)
	{
		if (incoming_moves.is_empty())
		{
			buffering = true;
		}
		else
		{
			moves_this_tick = 1;

			// Well past the buffer means packets arrived in a clump, or this
			// machine's clock runs a little slow. Two a tick drains it; left
			// alone, the extra would be permanent added latency.
			if (incoming_moves.size() > input_buffer_ticks * 2 + 2)
			{
				moves_this_tick = 2;
			}
		}
	}

	if (moves_this_tick == 0)
	{
		starved_tick_count++;
		starved_seconds += delta;

		if (starved_seconds < FORCED_UPDATE_SECONDS)
		{
			return;
		}

		// Nothing from the client for a while. Keep simulating with no input, so
		// gravity still applies and a platform still carries, without claiming a
		// frame the client never sent - no acknowledgement goes out for this.
		Ref<MovementInput> idle;
		idle.instantiate();
		idle->set_frame(last_consumed_frame);
		if (last_consumed_input.is_valid())
		{
			idle->set_want_crouch(last_consumed_input->get_want_crouch());
		}

		sim_frame++;
		sim_time_ms += delta * 1000.0;
		component->simulate_with_input(idle, delta);
		forced_update_count++;
		return;
	}

	starved_seconds = 0.0;

	bool consumed = false;
	for (int i = 0; i < moves_this_tick; i++)
	{
		const Ref<MovementInput> move = take_next_server_move();
		if (move.is_null())
		{
			break;
		}

		sim_frame++;
		sim_time_ms += delta * 1000.0;
		component->simulate_with_input(move, delta);
		consumed = true;
	}

	if (consumed)
	{
		Pawn* pawn = component->get_pawn();
		if (pawn != nullptr)
		{
			send_ack(component, pawn->get_owner_peer_id(), component->get_state());
		}
	}
}

Ref<MovementInput> PredictedMovementBackend::take_next_server_move()
{
	if (incoming_moves.is_empty())
	{
		return Ref<MovementInput>();
	}

	const Ref<MovementInput> head = incoming_moves[0];
	const int64_t gap = head->get_frame() - last_consumed_frame;

	// A move lost despite the redundancy. Repeating the one before it - with the
	// one-shot parts cleared, so a jump is not taken twice - keeps the server on
	// the client's timeline, and the held input it repeats is almost always what
	// the client really sent.
	if (last_consumed_frame >= 0 && gap > 1 && gap <= MAX_SYNTHESIZED_GAP)
	{
		Ref<MovementInput> filler;
		filler.instantiate();
		if (last_consumed_input.is_valid())
		{
			filler->copy_from(last_consumed_input);
		}
		filler->set_frame(last_consumed_frame + 1);
		filler->set_want_jump(false);
		filler->set_custom_flags(0);

		last_consumed_frame = filler->get_frame();
		last_consumed_input = filler;
		synthesized_move_count++;
		return filler;
	}

	incoming_moves.remove_at(0);
	last_consumed_frame = head->get_frame();
	last_consumed_input = head;
	return head;
}

void PredictedMovementBackend::on_rollback(const Ref<MovementState>& new_state, int64_t new_frame)
{
	sim_frame = new_frame;
}

// --- Receiving ----------------------------------------------------------------

void PredictedMovementBackend::receive_moves(int64_t first_frame, const PackedVector3Array& move_inputs, const PackedInt32Array& flags)
{
	if (role != PREDICTION_ROLE_SERVER)
	{
		return;
	}

	const int count = MIN(MIN((int)move_inputs.size(), (int)flags.size()), MAX_MOVES_ACCEPTED_PER_CALL);

	for (int i = 0; i < count; i++)
	{
		const int64_t frame = first_frame + i;

		// Every packet repeats the moves before it, so most of what arrives has
		// been seen already.
		if (frame <= last_received_frame || frame <= last_consumed_frame)
		{
			continue;
		}

		if (incoming_moves.size() >= MAX_QUEUED_MOVES)
		{
			break;
		}

		const uint32_t bits = (uint32_t)flags[i];

		Ref<MovementInput> move;
		move.instantiate();
		move->set_frame(frame);

		// Clamped again here, whatever the client did: the length of this vector
		// is how fast the character may go, and it came from someone else's
		// machine.
		const Vector3 move_input = move_inputs[i];
		move->set_move_input(move_input.is_finite() ? move_input.limit_length(1.0f) : Vector3());
		move->set_want_jump((bits & WIRE_FLAG_JUMP) != 0);
		move->set_want_crouch((bits & WIRE_FLAG_CROUCH) != 0);
		move->set_custom_flags((int)((bits >> WIRE_CUSTOM_FLAGS_SHIFT) & MovementInput::CUSTOM_FLAGS_MASK));

		incoming_moves.push_back(move);
		last_received_frame = frame;
	}
}

void PredictedMovementBackend::receive_ack(int64_t frame, const Ref<MovementState>& authority_state)
{
	if (role != PREDICTION_ROLE_AUTONOMOUS_PROXY || authority_state.is_null())
	{
		return;
	}

	// Only the newest matters: an older one describes a tick the newer one has
	// already moved past.
	if (has_pending_ack && frame <= pending_ack_frame)
	{
		return;
	}

	has_pending_ack = true;
	pending_ack_frame = frame;
	pending_ack_state = authority_state;
}

// --- Sending ------------------------------------------------------------------

void PredictedMovementBackend::send_moves(Node* node)
{
	const int count = MIN((int)saved_moves.size(), max_moves_per_packet);
	if (count <= 0)
	{
		return;
	}

	// The newest moves, oldest first. Unreliable, so each one rides in several
	// consecutive packets: a single lost packet costs nothing, and the server
	// never has to wait for a resend.
	const int start = saved_moves.size() - count;

	PackedVector3Array move_inputs;
	PackedInt32Array flags;
	move_inputs.resize(count);
	flags.resize(count);

	for (int i = 0; i < count; i++)
	{
		const Ref<MovementInput>& input = saved_moves[start + i].input;

		uint32_t bits = 0;
		bits |= input->get_want_jump() ? WIRE_FLAG_JUMP : 0;
		bits |= input->get_want_crouch() ? WIRE_FLAG_CROUCH : 0;
		bits |= ((uint32_t)input->get_custom_flags() & MovementInput::CUSTOM_FLAGS_MASK) << WIRE_CUSTOM_FLAGS_SHIFT;

		move_inputs.set(i, input->get_move_input());
		flags.set(i, (int32_t)bits);
	}

	Array arguments;
	arguments.push_back(World::SERVER_PEER_ID);
	arguments.push_back(StringName(SERVER_RECEIVE_MOVES));
	arguments.push_back(saved_moves[start].frame);
	arguments.push_back(move_inputs);
	arguments.push_back(flags);
	send_call(node, arguments);
}

void PredictedMovementBackend::send_ack(Node* node, int peer_id, const Ref<MovementState>& state)
{
	Array arguments;
	arguments.push_back(peer_id);
	arguments.push_back(StringName(CLIENT_RECEIVE_MOVE_ACK));
	arguments.push_back(last_consumed_frame);
	arguments.push_back(pack_state(state));
	send_call(node, arguments);
}

void PredictedMovementBackend::send_call(Node* node, const Array& arguments)
{
	if (node == nullptr || !node->is_inside_tree())
	{
		return;
	}

	if (debug_latency_ms <= 0 && debug_jitter_ms <= 0 && debug_packet_loss <= 0.0f)
	{
		node->callv("rpc_id", arguments);
		return;
	}

	if (debug_packet_loss > 0.0f && UtilityFunctions::randf() < debug_packet_loss)
	{
		return;
	}

	int64_t delay_ms = debug_latency_ms;
	if (debug_jitter_ms > 0)
	{
		delay_ms += UtilityFunctions::randi_range(-debug_jitter_ms, debug_jitter_ms);
	}

	DelayedCall call;
	call.deliver_at_usec = Time::get_singleton()->get_ticks_usec() + (uint64_t)MAX((int64_t)0, delay_ms) * 1000;
	call.arguments = arguments;

	// Kept in delivery order, so jitter reorders packets the way a real network
	// does and flushing is a walk from the front.
	int insert_at = delayed_calls.size();
	while (insert_at > 0 && delayed_calls[insert_at - 1].deliver_at_usec > call.deliver_at_usec)
	{
		insert_at--;
	}
	delayed_calls.insert(insert_at, call);
}

void PredictedMovementBackend::flush_delayed_calls(Node* node)
{
	if (delayed_calls.is_empty())
	{
		return;
	}

	if (node == nullptr || !node->is_inside_tree())
	{
		delayed_calls.clear();
		return;
	}

	const uint64_t now = Time::get_singleton()->get_ticks_usec();

	int delivered = 0;
	while (delivered < delayed_calls.size() && delayed_calls[delivered].deliver_at_usec <= now)
	{
		node->callv("rpc_id", delayed_calls[delivered].arguments);
		delivered++;
	}

	if (delivered > 0)
	{
		delayed_calls = delayed_calls.slice(delivered);
	}
}

// --- Wire format --------------------------------------------------------------

Array PredictedMovementBackend::pack_state(const Ref<MovementState>& state)
{
	Array packed;
	if (state.is_null())
	{
		return packed;
	}

	packed.push_back(state->get_position());
	packed.push_back(state->get_rotation());
	packed.push_back(state->get_velocity());
	packed.push_back(state->get_movement_mode());
	packed.push_back(state->get_is_crouching());
	packed.push_back(state->get_extra());
	return packed;
}

Ref<MovementState> PredictedMovementBackend::unpack_state(const Array& packed)
{
	if (packed.size() < 6)
	{
		return Ref<MovementState>();
	}

	// Checked field by field: this arrived over the network, and a wrong type
	// would otherwise convert silently into a zero.
	if (packed[0].get_type() != Variant::VECTOR3 || packed[1].get_type() != Variant::BASIS || packed[2].get_type() != Variant::VECTOR3
		|| (packed[3].get_type() != Variant::STRING_NAME && packed[3].get_type() != Variant::STRING) || packed[4].get_type() != Variant::BOOL
		|| packed[5].get_type() != Variant::DICTIONARY)
	{
		return Ref<MovementState>();
	}

	Ref<MovementState> state;
	state.instantiate();
	state->set_position(packed[0]);
	state->set_rotation(packed[1]);
	state->set_velocity(packed[2]);
	state->set_movement_mode(StringName(packed[3]));
	state->set_is_crouching(packed[4]);
	state->set_extra(packed[5]);
	return state;
}

void PredictedMovementBackend::_bind_methods()
{
	BIND_ENUM_CONSTANT(PREDICTION_ROLE_NONE);
	BIND_ENUM_CONSTANT(PREDICTION_ROLE_LOCAL);
	BIND_ENUM_CONSTANT(PREDICTION_ROLE_AUTONOMOUS_PROXY);
	BIND_ENUM_CONSTANT(PREDICTION_ROLE_SERVER);

	ClassDB::bind_method(D_METHOD("get_role"), &PredictedMovementBackend::get_role);
	ClassDB::bind_method(D_METHOD("is_replaying"), &PredictedMovementBackend::is_replaying);

	ClassDB::bind_method(D_METHOD("get_last_acked_frame"), &PredictedMovementBackend::get_last_acked_frame);
	ClassDB::bind_method(D_METHOD("get_pending_move_count"), &PredictedMovementBackend::get_pending_move_count);
	ClassDB::bind_method(D_METHOD("get_correction_count"), &PredictedMovementBackend::get_correction_count);
	ClassDB::bind_method(D_METHOD("get_ack_count"), &PredictedMovementBackend::get_ack_count);
	ClassDB::bind_method(D_METHOD("get_last_replayed_ticks"), &PredictedMovementBackend::get_last_replayed_ticks);
	ClassDB::bind_method(D_METHOD("get_total_replayed_ticks"), &PredictedMovementBackend::get_total_replayed_ticks);
	ClassDB::bind_method(D_METHOD("get_last_correction_error"), &PredictedMovementBackend::get_last_correction_error);
	ClassDB::bind_method(D_METHOD("get_last_ack_error"), &PredictedMovementBackend::get_last_ack_error);

	ClassDB::bind_method(D_METHOD("get_last_consumed_frame"), &PredictedMovementBackend::get_last_consumed_frame);
	ClassDB::bind_method(D_METHOD("get_queued_move_count"), &PredictedMovementBackend::get_queued_move_count);
	ClassDB::bind_method(D_METHOD("get_starved_tick_count"), &PredictedMovementBackend::get_starved_tick_count);
	ClassDB::bind_method(D_METHOD("get_synthesized_move_count"), &PredictedMovementBackend::get_synthesized_move_count);
	ClassDB::bind_method(D_METHOD("get_forced_update_count"), &PredictedMovementBackend::get_forced_update_count);
}
