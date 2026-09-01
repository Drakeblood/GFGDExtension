#include "editor/gameplay_tag_container_editor_property.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tag_container.h"
#include "editor/gameplay_tag_editor_utils.h"
#include "gameplay_tags/gameplay_tags_manager.h"

using namespace godot;

namespace GFGD
{
GameplayTagContainerEditorProperty::GameplayTagContainerEditorProperty()
{
	VBoxContainer* v_box_container = memnew(VBoxContainer);

	items_container = memnew(VBoxContainer);
	v_box_container->add_child(items_container);

	edit_button = memnew(Button);
	edit_button->set_text("Edit tags...");
	edit_button->connect("pressed", callable_mp(this, &GameplayTagContainerEditorProperty::_on_edit_button_pressed));
	v_box_container->add_child(edit_button);

	add_child(v_box_container);

	picker = memnew(GameplayTagPickerPopup);
	picker->get_tag_tree()->set_mode(GameplayTagTree::MODE_CHECK_MANY);
	picker->get_tag_tree()->connect("tag_checked", callable_mp(this, &GameplayTagContainerEditorProperty::_on_tag_checked));
	add_child(picker);
}

void GameplayTagContainerEditorProperty::_update_property()
{
	const Ref<GameplayTagContainer> new_value = get_edited_object()->get(get_edited_property());
	if (new_value == current_value) { return; }

	current_value = new_value;
	_rebuild_rows();
}

void GameplayTagContainerEditorProperty::_rebuild_rows()
{
	TypedArray<Node> children = items_container->get_children();
	for (int i = 0; i < children.size(); i++)
	{
		Node* child = Object::cast_to<Node>(children[i]);
		items_container->remove_child(child);
		child->queue_free();
	}

	if (current_value.is_null()) { return; }

	for (int i = 0; i < current_value->get_length(); i++)
	{
		const Ref<GameplayTag> tag = current_value->get_tag(i);
		if (tag.is_null()) { continue; }

		const StringName tag_name = tag->get_tag_name();

		HBoxContainer* row = memnew(HBoxContainer);

		Label* label = memnew(Label);
		label->set_text(tag_name);
		label->set_tooltip_text(GameplayTagEditorUtils::build_tooltip(tag_name));
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		label->set_clip_text(true);
		if (GameplayTagEditorUtils::is_undeclared(tag_name))
		{
			label->add_theme_color_override("font_color", GameplayTagEditorUtils::get_warning_color());
		}
		row->add_child(label);

		Button* remove_button = memnew(Button);
		remove_button->set_text(" - ");
		remove_button->connect("pressed", callable_mp(this, &GameplayTagContainerEditorProperty::_on_remove_button_pressed).bind(tag_name));
		row->add_child(remove_button);

		items_container->add_child(row);
	}
}

void GameplayTagContainerEditorProperty::_on_edit_button_pressed()
{
	GameplayTagTree* tag_tree = picker->get_tag_tree();
	tag_tree->set_tag_names(GameplayTagsManager::get_singleton()->get_all_tag_names());
	tag_tree->set_checked_tags(current_value.is_valid() ? current_value->get_tags() : PackedStringArray());

	// Stays open across ticks - picking a handful of tags should not mean
	// reopening the picker for each one.
	picker->popup_under(edit_button);
}

void GameplayTagContainerEditorProperty::_on_tag_checked(const StringName& tag_name, bool checked)
{
	if (current_value.is_null())
	{
		current_value.instantiate();
	}

	if (checked)
	{
		current_value->add_tag_by_name(tag_name);
	}
	else
	{
		// request_tag, not get_tag: a reload triggered by filesystem_changed drops
		// undeclared tags from the lookup, and get_tag would then hand back a null
		// that remove_tag ignores - clearing the checkbox but not the container.
		current_value->remove_tag(GameplayTagsManager::get_singleton()->request_tag(tag_name));
	}

	_rebuild_rows();
	emit_changed(get_edited_property(), current_value);
}

void GameplayTagContainerEditorProperty::_on_remove_button_pressed(const StringName& tag_name)
{
	if (current_value.is_null()) { return; }

	current_value->remove_tag(GameplayTagsManager::get_singleton()->request_tag(tag_name));

	_rebuild_rows();
	emit_changed(get_edited_property(), current_value);
}

void GameplayTagContainerEditorProperty::_bind_methods()
{
}
}

#endif // TOOLS_ENABLED
