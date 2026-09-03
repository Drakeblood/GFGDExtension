extends GameInstance
## Lives for the whole application, across every level change - which is why the
## command line is read here rather than in a level. The same build runs as a
## single player game, as a listen server, as a dedicated server or as a client;
## nothing below this line has to know which.
##
##   godot --path project -- --server            dedicated server on port 7777
##   godot --path project -- --host              listen server, playing along
##   godot --path project -- --join 127.0.0.1    client
##   ... --port 7777 --auto-move                 for a run with nobody at the keyboard
##   --level res://levels/test_level.tscn        skip the menu and open a level
##   --travel-after 10                           server travels again after 10s

const ONLINE_LEVEL := "res://levels/online_level.tscn"

var auto_move := false


func _on_init(world: World) -> void:
	print("GFGD demo: >> GameInstance._on_init")

	var args: PackedStringArray = OS.get_cmdline_user_args()
	auto_move = args.has("--auto-move")

	if args.has("--server") or args.has("--host") or args.has("--join"):
		# The first level has to be up before anything can host or travel, and
		# level_loaded is the moment it is.
		world.level_loaded.connect(_start_networking.bind(world, args), CONNECT_ONE_SHOT)
	elif args.has("--level"):
		var level_path: String = _string_argument(args, "--level", "")
		world.level_loaded.connect(
			func(_level: Level) -> void: world.open_level(level_path), CONNECT_ONE_SHOT)


func _start_networking(_level: Level, world: World, args: PackedStringArray) -> void:
	var port: int = _int_argument(args, "--port", 7777)

	if args.has("--server"):
		print("GFGD demo: -- starting a dedicated server on port %d" % port)
		if world.create_dedicated_server(port) == OK:
			world.server_travel(ONLINE_LEVEL)
			_schedule_travel(world, args)
		return

	if args.has("--host"):
		print("GFGD demo: -- hosting on port %d" % port)
		if world.host_game(port) == OK:
			world.server_travel(ONLINE_LEVEL)
			_schedule_travel(world, args)
		return

	var address: String = _string_argument(args, "--join", "127.0.0.1")
	print("GFGD demo: -- joining %s:%d" % [address, port])
	world.connected_to_server.connect(func() -> void: print("GFGD demo: -- connected, waiting for the server's level"))
	world.connection_failed.connect(func() -> void: print("GFGD demo: !! connection failed"))
	world.server_disconnected.connect(func() -> void: print("GFGD demo: !! server disconnected"))
	world.join_game(address, port)


func _schedule_travel(world: World, args: PackedStringArray) -> void:
	# Travelling again with a client already connected is the interesting case:
	# every peer has to end up in the same level and be logged in again there.
	var delay: int = _int_argument(args, "--travel-after", 0)
	if delay <= 0:
		return

	world.create_timer(float(delay)).timeout.connect(func() -> void:
		print("GFGD demo: -- travelling again")
		world.server_travel(ONLINE_LEVEL))


func _string_argument(args: PackedStringArray, name: String, fallback: String) -> String:
	var index: int = args.find(name)
	if index >= 0 and index + 1 < args.size() and not args[index + 1].begins_with("--"):
		return args[index + 1]

	return fallback


func _int_argument(args: PackedStringArray, name: String, fallback: int) -> int:
	var raw: String = _string_argument(args, name, "")
	return int(raw) if raw.is_valid_int() else fallback


func _on_shutdown() -> void:
	print("GFGD demo: >> GameInstance._on_shutdown")
