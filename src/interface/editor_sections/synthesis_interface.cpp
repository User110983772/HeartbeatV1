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

SynthesisInterface::SynthesisInterface(Authentication* auth,
                                       const vital::output_map& mono_modulations,
                                       const vital::output_map& poly_modulations) : SynthSection("synthesis") {
  sound_engines_ = std::make_unique<SoundEngineList>(auth, mono_modulations, poly_modulations);
  addSubSection(sound_engines_.get());
  setOpaque(false);
}

SynthesisInterface::~SynthesisInterface() { }

void SynthesisInterface::paintBackground(Graphics& g) {
  paintChildrenBackgrounds(g);
}

void SynthesisInterface::resized() {
  sound_engines_->setBounds(getLocalBounds());
  SynthSection::resized();
}

Rectangle<int> SynthesisInterface::getOscillatorBounds(int index) {
  int height = sound_engines_->getOscillatorHeight();
  return Rectangle<int>(0, index * (height + getPadding()), getWidth(), height);
}

void SynthesisInterface::visibilityChanged() {
  if (isShowing()) {
    for (int i = 0; i < vital::kNumOscillators; ++i)
      getOscillatorSection(i)->loadBrowserState();
  }
}
