#ifndef GFGD_GAMEPLAY_DEBUGGER_H
#define GFGD_GAMEPLAY_DEBUGGER_H

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "debug/gameplay_debugger_category.h"

using namespace godot;

namespace godot
{
class PanelContainer;
class RichTextLabel;
}

namespace GFGD
{
class DebugCamera;
class PlayerController;

// The category registry - an engine singleton, so GameplayDebugger.
// register_category() works from any script. It exists on every machine,
// dedicated servers included, because that is where categories collect.
//
// The built-in categories (Basic, Abilities, Movement, Net) come first, then
// the scripts listed in application/game_framework/debug/
// gameplay_debugger_categories, in order. A category's slot (its numpad key)
// is its index in this list.
class GameplayDebugger : public Object
{
	GDCLASS(GameplayDebugger, Object)

private:
	static GameplayDebugger* singleton;
	Vector<Ref<GameplayDebuggerCategory>> categories;
	bool initialized;

public:
	GameplayDebugger();

	static GameplayDebugger* get_singleton() { return singleton; }
	static void create_singleton();
	static void destroy_singleton();

	void register_category(const Ref<GameplayDebuggerCategory>& category);
	void unregister_category(const String& name);
	Ref<GameplayDebuggerCategory> get_category(const String& name);
	TypedArray<GameplayDebuggerCategory> get_categories();
	PackedStringArray get_category_names();

	// Runs every named category's collect_data for target. Keyed by category name.
	Dictionary collect(Node* target, const PackedStringArray& category_names, PlayerController* viewer);

	// Forgets every category; they are rebuilt on next use. Called by the World
	// at shutdown, while script categories can still be freed.
	void release_script_references();

protected:
	static void _bind_methods();

private:
	void ensure_initialized();
};

// The overlay - one per machine with a screen, added by the World. The
// gameplay_debugger action (' by default) shows it; it then follows whatever
// is in the middle of the screen (the local pawn when nothing is), until the
// lock action (numpad *) pins the current target. The category actions
// (numpad 0-9) switch categories on and off.
//
// The free camera action (Tab) detaches the view into a DebugCamera while the
// overlay is shown - the pawn stands still, targets are picked from wherever the
// camera flies, and the teleport action (T) moves the pawn there. "gd.freecam"
// does the same without the overlay.
//
// Data comes from the server through this player's DebugReplicator; without a
// PlayerController (a menu) it is collected locally.
class GameplayDebuggerHUD : public CanvasLayer
{
	GDCLASS(GameplayDebuggerHUD, CanvasLayer)

private:
	bool active;
	bool locked;
	ObjectID target_id;
	PackedStringArray enabled_categories;
	bool enabled_initialized;

	PanelContainer* panel;
	RichTextLabel* label;
	double text_accumulator;
	double local_collect_accumulator;

	// What the server was last asked for, so a request is only sent on change.
	bool sent_active;
	NodePath sent_target;
	PackedStringArray sent_categories;

	Dictionary local_data;

	// What the aim ray hit on the last physics step, and a gd.target aim waiting for it.
	ObjectID aimed_id;
	bool aim_lock_pending;

	// Free camera: the camera, the one it replaced, and the mouse mode to restore.
	ObjectID free_camera_id;
	ObjectID previous_camera_id;
	Input::MouseMode mouse_mode_before;

public:
	GameplayDebuggerHUD();

	virtual void _ready() override;
	virtual void _exit_tree() override;
	virtual void _process(double delta) override;
	virtual void _physics_process(double delta) override;
	virtual void _input(const Ref<InputEvent>& event) override;

	void set_active(bool value);
	bool is_active() const { return active; }
	void set_locked(bool value) { locked = value; }
	bool is_locked() const { return locked; }

	void set_target(Node* node);
	Node* get_target() const;
	// The node a ray from the middle of the screen hits, resolved to its actor.
	// Cast during the physics step; called outside it, the last step's answer.
	Node* find_target_under_crosshair() const;

	void set_free_camera(bool value);
	bool is_free_camera() const;
	DebugCamera* get_free_camera() const;
	// Moves the player's pawn to the free camera (a cheat, so it runs on the
	// server) and leaves the free camera.
	void teleport_pawn_to_free_camera();

	void toggle_category(const String& name);
	void toggle_category_slot(int slot);
	PackedStringArray get_enabled_categories() const { return enabled_categories; }

	// The node a hit collider belongs to, for debugging purposes: the nearest
	// ancestor in the "gameplay_debugger_target" group, else a pawn's root,
	// else the nearest one with a script.
	static Node* resolve_actor(Node* node);

protected:
	static void _bind_methods();

private:
	void init_enabled_categories();
	Dictionary current_data(bool& waiting) const;
	void sync_request();
	void refresh_text();
	Node* default_target() const;

	void cmd_toggle(const PackedStringArray& args);
	void cmd_category(const PackedStringArray& args);
	void cmd_target(const PackedStringArray& args);
	void cmd_lock(const PackedStringArray& args);
	void cmd_freecam(const PackedStringArray& args);
};
}

#endif
