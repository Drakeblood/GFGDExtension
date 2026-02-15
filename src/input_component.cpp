#include "input_component.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
InputComponent::InputComponent()
{
	enabled = true;
}

InputComponent::~InputComponent()
{
	
}

void InputComponent::bind_action(const StringName& action_name, int trigger_event, const Callable& action)
{
	bool found = false;
	for (int i = 0; i < bindings.size(); i++)
	{
		Dictionary binding = bindings[i];
		if (binding["action_name"] == action_name)
		{
			Array actions = binding["actions"];
			if (actions.size() <= trigger_event)
			{
				actions.resize(trigger_event + 1);
			}
			actions[trigger_event] = Callable(actions[trigger_event]).bind_front(action);
			binding["actions"] = actions;
			found = true;
			break;
		}
	}

	if (!found)
	{
		Dictionary new_binding;
		new_binding["action_name"] = action_name;
		Array actions;
		actions.resize(5); // TriggerEventNum
		actions[trigger_event] = action;
		new_binding["actions"] = actions;
		bindings.append(new_binding);
	}
}

void InputComponent::remove_binding(const StringName& action_name, int trigger_event, const Callable& action)
{
	for (int i = 0; i < bindings.size(); i++)
	{
		Dictionary binding = bindings[i];
		if (binding["action_name"] == action_name)
		{
			Array actions = binding["actions"];
			if (actions.size() > trigger_event)
			{
				actions[trigger_event] = Callable(actions[trigger_event]).unbind(action);
			}
			break;
		}
	}
}

void InputComponent::remove_binding(const StringName& action_name)
{
	for (int i = bindings.size() - 1; i >= 0; i--)
	{
		Dictionary binding = bindings[i];
		if (binding["action_name"] == action_name)
		{
			bindings.remove_at(i);
			break;
		}
	}
}

void InputComponent::remove_all_bindings()
{
	for (int i = bindings.size() - 1; i >= 0; i--)
	{
		Dictionary binding = bindings[i];
		Array actions = binding["actions"];
		for (int j = 0; j < actions.size(); j++)
		{
			actions[j] = Callable();
		}
		binding["actions"] = actions;
	}
	bindings.clear();
}

bool InputComponent::is_action_pressed(const StringName& action_name) const
{
	if (!enabled) return false;

	for (int i = 0; i < bindings.size(); i++)
	{
		Dictionary binding = bindings[i];
		if (binding["pressed"] && binding["action_name"] == action_name) 
		{
			return true; 
		}
	}
	return false;
}

void InputComponent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("bind_action", "action_name", "trigger_event", "action"), &InputComponent::bind_action);
	ClassDB::bind_method(D_METHOD("remove_binding", "action_name", "trigger_event", "action"), &InputComponent::remove_binding);
	ClassDB::bind_method(D_METHOD("remove_binding", "action_name"), &InputComponent::remove_binding);
	ClassDB::bind_method(D_METHOD("remove_all_bindings"), &InputComponent::remove_all_bindings);
	ClassDB::bind_method(D_METHOD("is_action_pressed", "action_name"), &InputComponent::is_action_pressed);

	ClassDB::bind_method(D_METHOD("get_enabled"), &InputComponent::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &InputComponent::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
}
}