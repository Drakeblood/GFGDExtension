#include "framework/input_component.h"
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/player_controller.h"
#include "framework/player_input.h"

using namespace godot;

namespace GFGD
{
InputComponent::InputComponent()
{
	enabled = true;
	owning_controller = nullptr;
}

InputComponent::~InputComponent()
{

}

void InputComponent::bind_action(const StringName& action_name, TriggerEvent trigger_event, const Callable& action)
{
	if (!action.is_valid()) { return; }

	for (int i = 0; i < bindings.size(); i++)
	{
		Dictionary binding = bindings[i];
		if (binding["action_name"] == Variant(action_name) && (int)binding["event"] == (int)trigger_event && binding["action"] == Variant(action))
		{
			return;
		}
	}

	Dictionary new_binding;
	new_binding["action_name"] = action_name;
	new_binding["event"] = (int)trigger_event;
	new_binding["action"] = action;
	bindings.append(new_binding);
}

void InputComponent::remove_binding(const StringName& action_name, TriggerEvent trigger_event, const Callable& action)
{
	for (int i = bindings.size() - 1; i >= 0; i--)
	{
		Dictionary binding = bindings[i];
		if (binding["action_name"] == Variant(action_name) && (int)binding["event"] == (int)trigger_event && binding["action"] == Variant(action))
		{
			bindings.remove_at(i);
			return;
		}
	}
}

void InputComponent::remove_all_bindings()
{
	bindings.clear();
}

PlayerInput* InputComponent::get_player_input() const
{
	// Resolved on every query rather than cached: the PlayerController builds its
	// InputComponent in its constructor, long before the game mode hands it a
	// LocalPlayer, so there is no single point at which a cache could be filled.
	if (owning_controller == nullptr) { return nullptr; }

	return owning_controller->get_player_input();
}

void InputComponent::process_input(double delta)
{
	if (!enabled) { return; }

	PlayerInput* player_input = get_player_input();
	Input* input = Input::get_singleton();

	for (int i = 0; i < bindings.size(); i++)
	{
		Dictionary binding = bindings[i];
		StringName action_name = binding["action_name"];
		int trigger_event = binding["event"];
		Callable action = binding["action"];
		if (!action.is_valid()) { continue; }

		// Without a PlayerInput this falls back to the global Input singleton,
		// which is what this class did before per-player input existed.
		switch (trigger_event)
		{
			case STARTED:
				if (player_input != nullptr ? player_input->is_action_just_pressed(action_name) : input->is_action_just_pressed(action_name)) { action.call(); }
				break;
			case COMPLETED:
				if (player_input != nullptr ? player_input->is_action_just_released(action_name) : input->is_action_just_released(action_name)) { action.call(); }
				break;
			case TRIGGERED:
			default:
				if (player_input != nullptr ? player_input->is_action_pressed(action_name) : input->is_action_pressed(action_name)) { action.call(); }
				break;
		}
	}
}

bool InputComponent::is_action_pressed(const StringName& action_name) const
{
	if (!enabled) { return false; }

	PlayerInput* player_input = get_player_input();
	return player_input != nullptr ? player_input->is_action_pressed(action_name) : Input::get_singleton()->is_action_pressed(action_name);
}

bool InputComponent::is_action_just_pressed(const StringName& action_name) const
{
	if (!enabled) { return false; }

	PlayerInput* player_input = get_player_input();
	return player_input != nullptr ? player_input->is_action_just_pressed(action_name) : Input::get_singleton()->is_action_just_pressed(action_name);
}

bool InputComponent::is_action_just_released(const StringName& action_name) const
{
	if (!enabled) { return false; }

	PlayerInput* player_input = get_player_input();
	return player_input != nullptr ? player_input->is_action_just_released(action_name) : Input::get_singleton()->is_action_just_released(action_name);
}

float InputComponent::get_action_strength(const StringName& action_name) const
{
	if (!enabled) { return 0.0f; }

	PlayerInput* player_input = get_player_input();
	return player_input != nullptr ? player_input->get_action_strength(action_name) : Input::get_singleton()->get_action_strength(action_name);
}

float InputComponent::get_axis(const StringName& negative_action, const StringName& positive_action) const
{
	if (!enabled) { return 0.0f; }

	PlayerInput* player_input = get_player_input();
	return player_input != nullptr ? player_input->get_axis(negative_action, positive_action) : Input::get_singleton()->get_axis(negative_action, positive_action);
}

Vector2 InputComponent::get_vector(const StringName& negative_x, const StringName& positive_x, const StringName& negative_y, const StringName& positive_y, float deadzone) const
{
	if (!enabled) { return Vector2(); }

	PlayerInput* player_input = get_player_input();
	return player_input != nullptr
			? player_input->get_vector(negative_x, positive_x, negative_y, positive_y, deadzone)
			: Input::get_singleton()->get_vector(negative_x, positive_x, negative_y, positive_y, deadzone);
}

void InputComponent::_bind_methods()
{
	BIND_ENUM_CONSTANT(TRIGGERED);
	BIND_ENUM_CONSTANT(STARTED);
	BIND_ENUM_CONSTANT(COMPLETED);

	ClassDB::bind_method(D_METHOD("bind_action", "action_name", "trigger_event", "action"), &InputComponent::bind_action);
	ClassDB::bind_method(D_METHOD("remove_binding", "action_name", "trigger_event", "action"), &InputComponent::remove_binding);
	ClassDB::bind_method(D_METHOD("remove_all_bindings"), &InputComponent::remove_all_bindings);

	ClassDB::bind_method(D_METHOD("is_action_pressed", "action_name"), &InputComponent::is_action_pressed);
	ClassDB::bind_method(D_METHOD("is_action_just_pressed", "action_name"), &InputComponent::is_action_just_pressed);
	ClassDB::bind_method(D_METHOD("is_action_just_released", "action_name"), &InputComponent::is_action_just_released);
	ClassDB::bind_method(D_METHOD("get_action_strength", "action_name"), &InputComponent::get_action_strength);
	ClassDB::bind_method(D_METHOD("get_axis", "negative_action", "positive_action"), &InputComponent::get_axis);
	ClassDB::bind_method(D_METHOD("get_vector", "negative_x", "positive_x", "negative_y", "positive_y", "deadzone"), &InputComponent::get_vector, DEFVAL(-1.0f));

	ClassDB::bind_method(D_METHOD("get_owning_controller"), &InputComponent::get_owning_controller);
	ClassDB::bind_method(D_METHOD("set_owning_controller", "value"), &InputComponent::set_owning_controller);
	ClassDB::bind_method(D_METHOD("get_player_input"), &InputComponent::get_player_input);

	ClassDB::bind_method(D_METHOD("get_enabled"), &InputComponent::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &InputComponent::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
}
}
