#ifndef GAMEPLAY_TAGS_EDITOR_INSPECTOR_H
#define GAMEPLAY_TAGS_EDITOR_INSPECTOR_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/classes/editor_inspector_plugin.hpp>

using namespace godot;

namespace GFGD
{

class GameplayTagsEditorInspector : public EditorInspectorPlugin
{
	GDCLASS(GameplayTagsEditorInspector, EditorInspectorPlugin)

public:
	bool _can_handle(Object* object) const override;
	bool _parse_property(Object* object, Variant::Type type, const String& name, PropertyHint hint_type, const String& hint_string, BitField<PropertyUsageFlags> usage_flags, bool wide) override;

protected:
	static void _bind_methods();
};

}

#endif // TOOLS_ENABLED

#endif
