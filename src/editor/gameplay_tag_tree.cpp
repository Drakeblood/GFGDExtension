#include "editor/gameplay_tag_tree.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "editor/gameplay_tag_editor_utils.h"

using namespace godot;

namespace GFGD
{
GameplayTagTree::GameplayTagTree()
{
	filter_edit = memnew(LineEdit);
	filter_edit->set_placeholder("Filter tags");
	filter_edit->set_clear_button_enabled(true);
	filter_edit->connect("text_changed", callable_mp(this, &GameplayTagTree::_on_filter_changed));
	add_child(filter_edit);

	tree = memnew(Tree);
	tree->set_columns(1);
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_SINGLE);
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->connect("item_selected", callable_mp(this, &GameplayTagTree::_on_item_selected));
	tree->connect("item_edited", callable_mp(this, &GameplayTagTree::_on_item_edited));
	tree->connect("item_collapsed", callable_mp(this, &GameplayTagTree::_on_item_collapsed));
	add_child(tree);
}

void GameplayTagTree::set_mode(Mode value)
{
	if (mode == value) { return; }

	mode = value;
	rebuild();
}

void GameplayTagTree::set_tag_names(const PackedStringArray& value)
{
	tag_names = value;
	tag_names.sort();

	if (tag_names.size() <= AUTO_EXPAND_TAG_LIMIT)
	{
		for (int i = 0; i < tag_names.size(); i++)
		{
			expanded_tags.insert(tag_names[i]);
		}
	}

	rebuild();
}

void GameplayTagTree::set_selected_tag(const StringName& tag_name)
{
	selected_tag = tag_name;
	expand_ancestors_of(tag_name);
	rebuild();
}

void GameplayTagTree::set_checked_tags(const PackedStringArray& value)
{
	checked_tags.clear();
	for (int i = 0; i < value.size(); i++)
	{
		const StringName tag_name = value[i];
		checked_tags.insert(tag_name);
		expand_ancestors_of(tag_name);
	}

	rebuild();
}

void GameplayTagTree::focus_filter()
{
	filter_edit->grab_focus();
}

void GameplayTagTree::rebuild()
{
	// Guards the collapse and selection signals the rebuild itself provokes.
	rebuilding = true;

	tree->clear();
	TreeItem* root = tree->create_item();

	const bool filtering = !filter_edit->get_text().strip_edges().is_empty();
	const PackedStringArray visible_names = GameplayTagEditorUtils::expand_with_ancestors(tag_names, filter_edit->get_text());

	// Ancestors sort before their descendants ("A" < "A.B"), and every ancestor
	// is in the list, so one forward pass always finds the parent already built.
	HashMap<String, TreeItem*> items;
	for (int i = 0; i < visible_names.size(); i++)
	{
		const String tag_name = visible_names[i];
		const int last_dot = tag_name.rfind(".");

		TreeItem* parent = root;
		if (last_dot != -1)
		{
			TreeItem** found = items.getptr(tag_name.substr(0, last_dot));
			if (found != nullptr) { parent = *found; }
		}

		TreeItem* item = tree->create_item(parent);

		// set_cell_mode() resets the cell and wipes its text, so the mode has to
		// be settled before anything is written into it.
		if (mode == MODE_CHECK_MANY)
		{
			// Parent tags are pickable in their own right ("A.B" is a valid tag),
			// so every row gets a checkbox, not just the leaves.
			item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
			item->set_editable(0, true);
		}

		item->set_text(0, last_dot != -1 ? tag_name.substr(last_dot + 1) : tag_name);
		item->set_metadata(0, tag_name);
		// A filtered tree is always open - hiding a match behind a folded parent
		// would defeat the search.
		item->set_collapsed(!filtering && !expanded_tags.has(StringName(tag_name)));

		decorate_item(item, tag_name);
		items.insert(tag_name, item);

		// Only the single-tag mode selects a row. Tree highlights the relationship
		// lines of whatever is selected, and SELECT_SINGLE allows exactly one, so
		// in check mode there is no honest way to mark every ticked tag.
		if (mode == MODE_SELECT_ONE && tag_name == String(selected_tag))
		{
			item->select(0);
			tree->scroll_to_item(item);
		}
	}

	rebuilding = false;
}

void GameplayTagTree::expand_ancestors_of(const StringName& tag_name)
{
	String current = tag_name;
	while (true)
	{
		const int last_dot = current.rfind(".");
		if (last_dot == -1) { break; }

		current = current.substr(0, last_dot);
		expanded_tags.insert(current);
	}
}

void GameplayTagTree::decorate_item(TreeItem* item, const StringName& tag_name)
{
	if (mode == MODE_CHECK_MANY)
	{
		item->set_checked(0, checked_tags.has(tag_name));
	}

	item->set_tooltip_text(0, GameplayTagEditorUtils::build_tooltip(tag_name));
	if (GameplayTagEditorUtils::is_undeclared(tag_name))
	{
		item->set_custom_color(0, GameplayTagEditorUtils::get_warning_color());
	}
}

void GameplayTagTree::_on_filter_changed(const String& new_text)
{
	rebuild();
}

void GameplayTagTree::_on_item_selected()
{
	if (rebuilding || mode != MODE_SELECT_ONE) { return; }

	TreeItem* item = tree->get_selected();
	if (item == nullptr) { return; }

	selected_tag = StringName(String(item->get_metadata(0)));
	emit_signal("tag_selected", selected_tag);
}

void GameplayTagTree::_on_item_edited()
{
	if (rebuilding || mode != MODE_CHECK_MANY) { return; }

	TreeItem* item = tree->get_edited();
	if (item == nullptr) { return; }

	const StringName tag_name = StringName(String(item->get_metadata(0)));
	const bool checked = item->is_checked(0);

	if (checked) { checked_tags.insert(tag_name); }
	else { checked_tags.erase(tag_name); }

	emit_signal("tag_checked", tag_name, checked);
}

void GameplayTagTree::_on_item_collapsed(TreeItem* item)
{
	if (rebuilding || item == nullptr) { return; }

	const StringName tag_name = StringName(String(item->get_metadata(0)));
	if (item->is_collapsed()) { expanded_tags.erase(tag_name); }
	else { expanded_tags.insert(tag_name); }
}

void GameplayTagTree::_bind_methods()
{
	ADD_SIGNAL(MethodInfo("tag_selected", PropertyInfo(Variant::STRING_NAME, "tag_name")));
	ADD_SIGNAL(MethodInfo("tag_checked", PropertyInfo(Variant::STRING_NAME, "tag_name"), PropertyInfo(Variant::BOOL, "checked")));

	BIND_ENUM_CONSTANT(MODE_SELECT_ONE);
	BIND_ENUM_CONSTANT(MODE_CHECK_MANY);
}
}

#endif // TOOLS_ENABLED
