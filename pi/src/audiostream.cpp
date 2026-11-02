//
// audiostream.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "audiostream.h"

namespace sppie {

static const int32_t SampleMax = 0x7FFFFF;
static const int32_t SampleMin = -0x800000;

CAudioResampler::CAudioResampler (CAudioFifo *pFifo)
:	m_pFifo (pFifo),
	m_bPlaying (false),
	m_fFraction (0.0),
	m_nLast (0),
	m_nControlCount (0),
	m_fFill (0.0),
	m_fAdjust (0.0),
	m_nUnderruns (0)
{
	for (unsigned i = 0; i < 4; i++)
	{
		m_x[i] = 0;
	}
}

int32_t CAudioResampler::Next (void)
{
	if (!m_bPlaying)
	{
		if (m_pFifo->Available () < TargetFill)
		{
			return m_nLast;
		}

		// Start from the held output, so the restart does not step.
		m_x[0] = m_nLast;
		m_x[1] = m_nLast;
		m_x[2] = m_pFifo->Pop ();
		m_x[3] = m_pFifo->Pop ();
		m_fFraction = 0.0;
		m_fFill = m_pFifo->Available ();
		m_nControlCount = 0;
		m_bPlaying = true;
	}

	// Catmull-Rom between m_x[1] and m_x[2].
	double t = m_fFraction;
	double x0 = m_x[0], x1 = m_x[1], x2 = m_x[2], x3 = m_x[3];
	double c1 = 0.5 * (x2 - x0);
	double c2 = x0 - 2.5 * x1 + 2.0 * x2 - 0.5 * x3;
	double c3 = 0.5 * (x3 - x0) + 1.5 * (x1 - x2);
	double y = ((c3 * t + c2) * t + c1) * t + x1;

	int32_t nOut = y >= SampleMax ? SampleMax
		     : y <= SampleMin ? SampleMin
		     : (int32_t) (y < 0 ? y - 0.5 : y + 0.5);
	m_nLast = nOut;

	m_fFraction += 1.0 + m_fAdjust;
	while (m_fFraction >= 1.0)
	{
		if (m_pFifo->Available () == 0)
		{
			m_nUnderruns++;
			m_bPlaying = false;
			return nOut;
		}
		m_fFraction -= 1.0;
		m_x[0] = m_x[1];
		m_x[1] = m_x[2];
		m_x[2] = m_x[3];
		m_x[3] = m_pFifo->Pop ();
	}

	if (++m_nControlCount == ControlSamples)
	{
		m_nControlCount = 0;
		Control ();
	}

	return nOut;
}

void CAudioResampler::Control (void)
{
	m_fFill += ((double) m_pFifo->Available () - m_fFill) * FillSmoothing;

	double fAdjust = (m_fFill - (double) TargetFill) * Gain;
	if (fAdjust > MaxAdjust)
	{
		fAdjust = MaxAdjust;
	}
	else if (fAdjust < -MaxAdjust)
	{
		fAdjust = -MaxAdjust;
	}
	m_fAdjust = fAdjust;
}

}
