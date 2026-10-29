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

#include "noise_viewer.h"

#include "skin.h"
#include "synth_gui_interface.h"
#include "synth_module.h"

namespace {
  // Points 0..kResolution trace the lower half left to right, the rest mirror it back along the top,
  // the same closed shape as Wavetable3d's spectrum view.
  constexpr int kNumPoints = 2 * (NoiseViewer::kResolution + 1);

  std::pair<vital::Output*, vital::Output*> getNoiseOutputs(const vital::output_map& mono_modulations,
                                                            const vital::output_map& poly_modulations,
                                                            const std::string& name) {
    return { mono_modulations.at(name), poly_modulations.at(name) };
  }

  bool lanesEqual(vital::poly_float a, vital::poly_float b) {
    return !vital::poly_float::notEqual(a, b).anyMask();
  }
} // namespace

bool NoiseViewer::State::operator==(const State& other) const {
  return lanesEqual(color, other.color) && lanesEqual(focus, other.focus) && lanesEqual(midi, other.midi) &&
         lanesEqual(voices, other.voices) && lanesEqual(cents, other.cents) && lanesEqual(power, other.power) &&
         lanesEqual(blend, other.blend) && lanesEqual(color_spread, other.color_spread) &&
         lanesEqual(focus_spread, other.focus_spread) && stack_style == other.stack_style;
}

NoiseViewer::NoiseViewer(int index, const vital::output_map& mono_modulations,
                         const vital::output_map& poly_modulations) :
    OpenGlComponent("noise"), left_line_renderer_(kNumPoints), right_line_renderer_(kNumPoints),
    prefix_("osc_" + std::to_string(index + 1)),
    detune_range_control_(nullptr), blend_control_(nullptr), stack_style_control_(nullptr),
    color_spread_control_(nullptr), focus_spread_control_(nullptr),
    color_slider_(nullptr), focus_slider_(nullptr), transpose_slider_(nullptr), tune_slider_(nullptr),
    voices_slider_(nullptr), detune_slider_(nullptr), detune_power_slider_(nullptr),
    last_state_(), dirty_(true), animate_(false), active_(true) {
  color_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_noise_color");
  focus_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_noise_focus");
  transpose_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_transpose");
  tune_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_tune");
  voices_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_unison_voices");
  detune_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_unison_detune");
  detune_power_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_detune_power");
  detune_range_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_detune_range");
  blend_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_unison_blend");
  color_spread_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_spectral_morph_spread");
  focus_spread_outputs_ = getNoiseOutputs(mono_modulations, poly_modulations, prefix_ + "_noise_focus_spread");

  float octaves = std::log2(kMaxFrequency / kMinFrequency);
  for (int i = 0; i <= kResolution; ++i)
    evaluate_frequencies_[i] = kMinFrequency * std::exp2(octaves * i / kResolution);

  addAndMakeVisible(left_line_renderer_);
  addAndMakeVisible(right_line_renderer_);
  left_line_renderer_.setInterceptsMouseClicks(false, false);
  right_line_renderer_.setInterceptsMouseClicks(false, false);
  left_line_renderer_.setFill(true);
  right_line_renderer_.setFill(true);
  left_line_renderer_.setLineWidth(2.5f);
  right_line_renderer_.setLineWidth(2.5f);
}

NoiseViewer::~NoiseViewer() = default;

void NoiseViewer::init(OpenGlWrapper& open_gl) {
  left_line_renderer_.init(open_gl);
  right_line_renderer_.init(open_gl);
}

void NoiseViewer::destroy(OpenGlWrapper& open_gl) {
  left_line_renderer_.destroy(open_gl);
  right_line_renderer_.destroy(open_gl);
}

void NoiseViewer::paintBackground(Graphics& g) {
  OpenGlComponent::paintBackground(g);
  left_line_renderer_.setParent(parent_);
  right_line_renderer_.setParent(parent_);
}

void NoiseViewer::resized() {
  left_line_renderer_.setBounds(getLocalBounds());
  right_line_renderer_.setBounds(getLocalBounds());
  OpenGlComponent::resized();

  float width = getWidth();
  for (int i = 0; i <= kResolution; ++i) {
    float x = width * i / kResolution;
    for (OpenGlLineRenderer* renderer : { &left_line_renderer_, &right_line_renderer_ }) {
      renderer->setXAt(i, x);
      renderer->setXAt(kNumPoints - 1 - i, x);
    }
  }
  dirty_ = true;
}

vital::poly_float NoiseViewer::getOutputsTotal(std::pair<vital::Output*, vital::Output*> outputs,
                                               vital::poly_float default_value) {
  if (!outputs.first->owner->enabled() || !animate_)
    return default_value;
  if (num_voices_readout_ == nullptr || num_voices_readout_->value()[0] <= 0.0f)
    return outputs.first->trigger_value;

  return outputs.first->trigger_value + outputs.second->trigger_value;
}

// Detune range, blend, stack style and the spreads are edited in another section, so read them from the synth.
void NoiseViewer::loadControls() {
  if (stack_style_control_)
    return;

  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;

  vital::control_map& controls = parent->getSynth()->getControls();
  detune_range_control_ = controls[prefix_ + "_detune_range"];
  blend_control_ = controls[prefix_ + "_unison_blend"];
  stack_style_control_ = controls[prefix_ + "_stack_style"];
  color_spread_control_ = controls[prefix_ + "_spectral_morph_spread"];
  focus_spread_control_ = controls[prefix_ + "_noise_focus_spread"];
}

NoiseViewer::State NoiseViewer::readState() {
  State state;
  state.color = getOutputsTotal(color_outputs_, color_slider_->getValue());
  state.focus = getOutputsTotal(focus_outputs_, focus_slider_->getValue());
  state.midi = kReferenceMidiNote;
  if (transpose_slider_)
    state.midi += getOutputsTotal(transpose_outputs_, transpose_slider_->getValue());
  if (tune_slider_)
    state.midi += getOutputsTotal(tune_outputs_, tune_slider_->getValue());

  state.voices = 1.0f;
  state.cents = 0.0f;
  state.power = 0.0f;
  state.blend = 0.0f;
  state.color_spread = 0.0f;
  state.focus_spread = 0.0f;
  state.stack_style = 0.0f;
  loadControls();
  if (voices_slider_ && detune_slider_ && detune_power_slider_ && stack_style_control_) {
    state.voices = getOutputsTotal(voices_outputs_, voices_slider_->getValue());
    vital::poly_float detune = getOutputsTotal(detune_outputs_, detune_slider_->getValue());
    vital::poly_float range = getOutputsTotal(detune_range_outputs_, detune_range_control_->value());
    state.cents = range * detune;
    state.power = getOutputsTotal(detune_power_outputs_, detune_power_slider_->getValue());
    state.blend = getOutputsTotal(blend_outputs_, blend_control_->value());
    state.stack_style = stack_style_control_->value();
    state.color_spread = getOutputsTotal(color_spread_outputs_, color_spread_control_->value());
    state.focus_spread = getOutputsTotal(focus_spread_outputs_, focus_spread_control_->value());
  }
  return state;
}

void NoiseViewer::loadResponses(const State& state) {
  static constexpr float kDbRange = kMaxDb - kMinDb;
  static constexpr int kMaxUnison = vital::SynthOscillator::kMaxUnison;

  int unison = vital::utils::clamp(std::round(state.voices[0]), 1.0f, (float)kMaxUnison);
  int stack_style = std::max(0, std::min((int)std::round(state.stack_style),
                                         vital::SynthOscillator::kNumUnisonStackTypes - 1));
  vital::poly_float detunings[vital::SynthOscillator::kNumPolyPhase];
  vital::SynthOscillator::computeUnisonDetunings(unison, state.cents, state.power, stack_style, detunings);
  vital::poly_float center_amplitude, detuned_amplitude;
  vital::SynthOscillator::computeUnisonAmplitudes(unison, state.blend, &center_amplitude, &detuned_amplitude);

  int num_pairs = (unison + (unison % 2)) / 2;
  float center = getHeight() * 0.5f;
  OpenGlLineRenderer* renderers[2] = { &left_line_renderer_, &right_line_renderer_ };
  for (int lane = 0; lane < 2; ++lane) {
    float* power = power_[lane];
    std::fill(power, power + kResolution + 1, 0.0f);
    float frequency = vital::utils::midiNoteToFrequency(state.midi[lane]);
    for (int pair = 0; pair < num_pairs; ++pair) {
      float gain = pair == 0 ? center_amplitude[lane] : detuned_amplitude[lane];
      // The same spread the engine applies: the centre pair at the knobs, the outer pairs moved away.
      float t = 2.0f * pair / (std::max(2, num_pairs) - 1.0f);
      float color = vital::utils::clamp(state.color[lane] + t * state.color_spread[lane], 0.0f, 1.0f);
      float focus = vital::utils::clamp(state.focus[lane] + t * state.focus_spread[lane], 0.0f, 1.0f);
      if (gain > 0.0f) {
        response_.addPowerResponse(color, focus, frequency * detunings[pair][lane],
                                   gain, evaluate_frequencies_, power, kResolution + 1);
      }
    }

    for (int i = 0; i <= kResolution; ++i) {
      float db = 10.0f * std::log10(std::max(power[i], 1e-12f));
      float y = vital::utils::clamp((db - kMinDb) / kDbRange, 0.0f, 1.0f);
      renderers[lane]->setYAt(i, center + y * center);
      renderers[lane]->setYAt(kNumPoints - 1 - i, center - y * center);
    }
  }
}

void NoiseViewer::render(OpenGlWrapper& open_gl, bool animate) {
  animate_ = animate;
  if (parent_ == nullptr || color_slider_ == nullptr || focus_slider_ == nullptr)
    return;

  // Switching to noise can show this before paintBackground() has passed the parent on, and the
  // renderers look up skin values through it.
  left_line_renderer_.setParent(parent_);
  right_line_renderer_.setParent(parent_);

  State state = readState();
  if (dirty_ || !(state == last_state_)) {
    loadResponses(state);
    last_state_ = state;
    dirty_ = false;
  }

  float fill_fade = findValue(Skin::kWidgetFillFade);
  Colour line_left = findColour(Skin::kWidgetPrimary1, true);
  Colour line_right = findColour(Skin::kWidgetPrimary2, true);
  Colour fill_left = findColour(Skin::kWidgetSecondary1, true);
  Colour fill_right = findColour(Skin::kWidgetSecondary2, true);
  if (!isActive()) {
    line_left = line_right = findColour(Skin::kWidgetPrimaryDisabled, true);
    fill_left = fill_right = findColour(Skin::kWidgetSecondaryDisabled, true);
  }

  right_line_renderer_.setColor(line_right);
  left_line_renderer_.setColor(line_left);
  right_line_renderer_.setFillColors(fill_right.withMultipliedAlpha(1.0f - fill_fade), fill_right);
  left_line_renderer_.setFillColors(fill_left.withMultipliedAlpha(1.0f - fill_fade), fill_left);

  right_line_renderer_.render(open_gl, animate);
  left_line_renderer_.render(open_gl, animate);
  left_line_renderer_.renderCorners(open_gl, animate);
}
