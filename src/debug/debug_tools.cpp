#include "debug/debug_tools.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "debug/log_window.h"
#include "framework/player_controller.h"
#include "framework/world.h"

using namespace GFGD;

namespace
{
const char* ENABLE_IN_RELEASE_SETTING = "application/game_framework/debug/enable_in_release";

struct DefaultAction
{
	String name;
	Key key;
	bool physical;
};

Vector<DefaultAction> default_actions()
{
	Vector<DefaultAction> actions;
	// Physical keys for the two that sit on punctuation, so they stay where
	// they are on any keyboard layout - the key left of 1 and the key left of Enter.
	actions.push_back({ DebugTools::ACTION_CONSOLE, KEY_QUOTELEFT, true });
	actions.push_back({ DebugTools::ACTION_GAMEPLAY_DEBUGGER, KEY_APOSTROPHE, true });
	actions.push_back({ DebugTools::ACTION_GAMEPLAY_DEBUGGER_LOCK, KEY_KP_MULTIPLY, false });
	actions.push_back({ DebugTools::ACTION_FREE_CAMERA, KEY_TAB, false });
	actions.push_back({ DebugTools::ACTION_FREE_CAMERA_TELEPORT, KEY_T, true });
	for (int slot = 0; slot < DebugTools::CATEGORY_SLOTS; slot++)
	{
		actions.push_back({ String(DebugTools::category_action(slot)), Key(KEY_KP_0 + slot), false });
	}
	return actions;
}

Ref<InputEventKey> make_key_event(const DefaultAction& action)
{
	Ref<InputEventKey> event;
	event.instantiate();
	// All devices, as the Input Map editor saves a key.
	event->set_device(-1);
	if (action.physical) { event->set_physical_keycode(action.key); }
	else { event->set_keycode(action.key); }
	return event;
}
}

StringName DebugTools::category_action(int slot)
{
	return StringName(String("gfgd_debug_category_") + String::num_int64(slot));
}

bool DebugTools::is_enabled()
{
	if (OS::get_singleton()->is_debug_build()) { return true; }
	return bool(ProjectSettings::get_singleton()->get_setting(ENABLE_IN_RELEASE_SETTING, false));
}

void DebugTools::register_settings()
{
	if (!Engine::get_singleton()->is_editor_hint()) { return; }

	// The editor only: an input/ setting is what the Input Map lists, and the
	// project keeps it from the next save on. Nothing is overwritten - an action
	// the developer rebound or deleted stays the way they left it... except that
	// a deleted one comes back, which is the price of having defaults at all.
	ProjectSettings* settings = ProjectSettings::get_singleton();
	const Vector<DefaultAction> actions = default_actions();
	for (const DefaultAction& action : actions)
	{
		const String setting = "input/" + action.name;
		if (settings->has_setting(setting)) { continue; }

		Array events;
		events.push_back(make_key_event(action));
		Dictionary value;
		value["deadzone"] = 0.2;
		value["events"] = events;
		settings->set_setting(setting, value);
	}
}

void DebugTools::open_log_window_if_requested()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	OS* os = OS::get_singleton();
	PackedStringArray args = os->get_cmdline_args();
	args.append_array(os->get_cmdline_user_args());
	if (!args.has("--log") && !args.has("-log")) { return; }

	const String name = ProjectSettings::get_singleton()->get_setting("application/config/name", "Godot");
	LogWindow::open((const wchar_t*)(name + String(" log")).utf16().get_data());
}

void DebugTools::ensure_actions()
{
	InputMap* input_map = InputMap::get_singleton();
	const Vector<DefaultAction> actions = default_actions();
	for (const DefaultAction& action : actions)
	{
		const StringName name(action.name);
		if (input_map->has_action(name)) { continue; }
		input_map->add_action(name);
		input_map->action_add_event(name, make_key_event(action));
	}
}

bool DebugTools::is_debug_action(const String& action_name)
{
	return action_name.begins_with(ACTION_PREFIX);
}

bool DebugTools::is_action_pressed(const Ref<InputEvent>& event, const StringName& action)
{
	if (event.is_null() || !InputMap::get_singleton()->has_action(action)) { return false; }
	return event->is_action_pressed(action, false, true);
}

String DebugTools::friendly_key_name(const String& name)
{
	// Godot's key names, as a player reads them on the keyboard.
	static const char* const pairs[][2] = {
		{ "QuoteLeft", "`" }, { "Apostrophe", "'" }, { "Semicolon", ";" }, { "Comma", "," }, { "Period", "." },
		{ "Slash", "/" }, { "BackSlash", "\\" }, { "BracketLeft", "[" }, { "BracketRight", "]" }, { "Minus", "-" },
		{ "Equal", "=" }, { "Kp Multiply", "Num *" }, { "Kp Add", "Num +" }, { "Kp Subtract", "Num -" },
		{ "Kp Divide", "Num /" }, { "Kp Period", "Num ." }, { "Kp Enter", "Num Enter" },
	};
	String text = name.replace(" (Physical)", "");
	for (const auto& pair : pairs)
	{
		if (text == pair[0]) { return pair[1]; }
	}
	if (text.begins_with("Kp ")) { return "Num " + text.substr(3); }
	return text;
}

String DebugTools::describe_action(const StringName& action)
{
	InputMap* input_map = InputMap::get_singleton();
	if (!input_map->has_action(action)) { return "-"; }

	PackedStringArray keys;
	TypedArray<InputEvent> events = input_map->action_get_events(action);
	for (int i = 0; i < events.size(); i++)
	{
		Ref<InputEventKey> key = events[i];
		if (key.is_valid())
		{
			keys.push_back(friendly_key_name(key->as_text_keycode().is_empty() || key->get_keycode() == KEY_NONE
				? key->as_text_physical_keycode() : key->as_text_keycode()));
			continue;
		}
		Ref<InputEvent> other = events[i];
		if (other.is_valid()) { keys.push_back(other->as_text()); }
	}
	return keys.is_empty() ? String("-") : String("/").join(keys);
}

PlayerController* DebugTools::get_local_player_controller()
{
	World* world = Object::cast_to<World>(Engine::get_singleton()->get_main_loop());
	if (world == nullptr) { return nullptr; }

	const Array controllers = world->get_player_controllers();
	for (int i = 0; i < controllers.size(); i++)
	{
		PlayerController* pc = Object::cast_to<PlayerController>(controllers[i]);
		if (pc != nullptr && pc->is_local_player_controller()) { return pc; }
	}
	return nullptr;
}
