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

#include "producers_module.h"

namespace vital {
  ProducersModule::ProducersModule() : SynthModule(kNumInputs, kNumOutputs),
                                       engine_order_(std::make_shared<EngineOrder>()) {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillators_[i] = new OscillatorModule("osc_" + number);
      addSubmodule(oscillators_[i]);
      addProcessor(oscillators_[i]);
      oscillators_[i]->enable(false);
      oscillator_groups_[i] = nullptr;
      (*engine_order_)[i] = i;
    }
  }

  void ProducersModule::init() {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillator_groups_[i] = createBaseControl("osc_" + number + "_group");
      // Kept for the router, which will be repurposed; groups decide routing for now.
      createBaseControl("osc_" + number + "_destination");

      oscillators_[i]->useInput(input(kReset), OscillatorModule::kReset);
      oscillators_[i]->useInput(input(kRetrigger), OscillatorModule::kRetrigger);
      oscillators_[i]->useInput(input(kMidi), OscillatorModule::kMidi);
      oscillators_[i]->useInput(input(kActiveVoices), OscillatorModule::kActiveVoices);
      oscillators_[i]->useInput(input(kNoteCount), OscillatorModule::kNoteCount);
    }

    SynthModule::init();

    // Every FM/RM option reads the engine above, which process() copies into the modulator buffer.
    for (int i = 0; i < kNumOscillators; ++i) {
      Output* modulator = output(kModulatorStart + i);
      oscillators_[i]->oscillator()->setFirstOscillatorOutput(modulator);
      oscillators_[i]->oscillator()->setSecondOscillatorOutput(modulator);
      oscillators_[i]->oscillator()->setSampleOutput(modulator);
    }
  }

  void ProducersModule::updateOscillatorMemory() {
    for (int i = 0; i < kNumOscillators; ++i) {
      bool needs_memory = oscillators_[i]->isOn() && !oscillators_[i]->isSample();
      oscillators_[i]->oscillator()->setSpectralMemory(needs_memory);
    }
  }

  bool ProducersModule::setEngineOrder(const EngineOrder& order) {
    bool seen[kNumOscillators] = {};
    for (int index : order) {
      if (index < 0 || index >= kNumOscillators || seen[index])
        return false;
      seen[index] = true;
    }
    *engine_order_ = order;
    return true;
  }

  int ProducersModule::groupOf(int index) const {
    return utils::iclamp(static_cast<int>(oscillator_groups_[index]->value()), 0, kNumOscillatorGroups - 1);
  }

  int ProducersModule::getEngineAbove(int index) const {
    int group = groupOf(index);
    int above = -1;
    for (int engine : *engine_order_) {
      if (engine == index)
        return above;
      if (groupOf(engine) == group)
        above = engine;
    }
    return -1;
  }

  bool ProducersModule::isGroupUsed(int group) const {
    for (int i = 0; i < kNumOscillators; ++i) {
      if (oscillators_[i]->isOn() && oscillator_groups_[i] && groupOf(i) == group)
        return true;
    }
    return false;
  }

  void ProducersModule::process(int num_samples) {
    SynthModule::process(num_samples);

    for (int g = 0; g < kNumOscillatorGroups; ++g)
      utils::zeroBuffer(output(kGroupOutputStart + g)->buffer, num_samples);

    // Engines run top to bottom; each group's chain is the engine order filtered to that group.
    int last_in_group[kNumOscillatorGroups];
    for (int g = 0; g < kNumOscillatorGroups; ++g)
      last_in_group[g] = -1;

    for (int index : *engine_order_) {
      OscillatorModule* module = oscillators_[index];
      int group = groupOf(index);
      int above = last_in_group[group];
      last_in_group[group] = index;

      poly_float* modulator = output(kModulatorStart + index)->buffer;
      if (above >= 0)
        utils::copyBuffer(modulator, oscillators_[above]->output(OscillatorModule::kRaw)->buffer, num_samples);
      else
        utils::zeroBuffer(modulator, num_samples);

      getLocalProcessor(module)->process(num_samples);

      poly_float* group_output = output(kGroupOutputStart + group)->buffer;
      const poly_float* levelled = module->output(OscillatorModule::kLevelled)->buffer;
      utils::addBuffers(group_output, group_output, levelled, num_samples);
    }
  }
} // namespace vital
