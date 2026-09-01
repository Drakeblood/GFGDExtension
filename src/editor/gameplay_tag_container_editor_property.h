#ifndef GAMEPLAY_TAG_CONTAINER_EDITOR_PROPERTY_H
#define GAMEPLAY_TAG_CONTAINER_EDITOR_PROPERTY_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

#include "gameplay_tags/gameplay_tag_container.h"
#include "editor/gameplay_tag_picker_popup.h"

using namespace godot;

namespace GFGD
{

class GameplayTagContainerEditorProperty : public EditorProperty
{
	GDCLASS(GameplayTagContainerEditorProperty, EditorProperty)

private:
	VBoxContainer* items_container = nullptr;
	Button* edit_button = nullptr;
	GameplayTagPickerPopup* picker = nullptr;
	Ref<GameplayTagContainer> current_value;

public:
	GameplayTagContainerEditorProperty();

	void _update_property() override;

protected:
	static void _bind_methods();

private:
	void _rebuild_rows();
	void _on_edit_button_pressed();
	void _on_tag_checked(const StringName& tag_name, bool checked);
	void _on_remove_button_pressed(const StringName& tag_name);
};

}

#endif // TOOLS_ENABLED

#endif
