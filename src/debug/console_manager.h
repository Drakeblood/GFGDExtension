#ifndef GFGD_CONSOLE_MANAGER_H
#define GFGD_CONSOLE_MANAGER_H

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace GFGD
{
class PlayerController;

// Console commands and console variables, with the CheatManager's methods as
// a third source of commands.
//
// An engine singleton: ConsoleManager.register_command(...) from any script.
// A line is "name arg arg ..."; arguments are separated by spaces, "quoted
// text" is one argument, and ';' separates commands on one line. Names are
// case-insensitive. A line resolves, in order, to:
//   1. a registered command - its Callable is called with the arguments
//      converted to the method's declared parameter types (int, float, bool,
//      String, Vector3 as "x,y,z", a resource as its res:// path, a GameplayTag
//      by name); extra words go into a trailing String parameter;
//   2. a console variable - "name" prints it, "name value" sets it;
//   3. a method of the player's CheatManager - always run on the server.
//
// FLAG_SERVER commands typed on a client are sent to the server and run there,
// as the client's PlayerController (get_executor()); whatever they print comes
// back to that client's console. FLAG_CHEAT ones run only while cheats are
// allowed: always in a debug build, otherwise when the server's "cheats"
// variable is on.
class ConsoleManager : public Object
{
	GDCLASS(ConsoleManager, Object)

public:
	enum CommandFlags
	{
		FLAG_NONE = 0,
		FLAG_CHEAT = 1,			// Only while cheats are allowed.
		FLAG_SERVER = 2,		// Runs on the server; a client forwards it.
		FLAG_SAVED = 4,			// Variables only: kept in user://gfgd_console.cfg.
		FLAG_RAW_ARGS = 8,		// Commands only: the Callable gets one PackedStringArray.
	};

	enum OutputLevel
	{
		OUTPUT_INFO = 0,
		OUTPUT_COMMAND = 1,		// The echoed "> line".
		OUTPUT_WARNING = 2,
		OUTPUT_ERROR = 3,
		OUTPUT_LOG = 4,			// Captured engine output (print, push_error...).
	};

private:
	struct Command
	{
		String name;
		Callable callable;
		String help;
		// Shown after the name; worked out from the method when empty.
		String usage;
		int flags = 0;
	};

	struct Variable
	{
		String name;
		Variant value;
		Variant default_value;
		String help;
		int flags = 0;
	};

	static ConsoleManager* singleton;

	HashMap<String, Command> commands;
	HashMap<String, Variable> variables;
	Ref<ConfigFile> saved_variables;

	// Set while the server runs a line a client sent; output goes back to it.
	ObjectID remote_sender_id;
	bool console_open;

public:
	ConsoleManager();

	static ConsoleManager* get_singleton() { return singleton; }
	static void create_singleton();
	static void destroy_singleton();

	void register_command(const String& name, const Callable& callable, const String& help = String(), int flags = 0);
	void unregister_command(const String& name);
	bool has_command(const String& name) const;
	// The arguments shown after a command's name, e.g. "<path> [count]" - for
	// a FLAG_RAW_ARGS command, whose parameters cannot be read off the method.
	void set_command_usage(const String& name, const String& usage);

	// Returns the value now in effect: the saved one for a FLAG_SAVED variable
	// that has one, else the default. Registering again keeps the current value.
	Variant register_variable(const String& name, const Variant& default_value, const String& help = String(), int flags = 0);
	void unregister_variable(const String& name);
	bool has_variable(const String& name) const;
	Variant get_variable(const String& name, const Variant& default_value = Variant()) const;
	// Converted to the type of the variable's default value.
	void set_variable(const String& name, const Variant& value);

	// Runs a line typed at this machine. False if any part of it failed.
	bool execute(const String& line);
	// Runs every line of a file; # and // start comments.
	bool execute_file(const String& path);

	// Names (commands, variables, cheats) starting with prefix, sorted.
	PackedStringArray get_completions(const String& prefix) const;
	PackedStringArray get_all_names() const;
	// "name <arg:type> [optional]  -  help", or empty for an unknown name.
	String describe(const String& name) const;
	// The same in parts: name, kind ("command", "variable" or "cheat"),
	// usage (the arguments), help, tags ("cheat", "server") and, for a
	// variable, value. Empty for an unknown name.
	Dictionary get_info(const String& name) const;

	void print_line(const String& text, int level = OUTPUT_INFO);
	void print_warning(const String& text) { print_line(text, OUTPUT_WARNING); }
	void print_error(const String& text) { print_line(text, OUTPUT_ERROR); }

	// The PlayerController the running command acts for: the sender while the
	// server runs a forwarded line, else this machine's player. May be null.
	PlayerController* get_executor() const;

	bool are_cheats_allowed() const;

	// Set by the console overlay; other debug overlays ignore their keys while it is open.
	bool is_console_open() const { return console_open; }
	void set_console_open(bool value) { console_open = value; }

	// Drops every command not bound to this object, and the saved-variable
	// file handle. Called by the World at shutdown, before scripts are freed.
	void release_script_references();

	// --- Called by DebugReplicator -------------------------------------------
	void execute_remote(const String& line, PlayerController* sender);
	void emit_output(const String& text, int level);

	static Vector<PackedStringArray> parse_line(const String& line);
	// A console word as a value of the given type. ok is false when it does not parse.
	static Variant convert_argument(const String& text, Variant::Type type, const StringName& class_name, bool& ok);
	static Variant guess_argument(const String& text);

protected:
	static void _bind_methods();

private:
	bool execute_tokens(const PackedStringArray& tokens);
	bool invoke(const Callable& callable, const String& name, const PackedStringArray& args, int flags);
	bool forward_to_server(const String& line);
	bool is_authority() const;
	PlayerController* get_local_controller() const;
	Dictionary find_method_info(Object* object, const StringName& method) const;
	String describe_method(const String& name, const Dictionary& method_info) const;
	String describe_arguments(const Dictionary& method_info) const;
	String find_cheat(const String& lowered) const;
	PackedStringArray get_cheat_names() const;
	void save_variable(const Variable& variable);
	void register_builtins();

	void cmd_dump_commands(const PackedStringArray& args);
	void cmd_echo(const PackedStringArray& args);
	void cmd_exec(const PackedStringArray& args);
	void cmd_slomo(const PackedStringArray& args);
	void cmd_quit(const PackedStringArray& args);
};
}

VARIANT_ENUM_CAST(GFGD::ConsoleManager::CommandFlags);
VARIANT_ENUM_CAST(GFGD::ConsoleManager::OutputLevel);

#endif
