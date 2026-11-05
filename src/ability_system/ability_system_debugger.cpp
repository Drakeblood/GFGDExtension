#include "ability_system/ability_system_debugger.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_ability.h"
#include "ability_system/gameplay_effect.h"
#include "debug/console_manager.h"
#include "debug/debug_tools.h"
#include "framework/controller.h"
#include "framework/pawn.h"
#include "framework/player_controller.h"
#include "framework/world.h"

using namespace GFGD;

namespace
{
const char* COLOR_HEAD = "#ffd36e";
const char* COLOR_DIM = "#9aa4b1";
const char* COLOR_GOOD = "#8fe388";
const char* COLOR_BAD = "#ff8a7a";
const char* COLOR_MOD = "#7cc7ff";

String fmt_time(double seconds)
{
	return String::num(seconds, 1) + "s";
}

String policy_name(int policy)
{
	switch (policy)
	{
		case GameplayAbility::LOCAL_ONLY: return "local";
		case GameplayAbility::LOCAL_PREDICTED: return "predicted";
		case GameplayAbility::SERVER_ONLY: return "server";
	}
	return "?";
}

String join(const PackedStringArray& items)
{
	return items.is_empty() ? String("-") : String(", ").join(items);
}
}

AbilitySystemDebugger::AbilitySystemDebugger()
{
	toggle_action = DebugTools::ACTION_GAS_DEBUGGER;
	cycle_action = DebugTools::ACTION_GAS_DEBUGGER_NEXT;
	refresh_interval = 0.1;
	log_size = 14;
	panel = nullptr;
	label = nullptr;
	target_index = -1;
	refresh_accumulator = 0.0;
}

void AbilitySystemDebugger::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	set_layer(120);
	panel = memnew(PanelContainer);
	panel->set_name("Panel");
	panel->set_custom_minimum_size(Vector2(560, 0));
	// Right edge, vertically centred: clear of the usual top-left debug text and top-right HUD.
	panel->set_anchors_and_offsets_preset(Control::PRESET_CENTER_RIGHT, Control::PRESET_MODE_MINSIZE, 12);
	panel->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
	panel->set_v_grow_direction(Control::GROW_DIRECTION_BOTH);
	panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(Color(0.05, 0.06, 0.08, 0.82));
	style->set_corner_radius_all(6);
	style->set_content_margin_all(10);
	panel->add_theme_stylebox_override("panel", style);
	add_child(panel);

	label = memnew(RichTextLabel);
	label->set_use_bbcode(true);
	label->set_fit_content(true);
	label->set_scroll_active(false);
	label->set_custom_minimum_size(Vector2(560, 0));
	label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	label->add_theme_font_size_override("normal_font_size", 13);
	label->add_theme_font_size_override("bold_font_size", 13);
	panel->add_child(label);

	set_shown(false);
	set_process_input(true);
}

void AbilitySystemDebugger::_input(const Ref<InputEvent>& event)
{
	ConsoleManager* console = ConsoleManager::get_singleton();
	if (console != nullptr && console->is_console_open()) { return; }

	if (DebugTools::is_action_pressed(event, toggle_action))
	{
		set_shown(!is_shown());
		get_viewport()->set_input_as_handled();
	}
	else if (is_shown() && DebugTools::is_action_pressed(event, cycle_action))
	{
		cycle_target();
		get_viewport()->set_input_as_handled();
	}
}

void AbilitySystemDebugger::_process(double delta)
{
	if (!is_shown()) { return; }

	refresh_accumulator += delta;
	if (refresh_accumulator < refresh_interval) { return; }
	refresh_accumulator = 0.0;
	refresh();
}

void AbilitySystemDebugger::set_shown(bool value)
{
	if (panel != nullptr) { panel->set_visible(value); }
	set_process(value);
	if (value) { refresh(); }
}

bool AbilitySystemDebugger::is_shown() const
{
	return panel != nullptr && panel->is_visible();
}

Array AbilitySystemDebugger::find_components() const
{
	if (!is_inside_tree()) { return Array(); }
	TypedArray<Node> found = get_tree()->get_root()->find_children("*", "AbilitySystemComponent", true, false);
	return found;
}

AbilitySystemComponent* AbilitySystemDebugger::find_default_target() const
{
	World* world = Object::cast_to<World>(get_tree());
	if (world != nullptr)
	{
		PlayerController* pc = world->get_first_player_controller();
		Pawn* pawn = pc != nullptr ? pc->get_pawn() : nullptr;
		Node* root = pawn != nullptr ? pawn->get_pawn_root() : nullptr;
		if (root != nullptr)
		{
			TypedArray<Node> found = root->find_children("*", "AbilitySystemComponent", true, false);
			if (!found.is_empty()) { return Object::cast_to<AbilitySystemComponent>(found[0]); }
		}
	}
	Array all = find_components();
	return all.is_empty() ? nullptr : Object::cast_to<AbilitySystemComponent>(all[0]);
}

AbilitySystemComponent* AbilitySystemDebugger::get_target() const
{
	return Object::cast_to<AbilitySystemComponent>(ObjectDB::get_instance(target_id));
}

void AbilitySystemDebugger::set_target(AbilitySystemComponent* component)
{
	AbilitySystemComponent* old = get_target();
	if (old == component) { return; }
	if (old != nullptr) { connect_target(old, false); }
	target_id = component != nullptr ? component->get_instance_id() : ObjectID();
	event_log.clear();
	if (component != nullptr)
	{
		connect_target(component, true);
		add_event(String("[color=") + COLOR_DIM + "]watching " + String(component->get_path()) + "[/color]");
	}
}

void AbilitySystemDebugger::cycle_target()
{
	Array all = find_components();
	if (all.is_empty())
	{
		set_target(nullptr);
		return;
	}
	target_index = (target_index + 1) % all.size();
	set_target(Object::cast_to<AbilitySystemComponent>(all[target_index]));
	refresh();
}

void AbilitySystemDebugger::connect_target(AbilitySystemComponent* component, bool connect)
{
	struct Hook { const char* signal; Callable callable; };
	const Hook hooks[] = {
		{ "ability_activated", callable_mp(this, &AbilitySystemDebugger::_on_ability_activated) },
		{ "ability_activation_failed", callable_mp(this, &AbilitySystemDebugger::_on_ability_failed) },
		{ "ability_ended", callable_mp(this, &AbilitySystemDebugger::_on_ability_ended) },
		{ "gameplay_effect_applied", callable_mp(this, &AbilitySystemDebugger::_on_effect_applied) },
		{ "gameplay_effect_removed", callable_mp(this, &AbilitySystemDebugger::_on_effect_removed) },
		{ "owned_tag_changed", callable_mp(this, &AbilitySystemDebugger::_on_tag_changed) },
		{ "gameplay_cue", callable_mp(this, &AbilitySystemDebugger::_on_cue) },
	};
	for (const Hook& hook : hooks)
	{
		const bool connected = component->is_connected(hook.signal, hook.callable);
		if (connect && !connected) { component->connect(hook.signal, hook.callable); }
		else if (!connect && connected) { component->disconnect(hook.signal, hook.callable); }
	}
}

void AbilitySystemDebugger::add_event(const String& text)
{
	const double now = Time::get_singleton()->get_ticks_msec() / 1000.0;
	event_log.push_back("[color=" + String(COLOR_DIM) + "]" + String::num(now, 1) + "[/color]  " + text);
	while (event_log.size() > log_size) { event_log.remove_at(0); }
}

void AbilitySystemDebugger::refresh()
{
	if (label == nullptr) { return; }
	AbilitySystemComponent* target = get_target();
	if (target == nullptr)
	{
		target = find_default_target();
		set_target(target);
	}
	label->set_text(describe(target));
}

String AbilitySystemDebugger::describe(AbilitySystemComponent* component) const
{
	String out;
	const String head = String("[color=") + COLOR_HEAD + "][b]";
	const String end_head = "[/b][/color]\n";
	out += head + String("GAS DEBUGGER") + end_head;
	out += String("[color=") + COLOR_DIM + "]" + DebugTools::describe_action(toggle_action) + " hide   "
		+ DebugTools::describe_action(cycle_action) + " next component[/color]\n";
	if (component == nullptr)
	{
		return out + "No AbilitySystemComponent in the tree.";
	}

	const Dictionary s = component->get_debug_snapshot();
	static const char* MODES[] = { "none", "minimal", "mixed", "full" };
	const int mode = s["replication_mode"];
	out += "[b]" + String(s["avatar"]) + "[/b]  " + String(component->get_path()) + "\n";
	out += String("role: ") + String(s["role"]) + "   owner peer: " + String::num_int64(s["owner_peer"])
		+ "   replication: " + MODES[CLAMP(mode, 0, 3)] + "\n\n";
	out += format_snapshot(s);

	out += head + String("Events") + end_head;
	for (int i = 0; i < event_log.size(); i++)
	{
		out += "  " + event_log[i] + "\n";
	}
	return out;
}

String AbilitySystemDebugger::format_snapshot(const Dictionary& s)
{
	String out;
	const String head = String("[color=") + COLOR_HEAD + "][b]";
	const String end_head = "[/b][/color]\n";

	out += head + String("Attributes") + end_head;
	const Dictionary attributes = s["attributes"];
	const Array names = attributes.keys();
	if (names.is_empty()) { out += "-\n"; }
	for (int i = 0; i < names.size(); i++)
	{
		const Array values = attributes[names[i]];
		const double base = values[0];
		const double current = values[1];
		String line = "  " + String(names[i]) + ": " + String::num(current, 2);
		if (!Math::is_equal_approx(base, current))
		{
			line = "  " + String(names[i]) + ": [color=" + COLOR_MOD + "]" + String::num(current, 2) + "[/color]"
				+ " (base " + String::num(base, 2) + ")";
		}
		out += line + "\n";
	}

	out += head + String("Owned tags") + end_head;
	const Dictionary tags = s["tags"];
	const Array tag_names = tags.keys();
	PackedStringArray tag_items;
	for (int i = 0; i < tag_names.size(); i++)
	{
		const int count = tags[tag_names[i]];
		tag_items.push_back(String(tag_names[i]) + (count > 1 ? " ×" + String::num_int64(count) : String()));
	}
	out += "  " + join(tag_items) + "\n";
	const PackedStringArray blocked = s["blocked_ability_tags"];
	if (!blocked.is_empty())
	{
		out += String("  [color=") + COLOR_BAD + "]blocking abilities: " + join(blocked) + "[/color]\n";
	}

	out += head + String("Active effects") + end_head;
	const Array effects = s["effects"];
	if (effects.is_empty()) { out += "  -\n"; }
	for (int i = 0; i < effects.size(); i++)
	{
		const Dictionary e = effects[i];
		const int policy = e["policy"];
		String timing = policy == GameplayEffect::INFINITE ? String("infinite")
			: fmt_time(e["remaining"]) + " / " + fmt_time(e["duration"]);
		const int stacks = e["stack"];
		String line = "  #" + String::num_int64(e["id"]) + " " + String(e["name"]) + "  " + timing;
		if (stacks > 1) { line += "  ×" + String::num_int64(stacks); }
		const PackedStringArray granted = e["granted_tags"];
		if (!granted.is_empty()) { line += String("  [color=") + COLOR_DIM + "]grants " + join(granted) + "[/color]"; }
		if (bool(e["replicated"])) { line += String("  [color=") + COLOR_DIM + "](mirror)[/color]"; }
		out += line + "\n";
	}

	out += head + String("Abilities") + end_head;
	const Array abilities = s["abilities"];
	if (abilities.is_empty()) { out += "  -\n"; }
	for (int i = 0; i < abilities.size(); i++)
	{
		const Dictionary a = abilities[i];
		String state;
		const double cooldown = a["cooldown_remaining"];
		if (bool(a["active"]))
		{
			state = String("[color=") + COLOR_GOOD + "]ACTIVE " + fmt_time(a["time_active"]) + "[/color]";
		}
		else if (cooldown > 0.0)
		{
			state = String("[color=") + COLOR_MOD + "]cooldown " + fmt_time(cooldown) + "[/color]";
		}
		else
		{
			state = "ready";
		}
		const StringName failure = a["last_failure"];
		if (failure != StringName())
		{
			state += String("  [color=") + COLOR_BAD + "]last refused: " + String(failure) + "[/color]";
		}
		const StringName input = a["input"];
		out += "  " + String(a["name"]) + "  [" + (input == StringName() ? String("-") : String(input))
			+ (bool(a["input_pressed"]) ? String(" ▼") : String()) + "]  " + policy_name(a["policy"]) + "  " + state + "\n";
	}

	const PackedStringArray cues = s["active_cues"];
	if (!cues.is_empty())
	{
		out += head + String("Active cues") + end_head + "  " + join(cues) + "\n";
	}
	return out;
}

// --- Event log ----------------------------------------------------------------

namespace
{
String ability_label(const Variant& ability)
{
	Ref<GameplayAbility> a = ability;
	return a.is_valid() ? String(a->get_ability_name()) : String("?");
}

String effect_label(const Variant& effect)
{
	Ref<GameplayEffect> e = effect;
	if (e.is_null()) { return "?"; }
	const String path = e->get_path();
	return path.is_empty() ? String("(built in code)") : path.get_file().get_basename();
}
}

void AbilitySystemDebugger::_on_ability_activated(const Variant& ability)
{
	add_event(String("[color=") + COLOR_GOOD + "]activated[/color] " + ability_label(ability));
}

void AbilitySystemDebugger::_on_ability_failed(const Variant& ability, const Variant& reason)
{
	add_event(String("[color=") + COLOR_BAD + "]refused[/color] " + ability_label(ability) + " — " + String(reason));
}

void AbilitySystemDebugger::_on_ability_ended(const Variant& ability, const Variant& was_canceled)
{
	add_event(String("ended ") + ability_label(ability) + (bool(was_canceled) ? String(" (canceled)") : String()));
}

void AbilitySystemDebugger::_on_effect_applied(const Variant& effect, const Variant& active_id)
{
	add_event(String("[color=") + COLOR_MOD + "]+effect[/color] " + effect_label(effect) + " #" + String::num_int64(active_id));
}

void AbilitySystemDebugger::_on_effect_removed(const Variant& effect, const Variant& active_id)
{
	add_event(String("-effect ") + effect_label(effect) + " #" + String::num_int64(active_id));
}

void AbilitySystemDebugger::_on_tag_changed(const Variant& tag_name, const Variant& new_count)
{
	const int count = new_count;
	add_event(String(count > 0 ? "+tag " : "-tag ") + String(tag_name) + (count > 1 ? " ×" + String::num_int64(count) : String()));
}

void AbilitySystemDebugger::_on_cue(const Variant& cue_tag, const Variant& event_type, const Variant& /*parameters*/)
{
	static const char* EVENTS[] = { "executed", "added", "removed" };
	const int type = event_type;
	add_event(String("[color=") + COLOR_DIM + "]cue " + String(cue_tag) + " " + EVENTS[CLAMP(type, 0, 2)] + "[/color]");
}

void AbilitySystemDebugger::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_toggle_action", "value"), &AbilitySystemDebugger::set_toggle_action);
	ClassDB::bind_method(D_METHOD("get_toggle_action"), &AbilitySystemDebugger::get_toggle_action);
	ClassDB::bind_method(D_METHOD("set_cycle_action", "value"), &AbilitySystemDebugger::set_cycle_action);
	ClassDB::bind_method(D_METHOD("get_cycle_action"), &AbilitySystemDebugger::get_cycle_action);
	ClassDB::bind_static_method("AbilitySystemDebugger", D_METHOD("format_snapshot", "snapshot"), &AbilitySystemDebugger::format_snapshot);
	ClassDB::bind_method(D_METHOD("set_refresh_interval", "value"), &AbilitySystemDebugger::set_refresh_interval);
	ClassDB::bind_method(D_METHOD("get_refresh_interval"), &AbilitySystemDebugger::get_refresh_interval);
	ClassDB::bind_method(D_METHOD("set_log_size", "value"), &AbilitySystemDebugger::set_log_size);
	ClassDB::bind_method(D_METHOD("get_log_size"), &AbilitySystemDebugger::get_log_size);
	ClassDB::bind_method(D_METHOD("set_shown", "value"), &AbilitySystemDebugger::set_shown);
	ClassDB::bind_method(D_METHOD("is_shown"), &AbilitySystemDebugger::is_shown);
	ClassDB::bind_method(D_METHOD("cycle_target"), &AbilitySystemDebugger::cycle_target);
	ClassDB::bind_method(D_METHOD("set_target", "component"), &AbilitySystemDebugger::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &AbilitySystemDebugger::get_target);
	ClassDB::bind_method(D_METHOD("describe", "component"), &AbilitySystemDebugger::describe);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "toggle_action"), "set_toggle_action", "get_toggle_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "cycle_action"), "set_cycle_action", "get_cycle_action");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "refresh_interval"), "set_refresh_interval", "get_refresh_interval");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "log_size"), "set_log_size", "get_log_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shown"), "set_shown", "is_shown");
}
