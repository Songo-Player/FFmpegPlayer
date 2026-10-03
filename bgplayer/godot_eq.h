// Godot's 10 band equalizer (AudioEffectEQ10), ported so the background player
// sounds the same as Songo with its EQ on. The filter design and processing
// follow servers/audio/effects/eq_filter.cpp and audio_effect_eq.cpp from
// Godot 4.3-stable; only the Godot types are swapped for standard ones.
//
// Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
// Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef GODOT_EQ_H
#define GODOT_EQ_H

#include <cmath>

class GodotEQ10 {
public:
	static const int BAND_COUNT = 10;

	// Same centres as Godot's PRESET_10_BANDS
	explicit GodotEQ10(double p_mix_rate) {
		static const double freqs[BAND_COUNT] = { 31.25, 62.5, 125, 250, 500, 1e3, 2e3, 4e3, 8e3, 16e3 };
		for (int i = 0; i < BAND_COUNT; i++) {
			band_freq[i] = freqs[i];
			gain[i] = 1.0f;
		}
		recalculate_band_coefficients(p_mix_rate);
	}

	void set_band_gain_db(int p_band, float p_gain_db) {
		if (p_band >= 0 && p_band < BAND_COUNT) {
			// Math::db_to_linear
			gain[p_band] = (float)std::exp(p_gain_db * 0.11512925464970228420089957273422);
		}
	}

	// In-place over interleaved stereo float frames, like
	// AudioEffectEQInstance::process: every band filters the dry input and the
	// output is their gain-weighted sum.
	void process(float *p_frames, int p_frame_count) {
		for (int i = 0; i < p_frame_count; i++) {
			float src_l = p_frames[i * 2];
			float src_r = p_frames[i * 2 + 1];
			float dst_l = 0.0f;
			float dst_r = 0.0f;

			for (int j = 0; j < BAND_COUNT; j++) {
				float l = src_l;
				float r = src_r;

				proc[0][j].process_one(l);
				proc[1][j].process_one(r);

				dst_l += l * gain[j];
				dst_r += r * gain[j];
			}

			p_frames[i * 2] = dst_l;
			p_frames[i * 2 + 1] = dst_r;
		}
	}

private:
	struct BandProcess {
		float c1 = 0, c2 = 0, c3 = 0;
		float a1 = 0, a2 = 0, a3 = 0;
		float b1 = 0, b2 = 0, b3 = 0;

		inline void process_one(float &p_data) {
			a1 = p_data;
			b1 = c1 * (a1 - a3) + c3 * b2 - c2 * b3;
			p_data = b1;
			a3 = a2;
			a2 = a1;
			b3 = b2;
			b2 = b1;
		}
	};

	double band_freq[BAND_COUNT];
	float gain[BAND_COUNT];
	BandProcess proc[2][BAND_COUNT];

	static int solve_quadratic(double a, double b, double c, double *r1, double *r2) {
		double base = 2 * a;
		if (base == 0.0f) {
			return 0;
		}

		double squared = b * b - 4 * a * c;
		if (squared < 0.0) {
			return 0;
		}

		squared = std::sqrt(squared);

		*r1 = (-b + squared) / base;
		*r2 = (-b - squared) / base;

		return *r1 == *r2 ? 1 : 2;
	}

	// EQ::recalculate_band_coefficients
	static double band_log(double f) {
		return std::log(f) / std::log(2.);
	}

	void recalculate_band_coefficients(double mix_rate) {
		const double sqrt12 = 0.7071067811865475244008443621048490;
		const double tau = 6.2831853071795864769252867666;

		for (int i = 0; i < BAND_COUNT; i++) {
			double octave_size;
			double frq = band_freq[i];

			if (i == 0) {
				octave_size = band_log(band_freq[1]) - band_log(frq);
			} else if (i == BAND_COUNT - 1) {
				octave_size = band_log(frq) - band_log(band_freq[i - 1]);
			} else {
				double next = band_log(band_freq[i + 1]) - band_log(frq);
				double prev = band_log(frq) - band_log(band_freq[i - 1]);
				octave_size = (next + prev) / 2.0;
			}

			double frq_l = std::round(frq / std::pow(2.0, octave_size / 2.0));

			double side_gain2 = sqrt12 * sqrt12;
			double th = tau * frq / mix_rate;
			double th_l = tau * frq_l / mix_rate;

			double c2a = side_gain2 * std::cos(th) * std::cos(th) - 2.0 * side_gain2 * std::cos(th_l) * std::cos(th) + side_gain2 - std::sin(th_l) * std::sin(th_l);
			double c2b = 2.0 * side_gain2 * std::cos(th_l) * std::cos(th_l) + side_gain2 * std::cos(th) * std::cos(th) - 2.0 * side_gain2 * std::cos(th_l) * std::cos(th) - side_gain2 + std::sin(th_l) * std::sin(th_l);
			double c2c = 0.25 * side_gain2 * std::cos(th) * std::cos(th) - 0.5 * side_gain2 * std::cos(th_l) * std::cos(th) + 0.25 * side_gain2 - 0.25 * std::sin(th_l) * std::sin(th_l);

			double r1 = 0, r2 = 0;
			if (solve_quadratic(c2a, c2b, c2c, &r1, &r2) == 0) {
				continue;
			}

			// Godot keeps the coefficients as floats
			float c1 = (float)(2.0 * ((0.5 - r1) / 2.0));
			float c2 = (float)(2.0 * r1);
			float c3 = (float)(2.0 * (0.5 + r1) * std::cos(th));
			for (int ch = 0; ch < 2; ch++) {
				proc[ch][i].c1 = c1;
				proc[ch][i].c2 = c2;
				proc[ch][i].c3 = c3;
			}
		}
	}
};

#endif // GODOT_EQ_H
