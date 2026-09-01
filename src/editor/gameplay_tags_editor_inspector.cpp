#include "editor/gameplay_tags_editor_inspector.h"

#ifdef TOOLS_ENABLED

#include <godot_cpp/core/class_db.hpp>

#include "editor/gameplay_tag_editor_property.h"
#include "editor/gameplay_tag_container_editor_property.h"
#include "gameplay_tags/gameplay_tag_table.h"
#include "editor/gameplay_tag_table_editor_property.h"

using namespace godot;

namespace GFGD
{
bool GameplayTagsEditorInspector::_can_handle(Object* object) const
{
	return true;
}

bool GameplayTagsEditorInspector::_parse_property(Object* object, Variant::Type type, const String& name, PropertyHint hint_type, const String& hint_string, BitField<PropertyUsageFlags> usage_flags, bool wide)
{
	// The table's own dictionary is matched on the object, not a hint: "tags" is a
	// plain typed dictionary and carries nothing to key off.
	if (name == "tags" && Object::cast_to<GameplayTagTable>(object) != nullptr)
	{
		add_property_editor(name, memnew(GameplayTagTableEditorProperty));
		return true;
	}
	if (hint_string == "GameplayTagContainer")
	{
		add_property_editor(name, memnew(GameplayTagContainerEditorProperty));
		return true;
	}
	if (hint_string == "GameplayTag")
	{
		add_property_editor(name, memnew(GameplayTagEditorProperty));
		return true;
	}

	return false;
}

void GameplayTagsEditorInspector::_bind_methods()
{
}
}

#endif // TOOLS_ENABLED
