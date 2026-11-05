#include "debug/game_console.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/v_scroll_bar.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "debug/console_manager.h"
#include "debug/debug_tools.h"
#include "framework/player_input.h"

using namespace GFGD;

namespace
{
const char* HISTORY_PATH = "user://gfgd_console_history.txt";
const char* ENGINE_LOG_VARIABLE = "console.engine_log";
constexpr int MAX_HISTORY = 100;
constexpr int MAX_SUGGESTIONS = 12;
constexpr int MAX_BAR_OUTPUT = 6;
constexpr float BAR_HEIGHT = 36.0f;
constexpr float FULL_FRACTION = 0.45f;

const char* COLOR_NAME = "#ffd36e";
const char* COLOR_USAGE = "#9aa4b1";
const char* COLOR_HELP = "#c9d1dc";
const char* COLOR_TAG = "#7cc7ff";
const char* COLOR_SELECTED = "#34466a";

Color level_color(int level)
{
	switch (level)
	{
		case ConsoleManager::OUTPUT_COMMAND: return Color(1.0, 0.83, 0.43);
		case ConsoleManager::OUTPUT_WARNING: return Color(1.0, 0.8, 0.4);
		case ConsoleManager::OUTPUT_ERROR: return Color(1.0, 0.54, 0.48);
		case ConsoleManager::OUTPUT_LOG: return Color(0.62, 0.66, 0.72);
	}
	return Color(0.92, 0.93, 0.95);
}

// Text going into BBCode: an opening bracket would start a tag ("[filter]").
String escape(const String& text)
{
	return text.replace("[", "[lb]");
}

Ref<StyleBoxFlat> make_style(const Color& background, int bottom_border = 0, int top_border = 0)
{
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(background);
	style->set_border_color(Color(1.0, 0.83, 0.43, 0.6));
	style->set_border_width(SIDE_BOTTOM, bottom_border);
	style->set_border_width(SIDE_TOP, top_border);
	style->set_content_margin_all(6);
	return style;
}
}

// --- Logger -------------------------------------------------------------------

void GameConsoleLogger::_log_message(const String& message, bool error)
{
	if (errors_only && !error) { return; }
	std::lock_guard<std::mutex> lock(mutex);
	if (pending.size() > 2000) { return; }
	pending.push_back({ message.strip_edges(false, true), error ? ConsoleManager::OUTPUT_ERROR : ConsoleManager::OUTPUT_LOG });
}

void GameConsoleLogger::_log_error(const String& function, const String& file, int32_t line, const String& code, const String& rationale, bool /*editor_notify*/, int32_t error_type, const TypedArray<Ref<ScriptBacktrace>>& /*script_backtraces*/)
{
	// ERROR_TYPE_WARNING is 1 in Logger::ErrorType.
	const bool warning = error_type == 1;
	const String what = rationale.is_empty() ? code : rationale;
	const String text = String(warning ? "WARNING: " : "ERROR: ") + what + "  (" + file.get_file() + ":" + String::num_int64(line) + " " + function + ")";
	std::lock_guard<std::mutex> lock(mutex);
	if (pending.size() > 2000) { return; }
	pending.push_back({ text, warning ? ConsoleManager::OUTPUT_WARNING : ConsoleManager::OUTPUT_ERROR });
}

std::vector<GameConsoleLogger::Entry> GameConsoleLogger::take()
{
	std::lock_guard<std::mutex> lock(mutex);
	std::vector<Entry> taken;
	taken.swap(pending);
	return taken;
}

// --- Console ------------------------------------------------------------------

GameConsole::GameConsole()
{
	log_panel = nullptr;
	log = nullptr;
	input_panel = nullptr;
	input = nullptr;
	suggest_panel = nullptr;
	suggest_label = nullptr;
	logger_added = false;
	mode = MODE_CLOSED;
	max_log_lines = 1000;
	window_start = 0;
	suggestion_index = -1;
	showing_history = false;
	changing_text = false;
	collecting_output = false;
}

void GameConsole::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	set_layer(128);
	set_process_mode(PROCESS_MODE_ALWAYS);

	// The full console: what was typed and what it printed.
	log_panel = memnew(PanelContainer);
	log_panel->set_name("Log");
	log_panel->add_theme_stylebox_override("panel", make_style(Color(0.03, 0.04, 0.05, 0.92)));
	add_child(log_panel);

	log = memnew(RichTextLabel);
	log->set_scroll_follow(true);
	log->set_selection_enabled(true);
	log->set_context_menu_enabled(true);
	log->set_use_bbcode(false);
	log->set_focus_mode(Control::FOCUS_NONE);
	log->add_theme_font_size_override("normal_font_size", 14);
	log_panel->add_child(log);

	// The command line, at the bottom of the screen or under the full console.
	input_panel = memnew(PanelContainer);
	input_panel->set_name("Input");
	input_panel->add_theme_stylebox_override("panel", make_style(Color(0.02, 0.03, 0.04, 0.95), 2, 1));
	add_child(input_panel);

	input = memnew(LineEdit);
	input->set_placeholder("command   (dumpcommands lists them all)");
	input->set_keep_editing_on_text_submit(true);
	input->set_context_menu_enabled(false);
	input->set_flat(true);
	input->add_theme_font_size_override("font_size", 15);
	input->connect("text_submitted", callable_mp(this, &GameConsole::_on_text_submitted));
	input->connect("text_changed", callable_mp(this, &GameConsole::_on_text_changed));
	input_panel->add_child(input);

	// Suggestions, history and the last output, next to the command line.
	suggest_panel = memnew(PanelContainer);
	suggest_panel->set_name("Suggestions");
	suggest_panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	suggest_panel->add_theme_stylebox_override("panel", make_style(Color(0.06, 0.07, 0.1, 0.95)));
	add_child(suggest_panel);

	suggest_label = memnew(RichTextLabel);
	suggest_label->set_use_bbcode(true);
	suggest_label->set_fit_content(true);
	suggest_label->set_scroll_active(false);
	suggest_label->set_autowrap_mode(TextServer::AUTOWRAP_OFF);
	suggest_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	suggest_label->add_theme_font_size_override("normal_font_size", 14);
	suggest_label->add_theme_font_size_override("bold_font_size", 14);
	suggest_panel->add_child(suggest_label);

	logger.instantiate();

	if (ConsoleManager* console = ConsoleManager::get_singleton())
	{
		console->connect("output_printed", callable_mp(this, &GameConsole::_on_output));
		console->connect("variable_changed", callable_mp(this, &GameConsole::_on_variable_changed));
		console->register_command("clear", callable_mp(this, &GameConsole::cmd_clear), "Clears the full console.", ConsoleManager::FLAG_RAW_ARGS);
		console->register_variable(ENGINE_LOG_VARIABLE, 0,
			"Engine output in the full console: 0 none, 1 warnings and errors, 2 everything. The log file and --log always have all of it.",
			ConsoleManager::FLAG_SAVED);
	}
	update_logger();

	load_history();
	add_line("GFGD console - " + DebugTools::describe_action(DebugTools::ACTION_CONSOLE)
		+ " opens the command line, again for the full console. dumpcommands lists every command.", ConsoleManager::OUTPUT_INFO);
	layout();
	set_process(true);
	set_process_input(true);
}

void GameConsole::_exit_tree()
{
	if (logger_added) { OS::get_singleton()->remove_logger(logger); }
	logger_added = false;
	if (ConsoleManager* console = ConsoleManager::get_singleton())
	{
		const Callable output = callable_mp(this, &GameConsole::_on_output);
		if (console->is_connected("output_printed", output)) { console->disconnect("output_printed", output); }
		const Callable changed = callable_mp(this, &GameConsole::_on_variable_changed);
		if (console->is_connected("variable_changed", changed)) { console->disconnect("variable_changed", changed); }
		console->unregister_command("clear");
		console->set_console_open(false);
	}
	if (mode != MODE_CLOSED) { PlayerInput::unblock_gameplay_input(); }
}

void GameConsole::update_logger()
{
	ConsoleManager* console = ConsoleManager::get_singleton();
	const int level = console != nullptr ? int(console->get_variable(ENGINE_LOG_VARIABLE, 0)) : 0;
	if (level <= 0)
	{
		if (logger_added) { OS::get_singleton()->remove_logger(logger); }
		logger_added = false;
		return;
	}
	logger->set_errors_only(level < 2);
	if (!logger_added)
	{
		OS::get_singleton()->add_logger(logger);
		logger_added = true;
	}
}

void GameConsole::_on_variable_changed(const String& name, const Variant& /*value*/)
{
	if (name.to_lower() == ENGINE_LOG_VARIABLE) { update_logger(); }
}

void GameConsole::_process(double /*delta*/)
{
	if (!logger_added) { return; }
	const std::vector<GameConsoleLogger::Entry> entries = logger->take();
	for (const GameConsoleLogger::Entry& entry : entries)
	{
		add_line(entry.text, entry.level);
	}
}

void GameConsole::add_line(const String& text, int level)
{
	if (log == nullptr) { return; }
	log->push_color(level_color(level));
	log->add_text(text);
	log->pop();
	log->add_text("\n");
	while (log->get_paragraph_count() > max_log_lines)
	{
		log->remove_paragraph(0);
	}
}

void GameConsole::clear_log()
{
	if (log != nullptr) { log->clear(); }
}

// --- Modes --------------------------------------------------------------------

void GameConsole::layout()
{
	if (input_panel == nullptr) { return; }
	const bool full = mode == MODE_FULL;

	log_panel->set_visible(full);
	input_panel->set_visible(mode != MODE_CLOSED);
	if (mode == MODE_CLOSED) { suggest_panel->set_visible(false); }

	log_panel->set_anchor(SIDE_LEFT, 0.0);
	log_panel->set_anchor(SIDE_RIGHT, 1.0);
	log_panel->set_anchor(SIDE_TOP, 0.0);
	log_panel->set_anchor(SIDE_BOTTOM, FULL_FRACTION);
	log_panel->set_offset(SIDE_LEFT, 0);
	log_panel->set_offset(SIDE_RIGHT, 0);
	log_panel->set_offset(SIDE_TOP, 0);
	log_panel->set_offset(SIDE_BOTTOM, 0);

	// The line sits at the very bottom on its own, or right under the full console.
	const float line_anchor = full ? FULL_FRACTION : 1.0f;
	input_panel->set_anchor(SIDE_LEFT, 0.0);
	input_panel->set_anchor(SIDE_RIGHT, 1.0);
	input_panel->set_anchor(SIDE_TOP, line_anchor);
	input_panel->set_anchor(SIDE_BOTTOM, line_anchor);
	input_panel->set_offset(SIDE_LEFT, 0);
	input_panel->set_offset(SIDE_RIGHT, 0);
	input_panel->set_offset(SIDE_TOP, full ? 0.0f : -BAR_HEIGHT);
	input_panel->set_offset(SIDE_BOTTOM, full ? BAR_HEIGHT : 0.0f);

	// Suggestions open away from the screen edge: above the bottom line, below
	// the full console's line.
	suggest_panel->set_anchor(SIDE_LEFT, 0.0);
	suggest_panel->set_anchor(SIDE_RIGHT, 0.75);
	suggest_panel->set_anchor(SIDE_TOP, line_anchor);
	suggest_panel->set_anchor(SIDE_BOTTOM, line_anchor);
	suggest_panel->set_offset(SIDE_LEFT, 0);
	suggest_panel->set_offset(SIDE_RIGHT, 0);
	const float edge = full ? BAR_HEIGHT : -BAR_HEIGHT;
	suggest_panel->set_offset(SIDE_TOP, edge);
	suggest_panel->set_offset(SIDE_BOTTOM, edge);
	suggest_panel->set_v_grow_direction(full ? Control::GROW_DIRECTION_END : Control::GROW_DIRECTION_BEGIN);

	render_suggestions();
}

void GameConsole::set_mode(Mode value)
{
	if (mode == value || input_panel == nullptr) { return; }
	const bool was_open = mode != MODE_CLOSED;
	mode = value;
	const bool now_open = mode != MODE_CLOSED;

	if (ConsoleManager* console = ConsoleManager::get_singleton()) { console->set_console_open(now_open); }
	// The mouse is left as it is - the console is driven from the keyboard -
	// and _input keeps every mouse event from the game while it is open.
	if (now_open && !was_open)
	{
		PlayerInput::block_gameplay_input();
		last_output.clear();
		hide_suggestions();
	}
	else if (!now_open && was_open)
	{
		PlayerInput::unblock_gameplay_input();
		input->release_focus();
	}

	layout();
	if (now_open) { input->grab_focus(); }
}

void GameConsole::_input(const Ref<InputEvent>& event)
{
	if (DebugTools::is_action_pressed(event, DebugTools::ACTION_CONSOLE))
	{
		set_mode(Mode((int(mode) + 1) % 3));
		// Handled, so the key's own character never lands in the line.
		get_viewport()->set_input_as_handled();
		return;
	}
	if (mode == MODE_CLOSED) { return; }

	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed())
	{
		// "Away from the line": up when the list is above it, down when below.
		const int away = mode == MODE_BAR ? 1 : -1;
		switch (key->get_keycode())
		{
			case KEY_ESCAPE:
				set_mode(MODE_CLOSED);
				get_viewport()->set_input_as_handled();
				return;
			case KEY_UP:
				if (suggestions.is_empty()) { show_history(); }
				else { move_selection(away); }
				get_viewport()->set_input_as_handled();
				return;
			case KEY_DOWN:
				if (suggestions.is_empty() && mode == MODE_FULL) { show_history(); }
				else { move_selection(-away); }
				get_viewport()->set_input_as_handled();
				return;
			case KEY_TAB:
				complete();
				get_viewport()->set_input_as_handled();
				return;
			case KEY_PAGEUP:
			case KEY_PAGEDOWN:
			{
				VScrollBar* bar = log->get_v_scroll_bar();
				const double page = bar->get_page() * 0.8;
				bar->set_value(bar->get_value() + (key->get_keycode() == KEY_PAGEUP ? -page : page));
				get_viewport()->set_input_as_handled();
				return;
			}
			default:
				break;
		}
	}

	// No mouse event reaches the game while the console is open: motion would
	// turn its camera, and a click or a wheel step past the end of the log
	// would reach whatever captures the mouse on a click.
	Ref<InputEventMouse> mouse = event;
	if (mouse.is_null()) { return; }

	Ref<InputEventMouseButton> button = mouse;
	if (button.is_valid() && button->is_pressed() && mode == MODE_FULL
		&& (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP || button->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN))
	{
		// Scrolled here, so it works whether or not the cursor is over the log
		// (or shown at all).
		VScrollBar* bar = log->get_v_scroll_bar();
		const double step = bar->get_page() * 0.15 * button->get_factor();
		bar->set_value(bar->get_value() + (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP ? -step : step));
		get_viewport()->set_input_as_handled();
		return;
	}

	// A visible cursor over the console itself may still select log text or
	// place the caret; everything else stops here.
	const bool cursor_visible = Input::get_singleton()->get_mouse_mode() == Input::MOUSE_MODE_VISIBLE;
	const bool over_console = input_panel->get_global_rect().has_point(mouse->get_position())
		|| (log_panel->is_visible() && log_panel->get_global_rect().has_point(mouse->get_position()));
	if (!(cursor_visible && over_console)) { get_viewport()->set_input_as_handled(); }
}

// --- Suggestions --------------------------------------------------------------

void GameConsole::set_input_text(const String& text)
{
	changing_text = true;
	input->set_text(text);
	input->set_caret_column(text.length());
	changing_text = false;
}

void GameConsole::hide_suggestions()
{
	suggestions.clear();
	suggestion_index = -1;
	window_start = 0;
	showing_history = false;
	if (suggest_label != nullptr) { suggest_label->clear(); }
	if (suggest_panel != nullptr) { suggest_panel->set_visible(false); }
}

void GameConsole::show_lines(const PackedStringArray& lines)
{
	suggestions.clear();
	suggestion_index = -1;
	showing_history = false;
	if (lines.is_empty())
	{
		hide_suggestions();
		return;
	}
	for (int i = 0; i < lines.size(); i++)
	{
		Suggestion line;
		line.display = lines[i];
		suggestions.push_back(line);
	}
	render_suggestions();
	// Display only: nothing to pick.
	suggestions.clear();
}

void GameConsole::show_completions(const String& text)
{
	ConsoleManager* console = ConsoleManager::get_singleton();
	const String trimmed = text.strip_edges(true, false);
	if (console == nullptr || trimmed.is_empty())
	{
		hide_suggestions();
		return;
	}

	const String word = trimmed.get_slice(" ", 0);
	auto format = [](const Dictionary& info) -> String
	{
		String line = String("[color=") + COLOR_NAME + "][b]" + escape(info.get("name", "")) + "[/b][/color]";
		const String usage = info.get("usage", String());
		if (String(info.get("kind", "")) == "variable")
		{
			line += String(" [color=") + COLOR_USAGE + "]= " + escape(String(info.get("value", ""))) + "[/color]";
		}
		else if (!usage.is_empty())
		{
			line += String(" [color=") + COLOR_USAGE + "]" + escape(usage) + "[/color]";
		}
		const PackedStringArray tags = info.get("tags", PackedStringArray());
		if (!tags.is_empty()) { line += String("  [color=") + COLOR_TAG + "]" + String(", ").join(tags) + "[/color]"; }
		const String help = info.get("help", String());
		if (!help.is_empty()) { line += String("   [color=") + COLOR_HELP + "]" + escape(help) + "[/color]"; }
		return line;
	};

	// Typing the arguments: the one command's usage and description.
	if (trimmed.contains(" "))
	{
		const Dictionary info = console->get_info(word);
		PackedStringArray lines;
		if (!info.is_empty()) { lines.push_back(format(info)); }
		show_lines(lines);
		return;
	}

	const PackedStringArray matches = console->get_completions(word);
	suggestions.clear();
	showing_history = false;
	// All of them: the list shows MAX_SUGGESTIONS at a time and scrolls with the selection.
	for (int i = 0; i < matches.size(); i++)
	{
		Suggestion suggestion;
		suggestion.insert = matches[i];
		suggestion.display = format(console->get_info(matches[i]));
		suggestions.push_back(suggestion);
	}
	suggestion_index = -1;
	window_start = 0;
	if (suggestions.is_empty()) { hide_suggestions(); }
	else { render_suggestions(); }
}

void GameConsole::show_history()
{
	if (history.is_empty()) { return; }
	suggestions.clear();
	showing_history = true;
	// Newest first, so the first step lands on the last line run.
	for (int i = history.size() - 1; i >= 0; i--)
	{
		Suggestion entry;
		entry.insert = history[i];
		entry.display = escape(history[i]);
		suggestions.push_back(entry);
	}
	suggestion_index = 0;
	window_start = 0;
	set_input_text(suggestions[0].insert);
	render_suggestions();
}

void GameConsole::move_selection(int step)
{
	if (suggestions.is_empty()) { return; }

	// -1 is "nothing chosen", the line as typed; past either end it stops.
	const int index = CLAMP(suggestion_index + step, -1, suggestions.size() - 1);
	if (index == suggestion_index) { return; }
	suggestion_index = index;
	if (suggestion_index >= 0) { set_input_text(suggestions[suggestion_index].insert); }
	render_suggestions();
}

void GameConsole::complete()
{
	if (suggestions.is_empty())
	{
		show_completions(input->get_text());
		if (suggestions.is_empty()) { return; }
	}
	const int index = suggestion_index >= 0 ? suggestion_index : 0;
	const String chosen = suggestions[index].insert;
	if (chosen.is_empty()) { return; }

	// A command gets a space, ready for its arguments; a history line is whole already.
	const String text = showing_history ? chosen : chosen + String(" ");
	set_input_text(text);
	if (showing_history) { hide_suggestions(); }
	else { show_completions(text); }
}

void GameConsole::render_suggestions()
{
	if (suggest_panel == nullptr) { return; }
	if (suggestions.is_empty() || mode == MODE_CLOSED)
	{
		suggest_panel->set_visible(false);
		return;
	}

	// The window follows the selection, so every entry can be reached.
	if (suggestion_index < 0) { window_start = 0; }
	else if (suggestion_index < window_start) { window_start = suggestion_index; }
	else if (suggestion_index >= window_start + MAX_SUGGESTIONS) { window_start = suggestion_index - MAX_SUGGESTIONS + 1; }
	window_start = CLAMP(window_start, 0, MAX(0, suggestions.size() - MAX_SUGGESTIONS));
	const int window_end = MIN(window_start + MAX_SUGGESTIONS, suggestions.size());

	// Index 0 is the best match and sits next to the line: at the bottom of a
	// list above it, at the top of a list below it. Counts of what is scrolled
	// out of view go at either end.
	PackedStringArray lines;
	const String more_color = String("[color=") + COLOR_USAGE + "]";
	if (window_start > 0) { lines.push_back(more_color + String("... ") + String::num_int64(window_start) + " more[/color]"); }
	for (int i = window_start; i < window_end; i++)
	{
		const String line = i == suggestion_index
			? String("[bgcolor=") + COLOR_SELECTED + "]" + suggestions[i].display + "[/bgcolor]"
			: suggestions[i].display;
		lines.push_back(line);
	}
	if (window_end < suggestions.size()) { lines.push_back(more_color + String("... ") + String::num_int64(suggestions.size() - window_end) + " more[/color]"); }
	if (mode == MODE_BAR) { lines.reverse(); }
	suggest_label->clear();
	suggest_label->append_text(String("\n").join(lines));
	suggest_panel->set_visible(true);
	// Collapsed to the line's edge every time: the grow direction then sizes it
	// to the new content, so a shorter list shrinks instead of keeping the
	// height of the longest one.
	const float edge = mode == MODE_FULL ? BAR_HEIGHT : -BAR_HEIGHT;
	suggest_panel->set_offset(SIDE_TOP, edge);
	suggest_panel->set_offset(SIDE_BOTTOM, edge);
}

// --- Running ------------------------------------------------------------------

void GameConsole::submit(const String& text)
{
	const String line = text.strip_edges();
	set_input_text(String());
	hide_suggestions();
	if (line.is_empty()) { return; }

	add_line("> " + line, ConsoleManager::OUTPUT_COMMAND);
	if (history.is_empty() || history[history.size() - 1] != line)
	{
		history.push_back(line);
		while (history.size() > MAX_HISTORY) { history.remove_at(0); }
		save_history();
	}

	last_output.clear();
	if (ConsoleManager* console = ConsoleManager::get_singleton()) { console->execute(line); }
}

void GameConsole::_on_output(const String& text, int level)
{
	add_line(text, level);

	// On the bottom line there is no log to read: show what the command said
	// right above the line, until the next keystroke. Server commands answer a
	// moment later, which lands here the same way.
	if (mode == MODE_BAR && input != nullptr && input->get_text().is_empty() && !showing_history)
	{
		const Color color = level_color(level);
		last_output.push_back("[color=#" + color.to_html(false) + "]" + escape(text) + "[/color]");
		while (last_output.size() > MAX_BAR_OUTPUT) { last_output.remove_at(0); }
		PackedStringArray newest_first = last_output;
		newest_first.reverse();
		show_lines(newest_first);
	}
}

void GameConsole::_on_text_submitted(const String& text)
{
	submit(text);
}

void GameConsole::_on_text_changed(const String& text)
{
	if (changing_text) { return; }
	last_output.clear();
	show_completions(text);
}

void GameConsole::load_history()
{
	Ref<FileAccess> file = FileAccess::open(HISTORY_PATH, FileAccess::READ);
	if (file.is_null()) { return; }
	while (!file->eof_reached())
	{
		const String line = file->get_line().strip_edges();
		if (!line.is_empty()) { history.push_back(line); }
	}
	while (history.size() > MAX_HISTORY) { history.remove_at(0); }
}

void GameConsole::save_history()
{
	Ref<FileAccess> file = FileAccess::open(HISTORY_PATH, FileAccess::WRITE);
	if (file.is_null()) { return; }
	for (int i = 0; i < history.size(); i++) { file->store_line(history[i]); }
}

void GameConsole::cmd_clear(const PackedStringArray& /*args*/)
{
	clear_log();
}

void GameConsole::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_mode", "value"), &GameConsole::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &GameConsole::get_mode);
	ClassDB::bind_method(D_METHOD("set_open", "value"), &GameConsole::set_open);
	ClassDB::bind_method(D_METHOD("is_open"), &GameConsole::is_open);
	ClassDB::bind_method(D_METHOD("clear_log"), &GameConsole::clear_log);
	ClassDB::bind_method(D_METHOD("add_line", "text", "level"), &GameConsole::add_line);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Closed,Bar,Full"), "set_mode", "get_mode");

	BIND_ENUM_CONSTANT(MODE_CLOSED);
	BIND_ENUM_CONSTANT(MODE_BAR);
	BIND_ENUM_CONSTANT(MODE_FULL);
}
