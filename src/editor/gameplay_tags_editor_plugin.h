#ifndef GAMEPLAY_TAGS_EDITOR_PLUGIN_H
#define GAMEPLAY_TAGS_EDITOR_PLUGIN_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/editor_plugin.hpp>

#include "editor/gameplay_tags_editor_inspector.h"

using namespace godot;

namespace GFGD
{

class GameplayTagsEditorPlugin : public EditorPlugin
{
	GDCLASS(GameplayTagsEditorPlugin, EditorPlugin)

private:
	Ref<GameplayTagsEditorInspector> inspector_plugin;

public:
	void _enter_tree() override;
	void _exit_tree() override;

protected:
	static void _bind_methods();

private:
	void _on_filesystem_changed();
};

}

#endif // TOOLS_ENABLED

#endif
