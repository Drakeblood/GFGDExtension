// Kept apart from the rest: windows.h defines macros (min, max, ERROR...) that
// collide with Godot's headers, so it is the only thing this file includes
// besides the declaration it implements.
#include "debug/log_window.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#endif

bool GFGD::LogWindow::open(const wchar_t* title)
{
#ifdef _WIN32
	// Already attached to a terminal (the *_console.exe wrapper, a shell, the
	// editor's output): the log is visible there.
	if (GetConsoleWindow() != nullptr) { return false; }
	if (!AllocConsole()) { return false; }

	// Godot's terminal logger asks GetStdHandle() for every line, so the new
	// console's handles are all it needs; the C streams are pointed there too
	// for anything printing through stdio.
	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	SetConsoleTitleW(title);
	SetConsoleOutputCP(CP_UTF8);

	// Colours, as the terminal logger writes ANSI sequences.
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD console_mode = 0;
	if (GetConsoleMode(output, &console_mode))
	{
		SetConsoleMode(output, console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}
	return true;
#else
	(void)title;
	return false;
#endif
}
