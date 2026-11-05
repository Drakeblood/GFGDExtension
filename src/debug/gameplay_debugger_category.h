#ifndef GFGD_GAMEPLAY_DEBUGGER_CATEGORY_H
#define GFGD_GAMEPLAY_DEBUGGER_CATEGORY_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace GFGD
{
class PlayerController;

// One section of the gameplay debugger. It is split in two, because the
// interesting state lives on the server:
//
//   _collect_data(target, viewer) -> Dictionary   runs on the SERVER, for the
//       node the viewer is looking at (target may be null). Return plain data
//       - numbers, strings, arrays, dictionaries - it is sent over the network.
//   _draw_data(target, data) -> String           runs on the viewer's machine
//       when new data arrives; returns BBCode for the overlay panel. target is
//       the local copy of the node, so comparing it with the server's data is
//       how a prediction or replication problem shows up.
//   _draw_world(target, data)                    runs on the viewer's machine
//       every frame; draw shapes in the world with DebugDraw.
//
// Register an instance with GameplayDebugger.register_category(), or list the
// script in application/game_framework/debug/gameplay_debugger_categories.
class GameplayDebuggerCategory : public RefCounted
{
	GDCLASS(GameplayDebuggerCategory, RefCounted)

private:
	String category_name;
	bool enabled_by_default;

public:
	GameplayDebuggerCategory();

	String get_category_name() const { return category_name; }
	void set_category_name(const String& value) { category_name = value; }
	bool is_enabled_by_default() const { return enabled_by_default; }
	void set_enabled_by_default(bool value) { enabled_by_default = value; }

	virtual Dictionary collect_data(Node* target, PlayerController* viewer);
	virtual String draw_data(Node* target, const Dictionary& data);
	virtual void draw_world(Node* target, const Dictionary& data);

	// The first node of a class at or under target - or, when target is a
	// pawn's root or its Pawn node, anywhere in that pawn.
	static Node* find_component(Node* target, const String& class_name);

protected:
	static void _bind_methods();

	GDVIRTUAL2R(Dictionary, _collect_data, Node*, Node*)
	GDVIRTUAL2R(String, _draw_data, Node*, Dictionary)
	GDVIRTUAL2(_draw_world, Node*, Dictionary)
};

// --- Built-in categories ----------------------------------------------------

// Name, class, script, position, velocity, network authority and owner.
class GameplayDebuggerCategoryBasic : public GameplayDebuggerCategory
{
	GDCLASS(GameplayDebuggerCategoryBasic, GameplayDebuggerCategory)
public:
	GameplayDebuggerCategoryBasic();
	virtual Dictionary collect_data(Node* target, PlayerController* viewer) override;
	virtual String draw_data(Node* target, const Dictionary& data) override;
	virtual void draw_world(Node* target, const Dictionary& data) override;
protected:
	static void _bind_methods() {}
};

// The server's AbilitySystemComponent state, and where the client's mirror
// disagrees with it.
class GameplayDebuggerCategoryAbilities : public GameplayDebuggerCategory
{
	GDCLASS(GameplayDebuggerCategoryAbilities, GameplayDebuggerCategory)
public:
	GameplayDebuggerCategoryAbilities();
	virtual Dictionary collect_data(Node* target, PlayerController* viewer) override;
	virtual String draw_data(Node* target, const Dictionary& data) override;
protected:
	static void _bind_methods() {}
};

// CharacterMovementComponent: mode, velocity, floor, base, and how far the
// local copy is from the server's.
class GameplayDebuggerCategoryMovement : public GameplayDebuggerCategory
{
	GDCLASS(GameplayDebuggerCategoryMovement, GameplayDebuggerCategory)
public:
	GameplayDebuggerCategoryMovement();
	virtual Dictionary collect_data(Node* target, PlayerController* viewer) override;
	virtual String draw_data(Node* target, const Dictionary& data) override;
	virtual void draw_world(Node* target, const Dictionary& data) override;
protected:
	static void _bind_methods() {}
};

// Peers and round-trip times, server frame rate, time scale.
class GameplayDebuggerCategoryNet : public GameplayDebuggerCategory
{
	GDCLASS(GameplayDebuggerCategoryNet, GameplayDebuggerCategory)
public:
	GameplayDebuggerCategoryNet();
	virtual Dictionary collect_data(Node* target, PlayerController* viewer) override;
	virtual String draw_data(Node* target, const Dictionary& data) override;
protected:
	static void _bind_methods() {}
};
}

#endif
