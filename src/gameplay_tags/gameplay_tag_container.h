#ifndef GAMEPLAY_TAG_CONTAINER_H
#define GAMEPLAY_TAG_CONTAINER_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "gameplay_tags/gameplay_tag.h"

using namespace godot;

namespace GFGD
{

class GameplayTagContainer : public Resource
{
	GDCLASS(GameplayTagContainer, Resource)

private:
	Vector<Ref<GameplayTag>> gameplay_tags;

public:
	GameplayTagContainer();

	void add_tag(const Ref<GameplayTag>& tag);
	void add_tag_by_name(const StringName& tag_name);
	void remove_tag(const Ref<GameplayTag>& tag);

	bool has_tag(const Ref<GameplayTag>& tag_to_check) const;
	bool has_tag_exact(const Ref<GameplayTag>& tag_to_check) const;
	bool has_any(const Ref<GameplayTagContainer>& tag_container) const;
	bool has_all(const Ref<GameplayTagContainer>& tag_container) const;
	bool has_all_exact(const Ref<GameplayTagContainer>& tag_container) const;

	int get_length() const { return gameplay_tags.size(); }
	Ref<GameplayTag> get_tag(int index) const;

	// Serialized/editor form: plain tag names resolved through GameplayTagsManager.
	void set_tags(const PackedStringArray& tag_names);
	PackedStringArray get_tags() const;

protected:
	static void _bind_methods();

private:
	int find_tag_index(const StringName& tag_name) const;
};

}

#endif
