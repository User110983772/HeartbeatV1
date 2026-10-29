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

#include "oscillator_advanced_section.h"

#include "oscillator_section.h"
#include "fonts.h"
#include "bar_renderer.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "synth_oscillator.h"
#include "text_selector.h"
#include "text_look_and_feel.h"

class OscillatorOptions : public SynthSection {
  public:
    OscillatorOptions(int index) : SynthSection(String("OSC ") + String(index) + " OPTIONS"), index_(index) {
      createOffOverlay();
      oscillator_active_ = nullptr;

      String number(index);
      
      midi_track_ = std::make_unique<SynthButton>("osc_" + number + "_midi_track");
      addButton(midi_track_.get());
      midi_track_->setLookAndFeel(TextLookAndFeel::instance());
      midi_track_->setButtonText("NOTE TRACK");

      smooth_interpolation_ = std::make_unique<SynthButton>("osc_" + number + "_smooth_interpolation");
      addButton(smooth_interpolation_.get());
      smooth_interpolation_->setLookAndFeel(TextLookAndFeel::instance());
      smooth_interpolation_->setButtonText("HI-RES WAVETABLE");

      follow_group_ = std::make_unique<SynthButton>("osc_" + number + "_group_unison");
      addButton(follow_group_.get());
      follow_group_->setLookAndFeel(TextLookAndFeel::instance());
      follow_group_->setButtonText("GROUP UNISON");

      group_mode_ = std::make_unique<TextSelector>("osc_" + number + "_group_unison_mode");
      addSlider(group_mode_.get());
      group_mode_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      group_mode_->setLookAndFeel(TextLookAndFeel::instance());
      group_mode_->setLongStringLookup(strings::kGroupUnisonModeNames);
      group_mode_->getProperties().set(TextLookAndFeel::kCentreTextProperty, true);
    }

    virtual ~OscillatorOptions() { }

    void paintBackground(Graphics& g) override {
      SynthSection::paintBackground(g);

      g.setColour(findColour(Skin::kTextComponentBackground, true));
      float rounding = findValue(Skin::kLabelBackgroundRounding);
      g.fillRoundedRectangle(midi_track_->getBounds().toFloat(), rounding);
      if (!noise_)
        g.fillRoundedRectangle(smooth_interpolation_->getBounds().toFloat(), rounding);
      g.fillRoundedRectangle(follow_group_->getBounds().toFloat(), rounding);
      drawTextComponentBackground(g, group_mode_->getBounds(), true);
    }

    void paintBackgroundShadow(Graphics& g) override {
      if (isActive())
        paintTabShadow(g);
    }

    void resized() override {
      SynthSection::resized();

      int title_width = getTitleWidth();
      int widget_margin = getWidgetMargin();
      int x = title_width + widget_margin;
      int width = getWidth() - x - widget_margin;
      // Noise has no wavetable, so HI-RES WAVETABLE is gone and the other rows share its space evenly.
      std::vector<Component*> rows = { midi_track_.get(), follow_group_.get(), group_mode_.get() };
      if (!noise_)
        rows.insert(rows.begin() + 1, smooth_interpolation_.get());
      smooth_interpolation_->setVisible(!noise_);

      int count = static_cast<int>(rows.size());
      float row_height = (getHeight() - (count + 1) * widget_margin) / (1.0f * count);
      for (int i = 0; i < count; ++i) {
        int top = std::round(widget_margin + i * (row_height + widget_margin));
        int bottom = std::round(widget_margin + i * (row_height + widget_margin) + row_height);
        rows[i]->setBounds(x, top, width, bottom - top);
      }
    }

    void setNoise(bool noise) {
      if (noise == noise_)
        return;
      noise_ = noise;
      resized();
      repaintBackground();
    }

    // The mode only matters while the engine uses its group's unison; then its text is red like a switched on
    // button, otherwise dimmed.
    void updateGroupModeActive() {
      bool engine_on = oscillator_active_ == nullptr || oscillator_active_->getToggleState();
      bool group_unison = follow_group_->getToggleState();
      group_mode_->getProperties().set(TextLookAndFeel::kOnTextProperty, group_unison);
      group_mode_->setActive(group_unison && engine_on);
      group_mode_->redoImage();
    }

    void setAllValues(vital::control_map& controls) override {
      SynthSection::setAllValues(controls);

      if (oscillator_active_)
        setActive(oscillator_active_->getToggleState());
      updateGroupModeActive();
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == oscillator_active_)
        setActive(oscillator_active_->getToggleState());
      else
        SynthSection::buttonClicked(clicked_button);
      updateGroupModeActive();
    }

    void passOscillatorSection(const OscillatorSection* oscillator) {
      oscillator_active_ = oscillator->activator();
      oscillator_active_->addListener(this);
      setActive(oscillator_active_->getToggleState());
      updateGroupModeActive();
    }

  private:
    int index_;
    bool noise_ = false;
    ToggleButton* oscillator_active_;

    std::unique_ptr<SynthButton> midi_track_;
    std::unique_ptr<SynthButton> smooth_interpolation_;
    std::unique_ptr<SynthButton> follow_group_;
    std::unique_ptr<TextSelector> group_mode_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscillatorOptions)
};

class SpreadVisualizer : public BarRenderer {
  public:
    static constexpr int kNumSpreads = 3;

    SpreadVisualizer(int index, const vital::output_map& mono_modulations, const vital::output_map& poly_modulations) :
        BarRenderer(kNumSpreads * vital::SynthOscillator::kMaxUnison) {
      setAdditiveBlending(false);
      std::string number = std::to_string(index);

      std::string voices_name = "osc_" + number + "_unison_voices";
      voices_outputs_ = {
        mono_modulations.at(voices_name),
        poly_modulations.at(voices_name)
      };

      std::string wave_frame_name = "osc_" + number + "_wave_frame";
      wave_frame_outputs_ = {
        mono_modulations.at(wave_frame_name),
        poly_modulations.at(wave_frame_name)
      };

      std::string spectral_morph_name = "osc_" + number + "_spectral_morph_amount";
      spectral_morph_outputs_ = {
        mono_modulations.at(spectral_morph_name),
        poly_modulations.at(spectral_morph_name)
      };

      std::string distortion_name = "osc_" + number + "_distortion_amount";
      distortion_outputs_ = {
        mono_modulations.at(distortion_name),
        poly_modulations.at(distortion_name)
      };

      std::string table_spread_name = "osc_" + number + "_frame_spread";
      table_spread_outputs_ = {
        mono_modulations.at(table_spread_name),
        poly_modulations.at(table_spread_name)
      };

      std::string spectral_spread_name = "osc_" + number + "_spectral_morph_spread";
      spectral_spread_outputs_ = {
        mono_modulations.at(spectral_spread_name),
        poly_modulations.at(spectral_spread_name)
      };

      std::string distortion_spread_name = "osc_" + number + "_distortion_spread";
      distortion_spread_outputs_ = {
        mono_modulations.at(distortion_spread_name),
        poly_modulations.at(distortion_spread_name)
      };

      // Noise: the middle row shows color and its spread, the bottom row focus and its spread.
      std::string color_name = "osc_" + number + "_noise_color";
      color_outputs_ = { mono_modulations.at(color_name), poly_modulations.at(color_name) };
      std::string focus_name = "osc_" + number + "_noise_focus";
      focus_outputs_ = { mono_modulations.at(focus_name), poly_modulations.at(focus_name) };
      std::string focus_spread_name = "osc_" + number + "_noise_focus_spread";
      focus_spread_outputs_ = { mono_modulations.at(focus_spread_name), poly_modulations.at(focus_spread_name) };
      noise_ = false;
      color_slider_ = nullptr;
      focus_slider_ = nullptr;
      focus_spread_slider_ = nullptr;

      voices_slider_ = nullptr;

      wave_frame_slider_ = nullptr;
      spectral_morph_slider_ = nullptr;
      distortion_slider_ = nullptr;

      table_spread_slider_ = nullptr;
      spectral_spread_slider_ = nullptr;
      distortion_spread_slider_ = nullptr;
    }

    void setVoicesSlider(const SynthSlider* slider) { voices_slider_ = slider; }
    void setFrameSlider(const SynthSlider* slider) { wave_frame_slider_ = slider; }
    void setSpectralMorphSlider(const SynthSlider* slider) { spectral_morph_slider_ = slider; }
    void setDistortionSlider(const SynthSlider* slider) { distortion_slider_ = slider; }
    void setTableSpreadSlider(const SynthSlider* slider) { table_spread_slider_ = slider; }
    void setSpectralSpreadSlider(const SynthSlider* slider) { spectral_spread_slider_ = slider; }
    void setDistortionSpreadSlider(const SynthSlider* slider) { distortion_spread_slider_ = slider; }
    void setNoiseSliders(const SynthSlider* color, const SynthSlider* focus, const SynthSlider* focus_spread) {
      color_slider_ = color;
      focus_slider_ = focus;
      focus_spread_slider_ = focus_spread;
    }
    void setNoise(bool noise) { noise_ = noise; }

    inline vital::poly_float getOutputsTotal(std::pair<vital::Output*, vital::Output*> outputs,
                                             vital::poly_float default_value, bool animate) {
      if (!outputs.first->owner->enabled() || !animate)
        return default_value;
      return outputs.first->trigger_value + outputs.second->trigger_value;
    }

    void paintBackground(Graphics& g) override {
      if (!isVisible())
        return;

      g.setColour(findColour(Skin::kWidgetBackground, true));
      g.fillRoundedRectangle(getLocalBounds().toFloat(), findValue(Skin::kWidgetRoundedCorner));
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      if (voices_slider_ == nullptr)
        return;

      int voices = getOutputsTotal(voices_outputs_, voices_slider_->getValue(), animate)[0];
      voices = std::max(voices, 0);
      if (voices <= 2)
        return;

      bool noise = noise_ && color_slider_ && focus_slider_ && focus_spread_slider_;
      vital::poly_float frame = getOutputsTotal(wave_frame_outputs_, wave_frame_slider_->getValue(), animate);
      vital::poly_float morph = noise ? getOutputsTotal(color_outputs_, color_slider_->getValue(), animate) :
                                        getOutputsTotal(spectral_morph_outputs_, spectral_morph_slider_->getValue(), animate);
      vital::poly_float distortion = noise ? getOutputsTotal(focus_outputs_, focus_slider_->getValue(), animate) :
                                             getOutputsTotal(distortion_outputs_, distortion_slider_->getValue(), animate);

      vital::poly_float frame_spread = getOutputsTotal(table_spread_outputs_,
                                                       table_spread_slider_->getValue(), animate);
      vital::poly_float morph_spread = getOutputsTotal(spectral_spread_outputs_,
                                                       spectral_spread_slider_->getValue(), animate);
      vital::poly_float distortion_spread = noise ?
          getOutputsTotal(focus_spread_outputs_, focus_spread_slider_->getValue(), animate) :
          getOutputsTotal(distortion_spread_outputs_, distortion_spread_slider_->getValue(), animate);

      setColor(findColour(Skin::kWidgetSecondary1, true));
      setBarWidth(2.0f / getWidth());

      float height_buffer = 2.0f * findValue(Skin::kWidgetMargin) / getHeight();
      float height = (2.0f - height_buffer) / kNumSpreads - height_buffer;
      float y = height_buffer - 1.0f;
      for (int s = 0; s < kNumSpreads; ++s) {
        int start = s * vital::SynthOscillator::kMaxUnison;
        for (int i = 0; i < vital::SynthOscillator::kMaxUnison; ++i) {
          setBottom(start + i, y + height);
          setY(start + i, y);
        }

        y += height + height_buffer;
      }

      float buffer = 2.0f * findValue(Skin::kWidgetMargin) / getWidth();
      float mult = 2.0f - 2.0f * buffer;
      float offset = -1.0f + buffer - 1.0f / getWidth();
      float frame_scale = 1.0f / (vital::kNumOscillatorWaveFrames - 1.0f);
      voices += voices % 2;
      for (int i = 0; i < voices; i += 2) {
        float t = 2.0f * i / (voices - 2.0f);
        vital::poly_float voice_frame = frame + frame_spread * t;
        voice_frame = vital::utils::clamp(voice_frame * frame_scale, 0.0f, 1.0f);
        vital::poly_float voice_morph = vital::utils::clamp(morph + morph_spread * t, 0.0f, 1.0f);
        vital::poly_float voice_distortion = vital::utils::clamp(distortion + distortion_spread * t, 0.0f, 1.0f);

        // Noise has no wave frame, so its row stays empty.
        setX(i, noise ? -2.0f : voice_frame[0] * mult + offset);
        setX(i + 1, noise ? -2.0f : voice_frame[1] * mult + offset);

        setX(i + vital::SynthOscillator::kMaxUnison, voice_morph[0] * mult + offset);
        setX(i + vital::SynthOscillator::kMaxUnison + 1, voice_morph[1] * mult + offset);

        setX(i + 2 * vital::SynthOscillator::kMaxUnison, voice_distortion[0] * mult + offset);
        setX(i + 2 * vital::SynthOscillator::kMaxUnison + 1, voice_distortion[1] * mult + offset);
      }

      for (int s = 0; s < kNumSpreads; ++s) {
        int start = s * vital::SynthOscillator::kMaxUnison;
        for (int i = voices; i < vital::SynthOscillator::kMaxUnison; ++i)
          setX(start + i, -2.0f);
      }

      BarRenderer::render(open_gl, animate);
    }

  private:
    const SynthSlider* voices_slider_;

    const SynthSlider* wave_frame_slider_;
    const SynthSlider* spectral_morph_slider_;
    const SynthSlider* distortion_slider_;

    const SynthSlider* table_spread_slider_;
    const SynthSlider* spectral_spread_slider_;
    const SynthSlider* distortion_spread_slider_;

    std::pair<vital::Output*, vital::Output*> voices_outputs_;

    std::pair<vital::Output*, vital::Output*> wave_frame_outputs_;
    std::pair<vital::Output*, vital::Output*> spectral_morph_outputs_;
    std::pair<vital::Output*, vital::Output*> distortion_outputs_;

    std::pair<vital::Output*, vital::Output*> table_spread_outputs_;
    std::pair<vital::Output*, vital::Output*> spectral_spread_outputs_;
    std::pair<vital::Output*, vital::Output*> distortion_spread_outputs_;

    bool noise_;
    const SynthSlider* color_slider_;
    const SynthSlider* focus_slider_;
    const SynthSlider* focus_spread_slider_;
    std::pair<vital::Output*, vital::Output*> color_outputs_;
    std::pair<vital::Output*, vital::Output*> focus_outputs_;
    std::pair<vital::Output*, vital::Output*> focus_spread_outputs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpreadVisualizer)
};

class OscillatorUnison : public SynthSection {
  public:
    OscillatorUnison(int index, const vital::output_map& mono_modulations, const vital::output_map& poly_modulations) :
        SynthSection(String("OSC ") + String(index) + " UNISON"), index_(index) {
      createOffOverlay();
      voices_slider_ = nullptr;
      oscillator_active_ = nullptr;

      String number(index);

      spectral_unison_ = std::make_unique<SynthButton>("osc_" + number + "_spectral_unison");
      addButton(spectral_unison_.get());
      spectral_unison_->setLookAndFeel(TextLookAndFeel::instance());
      spectral_unison_->setButtonText("SPECTRAL UNISON");

      stack_style_ = std::make_unique<TextSelector>("osc_" + number + "_stack_style");
      addSlider(stack_style_.get());
      stack_style_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      stack_style_->setLookAndFeel(TextLookAndFeel::instance());
      stack_style_->setLongStringLookup(strings::kUnisonStackNames);

      blend_ = std::make_unique<SynthSlider>("osc_" + number + "_unison_blend");
      addSlider(blend_.get());
      blend_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

      detune_range_ = std::make_unique<SynthSlider>("osc_" + number + "_detune_range");
      addSlider(detune_range_.get());
      detune_range_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      detune_range_->setLookAndFeel(TextLookAndFeel::instance());

      frame_spread_ = std::make_unique<SynthSlider>("osc_" + number + "_frame_spread");
      addSlider(frame_spread_.get());
      frame_spread_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      frame_spread_->setBipolar(true);

      distortion_spread_ = std::make_unique<SynthSlider>("osc_" + number + "_distortion_spread");
      addSlider(distortion_spread_.get());
      distortion_spread_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      distortion_spread_->setBipolar(true);

      spectral_morph_spread_ = std::make_unique<SynthSlider>("osc_" + number + "_spectral_morph_spread");
      addSlider(spectral_morph_spread_.get());
      spectral_morph_spread_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      spectral_morph_spread_->setBipolar(true);

      // Noise's focus spread starts at 0, like the Focus knob, and takes the distortion spread's place.
      focus_spread_ = std::make_unique<SynthSlider>("osc_" + number + "_noise_focus_spread");
      addSlider(focus_spread_.get());
      focus_spread_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
      focus_spread_->setVisible(false);
      noise_ = false;

      spread_visualizer_ = std::make_unique<SpreadVisualizer>(index, mono_modulations, poly_modulations);
      addOpenGlComponent(spread_visualizer_.get());
      spread_visualizer_->setTableSpreadSlider(frame_spread_.get());
      spread_visualizer_->setSpectralSpreadSlider(spectral_morph_spread_.get());
      spread_visualizer_->setDistortionSpreadSlider(distortion_spread_.get());

      stereo_spread_ = std::make_unique<SynthSlider>("osc_" + number + "_stereo_spread");
      addSlider(stereo_spread_.get());
      stereo_spread_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    }

    virtual ~OscillatorUnison() = default;

    void paintBackground(Graphics& g) override {
      SynthSection::paintBackground(g);
      setLabelFont(g);

      drawTextComponentBackground(g, stack_style_->getBounds(), true);
      drawTextComponentBackground(g, detune_range_->getBounds(), true);
      drawLabelForComponent(g, TRANS("STACK"), stack_style_.get(), true);
      drawLabelForComponent(g, TRANS("DETUNE RANGE"), detune_range_.get(), true);
      drawLabelForComponent(g, TRANS("UNISON BLEND"), blend_.get());
      drawLabelForComponent(g, TRANS("STEREO UNISON"), stereo_spread_.get());
      if (!noise_)
        drawLabelForComponent(g, TRANS("TABLE SPREAD"), frame_spread_.get());
      drawLabelForComponent(g, noise_ ? TRANS("FOCUS SPREAD") : TRANS("DIST SPREAD"), distortion_spread_.get());
      drawLabelForComponent(g, noise_ ? TRANS("COLOR SPREAD") : TRANS("SPECT SPREAD"), spectral_morph_spread_.get());
    }

    void paintBackgroundShadow(Graphics& g) override {
      if (isActive())
        paintTabShadow(g);
    }

    void resized() override {
      static constexpr float kTextComponentWidthRatio = 0.23f;
      SynthSection::resized();

      int knob_section_height = getKnobSectionHeight();
      int title_width = getTitleWidth();
      int widget_margin = getWidgetMargin();

      int text_width = getWidth() * kTextComponentWidthRatio - 2 * widget_margin;
      int text_height = knob_section_height - 2 * widget_margin;
      stack_style_->setBounds(title_width + widget_margin, widget_margin, text_width, text_height);
      detune_range_->setBounds(title_width + widget_margin, knob_section_height, text_width, text_height);

      int controls_x = detune_range_->getRight();
      placeKnobsInArea(Rectangle<int>(controls_x, 0, getWidth() - controls_x, knob_section_height),
                       { blend_.get(), nullptr, nullptr, nullptr });
      stack_style_->setBounds(stack_style_->getBounds().withTop(widget_margin));

      // The spread display keeps the same size for noise; noise has no wave frames, so its two spreads (color
      // and focus) share the space under the display that the three wavetable spreads use.
      int knob_y2 = getHeight() - knob_section_height;
      Rectangle<int> knob_row(controls_x, knob_y2, getWidth() - controls_x, knob_section_height);
      placeKnobsInArea(knob_row, { stereo_spread_.get(), frame_spread_.get(),
                                   spectral_morph_spread_.get(), distortion_spread_.get() });
      int spreads_x = frame_spread_->getX();
      int spreads_right = distortion_spread_->getRight();
      spread_visualizer_->setBounds(spreads_x, widget_margin, spreads_right - spreads_x, text_height);
      focus_spread_->setBounds(distortion_spread_->getBounds());

      if (noise_) {
        Rectangle<int> spreads(spreads_x, knob_y2, spreads_right - spreads_x, knob_section_height);
        placeKnobsInArea(spreads, { spectral_morph_spread_.get(), focus_spread_.get() });
        distortion_spread_->setBounds(focus_spread_->getBounds());
      }
    }

    void setNoise(bool noise) {
      if (noise == noise_)
        return;
      noise_ = noise;
      distortion_spread_->setVisible(!noise);
      frame_spread_->setVisible(!noise);
      focus_spread_->setVisible(noise);
      spread_visualizer_->setNoise(noise);
      resized();
      repaintBackground();
    }

    void passOscillatorSection(const OscillatorSection* oscillator) {
      voices_slider_ = oscillator->getVoicesSlider();
      oscillator_active_ = oscillator->activator();
      oscillator_active_->addListener(this);
      voices_slider_->addListener(this);

      spread_visualizer_->setVoicesSlider(voices_slider_);
      spread_visualizer_->setFrameSlider(oscillator->getWaveFrameSlider());
      spread_visualizer_->setSpectralMorphSlider(oscillator->getSpectralMorphSlider());
      spread_visualizer_->setDistortionSlider(oscillator->getDistortionSlider());
      spread_visualizer_->setNoiseSliders(oscillator->getNoiseColorSlider(), oscillator->getNoiseFocusSlider(),
                                          focus_spread_.get());

      checkActive();
    }

    void checkActive() {
      if (voices_slider_ && oscillator_active_)
        setActive(voices_slider_->getValue() > 1.0 && oscillator_active_->getToggleState());
    }

    void sliderValueChanged(Slider* changed_slider) override {
      if (changed_slider == voices_slider_)
        checkActive();
      else
        SynthSection::sliderValueChanged(changed_slider);
    }

    void buttonClicked(Button* clicked_button) override {
      if (clicked_button == oscillator_active_)
        checkActive();
      else
        SynthSection::buttonClicked(clicked_button);
    }

    void setAllValues(vital::control_map& controls) override {
      SynthSection::setAllValues(controls);
      checkActive();
    }

  private:
    int index_;

    std::unique_ptr<SynthButton> spectral_unison_;
    std::unique_ptr<TextSelector> stack_style_;
    std::unique_ptr<SynthSlider> detune_range_;
    std::unique_ptr<SynthSlider> stereo_spread_;
    std::unique_ptr<SynthSlider> blend_;

    std::unique_ptr<SpreadVisualizer> spread_visualizer_;
    std::unique_ptr<SynthSlider> frame_spread_;
    std::unique_ptr<SynthSlider> distortion_spread_;
    std::unique_ptr<SynthSlider> spectral_morph_spread_;
    std::unique_ptr<SynthSlider> focus_spread_;
    bool noise_;

    ToggleButton* oscillator_active_;
    SynthSlider* voices_slider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscillatorUnison)
};

OscillatorAdvancedSection::OscillatorAdvancedSection(int index,
                                                     const vital::output_map& mono_modulations,
                                                     const vital::output_map& poly_modulations) :
    SynthSection(String("OSC ") + String(index)) {
  oscillator_options_ = std::make_unique<OscillatorOptions>(index);
  addSubSection(oscillator_options_.get());

  oscillator_unison_ = std::make_unique<OscillatorUnison>(index, mono_modulations, poly_modulations);
  addSubSection(oscillator_unison_.get());
}

OscillatorAdvancedSection::~OscillatorAdvancedSection() { }

void OscillatorAdvancedSection::resized() {
  SynthSection::resized();
  int padding = findValue(Skin::kPadding);
  int options_width = getWidth() * 0.22f;
  oscillator_options_->setBounds(0, 0, options_width, getHeight());

  int unison_x = oscillator_options_->getRight() + padding;
  oscillator_unison_->setBounds(unison_x, 0, getWidth() - unison_x, getHeight());
}

void OscillatorAdvancedSection::setNoise(bool noise) {
  oscillator_options_->setNoise(noise);
  oscillator_unison_->setNoise(noise);
}

void OscillatorAdvancedSection::passOscillatorSection(const OscillatorSection* oscillator) {
  oscillator_options_->passOscillatorSection(oscillator);
  oscillator_unison_->passOscillatorSection(oscillator);
}
