#include "framework/input_router.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/game_instance.h"
#include "framework/game_mode.h"
#include "framework/gfgd_scene_tree.h"
#include "framework/local_player.h"
#include "framework/player_input.h"

using namespace godot;

namespace GFGD
{
InputRouter::InputRouter()
{
	scene_tree = nullptr;
}

InputRouter::~InputRouter()
{

}

void InputRouter::_enter_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	set_process_input(true);

	// Input has to keep flowing while the tree is paused, or a paused game can
	// never read the button that unpauses it.
	set_process_mode(PROCESS_MODE_ALWAYS);

	Input::get_singleton()->connect("joy_connection_changed", callable_mp(this, &InputRouter::on_joy_connection_changed));
}

void InputRouter::_exit_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	Input* input = Input::get_singleton();
	if (input->is_connected("joy_connection_changed", callable_mp(this, &InputRouter::on_joy_connection_changed)))
	{
		input->disconnect("joy_connection_changed", callable_mp(this, &InputRouter::on_joy_connection_changed));
	}

	// The root may be torn down before MainLoop::finalize, so hand the scene tree
	// back a null router rather than letting it hold a pointer to a dead node.
	if (scene_tree != nullptr)
	{
		scene_tree->set_input_router(nullptr);
		scene_tree = nullptr;
	}
}

void InputRouter::_input(const Ref<InputEvent>& event)
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }
	if (scene_tree == nullptr || event.is_null()) { return; }

	GameInstance* game_instance = scene_tree->get_game_instance();
	if (game_instance == nullptr) { return; }

	const int device_slot = PlayerInput::device_slot_from_event(event);

	bool routed = false;
	const int local_player_count = game_instance->get_local_player_count();
	for (int i = 0; i < local_player_count; i++)
	{
		LocalPlayer* local_player = game_instance->get_local_player(i);
		if (local_player == nullptr) { continue; }

		PlayerInput* player_input = local_player->get_player_input();
		if (player_input == nullptr || !player_input->accepts_device_slot(device_slot)) { continue; }

		player_input->feed_input_event(event);
		routed = true;
	}

	// Only a device nobody owns can trigger a join. With the default player 0
	// holding DEVICE_SLOT_ALL this branch is unreachable, which is exactly the
	// single player behaviour projects have today.
	if (routed) { return; }
	if (!is_join_worthy(event)) { return; }

	emit_signal("unassigned_device_input", device_slot, event);

	if (GameMode* game_mode = scene_tree->get_game_mode())
	{
		game_mode->try_join(device_slot);
	}
}

bool InputRouter::is_join_worthy(const Ref<InputEvent>& event) const
{
	if (!event->is_action_type() || !event->is_pressed() || event->is_echo()) { return false; }

	// Stick drift must not drag a player into the game.
	Ref<InputEventJoypadMotion> joypad_motion = event;
	if (joypad_motion.is_valid() && Math::abs(joypad_motion->get_axis_value()) < 0.7f) { return false; }

	return true;
}

void InputRouter::on_joy_connection_changed(int32_t device, bool connected)
{
	if (scene_tree == nullptr) { return; }

	GameInstance* game_instance = scene_tree->get_game_instance();
	if (game_instance == nullptr) { return; }

	LocalPlayer* local_player = game_instance->find_local_player_for_device_slot(device);

	if (connected)
	{
		emit_signal("joypad_connected", device, local_player);
		return;
	}

	// Keep the LocalPlayer around so a pad plugged back in resumes the same
	// player, but drop the slot and everything it was holding - nothing is ever
	// going to deliver the release for a device that is gone.
	if (local_player != nullptr)
	{
		local_player->remove_device_slot(device);
	}

	emit_signal("joypad_disconnected", device, local_player);
}

void InputRouter::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_gfgd_scene_tree"), &InputRouter::get_gfgd_scene_tree);

	ADD_SIGNAL(MethodInfo("unassigned_device_input",
			PropertyInfo(Variant::INT, "device_slot"),
			PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent")));
	ADD_SIGNAL(MethodInfo("joypad_connected",
			PropertyInfo(Variant::INT, "device"),
			PropertyInfo(Variant::OBJECT, "local_player", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer")));
	ADD_SIGNAL(MethodInfo("joypad_disconnected",
			PropertyInfo(Variant::INT, "device"),
			PropertyInfo(Variant::OBJECT, "local_player", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer")));
}
}
