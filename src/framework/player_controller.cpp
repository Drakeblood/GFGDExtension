#include "framework/player_controller.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "framework/input_component.h"
#include "framework/local_player.h"
#include "framework/pawn.h"
#include "framework/player_input.h"
#include "framework/player_state.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
PlayerController::PlayerController()
{
	input_component = nullptr;
	scope_lock = false;
	local_player = nullptr;
	remote_player_input = nullptr;
	owner_peer_id = World::SERVER_PEER_ID;

	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// Built here rather than in _enter_tree: the game mode spawns and possesses
	// the player as part of logging them in, and possession needs the input
	// component to exist. Waiting for _enter_tree would silently skip
	// Pawn::_setup_input_component.
	input_component = memnew(InputComponent);
	input_component->set_name("InputComponent");
	add_child(input_component);
	register_input_component(input_component);
}

PlayerController::~PlayerController()
{
	if (remote_player_input != nullptr)
	{
		memdelete(remote_player_input);
		remote_player_input = nullptr;
	}
}

void PlayerController::_enter_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// The base class registers this controller with the world. godot-cpp only
	// binds the most derived override, so skipping this call would quietly leave
	// every PlayerController out of the world's lists.
	Controller::_enter_tree();

	configure_rpcs();
	set_process(true);
}

void PlayerController::configure_rpcs()
{
	Dictionary config;
	config["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	// Input is a stream of states, not a list of events: a lost packet is
	// corrected by the next one, and holding it up would only add lag.
	config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED;
	config["call_local"] = false;
	config["channel"] = 0;

	rpc_config("server_receive_input", config);
}

void PlayerController::_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	if (has_authority())
	{
		// Where the pawn is authoritative, the bindings fire - from this machine's
		// devices for a local player, and from what a remote player sent for
		// everyone else. Both arrive as the same action state.
		pump_input(delta);
		return;
	}

	if (is_local_player_controller())
	{
		send_input_to_server();
	}
}

void PlayerController::pump_input(double delta)
{
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

const PackedStringArray& PlayerController::get_replicated_action_list()
{
	if (!replicated_actions.is_empty())
	{
		return replicated_actions;
	}

	if (replicated_action_cache.is_empty())
	{
		TypedArray<StringName> actions = InputMap::get_singleton()->get_actions();
		for (int i = 0; i < actions.size(); i++)
		{
			const String action_name = String(actions[i]);
			// The engine's own menu actions are handled by the interface on the
			// machine they were pressed on, so sending them would be noise.
			if (action_name.begins_with("ui_")) { continue; }

			replicated_action_cache.push_back(action_name);
		}
	}

	return replicated_action_cache;
}

void PlayerController::send_input_to_server()
{
	PlayerInput* player_input = get_player_input();
	if (player_input == nullptr) { return; }

	const PackedStringArray& action_list = get_replicated_action_list();

	PackedStringArray changed_names;
	PackedByteArray changed_pressed;
	PackedFloat32Array changed_strengths;

	for (int i = 0; i < action_list.size(); i++)
	{
		const StringName action_name = StringName(action_list[i]);
		const bool pressed = player_input->is_action_pressed(action_name);
		const float strength = player_input->get_action_strength(action_name);

		const Vector2 state = Vector2(pressed ? 1.0f : 0.0f, strength);
		const Variant previous = sent_action_state.get(action_name, Variant());
		if (previous.get_type() == Variant::VECTOR2 && Vector2(previous).is_equal_approx(state))
		{
			continue;
		}

		sent_action_state[action_name] = state;
		changed_names.push_back(action_list[i]);
		changed_pressed.push_back(pressed ? 1 : 0);
		changed_strengths.push_back(strength);
	}

	if (changed_names.is_empty()) { return; }

	rpc_id(World::SERVER_PEER_ID, "server_receive_input", changed_names, changed_pressed, changed_strengths);
}

void PlayerController::server_receive_input(const PackedStringArray& action_names, const PackedByteArray& pressed, const PackedFloat32Array& strengths)
{
	if (!has_authority()) { return; }

	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender_id = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;

	// Ownership decides who may drive this controller. A call from anyone else is
	// dropped, which is what stops one client from playing another's character.
	if (sender_id != owner_peer_id)
	{
		WARN_PRINT(vformat("GFGD: peer %d tried to send input to a controller owned by peer %d.", sender_id, owner_peer_id));
		return;
	}

	PlayerInput* player_input = get_player_input();
	if (player_input == nullptr) { return; }

	const int count = Math::min(action_names.size(), Math::min(pressed.size(), strengths.size()));
	for (int i = 0; i < count; i++)
	{
		const StringName action_name = StringName(action_names[i]);
		if (pressed[i] != 0)
		{
			player_input->action_press(action_name, strengths[i]);
		}
		else
		{
			player_input->action_release(action_name);
		}
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
	if (local_player != nullptr)
	{
		return local_player->get_player_input();
	}

	if (remote_player_input == nullptr)
	{
		// No devices are assigned to it, so nothing local can write to it and
		// every query answers from what the owning client last sent.
		const_cast<PlayerController*>(this)->remote_player_input = memnew(PlayerInput);
	}

	return remote_player_input;
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

bool PlayerController::is_local_controller() const
{
	World* world = get_world();
	if (world == nullptr) { return true; }

	return owner_peer_id == world->get_local_peer_id();
}

bool PlayerController::is_local_player_controller() const
{
	return local_player != nullptr && is_local_controller();
}

void PlayerController::set_replicated_actions(const PackedStringArray& value)
{
	replicated_actions = value;
	replicated_action_cache.clear();
	sent_action_state.clear();
}

void PlayerController::on_possess(Pawn* pawn)
{
	// Only the machine the player is sitting at has a view to point somewhere.
	if (is_local_player_controller())
	{
		set_pawn_camera_node_as_current();
	}

	if (input_component != nullptr)
	{
		pawn->setup_input_component(input_component);
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
	Pawn* pawn = get_pawn();
	if (pawn == nullptr) { return; }
	if (!pawn->get_auto_manage_camera()) { return; }

	Node* pawn_root = pawn->get_pawn_root();
	if (pawn_root == nullptr)
	{
		pawn_root = pawn;
	}

	Node* camera_node = nullptr;
	NodePath camera_path = pawn->get_camera_path();
	if (!camera_path.is_empty())
	{
		camera_node = pawn->get_node_or_null(camera_path);
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
		WARN_PRINT("GFGD: No camera found on the possessed pawn (set camera_path on the Pawn or add a Camera2D/Camera3D).");
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
	ClassDB::bind_method(D_METHOD("get_player_input"), &PlayerController::get_player_input);
	ClassDB::bind_method(D_METHOD("get_player_viewport"), &PlayerController::get_player_viewport);
	ClassDB::bind_method(D_METHOD("get_player_index"), &PlayerController::get_player_index);
	ClassDB::bind_method(D_METHOD("is_local_player_controller"), &PlayerController::is_local_player_controller);

	ClassDB::bind_method(D_METHOD("set_owner_peer_id", "value"), &PlayerController::set_owner_peer_id);

	ClassDB::bind_method(D_METHOD("get_replicated_actions"), &PlayerController::get_replicated_actions);
	ClassDB::bind_method(D_METHOD("set_replicated_actions", "value"), &PlayerController::set_replicated_actions);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "replicated_actions"), "set_replicated_actions", "get_replicated_actions");

	ClassDB::bind_method(D_METHOD("server_receive_input", "action_names", "pressed", "strengths"), &PlayerController::server_receive_input);

	ADD_SIGNAL(MethodInfo("local_player_changed", PropertyInfo(Variant::OBJECT, "local_player")));
}
}
