extends GameplayAbility
# Used by tests/gas_replication_probe.gd: a predicted ability that executes a
# cue and ends. It runs on the client and on the server; the client that
# predicted it must hear the cue once - its own - and not again from the server.


func _activate_ability() -> void:
	execute_gameplay_cue(&"Cue.Test")
	end_ability(false)
