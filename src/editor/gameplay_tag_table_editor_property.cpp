#include "editor/gameplay_tag_table_editor_property.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "editor/gameplay_tag_editor_utils.h"
#include "gameplay_tags/gameplay_tags_manager.h"

using namespace godot;

namespace GFGD
{
GameplayTagTableEditorProperty::GameplayTagTableEditorProperty()
{
	VBoxContainer* root_box = memnew(VBoxContainer);

	filter_edit = memnew(LineEdit);
	filter_edit->set_placeholder("Filter tags");
	filter_edit->set_clear_button_enabled(true);
	filter_edit->connect("text_changed", callable_mp(this, &GameplayTagTableEditorProperty::_on_filter_changed));
	root_box->add_child(filter_edit);

	tree = memnew(Tree);
	tree->set_columns(2);
	tree->set_column_titles_visible(true);
	tree->set_column_title(COLUMN_NAME, "Tag");
	tree->set_column_title(COLUMN_DESCRIPTION, "Description");
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_SINGLE);
	tree->set_custom_minimum_size(Vector2(0, 260));
	tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree->connect("item_selected", callable_mp(this, &GameplayTagTableEditorProperty::_on_item_selected));
	tree->connect("item_activated", callable_mp(this, &GameplayTagTableEditorProperty::_on_item_activated));
	tree->connect("item_edited", callable_mp(this, &GameplayTagTableEditorProperty::_on_item_edited));
	tree->connect("item_collapsed", callable_mp(this, &GameplayTagTableEditorProperty::_on_item_collapsed));
	root_box->add_child(tree);

	HBoxContainer* controls = memnew(HBoxContainer);

	new_tag_edit = memnew(LineEdit);
	new_tag_edit->set_placeholder("New tag, e.g. Ability.Combat.Melee");
	new_tag_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	new_tag_edit->connect("text_submitted", callable_mp(this, &GameplayTagTableEditorProperty::_on_add_pressed).unbind(1));
	controls->add_child(new_tag_edit);

	add_button = memnew(Button);
	add_button->set_text("Add");
	add_button->connect("pressed", callable_mp(this, &GameplayTagTableEditorProperty::_on_add_pressed));
	controls->add_child(add_button);

	rename_button = memnew(Button);
	rename_button->set_text("Rename");
	rename_button->set_tooltip_text("Edit the selected cell. Double-clicking a row does the same.");
	rename_button->connect("pressed", callable_mp(this, &GameplayTagTableEditorProperty::_on_rename_pressed));
	controls->add_child(rename_button);

	remove_button = memnew(Button);
	remove_button->set_text("Remove");
	remove_button->connect("pressed", callable_mp(this, &GameplayTagTableEditorProperty::_on_remove_pressed));
	controls->add_child(remove_button);

	root_box->add_child(controls);

	add_child(root_box);
	// Tag names and descriptions need the full inspector width, not the narrow
	// value column a property editor normally gets.
	set_bottom_editor(root_box);

	remove_dialog = memnew(ConfirmationDialog);
	remove_dialog->set_title("Remove gameplay tags");
	remove_dialog->set_ok_button_text("Remove");
	remove_dialog->connect("confirmed", callable_mp(this, &GameplayTagTableEditorProperty::_on_remove_confirmed));
	add_child(remove_dialog);
}

void GameplayTagTableEditorProperty::_update_property()
{
	tags = TypedDictionary<StringName, String>(get_edited_object()->get(get_edited_property()));
	rebuild();
}

void GameplayTagTableEditorProperty::rebuild()
{
	rebuilding = true;

	// Every item is about to be freed, so drop the reference before it dangles.
	open_cell_item = nullptr;
	open_cell_column = -1;

	tree->clear();
	TreeItem* root = tree->create_item();

	const bool filtering = !filter_edit->get_text().strip_edges().is_empty();

	PackedStringArray declared;
	const Array keys = tags.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		declared.push_back(keys[i]);
	}

	const PackedStringArray rows = GameplayTagEditorUtils::expand_with_ancestors(declared, filter_edit->get_text());

	HashMap<String, TreeItem*> items;
	for (int i = 0; i < rows.size(); i++)
	{
		const String tag_name = rows[i];
		const int last_dot = tag_name.rfind(".");

		TreeItem* parent = root;
		if (last_dot != -1)
		{
			TreeItem** found = items.getptr(tag_name.substr(0, last_dot));
			if (found != nullptr) { parent = *found; }
		}

		TreeItem* item = tree->create_item(parent);
		item->set_text(COLUMN_NAME, last_dot != -1 ? tag_name.substr(last_dot + 1) : tag_name);
		item->set_metadata(COLUMN_NAME, tag_name);
		item->set_text(COLUMN_DESCRIPTION, tags.get(StringName(tag_name), String()));
		item->set_collapsed(!filtering && !expanded_tags.has(StringName(tag_name)));

		// An ancestor nobody declared is a perfectly usable tag, so it reads like
		// any other row; the tooltip is the only place the difference shows.
		item->set_tooltip_text(COLUMN_NAME, tags.has(StringName(tag_name))
			? String(tag_name)
			: vformat("%s\nImplied by a longer tag in this table. Give it a description to declare it here.", tag_name));

		items.insert(tag_name, item);
	}

	rebuilding = false;
}

void GameplayTagTableEditorProperty::close_open_cell()
{
	if (open_cell_item != nullptr && open_cell_column >= 0)
	{
		open_cell_item->set_editable(open_cell_column, false);
	}

	open_cell_item = nullptr;
	open_cell_column = -1;
}

void GameplayTagTableEditorProperty::commit(const TypedDictionary<StringName, String>& new_tags)
{
	tags = new_tags;
	rebuild();

	// The inspector wraps this into an undo action, so even a cascading rename
	// comes back with a single Ctrl+Z.
	emit_changed(get_edited_property(), new_tags);
}

PackedStringArray GameplayTagTableEditorProperty::collect_subtree_keys(const StringName& tag_name) const
{
	const String prefix = String(tag_name) + ".";

	PackedStringArray matched;
	const Array keys = tags.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		const String key = keys[i];
		if (key == String(tag_name) || key.begins_with(prefix))
		{
			matched.push_back(key);
		}
	}
	matched.sort();

	return matched;
}

StringName GameplayTagTableEditorProperty::get_selected_tag() const
{
	TreeItem* item = tree->get_selected();
	if (item == nullptr) { return StringName(); }
	return StringName(String(item->get_metadata(COLUMN_NAME)));
}

void GameplayTagTableEditorProperty::_on_filter_changed(const String& new_text)
{
	rebuild();
}

void GameplayTagTableEditorProperty::_on_item_selected()
{
	if (rebuilding) { return; }

	// Seed the add field with the selected tag and park the caret at its end, so
	// declaring a child is just typing ".Something" onto what is already there.
	close_open_cell();

	const String selected = get_selected_tag();
	new_tag_edit->set_text(selected);
	new_tag_edit->set_caret_column(selected.length());
}

void GameplayTagTableEditorProperty::_on_item_activated()
{
	// Cells are read-only at rest so that a plain click selects the row instead of
	// dropping into a rename box - which left the fold arrow as the only spot that
	// could select, and on a row with children that just folds it. Editing is
	// opened deliberately here, and closed again in _on_item_edited().
	close_open_cell();

	TreeItem* item = tree->get_selected();
	const int column = tree->get_selected_column();
	if (item == nullptr || column < 0) { return; }

	item->set_editable(column, true);
	open_cell_item = item;
	open_cell_column = column;
	tree->edit_selected(true);
}

void GameplayTagTableEditorProperty::_on_rename_pressed()
{
	_on_item_activated();
}

void GameplayTagTableEditorProperty::_on_item_edited()
{
	if (rebuilding) { return; }

	TreeItem* item = tree->get_edited();
	if (item == nullptr) { return; }

	const StringName old_name = StringName(String(item->get_metadata(COLUMN_NAME)));
	const int column = tree->get_edited_column();

	// Close the cell again straight away; every path below either rebuilds the
	// tree or leaves this row in place, and it must not stay editable either way.
	close_open_cell();

	TypedDictionary<StringName, String> new_tags;

	if (column == COLUMN_DESCRIPTION)
	{
		const Array keys = tags.keys();
		for (int i = 0; i < keys.size(); i++)
		{
			new_tags[keys[i]] = tags[keys[i]];
		}
		// Writing a description onto an implied ancestor declares it outright.
		new_tags[old_name] = item->get_text(COLUMN_DESCRIPTION);

		commit(new_tags);
		return;
	}

	const String new_segment = item->get_text(COLUMN_NAME).strip_edges();
	const int last_dot = String(old_name).rfind(".");
	const String new_name = (last_dot != -1) ? String(old_name).substr(0, last_dot + 1) + new_segment : new_segment;

	if (new_name == String(old_name))
	{
		return;
	}

	// A segment is a tag name without the dots: reuse the loader's rule so the
	// editor cannot author a name the loader would later reject.
	if (new_segment.contains(".") || !GameplayTagsManager::is_valid_tag_name(new_name))
	{
		WARN_PRINT(vformat("GFGD: \"%s\" is not a valid gameplay tag name.", new_name));
		rebuild();
		return;
	}

	const PackedStringArray moved = collect_subtree_keys(old_name);
	const String old_prefix = String(old_name) + ".";
	const String new_prefix = new_name + String(".");

	// Refuse a rename that would land on top of tags living somewhere else.
	const Array keys = tags.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		const String key = keys[i];
		if (moved.find(key) != -1) { continue; }

		if (key == new_name || key.begins_with(new_prefix))
		{
			WARN_PRINT(vformat("GFGD: Cannot rename \"%s\" to \"%s\" - that would collide with the existing tag \"%s\".", old_name, new_name, key));
			rebuild();
			return;
		}
	}

	for (int i = 0; i < keys.size(); i++)
	{
		const String key = keys[i];
		if (key == String(old_name))
		{
			new_tags[new_name] = tags[key];
		}
		else if (key.begins_with(old_prefix))
		{
			new_tags[new_name + key.substr(String(old_name).length())] = tags[key];
		}
		else
		{
			new_tags[key] = tags[key];
		}
	}

	commit(new_tags);
}

void GameplayTagTableEditorProperty::_on_item_collapsed(TreeItem* item)
{
	if (rebuilding || item == nullptr) { return; }

	const StringName tag_name = StringName(String(item->get_metadata(COLUMN_NAME)));
	if (item->is_collapsed()) { expanded_tags.erase(tag_name); }
	else { expanded_tags.insert(tag_name); }
}

void GameplayTagTableEditorProperty::_on_add_pressed()
{
	const String tag_name = new_tag_edit->get_text().strip_edges();
	if (tag_name.is_empty()) { return; }

	if (!GameplayTagsManager::is_valid_tag_name(tag_name))
	{
		WARN_PRINT(vformat("GFGD: \"%s\" is not a valid gameplay tag name.", tag_name));
		return;
	}

	if (tags.has(StringName(tag_name)))
	{
		WARN_PRINT(vformat("GFGD: This table already declares \"%s\".", tag_name));
		return;
	}

	TypedDictionary<StringName, String> new_tags;
	const Array keys = tags.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		new_tags[keys[i]] = tags[keys[i]];
	}
	new_tags[StringName(tag_name)] = String();

	// Keep the parent unfolded, otherwise a freshly added deep tag lands out of
	// sight behind a collapsed row.
	String ancestor = tag_name;
	while (true)
	{
		const int last_dot = ancestor.rfind(".");
		if (last_dot == -1) { break; }

		ancestor = ancestor.substr(0, last_dot);
		expanded_tags.insert(ancestor);
	}

	new_tag_edit->clear();
	commit(new_tags);
}

void GameplayTagTableEditorProperty::_on_remove_pressed()
{
	const StringName selected = get_selected_tag();
	if (String(selected).is_empty()) { return; }

	const PackedStringArray affected = collect_subtree_keys(selected);
	if (affected.is_empty())
	{
		// An implied ancestor holds no key of its own; there is nothing to erase
		// unless it has declared descendants, which collect_subtree_keys catches.
		WARN_PRINT(vformat("GFGD: \"%s\" is not declared in this table.", selected));
		return;
	}

	pending_removal = selected;

	if (affected.size() == 1)
	{
		_on_remove_confirmed();
		return;
	}

	remove_dialog->set_text(vformat("Remove \"%s\" and everything under it?\n\n%s", selected, String("\n").join(affected)));
	remove_dialog->popup_centered();
}

void GameplayTagTableEditorProperty::_on_remove_confirmed()
{
	if (String(pending_removal).is_empty()) { return; }

	const PackedStringArray removed = collect_subtree_keys(pending_removal);
	pending_removal = StringName();

	TypedDictionary<StringName, String> new_tags;
	const Array keys = tags.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		const String key = keys[i];
		if (removed.find(key) != -1) { continue; }

		new_tags[key] = tags[key];
	}

	commit(new_tags);
}

void GameplayTagTableEditorProperty::_bind_methods()
{
}
}

#endif // TOOLS_ENABLED
