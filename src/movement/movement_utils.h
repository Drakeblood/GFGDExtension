#ifndef MOVEMENT_UTILS_H
#define MOVEMENT_UTILS_H

#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/physics_point_query_parameters3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/physics_test_motion_parameters3d.hpp>
#include <godot_cpp/classes/physics_test_motion_result3d.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "movement/movement_types.h"

using namespace godot;

namespace GFGD
{
// The collision solver.
//
// Every function here is stateless and takes the transform to work from, rather
// than reading one off a node. That is not a style preference: it is the only
// shape in which the same code can be run twice on one frame from two different
// starting states, which is what a resimulation is. A function that read the
// body's current position would silently make prediction impossible, and would
// do it without ever failing a test.
//
// This is also why move_and_slide() is not used anywhere below. It reads
// velocity off the body, derives its step from the engine's own physics delta,
// and writes the result back to the node - three separate reasons it cannot be
// asked to simulate a hypothetical.
namespace MovementUtils
{
// Godot works in metres where Unreal works in centimetres, so every constant
// lifted from CharacterMovementComponent is its value divided by 100.

// How close to the floor the capsule is kept. Below the minimum it is pushed
// out, above the maximum it is pulled down, and in between it is left alone -
// the band is what stops the adjustment from fighting itself every tick.
constexpr float MIN_FLOOR_DIST = 0.019f;
constexpr float MAX_FLOOR_DIST = 0.024f;

// A hit this close to the capsule's edge is treated as a wall graze rather than
// a floor, because the normal there belongs to the curve of the capsule and not
// to the surface underneath it.
constexpr float SWEEP_EDGE_REJECT_DISTANCE = 0.0015f;

// Below this the character is considered stopped, so braking does not leave a
// permanent millimetre-per-second drift.
constexpr float BRAKE_TO_STOP_VELOCITY = 0.1f;

// Braking is integrated in steps no longer than this, so the deceleration a
// player feels does not change with the frame rate.
constexpr float BRAKING_SUB_STEP_TIME = 1.0f / 33.0f;

// Everything the solver needs to know about the thing being moved. Held by
// value so no function below has a reason to reach for a node.
//
// The two query objects are carried here rather than allocated per sweep, and
// rather than kept in a static: a character does several sweeps per tick, and a
// static Ref in a GDExtension is destroyed after Godot has already torn down the
// class it points at. Owning them from the component sidesteps both.
struct Body
{
	RID rid;
	RID space;
	uint32_t collision_mask;
	float margin;

	Ref<PhysicsTestMotionParameters3D> params;
	Ref<PhysicsTestMotionResult3D> result;

	// The perch test needs a shape narrower than the body's own, and
	// body_test_motion can only ever sweep the real one. These go through the
	// space state instead, with a capsule this owns and resizes.
	Ref<CapsuleShape3D> probe_shape;
	Ref<PhysicsShapeQueryParameters3D> probe_params;

	// Asking which area is at a point - water, so far.
	Ref<PhysicsPointQueryParameters3D> point_params;

	Body()
		: collision_mask(0xFFFFFFFF)
		, margin(0.001f)
	{
	}

	bool is_valid() const { return rid.is_valid() && space.is_valid() && params.is_valid() && result.is_valid(); }

	bool can_probe() const { return is_valid() && probe_shape.is_valid() && probe_params.is_valid(); }
};

struct SweepResult
{
	// Where the body ended up, including any depenetration the physics server
	// performed on the way.
	Transform3D transform;

	Vector3 remaining;
	bool collided;
	Vector3 normal;
	Vector3 point;
	Object* collider;
	Vector3 collider_velocity;

	SweepResult()
		: collided(false)
		, collider(nullptr)
	{
	}
};

// The knobs the ground solver needs, gathered so the functions below do not each
// take nine arguments.
struct GroundMoveSettings
{
	float max_slope_cos;
	int max_slides;
	float max_step_height;
	float capsule_radius;
	float capsule_half_height;

	// How far in from the capsule's edge the ground still has to be for the
	// character to count as standing. Zero disables perching entirely, which is
	// also Unreal's default.
	float perch_radius_threshold;

	// Extra height above a walkable floor that a perched character is allowed to
	// hang at, on top of max_step_height.
	float perch_additional_height;

	// When false the character stops at a ledge instead of stepping off it.
	bool can_walk_off_ledges;

	GroundMoveSettings()
		: max_slope_cos(0.7071f)
		, max_slides(4)
		, max_step_height(0.45f)
		, capsule_radius(0.5f)
		, capsule_half_height(1.0f)
		, perch_radius_threshold(0.0f)
		, perch_additional_height(0.4f)
		, can_walk_off_ledges(true)
	{
	}
};

struct SlideResult
{
	Transform3D transform;

	// Reduced by each surface it was slid along, the way move_and_slide would
	// have done it.
	Vector3 velocity;

	bool hit_wall;
	bool hit_walkable;
	Vector3 walkable_normal;
	Object* walkable_collider;

	// Set when move_along_floor climbed something. The caller wants to know
	// because a step up moves the character vertically without any vertical
	// velocity having caused it.
	bool stepped_up;

	SlideResult()
		: hit_wall(false)
		, hit_walkable(false)
		, walkable_collider(nullptr)
		, stepped_up(false)
	{
	}
};

// One shape cast. This is the primitive everything else is built from, and the
// reason the whole solver can run from an arbitrary transform:
// PhysicsServer3D::body_test_motion sweeps from a Transform3D that is handed to
// it, without moving the body it borrows the shape from.
SweepResult sweep(const Body& body, const Transform3D& from, const Vector3& motion);

// Collide and slide. max_slides bounds the work in a corner; running out of
// iterations stops the move rather than letting it tunnel.
SlideResult slide_move(const Body& body, const Transform3D& from, const Vector3& motion, const Vector3& velocity, const Vector3& up, float max_slope_cos, int max_slides);

// Tries to climb the thing that just blocked a ground move: up by the step
// height, forward, then back down, keeping the result only if it lands somewhere
// that could have been walked to.
//
// Doing it as three sweeps rather than one is what tells a step apart from a
// wall, and the step *down* is what makes it honest - without it a character
// climbs anything by teleporting to the top of it. Returns false and leaves
// out_transform untouched when the climb is rejected, which is the common case.
bool step_up(const Body& body, const Transform3D& from, const Vector3& delta, const Vector3& hit_point, const Vector3& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings, Transform3D& out_transform);

// The walking move: slide along the floor, and when something blocks it that is
// too steep to walk on, try to step over it before giving up and sliding.
SlideResult move_along_floor(const Body& body, const Transform3D& from, const Vector3& delta, const Vector3& velocity, const Vector3& up, const Ref<FloorResult>& current_floor, const GroundMoveSettings& settings);

// The footprint the perch test stands on: the capsule's radius pulled in by
// perch_radius_threshold, never below a floor of 11 centimetres.
float get_valid_perch_radius(const GroundMoveSettings& settings);

// Whether a perch test is worth running at all. Cheap rejections first: no
// threshold means perching is off, and a contact already inside the perch radius
// is not an edge case in the first place.
bool should_compute_perch_result(const GroundMoveSettings& settings, const Vector3& capsule_center, const Vector3& impact_point, const Vector3& up, bool check_radius);

// find_floor with a shape narrower than the body's own.
//
// This exists because body_test_motion sweeps the body's real collision shape
// and nothing else, so the one question perching asks - "is there ground under
// the inner part of my feet?" - cannot be put to it. The narrow capsule goes
// through PhysicsDirectSpaceState3D instead.
void compute_floor_dist_with_radius(const Body& body, const Transform3D& from, const Vector3& up, float test_radius, float capsule_half_height, float line_distance, float sweep_distance, float max_slope_cos, const Ref<FloorResult>& out_result);

// Whether enough of the character's inner footprint is over solid ground for it
// to keep standing, when a full-width floor test has already said no.
bool compute_perch_result(const Body& body, const Transform3D& from, const Vector3& up, const Ref<FloorResult>& blocking_floor, float max_floor_dist, const GroundMoveSettings& settings, const Ref<FloorResult>& out_result);

// The first Area3D overlapping this point, or null. Bodies are ignored - the
// question is only ever "what volume am I in".
//
// A query rather than a signal on purpose: an Area3D's entered and exited
// signals are state accumulated outside the simulation, and a replayed tick
// would see whatever the last one happened to leave behind rather than what was
// true at that tick.
Object* find_area_at(const Body& body, const Vector3& point);

// Whether a capsule of this size would fit here without overlapping anything.
//
// Used to decide whether a crouching character may stand up. It builds its own
// shape rather than borrowing the body's, because the body's is still the
// crouched one at the moment the question is asked.
bool would_capsule_fit(const Body& body, const Transform3D& at, float radius, float half_height);

// The normal of whatever surface is at this point, read with a short ray.
//
// A shape sweep that lands on the lip of a step reports a normal blended between
// the two faces meeting there, and that blend reads as unwalkable even when the
// surface under the contact is flat. Unreal keeps a separate ImpactNormal for
// exactly this; Godot does not, so the surface has to be asked directly.
// Returns a zero vector when nothing is there.
Vector3 surface_normal_at(const Body& body, const Vector3& point, const Vector3& up, float probe_distance);

// Whether a surface with this normal may be stood on.
bool is_hit_surface_walkable(const Vector3& normal, const Vector3& up, float max_slope_cos);

// Whether an impact point is far enough from the capsule's vertical axis to be
// the surface below rather than the curve of the capsule catching a wall.
bool is_within_edge_tolerance(const Vector3& capsule_center, const Vector3& impact_point, float capsule_radius, const Vector3& up);

// Is there ground under this transform, and may it be stood on.
//
// Two phases, following CharacterMovementComponent::ComputeFloorDist: a shape
// sweep downward first, and a ray as the fallback for when the sweep caught
// something that is not a floor. Without the second phase a character standing
// where a wall meets the ground reports no floor and starts falling.
// The half-height in settings is what lets the ray, which starts at the body
// origin, be measured on the same scale as the sweep, which starts at the
// capsule's feet.
//
// When perching is switched on, this is also where it happens - and it cuts both
// ways, exactly as it does in Unreal. A narrow probe that finds ground lets a
// character keep standing where the full-width test said the surface was
// unwalkable; a narrow probe that finds nothing invalidates the floor and drops
// the character, even when the full-width test was happy. That second half is
// what perch_radius_threshold is really for: it is a restriction on standing
// near an edge, not a licence to hang further off one.
//
// perch_scratch may be null when perching is off; it is only written to.
void find_floor(const Body& body, const Transform3D& from, const Vector3& up, float sweep_distance, float line_distance, const GroundMoveSettings& settings, bool is_moving_on_ground, const Ref<FloorResult>& out_result, const Ref<FloorResult>& perch_scratch);

// Keeps the capsule inside the MIN/MAX_FLOOR_DIST band. Returns the corrected
// transform, or the one given when no correction was needed.
Transform3D adjust_floor_height(const Body& body, const Transform3D& from, const Vector3& up, float capsule_radius, const Ref<FloorResult>& floor);

// The acceleration, friction and braking model. This is
// CharacterMovementComponent::CalcVelocity: friction steers, braking stops, and
// the two are deliberately not the same number.
Vector3 compute_velocity(const Vector3& in_velocity, const Vector3& acceleration, float friction, float braking_deceleration, float braking_friction_factor, float max_speed, double delta);

// Applied when there is no input, or when the character is over its speed cap.
Vector3 apply_velocity_braking(const Vector3& in_velocity, float friction, float braking_deceleration, float braking_friction_factor, double delta);

// Bends a horizontal move to run along a slope instead of into or off it.
// Without it, walking uphill drives the capsule into the ramp and walking
// downhill launches it off the edge every tick.
Vector3 compute_ground_movement_delta(const Vector3& delta, const Vector3& floor_normal, const Vector3& up);

// The slide direction along one surface.
Vector3 compute_slide_vector(const Vector3& delta, float time, const Vector3& normal);

// The direction left when two surfaces both block the move - an inside corner.
// Without this a character in a corner jitters between the two walls.
// new_normal is the surface just hit, old_normal the one already slid along.
Vector3 two_wall_adjust(const Vector3& delta, const Vector3& new_normal, const Vector3& old_normal);
}
}

#endif
