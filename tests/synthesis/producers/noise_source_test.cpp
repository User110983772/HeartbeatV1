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

#include "noise_source_test.h"
#include "noise_source.h"

#include <cmath>
#include <vector>

namespace {
  constexpr int kBlockSize = 64;
  const double kWhiteRms = 1.0 / std::sqrt(3.0);

  std::vector<vital::poly_float> render(vital::NoiseSource& noise, int num_samples,
                                        float color, float focus, float frequency) {
    std::vector<vital::poly_float> result(num_samples);
    for (int i = 0; i + kBlockSize <= num_samples; i += kBlockSize)
      noise.process(result.data() + i, kBlockSize, color, focus, frequency);
    return result;
  }

  double rms(const std::vector<vital::poly_float>& samples, int lane) {
    double sum = 0.0;
    for (const vital::poly_float& sample : samples)
      sum += sample[lane] * sample[lane];
    return std::sqrt(sum / samples.size());
  }

  double decibels(double ratio) {
    return 20.0 * std::log10(ratio);
  }

  // Average Hann-windowed DFT power over [low, high] Hz, across consecutive frames.
  double bandPower(const std::vector<vital::poly_float>& samples, int sample_rate, double low, double high) {
    constexpr int kFrameSize = 4096;
    int first_bin = static_cast<int>(std::ceil(low * kFrameSize / sample_rate));
    int last_bin = static_cast<int>(std::floor(high * kFrameSize / sample_rate));
    double total = 0.0;
    int count = 0;
    for (size_t start = 0; start + kFrameSize <= samples.size(); start += kFrameSize) {
      for (int bin = first_bin; bin <= last_bin; ++bin) {
        double real = 0.0;
        double imaginary = 0.0;
        for (int i = 0; i < kFrameSize; ++i) {
          double window = 0.5 - 0.5 * std::cos(2.0 * vital::kPi * i / kFrameSize);
          double value = samples[start + i][0] * window;
          double angle = 2.0 * vital::kPi * bin * i / kFrameSize;
          real += value * std::cos(angle);
          imaginary -= value * std::sin(angle);
        }
        total += real * real + imaginary * imaginary;
        count++;
      }
    }
    return total / count;
  }

  double correlation(const std::vector<vital::poly_float>& a, int lane_a,
                     const std::vector<vital::poly_float>& b, int lane_b) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
      ab += a[i][lane_a] * b[i][lane_b];
      aa += a[i][lane_a] * a[i][lane_a];
      bb += b[i][lane_b] * b[i][lane_b];
    }
    return ab / std::sqrt(aa * bb);
  }
} // namespace

void NoiseSourceTest::runTest() {
  beginTest("Output stays finite and bounded");
  {
    const float colors[] = { 0.0f, 0.1f, 0.25f, 0.4f, 0.5f, 0.75f, 1.0f, -1.0f, 2.0f };
    const float focuses[] = { 0.0f, 0.1f, 0.25f, 0.5f, 0.9f, 1.0f, -1.0f, 2.0f };
    const float frequencies[] = { 0.0f, 8.0f, 55.0f, 440.0f, 5000.0f, 20000.0f, 90000.0f };
    const int sample_rates[] = { 44100, 96000, 192000 };

    bool all_finite = true;
    float peak = 0.0f;
    for (int sample_rate : sample_rates) {
      vital::NoiseSource noise;
      noise.setSampleRate(sample_rate);
      for (float color : colors) {
        for (float focus : focuses) {
          for (float frequency : frequencies) {
            for (const vital::poly_float& sample : render(noise, 2048, color, focus, frequency)) {
              for (int lane = 0; lane < vital::poly_float::kSize; ++lane) {
                all_finite = all_finite && std::isfinite(sample[lane]);
                peak = std::max(peak, std::abs(sample[lane]));
              }
            }
          }
        }
      }
    }
    expect(all_finite, "noise output contains a non-finite sample");
    expectLessThan(peak, 8.0f, "noise output peak");
  }

  beginTest("Every color matches white noise loudness");
  for (int sample_rate : { 44100, 48000, 96000 }) {
    for (float color : { 0.0f, 0.25f, 0.5f, 1.0f }) {
      vital::NoiseSource noise;
      noise.setSampleRate(sample_rate);
      double level = decibels(rms(render(noise, sample_rate * 20, color, 0.0f, 440.0f), 0) / kWhiteRms);
      expectWithinAbsoluteError(level, 0.0, 1.0,
                                "color " + String(color) + " at " + String(sample_rate) + " Hz, dB from white");
    }
  }

  beginTest("Color sets the spectral tilt");
  {
    constexpr int kSampleRate = 48000;
    // Power at 4 kHz relative to 250 Hz, four octaves apart.
    const float colors[] = { 0.0f, 0.25f, 0.5f, 1.0f };
    const double expected_tilt[] = { -24.0, -12.0, 0.0, 23.7 };
    for (int i = 0; i < 4; ++i) {
      vital::NoiseSource noise;
      noise.setSampleRate(kSampleRate);
      std::vector<vital::poly_float> samples = render(noise, 4096 * 24, colors[i], 0.0f, 440.0f);
      double tilt = 10.0 * std::log10(bandPower(samples, kSampleRate, 3800.0, 4200.0) /
                                      bandPower(samples, kSampleRate, 238.0, 262.0));
      expectWithinAbsoluteError(tilt, expected_tilt[i], 2.0, "color " + String(colors[i]) + " tilt in dB");
    }
  }

  beginTest("Full focus concentrates energy at the note and keeps loudness");
  {
    constexpr int kSampleRate = 48000;
    constexpr float kFrequency = 1000.0f;
    vital::NoiseSource noise;
    noise.setSampleRate(kSampleRate);
    std::vector<vital::poly_float> samples = render(noise, 4096 * 24, 0.5f, 1.0f, kFrequency);

    double in_band = bandPower(samples, kSampleRate, 950.0, 1050.0) * (1050.0 - 950.0);
    double total = bandPower(samples, kSampleRate, 20.0, 20000.0) * (20000.0 - 20.0);
    expectGreaterThan(in_band / total, 0.85, "fraction of power within 50 Hz of the note");

    double level = decibels(rms(samples, 0) / kWhiteRms);
    expectWithinAbsoluteError(level, 0.0, 2.0, "focused noise dB from white");
  }

  beginTest("Focused noise keeps white loudness for every color");
  for (float focus : { 0.25f, 0.5f, 1.0f }) {
    for (float frequency : { 110.0f, 1000.0f, 6000.0f }) {
      for (float color : { 0.0f, 0.25f, 0.5f, 1.0f }) {
        vital::NoiseSource noise;
        noise.setSampleRate(48000);
        double level = decibels(rms(render(noise, 48000 * 20, color, focus, frequency), 0) / kWhiteRms);
        expectWithinAbsoluteError(level, 0.0, 2.0, "color " + String(color) + " focus " + String(focus) +
                                  " at " + String(frequency) + " Hz, dB from white");
      }
    }
  }

  beginTest("Lanes and clones produce independent noise");
  {
    vital::NoiseSource noise;
    vital::NoiseSource clone(noise);
    std::vector<vital::poly_float> original = render(noise, 48000, 0.5f, 0.0f, 440.0f);
    std::vector<vital::poly_float> cloned = render(clone, 48000, 0.5f, 0.0f, 440.0f);
    for (int lane = 1; lane < vital::poly_float::kSize; ++lane)
      expectLessThan(std::abs(correlation(original, 0, original, lane)), 0.03, "lane correlation");
    expectLessThan(std::abs(correlation(original, 0, cloned, 0)), 0.03, "clone correlation");
  }

  beginTest("Reset clears filter state and snaps parameters");
  {
    vital::NoiseSource fresh;
    fresh.seed(5);
    std::vector<vital::poly_float> expected = render(fresh, 1024, 0.25f, 0.6f, 300.0f);

    vital::NoiseSource used;
    render(used, 4096, 0.0f, 1.0f, 80.0f);
    used.reset(vital::poly_float::equal(0.0f, 0.0f));
    used.seed(5);
    std::vector<vital::poly_float> after_reset = render(used, 1024, 0.25f, 0.6f, 300.0f);

    bool identical = true;
    for (size_t i = 0; i < expected.size(); ++i) {
      for (int lane = 0; lane < vital::poly_float::kSize; ++lane)
        identical = identical && expected[i][lane] == after_reset[i][lane];
    }
    expect(identical, "output after reset differs from a fresh source");
  }

  beginTest("Magnitude response matches the rendered spectrum");
  {
    constexpr int kFrameSize = 4096;
    constexpr int kSampleRate = 48000;
    vital::NoiseSource white;
    white.setSampleRate(kSampleRate);
    std::vector<vital::poly_float> white_samples = render(white, kSampleRate * 8, 0.5f, 0.0f, 1000.0f);

    for (float color : { 0.0f, 0.25f, 0.75f, 1.0f }) {
      for (float focus : { 0.0f, 0.3f }) {
        vital::NoiseSource noise;
        noise.setSampleRate(kSampleRate);
        std::vector<vital::poly_float> samples = render(noise, kSampleRate * 8, color, focus, 1000.0f);

        for (double center : { 250.0, 1000.0, 4000.0 }) {
          double low = center * 0.95;
          double high = center * 1.05;
          double predicted = 0.0;
          int bins = 0;
          for (int bin = static_cast<int>(std::ceil(low * kFrameSize / kSampleRate));
               bin <= static_cast<int>(std::floor(high * kFrameSize / kSampleRate)); ++bin, ++bins) {
            double magnitude = noise.magnitudeResponse(color, focus, 1000.0f, bin * (float)kSampleRate / kFrameSize);
            predicted += magnitude * magnitude;
          }
          double measured = bandPower(samples, kSampleRate, low, high) / bandPower(white_samples, kSampleRate, low, high);
          expectWithinAbsoluteError(10.0 * std::log10(measured), 10.0 * std::log10(predicted / bins), 1.5,
                                    "color " + String(color) + " focus " + String(focus) + " at " + String(center) + " Hz");
        }
      }
    }
  }
}

static NoiseSourceTest noise_source_test;
