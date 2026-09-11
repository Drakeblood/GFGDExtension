#include "movement/movement_utils.h"

#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;
using namespace GFGD;

namespace GFGD
{
namespace MovementUtils
{

SweepResult sweep(const Body& body, const Transform3D& from, const Vector3& motion)
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
	body.params->set_max_collisions(1);

	// Depenetration still happens and is folded into the reported travel; it just
	// does not count as a collision. A character that spawns half inside the floor
	// is pushed out rather than reporting a wall it never moved into.
	body.params->set_recovery_as_collision_enabled(false);

	out.collided = PhysicsServer3D::get_singleton()->body_test_motion(body.rid, body.params, body.result);

	out.transform = from.translated(body.result->get_travel());
	out.remaining = body.result->get_remainder();

	if (out.collided && body.result->get_collision_count() > 0)
	{
		out.normal = body.result->get_collision_normal(0);
		out.point = body.result->get_collision_point(0);
		out.collider = body.result->get_collider(0);
		out.collider_velocity = body.result->get_collider_velocity(0);
	}

	return out;
}

SlideResult slide_move(const Body& body, const Transform3D& from, const Vector3& motion, const Vector3& velocity, const Vector3& up, float max_slope_cos, int max_slides)
{
	SlideResult out;
	out.transform = from;
	out.velocity = velocity;

	Vector3 remaining = motion;
	Vector3 previous_normal;
	bool has_previous_normal = false;

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

		// The first impact is a plain slide; a second one means a corner, where
		// sliding along either wall alone would push straight back into the other.
		if (has_previous_normal)
		{
			remaining = two_wall_adjust(hit.remaining, hit.normal, previous_normal);
		}
		else
		{
			remaining = compute_slide_vector(hit.remaining, 1.0f, hit.normal);
		}

		out.velocity = out.velocity.slide(hit.normal);

		previous_normal = hit.normal;
		has_previous_normal = true;
	}

	return out;
}

bool step_up(const Body& body, const Transform3D& from, const Vector3& delta, const Vector3& hit_point, const Vector3& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings, Transform3D& out_transform)
{
	if (settings.max_step_height <= 0.0f || !body.is_valid())
	{
		return false;
	}

	const float impact_along_up = hit_point.dot(up);
	const float start_along_up = from.origin.dot(up);

	// An impact against the capsule's dome rather than its side is a ceiling, not
	// a step. Climbing towards it would only wedge the character.
	if (impact_along_up > start_along_up + (settings.capsule_half_height - settings.capsule_radius))
	{
		return false;
	}

	float travel_up = settings.max_step_height;
	float travel_down = settings.max_step_height;

	// Two different heights, and the difference matters. floor_base is where the
	// ground actually is, used to reject an impact that is already below us.
	// floor_point is what the climb is measured against, so that hovering the
	// usual fraction above the floor does not eat into the step budget.
	float floor_base_along_up = start_along_up - settings.capsule_half_height;
	float floor_point_along_up = floor_base_along_up;

	if (current_floor.is_valid() && current_floor->is_walkable_floor())
	{
		const float floor_distance = MAX(0.0f, current_floor->get_distance_to_floor());

		floor_base_along_up -= floor_distance;
		travel_up = MAX(travel_up - floor_distance, 0.0f);
		travel_down = settings.max_step_height + MAX_FLOOR_DIST * 2.0f;

		const bool hit_vertical_face = !is_within_edge_tolerance(from.origin, hit_point, settings.capsule_radius, up);
		if (!current_floor->get_line_trace() && !hit_vertical_face)
		{
			floor_point_along_up = current_floor->get_point().dot(up);
		}
		else
		{
			floor_point_along_up -= current_floor->get_floor_dist();
		}
	}

	// The thing blocking us is at or below our feet, so there is nothing to climb.
	if (impact_along_up <= floor_base_along_up)
	{
		return false;
	}

	const SweepResult up_sweep = sweep(body, from, up * travel_up);
	const SweepResult forward_sweep = sweep(body, up_sweep.transform, delta);

	Transform3D after_forward = forward_sweep.transform;

	if (forward_sweep.collided)
	{
		const Vector3 leftover = compute_slide_vector(forward_sweep.remaining, 1.0f, forward_sweep.normal);
		const SweepResult slide_sweep = sweep(body, after_forward, leftover);
		after_forward = slide_sweep.transform;

			// Climbing to go nowhere is not worth the vertical displacement it would
			// leave behind.
			if (after_forward.origin.distance_squared_to(up_sweep.transform.origin) < (float)CMP_EPSILON)
		{
			return false;
		}
	}

	const SweepResult down_sweep = sweep(body, after_forward, -up * travel_down);

	if (down_sweep.collided)
	{
		// The whole point of the step down: measure what was actually climbed and
		// refuse it if it was more than a step.
		const float climbed = down_sweep.point.dot(up) - floor_point_along_up;
		if (climbed > settings.max_step_height)
		{
			return false;
		}

		// A sweep that lands on the lip of a step reports a normal blended between
		// the top face and the front one - Godot has no equivalent of Unreal's
		// separate ImpactNormal - and that blend reads as unwalkable even when the
		// surface underneath is flat. Ask the surface itself before believing it.
		bool landed_walkable = is_hit_surface_walkable(down_sweep.normal, up, settings.max_slope_cos);
		if (!landed_walkable)
		{
			// Nudged into the surface along the direction of travel before asking.
			// The contact a sweep reports sits one collision margin in front of what
			// it touched, so a ray from the point itself passes in front of a step's
			// top face and finds nothing - and the edge normal, which is mostly
			// sideways, then reads as a wall.
			const Vector3 probe_at = down_sweep.point + delta.normalized() * (body.margin * 2.0f);
			const Vector3 surface_normal = surface_normal_at(body, probe_at, up, MAX_FLOOR_DIST);
			landed_walkable = is_hit_surface_walkable(surface_normal, up, settings.max_slope_cos);
		}

		if (!landed_walkable)
		{
			// Landing on something too steep is only acceptable when it is below
			// where we started - that is walking down onto a slope, which is fine.
			// Facing into it, or ending up higher, is not.
			if (delta.dot(down_sweep.normal) < 0.0f)
			{
				return false;
			}

			if (down_sweep.transform.origin.dot(up) > start_along_up)
			{
				return false;
			}
		}

		// Same rejection find_floor makes, for the same reason: a hit this close
		// to the capsule's edge is the curve of the capsule, not a surface.
		if (!is_within_edge_tolerance(down_sweep.transform.origin, down_sweep.point, settings.capsule_radius, up))
		{
			return false;
		}
	}

	out_transform = down_sweep.transform;
	return true;
}

SlideResult move_along_floor(const Body& body, const Transform3D& from, const Vector3& delta, const Vector3& velocity, const Vector3& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings)
{
	SlideResult out;
	out.transform = from;
	out.velocity = velocity;

	Vector3 remaining = delta;
	Vector3 previous_normal;
	bool has_previous_normal = false;

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

			remaining = compute_slide_vector(hit.remaining, 1.0f, hit.normal);
			out.velocity = out.velocity.slide(hit.normal);
			previous_normal = hit.normal;
			has_previous_normal = true;
			continue;
		}

		out.hit_wall = true;

		// Too steep to walk on - so before sliding along it like a wall, see
		// whether it is short enough to walk *over*. This is the only place a step
		// is told apart from a wall, and getting the order wrong means a character
		// that slides along the front of every stair.
		Transform3D stepped;
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
			const float climbed = (stepped.origin - out.transform.origin).dot(up);
			if (climbed > settings.capsule_radius * 0.01f)
			{
				out.transform = stepped;
				out.stepped_up = true;
				break;
			}
		}

		if (has_previous_normal)
		{
			remaining = two_wall_adjust(hit.remaining, hit.normal, previous_normal);
		}
		else
		{
			remaining = compute_slide_vector(hit.remaining, 1.0f, hit.normal);
		}

		out.velocity = out.velocity.slide(hit.normal);
		previous_normal = hit.normal;
		has_previous_normal = true;
	}

	return out;
}

float get_valid_perch_radius(const GroundMoveSettings& settings)
{
	// The 11 centimetre floor is Unreal's, and it is there so that a threshold
	// larger than the capsule cannot collapse the test to a point.
	return CLAMP(settings.capsule_radius - settings.perch_radius_threshold, 0.11f, settings.capsule_radius);
}

bool should_compute_perch_result(const GroundMoveSettings& settings, const Vector3& capsule_center, const Vector3& impact_point, const Vector3& up, bool check_radius)
{
	if (settings.perch_radius_threshold <= SWEEP_EDGE_REJECT_DISTANCE)
	{
		return false;
	}

	if (check_radius)
	{
		const float distance_squared = (impact_point - capsule_center).slide(up).length_squared();
		const float perch_radius = get_valid_perch_radius(settings);

		// The contact is already well inside the footprint, so this is ordinary
		// ground rather than an edge and the narrow test would only agree.
		if (distance_squared <= perch_radius * perch_radius)
		{
			return false;
		}
	}

	return true;
}

void compute_floor_dist_with_radius(const Body& body, const Transform3D& from, const Vector3& up, float test_radius, float capsule_half_height, float line_distance, float sweep_distance, float max_slope_cos, const Ref<FloorResult>& out_result)
{
	ERR_FAIL_COND_MSG(out_result.is_null(), "GFGD: MovementUtils::compute_floor_dist_with_radius was given nowhere to write its result.");

	out_result->clear();

	if (!body.can_probe())
	{
		return;
	}

	PhysicsDirectSpaceState3D* space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return;
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	if (sweep_distance > 0.0f)
	{
		body.probe_shape->set_radius(test_radius);
		body.probe_shape->set_height(MAX(2.0f * capsule_half_height, 2.0f * test_radius));

		body.probe_params->set_shape(body.probe_shape);
		body.probe_params->set_transform(from);
		body.probe_params->set_motion(-up * sweep_distance);
		body.probe_params->set_margin(body.margin);
		body.probe_params->set_collision_mask(body.collision_mask);
		body.probe_params->set_exclude(exclude);

		const PackedFloat32Array fractions = space_state->cast_motion(body.probe_params);

		// cast_motion reports where the shape can safely stop and where it first
		// touches. Equal to one means it never touched anything.
		if (fractions.size() >= 2 && fractions[0] < 1.0f)
		{
			const float safe_fraction = fractions[0];
			const float unsafe_fraction = fractions[1];

			// The normal has to be read where the shape is actually in contact,
			// which is the unsafe end of the cast, not the safe one.
			Transform3D contact = from;
			contact.origin -= up * (sweep_distance * unsafe_fraction);

			body.probe_params->set_transform(contact);
			body.probe_params->set_motion(Vector3());

			const Dictionary rest = space_state->get_rest_info(body.probe_params);
			if (!rest.is_empty())
			{
				const Vector3 point = rest["point"];
				Vector3 surface_normal = rest["normal"];

				// The same correction find_floor_basic makes, and for the same
				// reason: a contact out at the rim reports a normal blended between
				// the surface and whatever drops away beside it. Here that matters
				// more, not less. This test exists to judge a footprint hanging over
				// a lip, so an edge contact is not the exception - it is every case
				// it is asked about. Measured on a 0.16 m slot, the raw normal came
				// back at 47 degrees for ground that is flat: two degrees over the
				// limit, and enough that perching could only ever take standing
				// away and never give it back.
				const Vector3 offset = (point - contact.origin).slide(up);
				if (offset.length() > SWEEP_EDGE_REJECT_DISTANCE)
				{
					const Vector3 probed = surface_normal_at(body, point + offset.normalized() * (body.margin * 4.0f), up, MAX_FLOOR_DIST);
					if (is_hit_surface_walkable(probed, up, max_slope_cos))
					{
						surface_normal = probed;
					}
				}

				out_result->set_blocking_hit(true);
				out_result->set_floor_dist(sweep_distance * safe_fraction);
				out_result->set_normal(surface_normal);
				out_result->set_point(point);
				out_result->set_walkable_floor(is_hit_surface_walkable(surface_normal, up, max_slope_cos));

				if (out_result->is_walkable_floor())
				{
					return;
				}
			}
		}
	}

	// Same fallback as find_floor, and it needs the same correction: the ray
	// starts at the body origin, so it has to cover the half-height first.
	if (line_distance <= 0.0f)
	{
		return;
	}

	const float ray_length = line_distance + capsule_half_height;
	const Ref<PhysicsRayQueryParameters3D> ray = PhysicsRayQueryParameters3D::create(from.origin, from.origin - up * ray_length, body.collision_mask, exclude);
	const Dictionary hit = space_state->intersect_ray(ray);
	if (hit.is_empty())
	{
		return;
	}

	const Vector3 normal = hit["normal"];
	if (!is_hit_surface_walkable(normal, up, max_slope_cos))
	{
		return;
	}

	const Vector3 point = hit["position"];

	out_result->set_blocking_hit(true);
	out_result->set_line_trace(true);
	out_result->set_line_dist(MAX(0.0f, (from.origin - point).dot(up) - capsule_half_height));
	out_result->set_normal(normal);
	out_result->set_point(point);
	out_result->set_collider(Object::cast_to<Object>(hit["collider"]));
	out_result->set_walkable_floor(true);
}

bool compute_perch_result(const Body& body, const Transform3D& from, const Vector3& up, const Ref<FloorResult>& blocking_floor, float max_floor_dist, const GroundMoveSettings& settings, const Ref<FloorResult>& out_result)
{
	if (max_floor_dist <= 0.0f || blocking_floor.is_null() || out_result.is_null())
	{
		return false;
	}

	// How far up the capsule the blocking contact sat. A contact well above the
	// base eats into the distance the narrow test is allowed to look.
	const float hit_above_base = MAX(0.0f, (blocking_floor->get_point() - from.origin).dot(up) + settings.capsule_half_height);

	const float line_distance = MAX(0.0f, max_floor_dist - hit_above_base);

	// Swept further than asked, because a narrower shape slips past contacts the
	// full-width one would have caught.
	const float sweep_distance = MAX(0.0f, max_floor_dist) + settings.capsule_radius;

	compute_floor_dist_with_radius(body, from, up, get_valid_perch_radius(settings), settings.capsule_half_height, line_distance, sweep_distance, settings.max_slope_cos, out_result);

	if (!out_result->is_walkable_floor())
	{
		return false;
	}

	if (hit_above_base + out_result->get_floor_dist() > max_floor_dist)
	{
		// Something is down there, but too far to be standing on.
		out_result->set_walkable_floor(false);
		return false;
	}

	return true;
}

Object* find_area_at(const Body& body, const Vector3& point)
{
	if (!body.is_valid() || body.point_params.is_null())
	{
		return nullptr;
	}

	PhysicsDirectSpaceState3D* space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(body.space);
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

bool would_capsule_fit(const Body& body, const Transform3D& at, float radius, float half_height)
{
	if (!body.can_probe())
	{
		return true;
	}

	PhysicsDirectSpaceState3D* space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return true;
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	// Pulled in very slightly, so resting against a wall does not read as being
	// blocked by it.
	body.probe_shape->set_radius(MAX(0.01f, radius - SWEEP_EDGE_REJECT_DISTANCE));
	body.probe_shape->set_height(MAX(2.0f * half_height - SWEEP_EDGE_REJECT_DISTANCE, 2.0f * radius));

	body.probe_params->set_shape(body.probe_shape);
	body.probe_params->set_transform(at);
	body.probe_params->set_motion(Vector3());
	body.probe_params->set_margin(0.0f);
	body.probe_params->set_collision_mask(body.collision_mask);
	body.probe_params->set_exclude(exclude);

	return space_state->intersect_shape(body.probe_params, 1).is_empty();
}

Vector3 surface_normal_at(const Body& body, const Vector3& point, const Vector3& up, float probe_distance)
{
	if (!body.is_valid())
	{
		return Vector3();
	}

	PhysicsDirectSpaceState3D* space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return Vector3();
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	const Ref<PhysicsRayQueryParameters3D> ray = PhysicsRayQueryParameters3D::create(point + up * probe_distance, point - up * probe_distance, body.collision_mask, exclude);
	const Dictionary hit = space_state->intersect_ray(ray);
	if (hit.is_empty())
	{
		return Vector3();
	}

	return hit["normal"];
}

bool is_hit_surface_walkable(const Vector3& normal, const Vector3& up, float max_slope_cos)
{
	if (normal.is_zero_approx())
	{
		return false;
	}

	// A surface facing away from up is a ceiling, never a floor, whatever the
	// slope limit says.
	const float facing = normal.dot(up);
	if (facing <= 0.0f)
	{
		return false;
	}

	return facing >= max_slope_cos;
}

bool is_within_edge_tolerance(const Vector3& capsule_center, const Vector3& impact_point, float capsule_radius, const Vector3& up)
{
	// Only the distance across the capsule matters; how far down the impact is
	// says nothing about whether it belongs to the surface below.
	const Vector3 offset = (impact_point - capsule_center).slide(up);
	const float distance_squared = offset.length_squared();

	const float reduced_radius = MAX(SWEEP_EDGE_REJECT_DISTANCE + (float)CMP_EPSILON, capsule_radius - SWEEP_EDGE_REJECT_DISTANCE);
	return distance_squared < reduced_radius * reduced_radius;
}

static void find_floor_basic(const Body& body, const Transform3D& from, const Vector3& up, float sweep_distance, float line_distance, float capsule_half_height, float capsule_radius, float max_slope_cos, const Ref<FloorResult>& out_result)
{
	ERR_FAIL_COND_MSG(out_result.is_null(), "GFGD: MovementUtils::find_floor was given nowhere to write its result.");

	out_result->clear();

	if (!body.is_valid())
	{
		return;
	}

	// Phase one: sweep the shape straight down.
	if (sweep_distance > 0.0f)
	{
		const SweepResult hit = sweep(body, from, -up * sweep_distance);
		if (hit.collided)
		{
			Vector3 surface_normal = hit.normal;

			// A contact off to one side is what an edge looks like, and the normal a
			// sweep reports there is blended between the top face and the drop - it
			// stays walkable while tilting further and further, so the ground move
			// follows it downhill and the character sinks along a slope that is not
			// there. Unreal has a separate ImpactNormal for this; Godot has one
			// normal, so the surface is asked directly.
			//
			// Probed outwards from the capsule's axis, which is the direction the
			// supporting surface is in when the character is overhanging. A contact
			// under the middle - ordinary ground - is skipped entirely.
			const Vector3 offset = (hit.point - hit.transform.origin).slide(up);
			if (offset.length() > SWEEP_EDGE_REJECT_DISTANCE)
			{
				const Vector3 probed = surface_normal_at(body, hit.point + offset.normalized() * (body.margin * 4.0f), up, MAX_FLOOR_DIST);
				if (is_hit_surface_walkable(probed, up, max_slope_cos))
				{
					surface_normal = probed;
				}
			}

			// A contact this close to the capsule's rim belongs to the curve of the
			// capsule catching an edge, not to a surface anything could stand on.
			const bool within_edge = is_within_edge_tolerance(hit.transform.origin, hit.point, capsule_radius, up);
			const bool walkable = within_edge && is_hit_surface_walkable(surface_normal, up, max_slope_cos);

			out_result->set_blocking_hit(true);
			out_result->set_floor_dist((hit.transform.origin - from.origin).length());
			out_result->set_normal(surface_normal);
			out_result->set_point(hit.point);
			out_result->set_collider(hit.collider);
			out_result->set_collider_velocity(hit.collider_velocity);
			out_result->set_walkable_floor(walkable);

			if (walkable)
			{
				return;
			}
		}
	}

	// Phase two: a ray, for when the sweep caught something that is not a floor.
	// The case this exists for is a character standing where a wall meets the
	// ground: the sweep reports the wall, and without the ray the character
	// decides it has no floor and starts falling.
	if (line_distance <= 0.0f)
	{
		return;
	}

	PhysicsDirectSpaceState3D* space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(body.space);
	if (space_state == nullptr)
	{
		return;
	}

	TypedArray<RID> exclude;
	exclude.push_back(body.rid);

	// The ray starts at the body origin, not at the capsule's feet, so it has to
	// cover the half-height before it covers any of the gap being measured.
	// Without that it cannot reach the floor even when the character is standing
	// on it, and every sweep the first phase rejects becomes a phantom fall.
	const float ray_length = line_distance + capsule_half_height;
	const Ref<PhysicsRayQueryParameters3D> ray = PhysicsRayQueryParameters3D::create(from.origin, from.origin - up * ray_length, body.collision_mask, exclude);
	const Dictionary hit = space_state->intersect_ray(ray);
	if (hit.is_empty())
	{
		return;
	}

	const Vector3 normal = hit["normal"];
	if (!is_hit_surface_walkable(normal, up, max_slope_cos))
	{
		return;
	}

	const Vector3 point = hit["position"];

	// The ray started at the body origin, so its distance has to be brought back
	// to the same measure the sweep reports: the gap below the capsule.
	const float distance_from_origin = (from.origin - point).dot(up);

	out_result->set_blocking_hit(true);
	out_result->set_line_trace(true);
	out_result->set_line_dist(MAX(0.0f, distance_from_origin - capsule_half_height));
	out_result->set_normal(normal);
	out_result->set_point(point);
	out_result->set_collider(Object::cast_to<Object>(hit["collider"]));
	out_result->set_walkable_floor(true);
}

void find_floor(const Body& body, const Transform3D& from, const Vector3& up, float sweep_distance, float line_distance, const GroundMoveSettings& settings, bool is_moving_on_ground, const Ref<FloorResult>& out_result, const Ref<FloorResult>& perch_scratch)
{
	find_floor_basic(body, from, up, sweep_distance, line_distance, settings.capsule_half_height, settings.capsule_radius, settings.max_slope_cos, out_result);

	if (out_result.is_null() || perch_scratch.is_null())
	{
		return;
	}

	// Only a sweep result can be perched on. A floor the ray found is already the
	// fallback answer, and narrowing it further would be asking the same question
	// twice.
	if (!out_result->get_blocking_hit() || out_result->get_line_trace())
	{
		return;
	}

	if (!should_compute_perch_result(settings, from.origin, out_result->get_point(), up, true))
	{
		return;
	}

	float max_perch_floor_dist = MAX(MAX_FLOOR_DIST, settings.max_step_height);
	if (is_moving_on_ground)
	{
		max_perch_floor_dist += MAX(0.0f, settings.perch_additional_height);
	}

	if (!compute_perch_result(body, from, up, out_result, max_perch_floor_dist, settings, perch_scratch))
	{
		// The contact is near the capsule's edge and nothing holds up the inner
		// footprint. This is the half of perching that takes standing away.
		out_result->set_walkable_floor(false);
		return;
	}

	// Do not let the floor adjustment lift the character so far that it leaves
	// perch range and falls on the next tick instead.
	const float average_floor_dist = 0.5f * (MIN_FLOOR_DIST + MAX_FLOOR_DIST);
	const float move_up_distance = average_floor_dist - out_result->get_floor_dist();
	if (move_up_distance + perch_scratch->get_floor_dist() >= max_perch_floor_dist)
	{
		out_result->set_floor_dist(average_floor_dist);
	}

	if (out_result->get_walkable_floor())
	{
		return;
	}

	// The wide capsule is on something unwalkable but the narrow one has ground,
	// so adopt the narrow answer. It is recorded as a line result on purpose: the
	// distance that has to drive adjust_floor_height is still the wide capsule's.
	out_result->set_line_trace(true);
	out_result->set_line_dist(MAX(out_result->get_floor_dist(), MIN_FLOOR_DIST));
	out_result->set_normal(perch_scratch->get_normal());
	out_result->set_point(perch_scratch->get_point());
	out_result->set_collider(perch_scratch->get_collider());
	out_result->set_walkable_floor(true);
}

Transform3D adjust_floor_height(const Body& body, const Transform3D& from, const Vector3& up, float capsule_radius, const Ref<FloorResult>& floor)
{
	if (floor.is_null() || !floor->is_walkable_floor())
	{
		return from;
	}

	const float current_distance = floor->get_distance_to_floor();

	// Inside the band there is nothing to correct. The band is the point: a
	// single target distance would have the adjustment overshoot and fight
	// itself on every tick.
	if (current_distance >= MIN_FLOOR_DIST && current_distance <= MAX_FLOOR_DIST)
	{
		return from;
	}

	const float target_distance = 0.5f * (MIN_FLOOR_DIST + MAX_FLOOR_DIST);
	const float correction = target_distance - current_distance;

	if (Math::abs(correction) < (float)CMP_EPSILON)
	{
		return from;
	}

	// Never settle *down* onto a contact that is off to one side. That is a ledge
	// under the edge of the capsule, not ground under its feet, and each tick of
	// pulling towards it walks the character off the drop a few centimetres at a
	// time - the floor stays walkable the whole way down, so nothing else stops
	// it. Pushing up out of a surface is always fine.
	if (correction < 0.0f)
	{
		const Vector3 contact_offset = (floor->get_point() - from.origin).slide(up);
		if (contact_offset.length() > capsule_radius * 0.5f)
		{
			return from;
		}
	}

	const SweepResult hit = sweep(body, from, up * correction);
	return hit.transform;
}

Vector3 apply_velocity_braking(const Vector3& in_velocity, float friction, float braking_deceleration, float braking_friction_factor, double delta)
{
	Vector3 velocity = in_velocity;

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

	const Vector3 starting_velocity = velocity;
	const Vector3 reverse_acceleration = (braking_deceleration == 0.0f) ? Vector3() : (-braking_deceleration * velocity.normalized());

	// Integrated in fixed steps so the stopping distance a player feels is the
	// same at 30 and at 144 frames per second.
	float remaining_time = (float)delta;
	const float max_time_step = CLAMP(BRAKING_SUB_STEP_TIME, 1.0f / 75.0f, 1.0f / 20.0f);

	while (remaining_time > (float)CMP_EPSILON)
	{
		const float step = (remaining_time > max_time_step) ? MIN(max_time_step, remaining_time * 0.5f) : remaining_time;
		remaining_time -= step;

		velocity = velocity + ((-friction) * velocity + reverse_acceleration) * step;

		// Braking may stop a character; it may never reverse one.
		if (velocity.dot(starting_velocity) <= 0.0f)
		{
			return Vector3();
		}
	}

	if (velocity.length_squared() <= BRAKE_TO_STOP_VELOCITY * BRAKE_TO_STOP_VELOCITY)
	{
		return Vector3();
	}

	return velocity;
}

Vector3 compute_velocity(const Vector3& in_velocity, const Vector3& acceleration, float friction, float braking_deceleration, float braking_friction_factor, float max_speed, double delta)
{
	if (delta <= 0.0)
	{
		return in_velocity;
	}

	Vector3 velocity = in_velocity;
	max_speed = MAX(0.0f, max_speed);

	const bool zero_acceleration = acceleration.is_zero_approx();
	const bool over_max_speed = velocity.length_squared() > (max_speed * max_speed) + (float)CMP_EPSILON;

	if (zero_acceleration || over_max_speed)
	{
		const Vector3 old_velocity = velocity;
		velocity = apply_velocity_braking(velocity, friction, braking_deceleration, braking_friction_factor, delta);

		// Braking must not drag a character that was over the cap below it while
		// they are still pushing forward - that would make a landing from a launch
		// feel like hitting glue.
		if (over_max_speed && velocity.length_squared() < max_speed * max_speed && acceleration.dot(old_velocity) > 0.0f)
		{
			velocity = old_velocity.normalized() * max_speed;
		}
	}
	else
	{
		// Friction here is not braking: it decides how fast a character can change
		// direction while still being pushed. Turning sharply at speed is what this
		// number controls.
		const Vector3 acceleration_direction = acceleration.normalized();
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

Vector3 compute_ground_movement_delta(const Vector3& delta, const Vector3& floor_normal, const Vector3& up)
{
	const float normal_along_up = floor_normal.dot(up);

	// Flat ground and walls are both left alone: there is no ramp to follow in
	// the first case, and dividing by the second would blow up.
	if (normal_along_up >= 1.0f - (float)CMP_EPSILON || normal_along_up <= (float)CMP_EPSILON)
	{
		return delta;
	}

	const float floor_dot_delta = floor_normal.dot(delta);
	return delta.slide(up) + up * (-floor_dot_delta / normal_along_up);
}

Vector3 compute_slide_vector(const Vector3& delta, float time, const Vector3& normal)
{
	return delta.slide(normal) * time;
}

Vector3 two_wall_adjust(const Vector3& delta, const Vector3& new_normal, const Vector3& old_normal)
{
	Vector3 result = delta;

	if (old_normal.dot(new_normal) <= 0.0f)
	{
		// A corner of ninety degrees or tighter. The only direction that does not
		// push back into one of the two walls is the crease between them.
		Vector3 crease = new_normal.cross(old_normal);
		if (crease.is_zero_approx())
		{
			return Vector3();
		}

		crease = crease.normalized();
		result = crease * result.dot(crease);

		if (new_normal.dot(result) < 0.0f)
		{
			result = -result;
		}
	}
	else
	{
		// An obtuse corner: sliding along the new wall is enough, unless doing so
		// would send the character back the way they came.
		result = result.slide(new_normal);

		if (result.dot(delta) <= 0.0f)
		{
			return Vector3();
		}
	}

	return result;
}

}
}
