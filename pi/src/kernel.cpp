//
// kernel.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include "picture.h"
#include <circle/bcmpropertytags.h>
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/util.h>

using namespace sppie;

// Start-up failure codes (ACT LED blinks)
static const unsigned FailSystem	= 1;
static const unsigned FailFrameBuffer	= 2;
static const unsigned FailReader	= 3;
static const unsigned FailCores		= 4;
static const unsigned FailFuse		= 5;

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_EMMC (&m_Interrupt, &m_Timer, 0),
	m_pFrameBuffer (0),
	m_pReader (0),
	m_pSound (0),
	m_pApp (0)
{
	m_ActLED.Blink (1);	// show that the kernel is running
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	if (   !m_Logger.Initialize (&m_NullDevice)
	    || !m_Interrupt.Initialize ()
	    || !m_Timer.Initialize ())
	{
		Fail (FailSystem);
	}

	ReadBootConfig ();

	// The HDMI mode's size, for the splash screen. Before a frame buffer
	// is allocated, the firmware gives the size of the display mode here
	// (Circle's CBcmFrameBuffer reads it the same way to choose its size
	// when it is given none).
	unsigned nHdmiWidth = 0, nHdmiHeight = 0;
	{
		CBcmPropertyTags Tags;
		TPropertyTagDisplayDimensions Dimensions;
		if (Tags.GetTag (PROPTAG_GET_DISPLAY_DIMENSIONS, &Dimensions, sizeof Dimensions))
		{
			nHdmiWidth = Dimensions.nWidth;
			nHdmiHeight = Dimensions.nHeight;
		}
	}

	// 960 x 540 at 32 bits per pixel (picture.h: 2 x 2 frame buffer pixels
	// for each display pixel), twice as high for the flip. The firmware
	// scales it to the HDMI mode, x2 for 1080p, and keeps its shape.
	m_pFrameBuffer = new CBcmFrameBuffer (FrameBufferWidth, FrameBufferHeight, 32,
					      FrameBufferWidth, FrameBufferHeight * 2);
	if (   m_pFrameBuffer == 0
	    || !m_pFrameBuffer->Initialize ()
	    || m_pFrameBuffer->GetPitch () < FrameBufferWidth * 4
	    || m_pFrameBuffer->GetPitch () % 8 != 0)
	{
		Fail (FailFrameBuffer);
	}
	m_pFrameBuffer->SetVirtualOffset (0, 0);

	m_pReader = new CSmiEventReader;
	if (m_pReader == 0 || !m_pReader->Initialize ())
	{
		Fail (FailReader);
	}

	// HDMI sound at 48 kHz, in polling mode: core 3 feeds its FIFO, so it
	// needs no interrupt and no DMA channel. A display without sound, or
	// a link in DVI mode, makes Start () fail; the kernel then runs
	// without sound.
	m_pSound = new CHDMISoundBaseDevice (48000);
	if (m_pSound != 0 && !m_pSound->Start ())
	{
		delete m_pSound;
		m_pSound = 0;
	}

	unsigned nArmClockMHz = CMachineInfo::Get ()->GetClockRate (CLOCK_ID_ARM) / 1000000;

	m_pApp = new CSpPieApp (CMemorySystem::Get (), m_pFrameBuffer, m_pReader, m_pSound,
				nArmClockMHz, MachineChoice (), nHdmiWidth, nHdmiHeight,
				m_BootConfig.Defaults, m_ConfigNote, m_BootConfig.nIgnored != 0);
	if (m_pApp == 0 || !m_pApp->InitializeRenderer ())
	{
		Fail (FailFuse);
	}
	if (!m_pApp->Initialize ())	// starts cores 1 to 3; must be last
	{
		Fail (FailCores);
	}

	return TRUE;
}

TShutdownMode CKernel::Run (void)
{
	m_pApp->Run (0);	// renderer; does not return

	return ShutdownHalt;
}

// machine= in the [sp-pie] section of config.txt, else the "machine"
// option in cmdline.txt: auto (the default), auto128, 48, 128 or plus3.
// See machine.h, TMachineChoice.
TMachineChoice CKernel::MachineChoice (void) const
{
	if (m_BootConfig.bHaveMachine)
	{
		return m_BootConfig.Machine;
	}

	const char *pValue = m_Options.GetAppOptionString ("machine", "auto");

	if (strcmp (pValue, "auto128") == 0)	return ChooseAuto128;
	if (strcmp (pValue, "48") == 0)		return Choose48K;
	if (strcmp (pValue, "128") == 0)	return Choose128K;
	if (strcmp (pValue, "plus3") == 0)	return ChoosePlus3;

	return ChooseAuto;
}

// Appends pText to the splash note, as far as it fits.
static void AppendNote (char *pNote, size_t nSize, const char *pText)
{
	size_t n = 0;
	while (n < nSize - 1 && pNote[n] != '\0')
	{
		n++;
	}
	while (n < nSize - 1 && *pText != '\0')
	{
		pNote[n++] = *pText++;
	}
	pNote[n] = '\0';
}

void CKernel::ReadBootConfig (void)
{
	// config.txt is a small text file; a longer one has its tail ignored.
	static char Buffer[16384];

	DefaultBootConfig (m_BootConfig);
	m_ConfigNote[0] = '\0';

	FIL File;
	UINT nRead = 0;
	if (   !m_EMMC.Initialize ()
	    || f_mount (&m_FileSystem, "SD:", 1) != FR_OK
	    || f_open (&File, "SD:/config.txt", FA_READ) != FR_OK)
	{
		AppendNote (m_ConfigNote, sizeof m_ConfigNote, "config.txt: not read");
		return;
	}
	FRESULT Result = f_read (&File, Buffer, sizeof Buffer, &nRead);
	f_close (&File);
	f_mount (0, "SD:", 0);
	if (Result != FR_OK)
	{
		AppendNote (m_ConfigNote, sizeof m_ConfigNote, "config.txt: not read");
		return;
	}

	ParseBootConfig (Buffer, nRead, m_BootConfig);
	if (m_BootConfig.nIgnored != 0)
	{
		AppendNote (m_ConfigNote, sizeof m_ConfigNote, "config.txt ignored: ");
		AppendNote (m_ConfigNote, sizeof m_ConfigNote, m_BootConfig.Ignored);
	}
	else if (m_BootConfig.bSection)
	{
		AppendNote (m_ConfigNote, sizeof m_ConfigNote, "config.txt: [sp-pie] read");
	}
	else
	{
		AppendNote (m_ConfigNote, sizeof m_ConfigNote, "config.txt: no [sp-pie] section");
	}
}

void CKernel::Fail (unsigned nCount)
{
	while (1)
	{
		m_ActLED.Blink (nCount, 200, 300);
		CTimer::SimpleMsDelay (1500);
	}
}
