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

#include "oscillator_group_mixer.h"

#include "futils.h"
#include "utils.h"

namespace vital {

  OscillatorGroupMixer::OscillatorGroupMixer() : Processor(kNumInputs, kNumOutputs), last_amplitude_(0.0f) {
    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      last_sources_[g] = 0.0f;
      last_levels_[g] = 0.0f;
    }
  }

  force_inline poly_float OscillatorGroupMixer::readControl(int index, int sample) const {
    const Output* source = input(index)->source;
    return source->isControlRate() ? source->buffer[0] : source->buffer[sample];
  }

  void OscillatorGroupMixer::process(int num_samples) {
    poly_float* fx_out = output(kFxBusOut)->buffer;
    poly_float* direct_out = output(kDirectOut)->buffer;
    utils::zeroBuffer(fx_out, num_samples);
    utils::zeroBuffer(direct_out, num_samples);

    poly_mask reset_mask = getResetMask(kReset);
    mono_float sample_inc = 1.0f / num_samples;

    poly_float amplitude = input(kAmplitude)->at(0);
    poly_float start_amplitude = utils::maskLoad(last_amplitude_, amplitude, reset_mask);
    poly_float delta_amplitude = (amplitude - start_amplitude) * sample_inc;
    last_amplitude_ = amplitude;

    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      int source_index = utils::iclamp(static_cast<int>(input(kAmpSourceIndices + g)->at(0)[0]),
                                       0, kNumGroupAmpSources - 1);
      int output_index = static_cast<int>(input(kOutputIndices + g)->at(0)[0]);
      poly_float* dest = output_index == constants::kGroupDirectOut ? direct_out : fx_out;

      const Output* source = input(kAmpSources + source_index)->source;
      const Output* level = input(kLevels + g)->source;
      const poly_float* audio = input(kGroupAudio + g)->source->buffer;

      // Control-rate sources and levels are ramped across the block, like SmoothMultiply does.
      poly_float source_end = utils::clamp(source->buffer[0], 0.0f, 1.0f);
      poly_float source_value = utils::maskLoad(last_sources_[g], source_end, reset_mask);
      poly_float source_delta = (source_end - source_value) * sample_inc;
      poly_float level_end = level->buffer[0];
      poly_float level_value = utils::maskLoad(last_levels_[g], level_end, reset_mask);
      poly_float level_delta = (level_end - level_value) * sample_inc;

      bool audio_rate_source = !source->isControlRate();
      bool audio_rate_level = !level->isControlRate();
      poly_float current_amplitude = start_amplitude;

      for (int i = 0; i < num_samples; ++i) {
        source_value += source_delta;
        level_value += level_delta;
        current_amplitude += delta_amplitude;

        poly_float s = audio_rate_source ? utils::clamp(source->buffer[i], 0.0f, 1.0f) : source_value;
        poly_float l = audio_rate_level ? level->buffer[i] : level_value;
        poly_float gain = s * current_amplitude;
        dest[i] += audio[i] * gain * gain * l;
      }

      last_sources_[g] = audio_rate_source ? utils::clamp(source->buffer[num_samples - 1], 0.0f, 1.0f) : source_end;
      last_levels_[g] = audio_rate_level ? level->buffer[num_samples - 1] : level_end;
    }
  }
} // namespace vital
