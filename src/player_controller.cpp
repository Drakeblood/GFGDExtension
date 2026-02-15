#include "player_controller.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input.hpp>

#include "pawn_handler.h"
#include "input_component.h"

using namespace godot;

namespace GFGD
{
PlayerController::PlayerController()
{
	
}

PlayerController::~PlayerController()
{
	
}

void PlayerController::_enter_tree()
{
	input_component = memnew(InputComponent());
	add_child(input_component);
	register_input_component(input_component);
}

void PlayerController::_process(double delta)
{
	scope_lock = true;
	for (int i = current_input_stack.size() - 1; i >= 0; i--)
	{
		InputComponent* input_component = Object::cast_to<InputComponent>(current_input_stack[i]);
		if (!input_component->get_enabled()) { continue; }

		Array bindings = input_component->bindings;
		for (int j = 0; j < bindings.size(); j++)
		{
			Dictionary binding = bindings[j];
			StringName action_name = binding["action_name"];
			if (Input::get_singleton()->is_action_pressed(action_name))
			{
				Array actions = binding["actions"];
				if (actions.size() > 0 && !actions[0].is_null())
				{
					Callable(actions[0]).call();
				}
			}
		}
	}
	scope_lock = false;

	if (current_input_stack.size() > 0)
	{
		for (int i = 0; i < pending_remove_input_component.size(); i++)
		{
			InputComponent* input = Object::cast_to<InputComponent>(pending_remove_input_component[i]);
			unregister_input_component(input);
		}
		pending_remove_input_component.clear();
	}
}

void PlayerController::_input(const Ref<InputEvent>& event)
{
	scope_lock = true;
	for (int i = current_input_stack.size() - 1; i >= 0; i--)
	{
		InputComponent* input_component = Object::cast_to<InputComponent>(current_input_stack[i]);
		if (!input_component->get_enabled()) { continue; }

		Array bindings = input_component->bindings;
		for (int j = 0; j < bindings.size(); j++)
		{
			Dictionary binding = bindings[j];
			StringName action_name = binding["action_name"];
			
			if (Input::get_singleton()->is_action_just_pressed(action_name))
			{
				binding["pressed"] = true;
				Array actions = binding["actions"];
				if (actions.size() > 1 && !actions[1].is_null())
				{
					Callable(actions[1]).call();
				}
				if (actions.size() > 0 && !actions[0].is_null())
				{
					Callable(actions[0]).call();
				}
			}
			else if (Input::get_singleton()->is_action_just_released(action_name))
			{
				binding["pressed"] = false;
				Array actions = binding["actions"];
				if (actions.size() > 4 && !actions[4].is_null())
				{
					Callable(actions[4]).call();
				}
			}
		}
	}
	scope_lock = false;
}

void PlayerController::on_possess(PawnHandler* pawn_to_possess)
{
	if (pawn_to_possess != nullptr)
	{
		bool b_new_pawn = (get_pawn_handler() != pawn_to_possess);

		if (get_pawn_handler() != nullptr && b_new_pawn)
		{
			unpossess();
		}

		if (pawn_to_possess->get_controller() != nullptr)
		{
			pawn_to_possess->get_controller()->unpossess();
		}

		pawn_to_possess->possessed_by(this);

		set_pawn_handler(pawn_to_possess);
		ERR_FAIL_NULL(get_pawn_handler());

		set_pawn_camera_node_as_current();
	}
}

void PlayerController::on_unpossess()
{
	if (get_pawn_handler() != nullptr)
	{
		get_pawn_handler()->unpossessed();
	}

	set_pawn_handler(nullptr);
}

void PlayerController::set_pawn_camera_node_as_current()
{
	Node* camera_node = get_pawn_handler()->get_parent()->find_child("Camera");
	if (camera_node != nullptr && camera_node->is_inside_tree())
	{
		if (Camera3D* camera3d = Object::cast_to<Camera3D>(camera_node))
		{
			camera3d->make_current();
		}
		else if (Camera2D* camera2d = Object::cast_to<Camera2D>(camera_node))
		{
			camera2d->make_current();
		}
	}
}

void PlayerController::register_input_component(InputComponent* input)
{
	if (!current_input_stack.has(input))
	{
		current_input_stack.append(input);
	}
}

void PlayerController::unregister_input_component(InputComponent* input)
{
	if (scope_lock)
	{
		pending_remove_input_component.append(input);
	}
	else
	{
		current_input_stack.erase(input);
		input->remove_all_bindings();
	}
}

void PlayerController::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("_enter_tree"), &PlayerController::_enter_tree);
	ClassDB::bind_method(D_METHOD("_process", "delta"), &PlayerController::_process);
	ClassDB::bind_method(D_METHOD("_input", "event"), &PlayerController::_input);
	ClassDB::bind_method(D_METHOD("on_possess", "pawn_to_possess"), &PlayerController::on_possess);
	ClassDB::bind_method(D_METHOD("on_unpossess"), &PlayerController::on_unpossess);
	ClassDB::bind_method(D_METHOD("set_pawn_camera_node_as_current"), &PlayerController::set_pawn_camera_node_as_current);
	ClassDB::bind_method(D_METHOD("register_input_component", "input"), &PlayerController::register_input_component);
	ClassDB::bind_method(D_METHOD("unregister_input_component", "input"), &PlayerController::unregister_input_component);
}
}