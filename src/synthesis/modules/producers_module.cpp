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
  ProducersModule::ProducersModule() :
      SynthModule(kNumInputs, kNumOutputs), sample_destination_(nullptr) {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillators_[i] = new OscillatorModule("osc_" + number);
      addSubmodule(oscillators_[i]);
      addProcessor(oscillators_[i]);
      oscillators_[i]->enable(false);
      oscillator_groups_[i] = nullptr;
    }

    sampler_ = new SampleModule();
    addSubmodule(sampler_);
    addProcessor(sampler_);
    sampler_->enable(false);
  }

  void ProducersModule::init() {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillator_groups_[i] = createBaseControl("osc_" + number + "_group");
      // Kept for the oscillator router, which will be repurposed; groups decide routing for now.
      createBaseControl("osc_" + number + "_destination");

      oscillators_[i]->useInput(input(kReset), OscillatorModule::kReset);
      oscillators_[i]->useInput(input(kRetrigger), OscillatorModule::kRetrigger);
      oscillators_[i]->useInput(input(kMidi), OscillatorModule::kMidi);
      oscillators_[i]->useInput(input(kActiveVoices), OscillatorModule::kActiveVoices);
    }

    sample_destination_ = createBaseControl("sample_destination");
    sampler_->useInput(input(kReset), SampleModule::kReset);
    sampler_->useInput(input(kNoteCount), SampleModule::kNoteCount);
    sampler_->useInput(input(kMidi), SampleModule::kMidi);

    SynthModule::init();
    for (int i = 0; i < kNumOscillators; ++i) {
      int index1 = getFirstModulationIndex(i);
      int index2 = getSecondModulationIndex(i);
      oscillators_[i]->oscillator()->setFirstOscillatorOutput(oscillators_[index1]->output(OscillatorModule::kRaw));
      oscillators_[i]->oscillator()->setSecondOscillatorOutput(oscillators_[index2]->output(OscillatorModule::kRaw));
      oscillators_[i]->oscillator()->setSampleOutput(sampler_->output(SampleModule::kRaw));
    }
  }

  void ProducersModule::updateOscillatorMemory() {
    for (int i = 0; i < kNumOscillators; ++i)
      oscillators_[i]->oscillator()->setSpectralMemory(oscillators_[i]->isOn());
  }

  bool ProducersModule::isGroupUsed(int group) const {
    for (int i = 0; i < kNumOscillators; ++i) {
      if (oscillators_[i]->isOn() && oscillator_groups_[i] &&
          static_cast<int>(oscillator_groups_[i]->value()) == group) {
        return true;
      }
    }
    return false;
  }

  void ProducersModule::process(int num_samples) {
    SynthModule::process(num_samples);

    getLocalProcessor(sampler_)->process(num_samples);

    SynthOscillator::DistortionType distortion_types[kNumOscillators];
    bool processed[kNumOscillators];
    for (int i = 0; i < kNumOscillators; ++i) {
      distortion_types[i] = oscillators_[i]->getDistortionType();
      processed[i] = false;
    }
   
    int num_processed = 0;
    int index = 0;
    for (int i = 0; i < kNumOscillators * kNumOscillators && num_processed < kNumOscillators; ++i) {
      OscillatorModule* module = oscillators_[index];
      int first_source = getFirstModulationIndex(index);
      int second_source = getSecondModulationIndex(index);
      if ((!SynthOscillator::isFirstModulation(distortion_types[index]) || processed[first_source]) &&
          (!SynthOscillator::isSecondModulation(distortion_types[index]) || processed[second_source]) &&
          !processed[index]) {
        num_processed++;
        processed[index] = true;
        getLocalProcessor(module)->process(num_samples);
      }
      index = (index + 1) % kNumOscillators;
    }

    poly_float* raw_output = output(kRawOut)->buffer;
    poly_float* direct_output = output(kDirectOut)->buffer;
    utils::zeroBuffer(raw_output, num_samples);
    utils::zeroBuffer(direct_output, num_samples);

    for (int g = 0; g < kNumOscillatorGroups; ++g)
      utils::zeroBuffer(output(kGroupOutputStart + g)->buffer, num_samples);

    // Oscillators are summed per group; the voice handler applies each group's amp source, level and output.
    for (int i = 0; i < kNumOscillators; ++i) {
      const poly_float* buffer = oscillators_[i]->output(OscillatorModule::kLevelled)->buffer;
      int group = utils::iclamp(static_cast<int>(oscillator_groups_[i]->value()), 0, kNumOscillatorGroups - 1);
      poly_float* group_output = output(kGroupOutputStart + group)->buffer;
      utils::addBuffers(group_output, group_output, buffer, num_samples);
    }

    const poly_float* sample = sampler_->output(SampleModule::kLevelled)->buffer;

    // There are no voice filters; the sample goes to the effects unless it's routed to direct out.
    bool sample_direct_out = static_cast<int>(sample_destination_->value()) == constants::kDirectOut;
    poly_float* sample_output = sample_direct_out ? direct_output : raw_output;
    utils::addBuffers(sample_output, sample_output, sample, num_samples);
  }
} // namespace vital
