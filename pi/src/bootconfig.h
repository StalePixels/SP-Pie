//
// bootconfig.h
//
// SP-Pie's start-up settings, from the [sp-pie] section of config.txt on
// the SD card (README.md, Settings in config.txt).
//
// config.txt is the Raspberry Pi firmware's own configuration file. It is
// divided into sections by lines in square brackets, which the firmware
// reads as conditions ([pi4], [all] and so on). The [sp-pie] section holds
// keys the firmware does not use. It goes last in the file; settings for
// the firmware after it need an [all] line first.
//
// The section takes one key per feature and the model choice:
//
//   feature1=0x07     the default of each control-port feature
//   feature2=0x00     (docs/control-ports.md): feature0, feature1,
//   feature3=0x01     feature2, feature3 and feature255. Decimal, or hex
//   feature255=0x01   with 0x.
//   machine=auto      auto, auto128, 48, 128 or plus3 (machine.h)
//
// Section and key names are not case-sensitive. Blank lines and lines
// that start with # are skipped. A value with reserved bits set, a
// reserved feature, an unknown key or an unknown model is ignored, and
// the first ignored line is kept for the splash screen. A key given twice
// takes its last value.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_bootconfig_h
#define _sppie_bootconfig_h

#include "controlports.h"
#include "machine.h"
#include <stddef.h>
#include <stdint.h>

namespace sppie {

struct TBootConfig
{
	bool		bSection;	// the file has an [sp-pie] section
	CControlPorts::TDefaults Defaults;
	bool		bHaveMachine;	// machine= was given and is valid
	TMachineChoice	Machine;
	unsigned	nIgnored;	// lines of the section that were ignored
	char		Ignored[40];	// the first of them, cut to fit
};

// The built-in settings: no section, the built-in defaults, no model.
void DefaultBootConfig (TBootConfig &rConfig);

// Reads the [sp-pie] section of config.txt, nLength bytes at pText, over
// the built-in settings.
void ParseBootConfig (const char *pText, size_t nLength, TBootConfig &rConfig);

}

#endif
