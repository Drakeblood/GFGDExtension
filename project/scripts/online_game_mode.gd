extends GameModeBase
## Rules for the networked level. The same game mode runs a standalone game, a
## listen server and a dedicated server; on a client it never runs at all, and
## only the scenes it names are read there.


func _init_game(world: World) -> bool:
	print("GFGD demo: >> OnlineGameMode._init_game | net_mode=%d authority=%s" % [
		world.get_net_mode(), world.has_authority()])
	return false


func _pre_login(peer_id: int) -> String:
	# An empty string lets the peer in. Anything else refuses it and says why.
	print("GFGD demo: >> pre_login peer %d (%d/%d players)" % [peer_id, get_num_players(), max_players])
	return ""


func _on_post_login(player_controller: PlayerController) -> void:
	var player_state: PlayerState = player_controller.get_player_state()
	print("GFGD demo: >> logged in %s | player_id=%d peer=%d local=%s" % [
		player_state.player_name, player_state.player_id,
		player_controller.get_owner_peer_id(), player_controller.is_local_player_controller()])


func _on_logout(player_controller: PlayerController) -> void:
	print("GFGD demo: >> logged out player_id=%d peer=%d" % [
		player_controller.get_player_id(), player_controller.get_owner_peer_id()])
