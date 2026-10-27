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
#include "noise_source.h"
#include "open_gl_line_renderer.h"
#include "synth_oscillator.h"
#include "synth_types.h"

// Spectrum of an oscillator's noise source, drawn like Wavetable3d's spectrum view: one filled line
// per stereo channel, following live modulation. Unison voices are independent noise, so their
// responses add in power, with the engine's detune ratios and center/detuned amplitudes.
class NoiseViewer : public OpenGlComponent {
  public:
    static constexpr int kResolution = 256;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr float kMinDb = -36.0f;
    static constexpr float kMaxDb = 54.0f;
    // Focus is shown at the oscillator's transposed middle C.
    static constexpr float kReferenceMidiNote = 60.0f;

    NoiseViewer(int index, const vital::output_map& mono_modulations, const vital::output_map& poly_modulations);
    virtual ~NoiseViewer();

    void init(OpenGlWrapper& open_gl) override;
    void render(OpenGlWrapper& open_gl, bool animate) override;
    void destroy(OpenGlWrapper& open_gl) override;
    void paintBackground(Graphics& g) override;
    void resized() override;

    void setColorSlider(Slider* slider) { color_slider_ = slider; }
    void setFocusSlider(Slider* slider) { focus_slider_ = slider; }
    void setTransposeSlider(Slider* slider) { transpose_slider_ = slider; }
    void setTuneSlider(Slider* slider) { tune_slider_ = slider; }
    void setUnisonSliders(Slider* voices, Slider* detune, Slider* detune_power) {
      voices_slider_ = voices;
      detune_slider_ = detune;
      detune_power_slider_ = detune_power;
    }

    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }

  private:
    vital::poly_float getOutputsTotal(std::pair<vital::Output*, vital::Output*> outputs,
                                      vital::poly_float default_value);
    struct State {
      vital::poly_float color, focus, midi, voices, cents, power, blend;
      float stack_style;
      bool operator==(const State& other) const;
    };

    State readState();
    void loadResponses(const State& state);
    void loadControls();

    OpenGlLineRenderer left_line_renderer_;
    OpenGlLineRenderer right_line_renderer_;
    vital::NoiseSource response_;

    std::pair<vital::Output*, vital::Output*> color_outputs_;
    std::pair<vital::Output*, vital::Output*> focus_outputs_;
    std::pair<vital::Output*, vital::Output*> transpose_outputs_;
    std::pair<vital::Output*, vital::Output*> tune_outputs_;
    std::pair<vital::Output*, vital::Output*> voices_outputs_;
    std::pair<vital::Output*, vital::Output*> detune_outputs_;
    std::pair<vital::Output*, vital::Output*> detune_power_outputs_;
    std::pair<vital::Output*, vital::Output*> detune_range_outputs_;
    std::pair<vital::Output*, vital::Output*> blend_outputs_;
    std::string prefix_;
    vital::Value* detune_range_control_;
    vital::Value* blend_control_;
    vital::Value* stack_style_control_;
    float evaluate_frequencies_[kResolution + 1];
    float power_[2][kResolution + 1];

    Slider* color_slider_;
    Slider* focus_slider_;
    Slider* transpose_slider_;
    Slider* tune_slider_;
    Slider* voices_slider_;
    Slider* detune_slider_;
    Slider* detune_power_slider_;

    State last_state_;
    bool dirty_;
    bool animate_;
    bool active_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseViewer)
};
