//
// audiostream.h
//
// The sound path from the renderer core to the audio core.
//
// CAudioFifo is a single-producer, single-consumer ring of mono samples
// (signed 24-bit in an int32_t), with no lock. The renderer core writes
// each Spectrum frame's samples after it has run the frame through Fuse.
// When the ring is full the rest of the frame is dropped and counted.
//
// CAudioResampler is the consumer. It gives one output sample per call, at
// the output rate, read from the ring with a ratio near 1.
//
// Why a ratio. The samples are made in Spectrum time: 48,000 for each
// second of the Spectrum's clock, on every model, whatever its frame rate.
// The HDMI output takes 48,000 per second of the Pi's clock. The two
// clocks come from different crystals, so the ring would slowly fill or
// empty. The resampler keeps it near TargetFill: every ControlSamples
// outputs it averages the fill level and sets the ratio to
// 1 + (average fill - TargetFill) x Gain, limited to MaxAdjust either way.
// A ring that holds more than the target is read a little faster. The
// fill error then falls with a time constant of 1 / (48,000 x Gain), about
// 21 seconds, and a constant clock difference leaves an offset of the
// difference / Gain samples (about 100 samples for 100 ppm).
//
// Between input samples it uses 4-point cubic (Catmull-Rom)
// interpolation. When the ring runs empty it holds the last output and
// counts an underrun, then starts again once the ring holds TargetFill
// samples.
//
// No platform dependencies. Uses the GCC/Clang __atomic builtins.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_audiostream_h
#define _sppie_audiostream_h

#include <stdint.h>

namespace sppie {

class CAudioFifo
{
public:
	static const uint32_t Entries	= 1u << 14;	// 16,384 samples, 341 ms
	static const uint32_t Mask	= Entries - 1;

	CAudioFifo (void)	{ Reset (); }

	// Before either core uses the ring.
	void Reset (void)
	{
		m_P.nWrite = 0;
		m_P.nDropped = 0;
		m_C.nRead = 0;
	}

	// --- producer --------------------------------------------------------

	// Stores up to nCount samples and publishes them. Returns the number
	// stored; the rest are dropped and counted.
	uint32_t Write (const int32_t *pSamples, uint32_t nCount)
	{
		uint32_t nRead = __atomic_load_n (&m_C.nRead, __ATOMIC_ACQUIRE);
		uint32_t nFree = Entries - (m_P.nWrite - nRead);
		uint32_t nStore = nCount < nFree ? nCount : nFree;

		for (uint32_t i = 0; i < nStore; i++)
		{
			m_Entries[(m_P.nWrite + i) & Mask] = pSamples[i];
		}
		__atomic_store_n (&m_P.nWrite, m_P.nWrite + nStore, __ATOMIC_RELEASE);
		m_P.nDropped += nCount - nStore;

		return nStore;
	}

	uint32_t Dropped (void) const	{ return m_P.nDropped; }

	// --- consumer --------------------------------------------------------

	uint32_t Available (void) const
	{
		return __atomic_load_n (&m_P.nWrite, __ATOMIC_ACQUIRE) - m_C.nRead;
	}

	// The next sample. Only when Available () is not 0.
	int32_t Pop (void)
	{
		int32_t nSample = m_Entries[m_C.nRead & Mask];
		__atomic_store_n (&m_C.nRead, m_C.nRead + 1, __ATOMIC_RELEASE);
		return nSample;
	}

private:
	// The padding keeps each core's index in its own 64-byte cache line.
	struct TProducerSide
	{
		uint32_t	nWrite;
		uint32_t	nDropped;
		uint32_t	Pad[14];
	};

	struct TConsumerSide
	{
		uint32_t	nRead;
		uint32_t	Pad[15];
	};

	TProducerSide	m_P;
	TConsumerSide	m_C;
	int32_t		m_Entries[Entries];
};

class CAudioResampler
{
public:
	static const uint32_t TargetFill	= 2048;		// samples, about 43 ms
	static const uint32_t ControlSamples	= 480;		// 10 ms of output
	static constexpr double MaxAdjust	= 1e-3;		// 1,000 ppm
	static constexpr double Gain		= MaxAdjust / 1024.0;	// per sample of fill error
	static constexpr double FillSmoothing	= 1.0 / 64.0;	// per control step

	explicit CAudioResampler (CAudioFifo *pFifo);

	// The next output sample, signed 24-bit.
	int32_t Next (void);

	double Adjust (void) const		{ return m_fAdjust; }	// ratio - 1
	double AverageFill (void) const		{ return m_fFill; }
	uint32_t Underruns (void) const		{ return m_nUnderruns; }
	bool Playing (void) const		{ return m_bPlaying; }

private:
	void Control (void);

private:
	CAudioFifo	*m_pFifo;

	bool		m_bPlaying;
	int32_t		m_x[4];		// the input samples around the position
	double		m_fFraction;	// position between m_x[1] and m_x[2]
	int32_t		m_nLast;	// the last output
	uint32_t	m_nControlCount;
	double		m_fFill;	// average fill level
	double		m_fAdjust;
	uint32_t	m_nUnderruns;
};

}

#endif
