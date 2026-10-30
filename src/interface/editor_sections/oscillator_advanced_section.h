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
#include "synth_section.h"

class IncrementerButtons;
class TextSelector;
class OscillatorOptions;
class OscillatorSection;
class OscillatorUnison;

class SampleSection;

class OscillatorAdvancedSection : public SynthSection {
  public:
    OscillatorAdvancedSection(int index, const vital::output_map& mono_modulations,
                              const vital::output_map& poly_modulations);
    virtual ~OscillatorAdvancedSection();

    void paintBackground(Graphics& g) override { paintChildrenBackgrounds(g); }
    void resized() override;
    void passOscillatorSection(const OscillatorSection* oscillator);
    // Noise engines spread color and focus instead of spectral morph and distortion.
    void setNoise(bool noise);
    // Sample engines: their own note tracking and power switch, and start and loop spreads.
    void setSample(bool sample);
    // The engine's group switch: while it's off, the engine's own switch can't brighten the drawer.
    void setGroupActive(bool active);
    void passSampleSection(const SampleSection* sample);

  private:
    std::unique_ptr<OscillatorOptions> oscillator_options_;
    std::unique_ptr<OscillatorUnison> oscillator_unison_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscillatorAdvancedSection)
};

