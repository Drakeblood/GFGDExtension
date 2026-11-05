#ifndef GFGD_LOG_WINDOW_H
#define GFGD_LOG_WINDOW_H

namespace GFGD
{
// A separate terminal window showing the game's log as it is written - what
// starting the game with --log (or -log) opens. Windows only; elsewhere the
// game is started from a terminal anyway.
namespace LogWindow
{
	// Opens the window unless the process already has a terminal. True if one
	// was opened.
	bool open(const wchar_t* title);
}
}

#endif
