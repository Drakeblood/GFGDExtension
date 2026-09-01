#include "gameplay_tags/gameplay_tag_table.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
GameplayTagTable::GameplayTagTable()
{

}

bool GameplayTagTable::has_tag(const StringName& tag_name) const
{
	return tags.has(tag_name);
}

String GameplayTagTable::get_description(const StringName& tag_name) const
{
	return tags.get(tag_name, String());
}

PackedStringArray GameplayTagTable::get_tag_names() const
{
	PackedStringArray names;

	const Array keys = tags.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		names.push_back(keys[i]);
	}
	names.sort();

	return names;
}

void GameplayTagTable::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_tags", "value"), &GameplayTagTable::set_tags);
	ClassDB::bind_method(D_METHOD("get_tags"), &GameplayTagTable::get_tags);
	PropertyInfo tags_info = GetTypeInfo<TypedDictionary<StringName, String>>::get_class_info();
	tags_info.name = "tags";
	ADD_PROPERTY(tags_info, "set_tags", "get_tags");

	ClassDB::bind_method(D_METHOD("has_tag", "tag_name"), &GameplayTagTable::has_tag);
	ClassDB::bind_method(D_METHOD("get_description", "tag_name"), &GameplayTagTable::get_description);
	ClassDB::bind_method(D_METHOD("get_tag_names"), &GameplayTagTable::get_tag_names);
}
}
