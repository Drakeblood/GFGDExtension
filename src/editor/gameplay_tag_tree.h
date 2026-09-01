#ifndef GAMEPLAY_TAG_TREE_H
#define GAMEPLAY_TAG_TREE_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/templates/hash_set.hpp>

using namespace godot;

namespace GFGD
{

// Hierarchical tag picker: "A.B.C" becomes three nested rows instead of one
// entry in a flat list. Fed with plain tag names, so it works off whatever
// source the caller has - the whole GameplayTagsManager lookup, or a single
// table's own dictionary.
class GameplayTagTree : public VBoxContainer
{
	GDCLASS(GameplayTagTree, VBoxContainer)

public:
	enum Mode
	{
		MODE_SELECT_ONE,
		MODE_CHECK_MANY,
	};

private:
	// Below this many tags the tree opens fully expanded; past it only the
	// branches leading to the current selection are unfolded, so a large project
	// gets a short list of roots instead of a wall of rows.
	static constexpr int AUTO_EXPAND_TAG_LIMIT = 25;

	LineEdit* filter_edit = nullptr;
	Tree* tree = nullptr;

	Mode mode = MODE_SELECT_ONE;
	PackedStringArray tag_names;
	HashSet<StringName> checked_tags;
	StringName selected_tag;

	// Rows the user unfolded, kept across rebuilds so filtering or reopening the
	// picker does not throw the layout back to its default.
	HashSet<StringName> expanded_tags;
	bool rebuilding = false;

public:
	GameplayTagTree();

	void set_mode(Mode value);
	void set_tag_names(const PackedStringArray& value);
	void set_selected_tag(const StringName& tag_name);
	void set_checked_tags(const PackedStringArray& value);

	void focus_filter();

protected:
	static void _bind_methods();

private:
	void rebuild();
	void expand_ancestors_of(const StringName& tag_name);
	void decorate_item(TreeItem* item, const StringName& tag_name);

	void _on_filter_changed(const String& new_text);
	void _on_item_selected();
	void _on_item_edited();
	void _on_item_collapsed(TreeItem* item);
};

}

VARIANT_ENUM_CAST(GFGD::GameplayTagTree::Mode);

#endif // TOOLS_ENABLED

#endif
