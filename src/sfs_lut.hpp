#pragma once
// Shared sine / cosine / Hann lookup tables for SFS modules.
// Initialized once at static-init time and reused across modules.
//
// Each table covers a full period (phase 0..1) with linear interpolation
// between samples. 1024 entries gives a worst-case interpolation error
// well below 16-bit audio noise floor for the sine/cosine tables.

#include <cmath>

namespace sfs_lut {

constexpr int SIZE = 1024;

struct Tables {
	float sine[SIZE + 1];   // sin(phase * 2π) for phase ∈ [0,1]
	float cosine[SIZE + 1]; // cos(phase * 2π) for phase ∈ [0,1]
	float hann[SIZE + 1];   // 0.5 * (1 - cos(phase * 2π))
	Tables() {
		for (int i = 0; i <= SIZE; i++) {
			float phase = (float)i / (float)SIZE;
			float angle = phase * 2.f * (float)M_PI;
			sine[i]   = std::sin(angle);
			cosine[i] = std::cos(angle);
			hann[i]   = 0.5f * (1.f - cosine[i]);
		}
	}
};

inline const Tables& tables() {
	static const Tables t;
	return t;
}

// Wrap phase into [0,1) and look up with linear interpolation.
inline float lookup(const float* lut, float phase) {
	// Wrap (cheap modulo for small overshoots; assume |phase| not huge)
	phase -= (int)phase;
	if (phase < 0.f) phase += 1.f;
	float idxF = phase * (float)SIZE;
	int idx = (int)idxF;
	float frac = idxF - (float)idx;
	return lut[idx] + (lut[idx + 1] - lut[idx]) * frac;
}

inline float sine(float phase)   { return lookup(tables().sine,   phase); }
inline float cosine(float phase) { return lookup(tables().cosine, phase); }
inline float hann(float phase) {
	// Hann is non-periodic in our usage (phase ∈ [0,1] over a grain's
	// lifetime), so don't wrap — clamp instead.
	if (phase < 0.f) phase = 0.f;
	else if (phase > 1.f) phase = 1.f;
	float idxF = phase * (float)SIZE;
	int idx = (int)idxF;
	float frac = idxF - (float)idx;
	return tables().hann[idx] + (tables().hann[idx + 1] - tables().hann[idx]) * frac;
}

} // namespace sfs_lut
