#ifndef PREDICTED_MOVEMENT_BACKEND_H
#define PREDICTED_MOVEMENT_BACKEND_H

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "movement/movement_backend.h"

using namespace godot;

namespace godot
{
class Node;
}

namespace GFGD
{
class Pawn;

// Client-side prediction for one character, and the server half that feeds it.
//
// The same object runs on every peer and decides from the pawn which job it has:
//
//   the owning client   simulates every tick immediately with the input it just
//                       sampled, keeps (input, resulting state) per tick until
//                       the server acknowledges it, sends the unacknowledged
//                       inputs every tick, and on a correction rewinds to the
//                       acknowledged state and replays everything after it.
//   the server          buffers the inputs that arrive, consumes one per tick,
//                       and answers with the tick it reached and the state that
//                       came out of it.
//   anybody else        does nothing - a simulated proxy is moved by the
//                       transform replication the Pawn already has.
//   no network, or a pawn the server itself drives (the host's own, a bot)
//                       one step per physics frame, exactly like
//                       StandaloneMovementBackend.
//
// Not one line of any movement mode knows any of this. A mode runs once per
// simulate() call, and this class only decides how many calls there are and
// what state they start from - which is the seam MovementBackend was cut for.
class PredictedMovementBackend : public MovementBackend
{
	GDCLASS(PredictedMovementBackend, MovementBackend)

public:
	enum PredictionRole
	{
		// Nothing to simulate here: a simulated proxy, or not set up yet.
		PREDICTION_ROLE_NONE = 0,

		// Simulates on its own clock, like the standalone backend.
		PREDICTION_ROLE_LOCAL = 1,

		// The owning client, predicting.
		PREDICTION_ROLE_AUTONOMOUS_PROXY = 2,

		// The server, consuming a remote client's inputs.
		PREDICTION_ROLE_SERVER = 3,
	};

	// Bits of the per-move flags word on the wire. The high sixteen carry
	// MovementInput::custom_flags.
	static constexpr int WIRE_FLAG_JUMP = 1 << 0;
	static constexpr int WIRE_FLAG_CROUCH = 1 << 1;
	static constexpr int WIRE_CUSTOM_FLAGS_SHIFT = 16;

private:
	struct SavedMove
	{
		int64_t frame = 0;
		double delta = 0.0;

		// The simulation clock after this tick, so a replay can put it back.
		double sim_time_ms = 0.0;

		Ref<MovementInput> input;

		// What this client predicted the tick would produce - deep, so the
		// layered moves in it are this history's own.
		Ref<MovementState> state;
	};

	struct DelayedCall
	{
		uint64_t deliver_at_usec = 0;
		Array arguments;
	};

	PredictionRole role;

	int64_t sim_frame;
	double sim_time_ms;
	bool replaying;

	// --- Owning client ------------------------------------------------------

	Vector<SavedMove> saved_moves;
	int64_t last_acked_frame;

	// The newest acknowledgement that arrived since the last tick. Held rather
	// than acted on, because it arrives on the network poll and a replay runs
	// the solver, which belongs on the physics tick.
	bool has_pending_ack;
	int64_t pending_ack_frame;
	Ref<MovementState> pending_ack_state;

	int64_t correction_count;
	int64_t ack_count;
	int64_t last_replayed_ticks;
	int64_t total_replayed_ticks;
	float last_correction_error;

	// How far the prediction was from the server at the last acknowledgement,
	// corrected or not. What a test watches to see prediction staying exact.
	float last_ack_error;

	// --- Server -------------------------------------------------------------

	Vector<Ref<MovementInput>> incoming_moves;
	int64_t last_received_frame;
	int64_t last_consumed_frame;
	Ref<MovementInput> last_consumed_input;

	// Waiting for input_buffer_ticks moves to be queued before consuming again.
	// Set at the start and after every starvation, so the next packet that is a
	// little late finds a move waiting instead of another hole.
	bool buffering;
	double starved_seconds;

	int64_t starved_tick_count;
	int64_t synthesized_move_count;
	int64_t forced_update_count;

	// --- Tuning, read from the project settings once ------------------------

	int input_buffer_ticks;
	int max_moves_per_packet;

	// --- Debug network conditions ---------------------------------------------

	int debug_latency_ms;
	int debug_jitter_ms;
	float debug_packet_loss;
	Vector<DelayedCall> delayed_calls;

public:
	PredictedMovementBackend();
	~PredictedMovementBackend();

	virtual void tick(CharacterMovementComponent* component, double delta) override;
	virtual void tick_2d(CharacterMovementComponent2D* component, double delta) override;

	virtual int64_t get_sim_frame() const override { return sim_frame; }
	virtual double get_sim_time_ms() const override { return sim_time_ms; }
	virtual bool should_resim() const override { return true; }

	// Puts the clock back to the tick being replayed from. The component's state
	// is restored by the component itself - it is the one that owns the capsule,
	// the floor and the body a state has to be written into.
	virtual void on_rollback(const Ref<MovementState>& new_state, int64_t new_frame) override;

	PredictionRole get_role() const { return role; }
	bool is_replaying() const { return replaying; }

	// --- Called by the component's remote calls -----------------------------

	// Server: a client's latest unacknowledged moves, frames first_frame onward.
	void receive_moves(int64_t first_frame, const PackedVector3Array& move_inputs, const PackedInt32Array& flags);

	// Owning client: the server consumed everything up to frame and ended in
	// this state.
	void receive_ack(int64_t frame, const Ref<MovementState>& authority_state);

	// --- Diagnostics ----------------------------------------------------------

	int64_t get_last_acked_frame() const { return last_acked_frame; }
	int get_pending_move_count() const { return saved_moves.size(); }
	int64_t get_correction_count() const { return correction_count; }
	int64_t get_ack_count() const { return ack_count; }
	int64_t get_last_replayed_ticks() const { return last_replayed_ticks; }
	int64_t get_total_replayed_ticks() const { return total_replayed_ticks; }
	float get_last_correction_error() const { return last_correction_error; }
	float get_last_ack_error() const { return last_ack_error; }

	int64_t get_last_consumed_frame() const { return last_consumed_frame; }
	int get_queued_move_count() const { return incoming_moves.size(); }
	int64_t get_starved_tick_count() const { return starved_tick_count; }
	int64_t get_synthesized_move_count() const { return synthesized_move_count; }
	int64_t get_forced_update_count() const { return forced_update_count; }

	// Decides the role from the pawn. Public so the component can ask whether a
	// client should be ticking at all.
	static PredictionRole resolve_role(Pawn* pawn, bool prediction_enabled);

	// Packs and unpacks a state for the acknowledgement. The base is left out:
	// its id is an ObjectID, which means nothing on another machine.
	static Array pack_state(const Ref<MovementState>& state);
	static Ref<MovementState> unpack_state(const Array& packed);

protected:
	static void _bind_methods();

private:
	template <typename TComponent>
	void tick_impl(TComponent* component, double delta);

	template <typename TComponent>
	void tick_autonomous(TComponent* component, double delta);

	template <typename TComponent>
	void tick_server(TComponent* component, double delta);

	template <typename TComponent>
	void apply_pending_ack(TComponent* component);

	void reset_for_role(PredictionRole new_role);

	void send_moves(Node* node);
	void send_ack(Node* node, int peer_id, const Ref<MovementState>& state);

	// Every remote call this backend makes goes through here, so the debug
	// latency, jitter and loss apply to all of them and to nothing else.
	void send_call(Node* node, const Array& arguments);
	void flush_delayed_calls(Node* node);

	Ref<MovementInput> take_next_server_move();
};
}

VARIANT_ENUM_CAST(GFGD::PredictedMovementBackend::PredictionRole);

#endif
