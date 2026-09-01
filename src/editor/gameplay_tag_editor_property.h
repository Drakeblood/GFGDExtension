#ifndef GAMEPLAY_TAG_EDITOR_PROPERTY_H
#define GAMEPLAY_TAG_EDITOR_PROPERTY_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/editor_property.hpp>

#include "editor/gameplay_tag_picker_popup.h"

using namespace godot;

namespace GFGD
{

class GameplayTagEditorProperty : public EditorProperty
{
	GDCLASS(GameplayTagEditorProperty, EditorProperty)

private:
	Button* tag_button = nullptr;
	GameplayTagPickerPopup* picker = nullptr;
	StringName current_tag_name;

public:
	GameplayTagEditorProperty();

	void _enter_tree() override;
	void _update_property() override;

protected:
	static void _bind_methods();

private:
	void _set_tag_name(const StringName& tag_name);
	void _on_button_pressed();
	void _on_tag_selected(const StringName& tag_name);
};

}

#endif // TOOLS_ENABLED

#endif
