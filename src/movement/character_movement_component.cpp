#include "movement/character_movement_component.h"

#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/physics_body3d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/physics_test_motion_parameters3d.hpp>
#include <godot_cpp/classes/physics_test_motion_result3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "framework/net_replication.h"
#include "framework/pawn.h"
#include "framework/world.h"
#include "movement/layered_move.h"
#include "movement/movement_backend.h"
#include "movement/movement_mode.h"
#include "movement/movement_mode_transition.h"
#include "movement/water_movement_transition.h"
#include "movement/water_volume.h"
#include "movement/modes/falling_mode.h"
#include "movement/modes/flying_mode.h"
#include "movement/modes/swimming_mode.h"
#include "movement/modes/walking_mode.h"

using namespace godot;
using namespace GFGD;

// Anything further than this from where the simulation left the body was moved
// by something else - a respawn, a teleport, a script. Squared, in metres.
static constexpr float TELEPORT_DISTANCE_SQUARED = 1.0e-6f;

// Below this the character is not going anywhere worth turning towards.
static constexpr float ROTATION_SPEED_THRESHOLD_SQUARED = 0.01f;

CharacterMovementComponent::CharacterMovementComponent()
	: updated_body_path(NodePath(".."))
	, starting_mode(StringName(MODE_WALKING))
	, updated_body(nullptr)
	, pawn(nullptr)
	, capsule_radius(0.5f)
	, capsule_half_height(1.0f)
	, standing_half_height(1.0f)
	, capsule_shape_node(nullptr)
	, backend(nullptr)
	, has_base_last_transform(false)
	, has_queued_mode(false)
	, jump_pressed_latch(false)
	, crouch_latch(false)
	, up_direction(Vector3(0, 1, 0))
	, gravity(9.8f)
	, gravity_scale(1.0f)
	, max_walk_speed(6.0f)
	, min_analog_walk_speed(0.0f)
	, can_crouch(true)
	, crouched_half_height(0.5f)
	, max_walk_speed_crouched(3.0f)
	, max_acceleration(20.48f)
	, ground_friction(8.0f)
	, braking_deceleration_walking(20.48f)
	, braking_friction_factor(2.0f)
	, use_separate_braking_friction(false)
	, braking_friction(8.0f)
	, walkable_floor_angle(45.0f)
	, walkable_floor_cos(Math::cos(Math::deg_to_rad(45.0f)))
	, floor_sweep_distance(0.4f)
	, max_step_height(0.45f)
	, perch_radius_threshold(0.0f)
	, perch_additional_height(0.4f)
	, can_walk_off_ledges(true)
	, max_fly_speed(6.0f)
	, braking_deceleration_flying(20.48f)
	, max_swim_speed(3.0f)
	, braking_deceleration_swimming(20.48f)
	, out_of_water_jump_velocity(5.2f)
	, jump_velocity(5.2f)
	, air_control(0.05f)
	, falling_lateral_friction(0.0f)
	, braking_deceleration_falling(0.0f)
	, max_simulation_time_step(0.05f)
	, max_simulation_iterations(8)
	, max_slides(4)
	, orient_rotation_to_movement(false)
	, rotation_rate_deg(360.0f)
	, move_with_base(true)
	, rotate_with_base(true)
	, impart_base_velocity(true)
	, replicate_movement_mode(true)
{
	state.instantiate();
	input.instantiate();
	proposed_move.instantiate();
	current_floor.instantiate();
	perch_floor.instantiate();

	tick_params.instantiate();
	Ref<MovementState> out_state;
	out_state.instantiate();
	tick_params->set_out_state(out_state);

	body.params.instantiate();
	body.result.instantiate();
	body.probe_shape.instantiate();
	body.probe_params.instantiate();
	body.point_params.instantiate();

	layered_proposal.instantiate();
}

CharacterMovementComponent::~CharacterMovementComponent()
{
	if (backend != nullptr)
	{
		memdelete(backend);
		backend = nullptr;
	}
}

void CharacterMovementComponent::_ready()
{
	if (Engine::get_singleton()->is_editor_hint())
	{
		return;
	}

	resolve_updated_body();
	resolve_capsule_dimensions();
	register_default_modes();

	pawn = Pawn::find_in(get_parent());

	// Water is handled by a transition rather than by the modes, so dropping a
	// WaterVolume into a level is the whole of making a character swim. A game
	// with no water anywhere can clear transitions to stop paying for the point
	// query it costs.
	if (transitions.is_empty())
	{
		Ref<WaterMovementTransition> water;
		water.instantiate();
		transitions.push_back(water);
	}

	if (backend == nullptr)
	{
		backend = memnew(StandaloneMovementBackend);
	}

	state->set_movement_mode(find_mode(starting_mode).is_valid() ? starting_mode : StringName(MODE_NULL));

	if (updated_body != nullptr)
	{
		const Transform3D transform = updated_body->get_global_transform();
		state->set_position(transform.origin);
		state->set_rotation(transform.basis);
	}

	setup_replication();

	set_physics_process(true);
}

void CharacterMovementComponent::_physics_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint())
	{
		return;
	}

	if (updated_body == nullptr || backend == nullptr)
	{
		return;
	}

	// Checked every frame rather than latched at _ready: a pawn's authority is
	// decided by possession, which happens after this node is in the tree.
	if (pawn != nullptr && !pawn->has_authority())
	{
		return;
	}

	backend->tick(this, delta);
}

void CharacterMovementComponent::simulate(double delta)
{
	if (delta <= 0.0 || updated_body == nullptr)
	{
		return;
	}

	// Something outside the simulation may have moved the body - a respawn, a
	// teleport, a script setting position directly. Adopt it rather than fighting
	// it, and drop the cached floor, which describes a place we are no longer in.
	const Transform3D body_transform = updated_body->get_global_transform();
	if (body_transform.origin.distance_squared_to(state->get_position()) > TELEPORT_DISTANCE_SQUARED)
	{
		state->set_position(body_transform.origin);
		state->set_rotation(body_transform.basis);
		current_floor->clear();

		// The base has to go with the floor. Left behind, the stored offset would
		// drag the character straight back onto the platform it was just moved
		// away from.
		state->clear_base();
		has_base_last_transform = false;
	}

	// After the teleport check, so a platform's motion is not mistaken for one,
	// and before the modes run, so they start from where the platform has already
	// put the character.
	update_based_movement();

	gather_input(backend->get_sim_frame(), delta);

	// Before the modes run, so a mode that is about to move sees the size the
	// character will be moving at.
	update_crouch_state();

	// A frame longer than max_simulation_time_step is split, so a hitch cannot
	// walk a character through a wall. Running out of iterations drops the rest
	// of the frame instead, which is the lesser wrong.
	double remaining = delta;
	int iterations = 0;

	while (remaining > 0.0 && iterations < max_simulation_iterations)
	{
		iterations++;

		const double step = (remaining > (double)max_simulation_time_step) ? (double)max_simulation_time_step : remaining;
		remaining -= step;

		run_mode_tick(step);
		resolve_queued_mode();
	}

	save_base_location();

	apply_rotation(delta);
	apply_state_to_body();

	jump_pressed_latch = false;
}

void CharacterMovementComponent::run_mode_tick(double delta)
{
	const Ref<MovementMode> mode = find_mode(state->get_movement_mode());
	if (mode.is_null())
	{
		return;
	}

	tick_params->set_component(this);
	tick_params->set_start_state(state);
	tick_params->set_input(input);
	tick_params->set_proposed_move(proposed_move);
	tick_params->set_delta(delta);

	// The mode is handed a working copy that already says "nothing happened", so
	// a mode that returns without writing is correct rather than undefined.
	const Ref<MovementState> out_state = tick_params->get_out_state();
	out_state->copy_from(state);

	mode->generate_move(tick_params);

	// Between the proposal and its execution: the only point at which motion can
	// be blended, because it is the only point at which motion is still a value
	// rather than a position.
	mix_layered_moves(tick_params);

	mode->simulation_tick(tick_params);

	state->copy_from(out_state);

	retire_finished_layered_moves();
	evaluate_transitions(mode);
}

double CharacterMovementComponent::get_sim_time_ms() const
{
	return backend != nullptr ? backend->get_sim_time_ms() : 0.0;
}

void CharacterMovementComponent::queue_layered_move(const Ref<LayeredMove>& move)
{
	if (move.is_null())
	{
		return;
	}

	state->get_queued_layered_moves().push_back(move);
}

void CharacterMovementComponent::cancel_all_layered_moves()
{
	Array active = state->get_active_layered_moves();

	for (int i = 0; i < active.size(); i++)
	{
		const Ref<LayeredMove> move = active[i];
		if (move.is_valid())
		{
			state->set_velocity(move->apply_finish_velocity(state->get_velocity()));
			emit_signal("layered_move_finished", move);
		}
	}

	active.clear();
	state->get_queued_layered_moves().clear();
}

void CharacterMovementComponent::mix_layered_moves(const Ref<MovementTickParams>& params)
{
	const double now = get_sim_time_ms();

	// The working copy, not the live state. run_mode_tick copies the state into
	// out_state before this runs and copies it back afterwards, so anything the
	// mixer wrote to the live state would be thrown away between those two - the
	// move would start and then silently un-start.
	const Ref<MovementState> tick_state = params->get_out_state();
	Array queued = tick_state->get_queued_layered_moves();
	Array active = tick_state->get_active_layered_moves();

	for (int i = 0; i < queued.size(); i++)
	{
		const Ref<LayeredMove> move = queued[i];
		if (move.is_null())
		{
			continue;
		}

		move->set_start_time_ms(now);
		move->set_started(true);
		move->on_start(params);

		// Inserted in priority order so the mixing loop can stay a straight walk:
		// lowest first, so the highest priority is the last to overwrite anything.
		int insert_at = active.size();
		for (int j = 0; j < active.size(); j++)
		{
			const Ref<LayeredMove> other = active[j];
			if (other.is_valid() && other->get_priority() > move->get_priority())
			{
				insert_at = j;
				break;
			}
		}

		active.insert(insert_at, move);
		emit_signal("layered_move_started", move);
	}

	queued.clear();

	if (active.is_empty())
	{
		return;
	}

	const Ref<ProposedMove> combined = params->get_proposed_move();

	for (int i = 0; i < active.size(); i++)
	{
		const Ref<LayeredMove> move = active[i];
		if (move.is_null())
		{
			continue;
		}

		layered_proposal->reset();
		move->generate_move(params, layered_proposal);

		switch (layered_proposal->get_mix_mode())
		{
			case ProposedMove::ADDITIVE_VELOCITY:
				combined->set_linear_velocity(combined->get_linear_velocity() + layered_proposal->get_linear_velocity());
				break;

			case ProposedMove::OVERRIDE_ALL_EXCEPT_VERTICAL:
				// Sideways is taken over, along up is left alone - which is what
				// lets a dash steer a falling character without cancelling gravity.
				combined->set_linear_velocity(layered_proposal->get_linear_velocity().slide(up_direction) + combined->get_linear_velocity().project(up_direction));
				break;

			case ProposedMove::OVERRIDE_ALL:
				combined->set_linear_velocity(layered_proposal->get_linear_velocity());
				combined->set_angular_velocity_deg(layered_proposal->get_angular_velocity_deg());
				break;

			case ProposedMove::OVERRIDE_VELOCITY:
			default:
				combined->set_linear_velocity(layered_proposal->get_linear_velocity());
				break;
		}

		if (layered_proposal->get_preferred_mode() != StringName())
		{
			combined->set_preferred_mode(layered_proposal->get_preferred_mode());
		}
	}

	// A move that asked for a mode gets it at the next resolution, like any other
	// request - never in the middle of the move it is part of.
	if (combined->get_preferred_mode() != StringName())
	{
		queue_next_mode(combined->get_preferred_mode());
	}
}

void CharacterMovementComponent::retire_finished_layered_moves()
{
	Array active = state->get_active_layered_moves();

	if (active.is_empty())
	{
		return;
	}

	const double now = get_sim_time_ms();

	for (int i = active.size() - 1; i >= 0; i--)
	{
		const Ref<LayeredMove> move = active[i];

		if (move.is_valid() && !move->is_finished(now))
		{
			continue;
		}

		if (move.is_valid())
		{
			state->set_velocity(move->apply_finish_velocity(state->get_velocity()));
			emit_signal("layered_move_finished", move);
		}

		active.remove_at(i);
	}
}

void CharacterMovementComponent::evaluate_transitions(const Ref<MovementMode>& mode)
{
	// The mode already decided where it is going, and it knew more about the move
	// it just made than any rule watching from outside.
	if (has_queued_mode)
	{
		return;
	}

	const Array lists[2] = { mode->get_transitions(), transitions };

	for (int list = 0; list < 2; list++)
	{
		for (int i = 0; i < lists[list].size(); i++)
		{
			const Ref<MovementModeTransition> transition = lists[list][i];
			if (transition.is_null())
			{
				continue;
			}

			const StringName next_mode = transition->evaluate(tick_params);
			if (next_mode != StringName())
			{
				queue_next_mode(next_mode);
				return;
			}
		}
	}
}

void CharacterMovementComponent::resolve_queued_mode()
{
	if (!has_queued_mode)
	{
		return;
	}

	has_queued_mode = false;

	const StringName previous_mode = state->get_movement_mode();
	if (queued_mode == previous_mode)
	{
		return;
	}

	if (find_mode(queued_mode).is_null())
	{
		WARN_PRINT(vformat("GFGD: CharacterMovementComponent was asked to switch to movement mode '%s', which is not registered. Staying in '%s'.", String(queued_mode), String(previous_mode)));
		return;
	}

	state->set_movement_mode(queued_mode);
	emit_signal("movement_mode_changed", previous_mode, queued_mode);
}

void CharacterMovementComponent::gather_input(int64_t frame, double tick_delta)
{
	input->reset();
	input->set_frame(frame);
	input->set_want_jump(jump_pressed_latch);

	if (pawn == nullptr)
	{
		return;
	}

	// The pawn's chance to gather input on this tick rather than on a rendered
	// frame. A script that uses it accumulates exactly once per simulated step, so
	// the clamp below never has to do anything and an analog stick keeps its
	// magnitude.
	pawn->gather_movement_input(tick_delta);

	// Still clamped, because input read from _process is legal and common: it
	// accumulates once per rendered frame while this runs once per physics frame,
	// so the raw vector is however many frames happened to fit. Left alone,
	// walking speed would rise with the frame rate.
	input->set_move_input(pawn->consume_movement_input_vector().limit_length(1.0f));
}

Node3D* CharacterMovementComponent::resolve_base() const
{
	if (!state->has_base())
	{
		return nullptr;
	}

	return Object::cast_to<Node3D>(ObjectDB::get_instance(ObjectID(static_cast<uint64_t>(state->get_base_id()))));
}

void CharacterMovementComponent::update_based_movement()
{
	if (!move_with_base || !state->has_base())
	{
		return;
	}

	Node3D* base = resolve_base();
	if (base == nullptr || !base->is_inside_tree())
	{
		// The platform is gone. Keep the character where it is rather than
		// snapping it to a transform that no longer exists.
		state->clear_base();
		has_base_last_transform = false;
		return;
	}

	const Transform3D base_now = base->get_global_transform();

	if (!has_base_last_transform)
	{
		base_last_transform = base_now;
		has_base_last_transform = true;
		return;
	}

	// How much the base moved, applied to the character as a delta.
	//
	// Rebuilding the character's position from a stored relative offset instead
	// would be simpler, but it throws away every correction the solver made
	// during the tick - the floor-height adjustment above all - and the two then
	// fight each other a little more on every frame. The delta leaves the
	// character's own position authoritative and only adds what the platform did.
	const Transform3D base_delta = base_now * base_last_transform.affine_inverse();
	base_last_transform = base_now;

	const Transform3D character(state->get_rotation(), state->get_position());
	const Transform3D moved = base_delta * character;

	state->set_position(moved.origin);

	if (!rotate_with_base)
	{
		return;
	}

	// Only the turn about the up axis is carried. Taking the basis wholesale
	// would tip the character over with a tilting platform, which is never what
	// a character wants.
	const Vector3 forward = (-moved.basis.get_column(2)).slide(up_direction);
	if (forward.is_zero_approx())
	{
		return;
	}

	state->set_rotation(Basis::looking_at(forward.normalized(), up_direction));
}

void CharacterMovementComponent::save_base_location()
{
	if (!move_with_base)
	{
		return;
	}

	Node3D* new_base = nullptr;
	if (current_floor->is_walkable_floor())
	{
		new_base = Object::cast_to<Node3D>(current_floor->get_collider());
	}

	if (new_base == nullptr)
	{
		state->clear_base();
		has_base_last_transform = false;
		return;
	}

	const int64_t new_base_id = static_cast<int64_t>(new_base->get_instance_id());
	const Transform3D base_transform = new_base->get_global_transform();

	// Stepping onto a different platform starts a fresh delta rather than
	// carrying one over from the old one.
	if (new_base_id != state->get_base_id())
	{
		has_base_last_transform = false;
	}

	if (!has_base_last_transform)
	{
		base_last_transform = base_transform;
		has_base_last_transform = true;
	}

	// The relative offset is not what drives the movement - the delta above is -
	// but it is what a client would be sent to place the character on the right
	// platform, so it is kept up to date here.
	const Transform3D character(state->get_rotation(), state->get_position());
	const Transform3D relative = base_transform.affine_inverse() * character;

	state->set_base_id(new_base_id);
	state->set_base_relative_position(relative.origin);
	state->set_base_relative_rotation(relative.basis);
}

void CharacterMovementComponent::crouch()
{
	crouch_latch = true;
}

void CharacterMovementComponent::un_crouch()
{
	crouch_latch = false;
}

void CharacterMovementComponent::apply_capsule_half_height(float value)
{
	capsule_half_height = value;

	if (capsule_shape.is_valid())
	{
		capsule_shape->set_height(MAX(2.0f * value, 2.0f * capsule_radius));
	}
}

void CharacterMovementComponent::update_crouch_state()
{
	if (!can_crouch || capsule_shape.is_null())
	{
		return;
	}

	const bool wants_crouch = input->get_want_crouch() || crouch_latch;
	const bool crouching = state->get_is_crouching();

	if (wants_crouch == crouching)
	{
		return;
	}

	// One distance, used in both directions: how much shorter the crouched capsule
	// is. Deriving it from the target height instead makes it zero when standing
	// up, which reads as "the standing capsule fits exactly where the crouched one
	// is" - and that is always blocked by the floor.
	const float crouched_target = MIN(crouched_half_height, standing_half_height);
	const float shift = standing_half_height - crouched_target;
	const float target = wants_crouch ? crouched_target : standing_half_height;

	if (wants_crouch)
	{
		// The capsule shrinks around its centre, so the feet would rise by the
		// difference. Dropping the body by the same amount leaves them where they
		// were, which is what makes crouching look like crouching rather than
		// like sinking into the floor.
		apply_capsule_half_height(target);
		state->set_position(state->get_position() - up_direction * shift);
		state->set_is_crouching(true);
		emit_signal("crouch_changed", true);
		return;
	}

	const Vector3 stood_position = state->get_position() + up_direction * shift;
	const Transform3D stood(state->get_rotation(), stood_position);

	// Standing up is a request, not an order. Under a low ceiling the character
	// stays down and tries again next tick.
	if (!MovementUtils::would_capsule_fit(body, stood, capsule_radius, standing_half_height))
	{
		return;
	}

	apply_capsule_half_height(standing_half_height);
	state->set_position(stood_position);
	state->set_is_crouching(false);
	emit_signal("crouch_changed", false);
}

void CharacterMovementComponent::apply_state_to_body()
{
	Transform3D transform = updated_body->get_global_transform();
	transform.origin = state->get_position();
	transform.basis = state->get_rotation();
	updated_body->set_global_transform(transform);
}

void CharacterMovementComponent::apply_rotation(double delta)
{
	if (!orient_rotation_to_movement || delta <= 0.0)
	{
		return;
	}

	const Vector3 flat_velocity = state->get_velocity().slide(up_direction);
	if (flat_velocity.length_squared() < ROTATION_SPEED_THRESHOLD_SQUARED)
	{
		return;
	}

	const Basis current = state->get_rotation();
	const Basis target = Basis::looking_at(flat_velocity.normalized(), up_direction);

	const Vector3 current_forward = -current.get_column(2);
	const float angle = current_forward.angle_to(flat_velocity.normalized());
	if (angle < (float)CMP_EPSILON)
	{
		return;
	}

	const float max_step = Math::deg_to_rad(rotation_rate_deg) * (float)delta;
	state->set_rotation(current.slerp(target, MIN(1.0f, max_step / angle)));
}

void CharacterMovementComponent::queue_next_mode(const StringName& mode_name)
{
	queued_mode = mode_name;
	has_queued_mode = true;
}

StringName CharacterMovementComponent::get_movement_mode() const
{
	return state->get_movement_mode();
}

void CharacterMovementComponent::set_movement_mode(const StringName& mode_name)
{
	const StringName previous_mode = state->get_movement_mode();
	if (previous_mode == mode_name)
	{
		return;
	}

	state->set_movement_mode(mode_name);
	has_queued_mode = false;

	emit_signal("movement_mode_changed", previous_mode, mode_name);
}

Ref<MovementMode> CharacterMovementComponent::find_mode(const StringName& mode_name) const
{
	if (!modes.has(mode_name))
	{
		return Ref<MovementMode>();
	}

	return Ref<MovementMode>(modes[mode_name]);
}

Dictionary CharacterMovementComponent::sweep_from(const Transform3D& from, const Vector3& motion) const
{
	Dictionary result;

	const MovementUtils::SweepResult hit = MovementUtils::sweep(body, from, motion);
	result["transform"] = hit.transform;
	result["collided"] = hit.collided;
	result["remaining"] = hit.remaining;
	result["normal"] = hit.normal;
	result["point"] = hit.point;
	result["collider"] = hit.collider;
	result["collider_velocity"] = hit.collider_velocity;

	return result;
}

static Dictionary slide_result_to_dictionary(const MovementUtils::SlideResult& slide)
{
	Dictionary result;
	result["transform"] = slide.transform;
	result["velocity"] = slide.velocity;
	result["hit_wall"] = slide.hit_wall;
	result["hit_walkable"] = slide.hit_walkable;
	result["walkable_normal"] = slide.walkable_normal;
	result["stepped_up"] = slide.stepped_up;
	return result;
}

Dictionary CharacterMovementComponent::slide_move_from(const Transform3D& from, const Vector3& motion, const Vector3& velocity) const
{
	return slide_result_to_dictionary(MovementUtils::slide_move(body, from, motion, velocity, up_direction, walkable_floor_cos, max_slides));
}

Dictionary CharacterMovementComponent::move_along_floor_from(const Transform3D& from, const Vector3& motion, const Vector3& velocity) const
{
	return slide_result_to_dictionary(MovementUtils::move_along_floor(body, from, motion, velocity, up_direction, current_floor, get_ground_move_settings()));
}

Ref<FloorResult> CharacterMovementComponent::find_floor_at(const Transform3D& from) const
{
	Ref<FloorResult> result;
	result.instantiate();

	MovementUtils::find_floor(body, from, up_direction, floor_sweep_distance, floor_sweep_distance, get_ground_move_settings(), is_on_ground(), result, perch_floor);

	return result;
}

Transform3D CharacterMovementComponent::adjust_floor_height_at(const Transform3D& from, const Ref<FloorResult>& floor) const
{
	return MovementUtils::adjust_floor_height(body, from, up_direction, capsule_radius, floor);
}

Vector3 CharacterMovementComponent::compute_velocity(const Vector3& velocity, const Vector3& acceleration, float friction, float braking_deceleration, float max_speed, double delta) const
{
	return MovementUtils::compute_velocity(velocity, acceleration, friction, braking_deceleration, braking_friction_factor, max_speed, delta);
}

Object* CharacterMovementComponent::find_water_volume_at(const Vector3& point) const
{
	return Object::cast_to<WaterVolume>(MovementUtils::find_area_at(body, point));
}

float CharacterMovementComponent::get_immersion_depth_at(const Transform3D& from) const
{
	const float reach = capsule_half_height * 0.9f;

	// Head under: nothing else needs asking, and this is the common case for a
	// character actually swimming.
	if (find_water_volume_at(from.origin + up_direction * reach) != nullptr)
	{
		return 1.0f;
	}

	// Middle dry: not swimming, and the samples in between would all be dry too.
	if (find_water_volume_at(from.origin) == nullptr)
	{
		return 0.0f;
	}

	// Somewhere at the surface, which is the only place the exact figure matters -
	// it is what damps the rise and what scales the buoyancy, and a three-valued
	// answer makes both of those step. Four more samples up the capsule is coarse
	// next to Unreal's trace, but it is smooth enough that a character holding
	// "up" settles instead of porpoising.
	int submerged_samples = 3;
	for (int i = 1; i <= 4; i++)
	{
		const float height = reach * (float)i / 5.0f;
		if (find_water_volume_at(from.origin + up_direction * height) != nullptr)
		{
			submerged_samples++;
		}
	}

	return (float)submerged_samples / 7.0f;
}

float CharacterMovementComponent::get_analog_max_speed(const Vector3& move_input, float base_max_speed) const
{
	const float analog = MIN(move_input.length(), 1.0f);

	// No input at all leaves the cap alone: it is still what the over-speed test
	// and the braking below it are measured against, and scaling it to zero would
	// make a character that was launched fast brake as if it were sprinting.
	if (analog <= 0.0f)
	{
		return base_max_speed;
	}

	return MAX(base_max_speed * analog, min_analog_walk_speed);
}

MovementUtils::GroundMoveSettings CharacterMovementComponent::get_ground_move_settings() const
{
	MovementUtils::GroundMoveSettings settings;
	settings.max_slope_cos = walkable_floor_cos;
	settings.max_slides = max_slides;
	settings.max_step_height = max_step_height;
	settings.capsule_radius = capsule_radius;
	settings.capsule_half_height = capsule_half_height;
	settings.perch_radius_threshold = perch_radius_threshold;
	settings.perch_additional_height = perch_additional_height;
	settings.can_walk_off_ledges = can_walk_off_ledges;
	return settings;
}

Vector3 CharacterMovementComponent::get_velocity() const
{
	return state->get_velocity();
}

void CharacterMovementComponent::set_velocity(const Vector3& value)
{
	state->set_velocity(value);
}

bool CharacterMovementComponent::is_on_ground() const
{
	return state->get_movement_mode() == StringName(MODE_WALKING);
}

bool CharacterMovementComponent::is_falling() const
{
	return state->get_movement_mode() == StringName(MODE_FALLING);
}

void CharacterMovementComponent::jump()
{
	jump_pressed_latch = true;
}

void CharacterMovementComponent::stop_jumping()
{
	jump_pressed_latch = false;
}

void CharacterMovementComponent::set_up_direction(const Vector3& value)
{
	if (value.is_zero_approx())
	{
		WARN_PRINT("GFGD: CharacterMovementComponent.up_direction cannot be zero; keeping the previous value.");
		return;
	}

	up_direction = value.normalized();
}

void CharacterMovementComponent::set_walkable_floor_angle(float value)
{
	walkable_floor_angle = CLAMP(value, 0.0f, 89.0f);
	walkable_floor_cos = Math::cos(Math::deg_to_rad(walkable_floor_angle));
}

void CharacterMovementComponent::resolve_updated_body()
{
	Node* target = updated_body_path.is_empty() ? get_parent() : get_node_or_null(updated_body_path);

	updated_body = Object::cast_to<PhysicsBody3D>(target);
	if (updated_body == nullptr)
	{
		WARN_PRINT(vformat("GFGD: CharacterMovementComponent at %s found no PhysicsBody3D at '%s'. Nothing will move.", String(get_path()), String(updated_body_path)));
		return;
	}

	body.rid = updated_body->get_rid();
	body.space = PhysicsServer3D::get_singleton()->body_get_space(body.rid);
	body.collision_mask = updated_body->get_collision_mask();
	body.margin = 0.001f;
}

void CharacterMovementComponent::resolve_capsule_dimensions()
{
	if (updated_body == nullptr)
	{
		return;
	}

	const TypedArray<Node> shapes = updated_body->find_children("*", "CollisionShape3D", true, false);
	for (int i = 0; i < shapes.size(); i++)
	{
		const CollisionShape3D* shape_node = Object::cast_to<CollisionShape3D>(shapes[i]);
		if (shape_node == nullptr)
		{
			continue;
		}

		const Ref<CapsuleShape3D> capsule = shape_node->get_shape();
		if (capsule.is_null())
		{
			continue;
		}

		capsule_radius = capsule->get_radius();
		capsule_half_height = capsule->get_height() * 0.5f;
		standing_half_height = capsule_half_height;

		if (can_crouch)
		{
			// The shape has to belong to this character before its height can be
			// changed. A CapsuleShape3D authored once and used by every pawn of a
			// kind is shared, and resizing it would crouch all of them at once.
			capsule_shape_node = const_cast<CollisionShape3D*>(shape_node);
			capsule_shape = capsule->duplicate();
			capsule_shape_node->set_shape(capsule_shape);
		}

		return;
	}

	// Not fatal: the numbers are only used to tell a floor from a wall the
	// capsule is grazing, and to correct the fallback ray's distance. A body with
	// some other shape still walks, it just judges those two things by the
	// defaults.
	WARN_PRINT(vformat("GFGD: CharacterMovementComponent at %s found no CapsuleShape3D under its body; using default capsule dimensions.", String(get_path())));
}

void CharacterMovementComponent::setup_replication()
{
	if (!replicate_movement_mode)
	{
		return;
	}

	PackedStringArray properties;
	properties.push_back("movement_mode");

	Replication::attach(this, this, Replication::validate_properties(this, properties), World::SERVER_PEER_ID, "MovementReplication");
}

void CharacterMovementComponent::register_default_modes()
{
	// Only fills what a project has not authored itself, so a mode set up in the
	// inspector is never quietly replaced.
	if (!modes.has(StringName(MODE_WALKING)))
	{
		Ref<WalkingMode> walking;
		walking.instantiate();
		modes[StringName(MODE_WALKING)] = walking;
	}

	if (!modes.has(StringName(MODE_FALLING)))
	{
		Ref<FallingMode> falling;
		falling.instantiate();
		modes[StringName(MODE_FALLING)] = falling;
	}

	if (!modes.has(StringName(MODE_FLYING)))
	{
		Ref<FlyingMode> flying;
		flying.instantiate();
		modes[StringName(MODE_FLYING)] = flying;
	}

	if (!modes.has(StringName(MODE_SWIMMING)))
	{
		Ref<SwimmingMode> swimming;
		swimming.instantiate();
		modes[StringName(MODE_SWIMMING)] = swimming;
	}

	if (!modes.has(StringName(MODE_NULL)))
	{
		Ref<NullMovementMode> null_mode;
		null_mode.instantiate();
		modes[StringName(MODE_NULL)] = null_mode;
	}
}

void CharacterMovementComponent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("simulate", "delta"), &CharacterMovementComponent::simulate);

	ClassDB::bind_method(D_METHOD("queue_next_mode", "mode_name"), &CharacterMovementComponent::queue_next_mode);
	ClassDB::bind_method(D_METHOD("find_mode", "mode_name"), &CharacterMovementComponent::find_mode);

	ClassDB::bind_method(D_METHOD("get_movement_mode"), &CharacterMovementComponent::get_movement_mode);
	ClassDB::bind_method(D_METHOD("set_movement_mode", "mode_name"), &CharacterMovementComponent::set_movement_mode);

	// Declared as a property so the synchronizer can find it, with no usage flags
	// so it is neither shown in the inspector nor written into a scene file. It is
	// where the character is right now, not something anyone authors - the knob
	// for that is starting_mode.
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "movement_mode", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_movement_mode", "get_movement_mode");

	ClassDB::bind_method(D_METHOD("get_state"), &CharacterMovementComponent::get_state);
	ClassDB::bind_method(D_METHOD("get_input"), &CharacterMovementComponent::get_input);
	ClassDB::bind_method(D_METHOD("get_current_floor"), &CharacterMovementComponent::get_current_floor);
	ClassDB::bind_method(D_METHOD("get_updated_body"), &CharacterMovementComponent::get_updated_body);
	ClassDB::bind_method(D_METHOD("get_pawn"), &CharacterMovementComponent::get_pawn);

	ClassDB::bind_method(D_METHOD("get_velocity"), &CharacterMovementComponent::get_velocity);
	ClassDB::bind_method(D_METHOD("set_velocity", "value"), &CharacterMovementComponent::set_velocity);

	ClassDB::bind_method(D_METHOD("is_on_ground"), &CharacterMovementComponent::is_on_ground);
	ClassDB::bind_method(D_METHOD("is_falling"), &CharacterMovementComponent::is_falling);

	ClassDB::bind_method(D_METHOD("queue_layered_move", "move"), &CharacterMovementComponent::queue_layered_move);
	ClassDB::bind_method(D_METHOD("cancel_all_layered_moves"), &CharacterMovementComponent::cancel_all_layered_moves);
	ClassDB::bind_method(D_METHOD("get_active_layered_moves"), &CharacterMovementComponent::get_active_layered_moves);
	ClassDB::bind_method(D_METHOD("get_sim_time_ms"), &CharacterMovementComponent::get_sim_time_ms);

	ClassDB::bind_method(D_METHOD("sweep_from", "from", "motion"), &CharacterMovementComponent::sweep_from);
	ClassDB::bind_method(D_METHOD("slide_move_from", "from", "motion", "velocity"), &CharacterMovementComponent::slide_move_from);
	ClassDB::bind_method(D_METHOD("move_along_floor_from", "from", "motion", "velocity"), &CharacterMovementComponent::move_along_floor_from);
	ClassDB::bind_method(D_METHOD("find_floor_at", "from"), &CharacterMovementComponent::find_floor_at);
	ClassDB::bind_method(D_METHOD("adjust_floor_height_at", "from", "floor"), &CharacterMovementComponent::adjust_floor_height_at);
	ClassDB::bind_method(D_METHOD("compute_velocity", "velocity", "acceleration", "friction", "braking_deceleration", "max_speed", "delta"), &CharacterMovementComponent::compute_velocity);

	ClassDB::bind_method(D_METHOD("save_base_location"), &CharacterMovementComponent::save_base_location);
	ClassDB::bind_method(D_METHOD("crouch"), &CharacterMovementComponent::crouch);
	ClassDB::bind_method(D_METHOD("un_crouch"), &CharacterMovementComponent::un_crouch);
	ClassDB::bind_method(D_METHOD("is_crouching"), &CharacterMovementComponent::is_crouching);
	ClassDB::bind_method(D_METHOD("get_effective_max_walk_speed"), &CharacterMovementComponent::get_effective_max_walk_speed);

	ClassDB::bind_method(D_METHOD("jump"), &CharacterMovementComponent::jump);
	ClassDB::bind_method(D_METHOD("stop_jumping"), &CharacterMovementComponent::stop_jumping);

	ClassDB::bind_method(D_METHOD("get_capsule_radius"), &CharacterMovementComponent::get_capsule_radius);
	ClassDB::bind_method(D_METHOD("get_capsule_half_height"), &CharacterMovementComponent::get_capsule_half_height);

	ClassDB::bind_method(D_METHOD("get_updated_body_path"), &CharacterMovementComponent::get_updated_body_path);
	ClassDB::bind_method(D_METHOD("set_updated_body_path", "value"), &CharacterMovementComponent::set_updated_body_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "updated_body_path"), "set_updated_body_path", "get_updated_body_path");

	ClassDB::bind_method(D_METHOD("get_modes"), &CharacterMovementComponent::get_modes);
	ClassDB::bind_method(D_METHOD("set_modes", "value"), &CharacterMovementComponent::set_modes);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "modes"), "set_modes", "get_modes");

	ClassDB::bind_method(D_METHOD("get_transitions"), &CharacterMovementComponent::get_transitions);
	ClassDB::bind_method(D_METHOD("set_transitions", "value"), &CharacterMovementComponent::set_transitions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transitions", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:MovementModeTransition", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_transitions", "get_transitions");

	ClassDB::bind_method(D_METHOD("get_starting_mode"), &CharacterMovementComponent::get_starting_mode);
	ClassDB::bind_method(D_METHOD("set_starting_mode", "value"), &CharacterMovementComponent::set_starting_mode);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "starting_mode"), "set_starting_mode", "get_starting_mode");

	ADD_GROUP("Physics", "");

	ClassDB::bind_method(D_METHOD("get_up_direction"), &CharacterMovementComponent::get_up_direction);
	ClassDB::bind_method(D_METHOD("set_up_direction", "value"), &CharacterMovementComponent::set_up_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "up_direction"), "set_up_direction", "get_up_direction");

	ClassDB::bind_method(D_METHOD("get_gravity"), &CharacterMovementComponent::get_gravity);
	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &CharacterMovementComponent::set_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m/s²"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &CharacterMovementComponent::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "value"), &CharacterMovementComponent::set_gravity_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_gravity_scale", "get_gravity_scale");

	ADD_GROUP("Walking", "");

	ClassDB::bind_method(D_METHOD("get_analog_max_speed", "move_input", "base_max_speed"), &CharacterMovementComponent::get_analog_max_speed);

	ClassDB::bind_method(D_METHOD("get_max_walk_speed"), &CharacterMovementComponent::get_max_walk_speed);
	ClassDB::bind_method(D_METHOD("set_max_walk_speed", "value"), &CharacterMovementComponent::set_max_walk_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_walk_speed", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_max_walk_speed", "get_max_walk_speed");

	ClassDB::bind_method(D_METHOD("get_min_analog_walk_speed"), &CharacterMovementComponent::get_min_analog_walk_speed);
	ClassDB::bind_method(D_METHOD("set_min_analog_walk_speed", "value"), &CharacterMovementComponent::set_min_analog_walk_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_analog_walk_speed", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_min_analog_walk_speed", "get_min_analog_walk_speed");

	ClassDB::bind_method(D_METHOD("get_can_crouch"), &CharacterMovementComponent::get_can_crouch);
	ClassDB::bind_method(D_METHOD("set_can_crouch", "value"), &CharacterMovementComponent::set_can_crouch);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "can_crouch"), "set_can_crouch", "get_can_crouch");

	ClassDB::bind_method(D_METHOD("get_crouched_half_height"), &CharacterMovementComponent::get_crouched_half_height);
	ClassDB::bind_method(D_METHOD("set_crouched_half_height", "value"), &CharacterMovementComponent::set_crouched_half_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crouched_half_height", PROPERTY_HINT_RANGE, "0.05,3,0.001,or_greater,suffix:m"), "set_crouched_half_height", "get_crouched_half_height");

	ClassDB::bind_method(D_METHOD("get_max_walk_speed_crouched"), &CharacterMovementComponent::get_max_walk_speed_crouched);
	ClassDB::bind_method(D_METHOD("set_max_walk_speed_crouched", "value"), &CharacterMovementComponent::set_max_walk_speed_crouched);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_walk_speed_crouched", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_max_walk_speed_crouched", "get_max_walk_speed_crouched");

	ClassDB::bind_method(D_METHOD("get_max_acceleration"), &CharacterMovementComponent::get_max_acceleration);
	ClassDB::bind_method(D_METHOD("set_max_acceleration", "value"), &CharacterMovementComponent::set_max_acceleration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_acceleration", PROPERTY_HINT_RANGE, "0,200,0.01,or_greater,suffix:m/s²"), "set_max_acceleration", "get_max_acceleration");

	ClassDB::bind_method(D_METHOD("get_ground_friction"), &CharacterMovementComponent::get_ground_friction);
	ClassDB::bind_method(D_METHOD("set_ground_friction", "value"), &CharacterMovementComponent::set_ground_friction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_friction", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_ground_friction", "get_ground_friction");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_walking"), &CharacterMovementComponent::get_braking_deceleration_walking);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_walking", "value"), &CharacterMovementComponent::set_braking_deceleration_walking);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_walking", PROPERTY_HINT_RANGE, "0,200,0.01,or_greater,suffix:m/s²"), "set_braking_deceleration_walking", "get_braking_deceleration_walking");

	ClassDB::bind_method(D_METHOD("get_braking_friction_factor"), &CharacterMovementComponent::get_braking_friction_factor);
	ClassDB::bind_method(D_METHOD("set_braking_friction_factor", "value"), &CharacterMovementComponent::set_braking_friction_factor);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_friction_factor", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_braking_friction_factor", "get_braking_friction_factor");

	ClassDB::bind_method(D_METHOD("get_use_separate_braking_friction"), &CharacterMovementComponent::get_use_separate_braking_friction);
	ClassDB::bind_method(D_METHOD("set_use_separate_braking_friction", "value"), &CharacterMovementComponent::set_use_separate_braking_friction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_separate_braking_friction"), "set_use_separate_braking_friction", "get_use_separate_braking_friction");

	ClassDB::bind_method(D_METHOD("get_braking_friction"), &CharacterMovementComponent::get_braking_friction);
	ClassDB::bind_method(D_METHOD("set_braking_friction", "value"), &CharacterMovementComponent::set_braking_friction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_friction", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_braking_friction", "get_braking_friction");

	ADD_GROUP("Floor", "");

	ClassDB::bind_method(D_METHOD("get_walkable_floor_angle"), &CharacterMovementComponent::get_walkable_floor_angle);
	ClassDB::bind_method(D_METHOD("set_walkable_floor_angle", "value"), &CharacterMovementComponent::set_walkable_floor_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walkable_floor_angle", PROPERTY_HINT_RANGE, "0,89,0.1,radians_as_degrees"), "set_walkable_floor_angle", "get_walkable_floor_angle");

	ClassDB::bind_method(D_METHOD("get_floor_sweep_distance"), &CharacterMovementComponent::get_floor_sweep_distance);
	ClassDB::bind_method(D_METHOD("set_floor_sweep_distance", "value"), &CharacterMovementComponent::set_floor_sweep_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_sweep_distance", PROPERTY_HINT_RANGE, "0,5,0.001,or_greater,suffix:m"), "set_floor_sweep_distance", "get_floor_sweep_distance");

	ClassDB::bind_method(D_METHOD("get_max_step_height"), &CharacterMovementComponent::get_max_step_height);
	ClassDB::bind_method(D_METHOD("set_max_step_height", "value"), &CharacterMovementComponent::set_max_step_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_step_height", PROPERTY_HINT_RANGE, "0,5,0.001,or_greater,suffix:m"), "set_max_step_height", "get_max_step_height");

	ClassDB::bind_method(D_METHOD("get_perch_radius_threshold"), &CharacterMovementComponent::get_perch_radius_threshold);
	ClassDB::bind_method(D_METHOD("set_perch_radius_threshold", "value"), &CharacterMovementComponent::set_perch_radius_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "perch_radius_threshold", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:m"), "set_perch_radius_threshold", "get_perch_radius_threshold");

	ClassDB::bind_method(D_METHOD("get_perch_additional_height"), &CharacterMovementComponent::get_perch_additional_height);
	ClassDB::bind_method(D_METHOD("set_perch_additional_height", "value"), &CharacterMovementComponent::set_perch_additional_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "perch_additional_height", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater,suffix:m"), "set_perch_additional_height", "get_perch_additional_height");

	ClassDB::bind_method(D_METHOD("get_can_walk_off_ledges"), &CharacterMovementComponent::get_can_walk_off_ledges);
	ClassDB::bind_method(D_METHOD("set_can_walk_off_ledges", "value"), &CharacterMovementComponent::set_can_walk_off_ledges);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "can_walk_off_ledges"), "set_can_walk_off_ledges", "get_can_walk_off_ledges");

	ADD_GROUP("Jumping / Falling", "");

	ClassDB::bind_method(D_METHOD("get_jump_velocity"), &CharacterMovementComponent::get_jump_velocity);
	ClassDB::bind_method(D_METHOD("set_jump_velocity", "value"), &CharacterMovementComponent::set_jump_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_velocity", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_jump_velocity", "get_jump_velocity");

	ClassDB::bind_method(D_METHOD("get_air_control"), &CharacterMovementComponent::get_air_control);
	ClassDB::bind_method(D_METHOD("set_air_control", "value"), &CharacterMovementComponent::set_air_control);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "air_control", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_air_control", "get_air_control");

	ClassDB::bind_method(D_METHOD("get_falling_lateral_friction"), &CharacterMovementComponent::get_falling_lateral_friction);
	ClassDB::bind_method(D_METHOD("set_falling_lateral_friction", "value"), &CharacterMovementComponent::set_falling_lateral_friction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "falling_lateral_friction", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_falling_lateral_friction", "get_falling_lateral_friction");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_falling"), &CharacterMovementComponent::get_braking_deceleration_falling);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_falling", "value"), &CharacterMovementComponent::set_braking_deceleration_falling);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_falling", PROPERTY_HINT_RANGE, "0,200,0.01,or_greater,suffix:m/s²"), "set_braking_deceleration_falling", "get_braking_deceleration_falling");

	ClassDB::bind_method(D_METHOD("find_water_volume_at", "point"), &CharacterMovementComponent::find_water_volume_at);
	ClassDB::bind_method(D_METHOD("get_immersion_depth_at", "from"), &CharacterMovementComponent::get_immersion_depth_at);

	ClassDB::bind_method(D_METHOD("get_max_swim_speed"), &CharacterMovementComponent::get_max_swim_speed);
	ClassDB::bind_method(D_METHOD("set_max_swim_speed", "value"), &CharacterMovementComponent::set_max_swim_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_swim_speed", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_max_swim_speed", "get_max_swim_speed");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_swimming"), &CharacterMovementComponent::get_braking_deceleration_swimming);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_swimming", "value"), &CharacterMovementComponent::set_braking_deceleration_swimming);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_swimming", PROPERTY_HINT_RANGE, "0,200,0.01,or_greater"), "set_braking_deceleration_swimming", "get_braking_deceleration_swimming");

	ClassDB::bind_method(D_METHOD("get_out_of_water_jump_velocity"), &CharacterMovementComponent::get_out_of_water_jump_velocity);
	ClassDB::bind_method(D_METHOD("set_out_of_water_jump_velocity", "value"), &CharacterMovementComponent::set_out_of_water_jump_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "out_of_water_jump_velocity", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_out_of_water_jump_velocity", "get_out_of_water_jump_velocity");

	ClassDB::bind_method(D_METHOD("get_max_fly_speed"), &CharacterMovementComponent::get_max_fly_speed);
	ClassDB::bind_method(D_METHOD("set_max_fly_speed", "value"), &CharacterMovementComponent::set_max_fly_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_fly_speed", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m/s"), "set_max_fly_speed", "get_max_fly_speed");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_flying"), &CharacterMovementComponent::get_braking_deceleration_flying);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_flying", "value"), &CharacterMovementComponent::set_braking_deceleration_flying);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_flying", PROPERTY_HINT_RANGE, "0,200,0.01,or_greater"), "set_braking_deceleration_flying", "get_braking_deceleration_flying");

	ADD_GROUP("Rotation", "");

	ClassDB::bind_method(D_METHOD("get_orient_rotation_to_movement"), &CharacterMovementComponent::get_orient_rotation_to_movement);
	ClassDB::bind_method(D_METHOD("set_orient_rotation_to_movement", "value"), &CharacterMovementComponent::set_orient_rotation_to_movement);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orient_rotation_to_movement"), "set_orient_rotation_to_movement", "get_orient_rotation_to_movement");

	ClassDB::bind_method(D_METHOD("get_rotation_rate_deg"), &CharacterMovementComponent::get_rotation_rate_deg);
	ClassDB::bind_method(D_METHOD("set_rotation_rate_deg", "value"), &CharacterMovementComponent::set_rotation_rate_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_rate_deg", PROPERTY_HINT_RANGE, "0,3600,1,or_greater,suffix:°/s"), "set_rotation_rate_deg", "get_rotation_rate_deg");

	ADD_GROUP("Networking", "");

	ClassDB::bind_method(D_METHOD("get_replicate_movement_mode"), &CharacterMovementComponent::get_replicate_movement_mode);
	ClassDB::bind_method(D_METHOD("set_replicate_movement_mode", "value"), &CharacterMovementComponent::set_replicate_movement_mode);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "replicate_movement_mode"), "set_replicate_movement_mode", "get_replicate_movement_mode");

	ADD_GROUP("Moving Platforms", "");

	ClassDB::bind_method(D_METHOD("get_move_with_base"), &CharacterMovementComponent::get_move_with_base);
	ClassDB::bind_method(D_METHOD("set_move_with_base", "value"), &CharacterMovementComponent::set_move_with_base);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "move_with_base"), "set_move_with_base", "get_move_with_base");

	ClassDB::bind_method(D_METHOD("get_rotate_with_base"), &CharacterMovementComponent::get_rotate_with_base);
	ClassDB::bind_method(D_METHOD("set_rotate_with_base", "value"), &CharacterMovementComponent::set_rotate_with_base);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_with_base"), "set_rotate_with_base", "get_rotate_with_base");

	ClassDB::bind_method(D_METHOD("get_impart_base_velocity"), &CharacterMovementComponent::get_impart_base_velocity);
	ClassDB::bind_method(D_METHOD("set_impart_base_velocity", "value"), &CharacterMovementComponent::set_impart_base_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "impart_base_velocity"), "set_impart_base_velocity", "get_impart_base_velocity");

	ADD_GROUP("Simulation", "");

	ClassDB::bind_method(D_METHOD("get_max_simulation_time_step"), &CharacterMovementComponent::get_max_simulation_time_step);
	ClassDB::bind_method(D_METHOD("set_max_simulation_time_step", "value"), &CharacterMovementComponent::set_max_simulation_time_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_simulation_time_step", PROPERTY_HINT_RANGE, "0.001,0.5,0.001,suffix:s"), "set_max_simulation_time_step", "get_max_simulation_time_step");

	ClassDB::bind_method(D_METHOD("get_max_simulation_iterations"), &CharacterMovementComponent::get_max_simulation_iterations);
	ClassDB::bind_method(D_METHOD("set_max_simulation_iterations", "value"), &CharacterMovementComponent::set_max_simulation_iterations);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_simulation_iterations", PROPERTY_HINT_RANGE, "1,32,1"), "set_max_simulation_iterations", "get_max_simulation_iterations");

	ClassDB::bind_method(D_METHOD("get_max_slides"), &CharacterMovementComponent::get_max_slides);
	ClassDB::bind_method(D_METHOD("set_max_slides", "value"), &CharacterMovementComponent::set_max_slides);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_slides", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_slides", "get_max_slides");

	ADD_SIGNAL(MethodInfo("movement_mode_changed", PropertyInfo(Variant::STRING_NAME, "from"), PropertyInfo(Variant::STRING_NAME, "to")));
	ADD_SIGNAL(MethodInfo("crouch_changed", PropertyInfo(Variant::BOOL, "crouching")));
	ADD_SIGNAL(MethodInfo("layered_move_started", PropertyInfo(Variant::OBJECT, "move", PROPERTY_HINT_RESOURCE_TYPE, "LayeredMove")));
	ADD_SIGNAL(MethodInfo("layered_move_finished", PropertyInfo(Variant::OBJECT, "move", PROPERTY_HINT_RESOURCE_TYPE, "LayeredMove")));
}
