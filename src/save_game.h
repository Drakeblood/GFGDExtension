#ifndef SAVE_GAME_H
#define SAVE_GAME_H

#include <godot_cpp/classes/resource.hpp>

using namespace godot;

namespace GFGD 
{
class SaveGame : public Resource
{
	GDCLASS(SaveGame, Resource)

public:
	SaveGame();
	~SaveGame();

	virtual String to_json() const;
	virtual Error from_json(const Variant& data);

protected:
	static void _bind_methods();
};

}

#endif