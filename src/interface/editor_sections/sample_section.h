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
#include "sample_source.h"
#include "preset_selector.h"
#include "sample_viewer.h"
#include "transpose_quantize.h"

class SynthSlider;
class OpenGlShapeButton;

class SampleSection : public SynthSection, public SampleViewer::Listener, public PresetSelector::Listener,
                      TransposeQuantizeButton::Listener {
  public:
    // The wavetable card's pitch column width, so the PITCH box matches it.
    static constexpr float kPitchWidthRatio = 0.19f;
    static constexpr float kKnobsWidthRatio = 0.42f;
    static constexpr double kDefaultLoopCrossfade = 0.05;

    class Listener {
      public:
        virtual ~Listener() { }
        virtual void sampleDestinationChanged(SampleSection* sample, int destination) = 0;
    };

    SampleSection(String name, int engine, const vital::output_map& mono_modulations,
                  const vital::output_map& poly_modulations);
    int engine() const { return engine_; }
    ~SampleSection();

    void parentHierarchyChanged() override;

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { if (isActive()) paintTabShadow(g); }
    void setActive(bool active) override;
    void resized() override;
    void reset() override;
    void setAllValues(vital::control_map& controls) override;
    void buttonClicked(Button* clicked_button) override;
    void setDestinationSelected(int selection);
    void setupDestination();

    void loadFile(const File& file) override;
    void sampleLoaded(const File& file) override { loadFile(file); }
    File getCurrentFile() override { return File(sample_->getLastBrowsedFile()); }

    void prevClicked() override;
    void nextClicked() override;
    void textMouseDown(const MouseEvent& e) override;
    void quantizeUpdated() override;

    void addListener(Listener* listener) { listeners_.push_back(listener); }

    void sliderValueChanged(Slider* changed_slider) override;
    void sampleMarkerMoved(int marker, float value) override;
    void updateMarkers();
    int pitchBoxHeight();
    SynthSlider* getVoicesSlider() const { return unison_voices_.get(); }
    const SynthSlider* getStartSlider() const { return start_.get(); }
    // Where a unison voice plays with the card's current settings (as the sampler places it).
    vital::SampleSource::Placement placeVoice(double t, double start_spread, double loop_spread,
                                              double loop_end_spread) const;
    const SynthSlider* getLoopStartSlider() const { return loop_start_.get(); }
    const SynthSlider* getLoopEndSlider() const { return loop_end_.get(); }
    // STRETCH (and SYNC, which needs it): the drawer's START and LOOP SPREAD don't apply while stretched.
    SynthButton* getStretchButton() const { return stretch_.get(); }
    void updateStretchControls(Button* clicked);
    void updateLoopControls();
    void keepMarkersInOrder(Slider* changed_slider);
    ToggleButton* getLoopButton() const { return loop_.get(); }
    void guessTempo();

  private:
    std::vector<Listener*> listeners_;
    std::unique_ptr<TransposeQuantizeButton> transpose_quantize_button_;
    std::unique_ptr<SynthSlider> transpose_;
    std::unique_ptr<SynthSlider> tune_;
    std::unique_ptr<SynthSlider> pan_;
    std::unique_ptr<SynthSlider> level_;
    std::unique_ptr<SampleViewer> sample_viewer_;
    std::unique_ptr<PresetSelector> preset_selector_;

    int current_destination_;
    std::string destination_control_name_;
    std::unique_ptr<PlainTextComponent> destination_text_;
    std::unique_ptr<ShapeButton> destination_selector_;
    std::unique_ptr<OpenGlShapeButton> prev_destination_;
    std::unique_ptr<OpenGlShapeButton> next_destination_;

    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<OpenGlShapeButton> loop_;
    std::unique_ptr<OpenGlShapeButton> bounce_;
    std::unique_ptr<OpenGlShapeButton> keytrack_;
    std::unique_ptr<OpenGlShapeButton> random_phase_;
    std::unique_ptr<OpenGlShapeButton> reverse_;

    std::unique_ptr<SynthSlider> start_;
    std::unique_ptr<SynthSlider> end_;
    std::unique_ptr<SynthButton> stretch_;
    std::unique_ptr<SynthButton> tempo_sync_;
    std::unique_ptr<SynthSlider> bpm_;
    std::unique_ptr<SynthButton> snap_zero_;
    std::unique_ptr<SynthButton> zoom_out_;
    std::unique_ptr<SynthButton> zoom_in_;
    std::unique_ptr<SynthSlider> loop_start_;
    std::unique_ptr<SynthSlider> loop_end_;
    std::unique_ptr<SynthSlider> crossfade_;
    std::unique_ptr<SynthSlider> unison_voices_;
    std::unique_ptr<SynthSlider> unison_detune_;
    std::unique_ptr<SynthSlider> stereo_;

    AudioSampleBuffer sample_buffer_;
    int engine_;
    std::string engine_prefix_;
    vital::Sample* sample_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleSection)
};

