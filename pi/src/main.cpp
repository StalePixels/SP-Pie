//
// main.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include <circle/startup.h>

int main (void)
{
	// CKernel is never destroyed: Run () does not return in normal use.
	CKernel Kernel;
	if (!Kernel.Initialize ())
	{
		halt ();
		return EXIT_HALT;
	}

	TShutdownMode ShutdownMode = Kernel.Run ();

	switch (ShutdownMode)
	{
	case ShutdownReboot:
		reboot ();
		return EXIT_REBOOT;

	case ShutdownHalt:
	default:
		halt ();
		return EXIT_HALT;
	}
}
