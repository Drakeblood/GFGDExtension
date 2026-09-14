extends SceneTree

# Client prediction, end to end: a real dedicated server and a real client in
# two processes on this machine, talking over ENet.
#
# Everything else in tests/ runs in one process, and prediction cannot: what it
# does is hide the time between two machines. So this starts a server, starts a
# client against it, and lets tests/prediction_probe.gd - loaded into both
# through the demo's --probe flag - drive the client's character and check the
# result from the inside. The client's exit code is the verdict.
#
# It runs twice: once on a clean loopback, and once through GFGD's debug
# delay-and-drop queue with the latency, jitter and loss a poor connection has.
# The second run is the one that matters; the first tells a network problem
# from a simulation one when the second fails.
#
# What this cannot tell you is how it feels - "rubber-bands at 2% loss" and
# "treacle when changing direction at 120 ms" need a person and a window, with
# the same --net-* flags passed to a windowed client.
#
# Run with:
#   godot --headless --path project --script res://tests/test_prediction.gd

const CONFIGURATIONS := [
	{ "name": "loopback", "latency": 0, "jitter": 0, "loss": 0 },
	{ "name": "50 ms each way, 10 ms jitter, 5% loss", "latency": 50, "jitter": 10, "loss": 5 },
]


func _initialize() -> void:
	var executable: String = OS.get_executable_path()
	var project: String = ProjectSettings.globalize_path("res://")
	var failed: Array[String] = []

	for index in CONFIGURATIONS.size():
		var configuration: Dictionary = CONFIGURATIONS[index]
		print("=== %s ===" % configuration["name"])

		# A different port each run, so a server still shutting down from the last
		# one cannot be the one the next client finds.
		var port: int = 27000 + index + (Time.get_ticks_msec() % 1000) * 10

		var network: PackedStringArray = [
			"--net-latency", str(configuration["latency"]),
			"--net-jitter", str(configuration["jitter"]),
			"--net-loss", str(configuration["loss"]),
			"--probe", "res://tests/prediction_probe.gd",
		]

		var server_arguments: PackedStringArray = ["--headless", "--path", project, "--", "--server", "--port", str(port)]
		server_arguments.append_array(network)
		# Piped, so the server's own chatter stays out of the report unless the
		# run fails and it is needed.
		var server: Dictionary = OS.execute_with_pipe(executable, server_arguments, false)
		if server.is_empty():
			print("    FAIL could not start the server")
			failed.append(configuration["name"])
			continue
		var server_pid: int = server["pid"]

		# The server needs a moment to be listening; the client retries nothing.
		OS.delay_msec(1500)

		var client_arguments: PackedStringArray = ["--headless", "--path", project, "--", "--join", "127.0.0.1", "--port", str(port)]
		client_arguments.append_array(network)

		var output: Array = []
		var exit_code: int = OS.execute(executable, client_arguments, output, true)
		OS.kill(server_pid)

		# Only the probe's own lines: the demo prints its lifecycle as it goes,
		# and that is noise here unless something went wrong.
		var text: String = "".join(output)
		for line in text.split("\n"):
			if exit_code != 0 or line.begins_with("    ") or line.begins_with("prediction probe"):
				print(line)

		if exit_code != 0:
			failed.append(configuration["name"])
			var server_output: FileAccess = server["stdio"]
			print("--- server output ---")
			print(server_output.get_as_text())

	if failed.is_empty():
		print("test_prediction: all checks passed")
		quit(0)
	else:
		print("test_prediction: FAILED in %s" % ", ".join(failed))
		quit(1)
