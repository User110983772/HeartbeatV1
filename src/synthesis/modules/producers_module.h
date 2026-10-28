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
        kNumInputs
      };

      enum {
        kGroupOutputStart,
        kModulatorStart = kGroupOutputStart + kNumOscillatorGroups,
        kNumOutputs = kModulatorStart + kNumOscillators
      };

      ProducersModule();
      virtual ~ProducersModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new ProducersModule(*this); }

      Wavetable* getWavetable(int index) {
        return oscillators_[index]->getWavetable();
      }

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
      int groupOf(int index) const;

      OscillatorModule* oscillators_[kNumOscillators];
      Value* oscillator_groups_[kNumOscillators];
      std::shared_ptr<EngineOrder> engine_order_;

      JUCE_LEAK_DETECTOR(ProducersModule)
  };
} // namespace vital
