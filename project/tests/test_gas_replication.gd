extends SceneTree

# The ability system over a real network: a dedicated server and two clients
# of the demo, in three processes, over ENet on this machine.
#
# tests/gas_replication_probe.gd is loaded into all three through the demo's
# --probe flag. The first client owns the character under test and checks
# everything its owner is promised - attributes, tags, effects, cues, abilities,
# predicted and server-only activation, a refused prediction. The second is an
# observer, and checks that it sees that character's state but not its
# abilities. Both exit with their verdict.
#
# Run with:
#   godot --headless --path project --script res://tests/test_gas_replication.gd


func _initialize() -> void:
	var executable: String = OS.get_executable_path()
	var project: String = ProjectSettings.globalize_path("res://")
	var port: int = 29000 + (Time.get_ticks_msec() % 1000) * 7
	var probe: PackedStringArray = ["--probe", "res://tests/gas_replication_probe.gd"]

	var server_arguments: PackedStringArray = ["--headless", "--path", project, "--", "--server", "--port", str(port)]
	server_arguments.append_array(probe)
	var server: Dictionary = OS.execute_with_pipe(executable, server_arguments, false)
	if server.is_empty():
		print("test_gas_replication: FAILED - could not start the server")
		quit(1)
		return

	OS.delay_msec(1500)

	var client_arguments: PackedStringArray = ["--headless", "--path", project, "--", "--join", "127.0.0.1", "--port", str(port)]
	client_arguments.append_array(probe)

	# The observer joins first, so the owner's pawn is the second one it sees
	# spawn and the owner's requests all happen while it is watching.
	var observer_arguments := client_arguments.duplicate()
	observer_arguments.append("--observer")
	var observer: Dictionary = OS.execute_with_pipe(executable, observer_arguments, false)
	OS.delay_msec(1500)

	var output: Array = []
	var owner_code: int = OS.execute(executable, client_arguments, output, true)

	var observer_pid: int = observer.get("pid", -1)
	for i in 100:
		if observer_pid <= 0 or not OS.is_process_running(observer_pid):
			break
		OS.delay_msec(100)
	var observer_code: int = OS.get_process_exit_code(observer_pid) if observer_pid > 0 else -1
	if observer_pid > 0 and OS.is_process_running(observer_pid):
		OS.kill(observer_pid)
		observer_code = -1

	OS.kill(server["pid"])

	var failed := owner_code != 0 or observer_code != 0

	for line in "".join(output).split("\n"):
		if failed or line.begins_with("    ") or line.begins_with("===") or line.begins_with("gas probe"):
			print(line)

	var observer_output: String = (observer["stdio"] as FileAccess).get_as_text() if observer.has("stdio") else ""
	for line in observer_output.split("\n"):
		if failed or line.begins_with("    ") or line.begins_with("===") or line.begins_with("gas probe"):
			print(line)

	if failed:
		print("--- server output ---")
		print((server["stdio"] as FileAccess).get_as_text())
		print("test_gas_replication: FAILED (owner exit %d, observer exit %d)" % [owner_code, observer_code])
		quit(1)
	else:
		print("test_gas_replication: all checks passed")
		quit(0)
