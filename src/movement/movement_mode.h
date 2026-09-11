#ifndef MOVEMENT_MODE_H
#define MOVEMENT_MODE_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include <godot_cpp/variant/array.hpp>

#include "movement/movement_types.h"

using namespace godot;

namespace GFGD
{
// One way of moving: walking, falling, flying, or whatever a game invents.
//
// A Resource rather than a Node so a mode can be authored inline in the
// inspector and shared between pawns, and so a project can write one in GDScript
// without touching this extension.
//
// The two halves are deliberately separate:
//
//   generate_move   proposes motion. Side effect free, touches nothing, and can
//                   therefore be blended with other proposals - which is what
//                   will make a dash on top of a walk possible without the
//                   walking code ever hearing about dashes.
//   simulation_tick executes it. Sweeps, resolves collisions, writes the state.
//
// CharacterMovementComponent in Unreal fuses the two inside each Phys* function,
// and that is precisely why layering anything on top of it is painful. Keeping
// them apart costs nothing here and cannot be retrofitted later.
class MovementMode : public Resource
{
	GDCLASS(MovementMode, Resource)

public:
	MovementMode();
	~MovementMode();

	// Fills params->proposed_move. The proposal is pre-allocated and reused, so
	// an implementation writes into it rather than returning a new one.
	virtual void generate_move(const Ref<MovementTickParams>& params);

	// Reads params->proposed_move and writes params->out_state, which arrives
	// pre-filled with a copy of the starting state.
	virtual void simulation_tick(const Ref<MovementTickParams>& params);

	// Rules for leaving this mode, authored beside it. Checked after the tick, in
	// order, before the component's global ones.
	Array get_transitions() const { return transitions; }
	void set_transitions(const Array& value) { transitions = value; }

	GDVIRTUAL1(_generate_move, Ref<MovementTickParams>)
	GDVIRTUAL1(_simulation_tick, Ref<MovementTickParams>)

protected:
	static void _bind_methods();

private:
	Array transitions;
};

// The mode that is active when nothing else is, so that there is always one and
// no caller has to handle its absence. It proposes nothing and moves nothing.
class NullMovementMode : public MovementMode
{
	GDCLASS(NullMovementMode, MovementMode)

public:
	NullMovementMode();
	~NullMovementMode();

	virtual void generate_move(const Ref<MovementTickParams>& params) override;
	virtual void simulation_tick(const Ref<MovementTickParams>& params) override;

protected:
	static void _bind_methods();
};
}

#endif
