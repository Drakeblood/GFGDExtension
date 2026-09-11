#include "movement/movement_backend.h"

#include <godot_cpp/core/class_db.hpp>

#include "movement/character_movement_component.h"
#include "movement/character_movement_component_2d.h"

using namespace godot;
using namespace GFGD;

// --- MovementBackend --------------------------------------------------------

MovementBackend::MovementBackend()
{
}

MovementBackend::~MovementBackend()
{
}

void MovementBackend::tick(CharacterMovementComponent* component, double delta)
{
	if (component == nullptr)
	{
		return;
	}

	component->simulate(delta);
}

void MovementBackend::tick_2d(CharacterMovementComponent2D* component, double delta)
{
	if (component == nullptr)
	{
		return;
	}

	component->simulate(delta);
}

void MovementBackend::on_rollback(const Ref<MovementState>& new_state, int64_t new_frame)
{
}

void MovementBackend::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_sim_frame"), &MovementBackend::get_sim_frame);
	ClassDB::bind_method(D_METHOD("get_sim_time_ms"), &MovementBackend::get_sim_time_ms);
	ClassDB::bind_method(D_METHOD("is_fixed_dt"), &MovementBackend::is_fixed_dt);
	ClassDB::bind_method(D_METHOD("should_resim"), &MovementBackend::should_resim);
}

// --- StandaloneMovementBackend ----------------------------------------------

StandaloneMovementBackend::StandaloneMovementBackend()
	: sim_frame(0)
	, sim_time_ms(0.0)
{
}

StandaloneMovementBackend::~StandaloneMovementBackend()
{
}

void StandaloneMovementBackend::tick(CharacterMovementComponent* component, double delta)
{
	if (component == nullptr || delta <= 0.0)
	{
		return;
	}

	sim_frame++;
	sim_time_ms += delta * 1000.0;

	component->simulate(delta);
}

void StandaloneMovementBackend::tick_2d(CharacterMovementComponent2D* component, double delta)
{
	if (component == nullptr || delta <= 0.0)
	{
		return;
	}

	sim_frame++;
	sim_time_ms += delta * 1000.0;

	component->simulate(delta);
}

void StandaloneMovementBackend::_bind_methods()
{
}
