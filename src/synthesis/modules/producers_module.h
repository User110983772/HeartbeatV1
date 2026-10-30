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

#include "synth_module.h"
#include "oscillator_module.h"

#include <array>
#include <memory>

namespace vital {
  // The sound engines (oscillators and samples). Each one belongs to a group, and within a group the engines
  // form a serial chain in engine order: an engine's FM/RM modulator is the engine directly above it.
  class ProducersModule : public SynthModule {
    public:
      enum {
        kReset,
        kRetrigger,
        kMidi,
        kActiveVoices,
        kNoteCount,
        // For the groups' voice settings, glide and humanise.
        kNote,
        kLastNote,
        kVoiceEvent,
        kNotesPressed,
        kNewest,
        kLegatoVoice,
        kLegatoNote,
        // The global glide's current note, which a group's own glide replaces.
        kGlobalGlide,
        kBeatsPerSecond,
        kNumInputs
      };

      enum {
        kGroupOutputStart,
        // Engines in the group routed straight to Direct Out.
        kGroupDirectStart = kGroupOutputStart + kNumOscillatorGroups,
        kModulatorStart = kGroupDirectStart + kNumOscillatorGroups,
        // Per engine values its group sets: see OscillatorModule's kMidiOffset onwards.
        kEngineMidiOffsetStart = kModulatorStart + kNumOscillators,
        kEnginePhaseOffsetStart = kEngineMidiOffsetStart + kNumOscillators,
        kEngineGroupUnisonStart = kEnginePhaseOffsetStart + kNumOscillators,
        kNumOutputs = kEngineGroupUnisonStart + kNumOscillators * OscillatorModule::kNumGroupUnisonInputs
      };

      ProducersModule();
      virtual ~ProducersModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new ProducersModule(*this); }

      Wavetable* getWavetable(int index) {
        return oscillators_[index]->getWavetable();
      }

      bool isGroupOn(int group) const;
      // On, with at least one engine on.
      bool isGroupUsed(int group) const;
      void updateOscillatorMemory();
      Sample* getSample(int index) { return oscillators_[index]->getSample(); }
      Output* samplePhaseOutput(int index) { return oscillators_[index]->getSamplePhaseOutput(); }

      // Call with processing locked. Invalid orders (not a permutation) are ignored.
      bool setEngineOrder(const EngineOrder& order);
      const EngineOrder& getEngineOrder() const { return *engine_order_; }
      // The engine directly above `index` in its group's chain, or -1 if it's at the top.
      int getEngineAbove(int index) const;

    protected:
      // A smooth random value per voice, for drift.
      struct DriftStream {
        mono_float position = 0.0f;
        poly_float points[4];
      };

      struct GroupControls {
        Output* drift = nullptr;
        Output* drift_rate = nullptr;
        Output* drift_spread = nullptr;
        Value* humanize_pitch = nullptr;
        Value* humanize_level = nullptr;
        Value* humanize_pan = nullptr;
        Value* humanize_phase = nullptr;
        const Output* unison[OscillatorModule::kNumGroupUnisonInputs] = {};
        Value* voice_mode = nullptr;
        Output* glide = nullptr;
        Output* glide_slope = nullptr;
        Processor* glide_note = nullptr;
      };

      int groupOf(int index) const;
      void advanceDrift(DriftStream& stream, mono_float rate, int num_samples);
      poly_float driftValue(const DriftStream& stream) const;
      void rollHumanize(poly_mask reset_mask);
      void setEngineInputs(int index, int group, const poly_float group_pitch[]);
      poly_float groupGain(int group);
      bool isLegato(int group) const;
      // The pitch a legato group plays: the newest held note, glided to from wherever the phrase was.
      poly_float legatoNote(int group, poly_mask reset_mask, int num_samples);
      void applyGroupGain(int group, poly_mask reset_mask, int num_samples);

      OscillatorModule* oscillators_[kNumOscillators];
      Value* oscillator_groups_[kNumOscillators];
      Value* oscillator_outputs_[kNumOscillators];
      Value* group_on_[kNumOscillatorGroups] = {};
      std::shared_ptr<EngineOrder> engine_order_;
      GroupControls group_controls_[kNumOscillatorGroups];

      // Per voice state; every voice processor has its own copy.
      utils::RandomGenerator random_;
      DriftStream group_drift_[kNumOscillatorGroups];
      DriftStream engine_drift_[kNumOscillators];
      poly_float humanize_pitch_[kNumOscillatorGroups];
      poly_float humanize_level_[kNumOscillatorGroups];
      poly_float humanize_pan_[kNumOscillatorGroups];
      poly_float humanize_phase_[kNumOscillators];
      poly_float group_gain_[kNumOscillatorGroups];
      poly_float legato_from_[kNumOscillatorGroups];
      poly_float legato_to_[kNumOscillatorGroups];
      poly_float legato_position_[kNumOscillatorGroups];

      JUCE_LEAK_DETECTOR(ProducersModule)
  };
} // namespace vital
