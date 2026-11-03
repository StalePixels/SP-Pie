//
// controlports.h
//
// SP-Pie's control ports (docs/control-ports.md). The Spectrum writes a
// feature number to the feature select port 0x043B, then the feature's
// value to the feature data port 0x053B. Both are decoded on all 16
// address bits. The Pi sees the writes in the event stream; the CPLD
// treats them like any other I/O write.
//
//  Feature 0, SP-Pie:            no bits yet
//  Feature 1, attached devices:  bit 0 = AY, bit 1 = TurboSound,
//                                bit 2 = beeper, bit 3 = Covox on 0xFB,
//                                bit 4 = Covox on 0xDD, bit 5 = Covox on 0xDF
//  Feature 2, border:            bit 0 = border extension
//  Feature 3, display:           bit 0 = interlace detection, bit 1 =
//                                interlace mixing always, bits 3 and 2 =
//                                mixing mode (00 average, 01 CRT-style,
//                                10 double high, 11 reserved), bits 5 and
//                                4 = detection method (00 per line, 01 per
//                                page, 10 and 11 reserved), bit 6 = double
//                                high first line from bank 7
//  Feature 0xFF, debug:          bit 0 = HUD (the overlay in the side areas)
//
// Other features and other bits are reserved: the data write is ignored,
// or the bit is dropped. A Spectrum reset restores the defaults and
// selects feature 0. The defaults are the built-in ones unless the
// [sp-pie] section of config.txt gives others (bootconfig.h,
// SetDefaults ()).
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_controlports_h
#define _sppie_controlports_h

#include <stdint.h>

namespace sppie {

class CControlPorts
{
public:
	static const uint16_t FeatureSelectPort	= 0x043B;
	static const uint16_t FeatureDataPort	= 0x053B;

	static const uint8_t FeatureSpPie	= 0;
	static const uint8_t FeatureDevices	= 1;
	static const uint8_t FeatureBorder	= 2;
	static const uint8_t FeatureDisplay	= 3;
	static const uint8_t FeatureDebug	= 0xFF;

	static const uint8_t DeviceAy		= 0x01;
	static const uint8_t DeviceTurboSound	= 0x02;
	static const uint8_t DeviceBeeper	= 0x04;
	static const uint8_t DeviceCovoxFB	= 0x08;
	static const uint8_t DeviceCovoxDD	= 0x10;
	static const uint8_t DeviceCovoxDF	= 0x20;
	static const uint8_t DeviceBits		= 0x3F;
	static const uint8_t DevicesDefault	= DeviceAy | DeviceTurboSound | DeviceBeeper;

	static const uint8_t BorderExtension	= 0x01;
	static const uint8_t BorderBits		= 0x01;
	static const uint8_t BorderDefault	= 0x00;

	static const uint8_t DisplayInterlaceAuto = 0x01;
	static const uint8_t DisplayInterlaceOn	= 0x02;
	static const uint8_t DisplayModeMask	= 0x0C;
	static const uint8_t DisplayModeAverage	= 0x00;	// the two frames, half each
	static const uint8_t DisplayModeCrt	= 0x04;	// the newest 5/8, the one before 3/8
	static const uint8_t DisplayModeDoubleHigh = 0x08;	// bank 5 and bank 7 on alternate lines
	static const uint8_t DisplayModeReserved = 0x0C;
	static const uint8_t DisplayDetectMask	= 0x30;	// the detection method
	static const uint8_t DisplayDetectLine	= 0x00;	// each line by its own bank
	static const uint8_t DisplayDetectPage	= 0x10;	// the whole frame when any line flips
	static const uint8_t DisplayFirstBank7	= 0x40;	// double high: bank 7 on the first line
	static const uint8_t DisplayBits	= 0x7F;
	static const uint8_t DisplayDefault	= DisplayInterlaceAuto | DisplayModeAverage | DisplayDetectPage;

	static const uint8_t DebugHud		= 0x01;
	static const uint8_t DebugBits		= 0x01;
	static const uint8_t DebugDefault	= DebugHud;

	// The value each feature takes at start-up and at every reset.
	struct TDefaults
	{
		uint8_t	Devices;
		uint8_t	Border;
		uint8_t	Display;
		uint8_t	Debug;
	};

	// The built-in defaults.
	static TDefaults BuiltInDefaults (void)
	{
		TDefaults Defaults = { DevicesDefault, BorderDefault, DisplayDefault, DebugDefault };
		return Defaults;
	}

	// The reserved bits of a feature's value, or 0xFF for a feature
	// number that is reserved. Feature 0 has every bit reserved.
	static uint8_t ReservedBits (uint8_t nFeature)
	{
		switch (nFeature)
		{
		case FeatureSpPie:	return 0xFF;
		case FeatureDevices:	return (uint8_t) ~DeviceBits;
		case FeatureBorder:	return (uint8_t) ~BorderBits;
		case FeatureDisplay:	return (uint8_t) ~DisplayBits;
		case FeatureDebug:	return (uint8_t) ~DebugBits;
		default:		return 0xFF;
		}
	}

	// A value that the feature takes: no reserved bit set, and for the
	// display feature not the reserved mode.
	static bool Valid (uint8_t nFeature, uint8_t nValue)
	{
		if ((nValue & ReservedBits (nFeature)) != 0)
		{
			return false;
		}
		return !(nFeature == FeatureDisplay
			 && (   (nValue & DisplayModeMask) == DisplayModeReserved
			     || (nValue & DisplayDetectMask) > DisplayDetectPage));
	}

	CControlPorts (void)
	:	m_Defaults (BuiltInDefaults ())
	{
		Reset ();
	}

	// New defaults, with their reserved bits dropped. They apply from the
	// next Reset ().
	void SetDefaults (const TDefaults &rDefaults)
	{
		m_Defaults.Devices = rDefaults.Devices & DeviceBits;
		m_Defaults.Border = rDefaults.Border & BorderBits;
		if (Valid (FeatureDisplay, rDefaults.Display & DisplayBits))
		{
			m_Defaults.Display = rDefaults.Display & DisplayBits;
		}
		m_Defaults.Debug = rDefaults.Debug & DebugBits;
	}

	const TDefaults &Defaults (void) const	{ return m_Defaults; }

	// The defaults, and feature 0 selected.
	void Reset (void)
	{
		m_nFeature = FeatureSpPie;
		m_nDevices = m_Defaults.Devices;
		m_nBorder = m_Defaults.Border;
		m_nDisplay = m_Defaults.Display;
		m_nDebug = m_Defaults.Debug;
	}

	// An I/O write. TRUE when it was to a control port.
	bool Write (uint16_t nPort, uint8_t nData)
	{
		if (nPort == FeatureSelectPort)
		{
			m_nFeature = nData;
			return true;
		}
		if (nPort != FeatureDataPort)
		{
			return false;
		}

		switch (m_nFeature)
		{
		case FeatureDevices:
			m_nDevices = nData & DeviceBits;
			break;

		case FeatureBorder:
			m_nBorder = nData & BorderBits;
			break;

		case FeatureDisplay:
			// A reserved mode or detection method leaves the feature as
			// it was.
			if (Valid (FeatureDisplay, nData & DisplayBits))
			{
				m_nDisplay = nData & DisplayBits;
			}
			break;

		case FeatureDebug:
			m_nDebug = nData & DebugBits;
			break;

		default:		// feature 0 and the reserved features
			break;
		}
		return true;
	}

	uint8_t Feature (void) const		{ return m_nFeature; }
	uint8_t Devices (void) const		{ return m_nDevices; }
	uint8_t Border (void) const		{ return m_nBorder; }
	uint8_t Display (void) const		{ return m_nDisplay; }
	uint8_t Debug (void) const		{ return m_nDebug; }
	bool Hud (void) const			{ return (m_nDebug & DebugHud) != 0; }
	bool BorderExtended (void) const	{ return (m_nBorder & BorderExtension) != 0; }

private:
	TDefaults m_Defaults;
	uint8_t	m_nFeature;
	uint8_t	m_nDevices;
	uint8_t	m_nBorder;
	uint8_t	m_nDisplay;
	uint8_t	m_nDebug;
};

}

#endif
