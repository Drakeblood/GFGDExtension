#ifndef GAMEPLAY_TAG_TABLE_EDITOR_PROPERTY_H
#define GAMEPLAY_TAG_TABLE_EDITOR_PROPERTY_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>

using namespace godot;

namespace GFGD
{

// Authoring view for a GameplayTagTable's "tags" dictionary: the same hierarchy
// the pickers show, but editable in place.
//
// It reads the table's own dictionary rather than GameplayTagsManager, so a table
// that is not listed in the gameplay_tag_tables setting yet is still editable.
// Rows for ancestors nobody declared ("A.B" when only "A.B.C" is a key) are shown
// dimmed; writing a description onto one promotes it to a real declaration.
//
// Every edit rebuilds the whole dictionary and hands it to emit_changed(), which
// puts the inspector's own undo/redo in charge - so a cascading rename is undone
// by one Ctrl+Z, with no EditorUndoRedoManager bookkeeping of our own.
class GameplayTagTableEditorProperty : public EditorProperty
{
	GDCLASS(GameplayTagTableEditorProperty, EditorProperty)

private:
	enum Column
	{
		COLUMN_NAME,
		COLUMN_DESCRIPTION,
	};

	LineEdit* filter_edit = nullptr;
	Tree* tree = nullptr;
	LineEdit* new_tag_edit = nullptr;
	Button* add_button = nullptr;
	Button* rename_button = nullptr;
	Button* remove_button = nullptr;
	ConfirmationDialog* remove_dialog = nullptr;

	TypedDictionary<StringName, String> tags;
	HashSet<StringName> expanded_tags;
	StringName pending_removal;
	bool rebuilding = false;

	// The one cell currently open for editing. Escape cancels an edit without
	// emitting item_edited, so the cell has to be closed from the outside too.
	TreeItem* open_cell_item = nullptr;
	int open_cell_column = -1;

public:
	GameplayTagTableEditorProperty();

	void _update_property() override;

protected:
	static void _bind_methods();

private:
	void rebuild();
	void close_open_cell();
	void commit(const TypedDictionary<StringName, String>& new_tags);

	// Keys equal to tag_name or sitting under it, i.e. what a rename rewrites and
	// a removal takes with it.
	PackedStringArray collect_subtree_keys(const StringName& tag_name) const;
	StringName get_selected_tag() const;

	void _on_filter_changed(const String& new_text);
	void _on_item_selected();
	void _on_item_activated();
	void _on_rename_pressed();
	void _on_item_edited();
	void _on_item_collapsed(TreeItem* item);
	void _on_add_pressed();
	void _on_remove_pressed();
	void _on_remove_confirmed();
};

}

#endif // TOOLS_ENABLED

#endif
