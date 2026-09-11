#include "movement/layered_move.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "movement/character_movement_component.h"

using namespace godot;
using namespace GFGD;

// --- LayeredMove ------------------------------------------------------------

LayeredMove::LayeredMove()
	: mix_mode(ProposedMove::OVERRIDE_VELOCITY)
	, priority(0)
	, duration(0.0f)
	, finish_velocity_mode(KEEP_VELOCITY)
	, finish_velocity(Vector3())
	, finish_clamp_speed(0.0f)
	, start_time_ms(0.0)
	, started(false)
{
}

LayeredMove::~LayeredMove()
{
}

void LayeredMove::on_start(const Ref<MovementTickParams>& params)
{
	GDVIRTUAL_CALL(_on_start, params);
}

void LayeredMove::generate_move(const Ref<MovementTickParams>& params, const Ref<ProposedMove>& out_proposal)
{
	GDVIRTUAL_CALL(_generate_move, params, out_proposal);
}

bool LayeredMove::is_finished(double sim_time_ms)
{
	bool script_result = false;
	if (GDVIRTUAL_CALL(_is_finished, sim_time_ms, script_result))
	{
		return script_result;
	}

	// Negative runs until something cancels it; zero is a single tick, which has
	// happened by the time this is asked.
	if (duration < 0.0f)
	{
		return false;
	}

	return get_elapsed(sim_time_ms) >= duration;
}

Vector3 LayeredMove::apply_finish_velocity(const Vector3& velocity) const
{
	switch (finish_velocity_mode)
	{
		case SET_VELOCITY:
			return finish_velocity;

		case CLAMP_VELOCITY:
			return velocity.limit_length(MAX(0.0f, finish_clamp_speed));

		case KEEP_VELOCITY:
		default:
			return velocity;
	}
}

void LayeredMove::_bind_methods()
{
	BIND_ENUM_CONSTANT(KEEP_VELOCITY);
	BIND_ENUM_CONSTANT(SET_VELOCITY);
	BIND_ENUM_CONSTANT(CLAMP_VELOCITY);

	ClassDB::bind_method(D_METHOD("get_mix_mode"), &LayeredMove::get_mix_mode);
	ClassDB::bind_method(D_METHOD("set_mix_mode", "value"), &LayeredMove::set_mix_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mix_mode", PROPERTY_HINT_ENUM, "Additive Velocity,Override Velocity,Override All,Override All Except Vertical"), "set_mix_mode", "get_mix_mode");

	ClassDB::bind_method(D_METHOD("get_priority"), &LayeredMove::get_priority);
	ClassDB::bind_method(D_METHOD("set_priority", "value"), &LayeredMove::set_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");

	ClassDB::bind_method(D_METHOD("get_duration"), &LayeredMove::get_duration);
	ClassDB::bind_method(D_METHOD("set_duration", "value"), &LayeredMove::set_duration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "-1,60,0.01,or_greater,suffix:s"), "set_duration", "get_duration");

	ClassDB::bind_method(D_METHOD("get_finish_velocity_mode"), &LayeredMove::get_finish_velocity_mode);
	ClassDB::bind_method(D_METHOD("set_finish_velocity_mode", "value"), &LayeredMove::set_finish_velocity_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "finish_velocity_mode", PROPERTY_HINT_ENUM, "Keep,Set,Clamp"), "set_finish_velocity_mode", "get_finish_velocity_mode");

	ClassDB::bind_method(D_METHOD("get_finish_velocity"), &LayeredMove::get_finish_velocity);
	ClassDB::bind_method(D_METHOD("set_finish_velocity", "value"), &LayeredMove::set_finish_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "finish_velocity"), "set_finish_velocity", "get_finish_velocity");

	ClassDB::bind_method(D_METHOD("get_finish_clamp_speed"), &LayeredMove::get_finish_clamp_speed);
	ClassDB::bind_method(D_METHOD("set_finish_clamp_speed", "value"), &LayeredMove::set_finish_clamp_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "finish_clamp_speed", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m/s"), "set_finish_clamp_speed", "get_finish_clamp_speed");

	ClassDB::bind_method(D_METHOD("get_start_time_ms"), &LayeredMove::get_start_time_ms);
	ClassDB::bind_method(D_METHOD("get_elapsed", "sim_time_ms"), &LayeredMove::get_elapsed);
	ClassDB::bind_method(D_METHOD("is_finished", "sim_time_ms"), &LayeredMove::is_finished);

	GDVIRTUAL_BIND(_on_start, "params");
	GDVIRTUAL_BIND(_generate_move, "params", "out_proposal");
	GDVIRTUAL_BIND(_is_finished, "sim_time_ms");
}

// --- LinearVelocityLayeredMove ----------------------------------------------

LinearVelocityLayeredMove::LinearVelocityLayeredMove()
	: velocity(Vector3())
	, decay_over_duration(false)
{
}

LinearVelocityLayeredMove::~LinearVelocityLayeredMove()
{
}

void LinearVelocityLayeredMove::generate_move(const Ref<MovementTickParams>& params, const Ref<ProposedMove>& out_proposal)
{
	ERR_FAIL_COND(params.is_null() || out_proposal.is_null());

	Vector3 contribution = velocity;

	if (decay_over_duration && get_duration() > 0.0f)
	{
		const float elapsed = get_elapsed(params->get_sim_time_ms());
		contribution *= CLAMP(1.0f - (elapsed / get_duration()), 0.0f, 1.0f);
	}

	out_proposal->set_linear_velocity(contribution);
	out_proposal->set_mix_mode(get_mix_mode());
}

void LinearVelocityLayeredMove::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_velocity"), &LinearVelocityLayeredMove::get_velocity);
	ClassDB::bind_method(D_METHOD("set_velocity", "value"), &LinearVelocityLayeredMove::set_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity"), "set_velocity", "get_velocity");

	ClassDB::bind_method(D_METHOD("get_decay_over_duration"), &LinearVelocityLayeredMove::get_decay_over_duration);
	ClassDB::bind_method(D_METHOD("set_decay_over_duration", "value"), &LinearVelocityLayeredMove::set_decay_over_duration);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "decay_over_duration"), "set_decay_over_duration", "get_decay_over_duration");
}
