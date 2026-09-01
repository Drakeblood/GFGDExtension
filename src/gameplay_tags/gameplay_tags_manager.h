#ifndef GAMEPLAY_TAGS_MANAGER_H
#define GAMEPLAY_TAGS_MANAGER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "gameplay_tags/gameplay_tag.h"

using namespace godot;

namespace GFGD
{

// Flattens every registered GameplayTagTable into one lookup and owns the single
// canonical GameplayTag instance per name. Tags are always referenced by name -
// containers serialize plain strings and resolve them here - so a table can be
// added, split or removed without touching the assets that use its tags.
class GameplayTagsManager : public Object
{
	GDCLASS(GameplayTagsManager, Object)

public:
	enum TagSourceKind
	{
		// Declared in a GameplayTagTable; get_tag_source_path() names the table.
		TAG_SOURCE_TABLE,
		// Ancestor implied by a longer declared tag: "A.B" registered because
		// some table declares "A.B.C". Not owned by any single table, since
		// several tables may imply the same ancestor.
		TAG_SOURCE_IMPLICIT,
		// Conjured on demand by request_tag() because nothing declared it -
		// usually a typo or a name left behind by a deleted table entry.
		TAG_SOURCE_RUNTIME,
	};

private:
	struct TagEntry
	{
		Ref<GameplayTag> tag;
		String source_path;
		String description;
		TagSourceKind source_kind = TAG_SOURCE_IMPLICIT;
	};

	static GameplayTagsManager* instance;
	HashMap<StringName, TagEntry> tags;
	PackedStringArray loaded_table_paths;

public:
	static GameplayTagsManager* get_singleton();
	static void destroy_singleton();

	GameplayTagsManager();
	~GameplayTagsManager();

	bool has_tag(const StringName& tag_name) const;
	Ref<GameplayTag> get_tag(const StringName& tag_name) const;
	// Never fails on a known-good name: registers the tag as TAG_SOURCE_RUNTIME
	// if no table declared it, so loading an asset that outlived its table still
	// yields a usable tag instead of a null.
	Ref<GameplayTag> request_tag(const StringName& tag_name);
	TypedArray<GameplayTag> get_separated_tag(const Ref<GameplayTag>& tag);
	PackedStringArray get_all_tag_names() const;

	TagSourceKind get_tag_source_kind(const StringName& tag_name) const;
	String get_tag_source_path(const StringName& tag_name) const;
	String get_tag_description(const StringName& tag_name) const;

	PackedStringArray get_loaded_table_paths() const { return loaded_table_paths; }

	// Rebuilds the whole lookup from the project setting. Safe to call again at
	// any time: tag instances that survive the rebuild are reused, so references
	// already handed out keep pointing at the canonical tag.
	void initialize_tags();

	// The rule the table loader applies, exposed so the editor can reject a bad
	// name at authoring time instead of silently dropping it on the next load.
	static bool is_valid_tag_name(const String& tag_name);

protected:
	static void _bind_methods();

private:

	void declare_tag(const String& tag_name, const String& source_path, const String& description, const HashMap<StringName, TagEntry>* previous_tags);
	// Inserts the tag and every missing ancestor as TAG_SOURCE_IMPLICIT, leaving
	// entries that already exist untouched. Returns nothing; callers refine the
	// entry for the tag itself afterwards.
	void register_tag_with_parents(const String& tag_name, const HashMap<StringName, TagEntry>* previous_tags);
	void insert_implicit_tag(const StringName& tag_name, const HashMap<StringName, TagEntry>* previous_tags);
};

}

VARIANT_ENUM_CAST(GFGD::GameplayTagsManager::TagSourceKind);

#endif
