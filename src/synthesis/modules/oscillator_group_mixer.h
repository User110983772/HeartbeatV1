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

#include "processor.h"
#include "synth_constants.h"

namespace vital {

  // Applies each oscillator group's amplitude source and level, then sends the group to its output.
  // The amp source is shaped like Vital's amplitude envelope: (source * amplitude)^2, where amplitude is
  // velocity tracking times voice amplitude, so a group on Env 1 sounds exactly like the old voice output.
  class OscillatorGroupMixer : public Processor {
    public:
      enum {
        kReset,
        kAmplitude,
        kGroupAudio,
        kAmpSources = kGroupAudio + kNumOscillatorGroups,
        kLevels = kAmpSources + kNumGroupAmpSources,
        kAmpSourceIndices = kLevels + kNumOscillatorGroups,
        kOutputIndices = kAmpSourceIndices + kNumOscillatorGroups,
        kNumInputs = kOutputIndices + kNumOscillatorGroups
      };

      enum {
        kFxBusOut,
        kDirectOut,
        kNumOutputs
      };

      OscillatorGroupMixer();
      virtual ~OscillatorGroupMixer() { }

      void process(int num_samples) override;
      virtual Processor* clone() const override { return new OscillatorGroupMixer(*this); }

    private:
      poly_float readControl(int index, int sample) const;

      poly_float last_amplitude_;
      poly_float last_sources_[kNumOscillatorGroups];
      poly_float last_levels_[kNumOscillatorGroups];

      JUCE_LEAK_DETECTOR(OscillatorGroupMixer)
  };
} // namespace vital
