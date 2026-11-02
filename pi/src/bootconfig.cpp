//
// bootconfig.cpp
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "bootconfig.h"

namespace sppie {

static const char Section[] = "sp-pie";

static bool IsSpace (char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char Lower (char c)
{
	return ('A' <= c && c <= 'Z') ? (char) (c + 'a' - 'A') : c;
}

// [pStart, pEnd) with the white space around it removed.
static void Trim (const char *&pStart, const char *&pEnd)
{
	while (pStart < pEnd && IsSpace (*pStart))
	{
		pStart++;
	}
	while (pEnd > pStart && IsSpace (pEnd[-1]))
	{
		pEnd--;
	}
}

// The trimmed text equals pName, not counting case.
static bool Match (const char *pStart, const char *pEnd, const char *pName)
{
	Trim (pStart, pEnd);
	for (; pStart < pEnd; pStart++, pName++)
	{
		if (*pName == '\0' || Lower (*pStart) != Lower (*pName))
		{
			return false;
		}
	}
	return *pName == '\0';
}

// A number of 0 to nMax: decimal, or hex after 0x.
static bool ParseNumber (const char *pStart, const char *pEnd, unsigned nMax, unsigned &rValue)
{
	Trim (pStart, pEnd);
	unsigned nBase = 10;
	if (pEnd - pStart > 2 && pStart[0] == '0' && Lower (pStart[1]) == 'x')
	{
		nBase = 16;
		pStart += 2;
	}
	if (pStart == pEnd)
	{
		return false;
	}

	unsigned nValue = 0;
	for (; pStart < pEnd; pStart++)
	{
		char c = Lower (*pStart);
		unsigned nDigit;
		if ('0' <= c && c <= '9')
		{
			nDigit = (unsigned) (c - '0');
		}
		else if (nBase == 16 && 'a' <= c && c <= 'f')
		{
			nDigit = (unsigned) (c - 'a' + 10);
		}
		else
		{
			return false;
		}
		nValue = nValue * nBase + nDigit;
		if (nValue > nMax)
		{
			return false;
		}
	}

	rValue = nValue;
	return true;
}

static bool ParseMachine (const char *pStart, const char *pEnd, TMachineChoice &rChoice)
{
	static const struct
	{
		const char	*pName;
		TMachineChoice	Choice;
	}
	Names[] =
	{
		{ "auto",	ChooseAuto },
		{ "auto128",	ChooseAuto128 },
		{ "48",		Choose48K },
		{ "128",	Choose128K },
		{ "plus3",	ChoosePlus3 },
	};
	for (const auto &rName : Names)
	{
		if (Match (pStart, pEnd, rName.pName))
		{
			rChoice = rName.Choice;
			return true;
		}
	}
	return false;
}

// feature<n>=<value>. FALSE when the line is not a valid one.
static bool ParseFeature (const char *pKey, const char *pKeyEnd, const char *pValue,
			  const char *pValueEnd, CControlPorts::TDefaults &rDefaults)
{
	static const char Prefix[] = "feature";
	Trim (pKey, pKeyEnd);
	const size_t nPrefix = sizeof Prefix - 1;
	if ((size_t) (pKeyEnd - pKey) <= nPrefix)
	{
		return false;
	}
	for (size_t i = 0; i < nPrefix; i++)
	{
		if (Lower (pKey[i]) != Prefix[i])
		{
			return false;
		}
	}
	for (const char *p = pKey + nPrefix; p < pKeyEnd; p++)
	{
		if (*p < '0' || *p > '9')
		{
			return false;		// feature numbers are decimal
		}
	}

	unsigned nFeature, nValue;
	if (   !ParseNumber (pKey + nPrefix, pKeyEnd, 255, nFeature)
	    || !ParseNumber (pValue, pValueEnd, 255, nValue)
	    || !CControlPorts::Valid ((uint8_t) nFeature, (uint8_t) nValue))
	{
		return false;
	}

	switch (nFeature)
	{
	case CControlPorts::FeatureSpPie:	break;	// no bits: 0 is its only value
	case CControlPorts::FeatureDevices:	rDefaults.Devices = (uint8_t) nValue;	break;
	case CControlPorts::FeatureBorder:	rDefaults.Border = (uint8_t) nValue;	break;
	case CControlPorts::FeatureDisplay:	rDefaults.Display = (uint8_t) nValue;	break;
	case CControlPorts::FeatureDebug:	rDefaults.Debug = (uint8_t) nValue;	break;
	default:				return false;
	}
	return true;
}

static void Ignore (const char *pLine, const char *pLineEnd, TBootConfig &rConfig)
{
	if (rConfig.nIgnored++ != 0)
	{
		return;
	}
	size_t n = 0;
	for (; pLine < pLineEnd && n < sizeof rConfig.Ignored - 1; pLine++)
	{
		rConfig.Ignored[n++] = *pLine;
	}
	rConfig.Ignored[n] = '\0';
}

void DefaultBootConfig (TBootConfig &rConfig)
{
	rConfig.bSection = false;
	rConfig.Defaults = CControlPorts::BuiltInDefaults ();
	rConfig.bHaveMachine = false;
	rConfig.Machine = ChooseAuto;
	rConfig.nIgnored = 0;
	rConfig.Ignored[0] = '\0';
}

void ParseBootConfig (const char *pText, size_t nLength, TBootConfig &rConfig)
{
	DefaultBootConfig (rConfig);

	bool bInSection = false;
	const char *p = pText, *pEnd = pText + nLength;
	while (p < pEnd)
	{
		const char *pLine = p;
		while (p < pEnd && *p != '\n' && *p != '\0')
		{
			p++;
		}
		const char *pLineEnd = p;
		if (p < pEnd)
		{
			p++;
		}

		Trim (pLine, pLineEnd);
		if (pLine == pLineEnd || *pLine == '#')
		{
			continue;
		}

		if (*pLine == '[')
		{
			bInSection = pLineEnd[-1] == ']' && Match (pLine + 1, pLineEnd - 1, Section);
			rConfig.bSection |= bInSection;
			continue;
		}
		if (!bInSection)
		{
			continue;
		}

		const char *pEquals = pLine;
		while (pEquals < pLineEnd && *pEquals != '=')
		{
			pEquals++;
		}
		if (pEquals == pLineEnd)
		{
			Ignore (pLine, pLineEnd, rConfig);
			continue;
		}

		bool bValid;
		if (Match (pLine, pEquals, "machine"))
		{
			TMachineChoice Choice;
			bValid = ParseMachine (pEquals + 1, pLineEnd, Choice);
			if (bValid)
			{
				rConfig.Machine = Choice;
				rConfig.bHaveMachine = true;
			}
		}
		else
		{
			bValid = ParseFeature (pLine, pEquals, pEquals + 1, pLineEnd, rConfig.Defaults);
		}
		if (!bValid)
		{
			Ignore (pLine, pLineEnd, rConfig);
		}
	}
}

}
