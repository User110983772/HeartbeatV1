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

#include "oscillator_module.h"

#include "synth_oscillator.h"
#include "wavetable.h"

namespace vital {

  namespace {
    // The larger of the engine's and group's value while the engine uses its group's unison, else the engine's.
    class WiderWhenOn : public Processor {
      public:
        enum { kOn, kOwn, kGroup, kNumInputs };

        WiderWhenOn() : Processor(kNumInputs, 1, true) { }
        Processor* clone() const override { return new WiderWhenOn(*this); }

        void process(int num_samples) override {
          poly_float own = input(kOwn)->at(0);
          bool on = input(kOn)->at(0)[0] != 0.0f;
          output()->buffer[0] = on ? utils::max(own, input(kGroup)->at(0)) : own;
        }
    };

    // 0 while the engine ignores its group's unison, otherwise 1 + its constants::GroupUnisonMode.
    class GroupUnisonMode : public Processor {
      public:
        enum { kOn, kMode, kNumInputs };

        GroupUnisonMode() : Processor(kNumInputs, 1, true) { }
        Processor* clone() const override { return new GroupUnisonMode(*this); }

        void process(int num_samples) override {
          bool on = input(kOn)->at(0)[0] != 0.0f;
          output()->buffer[0] = on ? input(kMode)->at(0) + 1.0f : 0.0f;
        }
    };

    // The voice's note plus the group's pitch offset. Not an Operator, so it runs whatever is plugged in when.
    class AddOffset : public Processor {
      public:
        AddOffset() : Processor(2, 1, true) { }
        Processor* clone() const override { return new AddOffset(*this); }

        void process(int num_samples) override {
          output()->buffer[0] = input(0)->at(0) + input(1)->at(0);
        }
    };

    // Start phase plus the group's humanise offset, wrapped to one cycle. The phase control is audio rate.
    class PhaseOffset : public Processor {
      public:
        enum { kPhase, kOffset, kNumInputs };

        PhaseOffset() : Processor(kNumInputs, 1) { }
        Processor* clone() const override { return new PhaseOffset(*this); }

        void process(int num_samples) override {
          const Output* phase = input(kPhase)->source;
          poly_float offset = input(kOffset)->at(0);
          poly_float* destination = output()->buffer;
          if (phase->buffer_size == 1) {
            poly_float value = utils::mod(phase->buffer[0] + offset);
            for (int i = 0; i < num_samples; ++i)
              destination[i] = value;
            return;
          }
          for (int i = 0; i < num_samples; ++i)
            destination[i] = utils::mod(phase->buffer[i] + offset);
        }
    };
  }

  OscillatorModule::OscillatorModule(std::string prefix) :
      SynthModule(kNumInputs, kNumOutputs), prefix_(std::move(prefix)), on_(nullptr), source_(nullptr), oscillator_(nullptr),
      distortion_type_(nullptr) {
    sampler_ = new SampleSource();
    wavetable_ = std::make_shared<Wavetable>(kNumOscillatorWaveFrames);
    was_on_ = std::make_shared<bool>(true);
  }

  void OscillatorModule::init() {
    oscillator_ = new SynthOscillator(wavetable_.get());

    createBaseControl(prefix_ + "_view_2d");
    on_ = createBaseControl(prefix_ + "_on");
    Value* midi_track = createBaseControl(prefix_ + "_midi_track");
    Value* smooth_interpolation = createBaseControl(prefix_ + "_smooth_interpolation");
    Value* spectral_unison = createBaseControl(prefix_ + "_spectral_unison");
    Value* stack_style = createBaseControl(prefix_ + "_stack_style");
    Value* transpose_quantize = createBaseControl(prefix_ + "_transpose_quantize");
    source_ = createBaseControl(prefix_ + "_source");
    Value* sample_keytrack = createBaseControl(prefix_ + "_sample_keytrack");
    Value* sample_loop = createBaseControl(prefix_ + "_sample_loop");
    Value* sample_bounce = createBaseControl(prefix_ + "_sample_bounce");

    Input* reset = input(kReset);

    Output* wave_frame = createPolyModControl(prefix_ + "_wave_frame");
    Output* transpose = createPolyModControl(prefix_ + "_transpose", true, false, nullptr, reset);
    Output* tune = createPolyModControl(prefix_ + "_tune", true, false, nullptr, reset);
    Output* unison_voices = createPolyModControl(prefix_ + "_unison_voices");
    Output* unison_detune = createPolyModControl(prefix_ + "_unison_detune");
    Output* detune_power = createPolyModControl(prefix_ + "_detune_power");
    Output* detune_range = createPolyModControl(prefix_ + "_detune_range");
    Output* amplitude = createPolyModControl(prefix_ + "_level", true, true, nullptr, reset);
    Output* pan = createPolyModControl(prefix_ + "_pan");
    Output* phase = createPolyModControl(prefix_ + "_phase", true, true, nullptr, reset);
    Output* distortion_phase = createPolyModControl(prefix_ + "_distortion_phase");
    Output* rand_phase = createPolyModControl(prefix_ + "_random_phase");
    Output* blend = createPolyModControl(prefix_ + "_unison_blend");
    Output* stereo_spread = createPolyModControl(prefix_ + "_stereo_spread");
    Output* frame_spread = createPolyModControl(prefix_ + "_frame_spread");
    Output* distortion_spread = createPolyModControl(prefix_ + "_distortion_spread");
    distortion_type_ = createBaseControl(prefix_ + "_distortion_type");
    Output* distortion_amount = createPolyModControl(prefix_ + "_distortion_amount");
    Output* spectral_morph_spread = createPolyModControl(prefix_ + "_spectral_morph_spread");
    Value* spectral_morph_type = createBaseControl(prefix_ + "_spectral_morph_type");
    Output* spectral_morph_amount = createPolyModControl(prefix_ + "_spectral_morph_amount");
    Output* noise_color = createPolyModControl(prefix_ + "_noise_color");
    Output* noise_focus = createPolyModControl(prefix_ + "_noise_focus");
    Output* noise_focus_spread = createPolyModControl(prefix_ + "_noise_focus_spread");

    Value* group_unison = createBaseControl(prefix_ + "_group_unison");
    Value* group_unison_mode = createBaseControl(prefix_ + "_group_unison_mode");
    GroupUnisonMode* unison_mode = new GroupUnisonMode();
    unison_mode->plug(group_unison, GroupUnisonMode::kOn);
    unison_mode->plug(group_unison_mode, GroupUnisonMode::kMode);
    addProcessor(unison_mode);

    auto wider = [&](const Output* own, int group_input) {
      WiderWhenOn* select = new WiderWhenOn();
      select->plug(group_unison, WiderWhenOn::kOn);
      select->plug(own, WiderWhenOn::kOwn);
      select->useInput(input(group_input), WiderWhenOn::kGroup);
      addProcessor(select);
      return select->output();
    };
    blend = wider(blend, kGroupUnisonBlend);
    stereo_spread = wider(stereo_spread, kGroupStereoSpread);

    AddOffset* engine_midi = new AddOffset();
    engine_midi->useInput(input(kMidi), 0);
    engine_midi->useInput(input(kMidiOffset), 1);
    addProcessor(engine_midi);

    PhaseOffset* engine_phase = new PhaseOffset();
    engine_phase->plug(phase, PhaseOffset::kPhase);
    engine_phase->useInput(input(kPhaseOffset), PhaseOffset::kOffset);
    addProcessor(engine_phase);
    phase = engine_phase->output();

    oscillator_->useInput(input(kReset), SynthOscillator::kReset);
    oscillator_->useInput(input(kRetrigger), SynthOscillator::kRetrigger);
    oscillator_->useInput(input(kActiveVoices), SynthOscillator::kActiveVoices);
    oscillator_->plug(engine_midi, SynthOscillator::kMidiNote);
    oscillator_->plug(wave_frame, SynthOscillator::kWaveFrame);
    oscillator_->plug(midi_track, SynthOscillator::kMidiTrack);
    oscillator_->plug(smooth_interpolation, SynthOscillator::kSmoothlyInterpolate);
    oscillator_->plug(spectral_unison, SynthOscillator::kSpectralUnison);
    oscillator_->plug(transpose_quantize, SynthOscillator::kTransposeQuantize);
    oscillator_->plug(source_, SynthOscillator::kSource);
    oscillator_->plug(transpose, SynthOscillator::kTranspose);
    oscillator_->plug(tune, SynthOscillator::kTune);
    oscillator_->plug(stack_style, SynthOscillator::kStackStyle);
    oscillator_->plug(unison_mode, SynthOscillator::kGroupUnisonMode);
    oscillator_->useInput(input(kGroupUnisonVoices), SynthOscillator::kGroupUnisonVoices);
    oscillator_->useInput(input(kGroupUnisonDetune), SynthOscillator::kGroupUnisonDetune);
    oscillator_->useInput(input(kGroupStackStyle), SynthOscillator::kGroupStackStyle);
    oscillator_->plug(unison_detune, SynthOscillator::kUnisonDetune);
    oscillator_->plug(unison_voices, SynthOscillator::kUnisonVoices);
    oscillator_->plug(phase, SynthOscillator::kPhase);
    oscillator_->plug(distortion_phase, SynthOscillator::kDistortionPhase);
    oscillator_->plug(rand_phase, SynthOscillator::kRandomPhase);
    oscillator_->plug(blend, SynthOscillator::kBlend);
    oscillator_->plug(amplitude, SynthOscillator::kAmplitude);
    oscillator_->plug(pan, SynthOscillator::kPan);
    oscillator_->plug(detune_power, SynthOscillator::kDetunePower);
    oscillator_->plug(detune_range, SynthOscillator::kDetuneRange);
    oscillator_->plug(stereo_spread, SynthOscillator::kStereoSpread);
    oscillator_->plug(frame_spread, SynthOscillator::kUnisonFrameSpread);
    oscillator_->plug(distortion_spread, SynthOscillator::kUnisonDistortionSpread);
    oscillator_->plug(distortion_type_, SynthOscillator::kDistortionType);
    oscillator_->plug(distortion_amount, SynthOscillator::kDistortionAmount);
    oscillator_->plug(spectral_morph_spread, SynthOscillator::kUnisonSpectralMorphSpread);
    oscillator_->plug(spectral_morph_type, SynthOscillator::kSpectralMorphType);
    oscillator_->plug(spectral_morph_amount, SynthOscillator::kSpectralMorphAmount);
    oscillator_->plug(noise_color, SynthOscillator::kNoiseColor);
    oscillator_->plug(noise_focus, SynthOscillator::kNoiseFocus);
    oscillator_->plug(noise_focus_spread, SynthOscillator::kNoiseFocusSpread);
    oscillator_->useOutput(output(kRaw), SynthOscillator::kRaw);
    oscillator_->useOutput(output(kLevelled), SynthOscillator::kLevelled);

    // A sample engine shares the oscillator's level, pan, tuning and random phase settings.
    sampler_->useInput(input(kReset), SampleSource::kReset);
    sampler_->plug(engine_midi, SampleSource::kMidi);
    sampler_->useInput(input(kNoteCount), SampleSource::kNoteCount);
    sampler_->plug(rand_phase, SampleSource::kRandomPhase);
    sampler_->plug(sample_keytrack, SampleSource::kKeytrack);
    sampler_->plug(sample_loop, SampleSource::kLoop);
    sampler_->plug(sample_bounce, SampleSource::kBounce);
    sampler_->plug(transpose, SampleSource::kTranspose);
    sampler_->plug(transpose_quantize, SampleSource::kTransposeQuantize);
    sampler_->plug(tune, SampleSource::kTune);
    sampler_->plug(amplitude, SampleSource::kLevel);
    sampler_->plug(pan, SampleSource::kPan);
    sampler_->useOutput(output(kRaw), SampleSource::kRaw);
    sampler_->useOutput(output(kLevelled), SampleSource::kLevelled);

    addProcessor(oscillator_);
    addProcessor(sampler_);
    SynthModule::init();
  }

  void OscillatorModule::process(int num_samples) {
    bool on = on_->value();

    if (on) {
      bool sample = isSample();
      oscillator_->enable(!sample);
      sampler_->enable(sample);
      SynthModule::process(num_samples);
    }
    else if (*was_on_) {
      output(kRaw)->clearBuffer();
      output(kLevelled)->clearBuffer();
      sampler_->getPhaseOutput()->buffer[0] = 0.0f;
    }

    *was_on_ = on;
  }
} // namespace vital
