#include "editor/gameplay_tags_editor_plugin.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_interface.hpp>

#include "editor/gameplay_tags_editor_inspector.h"
#include "gameplay_tags/gameplay_tags_manager.h"

using namespace godot;

namespace GFGD
{
void GameplayTagsEditorPlugin::_enter_tree()
{
	inspector_plugin.instantiate();
	add_inspector_plugin(inspector_plugin);

	// Tag tables are ordinary resources the user edits like any other, so rebuild
	// the lookup once the filesystem settles. Without this every tag dropdown
	// would keep showing the tables as they were when the editor started.
	EditorFileSystem* filesystem = EditorInterface::get_singleton()->get_resource_filesystem();
	if (filesystem != nullptr)
	{
		filesystem->connect("filesystem_changed", callable_mp(this, &GameplayTagsEditorPlugin::_on_filesystem_changed));
	}
}

void GameplayTagsEditorPlugin::_exit_tree()
{
	EditorFileSystem* filesystem = EditorInterface::get_singleton()->get_resource_filesystem();
	if (filesystem != nullptr)
	{
		filesystem->disconnect("filesystem_changed", callable_mp(this, &GameplayTagsEditorPlugin::_on_filesystem_changed));
	}

	remove_inspector_plugin(inspector_plugin);
	inspector_plugin.unref();
}

void GameplayTagsEditorPlugin::_on_filesystem_changed()
{
	GameplayTagsManager::get_singleton()->initialize_tags();
}

void GameplayTagsEditorPlugin::_bind_methods()
{
}
}

#endif // TOOLS_ENABLED
