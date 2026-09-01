#include "editor/gameplay_tag_picker_popup.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
namespace
{
const int PICKER_MIN_WIDTH = 320;
const int PICKER_HEIGHT = 420;
}

GameplayTagPickerPopup::GameplayTagPickerPopup()
{
	tag_tree = memnew(GameplayTagTree);
	tag_tree->set_anchors_preset(Control::PRESET_FULL_RECT);
	add_child(tag_tree);
}

void GameplayTagPickerPopup::popup_under(Control* anchor)
{
	if (anchor == nullptr) { return; }

	const float editor_scale = EditorInterface::get_singleton() != nullptr ? EditorInterface::get_singleton()->get_editor_scale() : 1.0f;
	const Vector2 anchor_position = anchor->get_screen_position();
	const Vector2i size = Vector2i(
		MAX((int)anchor->get_size().x, (int)(PICKER_MIN_WIDTH * editor_scale)),
		(int)(PICKER_HEIGHT * editor_scale));

	popup(Rect2i(Vector2i((int)anchor_position.x, (int)(anchor_position.y + anchor->get_size().y)), size));
	tag_tree->focus_filter();
}

void GameplayTagPickerPopup::_bind_methods()
{
}
}

#endif // TOOLS_ENABLED
