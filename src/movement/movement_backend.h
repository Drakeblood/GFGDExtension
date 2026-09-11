#ifndef MOVEMENT_BACKEND_H
#define MOVEMENT_BACKEND_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>

#include "movement/movement_types.h"

using namespace godot;

namespace GFGD
{
class CharacterMovementComponent;
class CharacterMovementComponent2D;

// Who decides when the simulation runs, and how many times.
//
// This is the seam client prediction plugs into, and it is here now for one
// reason: it cannot be added afterwards. A component that ticks itself has the
// number of steps per frame baked into it, and replaying three inputs after a
// server correction means exactly that number changing. Splitting it costs one
// small class today and saves rewriting every mode later.
//
// The standalone backend below runs one step per physics frame and nothing else.
// A predicted backend would implement the same five methods, call simulate()
// once per unacknowledged input after a correction, and answer should_resim with
// true - without a single line changing in any movement mode.
class MovementBackend : public Object
{
	GDCLASS(MovementBackend, Object)

public:
	MovementBackend();
	~MovementBackend();

	// Called once per physics frame. Decides how many simulation steps that is.
	//
	// Two overloads rather than one over a shared base, because there is no shared
	// base: Godot's 2D and 3D hierarchies meet nowhere. Both do the same thing.
	virtual void tick(CharacterMovementComponent* component, double delta);
	virtual void tick_2d(CharacterMovementComponent2D* component, double delta);

	virtual int64_t get_sim_frame() const { return 0; }
	virtual double get_sim_time_ms() const { return 0.0; }

	// Whether every step is the same length. False would mean a mode cannot
	// assume its delta, which matters to anything integrating by hand.
	virtual bool is_fixed_dt() const { return true; }

	// Whether this backend ever replays a step it has already run.
	virtual bool should_resim() const { return false; }

	// Snaps the simulation to an authoritative state. Does nothing while nothing
	// corrects us.
	virtual void on_rollback(const Ref<MovementState>& new_state, int64_t new_frame);

protected:
	static void _bind_methods();
};

// One simulation step per physics frame, no history, no replay.
//
// This is the whole of it for a server-authoritative game, and it is also what a
// single player game should keep using - there is nothing to predict when the
// machine simulating is the machine playing.
class StandaloneMovementBackend : public MovementBackend
{
	GDCLASS(StandaloneMovementBackend, MovementBackend)

private:
	int64_t sim_frame;
	double sim_time_ms;

public:
	StandaloneMovementBackend();
	~StandaloneMovementBackend();

	virtual void tick(CharacterMovementComponent* component, double delta) override;
	virtual void tick_2d(CharacterMovementComponent2D* component, double delta) override;

	virtual int64_t get_sim_frame() const override { return sim_frame; }
	virtual double get_sim_time_ms() const override { return sim_time_ms; }

protected:
	static void _bind_methods();
};
}

#endif
