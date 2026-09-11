#include "movement/movement_utils_2d.h"

#include <godot_cpp/classes/physics_direct_space_state2d.hpp>
#include <godot_cpp/classes/physics_point_query_parameters2d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters2d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;
using namespace GFGD;

namespace GFGD
{
namespace MovementUtils2D
{

SweepResult sweep(const Body& body, const Transform2D& from, const Vector2& motion)
{
	SweepResult out;
	out.transform = from;

	if (!body.is_valid() || motion.is_zero_approx())
	{
		return out;
	}

	body.params->set_from(from);
	body.params->set_motion(motion);
	body.params->set_margin(body.margin);
	body.params->set_recovery_as_collision_enabled(false);

	out.collided = PhysicsServer2D::get_singleton()->body_test_motion(body.rid, body.params, body.result);

	out.transform = from.translated(body.result->get_travel());
	out.remaining = body.result->get_remainder();

	if (out.collided)
	{
		out.normal = body.result->get_collision_normal();
		out.point = body.result->get_collision_point();
		out.collider = body.result->get_collider();
		out.collider_velocity = body.result->get_collider_velocity();
	}

	return out;
}

bool is_hit_surface_walkable(const Vector2& normal, const Vector2& up, float max_slope_cos)
{
	if (normal.is_zero_approx())
	{
		return false;
	}

	const float facing = normal.dot(up);
	if (facing <= 0.0f)
	{
		return false;
	}

	return facing >= max_slope_cos;
}

bool is_within_edge_tolerance(const Vector2& center, const Vector2& impact_point, float capsule_radius, const Vector2& up)
{
	// The component of the offset across the character rather than along it.
	const Vector2 offset = impact_point - center;
	const float across = Math::abs(offset.dot(Vector2(-up.y, up.x)));

	const float reduced_radius = MAX(SWEEP_EDGE_REJECT_DISTANCE + (float)CMP_EPSILON, capsule_radius - SWEEP_EDGE_REJECT_DISTANCE);
	return across < reduced_radius;
}

Object* find_area_at(const Body& body, const Vector2& point)
{
	if (!body.is_valid() || body.point_params.is_null())
	{
		return nullptr;
	}

	PhysicsDirectSpaceState2D* space_state = PhysicsServer2D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return nullptr;
	}

	body.point_params->set_position(point);
	body.point_params->set_collision_mask(body.collision_mask);
	body.point_params->set_collide_with_bodies(false);
	body.point_params->set_collide_with_areas(true);

	const TypedArray<Dictionary> hits = space_state->intersect_point(body.point_params, 1);
	if (hits.is_empty())
	{
		return nullptr;
	}

	const Dictionary hit = hits[0];
	return Object::cast_to<Object>(hit["collider"]);
}

bool would_capsule_fit(const Body& body, const Transform2D& at, float radius, float half_height)
{
	if (!body.can_probe())
	{
		return true;
	}

	PhysicsDirectSpaceState2D* space_state = PhysicsServer2D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return true;
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	// Pulled in very slightly, so resting against a wall does not read as being
	// blocked by it.
	body.probe_shape->set_radius(MAX(1.0f, radius - SWEEP_EDGE_REJECT_DISTANCE));
	body.probe_shape->set_height(MAX(2.0f * half_height - SWEEP_EDGE_REJECT_DISTANCE, 2.0f * radius));

	body.probe_params->set_shape(body.probe_shape);
	body.probe_params->set_transform(at);
	body.probe_params->set_motion(Vector2());
	body.probe_params->set_margin(0.0f);
	body.probe_params->set_collision_mask(body.collision_mask);
	body.probe_params->set_exclude(exclude);

	return space_state->intersect_shape(body.probe_params, 1).is_empty();
}

Vector2 surface_normal_at(const Body& body, const Vector2& point, const Vector2& up, float probe_distance)
{
	if (!body.is_valid())
	{
		return Vector2();
	}

	PhysicsDirectSpaceState2D* space_state = PhysicsServer2D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return Vector2();
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	const Ref<PhysicsRayQueryParameters2D> ray = PhysicsRayQueryParameters2D::create(point + up * probe_distance, point - up * probe_distance, body.collision_mask, exclude);
	const Dictionary hit = space_state->intersect_ray(ray);
	if (hit.is_empty())
	{
		return Vector2();
	}

	return hit["normal"];
}

bool step_up(const Body& body, const Transform2D& from, const Vector2& delta, const Vector2& hit_point, const Vector2& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings, Transform2D& out_transform)
{
	if (settings.max_step_height <= 0.0f || !body.is_valid())
	{
		return false;
	}

	const float impact_along_up = hit_point.dot(up);
	const float start_along_up = from.get_origin().dot(up);

	if (impact_along_up > start_along_up + (settings.capsule_half_height - settings.capsule_radius))
	{
		return false;
	}

	float travel_up = settings.max_step_height;
	float travel_down = settings.max_step_height;

	float floor_base_along_up = start_along_up - settings.capsule_half_height;
	float floor_point_along_up = floor_base_along_up;

	if (current_floor.is_valid() && current_floor->is_walkable_floor())
	{
		const float floor_distance = MAX(0.0f, current_floor->get_distance_to_floor());

		floor_base_along_up -= floor_distance;
		travel_up = MAX(travel_up - floor_distance, 0.0f);
		travel_down = settings.max_step_height + MAX_FLOOR_DIST * 2.0f;

		const bool hit_vertical_face = !is_within_edge_tolerance(from.get_origin(), hit_point, settings.capsule_radius, up);
		if (!current_floor->get_line_trace() && !hit_vertical_face)
		{
			const Vector3 point = current_floor->get_point();
			floor_point_along_up = Vector2(point.x, point.y).dot(up);
		}
		else
		{
			floor_point_along_up -= current_floor->get_floor_dist();
		}
	}

	if (impact_along_up <= floor_base_along_up)
	{
		return false;
	}

	const SweepResult up_sweep = sweep(body, from, up * travel_up);
	const SweepResult forward_sweep = sweep(body, up_sweep.transform, delta);

	Transform2D after_forward = forward_sweep.transform;

	if (forward_sweep.collided)
	{
		const SweepResult slide_sweep = sweep(body, after_forward, forward_sweep.remaining.slide(forward_sweep.normal));
		after_forward = slide_sweep.transform;

		// Climbing to go nowhere is not worth the vertical displacement it would
		// leave behind.
		if (after_forward.get_origin().distance_squared_to(up_sweep.transform.get_origin()) < (float)CMP_EPSILON)
		{
			return false;
		}
	}

	const SweepResult down_sweep = sweep(body, after_forward, -up * travel_down);

	if (down_sweep.collided)
	{
		const float climbed = down_sweep.point.dot(up) - floor_point_along_up;
		if (climbed > settings.max_step_height)
		{
			return false;
		}

		// Same disambiguation as in 3D: a sweep landing on the lip of a step
		// reports a normal blended towards the wall, so the surface is asked
		// directly before the climb is refused.
		bool landed_walkable = is_hit_surface_walkable(down_sweep.normal, up, settings.max_slope_cos);
		if (!landed_walkable)
		{
			// Nudged into the surface along the direction of travel before asking.
			// The contact a sweep reports sits one collision margin in front of what
			// it touched, so a ray from the point itself passes in front of a step's
			// top face and finds nothing - and the edge normal, which is mostly
			// sideways, then reads as a wall.
			const Vector2 probe_at = down_sweep.point + delta.normalized() * (body.margin * 2.0f);
			landed_walkable = is_hit_surface_walkable(surface_normal_at(body, probe_at, up, MAX_FLOOR_DIST), up, settings.max_slope_cos);
		}

		if (!landed_walkable)
		{
			if (delta.dot(down_sweep.normal) < 0.0f)
			{
				return false;
			}

			if (down_sweep.transform.get_origin().dot(up) > start_along_up)
			{
				return false;
			}
		}

		if (!is_within_edge_tolerance(down_sweep.transform.get_origin(), down_sweep.point, settings.capsule_radius, up))
		{
			return false;
		}
	}

	out_transform = down_sweep.transform;
	return true;
}

SlideResult slide_move(const Body& body, const Transform2D& from, const Vector2& motion, const Vector2& velocity, const Vector2& up, float max_slope_cos, int max_slides)
{
	SlideResult out;
	out.transform = from;
	out.velocity = velocity;

	Vector2 remaining = motion;

	for (int i = 0; i < max_slides; i++)
	{
		if (remaining.is_zero_approx())
		{
			break;
		}

		const SweepResult hit = sweep(body, out.transform, remaining);
		out.transform = hit.transform;

		if (!hit.collided)
		{
			break;
		}

		if (is_hit_surface_walkable(hit.normal, up, max_slope_cos))
		{
			out.hit_walkable = true;
			out.walkable_normal = hit.normal;
			out.walkable_collider = hit.collider;
		}
		else
		{
			out.hit_wall = true;
		}

		remaining = hit.remaining.slide(hit.normal);
		out.velocity = out.velocity.slide(hit.normal);
	}

	return out;
}

SlideResult move_along_floor(const Body& body, const Transform2D& from, const Vector2& motion, const Vector2& velocity, const Vector2& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings)
{
	SlideResult out;
	out.transform = from;
	out.velocity = velocity;

	Vector2 remaining = motion;

	for (int i = 0; i < settings.max_slides; i++)
	{
		if (remaining.is_zero_approx())
		{
			break;
		}

		const SweepResult hit = sweep(body, out.transform, remaining);
		out.transform = hit.transform;

		if (!hit.collided)
		{
			break;
		}

		if (is_hit_surface_walkable(hit.normal, up, settings.max_slope_cos))
		{
			out.hit_walkable = true;
			out.walkable_normal = hit.normal;
			out.walkable_collider = hit.collider;

			remaining = hit.remaining.slide(hit.normal);
			out.velocity = out.velocity.slide(hit.normal);
			continue;
		}

		out.hit_wall = true;

		// Too steep to walk on, so see whether it is short enough to walk over
		// before treating it as a wall. Getting this order wrong is what makes a
		// character slide along the front of every stair.
		Transform2D stepped;
		if (step_up(body, out.transform, hit.remaining, hit.point, up, current_floor, settings, stepped))
		{
			// A climb that gained no height is a wall, not a step. Against a tall
			// face all three sweeps can succeed and still put the character back on
			// the floor it started on, and taking that as a step breaks out of the
			// loop before the velocity is slid - so the character stands still while
			// reporting full speed, which is the figure animation reads.
			//
			// Height rather than forward progress: a real step is entered half a
			// pixel at a time, because the character is blocked and re-accelerating
			// from a standstill every tick.
			const float climbed = (stepped.get_origin() - out.transform.get_origin()).dot(up);
			if (climbed > settings.capsule_radius * 0.01f)
			{
				out.transform = stepped;
				out.stepped_up = true;
				break;
			}
		}

		remaining = hit.remaining.slide(hit.normal);
		out.velocity = out.velocity.slide(hit.normal);
	}

	return out;
}

void find_floor(const Body& body, const Transform2D& from, const Vector2& up, float sweep_distance, float line_distance, const GroundMoveSettings& settings, const Ref<FloorResult>& out_result)
{
	ERR_FAIL_COND_MSG(out_result.is_null(), "GFGD: MovementUtils2D::find_floor was given nowhere to write its result.");

	out_result->clear();

	if (!body.is_valid())
	{
		return;
	}

	if (sweep_distance > 0.0f)
	{
		const SweepResult hit = sweep(body, from, -up * sweep_distance);
		if (hit.collided)
		{
			const bool within_edge = is_within_edge_tolerance(hit.transform.get_origin(), hit.point, settings.capsule_radius, up);
			const bool walkable = within_edge && is_hit_surface_walkable(hit.normal, up, settings.max_slope_cos);

			out_result->set_blocking_hit(true);
			out_result->set_floor_dist((hit.transform.get_origin() - from.get_origin()).length());
			out_result->set_normal(Vector3(hit.normal.x, hit.normal.y, 0.0f));
			out_result->set_point(Vector3(hit.point.x, hit.point.y, 0.0f));
			out_result->set_collider(hit.collider);
			out_result->set_collider_velocity(Vector3(hit.collider_velocity.x, hit.collider_velocity.y, 0.0f));
			out_result->set_walkable_floor(walkable);

			if (walkable)
			{
				return;
			}
		}
	}

	if (line_distance <= 0.0f)
	{
		return;
	}

	PhysicsDirectSpaceState2D* space_state = PhysicsServer2D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return;
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	// The ray starts at the body origin, so it has to cover the half height
	// before it covers any of the gap being measured - the same correction the 3D
	// solver needs, and the same phantom falls without it.
	const float ray_length = line_distance + settings.capsule_half_height;
	const Ref<PhysicsRayQueryParameters2D> ray = PhysicsRayQueryParameters2D::create(from.get_origin(), from.get_origin() - up * ray_length, body.collision_mask, exclude);
	const Dictionary hit = space_state->intersect_ray(ray);
	if (hit.is_empty())
	{
		return;
	}

	const Vector2 normal = hit["normal"];
	if (!is_hit_surface_walkable(normal, up, settings.max_slope_cos))
	{
		return;
	}

	const Vector2 point = hit["position"];

	out_result->set_blocking_hit(true);
	out_result->set_line_trace(true);
	out_result->set_line_dist(MAX(0.0f, (from.get_origin() - point).dot(up) - settings.capsule_half_height));
	out_result->set_normal(Vector3(normal.x, normal.y, 0.0f));
	out_result->set_point(Vector3(point.x, point.y, 0.0f));
	out_result->set_collider(Object::cast_to<Object>(hit["collider"]));
	out_result->set_walkable_floor(true);
}

Transform2D adjust_floor_height(const Body& body, const Transform2D& from, const Vector2& up, const Ref<FloorResult>& floor)
{
	if (floor.is_null() || !floor->is_walkable_floor())
	{
		return from;
	}

	const float current_distance = floor->get_distance_to_floor();

	if (current_distance >= MIN_FLOOR_DIST && current_distance <= MAX_FLOOR_DIST)
	{
		return from;
	}

	const float correction = (0.5f * (MIN_FLOOR_DIST + MAX_FLOOR_DIST)) - current_distance;
	if (Math::abs(correction) < (float)CMP_EPSILON)
	{
		return from;
	}

	return sweep(body, from, up * correction).transform;
}

Vector2 compute_ground_movement_delta(const Vector2& delta, const Vector2& floor_normal, const Vector2& up)
{
	const float normal_along_up = floor_normal.dot(up);

	if (normal_along_up >= 1.0f - (float)CMP_EPSILON || normal_along_up <= (float)CMP_EPSILON)
	{
		return delta;
	}

	const float floor_dot_delta = floor_normal.dot(delta);
	return delta.slide(up) + up * (-floor_dot_delta / normal_along_up);
}

Vector2 apply_velocity_braking(const Vector2& in_velocity, float friction, float braking_deceleration, float braking_friction_factor, double delta)
{
	Vector2 velocity = in_velocity;

	if (velocity.is_zero_approx() || delta <= 0.0)
	{
		return velocity;
	}

	friction = MAX(0.0f, friction * MAX(0.0f, braking_friction_factor));
	braking_deceleration = MAX(0.0f, braking_deceleration);

	if (friction == 0.0f && braking_deceleration == 0.0f)
	{
		return velocity;
	}

	const Vector2 starting_velocity = velocity;
	const Vector2 reverse_acceleration = (braking_deceleration == 0.0f) ? Vector2() : (-braking_deceleration * velocity.normalized());

	float remaining_time = (float)delta;
	const float max_time_step = CLAMP(BRAKING_SUB_STEP_TIME, 1.0f / 75.0f, 1.0f / 20.0f);

	while (remaining_time > (float)CMP_EPSILON)
	{
		const float step = (remaining_time > max_time_step) ? MIN(max_time_step, remaining_time * 0.5f) : remaining_time;
		remaining_time -= step;

		velocity = velocity + ((-friction) * velocity + reverse_acceleration) * step;

		if (velocity.dot(starting_velocity) <= 0.0f)
		{
			return Vector2();
		}
	}

	if (velocity.length_squared() <= BRAKE_TO_STOP_VELOCITY * BRAKE_TO_STOP_VELOCITY)
	{
		return Vector2();
	}

	return velocity;
}

Vector2 compute_velocity(const Vector2& in_velocity, const Vector2& acceleration, float friction, float braking_deceleration, float braking_friction_factor, float max_speed, double delta)
{
	if (delta <= 0.0)
	{
		return in_velocity;
	}

	Vector2 velocity = in_velocity;
	max_speed = MAX(0.0f, max_speed);

	const bool zero_acceleration = acceleration.is_zero_approx();
	const bool over_max_speed = velocity.length_squared() > (max_speed * max_speed) + (float)CMP_EPSILON;

	if (zero_acceleration || over_max_speed)
	{
		const Vector2 old_velocity = velocity;
		velocity = apply_velocity_braking(velocity, friction, braking_deceleration, braking_friction_factor, delta);

		if (over_max_speed && velocity.length_squared() < max_speed * max_speed && acceleration.dot(old_velocity) > 0.0f)
		{
			velocity = old_velocity.normalized() * max_speed;
		}
	}
	else
	{
		const Vector2 acceleration_direction = acceleration.normalized();
		const float speed = velocity.length();
		velocity = velocity - (velocity - acceleration_direction * speed) * MIN((float)delta * MAX(0.0f, friction), 1.0f);
	}

	if (!zero_acceleration)
	{
		const float speed_cap = over_max_speed ? MAX(velocity.length(), max_speed) : max_speed;
		velocity += acceleration * (float)delta;
		velocity = velocity.limit_length(speed_cap);
	}

	return velocity;
}

}
}
