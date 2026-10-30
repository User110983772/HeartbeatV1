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

#include "audio_file_drop_source.h"
#include "common.h"
#include "open_gl_line_renderer.h"
#include "synth_types.h"
#include "sample_source.h"
#include "synth_module.h"

class SampleViewer : public OpenGlLineRenderer, public AudioFileDropSource {
  public:
    static constexpr float kResolution = 256;
    static constexpr float kBoostDecay = 0.9f;
    static constexpr float kSpeedDecayMult = 5.0f;

    class Listener {
      public:
        virtual ~Listener() { }
        virtual void sampleLoaded(const File& file) = 0;
        // A marker dragged in the display; value is a fraction of the sample.
        virtual void sampleMarkerMoved(int marker, float value) { }
        virtual void sampleViewChanged() { }
    };

    // The draggable markers: where notes start, the loop's ends, and where notes end.
    enum Marker { kNoMarker = -1, kStartMarker, kLoopStartMarker, kLoopEndMarker, kEndMarker, kNumMarkers };
    static constexpr float kMaxZoom = 64.0f;
    // The overview strip along the bottom, showing the whole sample and the part in view.
    static constexpr int kOverviewHeight = 8;

    SampleViewer(int engine);
    virtual ~SampleViewer();

    void init(OpenGlWrapper& open_gl) override {
      OpenGlLineRenderer::init(open_gl);
      bottom_.init(open_gl);
      dragging_overlay_.init(open_gl);
      loop_quad_.init(open_gl);
      fade_quads_.init(open_gl);
      loop_lines_.init(open_gl);
      start_line_.init(open_gl);
      end_line_.init(open_gl);
      outside_quads_.init(open_gl);
      overview_back_.init(open_gl);
      overview_window_.init(open_gl);
    }
    void render(OpenGlWrapper& open_gl, bool animate) override;
    void destroy(OpenGlWrapper& open_gl) override {
      OpenGlLineRenderer::destroy(open_gl);
      bottom_.destroy(open_gl);
      dragging_overlay_.destroy(open_gl);
      loop_quad_.destroy(open_gl);
      fade_quads_.destroy(open_gl);
      loop_lines_.destroy(open_gl);
      start_line_.destroy(open_gl);
      end_line_.destroy(open_gl);
      outside_quads_.destroy(open_gl);
      overview_back_.destroy(open_gl);
      overview_window_.destroy(open_gl);
    }

    void resized() override;
    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }

    void audioFileLoaded(const File& file) override;
    void repaintAudio();
    void setLinePositions();

    void fileDragEnter(const StringArray& files, int x, int y) override {
      dragging_audio_file_ = true;
    }

    void fileDragExit(const StringArray& files) override {
      dragging_audio_file_ = false;
    }

    std::string getName();

    void addListener(Listener* listener) {
      listeners_.push_back(listener);
    }

    void setSample(vital::Sample* sample) { sample_ = sample; setLinePositions(); }

    // Where the markers are drawn, as fractions of the sample; crossfade is a fraction of half the loop.
    void setMarkers(float start, float end, float loop_start, float loop_end, float crossfade, bool loop, bool reverse);
    // The engine's modulated START, END, loop and crossfade, so the markers move with any modulation applied.
    void setModulationOutputs(const vital::output_map& mono_modulations, const vital::output_map& poly_modulations,
                              const std::string& prefix);
    // Zoom (1 = the whole sample, up to kMaxZoom) around a point given as a fraction of the sample.
    void zoomAround(float fraction, float factor);
    void resetZoom();
    void zoomAroundCenter(float factor) { zoomAround(view_ + span() / 2.0f, factor); }
    float zoom() const { return zoom_; }
    void setSnapToZeroCrossings(bool snap) { snap_to_zero_ = snap; }
    void mouseDoubleClick(const MouseEvent& e) override;
    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseMove(const MouseEvent& e) override;
    void mouseExit(const MouseEvent& e) override;

  private:
    std::vector<Listener*> listeners_;

    const vital::StatusOutput* sample_phase_output_;
    std::string phase_status_name_;
    vital::poly_float last_phase_;
    vital::poly_float last_voice_;
    vital::Sample* sample_;
  
    OpenGlLineRenderer bottom_;
    OpenGlQuad dragging_overlay_;
    OpenGlMultiQuad loop_quad_;
    OpenGlMultiQuad fade_quads_;
    OpenGlMultiQuad loop_lines_;
    OpenGlQuad start_line_;
    OpenGlQuad end_line_;
    OpenGlMultiQuad outside_quads_;
    OpenGlQuad overview_back_;
    OpenGlQuad overview_window_;

    // The part of the sample in view starts at view_ and covers 1 / zoom_ of it.
    float zoom_ = 1.0f;
    float view_ = 0.0f;
    bool snap_to_zero_ = false;
    bool scrolling_ = false;
    float span() const { return 1.0f / zoom_; }
    float toView(float fraction) const { return (fraction - view_) * zoom_; }
    float fractionAt(float x) const { return view_ + x / std::max(1, getWidth()) * span(); }
    int waveHeight() const { return getHeight(); }
    void clampView();
    void viewChanged();
    float nearestZeroCrossing(float fraction) const;

    void setMarkerQuads();
    int markerAt(int x);
    float markerValue(int marker) const;
    void showMarkerPopup(int marker);

    // Marker order for the modulated values: START, END, LOOP START, LOOP END, XFADE.
    enum { kShownStart, kShownEnd, kShownLoopStart, kShownLoopEnd, kShownCrossfade, kNumShown };
    std::pair<vital::Output*, vital::Output*> modulation_outputs_[kNumShown] = {};
    float shown_[kNumShown] = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
    void updateShownMarkers();

    float start_ = 0.0f;
    float end_ = 1.0f;
    float loop_start_ = 0.0f;
    float loop_end_ = 1.0f;
    float crossfade_ = 0.0f;
    bool loop_ = false;
    bool reverse_ = false;
    int dragging_marker_ = kNoMarker;

    bool dragging_audio_file_;
    bool animate_;
    bool active_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleViewer)
};

