extends Level
## The networked level. Prints who it is and where the pawns are, so a run with
## no window still shows whether movement made it across.

var _auto_move := false
var _report_timer := 0.0


func _init_level(world: World) -> void:
	var instance: GameInstance = world.get_game_instance()
	_auto_move = instance.get("auto_move") == true

	print("GFGD demo: >> OnlineLevel._init_level | net_mode=%d peer=%d" % [
		world.get_net_mode(), world.get_local_peer_id()])


func _process(delta: float) -> void:
	var world: World = get_tree() as World
	if world == null:
		return

	if _auto_move:
		# Stands in for a hand on the keyboard, so a headless client still sends
		# something for the server to move a pawn with.
		var local_player: LocalPlayer = world.get_local_player(0)
		if local_player != null:
			local_player.get_player_input().action_press(&"move_forward", 1.0)

	_report_timer += delta
	if _report_timer < 2.0:
		return
	_report_timer = 0.0

	var lines: PackedStringArray = PackedStringArray()
	for pawn_root in world.get_pawn_container().get_children():
		var pawn: Pawn = pawn_root.get_node_or_null("Pawn")
		var local_role: int = pawn.get_local_role() if pawn != null else World.ROLE_NONE
		var remote_role: int = pawn.get_remote_role() if pawn != null else World.ROLE_NONE
		var owner_peer: int = pawn.get_owner_peer_id() if pawn != null else 0
		lines.append("%s at %.1f,%.1f role=%d/%d owner=%d" % [
			pawn_root.name, pawn_root.position.x, pawn_root.position.z, local_role, remote_role, owner_peer])

	print("GFGD demo: [net_mode %d] players=%d pawns: %s" % [
		world.get_net_mode(), world.get_game_state().get_player_count(), ", ".join(lines)])
