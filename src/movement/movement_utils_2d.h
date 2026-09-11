#ifndef MOVEMENT_UTILS_2D_H
#define MOVEMENT_UTILS_2D_H

#include <godot_cpp/classes/capsule_shape2d.hpp>
#include <godot_cpp/classes/physics_point_query_parameters2d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters2d.hpp>
#include <godot_cpp/classes/physics_test_motion_parameters2d.hpp>
#include <godot_cpp/classes/physics_test_motion_result2d.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "movement/movement_types.h"

using namespace godot;

namespace GFGD
{
// The collision solver, in two dimensions.
//
// A parallel implementation rather than a template over the 3D one. Godot keeps
// 2D and 3D in entirely separate hierarchies - CharacterBody2D and
// CharacterBody3D share no base, PhysicsServer2D and PhysicsServer3D share no
// interface - and GFGD already follows that split with PlayerStart2D and
// PlayerStart3D. A template spanning the two would have to abstract over the
// difference between a Basis and an angle, and would be harder to read than the
// duplication it saved.
//
// The same rule applies as in 3D: every function takes the transform to work
// from rather than reading it off a node, so a tick can be replayed.
namespace MovementUtils2D
{
// A 2D state keeps its position and velocity in the x and y of the shared
// Vector3 types and leaves z alone. These two are the whole of that seam.
inline Vector2 to_2d(const Vector3& v) { return Vector2(v.x, v.y); }
inline Vector3 to_3d(const Vector2& v) { return Vector3(v.x, v.y, 0.0f); }

// 2D works in pixels, and the convention that a metre is a hundred of them puts
// these back at the values CharacterMovementComponent uses in centimetres. The
// 3D solver divides the same numbers by a hundred.
constexpr float MIN_FLOOR_DIST = 1.9f;
constexpr float MAX_FLOOR_DIST = 2.4f;
constexpr float SWEEP_EDGE_REJECT_DISTANCE = 0.15f;
constexpr float BRAKE_TO_STOP_VELOCITY = 10.0f;
constexpr float BRAKING_SUB_STEP_TIME = 1.0f / 33.0f;

struct Body
{
	RID rid;
	RID space;
	uint32_t collision_mask;
	float margin;

	Ref<PhysicsTestMotionParameters2D> params;
	Ref<PhysicsTestMotionResult2D> result;

	// The headroom test needs a shape narrower and shorter than the body's own,
	// and body_test_motion can only ever sweep the real one.
	Ref<CapsuleShape2D> probe_shape;
	Ref<PhysicsShapeQueryParameters2D> probe_params;

	// Asks what area covers a point. Water is read this way, from the transform
	// being simulated, rather than accumulated from enter and exit signals.
	Ref<PhysicsPointQueryParameters2D> point_params;

	Body()
		: collision_mask(0xFFFFFFFF)
		, margin(0.08f)
	{
	}

	bool is_valid() const { return rid.is_valid() && space.is_valid() && params.is_valid() && result.is_valid(); }

	bool can_probe() const { return is_valid() && probe_shape.is_valid() && probe_params.is_valid(); }
};

struct SweepResult
{
	Transform2D transform;
	Vector2 remaining;
	bool collided;
	Vector2 normal;
	Vector2 point;
	Object* collider;
	Vector2 collider_velocity;

	SweepResult()
		: collided(false)
		, collider(nullptr)
	{
	}
};

struct GroundMoveSettings
{
	float max_slope_cos;
	int max_slides;
	float max_step_height;
	float capsule_radius;
	float capsule_half_height;

	GroundMoveSettings()
		: max_slope_cos(0.7071f)
		, max_slides(4)
		, max_step_height(45.0f)
		, capsule_radius(16.0f)
		, capsule_half_height(32.0f)
	{
	}
};

struct SlideResult
{
	Transform2D transform;
	Vector2 velocity;
	bool hit_wall;
	bool hit_walkable;
	Vector2 walkable_normal;
	Object* walkable_collider;
	bool stepped_up;

	SlideResult()
		: hit_wall(false)
		, hit_walkable(false)
		, walkable_collider(nullptr)
		, stepped_up(false)
	{
	}
};

// One shape cast, from a transform handed in rather than from wherever the body
// happens to be.
SweepResult sweep(const Body& body, const Transform2D& from, const Vector2& motion);

// Collide and slide. There is no two-wall adjustment here: in two dimensions
// being blocked by a second surface means being wedged, and the right answer is
// to stop rather than to find a crease.
SlideResult slide_move(const Body& body, const Transform2D& from, const Vector2& motion, const Vector2& velocity, const Vector2& up, float max_slope_cos, int max_slides);

// Climbs what just blocked a ground move, if it is short enough: up, forward,
// then back down, keeping the result only if it lands somewhere walkable.
bool step_up(const Body& body, const Transform2D& from, const Vector2& delta, const Vector2& hit_point, const Vector2& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings, Transform2D& out_transform);

// The walking move: slide along the floor, trying to step over anything too
// steep before giving up and sliding along it.
SlideResult move_along_floor(const Body& body, const Transform2D& from, const Vector2& motion, const Vector2& velocity, const Vector2& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings);

// The normal of the surface at a point, read with a short ray. Same reason as in
// 3D: a sweep landing on the lip of a step reports a normal blended towards the
// wall, and Godot has no separate impact normal to ask instead.
Vector2 surface_normal_at(const Body& body, const Vector2& point, const Vector2& up, float probe_distance);

// The area at a point, if any. Bodies are ignored: only areas answer.
Object* find_area_at(const Body& body, const Vector2& point);

// Whether a capsule of this size would fit here without overlapping anything.
// Used to decide whether a crouching character may stand up; the body's own
// shape is still the crouched one when the question is asked.
bool would_capsule_fit(const Body& body, const Transform2D& at, float radius, float half_height);

bool is_hit_surface_walkable(const Vector2& normal, const Vector2& up, float max_slope_cos);
bool is_within_edge_tolerance(const Vector2& center, const Vector2& impact_point, float capsule_radius, const Vector2& up);

// Is there ground under this transform, and may it be stood on. Sweep first,
// then a ray for when the sweep caught something that is not a floor.
void find_floor(const Body& body, const Transform2D& from, const Vector2& up, float sweep_distance, float line_distance, const GroundMoveSettings& settings, const Ref<FloorResult>& out_result);

// Keeps the body inside the MIN/MAX_FLOOR_DIST band.
Transform2D adjust_floor_height(const Body& body, const Transform2D& from, const Vector2& up, const Ref<FloorResult>& floor);

// Bends a move to run along a slope instead of into or off it.
Vector2 compute_ground_movement_delta(const Vector2& delta, const Vector2& floor_normal, const Vector2& up);

// The acceleration, friction and braking model. Friction steers, braking stops.
Vector2 compute_velocity(const Vector2& in_velocity, const Vector2& acceleration, float friction, float braking_deceleration, float braking_friction_factor, float max_speed, double delta);
Vector2 apply_velocity_braking(const Vector2& in_velocity, float friction, float braking_deceleration, float braking_friction_factor, double delta);
}
}

#endif
