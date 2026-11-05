#include "debug/console_manager.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "debug/cheat_manager.h"
#include "debug/debug_replicator.h"
#include "debug/debug_tools.h"
#include "framework/player_controller.h"
#include "framework/world.h"
#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tags_manager.h"

using namespace GFGD;

ConsoleManager* ConsoleManager::singleton = nullptr;

namespace
{
const char* SAVED_VARIABLES_PATH = "user://gfgd_console.cfg";
const char* SAVED_SECTION = "variables";

String type_label(const Dictionary& arg)
{
	const int type = arg.get("type", Variant::NIL);
	const String class_name = arg.get("class_name", String());
	if (type == Variant::OBJECT && !class_name.is_empty()) { return class_name; }
	if (type == Variant::NIL) { return "any"; }
	return Variant::get_type_name(Variant::Type(type));
}

PackedStringArray split_vector(const String& text)
{
	return text.replace("(", "").replace(")", "").replace(" ", "").split(",", false);
}
}

ConsoleManager::ConsoleManager()
{
	console_open = false;
}

void ConsoleManager::create_singleton()
{
	if (singleton != nullptr) { return; }
	singleton = memnew(ConsoleManager);
	singleton->register_builtins();
	Engine::get_singleton()->register_singleton("ConsoleManager", singleton);
}

void ConsoleManager::destroy_singleton()
{
	if (singleton == nullptr) { return; }
	Engine::get_singleton()->unregister_singleton("ConsoleManager");
	memdelete(singleton);
	singleton = nullptr;
}

// --- Registration -------------------------------------------------------------

void ConsoleManager::register_command(const String& name, const Callable& callable, const String& help, int flags)
{
	ERR_FAIL_COND_MSG(name.strip_edges().is_empty() || name.contains(" "), "ConsoleManager: a command name must be one word.");
	ERR_FAIL_COND_MSG(!callable.is_valid(), vformat("ConsoleManager: invalid Callable for command \"%s\".", name));

	Command command;
	command.name = name;
	command.callable = callable;
	command.help = help;
	command.flags = flags;
	commands[name.to_lower()] = command;
}

void ConsoleManager::unregister_command(const String& name)
{
	commands.erase(name.to_lower());
}

void ConsoleManager::release_script_references()
{
	Vector<String> doomed;
	for (const KeyValue<String, Command>& entry : commands)
	{
		if (entry.value.callable.get_object() != this) { doomed.push_back(entry.key); }
	}
	for (const String& key : doomed) { commands.erase(key); }

	// Script values in variables (an Object, a script-made Resource) too.
	for (KeyValue<String, Variable>& entry : variables)
	{
		if (entry.value.value.get_type() == Variant::OBJECT) { entry.value.value = Variant(); }
		if (entry.value.default_value.get_type() == Variant::OBJECT) { entry.value.default_value = Variant(); }
	}
	saved_variables.unref();
}

void ConsoleManager::set_command_usage(const String& name, const String& usage)
{
	if (HashMap<String, Command>::Iterator found = commands.find(name.to_lower())) { found->value.usage = usage; }
}

bool ConsoleManager::has_command(const String& name) const
{
	return commands.has(name.to_lower());
}

Variant ConsoleManager::register_variable(const String& name, const Variant& default_value, const String& help, int flags)
{
	ERR_FAIL_COND_V_MSG(name.strip_edges().is_empty() || name.contains(" "), Variant(), "ConsoleManager: a variable name must be one word.");

	const String key = name.to_lower();
	if (HashMap<String, Variable>::Iterator existing = variables.find(key))
	{
		existing->value.help = help;
		existing->value.flags = flags;
		existing->value.default_value = default_value;
		return existing->value.value;
	}

	Variable variable;
	variable.name = name;
	variable.default_value = default_value;
	variable.value = default_value;
	variable.help = help;
	variable.flags = flags;

	if (flags & FLAG_SAVED)
	{
		if (saved_variables.is_null())
		{
			saved_variables.instantiate();
			saved_variables->load(SAVED_VARIABLES_PATH);
		}
		if (saved_variables->has_section_key(SAVED_SECTION, key))
		{
			const Variant saved = saved_variables->get_value(SAVED_SECTION, key);
			if (default_value.get_type() == Variant::NIL || saved.get_type() == default_value.get_type())
			{
				variable.value = saved;
			}
		}
	}

	variables[key] = variable;
	return variable.value;
}

void ConsoleManager::unregister_variable(const String& name)
{
	variables.erase(name.to_lower());
}

bool ConsoleManager::has_variable(const String& name) const
{
	return variables.has(name.to_lower());
}

Variant ConsoleManager::get_variable(const String& name, const Variant& default_value) const
{
	HashMap<String, Variable>::ConstIterator found = variables.find(name.to_lower());
	return found != variables.end() ? found->value.value : default_value;
}

void ConsoleManager::set_variable(const String& name, const Variant& value)
{
	HashMap<String, Variable>::Iterator found = variables.find(name.to_lower());
	ERR_FAIL_COND_MSG(found == variables.end(), vformat("ConsoleManager: no variable \"%s\".", name));

	Variable& variable = found->value;
	Variant converted = value;
	const Variant::Type type = variable.default_value.get_type();
	if (type != Variant::NIL && value.get_type() != type)
	{
		bool ok = false;
		converted = convert_argument(String(value), type, StringName(), ok);
		ERR_FAIL_COND_MSG(!ok, vformat("ConsoleManager: \"%s\" is not a valid %s for %s.", String(value), Variant::get_type_name(type), variable.name));
	}

	variable.value = converted;
	if (variable.flags & FLAG_SAVED) { save_variable(variable); }
	emit_signal("variable_changed", variable.name, converted);
}

void ConsoleManager::save_variable(const Variable& variable)
{
	if (saved_variables.is_null())
	{
		saved_variables.instantiate();
		saved_variables->load(SAVED_VARIABLES_PATH);
	}
	saved_variables->set_value(SAVED_SECTION, variable.name.to_lower(), variable.value);
	saved_variables->save(SAVED_VARIABLES_PATH);
}

// --- Parsing ------------------------------------------------------------------

Vector<PackedStringArray> ConsoleManager::parse_line(const String& line)
{
	Vector<PackedStringArray> statements;
	PackedStringArray tokens;
	String current;
	bool in_quotes = false;
	bool has_token = false;

	for (int i = 0; i < line.length(); i++)
	{
		const char32_t c = line[i];
		if (c == '\\' && in_quotes && i + 1 < line.length() && line[i + 1] == '"')
		{
			current += "\"";
			i++;
			continue;
		}
		if (c == '"')
		{
			in_quotes = !in_quotes;
			has_token = true;
			continue;
		}
		if (!in_quotes && (c == ' ' || c == '\t'))
		{
			if (has_token) { tokens.push_back(current); }
			current = String();
			has_token = false;
			continue;
		}
		if (!in_quotes && c == ';')
		{
			if (has_token) { tokens.push_back(current); }
			if (!tokens.is_empty()) { statements.push_back(tokens); }
			tokens = PackedStringArray();
			current = String();
			has_token = false;
			continue;
		}
		current += String::chr(c);
		has_token = true;
	}
	if (has_token) { tokens.push_back(current); }
	if (!tokens.is_empty()) { statements.push_back(tokens); }
	return statements;
}

Variant ConsoleManager::guess_argument(const String& text)
{
	const String lower = text.to_lower();
	if (lower == "true") { return true; }
	if (lower == "false") { return false; }
	if (text.is_valid_int()) { return text.to_int(); }
	if (text.is_valid_float()) { return text.to_float(); }
	return text;
}

Variant ConsoleManager::convert_argument(const String& text, Variant::Type type, const StringName& class_name, bool& ok)
{
	ok = true;
	const String lower = text.to_lower();
	switch (type)
	{
		case Variant::NIL:
			return guess_argument(text);
		case Variant::BOOL:
			if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") { return true; }
			if (lower == "0" || lower == "false" || lower == "off" || lower == "no") { return false; }
			break;
		case Variant::INT:
			if (text.is_valid_int()) { return text.to_int(); }
			break;
		case Variant::FLOAT:
			if (text.is_valid_float()) { return text.to_float(); }
			break;
		case Variant::STRING:
			return text;
		case Variant::STRING_NAME:
			return StringName(text);
		case Variant::NODE_PATH:
			return NodePath(text);
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		{
			const PackedStringArray parts = split_vector(text);
			if (parts.size() != 2) { break; }
			const Vector2 v(parts[0].to_float(), parts[1].to_float());
			return type == Variant::VECTOR2 ? Variant(v) : Variant(Vector2i(v));
		}
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		{
			const PackedStringArray parts = split_vector(text);
			if (parts.size() != 3) { break; }
			const Vector3 v(parts[0].to_float(), parts[1].to_float(), parts[2].to_float());
			return type == Variant::VECTOR3 ? Variant(v) : Variant(Vector3i(v));
		}
		case Variant::COLOR:
			if (Color::html_is_valid(text)) { return Color::html(text); }
			if (split_vector(text).size() >= 3)
			{
				const PackedStringArray parts = split_vector(text);
				return Color(parts[0].to_float(), parts[1].to_float(), parts[2].to_float(), parts.size() > 3 ? parts[3].to_float() : 1.0f);
			}
			return Color::from_string(text, Color(1, 0, 1));
		case Variant::PACKED_STRING_ARRAY:
			return text.split(",", false);
		case Variant::OBJECT:
		{
			if (class_name == StringName("GameplayTag"))
			{
				GameplayTagsManager* tags = GameplayTagsManager::get_singleton();
				Ref<GameplayTag> tag = tags->get_tag(StringName(text));
				if (tag.is_null()) { tag = tags->request_tag(StringName(text)); }
				return tag;
			}
			if (text.begins_with("res://") || text.begins_with("user://") || text.begins_with("uid://"))
			{
				Ref<Resource> resource = ResourceLoader::get_singleton()->load(text);
				if (resource.is_valid()) { return resource; }
				break;
			}
			SceneTree* tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
			if (tree != nullptr && tree->get_root() != nullptr)
			{
				Node* node = tree->get_root()->get_node_or_null(NodePath(text));
				if (node != nullptr) { return node; }
			}
			break;
		}
		default:
		{
			const Variant parsed = UtilityFunctions::str_to_var(text);
			if (parsed.get_type() == type) { return parsed; }
			break;
		}
	}
	ok = false;
	return Variant();
}

// --- Execution ----------------------------------------------------------------

bool ConsoleManager::execute(const String& line)
{
	const Vector<PackedStringArray> statements = parse_line(line);
	bool success = true;
	for (const PackedStringArray& tokens : statements)
	{
		success = execute_tokens(tokens) && success;
	}
	return success;
}

bool ConsoleManager::execute_file(const String& path)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null())
	{
		print_error("exec: cannot open " + path);
		return false;
	}
	bool success = true;
	while (!file->eof_reached())
	{
		const String line = file->get_line().strip_edges();
		if (line.is_empty() || line.begins_with("#") || line.begins_with("//")) { continue; }
		success = execute(line) && success;
	}
	return success;
}

void ConsoleManager::execute_remote(const String& line, PlayerController* sender)
{
	const ObjectID previous = remote_sender_id;
	remote_sender_id = sender != nullptr ? sender->get_instance_id() : ObjectID();
	execute(line);
	remote_sender_id = previous;
}

bool ConsoleManager::execute_tokens(const PackedStringArray& tokens)
{
	if (tokens.is_empty()) { return true; }

	const String name = tokens[0];
	const String key = name.to_lower();
	const PackedStringArray args = tokens.slice(1);
	const bool remote = remote_sender_id.is_valid();

	String line = name;
	for (int i = 0; i < args.size(); i++)
	{
		line += args[i].contains(" ") ? " \"" + args[i].replace("\"", "\\\"") + "\"" : " " + args[i];
	}

	if (HashMap<String, Command>::Iterator found = commands.find(key))
	{
		const Command command = found->value;
		if (remote && !(command.flags & FLAG_SERVER))
		{
			print_error(command.name + String(" is not a server command."));
			return false;
		}
		if ((command.flags & FLAG_SERVER) && !remote && !is_authority())
		{
			return forward_to_server(line);
		}
		if ((command.flags & FLAG_CHEAT) && !are_cheats_allowed())
		{
			print_error(command.name + String(" is a cheat; cheats are off (set \"cheats 1\" on the server)."));
			return false;
		}
		return invoke(command.callable, command.name, args, command.flags);
	}

	if (HashMap<String, Variable>::Iterator found = variables.find(key))
	{
		if (remote)
		{
			print_error("Console variables are local to each machine.");
			return false;
		}
		Variable& variable = found->value;
		if (args.is_empty())
		{
			String text = variable.name + String(" = ") + String(variable.value);
			if (!variable.help.is_empty()) { text += "   - " + variable.help; }
			print_line(text);
			return true;
		}
		bool ok = false;
		const String joined = String(" ").join(args);
		const Variant value = convert_argument(joined, variable.default_value.get_type(), StringName(), ok);
		if (!ok)
		{
			print_error(vformat("%s expects a %s.", variable.name, Variant::get_type_name(variable.default_value.get_type())));
			return false;
		}
		set_variable(variable.name, value);
		print_line(variable.name + " = " + String(get_variable(variable.name)));
		return true;
	}

	const String cheat = find_cheat(key);
	if (!cheat.is_empty())
	{
		PlayerController* pc = get_executor();
		if (!remote && !is_authority())
		{
			return forward_to_server(line);
		}
		if (pc == nullptr)
		{
			print_error(cheat + String(" needs a PlayerController."));
			return false;
		}
		if (!are_cheats_allowed())
		{
			print_error("Cheats are off (set \"cheats 1\" on the server).");
			return false;
		}
		Ref<CheatManager> cheat_manager = pc->get_cheat_manager();
		if (cheat_manager.is_null()) { return false; }
		return invoke(Callable(cheat_manager.ptr(), StringName(cheat)), cheat, args, FLAG_CHEAT | FLAG_SERVER);
	}

	print_error("Unknown command: " + name + "   (dumpcommands lists everything)");
	return false;
}

bool ConsoleManager::invoke(const Callable& callable, const String& name, const PackedStringArray& args, int flags)
{
	if (!callable.is_valid())
	{
		print_error(name + String(": the command's target is gone."));
		return false;
	}

	Variant result;
	if (flags & FLAG_RAW_ARGS)
	{
		result = callable.call(args);
	}
	else
	{
		Array call_args;
		const Dictionary info = find_method_info(callable.get_object(), callable.get_method());
		if (!info.is_empty())
		{
			const Array params = info.get("args", Array());
			const Array defaults = info.get("default_args", Array());
			const int required = params.size() - defaults.size();

			if (params.size() == 1 && int(Dictionary(params[0]).get("type", Variant::NIL)) == Variant::PACKED_STRING_ARRAY)
			{
				call_args.push_back(args);
			}
			else
			{
				if (args.size() < required || (args.size() > params.size() && params.is_empty()))
				{
					print_error("usage: " + describe_method(name, info));
					return false;
				}
				for (int i = 0; i < params.size() && i < args.size(); i++)
				{
					const Dictionary param = params[i];
					const Variant::Type type = Variant::Type(int(param.get("type", Variant::NIL)));
					String text = args[i];
					// Extra words fold into a trailing String, so "say hello there" works unquoted.
					if (i == params.size() - 1 && args.size() > params.size() && (type == Variant::STRING || type == Variant::STRING_NAME))
					{
						text = String(" ").join(args.slice(i));
					}
					bool ok = false;
					const Variant value = convert_argument(text, type, param.get("class_name", StringName()), ok);
					if (!ok)
					{
						print_error(vformat("%s: \"%s\" is not a valid %s for %s.", name, text, type_label(param), String(param.get("name", ""))));
						print_line("usage: " + describe_method(name, info));
						return false;
					}
					call_args.push_back(value);
				}
				if (args.size() > params.size() && !params.is_empty())
				{
					const Variant::Type last = Variant::Type(int(Dictionary(params[params.size() - 1]).get("type", Variant::NIL)));
					if (last != Variant::STRING && last != Variant::STRING_NAME)
					{
						print_error("usage: " + describe_method(name, info));
						return false;
					}
				}
			}
		}
		else
		{
			for (int i = 0; i < args.size(); i++) { call_args.push_back(guess_argument(args[i])); }
		}
		result = callable.callv(call_args);
	}

	if (result.get_type() != Variant::NIL)
	{
		print_line(String(result));
	}
	return true;
}

bool ConsoleManager::forward_to_server(const String& line)
{
	PlayerController* pc = get_local_controller();
	DebugReplicator* replicator = pc != nullptr ? DebugReplicator::find_for(pc) : nullptr;
	if (replicator == nullptr)
	{
		print_error("Not connected to a server that can run this (no PlayerController).");
		return false;
	}
	replicator->request_exec(line);
	return true;
}

bool ConsoleManager::is_authority() const
{
	World* world = Object::cast_to<World>(Engine::get_singleton()->get_main_loop());
	return world == nullptr || world->has_authority();
}

PlayerController* ConsoleManager::get_local_controller() const
{
	return DebugTools::get_local_player_controller();
}

PlayerController* ConsoleManager::get_executor() const
{
	if (remote_sender_id.is_valid())
	{
		return Object::cast_to<PlayerController>(ObjectDB::get_instance(remote_sender_id));
	}
	return get_local_controller();
}

bool ConsoleManager::are_cheats_allowed() const
{
	if (OS::get_singleton()->is_debug_build()) { return true; }
	return bool(get_variable("cheats", false));
}

// --- Output -------------------------------------------------------------------

void ConsoleManager::print_line(const String& text, int level)
{
	if (remote_sender_id.is_valid())
	{
		PlayerController* sender = Object::cast_to<PlayerController>(ObjectDB::get_instance(remote_sender_id));
		DebugReplicator* replicator = sender != nullptr ? DebugReplicator::find_for(sender) : nullptr;
		if (replicator != nullptr && !sender->is_local_player_controller())
		{
			replicator->send_output(text, level);
			return;
		}
	}
	emit_output(text, level);
}

void ConsoleManager::emit_output(const String& text, int level)
{
	// Nothing listening - a dedicated server, or a game without the overlay - so
	// the output still has somewhere to go.
	if (get_signal_connection_list("output_printed").is_empty())
	{
		if (level == OUTPUT_ERROR) { UtilityFunctions::printerr(text); }
		else if (level != OUTPUT_LOG) { UtilityFunctions::print(text); }
		return;
	}
	emit_signal("output_printed", text, level);
}

// --- Discovery ----------------------------------------------------------------

Dictionary ConsoleManager::find_method_info(Object* object, const StringName& method) const
{
	if (object == nullptr || method == StringName()) { return Dictionary(); }
	const TypedArray<Dictionary> methods = object->get_method_list();
	for (int i = methods.size() - 1; i >= 0; i--)
	{
		const Dictionary info = methods[i];
		if (StringName(info.get("name", StringName())) == method) { return info; }
	}
	return Dictionary();
}

String ConsoleManager::describe_method(const String& name, const Dictionary& method_info) const
{
	const String arguments = describe_arguments(method_info);
	return arguments.is_empty() ? name : name + String(" ") + arguments;
}

String ConsoleManager::describe_arguments(const Dictionary& method_info) const
{
	PackedStringArray parts;
	const Array params = method_info.get("args", Array());
	const Array defaults = method_info.get("default_args", Array());
	const int required = params.size() - defaults.size();
	for (int i = 0; i < params.size(); i++)
	{
		const Dictionary param = params[i];
		const String part = String(param.get("name", "arg")) + ":" + type_label(param);
		parts.push_back(i < required ? "<" + part + ">" : "[" + part + "=" + String(defaults[i - required]) + "]");
	}
	return String(" ").join(parts);
}

PackedStringArray ConsoleManager::get_cheat_names() const
{
	PlayerController* pc = get_executor();
	Ref<CheatManager> cheat_manager = pc != nullptr ? pc->get_cheat_manager() : Ref<CheatManager>();
	return cheat_manager.is_valid() ? cheat_manager->get_command_names() : PackedStringArray();
}

String ConsoleManager::find_cheat(const String& lowered) const
{
	const PackedStringArray cheats = get_cheat_names();
	for (int i = 0; i < cheats.size(); i++)
	{
		if (cheats[i].to_lower() == lowered) { return cheats[i]; }
	}
	return String();
}

PackedStringArray ConsoleManager::get_all_names() const
{
	PackedStringArray names;
	for (const KeyValue<String, Command>& entry : commands) { names.push_back(entry.value.name); }
	for (const KeyValue<String, Variable>& entry : variables) { names.push_back(entry.value.name); }
	const PackedStringArray cheats = get_cheat_names();
	for (int i = 0; i < cheats.size(); i++)
	{
		if (!commands.has(cheats[i].to_lower())) { names.push_back(cheats[i]); }
	}
	names.sort();
	return names;
}

PackedStringArray ConsoleManager::get_completions(const String& prefix) const
{
	const String lowered = prefix.to_lower();
	const PackedStringArray all = get_all_names();
	PackedStringArray matches;
	PackedStringArray contains;
	for (int i = 0; i < all.size(); i++)
	{
		const String candidate = all[i].to_lower();
		if (candidate.begins_with(lowered)) { matches.push_back(all[i]); }
		else if (!lowered.is_empty() && candidate.contains(lowered)) { contains.push_back(all[i]); }
	}
	// Prefix matches first, then anything that merely contains the text.
	matches.append_array(contains);
	return matches;
}

Dictionary ConsoleManager::get_info(const String& name) const
{
	Dictionary info;
	const String key = name.to_lower();
	if (HashMap<String, Command>::ConstIterator found = commands.find(key))
	{
		const Command& command = found->value;
		String usage = command.usage;
		if (usage.is_empty() && !(command.flags & FLAG_RAW_ARGS))
		{
			usage = describe_arguments(find_method_info(command.callable.get_object(), command.callable.get_method()));
		}
		PackedStringArray tags;
		if (command.flags & FLAG_CHEAT) { tags.push_back("cheat"); }
		if (command.flags & FLAG_SERVER) { tags.push_back("server"); }
		info["name"] = command.name;
		info["kind"] = "command";
		info["usage"] = usage;
		info["help"] = command.help;
		info["tags"] = tags;
		return info;
	}
	if (HashMap<String, Variable>::ConstIterator found = variables.find(key))
	{
		const Variable& variable = found->value;
		info["name"] = variable.name;
		info["kind"] = "variable";
		info["usage"] = "[" + String(Variant::get_type_name(variable.default_value.get_type())) + "]";
		info["help"] = variable.help;
		info["tags"] = PackedStringArray();
		info["value"] = variable.value;
		return info;
	}
	const String cheat = find_cheat(key);
	if (!cheat.is_empty())
	{
		PlayerController* pc = get_executor();
		Ref<CheatManager> cheat_manager = pc != nullptr ? pc->get_cheat_manager() : Ref<CheatManager>();
		PackedStringArray tags;
		tags.push_back("cheat");
		tags.push_back("server");
		info["name"] = cheat;
		info["kind"] = "cheat";
		info["usage"] = cheat_manager.is_valid() ? describe_arguments(find_method_info(cheat_manager.ptr(), StringName(cheat))) : String();
		info["help"] = cheat_manager.is_valid() ? cheat_manager->get_command_help(cheat) : String();
		info["tags"] = tags;
	}
	return info;
}

String ConsoleManager::describe(const String& name) const
{
	const Dictionary info = get_info(name);
	if (info.is_empty()) { return String(); }

	String text = info["name"];
	const String usage = info["usage"];
	if (String(info["kind"]) == "variable") { text += " = " + String(info["value"]); }
	else if (!usage.is_empty()) { text += " " + usage; }
	const PackedStringArray tags = info["tags"];
	if (!tags.is_empty()) { text += "  (" + String(", ").join(tags) + ")"; }
	const String help = info["help"];
	if (!help.is_empty()) { text += "  - " + help; }
	return text;
}

// --- Built-in commands --------------------------------------------------------

void ConsoleManager::register_builtins()
{
	register_command("dumpcommands", callable_mp(this, &ConsoleManager::cmd_dump_commands), "Prints every command, variable and cheat with its description; the text filters them.", FLAG_RAW_ARGS);
	set_command_usage("dumpcommands", "[filter]");
	register_command("echo", callable_mp(this, &ConsoleManager::cmd_echo), "Prints its arguments.", FLAG_RAW_ARGS);
	set_command_usage("echo", "<text>");
	register_command("exec", callable_mp(this, &ConsoleManager::cmd_exec), "Runs every line of a file (user://autoexec.cfg runs at startup).", FLAG_RAW_ARGS);
	set_command_usage("exec", "<path>");
	register_command("slomo", callable_mp(this, &ConsoleManager::cmd_slomo), "Slows down or speeds up time on this machine (1 = normal).", FLAG_RAW_ARGS | FLAG_CHEAT);
	set_command_usage("slomo", "[scale]");
	register_command("quit", callable_mp(this, &ConsoleManager::cmd_quit), "Quits the game.", FLAG_RAW_ARGS);
	register_variable("cheats", false, "Allows cheat commands in release builds (checked on the server). Debug builds always allow them.");
}

void ConsoleManager::cmd_dump_commands(const PackedStringArray& args)
{
	const String filter = args.is_empty() ? String() : String(" ").join(args).to_lower();
	const PackedStringArray names = get_all_names();
	int shown = 0;
	for (int i = 0; i < names.size(); i++)
	{
		const String text = describe(names[i]);
		if (!filter.is_empty() && !text.to_lower().contains(filter)) { continue; }
		print_line(text);
		shown++;
	}
	if (shown == 0) { print_line("Nothing matches \"" + filter + "\"."); }
}

void ConsoleManager::cmd_echo(const PackedStringArray& args)
{
	print_line(String(" ").join(args));
}

void ConsoleManager::cmd_exec(const PackedStringArray& args)
{
	if (args.is_empty())
	{
		print_error("usage: exec <path>");
		return;
	}
	execute_file(args[0]);
}

void ConsoleManager::cmd_slomo(const PackedStringArray& args)
{
	if (args.is_empty() || !args[0].is_valid_float())
	{
		print_line("slomo = " + String::num(Engine::get_singleton()->get_time_scale(), 2));
		return;
	}
	const double scale = CLAMP(args[0].to_float(), 0.01, 20.0);
	Engine::get_singleton()->set_time_scale(scale);
	if (!is_authority())
	{
		print_warning("slomo on a client only slows this machine - predicted movement will fight the server.");
	}
	print_line("slomo = " + String::num(scale, 2));
}

void ConsoleManager::cmd_quit(const PackedStringArray& /*args*/)
{
	SceneTree* tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (tree != nullptr) { tree->quit(); }
}

void ConsoleManager::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("register_command", "name", "callable", "help", "flags"), &ConsoleManager::register_command, DEFVAL(String()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("unregister_command", "name"), &ConsoleManager::unregister_command);
	ClassDB::bind_method(D_METHOD("has_command", "name"), &ConsoleManager::has_command);
	ClassDB::bind_method(D_METHOD("register_variable", "name", "default_value", "help", "flags"), &ConsoleManager::register_variable, DEFVAL(String()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("unregister_variable", "name"), &ConsoleManager::unregister_variable);
	ClassDB::bind_method(D_METHOD("has_variable", "name"), &ConsoleManager::has_variable);
	ClassDB::bind_method(D_METHOD("get_variable", "name", "default_value"), &ConsoleManager::get_variable, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_variable", "name", "value"), &ConsoleManager::set_variable);
	ClassDB::bind_method(D_METHOD("execute", "line"), &ConsoleManager::execute);
	ClassDB::bind_method(D_METHOD("execute_file", "path"), &ConsoleManager::execute_file);
	ClassDB::bind_method(D_METHOD("get_completions", "prefix"), &ConsoleManager::get_completions);
	ClassDB::bind_method(D_METHOD("get_all_names"), &ConsoleManager::get_all_names);
	ClassDB::bind_method(D_METHOD("describe", "name"), &ConsoleManager::describe);
	ClassDB::bind_method(D_METHOD("get_info", "name"), &ConsoleManager::get_info);
	ClassDB::bind_method(D_METHOD("set_command_usage", "name", "usage"), &ConsoleManager::set_command_usage);
	ClassDB::bind_method(D_METHOD("print_line", "text", "level"), &ConsoleManager::print_line, DEFVAL(OUTPUT_INFO));
	ClassDB::bind_method(D_METHOD("print_warning", "text"), &ConsoleManager::print_warning);
	ClassDB::bind_method(D_METHOD("print_error", "text"), &ConsoleManager::print_error);
	ClassDB::bind_method(D_METHOD("get_executor"), &ConsoleManager::get_executor);
	ClassDB::bind_method(D_METHOD("are_cheats_allowed"), &ConsoleManager::are_cheats_allowed);
	ClassDB::bind_method(D_METHOD("is_console_open"), &ConsoleManager::is_console_open);

	ADD_SIGNAL(MethodInfo("output_printed", PropertyInfo(Variant::STRING, "text"), PropertyInfo(Variant::INT, "level")));
	ADD_SIGNAL(MethodInfo("variable_changed", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));

	BIND_ENUM_CONSTANT(FLAG_NONE);
	BIND_ENUM_CONSTANT(FLAG_CHEAT);
	BIND_ENUM_CONSTANT(FLAG_SERVER);
	BIND_ENUM_CONSTANT(FLAG_SAVED);
	BIND_ENUM_CONSTANT(FLAG_RAW_ARGS);

	BIND_ENUM_CONSTANT(OUTPUT_INFO);
	BIND_ENUM_CONSTANT(OUTPUT_COMMAND);
	BIND_ENUM_CONSTANT(OUTPUT_WARNING);
	BIND_ENUM_CONSTANT(OUTPUT_ERROR);
	BIND_ENUM_CONSTANT(OUTPUT_LOG);
}
