#ifndef GFGD_DEBUG_TOOLS_H
#define GFGD_DEBUG_TOOLS_H

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

namespace GFGD
{
class PlayerController;

// What the debug tools (console, gameplay debugger, ability system debugger,
// debug draw) share: whether they run at all, and the input actions that open
// them.
//
// Every key is an InputMap action, so it is rebound where every other action
// is - Project Settings > Input Map, or a game's own controls menu at runtime.
// The editor adds the defaults to the project the first time it loads the
// extension; a game that never saved them still gets them, because
// ensure_actions() adds whatever is missing to the InputMap at startup.
// PlayerController never sends gfgd_debug_* actions to the server.
namespace DebugTools
{
	constexpr const char* ACTION_PREFIX = "gfgd_debug_";
	constexpr const char* ACTION_CONSOLE = "gfgd_debug_console";
	constexpr const char* ACTION_GAMEPLAY_DEBUGGER = "gfgd_debug_gameplay_debugger";
	constexpr const char* ACTION_GAMEPLAY_DEBUGGER_LOCK = "gfgd_debug_gameplay_debugger_lock";
	constexpr const char* ACTION_FREE_CAMERA = "gfgd_debug_free_camera";
	constexpr const char* ACTION_FREE_CAMERA_TELEPORT = "gfgd_debug_free_camera_teleport";
	constexpr const char* ACTION_GAS_DEBUGGER = "gfgd_debug_gas_debugger";
	constexpr const char* ACTION_GAS_DEBUGGER_NEXT = "gfgd_debug_gas_debugger_next";
	// gfgd_debug_category_0 .. gfgd_debug_category_9 toggle the gameplay
	// debugger's category slots (numpad 0-9 by default).
	constexpr int CATEGORY_SLOTS = 10;
	StringName category_action(int slot);

	// Debug builds always; release builds only with
	// application/game_framework/debug/enable_in_release.
	bool is_enabled();

	// The project settings and default input actions. Called once at extension
	// load; in the editor it also writes the default actions into the project
	// so they show up in the Input Map.
	void register_settings();

	// With --log (or -log) on the command line, opens a terminal window with
	// the game's log on Windows. Any build; ignored in the editor.
	void open_log_window_if_requested();

	// Adds every missing gfgd_debug_* action to the InputMap with its default key.
	void ensure_actions();

	bool is_debug_action(const String& action_name);

	// Pressed (not echo) and bound to the action. Exact match, so Shift+F3 does
	// not also fire F3.
	bool is_action_pressed(const Ref<InputEvent>& event, const StringName& action);

	// The keys bound to an action, for on-screen hints ("F3", "Kp 1").
	String describe_action(const StringName& action);

	// The first PlayerController sitting at this machine, or null.
	PlayerController* get_local_player_controller();
}
}

#endif
