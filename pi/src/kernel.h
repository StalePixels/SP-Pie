//
// kernel.h
//
// Circle kernel: brings up the system on core 0, then hands over to
// CSpPieApp, which runs the renderer on core 0, event intake on core 1, the
// ULA clock on core 2 and audio output on core 3.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_kernel_h
#define _sppie_kernel_h

#include "bootconfig.h"
#include "smieventreader.h"
#include "sppieapp.h"
#include <circle/actled.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/nulldevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	// Blinks the ACT LED nCount times, for ever. The board has no UART and
	// the screen may not be up, so this is how a start-up failure shows.
	void Fail (unsigned nCount);

	sppie::TMachineChoice MachineChoice (void) const;

	// Reads the [sp-pie] section of config.txt on the SD card into
	// m_BootConfig, once at start-up. Sets m_pConfigNote.
	void ReadBootConfig (void);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CNullDevice		m_NullDevice;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;

	sppie::TBootConfig	m_BootConfig;
	char			m_ConfigNote[64];	// for the splash screen

	CBcmFrameBuffer		*m_pFrameBuffer;
	sppie::CSmiEventReader	*m_pReader;
	CHDMISoundBaseDevice	*m_pSound;	// 0 when HDMI sound did not start
	sppie::CSpPieApp	*m_pApp;
};

#endif
