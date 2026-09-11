#ifndef LAYERED_MOVE_H
#define LAYERED_MOVE_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "movement/movement_types.h"

using namespace godot;

namespace GFGD
{
// Motion laid on top of whatever mode is running: a dash, a knockback, a gust of
// wind, an animation driving the character.
//
// A layered move only ever *proposes*. It never sweeps, never writes state, and
// never knows which mode is active - the mode executes the mixed result. That is
// the whole reason MovementMode was split into generate_move and
// simulation_tick: a proposal is side effect free, so proposals can be blended,
// and a dash becomes possible without the walking code ever hearing about
// dashes. Unreal's CharacterMovementComponent fuses the two inside each Phys*
// function, which is exactly why layering anything on it is painful.
//
// Queue one with CharacterMovementComponent::queue_layered_move.
class LayeredMove : public Resource
{
	GDCLASS(LayeredMove, Resource)

public:
	// What happens to the character's velocity when the move ends.
	enum FinishVelocityMode
	{
		// Whatever the move left behind carries on. A dash that ends mid-air keeps
		// its speed.
		KEEP_VELOCITY = 0,

		// Replaced outright with finish_velocity - for a move that should stop
		// dead, set it to zero.
		SET_VELOCITY = 1,

		// Kept in direction but capped, so a launch does not hand the character a
		// speed the rest of the simulation was never tuned for.
		CLAMP_VELOCITY = 2,
	};

private:
	ProposedMove::MixMode mix_mode;

	// Higher wins. Moves are applied lowest first, so the highest priority is the
	// last to overwrite anything.
	int priority;

	// Seconds. Zero is a single tick - an impulse. Negative runs until something
	// cancels it.
	float duration;

	FinishVelocityMode finish_velocity_mode;
	Vector3 finish_velocity;
	float finish_clamp_speed;

	// Set when the move starts, from the backend's simulation clock.
	double start_time_ms;
	bool started;

public:
	LayeredMove();
	~LayeredMove();

	// Called once, on the tick the move first runs.
	virtual void on_start(const Ref<MovementTickParams>& params);

	// Fills out_proposal with the motion this move wants. Must not move anything.
	virtual void generate_move(const Ref<MovementTickParams>& params, const Ref<ProposedMove>& out_proposal);

	// Default is the duration test; override for a move that ends on a condition.
	virtual bool is_finished(double sim_time_ms);

	ProposedMove::MixMode get_mix_mode() const { return mix_mode; }
	void set_mix_mode(ProposedMove::MixMode value) { mix_mode = value; }

	int get_priority() const { return priority; }
	void set_priority(int value) { priority = value; }

	float get_duration() const { return duration; }
	void set_duration(float value) { duration = value; }

	FinishVelocityMode get_finish_velocity_mode() const { return finish_velocity_mode; }
	void set_finish_velocity_mode(FinishVelocityMode value) { finish_velocity_mode = value; }

	Vector3 get_finish_velocity() const { return finish_velocity; }
	void set_finish_velocity(const Vector3& value) { finish_velocity = value; }

	float get_finish_clamp_speed() const { return finish_clamp_speed; }
	void set_finish_clamp_speed(float value) { finish_clamp_speed = value; }

	double get_start_time_ms() const { return start_time_ms; }
	void set_start_time_ms(double value) { start_time_ms = value; }

	bool get_started() const { return started; }
	void set_started(bool value) { started = value; }

	// How long this move has been running, in seconds.
	float get_elapsed(double sim_time_ms) const { return (float)((sim_time_ms - start_time_ms) * 0.001); }

	// Applies finish_velocity_mode to a velocity the move has just stopped
	// contributing to.
	Vector3 apply_finish_velocity(const Vector3& velocity) const;

	GDVIRTUAL1(_on_start, Ref<MovementTickParams>)
	GDVIRTUAL2(_generate_move, Ref<MovementTickParams>, Ref<ProposedMove>)
	GDVIRTUAL1R(bool, _is_finished, double)

protected:
	static void _bind_methods();
};

// Constant velocity for a while: a dash, a knockback, a conveyor, a gust.
//
// The one concrete move that ships, because it is the shape most of them are.
// Anything else is a Resource subclass away, in C++ or in GDScript.
class LinearVelocityLayeredMove : public LayeredMove
{
	GDCLASS(LinearVelocityLayeredMove, LayeredMove)

private:
	Vector3 velocity;

	// Fades the contribution to nothing across the duration, so a knockback eases
	// off instead of stopping like a wall. Ignored when the duration is not
	// positive, because there is nothing to fade across.
	bool decay_over_duration;

public:
	LinearVelocityLayeredMove();
	~LinearVelocityLayeredMove();

	virtual void generate_move(const Ref<MovementTickParams>& params, const Ref<ProposedMove>& out_proposal) override;

	Vector3 get_velocity() const { return velocity; }
	void set_velocity(const Vector3& value) { velocity = value; }

	bool get_decay_over_duration() const { return decay_over_duration; }
	void set_decay_over_duration(bool value) { decay_over_duration = value; }

protected:
	static void _bind_methods();
};
}

VARIANT_ENUM_CAST(GFGD::LayeredMove::FinishVelocityMode);

#endif
