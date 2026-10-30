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

#include "processor.h"
#include "json/json.h"
#include "utils.h"

#include <cmath>

using json = nlohmann::json;

namespace vital {

  struct StretchSlot;
  class StretchPool;

  class Sample {
    public:
      static constexpr int kDefaultSampleLength = 44100;
      static constexpr int kUpsampleTimes = 1;
      static constexpr int kBufferSamples = 4;
      static constexpr int kMinSize = 4;

      struct SampleData {
        SampleData(int l, int sr, bool s) : length(l), sample_rate(sr), stereo(s) { }
        
        int length;
        int sample_rate;
        bool stereo;
        std::vector<std::unique_ptr<mono_float[]>> left_buffers;
        std::vector<std::unique_ptr<mono_float[]>> left_loop_buffers;
        std::vector<std::unique_ptr<mono_float[]>> right_buffers;
        std::vector<std::unique_ptr<mono_float[]>> right_loop_buffers;

        JUCE_LEAK_DETECTOR(SampleData)
      };
    
      Sample();
      ~Sample();

      // STRETCH and SYNC run a stretcher per note, from a pool built here on the message thread (about 30 MB, so
      // only for engines that use them). The audio thread asks for it and plays unstretched until it exists.
      void prepareStretch();
      bool stretchPrepared() const { return stretch_pool_.load() != nullptr; }
      bool stretchRequested() const { return stretch_requested_.load(); }
      void requestStretch() { stretch_requested_ = true; }
      StretchSlot* claimStretchSlot();

      void loadSample(const mono_float* buffer, int size, int sample_rate);
      void loadSample(const mono_float* left_buffer, const mono_float* right_buffer, int size, int sample_rate);
      void setName(const std::string& name) { name_ = name; }
      std::string getName() const { return name_; }
      void setLastBrowsedFile(const std::string& path) { last_browsed_file_ = path; }
      std::string getLastBrowsedFile() const { return last_browsed_file_; }

      force_inline int originalLength() const { return current_data_->length; }
      force_inline int upsampleLength() { return originalLength() * (1 << kUpsampleTimes); }
      force_inline int sampleRate() const { return current_data_->sample_rate; }

      force_inline int activeLength() const { return active_audio_data_.load()->length * (1 << kUpsampleTimes); }
      force_inline int activeSampleRate() const { return active_audio_data_.load()->sample_rate; }

      force_inline const mono_float* buffer() const { return current_data_->left_buffers[kUpsampleTimes].get() + 1; }
      void init();

      int getActiveIndex(mono_float delta) {
        int octaves = utils::ilog2(std::max<int>(delta, 1));
        return std::min(octaves, (int)active_audio_data_.load()->left_buffers.size() - 1);
      }

      force_inline const mono_float* getActiveLeftBuffer(int index) {
        VITAL_ASSERT(index >= 0 && index < active_audio_data_.load()->left_buffers.size());

        return active_audio_data_.load()->left_buffers[index].get();
      }

      force_inline const mono_float* getActiveLeftLoopBuffer(int index) {
        VITAL_ASSERT(index >= 0 && index < active_audio_data_.load()->left_loop_buffers.size());

        return active_audio_data_.load()->left_loop_buffers[index].get();
      }

      force_inline const mono_float* getActiveRightBuffer(int index) {
        if (active_audio_data_.load()->stereo) {
          VITAL_ASSERT(index >= 0 && index < active_audio_data_.load()->right_buffers.size());
          return active_audio_data_.load()->right_buffers[index].get();
        }
        return getActiveLeftBuffer(index);
      }

      force_inline const mono_float* getActiveRightLoopBuffer(int index) {
        if (active_audio_data_.load()->stereo) {
          VITAL_ASSERT(index >= 0 && index < active_audio_data_.load()->right_loop_buffers.size());
          return active_audio_data_.load()->right_loop_buffers[index].get();
        }
        return getActiveLeftLoopBuffer(index);
      }

      force_inline void markUsed() { active_audio_data_ = current_data_; }
      force_inline void markUnused() { active_audio_data_ = nullptr; }

      json stateToJson();
      void jsonToState(json data);

    protected:
      std::string name_;
      std::string last_browsed_file_;
      SampleData* current_data_;
      std::atomic<SampleData*> active_audio_data_;
      std::unique_ptr<SampleData> data_;
      std::unique_ptr<StretchPool> owned_stretch_pool_;
      std::atomic<StretchPool*> stretch_pool_;
      std::atomic<bool> stretch_requested_;

      JUCE_LEAK_DETECTOR(Sample)
  };

  class SampleSource : public Processor {
    public:
      static constexpr mono_float kMaxTranspose = 96.0f;
      static constexpr mono_float kMinTranspose = -96.0f;
      static constexpr mono_float kMaxAmplitude = 1.41421356237f;

      static constexpr int kMaxUnison = 16;

      // Where one unison voice plays, as fractions of the sample. With LOOP on the order is always
      // START <= LOOP START < LOOP END <= END: START and END can't cut into the loop (a loop end set before its
      // start loops between the two), the loop spreads move the voice's loop later but keep it inside END, and
      // START SPREAD (forwards) can't start a voice past where its loop's crossfade begins.
      struct Placement {
        double start;
        double end;
        double loop_start;
        double loop_end;
        double crossfade;
        // Furthest in a voice may start (forwards: the latest start; reversed: the earliest).
        double start_limit;
        double voice_start;
      };
      static Placement placeVoice(double start, double end, double loop_start, double loop_end, double crossfade,
                                  double start_spread, double loop_spread, double loop_end_spread, bool loop,
                                  bool reverse, double t, double min_loop) {
        auto clamp01 = [](double value) { return std::max(0.0, std::min(1.0, value)); };
        Placement p;
        p.start = clamp01(start);
        p.end = clamp01(end);
        double set_start = std::min(1.0 - min_loop, clamp01(std::min(loop_start, loop_end)));
        double set_end = std::min(1.0, std::max(set_start + min_loop, clamp01(std::max(loop_start, loop_end))));
        if (loop) {
          p.start = std::min(p.start, set_start);
          p.end = std::max(p.end, set_end);
        }
        p.start = std::min(p.start, 1.0 - min_loop);
        p.end = std::min(1.0, std::max(p.end, p.start + min_loop));

        if (loop) {
          p.loop_end = std::min(p.end, set_end + clamp01(loop_end_spread) * kLoopSpreadRange * t);
          p.loop_start = std::min(p.loop_end - min_loop, set_start + clamp01(loop_spread) * kLoopSpreadRange * t);
        }
        else {
          p.loop_start = p.start;
          p.loop_end = p.end;
        }
        p.crossfade = clamp01(crossfade) * 0.5 * (p.loop_end - p.loop_start);

        double offset = clamp01(start_spread) * kStartSpreadRange * t;
        if (reverse) {
          p.start_limit = loop ? std::min(p.end, p.loop_start + p.crossfade) : p.start;
          p.voice_start = std::max(p.start_limit, p.end - offset);
        }
        else {
          p.start_limit = loop ? std::max(p.start, p.loop_end - p.crossfade) : p.end;
          p.voice_start = std::min(p.start_limit, p.start + offset);
        }
        return p;
      }
      static constexpr int kMaxEntries = kMaxUnison / 2;
      // The shortest loop, in (upsampled) sample frames.
      static constexpr int kMinLoopFrames = 64;
      // How far START SPREAD and LOOP SPREAD move the outermost unison voice, as fractions of the sample.
      static constexpr mono_float kStartSpreadRange = 0.25f;
      static constexpr mono_float kLoopSpreadRange = 0.1f;

      static constexpr int kNumDownsampleTaps = 55;
      static constexpr int kNumUpsampleTaps = 52;

      enum {
        kReset,
        kMidi,
        kKeytrack,
        kLevel,
        kRandomPhase,
        kTranspose,
        kTransposeQuantize,
        kTune,
        kLoop,
        kBounce,
        kPan,
        kNoteCount,
        // Where notes start and the loop, as fractions of the sample; the crossfade as a fraction of half the loop.
        kStart,
        kLoopStart,
        kLoopEnd,
        kCrossfade,
        kReverse,
        // Unison, as for the oscillator, plus spreads of each voice's start and loop window.
        kUnisonVoices,
        kUnisonDetune,
        kDetunePower,
        kDetuneRange,
        kBlend,
        kStereoSpread,
        kStackStyle,
        kStartSpread,
        kLoopSpread,
        kEnd,
        kLoopEndSpread,
        kStretch,
        kTempoSync,
        kSampleBpm,
        kBeatsPerSecond,
        kGroupUnisonMode,
        kGroupUnisonVoices,
        kGroupUnisonDetune,
        kGroupStackStyle,
        // As for the oscillator: Combined scales the engine's blend and stereo, Nested gives each layer its own.
        kGroupBlend,
        kGroupStereoSpread,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      SampleSource();

      virtual void process(int num_samples) override;
      virtual Processor* clone() const override { return new SampleSource(*this); }
      Sample* getSample() { return sample_.get(); }
      force_inline Output* getPhaseOutput() const { return phase_output_.get(); }

    private:
      poly_float snapTranspose(poly_float input_midi, poly_float transpose, int quantize);
      void stereoBlend(poly_float* audio_out, int num_samples, poly_mask reset_mask, poly_float stereo_spread);

      // One read head per unison pair (entry) and lane (each lane is one voice's left or right).
      struct Head {
        double position = 0.0;
        int direction = 1;
        bool playing = false;
        // Has played during this note; a voice added mid-note joins the first voice once.
        bool started = false;
        // Samples to wait in this block before the note begins.
        int wait = 0;
        // Inside a loop crossfade: where it began, where the head jumps, and how far.
        bool fading = false;
        // Bouncing: turning at turn_point, easing over turn_span either side; turned once it has reversed.
        bool turning = false;
        bool turned = false;
        double turn_point = 0.0;
        double turn_span = 0.0;
        double fade_from = 0.0;
        double fade_to = 0.0;
        double jump = 0.0;
      };
      Head heads_[kMaxEntries][poly_float::kSize];
      // Per unison voice (entry and lane): its level, and the detune and spread position it last had, so a voice
      // removed during a note can fade out as it was playing.
      static constexpr mono_float kVoiceFadeSeconds = 0.02f;
      mono_float entry_gain_[kMaxEntries][poly_float::kSize] = {};
      // Its gain into the other channel (Nested places voices itself), and this block's targets for both.
      mono_float entry_cross_gain_[kMaxEntries][poly_float::kSize] = {};
      poly_float own_target_[kMaxEntries];
      poly_float cross_target_[kMaxEntries];
      bool nested_ = false;
      double entry_detune_[kMaxEntries][poly_float::kSize] = {};
      double entry_t_[kMaxEntries][poly_float::kSize] = {};

      // The part of the sample a lane plays: START..END, and the loop inside it (all in upsampled frames).
      struct Region {
        double length;
        double start;
        double end;
        double loop_start;
        double loop_end;
        double crossfade;
      };
      // Where one step of a head reads: its position and, inside a loop crossfade, the other point and the blend.
      struct Step {
        double position;
        double other;
        mono_float fade;
        // A bounce's turn blends linearly (the two reads match there); a loop's jump with equal power.
        bool linear;
      };
      static constexpr mono_float kEndFadeSeconds = 0.005f;
      // How loud a read at `position` is: fading out over the last `fade_frames` before the head stops.
      static mono_float endGain(double position, int direction, const Region& region, bool loop, double fade_frames);
      static force_inline mono_float blendStep(mono_float value, mono_float into, const Step& step) {
        if (step.linear)
          return value + (into - value) * step.fade;
        // Equal power on a quarter sine: unlike square roots, it starts and ends with a gentle slope, so the
        // incoming read doesn't arrive with a tick.
        mono_float angle = step.fade * (0.5f * kPi);
        return value * std::cos(angle) + into * std::sin(angle);
      }
      static void startHead(Head& head, double position, bool reverse, int wait);
      static bool stepHead(Head& head, const Region& region, bool loop, bool bounce, double increment, Step* step);
      static bool bounceStep(Head& head, const Region& region, double increment, Step* step);

      // Stretched playback, per voice (a pair of lanes): one stretcher fed by the voice's first head; unison
      // voices read its output at their own detune.
      struct Tap {
        double read = 0.0;
        double old_read = 0.0;
        int fade_left = 0;
        int fade_length = 1;
      };
      static constexpr int kVoicesPerSource = poly_float::kSize / 2;
      void processStretched(int voice, int num_samples, const Region& region, bool loop, bool bounce,
                            double increment, double transpose, int num_entries, const poly_float* detunings,
                            mono_float gain_step, bool note_start, poly_float* raw_output);
      bool beginStretchedNote(int voice, const Region& region, bool loop, bool bounce, double increment,
                              double transpose);
      Tap taps_[kMaxEntries][poly_float::kSize];
      StretchSlot* stretch_slots_[kVoicesPerSource] = { nullptr, nullptr };
      bool stretched_note_[kVoicesPerSource] = { false, false };
      int unison_;
      poly_float blend_stereo_multiply_;
      poly_float blend_center_multiply_;

      poly_float pan_amplitude_;
      int transpose_quantize_;
      poly_float last_quantized_transpose_;
      poly_float sample_index_;
      poly_float sample_fraction_;
      poly_float phase_inc_;
      poly_mask bounce_mask_;
      std::shared_ptr<cr::Output> phase_output_;
      utils::RandomGenerator random_generator_;

      std::shared_ptr<Sample> sample_;

      JUCE_LEAK_DETECTOR(SampleSource)
  };
} // namespace vital

