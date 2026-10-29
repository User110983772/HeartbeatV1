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
#include "synth_slider.h"
#include "wavetable_component_list.h"
#include "wavetable_creator.h"
#include "wavetable_playhead.h"

class DraggableFrame : public Component {
  public:
    DraggableFrame(bool full_frame = false) {
      setInterceptsMouseClicks(false, true);
      selected_ = false;
      full_frame_ = full_frame;
    }

    bool isInside(int x, int y);
    bool fullFrame() const { return full_frame_; }

    force_inline void select(bool selected) { selected_ = selected; }
    force_inline bool selected() const { return selected_; }

  protected:
    bool selected_;
    bool full_frame_;
};

class WavetableOrganizer : public SynthSection,
                           public WavetablePlayhead::Listener,
                           public WavetableComponentList::Listener {
  public:
    static constexpr float kHandleHeightPercent = 1.0f / 8.0f;
    static constexpr int kDrawSkip = 4;
    static constexpr int kDrawSkipLarge = 32;
    static constexpr int kMaxKeyframes = 2048;

    enum OrganizerMenu {
      kCancel = 0,
      kCreate,
      kRemove,
      kGoToFrame
    };

    enum MouseMode {
      kWaiting,
      kSelecting,
      kDragging,
      kRightClick,
      kNumMouseModes
    };

    class Listener {
      public:
        virtual ~Listener() { }

        virtual void positionsUpdated() { }
        virtual void frameSelected(WavetableKeyframe* keyframe) = 0;
        virtual void frameDragged(WavetableKeyframe* keyframe, int position) = 0;
        virtual void wheelMoved(const MouseEvent& e, const MouseWheelDetails& wheel) { }
        // Type a frame to jump to; `point` is in the organizer's coordinates.
        virtual void frameEntryRequestedAt(Component* source, Point<int> point, int initial_frame) { }
    };

    WavetableOrganizer(WavetableCreator* wavetable_creator, int max_frames);
    virtual ~WavetableOrganizer();

    void paintBackground(Graphics& g) override;
    void resized() override;

    void mouseDown(const MouseEvent& event) override;
    void mouseDrag(const MouseEvent& event) override;
    void mouseUp(const MouseEvent& event) override;
    void mouseDoubleClick(const MouseEvent& event) override;
    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override {
      for (Listener* listener : listeners_)
        listener->wheelMoved(e, wheel);
    }

    void playheadMoved(int position) override;
    void mouseMove(const MouseEvent& e) override;
    void mouseExit(const MouseEvent& e) override;
    void requestFrameEntry(Point<int> point, int initial_frame);
    Point<int> menuPosition() const { return menu_created_position_; }
    int menuFrame() const { return menu_frame_; }
    int getPlayheadX() { return handleWidth() / 2 + playhead_position_ * frame_width_; }
    // The keyframe (in a row with keyframes) under the mouse, or nullptr.
    WavetableKeyframe* getKeyframeAt(const MouseEvent& e);

    void componentAdded(WavetableComponent* component) override;
    void componentRemoved(WavetableComponent* component) override;
    void componentsReordered() override { }
    void componentsChanged() override { recreateVisibleFrames(); }
    void componentsScrolled(int offset) override;

    void addListener(Listener* listener) { listeners_.push_back(listener); }
    void removeListener(Listener* listener) {
      listeners_.erase(std::find(listeners_.begin(), listeners_.end(), listener));
    }
    int handleWidth() const;
    void deleteSelectedKeyframes();
    void createKeyframeAtMenu();
    void selectDefaultFrame();
    void clear() {
      clearVisibleFrames();
      currently_selected_.clear();
    }
    void init() {
      recreateVisibleFrames();
    }
    bool hasSelectedFrames() const { return !currently_selected_.empty(); }
    void selectFrame(WavetableKeyframe* keyframe);

  private:
    void clearVisibleFrames();
    void recreateVisibleFrames();
    void repositionVisibleFrames();
    WavetableComponent* getComponentAtRow(int row);
    WavetableKeyframe* getFrameAtMouseEvent(const MouseEvent& event);
    void deselect();
    void deleteKeyframe(WavetableKeyframe* keyframe);
    void createKeyframeAtPosition(Point<int> position);
    void selectFrames(std::vector<WavetableKeyframe*> keyframes);
    void positionSelectionBox(const MouseEvent& event);
    void setRowQuads();
    void setFrameQuads();

    int getRowFromY(int y);
    int getPositionFromX(int x);
    int getUnclampedPositionFromX(int x);
    bool isSelected(WavetableKeyframe* keyframe);

    WavetableCreator* wavetable_creator_;
    std::vector<Listener*> listeners_;
    std::map<WavetableKeyframe*, std::unique_ptr<DraggableFrame>> frame_lookup_;
    OpenGlMultiQuad unselected_frame_quads_;
    OpenGlMultiQuad selected_frame_quads_;
    OpenGlMultiQuad active_rows_;
    OpenGlQuad selection_quad_;
    OpenGlQuad playhead_quad_;
    Component hover_anchor_;
    int menu_frame_ = 0;

    MouseMode mouse_mode_;
    Point<int> mouse_down_position_;
    Point<int> menu_created_position_;
    std::vector<WavetableKeyframe*> currently_selected_;
    WavetableKeyframe* currently_dragged_;
    int dragged_start_x_;

    int draw_vertical_offset_;
    int playhead_position_;
    int max_frames_;
    float frame_width_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableOrganizer)
};

