#include "editor/gameplay_tag_editor_utils.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/templates/hash_set.hpp>

#include "gameplay_tags/gameplay_tags_manager.h"

using namespace godot;

namespace GFGD
{
namespace GameplayTagEditorUtils
{
namespace
{
// Used when the editor theme cannot be reached, or carries no error color under
// the name we ask for.
const Color FALLBACK_WARNING_COLOR = Color(1.0f, 0.47f, 0.42f);
}

String build_tooltip(const StringName& tag_name)
{
	const GameplayTagsManager* manager = GameplayTagsManager::get_singleton();

	String tooltip = String(tag_name);
	switch (manager->get_tag_source_kind(tag_name))
	{
		case GameplayTagsManager::TAG_SOURCE_TABLE:
			tooltip += vformat("\nDeclared in: %s", manager->get_tag_source_path(tag_name));
			break;
		case GameplayTagsManager::TAG_SOURCE_IMPLICIT:
			tooltip += "\nParent tag, implied by a longer registered tag.";
			break;
		case GameplayTagsManager::TAG_SOURCE_RUNTIME:
			tooltip += "\nNot declared in any gameplay tag table. Add it to a table, or fix the assets still referencing it.";
			break;
	}

	const String description = manager->get_tag_description(tag_name);
	if (!description.is_empty())
	{
		tooltip += vformat("\n\n%s", description);
	}

	return tooltip;
}

bool is_undeclared(const StringName& tag_name)
{
	return GameplayTagsManager::get_singleton()->get_tag_source_kind(tag_name) == GameplayTagsManager::TAG_SOURCE_RUNTIME;
}

Color get_warning_color()
{
	EditorInterface* editor_interface = EditorInterface::get_singleton();
	if (editor_interface == nullptr) { return FALLBACK_WARNING_COLOR; }

	const Ref<Theme> theme = editor_interface->get_editor_theme();
	if (theme.is_null() || !theme->has_color("error_color", "Editor")) { return FALLBACK_WARNING_COLOR; }

	return theme->get_color("error_color", "Editor");
}

PackedStringArray expand_with_ancestors(const PackedStringArray& tag_names, const String& filter)
{
	const String needle = filter.strip_edges().to_lower();

	HashSet<String> visible;
	for (int i = 0; i < tag_names.size(); i++)
	{
		const String tag_name = tag_names[i];
		if (!needle.is_empty() && !tag_name.to_lower().contains(needle)) { continue; }

		String current = tag_name;
		while (!current.is_empty() && !visible.has(current))
		{
			visible.insert(current);

			const int last_dot = current.rfind(".");
			if (last_dot == -1) { break; }
			current = current.substr(0, last_dot);
		}
	}

	PackedStringArray names;
	for (const String& name : visible)
	{
		names.push_back(name);
	}
	names.sort();

	return names;
}
}
}

#endif // TOOLS_ENABLED
