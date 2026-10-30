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

#include "sample_viewer.h"

#include "skin.h"
#include "synth_gui_interface.h"
#include "synth_section.h"

// Passed by reference (std::min), so C++14 needs the definition.
constexpr float SampleViewer::kMaxZoom;

SampleViewer::SampleViewer(int engine) : OpenGlLineRenderer(kResolution), bottom_(kResolution),
                                         dragging_overlay_(Shaders::kColorFragment),
                                         loop_quad_(1, Shaders::kColorFragment), fade_quads_(2, Shaders::kColorFragment),
                                         loop_lines_(2, Shaders::kColorFragment), start_line_(Shaders::kColorFragment),
                                         end_line_(Shaders::kColorFragment), outside_quads_(2, Shaders::kColorFragment),
                                         overview_back_(Shaders::kColorFragment),
                                         overview_window_(Shaders::kColorFragment) {
  phase_status_name_ = "osc_" + std::to_string(engine + 1) + "_sample_phase";
  addAndMakeVisible(bottom_);
  animate_ = false;
  active_ = true;
  sample_phase_output_ = nullptr;
  last_phase_ = 0.0f;
  sample_ = nullptr;
  dragging_audio_file_ = false;
  // No rounded corners: the display sits inside the card's panel, above its control bar.

  dragging_overlay_.setTargetComponent(this);
  bottom_.setInterceptsMouseClicks(false, false);
  for (OpenGlMultiQuad* quads : { &loop_quad_, &fade_quads_, &loop_lines_, (OpenGlMultiQuad*)&start_line_,
                                   (OpenGlMultiQuad*)&end_line_, &outside_quads_, (OpenGlMultiQuad*)&overview_back_,
                                   (OpenGlMultiQuad*)&overview_window_ })
    quads->setTargetComponent(this);
                                 
  setFill(true);
  bottom_.setFill(true);
  setLineWidth(2.0f);
  bottom_.setLineWidth(2.0f);
}

SampleViewer::~SampleViewer() { }

void SampleViewer::audioFileLoaded(const File& file) {
  for (Listener* listener : listeners_)
    listener->sampleLoaded(file);
  
  setLinePositions();
}

void SampleViewer::repaintAudio() {
  dragging_audio_file_ = false;
  setLinePositions();
}

void SampleViewer::setLinePositions() {
  if (sample_ == nullptr)
    return;

  // The waveform of the part in view, above the overview strip; zoomed in past one sample per point it shows the
  // samples themselves.
  double sample_length = sample_->originalLength();
  const vital::mono_float* buffer = sample_->buffer();
  float center = waveHeight() / 2.0f;
  double first = view_ * sample_length;
  double count = span() * sample_length;
  for (int i = 0; i < kResolution; ++i) {
    int start_index = std::min<int>(first + count * i / kResolution, sample_length - 1);
    int end_index = std::min<int>(std::max<double>(start_index + 1, first + count * (i + 1) / kResolution),
                                  sample_length);
    float max = buffer[start_index];
    for (int j = start_index + 1; j < end_index; ++j)
      max = std::max(max, buffer[j]);
    setYAt(i, center - max * center);
    bottom_.setYAt(i, center + max * center);
  }
}

std::string SampleViewer::getName() {
  if (sample_)
    return sample_->getName();
  return "";
}

void SampleViewer::resized() {
  bottom_.setBounds(getLocalBounds());
  dragging_overlay_.setColor(findColour(Skin::kOverlayScreen, true));

  for (int i = 0; i < kResolution; ++i) {
    float t = i / (kResolution - 1.0f);
    setXAt(i, getWidth() * t);
    bottom_.setXAt(i, getWidth() * t);
  }

  if (sample_phase_output_ == nullptr) {
    SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
    if (parent)
      sample_phase_output_ = parent->getSynth()->getStatusOutput(phase_status_name_);
  }

  OpenGlLineRenderer::resized();
  setLinePositions();
  setMarkerQuads();
}

void SampleViewer::setMarkers(float start, float end, float loop_start, float loop_end, float crossfade, bool loop,
                              bool reverse) {
  start_ = start;
  end_ = std::max(end, start);
  // A loop end before the loop start loops between the two, as the engine plays it.
  loop_start_ = std::min(loop_start, loop_end);
  loop_end_ = std::max(loop_start, loop_end);
  crossfade_ = crossfade;
  loop_ = loop;
  reverse_ = reverse;
  updateShownMarkers();
  setMarkerQuads();
}

void SampleViewer::setModulationOutputs(const vital::output_map& mono_modulations,
                                        const vital::output_map& poly_modulations, const std::string& prefix) {
  const char* names[kNumShown] = { "sample_start", "sample_end", "sample_loop_start", "sample_loop_end",
                                   "sample_crossfade" };
  for (int i = 0; i < kNumShown; ++i) {
    std::string name = prefix + names[i];
    if (mono_modulations.count(name) && poly_modulations.count(name))
      modulation_outputs_[i] = { mono_modulations.at(name), poly_modulations.at(name) };
  }
}

void SampleViewer::updateShownMarkers() {
  // Each marker where the engine has it now: the knob's value plus any modulation (the newest voice's while notes
  // play), kept in the same order as the engine keeps them.
  float base[kNumShown] = { start_, end_, loop_start_, loop_end_, crossfade_ };
  for (int i = 0; i < kNumShown; ++i) {
    std::pair<vital::Output*, vital::Output*> outputs = modulation_outputs_[i];
    float value = base[i];
    if (outputs.first && outputs.first->owner->enabled() && animate_) {
      value = outputs.first->trigger_value[0];
      if (num_voices_readout_ && num_voices_readout_->value()[0] > 0.0f)
        value += outputs.second->trigger_value[0];
    }
    shown_[i] = std::max(0.0f, std::min(1.0f, value));
  }
  // Placed exactly as the sampler places its first voice.
  vital::SampleSource::Placement placement = vital::SampleSource::placeVoice(
      shown_[kShownStart], shown_[kShownEnd], shown_[kShownLoopStart], shown_[kShownLoopEnd],
      shown_[kShownCrossfade], 0.0, 0.0, 0.0, loop_, reverse_, 0.0, 0.0);
  shown_[kShownStart] = placement.start;
  shown_[kShownEnd] = placement.end;
  shown_[kShownLoopStart] = placement.loop_start;
  shown_[kShownLoopEnd] = placement.loop_end;
}

void SampleViewer::clampView() {
  view_ = std::max(0.0f, std::min(view_, 1.0f - span()));
}

void SampleViewer::viewChanged() {
  clampView();
  setLinePositions();
  setMarkerQuads();
  for (Listener* listener : listeners_)
    listener->sampleViewChanged();
}

void SampleViewer::zoomAround(float fraction, float factor) {
  float zoom = std::max(1.0f, std::min(kMaxZoom, zoom_ * factor));
  view_ = fraction - (fraction - view_) * (zoom_ / zoom);
  zoom_ = zoom;
  viewChanged();
}

void SampleViewer::resetZoom() {
  zoom_ = 1.0f;
  view_ = 0.0f;
  viewChanged();
}

float SampleViewer::nearestZeroCrossing(float fraction) const {
  // The closest sign change in the left channel, within a few milliseconds.
  if (sample_ == nullptr)
    return fraction;
  int length = sample_->originalLength();
  const vital::mono_float* buffer = sample_->buffer();
  int center = std::max(1, std::min(length - 1, static_cast<int>(fraction * length)));
  int reach = std::min(length, sample_->sampleRate() / 200);
  for (int distance = 0; distance < reach; ++distance) {
    for (int index : { center - distance, center + distance }) {
      if (index > 0 && index < length && (buffer[index - 1] < 0.0f) != (buffer[index] < 0.0f))
        return index / static_cast<float>(length);
    }
  }
  return fraction;
}

void SampleViewer::setMarkerQuads() {
  if (getWidth() <= 0 || getHeight() <= kOverviewHeight)
    return;

  // Quads are placed in GL units across the display: -1 at the left edge, 1 at the right; the waveform area is
  // above the overview strip.
  float pixel = 2.0f / getWidth();
  auto x = [this](float t) { return 2.0f * toView(t) - 1.0f; };
  float line_width = 2.0f * pixel;
  float wave_bottom = 1.0f - 2.0f * waveHeight() / getHeight();
  float wave_height = 1.0f - wave_bottom;
  float start_ = shown_[kShownStart];
  float end_ = shown_[kShownEnd];
  float loop_start_ = shown_[kShownLoopStart];
  float loop_end_ = shown_[kShownLoopEnd];
  float crossfade_ = shown_[kShownCrossfade];

  start_line_.setQuad(0, x(start_) - pixel, wave_bottom, line_width, wave_height);
  end_line_.setQuad(0, x(end_) - pixel, wave_bottom, line_width, wave_height);
  // Outside START..END never plays, so it's shaded.
  outside_quads_.setQuad(0, -1.0f, wave_bottom, std::max(0.0f, x(start_) + 1.0f), wave_height);
  outside_quads_.setQuad(1, x(end_), wave_bottom, std::max(0.0f, 1.0f - x(end_)), wave_height);

  loop_quad_.setNumQuads(loop_ ? 1 : 0);
  loop_lines_.setNumQuads(loop_ ? 2 : 0);
  if (loop_) {
    loop_quad_.setQuad(0, x(loop_start_), wave_bottom, x(loop_end_) - x(loop_start_), wave_height);
    loop_lines_.setQuad(0, x(loop_start_) - pixel, wave_bottom, line_width, wave_height);
    loop_lines_.setQuad(1, x(loop_end_) - pixel, wave_bottom, line_width, wave_height);
  }

  // The crossfade: the stretch before the loop end that fades into the stretch after the loop start.
  float fade = crossfade_ * 0.5f * (loop_end_ - loop_start_);
  fade_quads_.setNumQuads(loop_ && fade > 0.0f ? 2 : 0);
  if (loop_ && fade > 0.0f) {
    fade_quads_.setQuad(0, x(loop_end_ - fade), wave_bottom, x(loop_end_) - x(loop_end_ - fade), wave_height);
    fade_quads_.setQuad(1, x(loop_start_), wave_bottom, x(loop_start_ + fade) - x(loop_start_), wave_height);
  }

  // Zoomed in, a thin strip along the bottom shows which part of the sample is in view.
  float strip_height = 2.0f * kOverviewHeight / getHeight();
  auto o = [](float t) { return 2.0f * t - 1.0f; };
  bool zoomed = zoom_ > 1.0f;
  overview_back_.setQuad(0, -1.0f, -1.0f, zoomed ? 2.0f : 0.0f, strip_height);
  overview_window_.setQuad(0, o(view_), -1.0f, zoomed ? 2.0f * span() : 0.0f, strip_height);
}

float SampleViewer::markerValue(int marker) const {
  if (marker == kStartMarker)
    return start_;
  if (marker == kEndMarker)
    return end_;
  if (marker == kLoopStartMarker)
    return loop_start_;
  return loop_end_;
}

int SampleViewer::markerAt(int x) {
  // The nearest marker within a few pixels, in view; the loop's markers only while looping.
  static constexpr int kGrabDistance = 6;
  int best = kNoMarker;
  float best_distance = kGrabDistance;
  for (int marker = kStartMarker; marker < kNumMarkers; ++marker) {
    if ((marker == kLoopStartMarker || marker == kLoopEndMarker) && !loop_)
      continue;
    float distance = std::abs(toView(markerValue(marker)) * getWidth() - x);
    if (distance <= best_distance) {
      best_distance = distance;
      best = marker;
    }
  }
  return best;
}

void SampleViewer::showMarkerPopup(int marker) {
  static const char* kNames[kNumMarkers] = { "Start", "Loop Start", "Loop End", "End" };
  SynthSection* section = findParentComponentOfClass<SynthSection>();
  if (section == nullptr)
    return;
  // More precision the further in the view is zoomed.
  int decimals = zoom_ >= 16.0f ? 3 : zoom_ >= 4.0f ? 2 : 1;
  float value = markerValue(marker);
  String text = String(kNames[marker]) + ": " + String(value * 100.0f, decimals) + "%";
  if (sample_ && sample_->sampleRate() > 0) {
    float seconds = value * sample_->originalLength() / sample_->sampleRate();
    text += "  (" + String(seconds * 1000.0f, decimals) + " ms)";
  }
  section->showPopupDisplay(this, text.toStdString(), BubbleComponent::above, true);
}

void SampleViewer::mouseDown(const MouseEvent& e) {
  scrolling_ = zoom_ > 1.0f && e.y >= getHeight() - kOverviewHeight;
  if (scrolling_) {
    view_ = e.x / std::max(1.0f, (float)getWidth()) - span() / 2.0f;
    viewChanged();
    return;
  }
  dragging_marker_ = markerAt(e.x);
  if (dragging_marker_ != kNoMarker)
    showMarkerPopup(dragging_marker_);
}

void SampleViewer::mouseDrag(const MouseEvent& e) {
  if (scrolling_) {
    view_ = e.x / std::max(1.0f, (float)getWidth()) - span() / 2.0f;
    viewChanged();
    return;
  }
  if (dragging_marker_ == kNoMarker)
    return;

  float value = std::max(0.0f, std::min(1.0f, fractionAt(e.x)));
  if (snap_to_zero_)
    value = nearestZeroCrossing(value);
  // START stays before END, and the loop inside them with its end after its start.
  if (dragging_marker_ == kStartMarker)
    value = std::min(value, end_);
  else if (dragging_marker_ == kEndMarker)
    value = std::max(value, start_);
  else if (dragging_marker_ == kLoopStartMarker)
    value = std::max(start_, std::min(value, loop_end_));
  else if (dragging_marker_ == kLoopEndMarker)
    value = std::min(end_, std::max(value, loop_start_));

  for (Listener* listener : listeners_)
    listener->sampleMarkerMoved(dragging_marker_, value);
  showMarkerPopup(dragging_marker_);
}

void SampleViewer::mouseUp(const MouseEvent& e) {
  dragging_marker_ = kNoMarker;
  scrolling_ = false;
}

void SampleViewer::mouseMove(const MouseEvent& e) {
  int marker = e.y < waveHeight() ? markerAt(e.x) : kNoMarker;
  setMouseCursor(marker == kNoMarker ? MouseCursor::NormalCursor : MouseCursor::LeftRightResizeCursor);
  SynthSection* section = findParentComponentOfClass<SynthSection>();
  if (marker != kNoMarker)
    showMarkerPopup(marker);
  else if (section)
    section->hidePopupDisplay(true);
}

void SampleViewer::mouseExit(const MouseEvent& e) {
  SynthSection* section = findParentComponentOfClass<SynthSection>();
  if (section)
    section->hidePopupDisplay(true);
}

void SampleViewer::mouseDoubleClick(const MouseEvent& e) {
  resetZoom();
}

void SampleViewer::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) {
  // The wheel zooms around the pointer; with shift (or a sideways scroll) it scrolls.
  float delta = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;
  if (e.mods.isShiftDown() || wheel.deltaX != 0.0f) {
    view_ -= delta * span() * 0.5f;
    viewChanged();
    return;
  }
  zoomAround(fractionAt(e.x), delta > 0.0f ? 1.25f : 0.8f);
}

void SampleViewer::render(OpenGlWrapper& open_gl, bool animate) {
  animate_ = animate;

  float boost_amount = findValue(Skin::kWidgetLineBoost);
  float fill_boost_amount = findValue(Skin::kWidgetFillBoost);
  setBoostAmount(boost_amount);
  bottom_.setBoostAmount(boost_amount);
  setFillBoostAmount(fill_boost_amount);
  bottom_.setFillBoostAmount(fill_boost_amount);
  
  if (sample_ == nullptr)
    return;

  int sample_length = sample_->originalLength();
  if (sample_phase_output_ == nullptr || sample_length == 0)
    return;

  vital::poly_float encoded_phase = sample_phase_output_->value();
  std::pair<vital::poly_float, vital::poly_float> decoded = vital::utils::decodePhaseAndVoice(encoded_phase);
  vital::poly_float phase = decoded.first;
  vital::poly_float voice = decoded.second;

  vital::poly_mask switch_mask = vital::poly_float::notEqual(voice, last_voice_);
  vital::poly_float phase_reset = vital::utils::max(0.0f, phase);
  last_phase_ = vital::utils::maskLoad(last_phase_, phase_reset, switch_mask);

  if (!sample_phase_output_->isClearValue(phase) && vital::poly_float::notEqual(phase, 0.0f).anyMask() != 0) {
    vital::poly_float phase_delta = vital::poly_float::abs(phase - last_phase_);
    vital::poly_float decay = vital::poly_float(1.0f) - phase_delta * kSpeedDecayMult;
    decay = vital::utils::clamp(decay, kBoostDecay, 1.0f);
    decayBoosts(decay);
    bottom_.decayBoosts(decay);

    if (animate_) {
      // The playhead's glow, where it is in the part in view.
      vital::poly_float from = vital::utils::clamp((last_phase_ - view_) * zoom_, 0.0f, 1.0f);
      vital::poly_float to = vital::utils::clamp((phase - view_) * zoom_, 0.0f, 1.0f);
      boostRange(from, to, 0, decay);
      bottom_.boostRange(from, to, 0, decay);
    }
  }
  else {
    decayBoosts(kBoostDecay);
    bottom_.decayBoosts(kBoostDecay);
  }

  last_phase_ = phase;
  last_voice_ = voice;

  float fill_fade = 0.0f;
  if (parent_)
    fill_fade = parent_->findValue(Skin::kWidgetFillFade);

  Colour line, fill;
  if (isActive()) {
    line = findColour(Skin::kWidgetPrimary2, true);
    fill = findColour(Skin::kWidgetSecondary2, true);
  }
  else {
    line = findColour(Skin::kWidgetPrimaryDisabled, true);
    fill = findColour(Skin::kWidgetSecondaryDisabled, true);
  }

  setColor(line);
  bottom_.setColor(line);
  setFillColors(fill.withMultipliedAlpha(1.0f - fill_fade), fill);
  bottom_.setFillColors(fill.withMultipliedAlpha(1.0f - fill_fade), fill);
  
  drawLines(open_gl, false);
  bottom_.drawLines(open_gl, false);

  if (isActive()) {
    line = findColour(Skin::kWidgetPrimary1, true);
    fill = findColour(Skin::kWidgetSecondary1, true);
  }
  else {
    line = findColour(Skin::kWidgetPrimaryDisabled, true);
    fill = findColour(Skin::kWidgetSecondaryDisabled, true);
  }
  
  setColor(line);
  bottom_.setColor(line);
  setFillColors(fill.withMultipliedAlpha(1.0f - fill_fade), fill);
  bottom_.setFillColors(fill.withMultipliedAlpha(1.0f - fill_fade), fill);
  
  drawLines(open_gl, anyBoostValue());
  bottom_.drawLines(open_gl, anyBoostValue());

  // The loop region and crossfade tint the waveform; the marker lines go on top, where modulation has them.
  float previous[kNumShown];
  std::copy(std::begin(shown_), std::end(shown_), previous);
  updateShownMarkers();
  if (!std::equal(std::begin(shown_), std::end(shown_), previous))
    setMarkerQuads();

  Colour accent = findColour(Skin::kWidgetPrimary1, true);
  if (!isActive())
    accent = findColour(Skin::kWidgetPrimaryDisabled, true);
  loop_quad_.setColor(accent.withMultipliedAlpha(0.12f));
  fade_quads_.setColor(accent.withMultipliedAlpha(0.3f));
  loop_lines_.setColor(accent);
  start_line_.setColor(findColour(Skin::kBodyText, true));
  outside_quads_.setColor(Colours::black.withAlpha(0.35f));
  end_line_.setColor(findColour(Skin::kBodyText, true));
  overview_back_.setColor(findColour(Skin::kBody, true));
  overview_window_.setColor(findColour(Skin::kBodyText, true).withAlpha(0.3f));
  outside_quads_.render(open_gl, animate);
  loop_quad_.render(open_gl, animate);
  fade_quads_.render(open_gl, animate);
  loop_lines_.render(open_gl, animate);
  start_line_.render(open_gl, animate);
  end_line_.render(open_gl, animate);
  overview_back_.render(open_gl, animate);
  overview_window_.render(open_gl, animate);

  if (dragging_audio_file_)
    dragging_overlay_.render(open_gl, animate);
}
