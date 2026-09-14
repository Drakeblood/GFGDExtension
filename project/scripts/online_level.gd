extends Level
## The networked level. Prints who it is and where the pawns are, so a run with
## no window still shows whether movement made it across.
##
## Two pads in the arena show the ability system across the wire: a heal pad on
## the west side and a fire on the east. Only the server applies their effects
## (gas_pad.gd checks has_authority); a client sees the result arrive - health,
## the State.Healing and State.Burning tags, and the cues, which play on every
## peer's copy of the character. The HUD reads the local character's component,
## so on a client it shows the replicated values, not ones it worked out itself.

const GAS_PAD := preload("res://scripts/gas_pad.gd")

var _auto_move := false
var _report_timer := 0.0
var _hud: Label
var _hud_intro := ""


func _ready() -> void:
	_add_pad("HealPad", Vector3(-11.0, 0.0, 0.0), Color(0.25, 0.62, 0.36),
		preload("res://resources/gas/heal_zone.tres"), GAS_PAD.Mode.WHILE_INSIDE)
	_add_pad("FirePad", Vector3(11.0, 0.0, 0.0), Color(0.78, 0.32, 0.12),
		preload("res://resources/gas/burning.tres"), GAS_PAD.Mode.PULSE)

	_hud = get_node_or_null(^"HUD")
	if _hud != null:
		_hud_intro = _hud.text + "\nGreen pad (west) heals, orange (east) burns. E sprints, the shout is server-only."


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

	_update_hud(world)

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

		# The movement mode is worth printing: it is the difference between a pawn
		# standing on the floor and one falling past it, which the coordinates
		# alone do not tell you.
		var movement: CharacterMovementComponent = pawn_root.get_node_or_null(^"CharacterMovementComponent")
		var mode: String = str(movement.get_movement_mode()) if movement != null else "-"

		lines.append("%s at %.1f,%.2f,%.1f %s role=%d/%d owner=%d" % [
			pawn_root.name, pawn_root.position.x, pawn_root.position.y, pawn_root.position.z,
			mode, local_role, remote_role, owner_peer])

	print("GFGD demo: [net_mode %d] players=%d pawns: %s" % [
		world.get_net_mode(), world.get_game_state().get_player_count(), ", ".join(lines)])


func _update_hud(world: World) -> void:
	if _hud == null:
		return

	var controller: PlayerController = world.get_first_player_controller()
	var pawn: Pawn = controller.get_pawn() if controller != null else null
	var asc: AbilitySystemComponent = pawn.get_pawn_root().get_node_or_null(^"AbilitySystemComponent") if pawn != null else null
	if asc == null:
		_hud.text = _hud_intro
		return

	var tags := PackedStringArray()
	var owned: GameplayTagContainer = asc.get_owned_gameplay_tags()
	for i in owned.get_length():
		tags.append(str(owned.get_tag(i).tag_name))

	_hud.text = "%s\nhealth %.0f / %.0f   stamina %.0f   tags %s" % [_hud_intro,
		asc.get_attribute_value(&"health"), asc.get_attribute_value(&"max_health"),
		asc.get_attribute_value(&"stamina"), ", ".join(tags)]


func _add_pad(node_name: String, centre: Vector3, colour: Color, effect: GameplayEffect, mode: int) -> void:
	var pad: Area3D = GAS_PAD.new()
	pad.name = node_name
	pad.position = centre
	pad.effect = effect
	pad.mode = mode

	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(3.0, 2.0, 3.0)
	shape.shape = box
	shape.position.y = 1.0
	pad.add_child(shape)

	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(3.0, 0.1, 3.0)
	visual.mesh = mesh
	visual.position.y = 0.05
	var material := StandardMaterial3D.new()
	material.albedo_color = colour
	visual.material_override = material
	pad.add_child(visual)

	add_child(pad)
