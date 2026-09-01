#ifndef GAMEPLAY_TAG_PICKER_POPUP_H
#define GAMEPLAY_TAG_PICKER_POPUP_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/popup_panel.hpp>

#include "editor/gameplay_tag_tree.h"

using namespace godot;

namespace GFGD
{

// Hosts a GameplayTagTree above the inspector instead of inside it, so a tag
// property stays one row tall however many tags the project declares.
class GameplayTagPickerPopup : public PopupPanel
{
	GDCLASS(GameplayTagPickerPopup, PopupPanel)

private:
	GameplayTagTree* tag_tree = nullptr;

public:
	GameplayTagPickerPopup();

	GameplayTagTree* get_tag_tree() const { return tag_tree; }

	// Opens the picker flush under the control that triggered it, at least as
	// wide as that control.
	void popup_under(Control* anchor);

protected:
	static void _bind_methods();
};

}

#endif // TOOLS_ENABLED

#endif
