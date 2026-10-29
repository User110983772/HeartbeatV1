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
#include "open_gl_image_component.h"
#include "open_gl_multi_quad.h"
#include "oscillator_advanced_section.h"
#include "oscillator_section.h"
#include "sample_section.h"
#include "synth_constants.h"
#include "synth_section.h"

class OpenGlShapeButton;
class SynthButton;
class OpenGlToggleButton;
class SynthSlider;
class TextSelector;
class SoundEngineContainer;

// Header of one oscillator group: collapse arrow, name, amp source, level, output and an add button.
class GroupHeader : public SynthSection {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void groupCollapseToggled(int group) = 0;
        virtual void addEngineClicked(int group, Component* source) = 0;
        virtual void groupPowerToggled(int group) = 0;
        virtual void groupAmpSourceChanged(int group) = 0;
        virtual void removeGroupClicked(int group) = 0;
    };

    GroupHeader(int group);

    void paintBackground(Graphics& g) override;
    void resized() override;
    void setSkinValues(const Skin& skin, bool top_level) override;
    void buttonClicked(Button* clicked_button) override;
    void sliderValueChanged(Slider* changed_slider) override;

    void setCollapsed(bool collapsed);
    void setNumEngines(int num_engines) { num_engines_ = num_engines; }
    int getNumEngines() const { return num_engines_; }
    void setCanAdd(bool can_add);
    void setCanRemove(bool can_remove) { remove_->setVisible(can_remove); }
    void addListener(Listener* listener) { listeners_.push_back(listener); }
    int group() const { return group_; }

  private:
    enum Label { kAmpLabel, kSmoothLabel, kLevelLabel, kOutLabel };
    String labelText(Label label);
    int labelWidth(Label label);
    Font getNameFont();
    int getNameRight();
    int getCountWidth();

    int group_;
    int num_engines_;
    std::vector<Listener*> listeners_;
    std::unique_ptr<OpenGlShapeButton> collapse_;
    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<OpenGlShapeButton> remove_;
    std::unique_ptr<TextSelector> amp_source_;
    std::unique_ptr<SynthSlider> level_;
    std::unique_ptr<SynthSlider> smoothing_;
    std::unique_ptr<TextSelector> output_;
    std::unique_ptr<OpenGlShapeButton> add_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupHeader)
};

class SoundEngineViewport : public Viewport {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void soundEnginesScrolled(int position) = 0;
    };

    void addListener(Listener* listener) { listeners_.push_back(listener); }
    void visibleAreaChanged(const Rectangle<int>& visible_area) override {
      for (Listener* listener : listeners_)
        listener->soundEnginesScrolled(visible_area.getY());

      Viewport::visibleAreaChanged(visible_area);
    }

  private:
    std::vector<Listener*> listeners_;
};

// The voice tab's scrolling list of oscillator groups and the sound engines in them, top to bottom in
// chain order. Engines and groups are added from the list; the scroll bar only shows while hovered.
class SoundEngineList : public SynthSection, public GroupHeader::Listener, public ScrollBar::Listener,
                        public SoundEngineViewport::Listener, public OscillatorSection::Listener,
                        public SampleSection::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void soundEnginesMoved() = 0;
        // Draws the modulation meters for knobs in the list, over its background.
        virtual void renderSoundEngineMeters(OpenGlWrapper& open_gl, bool animate, Viewport* viewport) = 0;
        // The engine's shown card (oscillator or sample) takes over the parameters both cards share.
        virtual void soundEngineCardShown(SynthSection* card) = 0;
    };

    SoundEngineList(Authentication* auth, const vital::output_map& mono_modulations,
                    const vital::output_map& poly_modulations);
    virtual ~SoundEngineList();

    void paintBackground(Graphics& g) override;
    void paintChildrenShadows(Graphics& g) override { }
    void resized() override;
    void setAllValues(vital::control_map& controls) override;
    void buttonClicked(Button* clicked_button) override;
    void mouseMove(const MouseEvent& e) override;
    void mouseExit(const MouseEvent& e) override;
    void mouseDown(const MouseEvent& e) override;
    bool keyPressed(const KeyPress& key) override;
    bool undoRemoval();
    bool redoRemoval();
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

    void initOpenGlComponents(OpenGlWrapper& open_gl) override;
    void renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) override;
    void destroyOpenGlComponents(OpenGlWrapper& open_gl) override;

    void groupCollapseToggled(int group) override;
    void addEngineClicked(int group, Component* source) override;
    void groupPowerToggled(int group) override { updateLayout(); }
    void groupAmpSourceChanged(int group) override { updateLayout(); }
    void removeGroupClicked(int group) override;
    void scrollBarMoved(ScrollBar* scroll_bar, double range_start) override;
    void soundEnginesScrolled(int position) override;

    void distortionTypeChanged(OscillatorSection* section, int type) override { }
    void oscillatorDestinationChanged(OscillatorSection* section, int destination) override { }
    void oscillatorSourceChanged(OscillatorSection* section, int source) override { updateLayout(); }
    void sampleDestinationChanged(SampleSection* section, int destination) override { }

    void addListener(Listener* listener) { listeners_.push_back(listener); }
    Viewport* getViewport() { return &viewport_; }
    OscillatorSection* getOscillatorSection(int engine) { return oscillators_[engine].get(); }
    int getOscillatorHeight();
    // Rebuilds the list from the synth's engines, groups and order.
    void updateLayout();

  private:
    // One entry per header, card and empty-group hint, top to bottom, for working out where a drag lands.
    struct Row {
      enum Type { kHeader, kCard, kEmptyGroup, kAddGroup };
      Type type;
      int group;
      int engine;
      int index;
      Rectangle<int> bounds;
    };

    enum DragType { kNoDrag, kEngineDrag, kGroupDrag };

    struct DropTarget {
      bool valid = false;
      int group = 0;
      // Engines: index within the group. Groups: index among the shown groups. Counts the dragged item.
      int position = 0;
    };

    DropTarget engineDropTarget(int y);
    DropTarget groupDropTarget(int y);
    Rectangle<int> groupBlockBounds(int group);
    bool canCopyDragged();
    int gapFreeY(int y) const;
    void placeDropSlot(int width);
    void updateDrag(const MouseEvent& e);
    void showUndoMenu(Component* source, Point<int> position);
    void endDrag();
    void redoBackgroundImage();
    void setScrollBarRange();
    void showRemoveButton(int engine);
    SynthSection* engineCard(int engine);
    // The card plus its open drawer.
    Rectangle<int> engineBounds(int engine);
    bool isSampleEngine(int engine);
    int engineGroup(int engine);

    std::vector<Listener*> listeners_;
    SoundEngineViewport viewport_;
    std::unique_ptr<SoundEngineContainer> container_;
    OpenGlImage background_;
    CriticalSection open_gl_critical_section_;
    std::unique_ptr<OpenGlScrollBar> scroll_bar_;

    std::unique_ptr<GroupHeader> headers_[vital::kNumOscillatorGroups];
    std::unique_ptr<OscillatorSection> oscillators_[vital::kNumOscillators];
    std::unique_ptr<SampleSection> samples_[vital::kNumOscillators];
    std::unique_ptr<OpenGlShapeButton> remove_buttons_[vital::kNumOscillators];
    // Each wavetable / noise card's ADVANCED drawer (its options and unison), opened under the card.
    std::unique_ptr<OscillatorAdvancedSection> advanced_[vital::kNumOscillators];
    std::unique_ptr<OpenGlShapeButton> drawer_buttons_[vital::kNumOscillators];
    bool drawer_open_[vital::kNumOscillators];
    std::unique_ptr<OpenGlToggleButton> add_group_;
    bool collapsed_[vital::kNumOscillatorGroups];
    std::vector<Rectangle<int>> empty_group_hints_;
    int hovered_engine_;

    std::vector<Row> rows_;
    std::vector<int> shown_groups_;
    DragType drag_type_;
    bool dragging_;
    int drag_engine_;
    int drag_group_;
    int drag_grab_offset_;
    Point<int> drag_start_;
    Component* drag_source_;
    Rectangle<int> drag_bounds_;
    bool drag_copy_;
    DropTarget drop_target_;
    Rectangle<int> slot_;
    int slot_shift_;
    std::unique_ptr<OpenGlQuad> drag_ghost_;
    std::unique_ptr<OpenGlQuad> dragged_dim_;
    std::unique_ptr<PlainTextComponent> copy_label_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundEngineList)
};
