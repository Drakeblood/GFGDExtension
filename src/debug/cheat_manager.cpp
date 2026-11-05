#include "debug/cheat_manager.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_ability.h"
#include "ability_system/gameplay_effect.h"
#include "debug/console_manager.h"
#include "framework/pawn.h"
#include "framework/player_controller.h"
#include "framework/world.h"
#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tags_manager.h"
#include "movement/character_movement_component.h"

using namespace GFGD;

namespace
{
struct BuiltIn
{
	const char* name;
	const char* help;
};

const BuiltIn BUILT_INS[] = {
	{ "teleport", "Moves your pawn to x,y,z (e.g. teleport 0,2,0)." },
	{ "where", "Prints your pawn's position." },
	{ "set_movement_mode", "Switches your pawn's CharacterMovementComponent mode (Walking, Falling, Flying...)." },
	{ "gas_add_tag", "Adds a loose gameplay tag to your pawn's AbilitySystemComponent." },
	{ "gas_remove_tag", "Removes a loose gameplay tag from your pawn's AbilitySystemComponent." },
	{ "gas_apply_effect", "Applies a GameplayEffect (res:// path) to your pawn." },
	{ "gas_grant_ability", "Grants a GameplayAbility (res:// path) to your pawn." },
	{ "gas_set_attribute", "Sets an attribute's base value on your pawn." },
	{ "gas_cancel_abilities", "Cancels every active ability on your pawn." },
};
}

void CheatManager::setup(PlayerController* controller)
{
	controller_id = controller != nullptr ? controller->get_instance_id() : ObjectID();
}

PlayerController* CheatManager::get_player_controller() const
{
	return Object::cast_to<PlayerController>(ObjectDB::get_instance(controller_id));
}

Pawn* CheatManager::get_pawn() const
{
	PlayerController* pc = get_player_controller();
	return pc != nullptr ? pc->get_pawn() : nullptr;
}

Node* CheatManager::get_pawn_root() const
{
	Pawn* pawn = get_pawn();
	return pawn != nullptr ? pawn->get_pawn_root() : nullptr;
}

World* CheatManager::get_world() const
{
	PlayerController* pc = get_player_controller();
	return pc != nullptr ? pc->get_world() : nullptr;
}

AbilitySystemComponent* CheatManager::get_ability_system() const
{
	Node* root = get_pawn_root();
	if (root == nullptr) { return nullptr; }
	TypedArray<Node> found = root->find_children("*", "AbilitySystemComponent", true, false);
	return found.is_empty() ? nullptr : Object::cast_to<AbilitySystemComponent>(found[0]);
}

void CheatManager::client_message(const String& text) const
{
	if (ConsoleManager* console = ConsoleManager::get_singleton()) { console->print_line(text); }
}

PackedStringArray CheatManager::get_command_names() const
{
	PackedStringArray names;
	for (const BuiltIn& built_in : BUILT_INS) { names.push_back(built_in.name); }

	Ref<Script> script = get_script();
	if (script.is_valid())
	{
		const TypedArray<Dictionary> methods = script->get_script_method_list();
		for (int i = 0; i < methods.size(); i++)
		{
			const String name = Dictionary(methods[i]).get("name", String());
			if (name.is_empty() || name.begins_with("_") || name.begins_with("@") || names.has(name)) { continue; }
			names.push_back(name);
		}
	}
	return names;
}

String CheatManager::get_command_help(const String& name) const
{
	String help;
	if (GDVIRTUAL_CALL(_get_command_help, name, help) && !help.is_empty()) { return help; }
	for (const BuiltIn& built_in : BUILT_INS)
	{
		if (name == built_in.name) { return built_in.help; }
	}
	return String();
}

// --- Built-in cheats ----------------------------------------------------------

void CheatManager::teleport(const Vector3& position)
{
	Node3D* root = Object::cast_to<Node3D>(get_pawn_root());
	if (root == nullptr)
	{
		client_message("teleport: no 3D pawn.");
		return;
	}
	// The movement component adopts a position that was set from outside (it
	// treats a jump that large as a teleport), so nothing else has to know.
	root->set_global_position(position);
	client_message("Teleported to " + String(Variant(position)));
}

void CheatManager::where() const
{
	Node3D* root = Object::cast_to<Node3D>(get_pawn_root());
	client_message(root != nullptr ? String(Variant(root->get_global_position())) : String("No 3D pawn."));
}

void CheatManager::set_movement_mode(const StringName& mode_name)
{
	Node* root = get_pawn_root();
	TypedArray<Node> found = root != nullptr ? root->find_children("*", "CharacterMovementComponent", true, false) : TypedArray<Node>();
	CharacterMovementComponent* cmc = found.is_empty() ? nullptr : Object::cast_to<CharacterMovementComponent>(found[0]);
	if (cmc == nullptr)
	{
		client_message("set_movement_mode: no CharacterMovementComponent on your pawn.");
		return;
	}
	if (cmc->find_mode(mode_name).is_null())
	{
		client_message("set_movement_mode: no mode \"" + String(mode_name) + "\".");
		return;
	}
	cmc->set_movement_mode(mode_name);
	client_message("Movement mode: " + String(mode_name));
}

void CheatManager::gas_add_tag(const StringName& tag_name)
{
	AbilitySystemComponent* asc = get_ability_system();
	if (asc == nullptr) { client_message("No AbilitySystemComponent on your pawn."); return; }
	asc->add_loose_gameplay_tag(GameplayTagsManager::get_singleton()->request_tag(tag_name));
	client_message("+tag " + String(tag_name));
}

void CheatManager::gas_remove_tag(const StringName& tag_name)
{
	AbilitySystemComponent* asc = get_ability_system();
	if (asc == nullptr) { client_message("No AbilitySystemComponent on your pawn."); return; }
	asc->remove_loose_gameplay_tag(GameplayTagsManager::get_singleton()->request_tag(tag_name));
	client_message("-tag " + String(tag_name));
}

void CheatManager::gas_apply_effect(const String& effect_path, float level)
{
	AbilitySystemComponent* asc = get_ability_system();
	if (asc == nullptr) { client_message("No AbilitySystemComponent on your pawn."); return; }
	Ref<GameplayEffect> effect = ResourceLoader::get_singleton()->load(effect_path);
	if (effect.is_null()) { client_message("Not a GameplayEffect: " + effect_path); return; }
	const int64_t id = asc->apply_gameplay_effect_to_self(effect, level);
	client_message("Applied " + effect_path.get_file() + " (#" + String::num_int64(id) + ")");
}

void CheatManager::gas_grant_ability(const String& ability_path, float level)
{
	AbilitySystemComponent* asc = get_ability_system();
	if (asc == nullptr) { client_message("No AbilitySystemComponent on your pawn."); return; }
	Ref<GameplayAbility> ability = ResourceLoader::get_singleton()->load(ability_path);
	if (ability.is_null()) { client_message("Not a GameplayAbility: " + ability_path); return; }
	const int64_t handle = asc->give_ability(ability, Variant(), level);
	client_message("Granted " + ability_path.get_file() + " (handle " + String::num_int64(handle) + ")");
}

void CheatManager::gas_set_attribute(const StringName& attribute_name, double value)
{
	AbilitySystemComponent* asc = get_ability_system();
	if (asc == nullptr) { client_message("No AbilitySystemComponent on your pawn."); return; }
	asc->set_attribute_base_value(attribute_name, value);
	client_message(String(attribute_name) + " base = " + String::num(value, 3));
}

void CheatManager::gas_cancel_abilities()
{
	AbilitySystemComponent* asc = get_ability_system();
	if (asc == nullptr) { client_message("No AbilitySystemComponent on your pawn."); return; }
	asc->cancel_all_abilities();
	client_message("Canceled all abilities.");
}

void CheatManager::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_controller"), &CheatManager::get_player_controller);
	ClassDB::bind_method(D_METHOD("get_pawn"), &CheatManager::get_pawn);
	ClassDB::bind_method(D_METHOD("get_pawn_root"), &CheatManager::get_pawn_root);
	ClassDB::bind_method(D_METHOD("get_world"), &CheatManager::get_world);
	ClassDB::bind_method(D_METHOD("get_ability_system"), &CheatManager::get_ability_system);
	ClassDB::bind_method(D_METHOD("client_message", "text"), &CheatManager::client_message);
	ClassDB::bind_method(D_METHOD("get_command_names"), &CheatManager::get_command_names);
	ClassDB::bind_method(D_METHOD("get_command_help", "name"), &CheatManager::get_command_help);

	ClassDB::bind_method(D_METHOD("teleport", "position"), &CheatManager::teleport);
	ClassDB::bind_method(D_METHOD("where"), &CheatManager::where);
	ClassDB::bind_method(D_METHOD("set_movement_mode", "mode_name"), &CheatManager::set_movement_mode);
	ClassDB::bind_method(D_METHOD("gas_add_tag", "tag_name"), &CheatManager::gas_add_tag);
	ClassDB::bind_method(D_METHOD("gas_remove_tag", "tag_name"), &CheatManager::gas_remove_tag);
	ClassDB::bind_method(D_METHOD("gas_apply_effect", "effect_path", "level"), &CheatManager::gas_apply_effect, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("gas_grant_ability", "ability_path", "level"), &CheatManager::gas_grant_ability, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("gas_set_attribute", "attribute_name", "value"), &CheatManager::gas_set_attribute);
	ClassDB::bind_method(D_METHOD("gas_cancel_abilities"), &CheatManager::gas_cancel_abilities);

	GDVIRTUAL_BIND(_get_command_help, "name");
}
