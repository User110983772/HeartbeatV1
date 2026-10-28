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

#include "synthesis_interface.h"

#include "oscillator_section.h"
#include "producers_module.h"
#include "synth_oscillator.h"
#include "sample_section.h"

SynthesisInterface::SynthesisInterface(Authentication* auth,
                                       const vital::output_map& mono_modulations,
                                       const vital::output_map& poly_modulations) : SynthSection("synthesis") {
  for (int i = 0; i < vital::kNumOscillatorSections; ++i) {
    oscillators_[i] = std::make_unique<OscillatorSection>(auth, i, mono_modulations, poly_modulations);
    addSubSection(oscillators_[i].get());
    oscillators_[i]->addListener(this);
  }

  sample_section_ = std::make_unique<SampleSection>("SMP");
  addSubSection(sample_section_.get());
  sample_section_->addListener(this);

  setOpaque(false);
}

SynthesisInterface::~SynthesisInterface() { }

void SynthesisInterface::paintBackground(Graphics& g) {
  paintChildrenBackgrounds(g);
}

void SynthesisInterface::resized() {
  int padding = getPadding();

  int oscillator_margin = oscillators_[0]->findValue(Skin::kWidgetMargin);
  int oscillator_height = 2 * (int)oscillators_[0]->getKnobSectionHeight() - oscillator_margin;

  for (int i = 0; i < vital::kNumOscillatorSections; ++i)
    oscillators_[i]->setBounds(0, i * (oscillator_height + padding), getWidth(), oscillator_height);

  int sample_y = oscillators_[vital::kNumOscillatorSections - 1]->getBottom() + padding;
  int sample_height = sample_section_->getKnobSectionHeight();
  sample_section_->setBounds(0, sample_y, getWidth(), sample_height);
  SynthSection::resized();
}

void SynthesisInterface::visibilityChanged() {
  if (isShowing()) {
    for (int i = 0; i < vital::kNumOscillatorSections; ++i)
      oscillators_[i]->loadBrowserState();
  }
}

void SynthesisInterface::distortionTypeChanged(OscillatorSection* section, int distortion_type) {
  bool dependents[vital::kNumOscillatorSections];
  for (int i = 0; i < vital::kNumOscillatorSections; ++i)
    dependents[i] = false;

  int index = section->index();
  int last_index = index;
  while (!dependents[index]) {
    dependents[index] = true;
    last_index = index;
    int type = oscillators_[index]->getDistortion();
    if (vital::SynthOscillator::isFirstModulation(type))
      index = vital::ProducersModule::getFirstModulationIndex(index);
    else if (vital::SynthOscillator::isSecondModulation(type))
      index = vital::ProducersModule::getSecondModulationIndex(index);
    else
      return;
  }

  oscillators_[last_index]->resetOscillatorModulationDistortionType();
}

// The oscillator and sample routers are kept for a later feature; with no voice filters they route nothing.
void SynthesisInterface::oscillatorDestinationChanged(OscillatorSection* section, int destination) { }

void SynthesisInterface::sampleDestinationChanged(SampleSection* section, int destination) { }
