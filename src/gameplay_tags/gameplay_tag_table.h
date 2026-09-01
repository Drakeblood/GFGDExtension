#ifndef GAMEPLAY_TAG_TABLE_H
#define GAMEPLAY_TAG_TABLE_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>

using namespace godot;

namespace GFGD
{

// Authoring-time declaration of gameplay tags: tag name -> description.
// Several tables are registered at once through the
// "application/game_framework/gameplay_tag_tables" project setting and merged by
// GameplayTagsManager, so a project can split tags per feature or ship them with
// an addon. Stored as a typed dictionary so the .tres serializes one tag per
// line, which keeps diffs and merges readable.
class GameplayTagTable : public Resource
{
	GDCLASS(GameplayTagTable, Resource)

private:
	TypedDictionary<StringName, String> tags;

public:
	GameplayTagTable();

	void set_tags(const TypedDictionary<StringName, String>& value) { tags = value; }
	TypedDictionary<StringName, String> get_tags() const { return tags; }

	bool has_tag(const StringName& tag_name) const;
	String get_description(const StringName& tag_name) const;

	// Sorted, so merging several tables stays deterministic regardless of the
	// order the dictionary happens to be stored in.
	PackedStringArray get_tag_names() const;

protected:
	static void _bind_methods();
};

}

#endif
