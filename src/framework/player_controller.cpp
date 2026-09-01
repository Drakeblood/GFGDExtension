#include "framework/player_controller.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>

#include "framework/pawn_handler.h"
#include "framework/input_component.h"
#include "framework/local_player.h"
#include "framework/player_input.h"
#include "framework/player_state.h"

using namespace godot;

namespace GFGD
{
PlayerController::PlayerController()
{
	input_component = nullptr;
	scope_lock = false;
	local_player = nullptr;
	player_state = nullptr;

	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// Built here rather than in _enter_tree: the game mode spawns and possesses
	// the player while the level is still outside the scene tree, and possession
	// needs the input component to exist. Waiting for _enter_tree would silently
	// skip PawnHandler::_setup_input_component.
	input_component = memnew(InputComponent);
	input_component->set_name("InputComponent");
	add_child(input_component);
	register_input_component(input_component);
}

PlayerController::~PlayerController()
{

}

void PlayerController::_enter_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// The base class registers this controller with the world. godot-cpp only
	// binds the most derived override, so skipping this call would quietly leave
	// every PlayerController out of GFGDSceneTree's lists.
	Controller::_enter_tree();

	set_process(true);
}

void PlayerController::_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	scope_lock = true;
	for (int i = current_input_stack.size() - 1; i >= 0; i--)
	{
		InputComponent* input = Object::cast_to<InputComponent>(current_input_stack[i].get_validated_object());
		if (input == nullptr || !input->get_enabled()) { continue; }

		input->process_input(delta);
	}
	scope_lock = false;

	if (pending_remove_input_component.size() > 0)
	{
		for (int i = 0; i < pending_remove_input_component.size(); i++)
		{
			InputComponent* input = Object::cast_to<InputComponent>(pending_remove_input_component[i].get_validated_object());
			unregister_input_component(input);
		}
		pending_remove_input_component.clear();
	}
}

void PlayerController::set_local_player(LocalPlayer* value)
{
	if (local_player == value) { return; }

	if (local_player != nullptr && local_player->get_player_controller() == this)
	{
		local_player->set_player_controller(nullptr);
	}

	local_player = value;

	if (local_player != nullptr)
	{
		local_player->set_player_controller(this);
	}

	emit_signal("local_player_changed", local_player);
}

PlayerInput* PlayerController::get_player_input() const
{
	return local_player != nullptr ? local_player->get_player_input() : nullptr;
}

Viewport* PlayerController::get_player_viewport() const
{
	if (local_player != nullptr && local_player->get_viewport_override() != nullptr)
	{
		return local_player->get_viewport_override();
	}

	return is_inside_tree() ? get_viewport() : nullptr;
}

int PlayerController::get_player_index() const
{
	return local_player != nullptr ? local_player->get_player_index() : -1;
}

void PlayerController::on_possess(PawnHandler* pawn_handler)
{
	set_pawn_camera_node_as_current();

	if (input_component != nullptr)
	{
		pawn_handler->setup_input_component(input_component);
	}
}

void PlayerController::on_unpossess()
{
	if (input_component != nullptr)
	{
		input_component->remove_all_bindings();
	}
}

void PlayerController::set_pawn_camera_node_as_current()
{
	PawnHandler* pawn_handler = get_pawn_handler();
	if (pawn_handler == nullptr) { return; }

	Node* pawn_root = pawn_handler->get_pawn_root();
	if (pawn_root == nullptr)
	{
		pawn_root = pawn_handler;
	}

	Node* camera_node = nullptr;
	NodePath camera_path = pawn_handler->get_camera_path();
	if (!camera_path.is_empty())
	{
		camera_node = pawn_handler->get_node_or_null(camera_path);
	}

	if (camera_node == nullptr)
	{
		TypedArray<Node> cameras = pawn_root->find_children("*", "Camera3D", true, false);
		if (cameras.size() == 0)
		{
			cameras = pawn_root->find_children("*", "Camera2D", true, false);
		}
		if (cameras.size() > 0)
		{
			camera_node = Object::cast_to<Node>(cameras[0]);
		}
	}

	if (camera_node == nullptr)
	{
		WARN_PRINT("GFGD: No camera found on the possessed pawn (set camera_path on the PawnHandler or add a Camera2D/Camera3D).");
		return;
	}

	Viewport* target_viewport = local_player != nullptr ? local_player->get_viewport_override() : nullptr;

	if (Camera2D* camera2d = Object::cast_to<Camera2D>(camera_node))
	{
		// A Camera2D can render into any viewport without being parented to it.
		if (target_viewport != nullptr)
		{
			camera2d->set_custom_viewport(target_viewport);
		}
		camera2d->make_current();
		return;
	}

	if (Camera3D* camera3d = Object::cast_to<Camera3D>(camera_node))
	{
		// Camera3D has no custom viewport: make_current() acts on its nearest
		// ancestor Viewport, so split screen needs the camera to physically live
		// inside the SubViewport. The framework does not move it - reparenting
		// would break a camera rigged to follow the pawn - it says so instead.
		if (target_viewport != nullptr && camera3d->is_inside_tree() && camera3d->get_viewport() != target_viewport)
		{
			WARN_PRINT(vformat("GFGD: Camera3D \"%s\" is not inside the viewport of local player %d, so make_current() applies to the wrong viewport. Call LocalPlayer.adopt_camera() or place the camera under the SubViewport yourself.", camera3d->get_name(), get_player_index()));
		}
		camera3d->make_current();
	}
}

void PlayerController::register_input_component(InputComponent* input)
{
	if (input == nullptr) { return; }
	if (current_input_stack.has(input)) { return; }

	input->set_owning_controller(this);
	current_input_stack.append(input);
}

void PlayerController::unregister_input_component(InputComponent* input)
{
	if (input == nullptr) { return; }

	if (scope_lock)
	{
		pending_remove_input_component.append(input);
	}
	else
	{
		current_input_stack.erase(input);
		if (input->get_owning_controller() == this)
		{
			input->set_owning_controller(nullptr);
		}
		input->remove_all_bindings();
	}
}

void PlayerController::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_pawn_camera_node_as_current"), &PlayerController::set_pawn_camera_node_as_current);
	ClassDB::bind_method(D_METHOD("get_input_component"), &PlayerController::get_input_component);
	ClassDB::bind_method(D_METHOD("register_input_component", "input"), &PlayerController::register_input_component);
	ClassDB::bind_method(D_METHOD("unregister_input_component", "input"), &PlayerController::unregister_input_component);

	ClassDB::bind_method(D_METHOD("get_local_player"), &PlayerController::get_local_player);
	ClassDB::bind_method(D_METHOD("set_local_player", "value"), &PlayerController::set_local_player);
	ClassDB::bind_method(D_METHOD("get_player_state"), &PlayerController::get_player_state);
	ClassDB::bind_method(D_METHOD("set_player_state", "value"), &PlayerController::set_player_state);
	ClassDB::bind_method(D_METHOD("get_player_input"), &PlayerController::get_player_input);
	ClassDB::bind_method(D_METHOD("get_player_viewport"), &PlayerController::get_player_viewport);
	ClassDB::bind_method(D_METHOD("get_player_index"), &PlayerController::get_player_index);
	ClassDB::bind_method(D_METHOD("is_local_player_controller"), &PlayerController::is_local_player_controller);

	ADD_SIGNAL(MethodInfo("local_player_changed", PropertyInfo(Variant::OBJECT, "local_player", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer")));
}
}
