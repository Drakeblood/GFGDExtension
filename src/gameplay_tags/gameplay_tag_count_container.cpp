#include "gameplay_tags/gameplay_tag_count_container.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tag_container.h"

using namespace godot;

namespace GFGD
{
GameplayTagCountContainer::GameplayTagCountContainer()
{
	explicit_tags.instantiate();
}

bool GameplayTagCountContainer::update_tag_count(const Ref<GameplayTag>& tag, int count_delta)
{
	if (tag.is_null() || count_delta == 0) { return false; }

	StringName tag_name = tag->get_tag_name();
	int old_count = get_tag_count(tag_name);
	int new_count = MAX(old_count + count_delta, 0);
	gameplay_tag_count_map[tag_name] = new_count;

	if (new_count == 0)
	{
		explicit_tags->remove_tag(tag);
	}
	else
	{
		explicit_tags->add_tag(tag);
	}

	const Vector<Callable>* delegates = gameplay_tag_event_map.getptr(tag_name);
	if (delegates != nullptr)
	{
		for (int i = 0; i < delegates->size(); i++)
		{
			const Callable& tag_delegate = (*delegates)[i];
			if (tag_delegate.is_valid())
			{
				tag_delegate.call(tag_name, new_count);
			}
		}
	}

	return (old_count == 0) != (new_count == 0);
}

int GameplayTagCountContainer::get_tag_count(const StringName& tag_name) const
{
	const int* count = gameplay_tag_count_map.getptr(tag_name);
	return count != nullptr ? *count : 0;
}

void GameplayTagCountContainer::register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate)
{
	if (tag.is_null() || !tag_delegate.is_valid()) { return; }
	gameplay_tag_event_map[tag->get_tag_name()].push_back(tag_delegate);
}

void GameplayTagCountContainer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("update_tag_count", "tag", "count_delta"), &GameplayTagCountContainer::update_tag_count);
	ClassDB::bind_method(D_METHOD("get_tag_count", "tag_name"), &GameplayTagCountContainer::get_tag_count);
	ClassDB::bind_method(D_METHOD("register_gameplay_tag_event", "tag", "tag_delegate"), &GameplayTagCountContainer::register_gameplay_tag_event);
	ClassDB::bind_method(D_METHOD("get_explicit_tags"), &GameplayTagCountContainer::get_explicit_tags);
}
}
