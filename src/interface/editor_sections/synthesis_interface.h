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

#include "JuceHeader.h"

#include "sound_engine_list.h"
#include "synth_section.h"

// The voice tab: the scrolling list of oscillator groups and sound engines.
class SynthesisInterface : public SynthSection {
  public:
    SynthesisInterface(Authentication* auth,
                       const vital::output_map& mono_modulations,
                       const vital::output_map& poly_modulations);
    virtual ~SynthesisInterface();

    void paintBackground(Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    void setFocus() { grabKeyboardFocus(); }

    Slider* getWaveFrameSlider(int index) { return getOscillatorSection(index)->getWaveFrameSlider(); }
    // Where the Advanced tab places its panel for sound engine `index` (it still shows a fixed stack).
    Rectangle<int> getOscillatorBounds(int index);
    void setWavetableName(int index, String name) { getOscillatorSection(index)->setName(name); }
    OscillatorSection* getOscillatorSection(int index) { return sound_engines_->getOscillatorSection(index); }
    SoundEngineList* getSoundEngineList() { return sound_engines_.get(); }

  private:
    std::unique_ptr<SoundEngineList> sound_engines_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthesisInterface)
};
