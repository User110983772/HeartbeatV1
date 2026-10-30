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

#include "producers_module.h"

#include "futils.h"
#include "portamento_slope.h"
#include "synth_constants.h"

namespace vital {
  ProducersModule::ProducersModule() : SynthModule(kNumInputs, kNumOutputs),
                                       engine_order_(std::make_shared<EngineOrder>()), random_(-1.0f, 1.0f) {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillators_[i] = new OscillatorModule("osc_" + number);
      addSubmodule(oscillators_[i]);
      addProcessor(oscillators_[i]);
      oscillators_[i]->enable(false);
      oscillator_groups_[i] = nullptr;
      oscillator_outputs_[i] = nullptr;
      (*engine_order_)[i] = i;
    }
  }

  void ProducersModule::init() {
    for (int i = 0; i < kNumOscillators; ++i) {
      std::string number = std::to_string(i + 1);
      oscillator_groups_[i] = createBaseControl("osc_" + number + "_group");
      oscillator_outputs_[i] = createBaseControl("osc_" + number + "_destination");

      oscillators_[i]->useInput(input(kReset), OscillatorModule::kReset);
      oscillators_[i]->useInput(input(kRetrigger), OscillatorModule::kRetrigger);
      oscillators_[i]->useInput(input(kMidi), OscillatorModule::kMidi);
      oscillators_[i]->useInput(input(kActiveVoices), OscillatorModule::kActiveVoices);
      oscillators_[i]->useInput(input(kNoteCount), OscillatorModule::kNoteCount);
      oscillators_[i]->useInput(input(kBeatsPerSecond), OscillatorModule::kBeatsPerSecond);
    }

    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      std::string prefix = "group_" + std::to_string(g + 1) + "_";
      group_on_[g] = createBaseControl(prefix + "on");

      GroupControls& controls = group_controls_[g];
      controls.drift = createPolyModControl(prefix + "drift");
      controls.drift_rate = createPolyModControl(prefix + "drift_rate");
      controls.drift_spread = createPolyModControl(prefix + "drift_spread");
      controls.humanize_pitch = createBaseControl(prefix + "humanize_pitch");
      controls.humanize_level = createBaseControl(prefix + "humanize_level");
      controls.humanize_pan = createBaseControl(prefix + "humanize_pan");
      controls.humanize_phase = createBaseControl(prefix + "humanize_phase");

      static_assert(OscillatorModule::kNumGroupUnisonInputs == 5, "Group unison controls don't match the engine inputs.");
      controls.unison[0] = createPolyModControl(prefix + "unison_voices");
      controls.unison[1] = createPolyModControl(prefix + "unison_detune");
      controls.unison[2] = createPolyModControl(prefix + "unison_blend");
      controls.unison[3] = createPolyModControl(prefix + "stereo_spread");
      controls.unison[4] = createBaseControl(prefix + "stack_style")->output();

      controls.voice_mode = createBaseControl(prefix + "voice_mode");

      controls.glide = createPolyModControl(prefix + "glide");
      PortamentoSlope* glide_note = new PortamentoSlope();
      glide_note->useInput(input(kNote), PortamentoSlope::kTarget);
      glide_note->useInput(input(kLastNote), PortamentoSlope::kSource);
      glide_note->plug(&constants::kValueOne, PortamentoSlope::kPortamentoForce);
      glide_note->plug(&constants::kValueZero, PortamentoSlope::kPortamentoScale);
      glide_note->plug(controls.glide, PortamentoSlope::kRunSeconds);
      controls.glide_slope = createPolyModControl(prefix + "glide_slope");
      glide_note->plug(controls.glide_slope, PortamentoSlope::kSlopePower);
      glide_note->useInput(input(kVoiceEvent), PortamentoSlope::kReset);
      glide_note->useInput(input(kNotesPressed), PortamentoSlope::kNumNotesPressed);
      addProcessor(glide_note);
      controls.glide_note = glide_note;
    }

    // Set directly rather than plugged: the engines run inside process(), after these are written.
    for (int i = 0; i < kNumOscillators; ++i) {
      oscillators_[i]->input(OscillatorModule::kMidiOffset)->source = output(kEngineMidiOffsetStart + i);
      oscillators_[i]->input(OscillatorModule::kPhaseOffset)->source = output(kEnginePhaseOffsetStart + i);
      for (int k = 0; k < OscillatorModule::kNumGroupUnisonInputs; ++k) {
        int engine_output = kEngineGroupUnisonStart + i * OscillatorModule::kNumGroupUnisonInputs + k;
        oscillators_[i]->input(OscillatorModule::kGroupUnisonVoices + k)->source = output(engine_output);
      }
    }

    SynthModule::init();

    // Every FM/RM option reads the engine above, which process() copies into the modulator buffer.
    for (int i = 0; i < kNumOscillators; ++i) {
      Output* modulator = output(kModulatorStart + i);
      oscillators_[i]->oscillator()->setFirstOscillatorOutput(modulator);
      oscillators_[i]->oscillator()->setSecondOscillatorOutput(modulator);
      oscillators_[i]->oscillator()->setSampleOutput(modulator);
    }
  }

  void ProducersModule::updateOscillatorMemory() {
    for (int i = 0; i < kNumOscillators; ++i) {
      bool needs_memory = oscillators_[i]->isOn() && !oscillators_[i]->isSample();
      oscillators_[i]->oscillator()->setSpectralMemory(needs_memory);
    }
  }

  bool ProducersModule::setEngineOrder(const EngineOrder& order) {
    bool seen[kNumOscillators] = {};
    for (int index : order) {
      if (index < 0 || index >= kNumOscillators || seen[index])
        return false;
      seen[index] = true;
    }
    *engine_order_ = order;
    return true;
  }

  int ProducersModule::groupOf(int index) const {
    return utils::iclamp(static_cast<int>(oscillator_groups_[index]->value()), 0, kNumOscillatorGroups - 1);
  }

  int ProducersModule::getEngineAbove(int index) const {
    int group = groupOf(index);
    int above = -1;
    for (int engine : *engine_order_) {
      if (engine == index)
        return above;
      if (groupOf(engine) == group)
        above = engine;
    }
    return -1;
  }

  bool ProducersModule::isGroupOn(int group) const {
    return group_on_[group] == nullptr || group_on_[group]->value() != 0.0f;
  }

  bool ProducersModule::isGroupUsed(int group) const {
    if (!isGroupOn(group))
      return false;
    for (int i = 0; i < kNumOscillators; ++i) {
      if (oscillators_[i]->isOn() && oscillator_groups_[i] && groupOf(i) == group)
        return true;
    }
    return false;
  }

  void ProducersModule::advanceDrift(DriftStream& stream, mono_float rate, int num_samples) {
    static constexpr int kMaxSteps = 4;
    stream.position += std::max(0.0f, rate) * num_samples / getSampleRate();
    for (int step = 0; stream.position >= 1.0f && step < kMaxSteps; ++step) {
      stream.points[0] = stream.points[1];
      stream.points[1] = stream.points[2];
      stream.points[2] = stream.points[3];
      stream.points[3] = random_.polyVoiceNext();
      stream.position -= 1.0f;
    }
    stream.position = std::min(stream.position, 1.0f);
  }

  poly_float ProducersModule::driftValue(const DriftStream& stream) const {
    // Catmull-Rom through the random points: smooth, and stays close to the -1 to 1 range.
    poly_float t = stream.position;
    const poly_float* p = stream.points;
    poly_float a = p[1] * 3.0f - p[0] - p[2] * 3.0f + p[3];
    poly_float b = p[0] * 2.0f - p[1] * 5.0f + p[2] * 4.0f - p[3];
    poly_float c = p[2] - p[0];
    return (((a * t + b) * t + c) * t) * 0.5f + p[1];
  }

  void ProducersModule::rollHumanize(poly_mask reset_mask) {
    if (reset_mask.anyMask() == 0)
      return;

    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      humanize_pitch_[g] = utils::maskLoad(humanize_pitch_[g], random_.polyVoiceNext(), reset_mask);
      humanize_level_[g] = utils::maskLoad(humanize_level_[g], random_.polyVoiceNext(), reset_mask);
      humanize_pan_[g] = utils::maskLoad(humanize_pan_[g], random_.polyVoiceNext(), reset_mask);
    }
    for (int i = 0; i < kNumOscillators; ++i) {
      poly_float phase = (random_.polyVoiceNext() + 1.0f) * 0.5f;
      humanize_phase_[i] = utils::maskLoad(humanize_phase_[i], phase, reset_mask);
    }
  }

  void ProducersModule::setEngineInputs(int index, int group, const poly_float group_pitch[]) {
    const GroupControls& controls = group_controls_[group];
    poly_float spread = utils::clamp(controls.drift_spread->buffer[0], 0.0f, 1.0f);
    poly_float drift = utils::interpolate(driftValue(group_drift_[group]), driftValue(engine_drift_[index]), spread);
    poly_float drift_semitones = drift * controls.drift->buffer[0] * 0.01f;

    output(kEngineMidiOffsetStart + index)->buffer[0] = group_pitch[group] + drift_semitones;
    output(kEnginePhaseOffsetStart + index)->buffer[0] = humanize_phase_[index] * controls.humanize_phase->value();
    for (int k = 0; k < OscillatorModule::kNumGroupUnisonInputs; ++k) {
      int engine_output = kEngineGroupUnisonStart + index * OscillatorModule::kNumGroupUnisonInputs + k;
      output(engine_output)->buffer[0] = controls.unison[k]->buffer[0];
    }
  }

  bool ProducersModule::isLegato(int group) const {
    return static_cast<int>(group_controls_[group].voice_mode->value()) == constants::kGroupLegato;
  }

  poly_float ProducersModule::legatoNote(int group, poly_mask reset_mask, int num_samples) {
    const GroupControls& controls = group_controls_[group];
    poly_float target = input(kLegatoNote)->at(0);

    // A new note snaps; a new target glides from where the phrase is now.
    poly_float current = futils::powerScale(legato_position_[group], -controls.glide_slope->buffer[0]);
    current = utils::interpolate(legato_from_[group], legato_to_[group], current);
    poly_mask moved = poly_float::notEqual(target, legato_to_[group]);
    legato_from_[group] = utils::maskLoad(legato_from_[group], current, moved);
    legato_position_[group] = utils::maskLoad(legato_position_[group], 0.0f, moved);
    legato_from_[group] = utils::maskLoad(legato_from_[group], target, reset_mask);
    legato_position_[group] = utils::maskLoad(legato_position_[group], 1.0f, reset_mask);
    legato_to_[group] = target;

    poly_float seconds = controls.glide->buffer[0];
    poly_mask gliding = poly_float::greaterThan(seconds, PortamentoSlope::kMinPortamentoTime);
    poly_float step = poly_float(1.0f * num_samples) / (utils::max(seconds, PortamentoSlope::kMinPortamentoTime) * getSampleRate());
    poly_float position = utils::clamp(legato_position_[group] + step, 0.0f, 1.0f);
    legato_position_[group] = utils::maskLoad(poly_float(1.0f), position, gliding);

    poly_float shaped = futils::powerScale(legato_position_[group], -controls.glide_slope->buffer[0]);
    return utils::interpolate(legato_from_[group], legato_to_[group], shaped);
  }

  poly_float ProducersModule::groupGain(int group) {
    const GroupControls& controls = group_controls_[group];
    poly_mask playing = constants::kFullMask;
    int mode = static_cast<int>(controls.voice_mode->value());
    if (mode == constants::kGroupMono)
      playing = poly_float::greaterThan(input(kNewest)->at(0), 0.5f);
    else if (mode == constants::kGroupLegato)
      playing = poly_float::greaterThan(input(kLegatoVoice)->at(0), 0.5f);

    poly_float level = futils::dbToMagnitude(humanize_level_[group] * controls.humanize_level->value());
    poly_float pan = humanize_pan_[group] * controls.humanize_pan->value();
    poly_float left = -utils::max(pan, 0.0f) + 1.0f;
    poly_float right = utils::min(pan, 0.0f) + 1.0f;
    return (utils::maskLoad(right, left, constants::kLeftMask) * level) & playing;
  }

  void ProducersModule::applyGroupGain(int group, poly_mask reset_mask, int num_samples) {
    static constexpr mono_float kFadeSeconds = 0.005f;

    poly_float target = groupGain(group);
    poly_float current = utils::maskLoad(group_gain_[group], target, reset_mask);
    poly_mask changing = ~poly_float::equal(current, target);
    poly_mask not_unity = ~poly_float::equal(current, 1.0f);
    if ((changing | not_unity).anyMask() == 0) {
      group_gain_[group] = current;
      return;
    }

    poly_float* audio = output(kGroupOutputStart + group)->buffer;
    poly_float* direct = output(kGroupDirectStart + group)->buffer;
    mono_float max_step = 1.0f / (kFadeSeconds * getSampleRate());
    for (int i = 0; i < num_samples; ++i) {
      current += utils::clamp(target - current, -max_step, max_step);
      audio[i] *= current;
      direct[i] *= current;
    }
    group_gain_[group] = current;
  }

  void ProducersModule::process(int num_samples) {
    SynthModule::process(num_samples);

    poly_mask reset_mask = getResetMask(kReset);
    rollHumanize(reset_mask);

    poly_float group_pitch[kNumOscillatorGroups];
    poly_float global_glide = input(kGlobalGlide)->at(0);
    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      const GroupControls& controls = group_controls_[g];
      poly_float pitch = humanize_pitch_[g] * (controls.humanize_pitch->value() * 0.01f);

      // A legato group plays the phrase's pitch in place of the voice's own note; otherwise a group's own
      // glide replaces the global one.
      if (isLegato(g))
        pitch += legatoNote(g, reset_mask, num_samples) - global_glide;
      else {
        poly_mask own_glide = poly_float::greaterThan(controls.glide->buffer[0], PortamentoSlope::kMinPortamentoTime);
        poly_float glide_offset = controls.glide_note->output()->buffer[0] - global_glide;
        pitch += glide_offset & own_glide;
      }
      group_pitch[g] = pitch;

      advanceDrift(group_drift_[g], controls.drift_rate->buffer[0][0], num_samples);
    }
    for (int i = 0; i < kNumOscillators; ++i) {
      if (oscillators_[i]->isOn())
        advanceDrift(engine_drift_[i], group_controls_[groupOf(i)].drift_rate->buffer[0][0], num_samples);
    }

    bool group_on[kNumOscillatorGroups];
    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      utils::zeroBuffer(output(kGroupOutputStart + g)->buffer, num_samples);
      utils::zeroBuffer(output(kGroupDirectStart + g)->buffer, num_samples);
      group_on[g] = isGroupOn(g);
    }

    // Engines run top to bottom; each group's chain is the engine order filtered to that group.
    int last_in_group[kNumOscillatorGroups];
    for (int g = 0; g < kNumOscillatorGroups; ++g)
      last_in_group[g] = -1;

    for (int index : *engine_order_) {
      OscillatorModule* module = oscillators_[index];
      int group = groupOf(index);
      // A switched off group's engines don't run.
      if (!group_on[group])
        continue;
      int above = last_in_group[group];
      last_in_group[group] = index;

      poly_float* modulator = output(kModulatorStart + index)->buffer;
      if (above >= 0)
        utils::copyBuffer(modulator, oscillators_[above]->output(OscillatorModule::kRaw)->buffer, num_samples);
      else
        utils::zeroBuffer(modulator, num_samples);

      setEngineInputs(index, group, group_pitch);
      getLocalProcessor(module)->process(num_samples);

      bool direct = static_cast<int>(oscillator_outputs_[index]->value()) == constants::kEngineDirectOut;
      poly_float* group_output = output((direct ? kGroupDirectStart : kGroupOutputStart) + group)->buffer;
      const poly_float* levelled = module->output(OscillatorModule::kLevelled)->buffer;
      utils::addBuffers(group_output, group_output, levelled, num_samples);
    }

    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      if (group_on[g])
        applyGroupGain(g, reset_mask, num_samples);
    }
  }
} // namespace vital
