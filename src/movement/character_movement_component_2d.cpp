#include "movement/character_movement_component_2d.h"

#include <godot_cpp/classes/capsule_shape2d.hpp>
#include <godot_cpp/classes/collision_shape2d.hpp>
#include <godot_cpp/classes/shape2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/physics_body2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/physics_test_motion_parameters2d.hpp>
#include <godot_cpp/classes/physics_test_motion_result2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "framework/pawn.h"
#include "movement/modes/swimming_mode_2d.h"
#include "movement/modes/walking_mode_2d.h"
#include "movement/layered_move.h"
#include "movement/movement_backend.h"
#include "movement/movement_mode.h"
#include "movement/movement_mode_transition.h"
#include "movement/water_movement_transition_2d.h"
#include "movement/water_volume_2d.h"

using namespace godot;
using namespace GFGD;

static constexpr float TELEPORT_DISTANCE_SQUARED = 0.01f;

CharacterMovementComponent2D::CharacterMovementComponent2D()
	: updated_body_path(NodePath(".."))
	, starting_mode(StringName(MODE_WALKING))
	, updated_body(nullptr)
	, pawn(nullptr)
	, capsule_radius(16.0f)
	, capsule_half_height(32.0f)
	, standing_half_height(32.0f)
	, capsule_shape_node(nullptr)
	, backend(nullptr)
	, has_base_last_transform(false)
	, has_queued_mode(false)
	, jump_pressed_latch(false)
	, crouch_latch(false)
	, up_direction(Vector2(0, -1))
	, gravity(980.0f)
	, gravity_scale(1.0f)
	, max_walk_speed(600.0f)
	, min_analog_walk_speed(0.0f)
	, can_crouch(true)
	, crouched_half_height(16.0f)
	, max_walk_speed_crouched(300.0f)
	, max_acceleration(2048.0f)
	, ground_friction(8.0f)
	, braking_deceleration_walking(2048.0f)
	, braking_friction_factor(2.0f)
	, walkable_floor_angle(45.0f)
	, walkable_floor_cos(Math::cos(Math::deg_to_rad(45.0f)))
	, floor_sweep_distance(40.0f)
	, max_step_height(45.0f)
	, max_fly_speed(600.0f)
	, braking_deceleration_flying(2048.0f)
	, max_swim_speed(300.0f)
	, braking_deceleration_swimming(2048.0f)
	, out_of_water_jump_velocity(420.0f)
	, jump_velocity(420.0f)
	, air_control(0.05f)
	, falling_lateral_friction(0.0f)
	, braking_deceleration_falling(0.0f)
	, max_simulation_time_step(0.05f)
	, max_simulation_iterations(8)
	, max_slides(4)
	, move_with_base(true)
	, impart_base_velocity(true)
{
	state.instantiate();
	input.instantiate();
	proposed_move.instantiate();
	current_floor.instantiate();

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

CharacterMovementComponent2D::~CharacterMovementComponent2D()
{
	if (backend != nullptr)
	{
		memdelete(backend);
		backend = nullptr;
	}
}

void CharacterMovementComponent2D::_ready()
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
	// WaterVolume2D into a level is the whole of making a character swim. A game
	// with no water anywhere can clear transitions to stop paying for the point
	// query it costs.
	if (transitions.is_empty())
	{
		Ref<WaterMovementTransition2D> water;
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
		const Transform2D transform = updated_body->get_global_transform();
		state->set_position(Vector3(transform.get_origin().x, transform.get_origin().y, 0.0f));
	}

	set_physics_process(true);
}

void CharacterMovementComponent2D::_physics_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint())
	{
		return;
	}

	if (updated_body == nullptr || backend == nullptr)
	{
		return;
	}

	if (pawn != nullptr && !pawn->has_authority())
	{
		return;
	}

	backend->tick_2d(this, delta);
}

void CharacterMovementComponent2D::simulate(double delta)
{
	if (delta <= 0.0 || updated_body == nullptr)
	{
		return;
	}

	const Vector2 body_position = updated_body->get_global_position();
	const Vector2 state_position = MovementUtils2D::to_2d(state->get_position());

	if (body_position.distance_squared_to(state_position) > TELEPORT_DISTANCE_SQUARED)
	{
		state->set_position(MovementUtils2D::to_3d(body_position));
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

	update_crouch_state();

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

	apply_state_to_body();
	jump_pressed_latch = false;
}

void CharacterMovementComponent2D::run_mode_tick(double delta)
{
	const Ref<MovementMode> mode = find_mode(state->get_movement_mode());
	if (mode.is_null())
	{
		return;
	}

	tick_params->set_component_2d(this);
	tick_params->set_start_state(state);
	tick_params->set_input(input);
	tick_params->set_proposed_move(proposed_move);
	tick_params->set_delta(delta);

	const Ref<MovementState> out_state = tick_params->get_out_state();
	out_state->copy_from(state);

	mode->generate_move(tick_params);

	// Between the proposal and its execution, exactly as in 3D: the only point at
	// which motion is still a value rather than a position.
	mix_layered_moves(tick_params);

	mode->simulation_tick(tick_params);

	state->copy_from(out_state);

	retire_finished_layered_moves();
	evaluate_transitions(mode);
}

double CharacterMovementComponent2D::get_sim_time_ms() const
{
	return backend != nullptr ? backend->get_sim_time_ms() : 0.0;
}

void CharacterMovementComponent2D::queue_layered_move(const Ref<LayeredMove>& move)
{
	if (move.is_null())
	{
		return;
	}

	state->get_queued_layered_moves().push_back(move);
}

void CharacterMovementComponent2D::cancel_all_layered_moves()
{
	Array active = state->get_active_layered_moves();

	for (int i = 0; i < active.size(); i++)
	{
		const Ref<LayeredMove> move = active[i];
		if (move.is_valid())
		{
			state->set_velocity(MovementUtils2D::to_3d(MovementUtils2D::to_2d(move->apply_finish_velocity(state->get_velocity()))));
			emit_signal("layered_move_finished", move);
		}
	}

	active.clear();
	state->get_queued_layered_moves().clear();
}

void CharacterMovementComponent2D::mix_layered_moves(const Ref<MovementTickParams>& params)
{
	const double now = get_sim_time_ms();

	// The working copy, not the live state: run_mode_tick copies the state out and
	// back around this, so anything written to the live one would be thrown away.
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
	const Vector3 up = MovementUtils2D::to_3d(up_direction);

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
				combined->set_linear_velocity(layered_proposal->get_linear_velocity().slide(up) + combined->get_linear_velocity().project(up));
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

	if (combined->get_preferred_mode() != StringName())
	{
		queue_next_mode(combined->get_preferred_mode());
	}
}

void CharacterMovementComponent2D::retire_finished_layered_moves()
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

void CharacterMovementComponent2D::evaluate_transitions(const Ref<MovementMode>& mode)
{
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

void CharacterMovementComponent2D::resolve_queued_mode()
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
		WARN_PRINT(vformat("GFGD: CharacterMovementComponent2D was asked to switch to movement mode '%s', which is not registered. Staying in '%s'.", String(queued_mode), String(previous_mode)));
		return;
	}

	state->set_movement_mode(queued_mode);
	emit_signal("movement_mode_changed", previous_mode, queued_mode);
}

void CharacterMovementComponent2D::gather_input(int64_t frame, double tick_delta)
{
	input->reset();
	input->set_frame(frame);
	input->set_want_jump(jump_pressed_latch);

	if (pawn == nullptr)
	{
		return;
	}

	pawn->gather_movement_input(tick_delta);
	input->set_move_input(pawn->consume_movement_input_vector().limit_length(1.0f));
}

Node2D* CharacterMovementComponent2D::resolve_base() const
{
	if (!state->has_base())
	{
		return nullptr;
	}

	return Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(static_cast<uint64_t>(state->get_base_id()))));
}

void CharacterMovementComponent2D::update_based_movement()
{
	if (!move_with_base || !state->has_base())
	{
		return;
	}

	Node2D* base = resolve_base();
	if (base == nullptr || !base->is_inside_tree())
	{
		// The platform is gone. Keep the character where it is rather than
		// snapping it to a transform that no longer exists.
		state->clear_base();
		has_base_last_transform = false;
		return;
	}

	const Transform2D base_now = base->get_global_transform();

	if (!has_base_last_transform)
	{
		base_last_transform = base_now;
		has_base_last_transform = true;
		return;
	}

	// How much the base moved, applied to the character as a delta.
	//
	// Rebuilding the character's position from the stored relative offset instead
	// would be simpler, but it throws away every correction the solver made during
	// the tick - the floor-height adjustment above all - and the two then fight
	// each other a little more every frame. The delta leaves the character's own
	// position authoritative and only adds what the platform did.
	const Transform2D base_delta = base_now * base_last_transform.affine_inverse();
	base_last_transform = base_now;

	const Vector2 moved = base_delta.xform(MovementUtils2D::to_2d(state->get_position()));
	state->set_position(MovementUtils2D::to_3d(moved));
}

void CharacterMovementComponent2D::save_base_location()
{
	if (!move_with_base)
	{
		return;
	}

	Node2D* new_base = nullptr;
	if (current_floor->is_walkable_floor())
	{
		new_base = Object::cast_to<Node2D>(current_floor->get_collider());
	}

	if (new_base == nullptr)
	{
		state->clear_base();
		has_base_last_transform = false;
		return;
	}

	const int64_t new_base_id = static_cast<int64_t>(new_base->get_instance_id());
	const Transform2D base_transform = new_base->get_global_transform();

	// Stepping onto a different platform starts a fresh delta rather than carrying
	// one over from the old one.
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
	const Vector2 relative = base_transform.affine_inverse().xform(MovementUtils2D::to_2d(state->get_position()));

	state->set_base_id(new_base_id);
	state->set_base_relative_position(MovementUtils2D::to_3d(relative));
}

void CharacterMovementComponent2D::crouch()
{
	crouch_latch = true;
}

void CharacterMovementComponent2D::un_crouch()
{
	crouch_latch = false;
}

void CharacterMovementComponent2D::apply_capsule_half_height(float value)
{
	capsule_half_height = value;

	if (capsule_shape.is_valid())
	{
		capsule_shape->set_height(MAX(2.0f * value, 2.0f * capsule_radius));
	}
}

void CharacterMovementComponent2D::update_crouch_state()
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
	// is. Deriving it from the target height makes it zero when standing up, and
	// the headroom test then asks whether the standing capsule fits exactly where
	// the crouched one is - which the floor always refuses.
	const float crouched_target = MIN(crouched_half_height, standing_half_height);
	const float shift = standing_half_height - crouched_target;
	const float target = wants_crouch ? crouched_target : standing_half_height;

	const Vector2 position = MovementUtils2D::to_2d(state->get_position());

	if (wants_crouch)
	{
		// The capsule shrinks around its centre, so the feet would rise by the
		// difference. Dropping the body by the same amount leaves them put.
		apply_capsule_half_height(target);
		state->set_position(MovementUtils2D::to_3d(position - up_direction * shift));
		state->set_is_crouching(true);
		emit_signal("crouch_changed", true);
		return;
	}

	const Vector2 stood_position = position + up_direction * shift;

	// Standing up is a request, not an order: under a low ceiling the character
	// stays down and tries again next tick.
	if (!MovementUtils2D::would_capsule_fit(body, Transform2D(0.0, stood_position), capsule_radius, standing_half_height))
	{
		return;
	}

	apply_capsule_half_height(standing_half_height);
	state->set_position(MovementUtils2D::to_3d(stood_position));
	state->set_is_crouching(false);
	emit_signal("crouch_changed", false);
}

void CharacterMovementComponent2D::apply_state_to_body()
{
	updated_body->set_global_position(MovementUtils2D::to_2d(state->get_position()));
}

void CharacterMovementComponent2D::queue_next_mode(const StringName& mode_name)
{
	queued_mode = mode_name;
	has_queued_mode = true;
}

StringName CharacterMovementComponent2D::get_movement_mode() const
{
	return state->get_movement_mode();
}

void CharacterMovementComponent2D::set_movement_mode(const StringName& mode_name)
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

Ref<MovementMode> CharacterMovementComponent2D::find_mode(const StringName& mode_name) const
{
	if (!modes.has(mode_name))
	{
		return Ref<MovementMode>();
	}

	return Ref<MovementMode>(modes[mode_name]);
}

Vector2 CharacterMovementComponent2D::get_velocity() const
{
	return MovementUtils2D::to_2d(state->get_velocity());
}

void CharacterMovementComponent2D::set_velocity(const Vector2& value)
{
	state->set_velocity(MovementUtils2D::to_3d(value));
}

bool CharacterMovementComponent2D::is_on_ground() const
{
	return state->get_movement_mode() == StringName(MODE_WALKING);
}

bool CharacterMovementComponent2D::is_falling() const
{
	return state->get_movement_mode() == StringName(MODE_FALLING);
}

void CharacterMovementComponent2D::jump()
{
	jump_pressed_latch = true;
}

void CharacterMovementComponent2D::stop_jumping()
{
	jump_pressed_latch = false;
}

void CharacterMovementComponent2D::set_up_direction(const Vector2& value)
{
	if (value.is_zero_approx())
	{
		WARN_PRINT("GFGD: CharacterMovementComponent2D.up_direction cannot be zero; keeping the previous value.");
		return;
	}

	up_direction = value.normalized();
}

void CharacterMovementComponent2D::set_walkable_floor_angle(float value)
{
	walkable_floor_angle = CLAMP(value, 0.0f, 89.0f);
	walkable_floor_cos = Math::cos(Math::deg_to_rad(walkable_floor_angle));
}

MovementUtils2D::GroundMoveSettings CharacterMovementComponent2D::get_ground_move_settings() const
{
	MovementUtils2D::GroundMoveSettings settings;
	settings.max_slope_cos = walkable_floor_cos;
	settings.max_slides = max_slides;
	settings.max_step_height = max_step_height;
	settings.capsule_radius = capsule_radius;
	settings.capsule_half_height = capsule_half_height;
	return settings;
}

float CharacterMovementComponent2D::get_analog_max_speed(const Vector2& move_input, float base_max_speed) const
{
	const float analog = MIN(move_input.length(), 1.0f);
	if (analog <= 0.0f)
	{
		return base_max_speed;
	}

	return MAX(base_max_speed * analog, min_analog_walk_speed);
}

Dictionary CharacterMovementComponent2D::sweep_from(const Transform2D& from, const Vector2& motion) const
{
	Dictionary result;

	const MovementUtils2D::SweepResult hit = MovementUtils2D::sweep(body, from, motion);
	result["transform"] = hit.transform;
	result["collided"] = hit.collided;
	result["remaining"] = hit.remaining;
	result["normal"] = hit.normal;
	result["point"] = hit.point;
	result["collider"] = hit.collider;
	result["collider_velocity"] = hit.collider_velocity;

	return result;
}

static Dictionary slide_result_to_dictionary_2d(const MovementUtils2D::SlideResult& slide)
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

Dictionary CharacterMovementComponent2D::slide_move_from(const Transform2D& from, const Vector2& motion, const Vector2& velocity) const
{
	return slide_result_to_dictionary_2d(MovementUtils2D::slide_move(body, from, motion, velocity, up_direction, walkable_floor_cos, max_slides));
}

Dictionary CharacterMovementComponent2D::move_along_floor_from(const Transform2D& from, const Vector2& motion, const Vector2& velocity) const
{
	return slide_result_to_dictionary_2d(MovementUtils2D::move_along_floor(body, from, motion, velocity, up_direction, current_floor, get_ground_move_settings()));
}

Ref<FloorResult> CharacterMovementComponent2D::find_floor_at(const Transform2D& from) const
{
	Ref<FloorResult> result;
	result.instantiate();

	MovementUtils2D::find_floor(body, from, up_direction, floor_sweep_distance, floor_sweep_distance, get_ground_move_settings(), result);

	return result;
}

Transform2D CharacterMovementComponent2D::adjust_floor_height_at(const Transform2D& from, const Ref<FloorResult>& floor) const
{
	return MovementUtils2D::adjust_floor_height(body, from, up_direction, floor);
}

Vector2 CharacterMovementComponent2D::compute_velocity(const Vector2& velocity, const Vector2& acceleration, float friction, float braking_deceleration, float max_speed, double delta) const
{
	return MovementUtils2D::compute_velocity(velocity, acceleration, friction, braking_deceleration, braking_friction_factor, max_speed, delta);
}

Object* CharacterMovementComponent2D::find_water_volume_at(const Vector2& point) const
{
	return Object::cast_to<WaterVolume2D>(MovementUtils2D::find_area_at(body, point));
}

float CharacterMovementComponent2D::get_immersion_depth_at(const Transform2D& from) const
{
	const Vector2 origin = from.get_origin();
	const float reach = capsule_half_height * 0.9f;

	// Head under: nothing else needs asking, and this is the common case for a
	// character actually swimming.
	if (find_water_volume_at(origin + up_direction * reach) != nullptr)
	{
		return 1.0f;
	}

	// Middle dry: not swimming, and the samples in between would all be dry too.
	if (find_water_volume_at(origin) == nullptr)
	{
		return 0.0f;
	}

	// Somewhere at the surface, which is the only place the exact figure matters -
	// it is what damps the rise and what scales the buoyancy, and a three-valued
	// answer makes both of those step.
	int submerged_samples = 3;
	for (int i = 1; i <= 4; i++)
	{
		const float height = reach * (float)i / 5.0f;
		if (find_water_volume_at(origin + up_direction * height) != nullptr)
		{
			submerged_samples++;
		}
	}

	return (float)submerged_samples / 7.0f;
}

void CharacterMovementComponent2D::resolve_updated_body()
{
	Node* target = updated_body_path.is_empty() ? get_parent() : get_node_or_null(updated_body_path);

	updated_body = Object::cast_to<PhysicsBody2D>(target);
	if (updated_body == nullptr)
	{
		WARN_PRINT(vformat("GFGD: CharacterMovementComponent2D at %s found no PhysicsBody2D at '%s'. Nothing will move.", String(get_path()), String(updated_body_path)));
		return;
	}

	body.rid = updated_body->get_rid();
	body.space = PhysicsServer2D::get_singleton()->body_get_space(body.rid);
	body.collision_mask = updated_body->get_collision_mask();
	body.margin = 0.08f;
}

void CharacterMovementComponent2D::resolve_capsule_dimensions()
{
	if (updated_body == nullptr)
	{
		return;
	}

	const TypedArray<Node> shapes = updated_body->find_children("*", "CollisionShape2D", true, false);
	for (int i = 0; i < shapes.size(); i++)
	{
		const CollisionShape2D* shape_node = Object::cast_to<CollisionShape2D>(shapes[i]);
		if (shape_node == nullptr)
		{
			continue;
		}

		const Ref<CapsuleShape2D> capsule = shape_node->get_shape();
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
			// changed: one authored once and shared between every pawn of a kind
			// would crouch all of them at once.
			capsule_shape_node = const_cast<CollisionShape2D*>(shape_node);
			capsule_shape = capsule->duplicate();
			capsule_shape_node->set_shape(capsule_shape);
		}

		return;
	}

	WARN_PRINT(vformat("GFGD: CharacterMovementComponent2D at %s found no CapsuleShape2D under its body; using default capsule dimensions.", String(get_path())));
}

void CharacterMovementComponent2D::register_default_modes()
{
	if (!modes.has(StringName(MODE_WALKING)))
	{
		Ref<WalkingMode2D> walking;
		walking.instantiate();
		modes[StringName(MODE_WALKING)] = walking;
	}

	if (!modes.has(StringName(MODE_FALLING)))
	{
		Ref<FallingMode2D> falling;
		falling.instantiate();
		modes[StringName(MODE_FALLING)] = falling;
	}

	if (!modes.has(StringName(MODE_FLYING)))
	{
		Ref<FlyingMode2D> flying;
		flying.instantiate();
		modes[StringName(MODE_FLYING)] = flying;
	}

	if (!modes.has(StringName(MODE_SWIMMING)))
	{
		Ref<SwimmingMode2D> swimming;
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

void CharacterMovementComponent2D::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("simulate", "delta"), &CharacterMovementComponent2D::simulate);
	ClassDB::bind_method(D_METHOD("queue_next_mode", "mode_name"), &CharacterMovementComponent2D::queue_next_mode);
	ClassDB::bind_method(D_METHOD("find_mode", "mode_name"), &CharacterMovementComponent2D::find_mode);
	ClassDB::bind_method(D_METHOD("get_movement_mode"), &CharacterMovementComponent2D::get_movement_mode);
	ClassDB::bind_method(D_METHOD("set_movement_mode", "mode_name"), &CharacterMovementComponent2D::set_movement_mode);

	ClassDB::bind_method(D_METHOD("get_state"), &CharacterMovementComponent2D::get_state);
	ClassDB::bind_method(D_METHOD("get_input"), &CharacterMovementComponent2D::get_input);
	ClassDB::bind_method(D_METHOD("get_current_floor"), &CharacterMovementComponent2D::get_current_floor);
	ClassDB::bind_method(D_METHOD("get_updated_body"), &CharacterMovementComponent2D::get_updated_body);
	ClassDB::bind_method(D_METHOD("get_pawn"), &CharacterMovementComponent2D::get_pawn);

	ClassDB::bind_method(D_METHOD("get_velocity"), &CharacterMovementComponent2D::get_velocity);
	ClassDB::bind_method(D_METHOD("set_velocity", "value"), &CharacterMovementComponent2D::set_velocity);
	ClassDB::bind_method(D_METHOD("is_on_ground"), &CharacterMovementComponent2D::is_on_ground);
	ClassDB::bind_method(D_METHOD("is_falling"), &CharacterMovementComponent2D::is_falling);
	ClassDB::bind_method(D_METHOD("queue_layered_move", "move"), &CharacterMovementComponent2D::queue_layered_move);
	ClassDB::bind_method(D_METHOD("cancel_all_layered_moves"), &CharacterMovementComponent2D::cancel_all_layered_moves);
	ClassDB::bind_method(D_METHOD("get_active_layered_moves"), &CharacterMovementComponent2D::get_active_layered_moves);
	ClassDB::bind_method(D_METHOD("get_sim_time_ms"), &CharacterMovementComponent2D::get_sim_time_ms);

	ClassDB::bind_method(D_METHOD("crouch"), &CharacterMovementComponent2D::crouch);
	ClassDB::bind_method(D_METHOD("un_crouch"), &CharacterMovementComponent2D::un_crouch);
	ClassDB::bind_method(D_METHOD("is_crouching"), &CharacterMovementComponent2D::is_crouching);
	ClassDB::bind_method(D_METHOD("get_effective_max_walk_speed"), &CharacterMovementComponent2D::get_effective_max_walk_speed);

	ClassDB::bind_method(D_METHOD("jump"), &CharacterMovementComponent2D::jump);
	ClassDB::bind_method(D_METHOD("stop_jumping"), &CharacterMovementComponent2D::stop_jumping);

	ClassDB::bind_method(D_METHOD("get_capsule_radius"), &CharacterMovementComponent2D::get_capsule_radius);
	ClassDB::bind_method(D_METHOD("get_capsule_half_height"), &CharacterMovementComponent2D::get_capsule_half_height);

	ClassDB::bind_method(D_METHOD("sweep_from", "from", "motion"), &CharacterMovementComponent2D::sweep_from);
	ClassDB::bind_method(D_METHOD("slide_move_from", "from", "motion", "velocity"), &CharacterMovementComponent2D::slide_move_from);
	ClassDB::bind_method(D_METHOD("move_along_floor_from", "from", "motion", "velocity"), &CharacterMovementComponent2D::move_along_floor_from);
	ClassDB::bind_method(D_METHOD("find_floor_at", "from"), &CharacterMovementComponent2D::find_floor_at);
	ClassDB::bind_method(D_METHOD("adjust_floor_height_at", "from", "floor"), &CharacterMovementComponent2D::adjust_floor_height_at);
	ClassDB::bind_method(D_METHOD("compute_velocity", "velocity", "acceleration", "friction", "braking_deceleration", "max_speed", "delta"), &CharacterMovementComponent2D::compute_velocity);
	ClassDB::bind_method(D_METHOD("get_analog_max_speed", "move_input", "base_max_speed"), &CharacterMovementComponent2D::get_analog_max_speed);

	ClassDB::bind_method(D_METHOD("get_updated_body_path"), &CharacterMovementComponent2D::get_updated_body_path);
	ClassDB::bind_method(D_METHOD("set_updated_body_path", "value"), &CharacterMovementComponent2D::set_updated_body_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "updated_body_path"), "set_updated_body_path", "get_updated_body_path");

	ClassDB::bind_method(D_METHOD("get_modes"), &CharacterMovementComponent2D::get_modes);
	ClassDB::bind_method(D_METHOD("set_modes", "value"), &CharacterMovementComponent2D::set_modes);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "modes"), "set_modes", "get_modes");

	ClassDB::bind_method(D_METHOD("get_transitions"), &CharacterMovementComponent2D::get_transitions);
	ClassDB::bind_method(D_METHOD("set_transitions", "value"), &CharacterMovementComponent2D::set_transitions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transitions", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:MovementModeTransition", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_transitions", "get_transitions");

	ClassDB::bind_method(D_METHOD("get_starting_mode"), &CharacterMovementComponent2D::get_starting_mode);
	ClassDB::bind_method(D_METHOD("set_starting_mode", "value"), &CharacterMovementComponent2D::set_starting_mode);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "starting_mode"), "set_starting_mode", "get_starting_mode");

	ADD_GROUP("Physics", "");

	ClassDB::bind_method(D_METHOD("get_up_direction"), &CharacterMovementComponent2D::get_up_direction);
	ClassDB::bind_method(D_METHOD("set_up_direction", "value"), &CharacterMovementComponent2D::set_up_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "up_direction"), "set_up_direction", "get_up_direction");

	ClassDB::bind_method(D_METHOD("get_gravity"), &CharacterMovementComponent2D::get_gravity);
	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &CharacterMovementComponent2D::set_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:px/s²"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &CharacterMovementComponent2D::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "value"), &CharacterMovementComponent2D::set_gravity_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_gravity_scale", "get_gravity_scale");

	ADD_GROUP("Walking", "");

	ClassDB::bind_method(D_METHOD("get_max_walk_speed"), &CharacterMovementComponent2D::get_max_walk_speed);
	ClassDB::bind_method(D_METHOD("set_max_walk_speed", "value"), &CharacterMovementComponent2D::set_max_walk_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_walk_speed", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_max_walk_speed", "get_max_walk_speed");

	ClassDB::bind_method(D_METHOD("get_min_analog_walk_speed"), &CharacterMovementComponent2D::get_min_analog_walk_speed);
	ClassDB::bind_method(D_METHOD("set_min_analog_walk_speed", "value"), &CharacterMovementComponent2D::set_min_analog_walk_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_analog_walk_speed", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_min_analog_walk_speed", "get_min_analog_walk_speed");

	ClassDB::bind_method(D_METHOD("get_can_crouch"), &CharacterMovementComponent2D::get_can_crouch);
	ClassDB::bind_method(D_METHOD("set_can_crouch", "value"), &CharacterMovementComponent2D::set_can_crouch);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "can_crouch"), "set_can_crouch", "get_can_crouch");

	ClassDB::bind_method(D_METHOD("get_crouched_half_height"), &CharacterMovementComponent2D::get_crouched_half_height);
	ClassDB::bind_method(D_METHOD("set_crouched_half_height", "value"), &CharacterMovementComponent2D::set_crouched_half_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crouched_half_height", PROPERTY_HINT_RANGE, "1,300,0.1,or_greater,suffix:px"), "set_crouched_half_height", "get_crouched_half_height");

	ClassDB::bind_method(D_METHOD("get_max_walk_speed_crouched"), &CharacterMovementComponent2D::get_max_walk_speed_crouched);
	ClassDB::bind_method(D_METHOD("set_max_walk_speed_crouched", "value"), &CharacterMovementComponent2D::set_max_walk_speed_crouched);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_walk_speed_crouched", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_max_walk_speed_crouched", "get_max_walk_speed_crouched");

	ClassDB::bind_method(D_METHOD("get_max_acceleration"), &CharacterMovementComponent2D::get_max_acceleration);
	ClassDB::bind_method(D_METHOD("set_max_acceleration", "value"), &CharacterMovementComponent2D::set_max_acceleration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_acceleration", PROPERTY_HINT_RANGE, "0,20000,1,or_greater,suffix:px/s²"), "set_max_acceleration", "get_max_acceleration");

	ClassDB::bind_method(D_METHOD("get_ground_friction"), &CharacterMovementComponent2D::get_ground_friction);
	ClassDB::bind_method(D_METHOD("set_ground_friction", "value"), &CharacterMovementComponent2D::set_ground_friction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_friction", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_ground_friction", "get_ground_friction");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_walking"), &CharacterMovementComponent2D::get_braking_deceleration_walking);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_walking", "value"), &CharacterMovementComponent2D::set_braking_deceleration_walking);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_walking", PROPERTY_HINT_RANGE, "0,20000,1,or_greater,suffix:px/s²"), "set_braking_deceleration_walking", "get_braking_deceleration_walking");

	ClassDB::bind_method(D_METHOD("get_braking_friction_factor"), &CharacterMovementComponent2D::get_braking_friction_factor);
	ClassDB::bind_method(D_METHOD("set_braking_friction_factor", "value"), &CharacterMovementComponent2D::set_braking_friction_factor);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_friction_factor", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_braking_friction_factor", "get_braking_friction_factor");

	ADD_GROUP("Floor", "");

	ClassDB::bind_method(D_METHOD("get_walkable_floor_angle"), &CharacterMovementComponent2D::get_walkable_floor_angle);
	ClassDB::bind_method(D_METHOD("set_walkable_floor_angle", "value"), &CharacterMovementComponent2D::set_walkable_floor_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walkable_floor_angle", PROPERTY_HINT_RANGE, "0,89,0.1,radians_as_degrees"), "set_walkable_floor_angle", "get_walkable_floor_angle");

	ClassDB::bind_method(D_METHOD("get_floor_sweep_distance"), &CharacterMovementComponent2D::get_floor_sweep_distance);
	ClassDB::bind_method(D_METHOD("set_floor_sweep_distance", "value"), &CharacterMovementComponent2D::set_floor_sweep_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_sweep_distance", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:px"), "set_floor_sweep_distance", "get_floor_sweep_distance");

	ClassDB::bind_method(D_METHOD("get_max_step_height"), &CharacterMovementComponent2D::get_max_step_height);
	ClassDB::bind_method(D_METHOD("set_max_step_height", "value"), &CharacterMovementComponent2D::set_max_step_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_step_height", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:px"), "set_max_step_height", "get_max_step_height");

	ClassDB::bind_method(D_METHOD("get_max_fly_speed"), &CharacterMovementComponent2D::get_max_fly_speed);
	ClassDB::bind_method(D_METHOD("set_max_fly_speed", "value"), &CharacterMovementComponent2D::set_max_fly_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_fly_speed", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_max_fly_speed", "get_max_fly_speed");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_flying"), &CharacterMovementComponent2D::get_braking_deceleration_flying);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_flying", "value"), &CharacterMovementComponent2D::set_braking_deceleration_flying);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_flying", PROPERTY_HINT_RANGE, "0,20000,1,or_greater"), "set_braking_deceleration_flying", "get_braking_deceleration_flying");

	ClassDB::bind_method(D_METHOD("find_water_volume_at", "point"), &CharacterMovementComponent2D::find_water_volume_at);
	ClassDB::bind_method(D_METHOD("get_immersion_depth_at", "from"), &CharacterMovementComponent2D::get_immersion_depth_at);

	ClassDB::bind_method(D_METHOD("get_max_swim_speed"), &CharacterMovementComponent2D::get_max_swim_speed);
	ClassDB::bind_method(D_METHOD("set_max_swim_speed", "value"), &CharacterMovementComponent2D::set_max_swim_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_swim_speed", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_max_swim_speed", "get_max_swim_speed");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_swimming"), &CharacterMovementComponent2D::get_braking_deceleration_swimming);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_swimming", "value"), &CharacterMovementComponent2D::set_braking_deceleration_swimming);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_swimming", PROPERTY_HINT_RANGE, "0,20000,1,or_greater"), "set_braking_deceleration_swimming", "get_braking_deceleration_swimming");

	ClassDB::bind_method(D_METHOD("get_out_of_water_jump_velocity"), &CharacterMovementComponent2D::get_out_of_water_jump_velocity);
	ClassDB::bind_method(D_METHOD("set_out_of_water_jump_velocity", "value"), &CharacterMovementComponent2D::set_out_of_water_jump_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "out_of_water_jump_velocity", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_out_of_water_jump_velocity", "get_out_of_water_jump_velocity");

	ADD_GROUP("Jumping / Falling", "");

	ClassDB::bind_method(D_METHOD("get_jump_velocity"), &CharacterMovementComponent2D::get_jump_velocity);
	ClassDB::bind_method(D_METHOD("set_jump_velocity", "value"), &CharacterMovementComponent2D::set_jump_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_velocity", PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s"), "set_jump_velocity", "get_jump_velocity");

	ClassDB::bind_method(D_METHOD("get_air_control"), &CharacterMovementComponent2D::get_air_control);
	ClassDB::bind_method(D_METHOD("set_air_control", "value"), &CharacterMovementComponent2D::set_air_control);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "air_control", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_air_control", "get_air_control");

	ClassDB::bind_method(D_METHOD("get_falling_lateral_friction"), &CharacterMovementComponent2D::get_falling_lateral_friction);
	ClassDB::bind_method(D_METHOD("set_falling_lateral_friction", "value"), &CharacterMovementComponent2D::set_falling_lateral_friction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "falling_lateral_friction", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_falling_lateral_friction", "get_falling_lateral_friction");

	ClassDB::bind_method(D_METHOD("get_braking_deceleration_falling"), &CharacterMovementComponent2D::get_braking_deceleration_falling);
	ClassDB::bind_method(D_METHOD("set_braking_deceleration_falling", "value"), &CharacterMovementComponent2D::set_braking_deceleration_falling);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "braking_deceleration_falling", PROPERTY_HINT_RANGE, "0,20000,1,or_greater,suffix:px/s²"), "set_braking_deceleration_falling", "get_braking_deceleration_falling");

	ADD_GROUP("Moving Platforms", "");

	ClassDB::bind_method(D_METHOD("get_move_with_base"), &CharacterMovementComponent2D::get_move_with_base);
	ClassDB::bind_method(D_METHOD("set_move_with_base", "value"), &CharacterMovementComponent2D::set_move_with_base);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "move_with_base"), "set_move_with_base", "get_move_with_base");

	ClassDB::bind_method(D_METHOD("get_impart_base_velocity"), &CharacterMovementComponent2D::get_impart_base_velocity);
	ClassDB::bind_method(D_METHOD("set_impart_base_velocity", "value"), &CharacterMovementComponent2D::set_impart_base_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "impart_base_velocity"), "set_impart_base_velocity", "get_impart_base_velocity");

	ADD_GROUP("Simulation", "");

	ClassDB::bind_method(D_METHOD("get_max_simulation_time_step"), &CharacterMovementComponent2D::get_max_simulation_time_step);
	ClassDB::bind_method(D_METHOD("set_max_simulation_time_step", "value"), &CharacterMovementComponent2D::set_max_simulation_time_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_simulation_time_step", PROPERTY_HINT_RANGE, "0.001,0.5,0.001,suffix:s"), "set_max_simulation_time_step", "get_max_simulation_time_step");

	ClassDB::bind_method(D_METHOD("get_max_simulation_iterations"), &CharacterMovementComponent2D::get_max_simulation_iterations);
	ClassDB::bind_method(D_METHOD("set_max_simulation_iterations", "value"), &CharacterMovementComponent2D::set_max_simulation_iterations);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_simulation_iterations", PROPERTY_HINT_RANGE, "1,32,1"), "set_max_simulation_iterations", "get_max_simulation_iterations");

	ClassDB::bind_method(D_METHOD("save_base_location"), &CharacterMovementComponent2D::save_base_location);

	ClassDB::bind_method(D_METHOD("get_max_slides"), &CharacterMovementComponent2D::get_max_slides);
	ClassDB::bind_method(D_METHOD("set_max_slides", "value"), &CharacterMovementComponent2D::set_max_slides);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_slides", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_slides", "get_max_slides");

	ADD_SIGNAL(MethodInfo("movement_mode_changed", PropertyInfo(Variant::STRING_NAME, "from"), PropertyInfo(Variant::STRING_NAME, "to")));
	ADD_SIGNAL(MethodInfo("crouch_changed", PropertyInfo(Variant::BOOL, "crouching")));
	ADD_SIGNAL(MethodInfo("layered_move_started", PropertyInfo(Variant::OBJECT, "move", PROPERTY_HINT_RESOURCE_TYPE, "LayeredMove")));
	ADD_SIGNAL(MethodInfo("layered_move_finished", PropertyInfo(Variant::OBJECT, "move", PROPERTY_HINT_RESOURCE_TYPE, "LayeredMove")));
}
