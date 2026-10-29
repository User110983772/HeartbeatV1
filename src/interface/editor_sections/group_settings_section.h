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
#include "open_gl_image.h"
#include "sound_engine_list.h"
#include "synth_constants.h"
#include "synth_section.h"

class OpenGlScrollBar;
class SynthSlider;
class TextSelector;
class GroupSettingsContainer;

// One group's drift & humanise, voice and unison settings, under a bar like the voice tab's group bar.
class GroupSettingsSection : public SynthSection {
  public:
    GroupSettingsSection(int group);

    void paintBackground(Graphics& g) override;
    void resized() override;

    // Shown in the bar: the group's engines.
    void setEngines(const std::vector<std::string>& engines) { engines_ = engines; }
    int getBarHeight();
    int getPanelTitleHeight();

  private:
    Rectangle<int> panelBounds(int panel);

    int group_;
    std::vector<std::string> engines_;

    std::unique_ptr<SynthSlider> drift_;
    std::unique_ptr<SynthSlider> drift_rate_;
    std::unique_ptr<SynthSlider> drift_spread_;
    std::unique_ptr<SynthSlider> humanize_pitch_;
    std::unique_ptr<SynthSlider> humanize_level_;
    std::unique_ptr<SynthSlider> humanize_pan_;
    std::unique_ptr<SynthSlider> humanize_phase_;

    std::unique_ptr<TextSelector> voice_mode_;
    std::unique_ptr<SynthSlider> glide_;
    std::unique_ptr<SynthSlider> glide_slope_;

    std::unique_ptr<SynthSlider> unison_voices_;
    std::unique_ptr<SynthSlider> unison_detune_;
    std::unique_ptr<SynthSlider> unison_blend_;
    std::unique_ptr<SynthSlider> stereo_spread_;
    std::unique_ptr<TextSelector> stack_style_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupSettingsSection)
};

// The Advanced tab's GROUPS page: a scrolling list with a section for every shown group, in the voice tab's order.
class GroupSettingsList : public SynthSection, public ScrollBar::Listener, public SoundEngineViewport::Listener {
  public:
    GroupSettingsList();
    virtual ~GroupSettingsList();

    void paintBackground(Graphics& g) override;
    void paintChildrenShadows(Graphics& g) override { }
    void resized() override;
    void setAllValues(vital::control_map& controls) override;
    void visibilityChanged() override;

    void initOpenGlComponents(OpenGlWrapper& open_gl) override;
    void renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) override;
    void destroyOpenGlComponents(OpenGlWrapper& open_gl) override;

    void scrollBarMoved(ScrollBar* scroll_bar, double range_start) override;
    void soundEnginesScrolled(int position) override;

    // Reuses the voice tab's listener for meters and for keeping modulation in place while scrolling.
    void addListener(SoundEngineList::Listener* listener) { listeners_.push_back(listener); }
    void updateLayout();

  private:
    void redoBackgroundImage();
    void setScrollBarRange();

    std::vector<SoundEngineList::Listener*> listeners_;
    SoundEngineViewport viewport_;
    std::unique_ptr<GroupSettingsContainer> container_;
    OpenGlImage background_;
    CriticalSection open_gl_critical_section_;
    std::unique_ptr<OpenGlScrollBar> scroll_bar_;
    std::unique_ptr<GroupSettingsSection> sections_[vital::kNumOscillatorGroups];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupSettingsList)
};
