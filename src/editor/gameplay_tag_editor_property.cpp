#include "editor/gameplay_tag_editor_property.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tags/gameplay_tag.h"
#include "editor/gameplay_tag_editor_utils.h"
#include "gameplay_tags/gameplay_tags_manager.h"

using namespace godot;

namespace GFGD
{
namespace
{
const char* NO_TAG_TEXT = "<none>";
}

GameplayTagEditorProperty::GameplayTagEditorProperty()
{
	tag_button = memnew(Button);
	tag_button->set_text(NO_TAG_TEXT);
	tag_button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	tag_button->set_clip_text(true);
	tag_button->connect("pressed", callable_mp(this, &GameplayTagEditorProperty::_on_button_pressed));

	add_child(tag_button);
	add_focusable(tag_button);

	picker = memnew(GameplayTagPickerPopup);
	picker->get_tag_tree()->set_mode(GameplayTagTree::MODE_SELECT_ONE);
	picker->get_tag_tree()->connect("tag_selected", callable_mp(this, &GameplayTagEditorProperty::_on_tag_selected));
	add_child(picker);
}

void GameplayTagEditorProperty::_enter_tree()
{
	const Ref<GameplayTag> current_value = get_edited_object()->get(get_edited_property());
	_set_tag_name(current_value.is_valid() ? current_value->get_tag_name() : StringName());
}

void GameplayTagEditorProperty::_update_property()
{
	const Ref<GameplayTag> new_value = get_edited_object()->get(get_edited_property());
	_set_tag_name(new_value.is_valid() ? new_value->get_tag_name() : StringName());
}

void GameplayTagEditorProperty::_set_tag_name(const StringName& tag_name)
{
	current_tag_name = tag_name;

	const bool has_tag = !String(tag_name).is_empty();
	tag_button->set_text(has_tag ? String(tag_name) : NO_TAG_TEXT);
	tag_button->set_tooltip_text(has_tag ? GameplayTagEditorUtils::build_tooltip(tag_name) : String());

	// Same signal the tree picker uses for an undeclared tag, so a stale name in
	// a .tres reads the same whether the picker is open or shut.
	if (has_tag && GameplayTagEditorUtils::is_undeclared(tag_name))
	{
		tag_button->add_theme_color_override("font_color", GameplayTagEditorUtils::get_warning_color());
	}
	else
	{
		tag_button->remove_theme_color_override("font_color");
	}
}

void GameplayTagEditorProperty::_on_button_pressed()
{
	GameplayTagTree* tag_tree = picker->get_tag_tree();
	tag_tree->set_tag_names(GameplayTagsManager::get_singleton()->get_all_tag_names());
	tag_tree->set_selected_tag(current_tag_name);

	picker->popup_under(tag_button);
}

void GameplayTagEditorProperty::_on_tag_selected(const StringName& tag_name)
{
	picker->hide();

	const Ref<GameplayTag> tag = GameplayTagsManager::get_singleton()->get_tag(tag_name);
	emit_changed(get_edited_property(), tag);
}

void GameplayTagEditorProperty::_bind_methods()
{
}
}

#endif // TOOLS_ENABLED
