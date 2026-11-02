//
// kernel.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include "renderer.h"
#include <circle/bcmpropertytags.h>
#include <circle/machineinfo.h>
#include <circle/memory.h>

using namespace sppie;

// Start-up failure codes (ACT LED blinks)
static const unsigned FailSystem	= 1;
static const unsigned FailFrameBuffer	= 2;
static const unsigned FailReader	= 3;
static const unsigned FailCores		= 4;

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_pFrameBuffer (0),
	m_pReader (0),
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

	// 320 x 240 at 32 bits per pixel, twice as high for the flip. The
	// firmware scales it to the HDMI mode.
	m_pFrameBuffer = new CBcmFrameBuffer (ScreenWidth, ScreenHeight, 32,
					      ScreenWidth, ScreenHeight * 2);
	if (   m_pFrameBuffer == 0
	    || !m_pFrameBuffer->Initialize ()
	    || m_pFrameBuffer->GetPitch () < ScreenWidth * 4)
	{
		Fail (FailFrameBuffer);
	}
	m_pFrameBuffer->SetVirtualOffset (0, 0);

	m_pReader = new CSmiEventReader;
	if (m_pReader == 0 || !m_pReader->Initialize ())
	{
		Fail (FailReader);
	}

	unsigned nArmClockMHz = CMachineInfo::Get ()->GetClockRate (CLOCK_ID_ARM) / 1000000;

	m_pApp = new CSpPieApp (CMemorySystem::Get (), m_pFrameBuffer, m_pReader, nArmClockMHz);
	if (m_pApp == 0 || !m_pApp->Initialize ())	// starts cores 1 to 3; must be last
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

void CKernel::Fail (unsigned nCount)
{
	while (1)
	{
		m_ActLED.Blink (nCount, 200, 300);
		CTimer::SimpleMsDelay (1500);
	}
}
