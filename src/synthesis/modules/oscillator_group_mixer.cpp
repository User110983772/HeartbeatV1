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

#include "oscillator_group_mixer.h"

#include "futils.h"

#include <cmath>
#include "envelope_module.h"
#include "lfo_module.h"
#include "producers_module.h"
#include "utils.h"

namespace vital {

  OscillatorGroupMixer::OscillatorGroupMixer() : Processor(kNumInputs, kNumOutputs), producers_(nullptr),
                                                 voice_state_(kVoiceIdle), last_amplitude_(0.0f) {
    for (int i = 0; i < kNumEnvelopes; ++i)
      envelopes_[i] = nullptr;
    for (int i = 0; i < kNumLfos; ++i)
      lfos_[i] = nullptr;
    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      last_sources_[g] = 0.0f;
      last_levels_[g] = 0.0f;
      smoothed_[g] = 0.0f;
      fades_[g] = 1.0f;
    }
  }

  void OscillatorGroupMixer::setSources(const ProducersModule* producers, const EnvelopeModule* const* envelopes,
                                        const LfoModule* const* lfos) {
    producers_ = producers;
    for (int i = 0; i < kNumEnvelopes; ++i)
      envelopes_[i] = envelopes[i];
    for (int i = 0; i < kNumLfos; ++i)
      lfos_[i] = lfos[i];
  }

  // Control-rate modulators only write the first sample of a block, even into a block-sized buffer
  // (Env 2-6 keep their outputs block sized and only switch the envelope inside), so ask the module.
  bool OscillatorGroupMixer::isControlRateSource(int source_index, const Output* source) const {
    if (source->isControlRate())
      return true;
    if (source_index < kNumEnvelopes && envelopes_[source_index])
      return envelopes_[source_index]->writesControlRate();
    return source->owner && source->owner->isControlRate();
  }

  poly_mask OscillatorGroupMixer::unfinishedMask(int source_index) const {
    int lfo_index = source_index - kNumEnvelopes;
    if (lfo_index < 0 || lfo_index >= kNumLfos || lfos_[lfo_index] == nullptr || !lfos_[lfo_index]->isOneShot())
      return 0;

    poly_float phase = utils::decodePhaseAndVoice(input(kLfoPhases + lfo_index)->at(0)).first;
    return poly_float::lessThan(phase, 0.9999f);
  }

  poly_mask OscillatorGroupMixer::fadeMask(int source_index, poly_mask released_mask, poly_mask kill_mask) const {
    if (source_index < kNumEnvelopes)
      return 0;
    return (released_mask & ~unfinishedMask(source_index)) | kill_mask;
  }

  void OscillatorGroupMixer::process(int num_samples) {
    poly_float* fx_out = output(kFxBusOut)->buffer;
    poly_float* direct_out = output(kDirectOut)->buffer;
    poly_float* alive_out = output(kVoiceAlive)->buffer;
    utils::zeroBuffer(fx_out, num_samples);
    utils::zeroBuffer(direct_out, num_samples);
    utils::zeroBuffer(alive_out, num_samples);

    const Output* trigger = input(kTrigger)->source;
    voice_state_ = utils::maskLoad(voice_state_, trigger->trigger_value, trigger->trigger_mask);
    poly_mask note_on_mask = trigger->trigger_mask & poly_float::equal(trigger->trigger_value, kVoiceOn);
    poly_mask kill_mask = poly_float::equal(voice_state_, kVoiceKill);
    poly_mask released_mask = poly_float::equal(voice_state_, kVoiceOff) | kill_mask;

    poly_mask reset_mask = getResetMask(kReset);
    mono_float sample_inc = 1.0f / num_samples;
    mono_float fade_delta = 1.0f / (kFadeSeconds * getSampleRate());

    // A block holds two voices; only Env 1 is silent in an idle voice's lanes, so mask the rest.
    poly_mask active_mask = poly_float::equal(input(kActiveVoices)->at(0), 1.0f);

    poly_float amplitude = input(kAmplitude)->at(0);
    poly_float start_amplitude = utils::maskLoad(last_amplitude_, amplitude, reset_mask);
    poly_float delta_amplitude = (amplitude - start_amplitude) * sample_inc;
    last_amplitude_ = amplitude;

    for (int g = 0; g < kNumOscillatorGroups; ++g) {
      int source_index = utils::iclamp(static_cast<int>(input(kAmpSourceIndices + g)->at(0)[0]),
                                       0, kNumGroupAmpSources - 1);
      int output_index = static_cast<int>(input(kOutputIndices + g)->at(0)[0]);
      poly_float* dest = output_index == constants::kGroupDirectOut ? direct_out : fx_out;
      bool used = producers_ == nullptr || producers_->isGroupUsed(g);

      const Output* source = input(kAmpSources + source_index)->source;
      const Output* level = input(kLevels + g)->source;
      const poly_float* audio = input(kGroupAudio + g)->source->buffer;
      // Engines routed straight to Direct Out still follow the group's amp source and level.
      const poly_float* direct_audio = input(kGroupDirectAudio + g)->source->buffer;

      fades_[g] = utils::maskLoad(fades_[g], 1.0f, note_on_mask | reset_mask);
      poly_float fade_step = utils::maskLoad(poly_float(0.0f), poly_float(fade_delta), fadeMask(source_index, released_mask, kill_mask));
      // A released voice on a one-shot LFO lives until the shape ends, even through silent parts of it.
      poly_float playing_out = poly_float(1.0f) & (unfinishedMask(source_index) & released_mask & ~kill_mask);

      // Control-rate sources and levels are ramped across the block, like SmoothMultiply does.
      poly_float source_end = utils::clamp(source->buffer[0], 0.0f, 1.0f);
      poly_float source_value = utils::maskLoad(last_sources_[g], source_end, reset_mask);
      poly_float source_delta = (source_end - source_value) * sample_inc;
      poly_float level_end = level->buffer[0];
      poly_float level_value = utils::maskLoad(last_levels_[g], level_end, reset_mask);
      poly_float level_delta = (level_end - level_value) * sample_inc;

      bool audio_rate_source = !isControlRateSource(source_index, source);
      bool audio_rate_level = !(level->isControlRate() || (level->owner && level->owner->isControlRate()));
      poly_float current_amplitude = start_amplitude;
      poly_float fade = fades_[g];

      // Smoothing is a one-pole glide on the amp source; a voice starts from silence so it fades in too.
      mono_float smoothing = std::max(0.0f, input(kSmoothings + g)->at(0)[0]);
      mono_float smoothing_seconds = smoothing * smoothing;
      mono_float smoothing_samples = smoothing_seconds * getSampleRate();
      poly_float smooth_coefficient = smoothing_samples > 1.0f ? 1.0f - std::exp(-1.0f / smoothing_samples) : 1.0f;
      poly_float smoothed = utils::maskLoad(smoothed_[g], 0.0f, reset_mask);

      for (int i = 0; i < num_samples; ++i) {
        source_value += source_delta;
        level_value += level_delta;
        current_amplitude += delta_amplitude;
        fade = utils::max(fade - fade_step, 0.0f);

        poly_float s = audio_rate_source ? utils::clamp(source->buffer[i], 0.0f, 1.0f) : source_value;
        smoothed += (s - smoothed) * smooth_coefficient;
        poly_float l = audio_rate_level ? level->buffer[i] : level_value;
        poly_float gain = smoothed * current_amplitude;
        poly_float total = (gain * gain * l * fade) & active_mask;
        dest[i] += audio[i] * total;
        direct_out[i] += direct_audio[i] * total;
        if (used)
          alive_out[i] = utils::max(alive_out[i], utils::max(total, playing_out));
      }

      fades_[g] = fade;
      smoothed_[g] = smoothed;
      last_sources_[g] = audio_rate_source ? utils::clamp(source->buffer[num_samples - 1], 0.0f, 1.0f) : source_end;
      last_levels_[g] = audio_rate_level ? level->buffer[num_samples - 1] : level_end;
    }
  }
} // namespace vital
