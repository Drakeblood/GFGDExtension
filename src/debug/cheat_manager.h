#ifndef GFGD_CHEAT_MANAGER_H
#define GFGD_CHEAT_MANAGER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/core/object_id.hpp>

using namespace godot;

namespace godot
{
class Node;
}

namespace GFGD
{
class AbilitySystemComponent;
class Pawn;
class PlayerController;
class World;

// A player's cheat commands. Every PlayerController has one (created on first use
// from application/game_framework/debug/cheat_manager_script, or this class),
// and every public method of it is a console command: in a script that
// extends CheatManager,
//
//     func give_money(amount: int = 100) -> void:
//         ...
//
// is typed as "give_money 500". Methods starting with _ are not commands.
// Cheats always run on the server, on the cheating player's controller's
// cheat manager - so get_player_controller() is the player who typed it, and
// whatever the method changes is authoritative and replicates as usual.
// client_message() sends text back to that player's console.
//
// _get_command_help(name) can return a one-line description per command.
class CheatManager : public RefCounted
{
	GDCLASS(CheatManager, RefCounted)

private:
	ObjectID controller_id;

public:
	void setup(PlayerController* controller);

	PlayerController* get_player_controller() const;
	Pawn* get_pawn() const;
	Node* get_pawn_root() const;
	World* get_world() const;
	// The first AbilitySystemComponent under the player's pawn, or null.
	AbilitySystemComponent* get_ability_system() const;

	void client_message(const String& text) const;

	// Every method that is a command: the script's own (inherited ones
	// included) and the built-in ones below.
	PackedStringArray get_command_names() const;
	String get_command_help(const String& name) const;

	// --- Built-in cheats ------------------------------------------------------
	void teleport(const Vector3& position);
	void where() const;
	void set_movement_mode(const StringName& mode_name);
	void gas_add_tag(const StringName& tag_name);
	void gas_remove_tag(const StringName& tag_name);
	void gas_apply_effect(const String& effect_path, float level = 1.0f);
	void gas_grant_ability(const String& ability_path, float level = 1.0f);
	void gas_set_attribute(const StringName& attribute_name, double value);
	void gas_cancel_abilities();

protected:
	static void _bind_methods();

	GDVIRTUAL1RC(String, _get_command_help, String)
};
}

#endif
