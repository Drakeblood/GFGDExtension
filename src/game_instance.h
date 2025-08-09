#ifndef GAME_INSTANCE_H
#define GAME_INSTANCE_H

#include <godot_cpp/classes/object.hpp>

using namespace godot;

namespace GFGD 
{
class GFGDSceneTree;

class GameInstance : public Object
{
	GDCLASS(GameInstance, Object)

public:
	GameInstance();
	~GameInstance();

	virtual void init(GFGDSceneTree* scene_tree);

protected:
	static void _bind_methods();

};
}

#endif