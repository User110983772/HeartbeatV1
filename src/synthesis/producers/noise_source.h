/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common.h"
#include "futils.h"
#include "poly_utils.h"

#include <atomic>
#include <cmath>
#include <complex>

namespace vital {

  // Colored, optionally pitch-focused noise for SynthOscillator's noise source.
  // Every lane (voice x stereo channel) runs an independent generator and filter state.
  //
  // color: 0 brown, 0.25 pink, 0.5 white, 1 violet. Each color is normalized to white's RMS.
  // focus: 0 plain colored noise, 1 a very narrow band at `frequency`. The band is a 4-pole bandpass
  //        (two identical state variable stages) whose loudness is compensated for the color's
  //        response at `frequency`. Its 12 dB/octave skirts outrun every color's 6 dB/octave tilt, so
  //        the band's energy stays near `frequency` for every color.
  class NoiseSource {
    public:
      static constexpr mono_float kReferenceSampleRate = 44100.0f;
      static constexpr mono_float kBrownCutoff = 20.0f;
      static constexpr mono_float kMinQ = 2.0f;
      static constexpr mono_float kMaxQ = 200.0f;
      // Caps Q on low notes so the band never rings for longer than ~1 / (pi * kMinBandwidth) seconds.
      static constexpr mono_float kMinBandwidth = 2.0f;
      static constexpr mono_float kFocusMixRange = 0.25f;
      static constexpr mono_float kMinFrequency = 8.0f;
      static constexpr mono_float kMaxFrequencyRatio = 0.45f;
      static constexpr mono_float kMaxFocusGain = 1e6f;
      // Below this the color has almost no energy at the band (violet under ~10 Hz); the band is
      // left quieter rather than amplifying rounding noise.
      static constexpr mono_float kMinColorMagnitude = 1e-3f;
      // Derived filter values are recomputed from the ramped controls this often, so their nonlinear
      // relationship holds during fast modulation.
      static constexpr int kControlInterval = 16;

      NoiseSource() : sample_rate_(0), oversample_(1) {
        seed(nextSeed()++);
        setSampleRate(static_cast<int>(kReferenceSampleRate));
        reset(poly_float::equal(0.0f, 0.0f));
      }

      // Clones (one per voice block) must not repeat each other's sequence.
      NoiseSource(const NoiseSource& other) : NoiseSource() {
        setSampleRate(other.sample_rate_, other.oversample_);
      }

      NoiseSource& operator=(const NoiseSource& other) = delete;

      void seed(uint32_t seed) {
        for (int i = 0; i < poly_int::kSize; ++i) {
          uint32_t lane_seed = hash(seed * poly_int::kSize + i);
          state_.set(i, lane_seed ? lane_seed : 1);
        }
      }

      // oversample: how far sample_rate is above the output rate. Only the band below the output's
      // Nyquist frequency is heard, so each color is normalized to unit power within it.
      void setSampleRate(int sample_rate, int oversample = 1) {
        oversample = std::max(oversample, 1);
        if ((sample_rate == sample_rate_ && oversample == oversample_) || sample_rate <= 0)
          return;

        sample_rate_ = sample_rate;
        oversample_ = oversample;
        double rate_ratio = kReferenceSampleRate / sample_rate;

        // Paul Kellet's economy pink filter, with poles moved to keep their corner frequencies.
        static constexpr double kPinkPoles[kNumPinkPoles] = { 0.99765, 0.96300, 0.57000 };
        static constexpr double kPinkGains[kNumPinkPoles] = { 0.0990460, 0.2965164, 1.0526913 };
        static constexpr double kPinkDirect = 0.1848;
        double poles[kNumPinkPoles];
        double gains[kNumPinkPoles];
        for (int i = 0; i < kNumPinkPoles; ++i) {
          poles[i] = std::pow(kPinkPoles[i], rate_ratio);
          gains[i] = kPinkGains[i] * (1.0 - poles[i]) / (1.0 - kPinkPoles[i]);
        }

        // Exact output variance for unit-variance white input.
        double pink_variance = kPinkDirect * kPinkDirect;
        for (int i = 0; i < kNumPinkPoles; ++i) {
          pink_variance += 2.0 * kPinkDirect * gains[i];
          for (int j = 0; j < kNumPinkPoles; ++j)
            pink_variance += gains[i] * gains[j] / (1.0 - poles[i] * poles[j]);
        }
        double pink_normalization = 1.0 / std::sqrt(pink_variance);
        for (int i = 0; i < kNumPinkPoles; ++i) {
          pink_poles_[i] = static_cast<mono_float>(poles[i]);
          pink_gains_[i] = static_cast<mono_float>(gains[i] * pink_normalization);
        }
        pink_direct_ = static_cast<mono_float>(kPinkDirect * pink_normalization);

        double brown_pole = std::exp(-2.0 * kPi * kBrownCutoff / sample_rate);
        brown_pole_ = static_cast<mono_float>(brown_pole);
        double brown_gain = (1.0 - brown_pole) * std::sqrt((1.0 + brown_pole) / (1.0 - brown_pole));

        // Every color now has unit total power. Remove what lies above the output's Nyquist frequency.
        typedef std::complex<double> complex;
        auto pink_power = [&](complex delay) {
          complex response = kPinkDirect * pink_normalization;
          for (int i = 0; i < kNumPinkPoles; ++i)
            response += gains[i] * pink_normalization / (1.0 - poles[i] * delay);
          return std::norm(response);
        };
        auto brown_power = [&](complex delay) { return std::norm(brown_gain / (1.0 - brown_pole * delay)); };
        auto violet_power = [&](complex delay) { return std::norm((1.0 - delay) * std::sqrt(0.5)); };

        double white_scale = std::sqrt(static_cast<double>(oversample));
        double pink_scale = 1.0 / std::sqrt(1.0 - powerAboveOutputNyquist(pink_power, oversample));
        double brown_scale = 1.0 / std::sqrt(1.0 - powerAboveOutputNyquist(brown_power, oversample));
        double violet_scale = 1.0 / std::sqrt(1.0 - powerAboveOutputNyquist(violet_power, oversample));

        for (int i = 0; i < kNumPinkPoles; ++i)
          pink_gains_[i] = static_cast<mono_float>(pink_gains_[i] * pink_scale);
        pink_direct_ = static_cast<mono_float>(pink_direct_ * pink_scale);
        brown_gain_ = static_cast<mono_float>(brown_gain * brown_scale);
        white_gain_ = static_cast<mono_float>(white_scale);
        violet_gain_ = static_cast<mono_float>(std::sqrt(0.5) * violet_scale);
      }

      void reset(poly_mask reset_mask) {
        for (int i = 0; i < kNumPinkPoles; ++i)
          pink_state_[i] = utils::maskLoad(pink_state_[i], 0.0f, reset_mask);
        brown_state_ = utils::maskLoad(brown_state_, 0.0f, reset_mask);
        last_white_ = utils::maskLoad(last_white_, 0.0f, reset_mask);
        for (int i = 0; i < kNumBandStages; ++i) {
          ic1_[i] = utils::maskLoad(ic1_[i], 0.0f, reset_mask);
          ic2_[i] = utils::maskLoad(ic2_[i], 0.0f, reset_mask);
        }
        snap_mask_ = snap_mask_ | reset_mask;
      }

      void process(poly_float* dest, int num_samples, poly_float color, poly_float focus, poly_float frequency) {
        render<false>(dest, num_samples, color, focus, frequency, 1.0f, 1.0f);
      }

      // Adds the output to dest, scaled by a gain that ramps linearly from gain_start to gain_end.
      void addTo(poly_float* dest, int num_samples, poly_float color, poly_float focus, poly_float frequency,
                 poly_float gain_start, poly_float gain_end) {
        render<true>(dest, num_samples, color, focus, frequency, gain_start, gain_end);
      }

    private:
      template<bool accumulate>
      void render(poly_float* dest, int num_samples, poly_float color, poly_float focus, poly_float frequency,
                  poly_float gain_start, poly_float gain_end) {
        mono_float max_frequency = kMaxFrequencyRatio * sample_rate_;
        poly_float target_color = utils::clamp(color, 0.0f, 1.0f);
        poly_float target_focus = utils::clamp(focus, 0.0f, 1.0f);
        poly_float target_frequency = utils::clamp(frequency, kMinFrequency, max_frequency);

        if (snap_mask_.anyMask()) {
          color_ = utils::maskLoad(color_, target_color, snap_mask_);
          focus_ = utils::maskLoad(focus_, target_focus, snap_mask_);
          frequency_ = utils::maskLoad(frequency_, target_frequency, snap_mask_);
          current_.loadFrom(computeParameters(color_, focus_, frequency_), snap_mask_);
          snap_mask_ = poly_mask(0);
        }

        mono_float block_inc = 1.0f / num_samples;
        poly_float gain = gain_start;
        poly_float gain_inc = (gain_end - gain_start) * block_inc;

        // Steady controls: current_ already holds their parameters, so skip recomputing them.
        poly_mask changed = poly_float::notEqual(target_color, color_) | poly_float::notEqual(target_focus, focus_) |
                            poly_float::notEqual(target_frequency, frequency_);
        if (!changed.anyMask()) {
          for (int i = 0; i < num_samples; ++i) {
            if (accumulate) {
              gain += gain_inc;
              dest[i] += tick(current_) * gain;
            }
            else
              dest[i] = tick(current_);
          }
          return;
        }

        // Color and focus ramp linearly, frequency geometrically, across the whole block.
        poly_float start_color = color_;
        poly_float start_focus = focus_;
        poly_float start_frequency = frequency_;
        poly_float frequency_ratio = target_frequency / start_frequency;

        for (int start = 0; start < num_samples; start += kControlInterval) {
          int samples = std::min<int>(+kControlInterval, num_samples - start);
          mono_float t = (start + samples) * block_inc;
          color_ = utils::interpolate(start_color, target_color, t);
          focus_ = utils::interpolate(start_focus, target_focus, t);
          frequency_ = start_frequency * utils::pow(frequency_ratio, t);
          renderInterval<accumulate>(dest + start, samples, computeParameters(color_, focus_, frequency_),
                                     gain, gain_inc);
          gain += gain_inc * samples;
        }

        color_ = target_color;
        focus_ = target_focus;
        frequency_ = target_frequency;
      }

    public:

      // Steady-state |H| at evaluate_frequency for the given controls, relative to white noise
      // (0 dB = white's level). Uses the same coefficients as the audio path, for displays.
      mono_float magnitudeResponse(mono_float color, mono_float focus, mono_float frequency,
                                   mono_float evaluate_frequency) const {
        mono_float power = 0.0f;
        addPowerResponse(color, focus, frequency, 1.0f, &evaluate_frequency, &power, 1);
        return std::sqrt(power);
      }

      // Adds gain^2 * |H|^2 (relative to white noise) at each of evaluate_frequencies to power.
      // Independent noise voices add in power, so unison responses sum this way.
      void addPowerResponse(mono_float color, mono_float focus, mono_float frequency, mono_float gain,
                            const mono_float* evaluate_frequencies, mono_float* power, int num_points) const {
        typedef std::complex<mono_float> complex;
        mono_float max_frequency = kMaxFrequencyRatio * sample_rate_;
        Parameters p = computeParameters(utils::clamp(color, 0.0f, 1.0f), utils::clamp(focus, 0.0f, 1.0f),
                                         utils::clamp(frequency, kMinFrequency, max_frequency));
        mono_float mix = p.focus_mix[0];
        mono_float scale = gain / white_gain_;

        for (int i = 0; i < num_points; ++i) {
          mono_float omega = 2.0f * kPi * evaluate_frequencies[i] / sample_rate_;
          complex delay = std::polar(1.0f, -omega);
          complex colored = colorResponse(p.brown[0], p.pink[0], p.white[0], p.violet[0], delay);

          // The TPT state variable filter is the bilinear transform of k s / (s^2 + k s + 1).
          complex s = (1.0f - delay) / ((1.0f + delay) * p.g[0]);
          complex band = p.k[0] * s / (s * s + p.k[0] * s + 1.0f);
          complex focused = band * band * p.focus_gain[0];
          power[i] += std::norm(colored * ((1.0f - mix) + mix * focused) * scale);
        }
      }

      int getSampleRate() const { return sample_rate_; }

    private:
      static constexpr int kNumPinkPoles = 3;
      static constexpr int kNumIntegrationSteps = 1024;

      // Fraction of a unit-variance white input's power that power_at(e^-jw) puts in [pi / oversample, pi].
      // That range is far from the brown and pink poles, so Simpson's rule is accurate there.
      template<typename PowerFunction>
      static double powerAboveOutputNyquist(PowerFunction power_at, int oversample) {
        if (oversample <= 1)
          return 0.0;

        double start = kPi / oversample;
        double step = (kPi - start) / kNumIntegrationSteps;
        double sum = 0.0;
        for (int i = 0; i <= kNumIntegrationSteps; ++i) {
          double weight = (i == 0 || i == kNumIntegrationSteps) ? 1.0 : (i % 2 ? 4.0 : 2.0);
          sum += weight * power_at(std::polar(1.0, -(start + i * step)));
        }
        return std::min(sum * step / (3.0 * kPi), 0.999);
      }
      static constexpr int kNumBandStages = 2;
      static constexpr mono_float kIntToUnit = 1.0f / 2147483648.0f;
      static constexpr mono_float kMinFocusGain = 1e-6f;
      // Uniform [-1, 1) has RMS 1 / sqrt(3); filters work on unit-RMS noise, the output returns to it.
      static constexpr mono_float kWhiteNormalization = 1.7320508f;
      static constexpr mono_float kOutputScale = 1.0f / 1.7320508f;
      static constexpr mono_float kSqrtHalf = 0.70710678f;

      struct Parameters {
        poly_float brown, pink, white, violet, focus_mix;
        poly_float g, k, focus_gain;

        void loadFrom(const Parameters& other, poly_mask mask) {
          brown = utils::maskLoad(brown, other.brown, mask);
          pink = utils::maskLoad(pink, other.pink, mask);
          white = utils::maskLoad(white, other.white, mask);
          violet = utils::maskLoad(violet, other.violet, mask);
          focus_mix = utils::maskLoad(focus_mix, other.focus_mix, mask);
          g = utils::maskLoad(g, other.g, mask);
          k = utils::maskLoad(k, other.k, mask);
          focus_gain = utils::maskLoad(focus_gain, other.focus_gain, mask);
        }
      };

      static std::atomic<uint32_t>& nextSeed() {
        static std::atomic<uint32_t> next_seed(1);
        return next_seed;
      }

      static uint32_t hash(uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352d;
        x ^= x >> 15; x *= 0x846ca68b;
        x ^= x >> 16;
        return x;
      }

      Parameters computeParameters(poly_float color, poly_float focus, poly_float frequency) const {
        Parameters result;
        poly_mask warm_mask = poly_float::lessThan(color, 0.5f);
        result.brown = utils::clamp(poly_float(1.0f) - color * 4.0f, 0.0f, 1.0f);
        result.pink = utils::clamp(poly_float(1.0f) - poly_float::abs(color - 0.25f) * 4.0f, 0.0f, 1.0f);
        poly_float warm_white = utils::clamp((color - 0.25f) * 4.0f, 0.0f, 1.0f);
        poly_float bright_white = utils::clamp(poly_float(1.0f) - (color - 0.5f) * 2.0f, 0.0f, 1.0f);
        result.white = utils::maskLoad(bright_white, warm_white, warm_mask);
        result.violet = utils::clamp((color - 0.5f) * 2.0f, 0.0f, 1.0f);
        result.focus_mix = utils::clamp(focus * (1.0f / kFocusMixRange), 0.0f, 1.0f);

        mono_float sample_rate = sample_rate_;
        poly_float q = futils::pow(poly_float(kMaxQ / kMinQ), focus) * kMinQ;
        q = utils::clamp(q, poly_float(kMinQ), utils::max(frequency * (1.0f / kMinBandwidth), kMinQ));
        result.g = utils::tan(frequency * (kPi / sample_rate));
        result.k = poly_float(1.0f) / q;

        // Two unity-peak bandpass stages pass about pi * f / (2 * Q * fs) of white noise's power;
        // dividing by the color's magnitude at f makes that hold for colored input too.
        poly_float focus_gain = utils::sqrt(q * (2.0f * sample_rate / kPi) / frequency);
        for (int i = 0; i < poly_float::kSize; ++i) {
          mono_float magnitude = colorMagnitude(result.brown[i], result.pink[i], result.white[i],
                                                result.violet[i], 2.0f * kPi * frequency[i] / sample_rate);
          focus_gain.set(i, focus_gain[i] / std::max(magnitude, +kMinColorMagnitude));
        }
        result.focus_gain = utils::clamp(focus_gain, kMinFocusGain, kMaxFocusGain);
        return result;
      }

      // H(z) of the weighted color mix at z^-1 = delay, matching tick().
      std::complex<mono_float> colorResponse(mono_float brown, mono_float pink, mono_float white,
                                             mono_float violet, std::complex<mono_float> delay) const {
        typedef std::complex<mono_float> complex;
        complex pink_response = pink_direct_;
        for (int i = 0; i < kNumPinkPoles; ++i)
          pink_response += pink_gains_[i] / (1.0f - pink_poles_[i] * delay);

        complex brown_response = brown_gain_ / (1.0f - brown_pole_ * delay);
        complex violet_response = (1.0f - delay) * violet_gain_;
        return brown * brown_response + pink * pink_response + white * white_gain_ + violet * violet_response;
      }

      mono_float colorMagnitude(mono_float brown, mono_float pink, mono_float white, mono_float violet,
                                mono_float omega) const {
        return std::abs(colorResponse(brown, pink, white, violet, std::polar(1.0f, -omega)));
      }

      // Mix weights ramp linearly; band frequency, resonance and gain ramp geometrically.
      template<bool accumulate>
      void renderInterval(poly_float* dest, int num_samples, const Parameters& target,
                          poly_float gain, poly_float gain_inc) {
        mono_float sample_inc = 1.0f / num_samples;
        poly_float delta_brown = (target.brown - current_.brown) * sample_inc;
        poly_float delta_pink = (target.pink - current_.pink) * sample_inc;
        poly_float delta_white = (target.white - current_.white) * sample_inc;
        poly_float delta_violet = (target.violet - current_.violet) * sample_inc;
        poly_float delta_focus_mix = (target.focus_mix - current_.focus_mix) * sample_inc;
        poly_float ratio_g = utils::pow(target.g / current_.g, sample_inc);
        poly_float ratio_k = utils::pow(target.k / current_.k, sample_inc);
        poly_float ratio_focus_gain = utils::pow(target.focus_gain / current_.focus_gain, sample_inc);

        for (int i = 0; i < num_samples; ++i) {
          current_.brown += delta_brown;
          current_.pink += delta_pink;
          current_.white += delta_white;
          current_.violet += delta_violet;
          current_.focus_mix += delta_focus_mix;
          current_.g *= ratio_g;
          current_.k *= ratio_k;
          current_.focus_gain *= ratio_focus_gain;
          if (accumulate) {
            gain += gain_inc;
            dest[i] += tick(current_) * gain;
          }
          else
            dest[i] = tick(current_);
        }
        current_ = target;
      }

      force_inline poly_float nextWhite() {
        // 32-bit LCG; only the high bits reach the output, through the signed conversion.
        state_ = state_ * poly_int(1664525) + poly_int(1013904223);
        return utils::toFloat(state_) * kIntToUnit;
      }

      force_inline poly_float tick(const Parameters& p) {
        poly_float white = nextWhite() * kWhiteNormalization;

        poly_float pink = white * pink_direct_;
        for (int i = 0; i < kNumPinkPoles; ++i) {
          pink_state_[i] = pink_state_[i] * pink_poles_[i] + white * pink_gains_[i];
          pink += pink_state_[i];
        }

        brown_state_ = brown_state_ * brown_pole_ + white * brown_gain_;
        poly_float violet = (white - last_white_) * violet_gain_;
        last_white_ = white;

        poly_float colored = brown_state_ * p.brown + pink * p.pink + white * (p.white * white_gain_) +
                             violet * p.violet;

        // Topology-preserving state variable filters (Simper). Scaling each stage's input by k makes
        // its v1 a unity-peak bandpass. All gain goes in before the filters, so on parameter jumps the
        // stored energy decays at its old level instead of being rescaled.
        poly_float a1 = poly_float(1.0f) / (p.g * (p.g + p.k) + 1.0f);
        poly_float a2 = p.g * a1;
        poly_float a3 = p.g * a2;
        poly_float band = colored * p.focus_gain;
        for (int i = 0; i < kNumBandStages; ++i) {
          poly_float v3 = band * p.k - ic2_[i];
          poly_float v1 = a1 * ic1_[i] + a2 * v3;
          poly_float v2 = ic2_[i] + a2 * ic1_[i] + a3 * v3;
          ic1_[i] = v1 * 2.0f - ic1_[i];
          ic2_[i] = v2 * 2.0f - ic2_[i];
          band = v1;
        }

        return utils::interpolate(colored, band, p.focus_mix) * kOutputScale;
      }

      int sample_rate_;
      int oversample_;
      poly_int state_;
      poly_mask snap_mask_;
      poly_float color_;
      poly_float focus_;
      poly_float frequency_;
      Parameters current_;

      mono_float pink_poles_[kNumPinkPoles];
      mono_float pink_gains_[kNumPinkPoles];
      mono_float pink_direct_;
      mono_float brown_pole_;
      mono_float brown_gain_;
      mono_float white_gain_;
      mono_float violet_gain_;

      poly_float pink_state_[kNumPinkPoles];
      poly_float brown_state_;
      poly_float last_white_;
      poly_float ic1_[kNumBandStages];
      poly_float ic2_[kNumBandStages];
  };
} // namespace vital
