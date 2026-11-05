#ifndef ABILITY_SYSTEM_DEBUGGER_H
#define ABILITY_SYSTEM_DEBUGGER_H

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/core/object_id.hpp>

using namespace godot;

namespace godot
{
class PanelContainer;
class RichTextLabel;
}

namespace GFGD
{

class AbilitySystemComponent;

// An in-game overlay for the ability system.
// Shows one AbilitySystemComponent at a time: attributes (base and current),
// owned and blocked tags with their counts, active effects with time left and
// stacks, granted abilities with their state, cooldown and last failure reason,
// active cues, and a log of what just happened (activations, refusals, effects,
// tag changes, cues).
//
// The World adds one in debug builds (application/game_framework/debug/
// gas_debugger, on by default); it stays hidden until the console shows it:
// gas.toggle, gas.next (steps through every component in the tree), gas.target
// self|<path>. It starts on the local player's pawn. There are no keys by default;
// a game can set toggle_action / cycle_action to its own Input Map actions. On a client it shows the replicated mirror, which
// is exactly what is useful when a client "does not see" something.
class AbilitySystemDebugger : public CanvasLayer
{
	GDCLASS(AbilitySystemDebugger, CanvasLayer)

private:
	StringName toggle_action;
	StringName cycle_action;
	double refresh_interval;
	int log_size;

	PanelContainer* panel;
	RichTextLabel* label;
	ObjectID target_id;
	int target_index;
	double refresh_accumulator;
	PackedStringArray event_log;

public:
	AbilitySystemDebugger();

	virtual void _ready() override;
	virtual void _process(double delta) override;
	virtual void _input(const Ref<InputEvent>& event) override;
	virtual void _exit_tree() override;

	void cmd_toggle(const PackedStringArray& args);
	void cmd_next(const PackedStringArray& args);
	void cmd_target(const PackedStringArray& args);

	void set_toggle_action(const StringName& value) { toggle_action = value; }
	StringName get_toggle_action() const { return toggle_action; }
	void set_cycle_action(const StringName& value) { cycle_action = value; }
	StringName get_cycle_action() const { return cycle_action; }
	void set_refresh_interval(double value) { refresh_interval = value; }
	double get_refresh_interval() const { return refresh_interval; }
	void set_log_size(int value) { log_size = value; }
	int get_log_size() const { return log_size; }

	void set_shown(bool value);
	bool is_shown() const;
	void cycle_target();
	void set_target(AbilitySystemComponent* component);
	AbilitySystemComponent* get_target() const;
	// The overlay's text (BBCode) for a component; also usable without the overlay.
	String describe(AbilitySystemComponent* component) const;
	// The attribute, tag, effect, ability and cue sections for a
	// get_debug_snapshot() dictionary - shared with the gameplay debugger.
	static String format_snapshot(const Dictionary& snapshot);

protected:
	static void _bind_methods();

private:
	Array find_components() const;
	AbilitySystemComponent* find_default_target() const;
	void connect_target(AbilitySystemComponent* component, bool connect);
	void add_event(const String& text);
	void refresh();

	void _on_ability_activated(const Variant& ability);
	void _on_ability_failed(const Variant& ability, const Variant& reason);
	void _on_ability_ended(const Variant& ability, const Variant& was_canceled);
	void _on_effect_applied(const Variant& effect, const Variant& active_id);
	void _on_effect_removed(const Variant& effect, const Variant& active_id);
	void _on_tag_changed(const Variant& tag_name, const Variant& new_count);
	void _on_cue(const Variant& cue_tag, const Variant& event_type, const Variant& parameters);
};

}

#endif
