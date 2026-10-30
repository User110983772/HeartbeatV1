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
  class EnvelopeModule;
  class LfoModule;
  class ProducersModule;

  // Applies each oscillator group's amplitude source and level, then sends the group to its output.
  // The amp source is shaped like Vital's amplitude envelope: (source * amplitude)^2, where amplitude is
  // velocity tracking times voice amplitude, so a group on Env 1 sounds exactly like the old voice output.
  //
  // It also decides when a voice can end: kVoiceAlive is the loudest gain of any group with an engine on.
  // Envelopes release on their own; LFOs and randoms don't, so after the key is released a group on a
  // looping LFO or random fades out quickly, and a group on a one-shot LFO fades once the LFO finishes.
  // A stolen (killed) voice fades every non-envelope group.
  class OscillatorGroupMixer : public Processor {
    public:
      static constexpr mono_float kFadeSeconds = kVoiceKillTime;

      enum {
        kReset,
        kTrigger,
        kActiveVoices,
        kAmplitude,
        kGroupAudio,
        kGroupDirectAudio = kGroupAudio + kNumOscillatorGroups,
        kAmpSources = kGroupDirectAudio + kNumOscillatorGroups,
        kLfoProgress = kAmpSources + kNumGroupAmpSources,
        kLevels = kLfoProgress + kNumLfos,
        kAmpSourceIndices = kLevels + kNumOscillatorGroups,
        kOutputIndices = kAmpSourceIndices + kNumOscillatorGroups,
        kSmoothings = kOutputIndices + kNumOscillatorGroups,
        kNumInputs = kSmoothings + kNumOscillatorGroups
      };

      enum {
        kFxBusOut,
        kDirectOut,
        kVoiceAlive,
        kNumOutputs
      };

      OscillatorGroupMixer();
      virtual ~OscillatorGroupMixer() { }

      // Global modules, read for which groups are used and which LFOs are one-shot.
      void setSources(const ProducersModule* producers, const EnvelopeModule* const* envelopes,
                      const LfoModule* const* lfos);

      void process(int num_samples) override;
      virtual Processor* clone() const override { return new OscillatorGroupMixer(*this); }

    private:
      bool isControlRateSource(int source_index, const Output* source) const;
      // Voices whose one-shot LFO source hasn't reached the end of its shape.
      poly_mask unfinishedMask(int source_index) const;
      poly_mask fadeMask(int source_index, poly_mask released_mask, poly_mask kill_mask) const;

      const ProducersModule* producers_;
      const EnvelopeModule* envelopes_[kNumEnvelopes];
      const LfoModule* lfos_[kNumLfos];

      poly_float voice_state_;
      poly_float last_amplitude_;
      poly_float last_sources_[kNumOscillatorGroups];
      poly_float last_levels_[kNumOscillatorGroups];
      poly_float smoothed_[kNumOscillatorGroups];
      poly_float fades_[kNumOscillatorGroups];

      JUCE_LEAK_DETECTOR(OscillatorGroupMixer)
  };
} // namespace vital
