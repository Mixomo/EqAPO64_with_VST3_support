//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Helpers
// Filename    : public.sdk/source/vst/utility/midiconvert.h
// Created by  : Steinberg, 03/2026
// Description : optional helper
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/fplatform.h"
#include <cstdint>
#include <type_traits>
#include <limits>

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {

//------------------------------------------------------------------------
SMTG_CONSTEXPR uint16_t upsample7To16Bit (uint8_t v)
{
	uint16_t result = (v << 9u);
	if (v > 64)
	{
		auto bits = v & 0x3F; // 6 bits
		result |= (bits << 3u) | (bits >> 3u);
	}

	return result;
}

//------------------------------------------------------------------------
SMTG_CONSTEXPR uint32_t upsample7To32Bit (uint8_t v)
{
	uint32_t result = (uint32_t (v) << 25u);
	if (v > 64)
	{
		uint32_t bits = (v & 0x3F);
		bits |= (bits << 6); // 12 bits
		result |= (bits << 13u) | (bits << 1) | (bits >> 11u);
	}

	return result;
}

//------------------------------------------------------------------------
SMTG_CONSTEXPR uint32_t upsample14To32Bit (uint16_t v)
{
	uint32_t result = (v << 18u);

	if (v > 8192)
	{
		auto bits = v & 0x1FFF; // 13 bits
		result |= (bits << 5u) | (bits >> 8u);
	}

	return result;
}

//------------------------------------------------------------------------
template <typename T, typename F> // F must be float or double
SMTG_CONSTEXPR T midiIntegralFromNormalized (F f)
{
	static_assert (std::is_integral_v<T>, "T must be an integral type");
	static_assert (sizeof (T) >= 2, "T must provide at least 16 bits of resolution");
	static_assert (std::is_floating_point_v<F>, "F must be float or double");

	if (f <= F (0))
		return 0;

	if (f >= F (1))
		return std::numeric_limits<T>::max ();

	SMTG_CONSTEXPR auto max = std::numeric_limits<T>::max ();

	if (f <= F (0.5))
	{
		SMTG_CONSTEXPR auto scale = static_cast<double> (max) + 1.;
		const double d = f * scale;
		return static_cast<T> (d);
	}

	SMTG_CONSTEXPR auto mid = (max >> 1) + 1;
	SMTG_CONSTEXPR auto scale = static_cast<double> (max);

	const double d = (f - 0.5) * scale;
	return mid + static_cast<T> (d);
}

//------------------------------------------------------------------------
template <typename T, typename F> // F must be float or double
SMTG_CONSTEXPR F midiIntegralToNormalized (T value)
{
	static_assert (std::is_integral_v<T>, "T must be an integral type");
	static_assert (sizeof (T) >= 2, "T must provide at least 16 bits of resolution");
	static_assert (std::is_floating_point_v<F>, "F must be float or double");

	SMTG_CONSTEXPR auto max = std::numeric_limits<T>::max ();
	SMTG_CONSTEXPR auto center = (max >> 1) + 1;

	if (value <= center)
		return static_cast<F> (value / static_cast<double> (center) / 2.);
	else
		return static_cast<F> (value / static_cast<double> (max));
}

//------------------------------------------------------------------------
template <typename F> // F must be float or double
SMTG_CONSTEXPR F midi7BitToNormalized (uint8_t value)
{
	static_assert (std::is_floating_point_v<F>, "F must be float or double");

	const uint32_t hires = upsample7To32Bit (value);
	return midiIntegralToNormalized<uint32_t, F> (hires);
}

//------------------------------------------------------------------------
template <typename F> // F must be float or double
SMTG_CONSTEXPR F midi14BitToNormalized (uint16_t value)
{
	static_assert (std::is_floating_point_v<F>, "F must be float or double");

	const uint32_t hires = upsample14To32Bit (value);
	return midiIntegralToNormalized<uint32_t, F> (hires);
}

//------------------------------------------------------------------------
template <typename F> // F must be float or double
SMTG_CONSTEXPR uint8_t midi7BitFromNormalized (F f)
{
	static_assert (std::is_floating_point_v<F>, "F must be float or double");

	const uint32_t hires = midiIntegralFromNormalized<uint32_t, F> (f);
	return (uint8_t) (hires >> 25u);
}

//------------------------------------------------------------------------
template <typename F> // F must be float or double
SMTG_CONSTEXPR uint16_t midi14BitFromNormalized (F f)
{
	static_assert (std::is_floating_point_v<F>, "F must be float or double");

	const uint32_t hires = midiIntegralFromNormalized<uint32_t, F> (f);
	return (uint16_t) (hires >> 18u);
}

//------------------------------------------------------------------------
} // Vst
} // Steinberg
