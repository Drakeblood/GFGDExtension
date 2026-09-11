#include "movement/root_motion_layered_move.h"

#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/physics_body2d.hpp>
#include <godot_cpp/classes/physics_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"
#include "movement/character_movement_component_2d.h"

using namespace godot;
using namespace GFGD;

RootMotionLayeredMove::RootMotionLayeredMove()
	: mixer(nullptr)
	, last_consumed_frame(-1)
{
	// Horizontal movement comes from the animation, the up axis is left to
	// gravity. An attack that plays on the ground should still fall off a ledge
	// it is pushed over, and OVERRIDE_ALL would leave it hanging in the air.
	set_mix_mode(ProposedMove::OVERRIDE_ALL_EXCEPT_VERTICAL);

	// Runs until something cancels it, which is normally whatever is driving the
	// animation.
	set_duration(-1.0f);
}

RootMotionLayeredMove::~RootMotionLayeredMove()
{
}

void RootMotionLayeredMove::on_start(const Ref<MovementTickParams>& params)
{
	LayeredMove::on_start(params);

	last_consumed_frame = -1;

	if (mixer != nullptr || params.is_null())
	{
		return;
	}

	if (animation_mixer_path.is_empty())
	{
		return;
	}

	// Either dimension. Only the node the mixer hangs under differs; the delta it
	// reports is read by the same call in both.
	Node* body = nullptr;
	if (CharacterMovementComponent* component = params->get_component())
	{
		body = Object::cast_to<Node>(component->get_updated_body());
	}
	else if (CharacterMovementComponent2D* component_2d = params->get_component_2d())
	{
		body = Object::cast_to<Node>(component_2d->get_updated_body());
	}

	if (body == nullptr)
	{
		return;
	}

	mixer = Object::cast_to<AnimationMixer>(body->get_node_or_null(animation_mixer_path));
	if (mixer == nullptr)
	{
		WARN_PRINT(vformat("GFGD: RootMotionLayeredMove found no AnimationMixer at '%s' under the pawn root; it will propose nothing.", String(animation_mixer_path)));
		return;
	}

	if (mixer->get_callback_mode_process() != AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS)
	{
		WARN_PRINT(vformat("GFGD: the AnimationMixer at '%s' advances on the render frame, but root motion is consumed on the physics tick. Set callback_mode_process to Physics or the movement will not match the animation.", String(animation_mixer_path)));
	}
}

void RootMotionLayeredMove::generate_move(const Ref<MovementTickParams>& params, const Ref<ProposedMove>& out_proposal)
{
	ERR_FAIL_COND(params.is_null() || out_proposal.is_null());

	out_proposal->set_mix_mode(get_mix_mode());

	const Ref<MovementInput> input = params->get_input();
	const Ref<MovementState> state = params->get_start_state();
	const double delta = params->get_delta();

	if (mixer == nullptr || input.is_null() || state.is_null() || delta <= 0.0)
	{
		out_proposal->set_linear_velocity(Vector3());
		return;
	}

	// One delta per frame, however many substeps that frame turns into. The later
	// substeps contribute nothing rather than repeating the motion.
	if (input->get_frame() == last_consumed_frame)
	{
		out_proposal->set_linear_velocity(Vector3());
		return;
	}

	last_consumed_frame = input->get_frame();

	// The mixer reports the step the animation took in the character's own space,
	// so which way the character is facing decides where that step goes.
	const Vector3 local_delta = mixer->get_root_motion_position();

	// A 2D state carries no rotation - facing there is a sprite flip, not a
	// transform the solver knows about - so the delta is taken as authored and its
	// z dropped. An animation that has to run both ways in 2D either flips sign in
	// the track or is authored twice; there is no facing here to do it for you.
	Vector3 world_delta;
	if (params->get_component_2d() != nullptr)
	{
		world_delta = Vector3(local_delta.x, local_delta.y, 0.0f);
	}
	else
	{
		world_delta = state->get_rotation().xform(local_delta);
	}

	out_proposal->set_linear_velocity(world_delta / (float)delta);
}

void RootMotionLayeredMove::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_animation_mixer_path"), &RootMotionLayeredMove::get_animation_mixer_path);
	ClassDB::bind_method(D_METHOD("set_animation_mixer_path", "value"), &RootMotionLayeredMove::set_animation_mixer_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_mixer_path"), "set_animation_mixer_path", "get_animation_mixer_path");

	ClassDB::bind_method(D_METHOD("get_mixer"), &RootMotionLayeredMove::get_mixer);
	ClassDB::bind_method(D_METHOD("set_mixer", "value"), &RootMotionLayeredMove::set_mixer);
}
