#ifndef ROOT_MOTION_LAYERED_MOVE_H
#define ROOT_MOTION_LAYERED_MOVE_H

#include <godot_cpp/variant/node_path.hpp>

#include "movement/layered_move.h"

using namespace godot;

namespace godot
{
class AnimationMixer;
}

namespace GFGD
{
// Motion driven by an animation, laid over whatever mode is running.
//
// This is the whole of root motion, and it is a [LayeredMove] rather than new
// machinery - which is the point the generate/execute split was making. The
// animation proposes a velocity; the active mode still does the sweeping, the
// collision and the floor. A root-motion attack therefore cannot walk through a
// wall or off a ledge, and needed no special case anywhere to stop it.
//
// The mixer's process callback mode has to be the physics one. Root motion is
// consumed per simulated tick, and a mixer advancing on the render frame hands
// out deltas that do not line up with it.
//
// Works on either component. The delta is read the same way in both, because
// AnimationMixer::get_root_motion_position() is the same call - but note what
// that means for a 2D pawn: Godot accumulates root motion only from
// TYPE_POSITION_3D tracks, and a 2D position track contributes nothing at all.
// A 2D character therefore needs a plain Node3D hung under it as the carrier for
// the track to address. That is legal and it works; the node is never actually
// moved, in 2D or in 3D, because a root motion track is excluded from being
// applied. The x and y of the delta are then pixels.
class RootMotionLayeredMove : public LayeredMove
{
	GDCLASS(RootMotionLayeredMove, LayeredMove)

private:
	// Relative to the pawn's root, which is the node the movement component
	// drives.
	NodePath animation_mixer_path;

	AnimationMixer* mixer;

	// The delta arrives once per frame, but generate_move runs once per substep -
	// a long frame is split into several. Applying the same delta to each would
	// move the character two or three times as far on exactly the frames where
	// things were already going badly.
	int64_t last_consumed_frame;

public:
	RootMotionLayeredMove();
	~RootMotionLayeredMove();

	virtual void on_start(const Ref<MovementTickParams>& params) override;
	virtual void generate_move(const Ref<MovementTickParams>& params, const Ref<ProposedMove>& out_proposal) override;

	NodePath get_animation_mixer_path() const { return animation_mixer_path; }
	void set_animation_mixer_path(const NodePath& value) { animation_mixer_path = value; }

	AnimationMixer* get_mixer() const { return mixer; }
	void set_mixer(AnimationMixer* value) { mixer = value; }

protected:
	static void _bind_methods();
};
}

#endif
