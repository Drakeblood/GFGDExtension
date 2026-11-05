#ifndef GFGD_GAME_CONSOLE_H
#define GFGD_GAME_CONSOLE_H

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <mutex>
#include <vector>

using namespace godot;

namespace godot
{
class LineEdit;
class PanelContainer;
class RichTextLabel;
}

namespace GFGD
{
// Collects engine output (print, push_warning, push_error, script errors) for
// the console when the console.engine_log variable asks for it. Called from
// any thread, so it only queues; the console drains the queue on the main
// thread.
class GameConsoleLogger : public Logger
{
	GDCLASS(GameConsoleLogger, Logger)

public:
	struct Entry
	{
		String text;
		int level;
	};

private:
	std::mutex mutex;
	std::vector<Entry> pending;
	bool errors_only = true;

public:
	void set_errors_only(bool value) { errors_only = value; }

	virtual void _log_message(const String& message, bool error) override;
	virtual void _log_error(const String& function, const String& file, int32_t line, const String& code, const String& rationale, bool editor_notify, int32_t error_type, const TypedArray<Ref<ScriptBacktrace>>& script_backtraces) override;

	std::vector<Entry> take();

protected:
	static void _bind_methods() {}
};

// The in-game console, in two sizes. The console action (the key left of 1 by
// default) steps through them:
//
//   1st press  a command line at the bottom of the screen. Suggestions appear
//              above it as you type - every matching command, variable and
//              cheat with its arguments and description.
//   2nd press  the full console from the top: what was typed and what it
//              printed, with the command line under it.
//   3rd press  closed. Esc closes it from either size.
//
// Up/Down walk the suggestions, or the history (user://gfgd_console_history.txt)
// when the line is empty; Tab completes the chosen suggestion; Enter runs the
// line; PageUp/PageDown scroll the full console.
//
// The game's own log is not shown: it goes to the log file and to a terminal
// (start the game with --log for a window of its own). console.engine_log 1
// adds engine warnings and errors to the full console, 2 everything.
//
// While it is open the game sees no input: PlayerInput reports every action
// released and no mouse event gets through. The mouse is left as it was; the
// wheel scrolls the full console.
class GameConsole : public CanvasLayer
{
	GDCLASS(GameConsole, CanvasLayer)

public:
	enum Mode
	{
		MODE_CLOSED = 0,
		MODE_BAR = 1,
		MODE_FULL = 2,
	};

private:
	struct Suggestion
	{
		String insert;
		String display;
	};

	PanelContainer* log_panel;
	RichTextLabel* log;
	PanelContainer* input_panel;
	LineEdit* input;
	PanelContainer* suggest_panel;
	RichTextLabel* suggest_label;
	Ref<GameConsoleLogger> logger;
	bool logger_added;

	Mode mode;
	PackedStringArray history;
	int max_log_lines;

	Vector<Suggestion> suggestions;
	int suggestion_index;
	// First entry shown; the list scrolls to keep the selection in view.
	int window_start;
	bool showing_history;
	bool changing_text;
	PackedStringArray last_output;
	bool collecting_output;

public:
	GameConsole();

	virtual void _ready() override;
	virtual void _exit_tree() override;
	virtual void _process(double delta) override;
	virtual void _input(const Ref<InputEvent>& event) override;

	void set_mode(Mode value);
	Mode get_mode() const { return mode; }
	void set_open(bool value) { set_mode(value ? MODE_BAR : MODE_CLOSED); }
	bool is_open() const { return mode != MODE_CLOSED; }
	void clear_log();
	void add_line(const String& text, int level);

protected:
	static void _bind_methods();

private:
	void layout();
	void submit(const String& text);
	void move_selection(int step);
	void complete();
	void show_completions(const String& text);
	void show_history();
	void show_lines(const PackedStringArray& lines);
	void hide_suggestions();
	void render_suggestions();
	void set_input_text(const String& text);
	void update_logger();
	void load_history();
	void save_history();

	void _on_output(const String& text, int level);
	void _on_text_submitted(const String& text);
	void _on_text_changed(const String& text);
	void _on_variable_changed(const String& name, const Variant& value);
	void cmd_clear(const PackedStringArray& args);
};
}

VARIANT_ENUM_CAST(GFGD::GameConsole::Mode);

#endif
