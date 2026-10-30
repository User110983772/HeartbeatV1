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

#include "sample_source.h"
#include "futils.h"
#include "synth_constants.h"
#include "synth_oscillator.h"

// Relative so the library's own includes resolve from its folder, with no extra include path in the builds.
#include "../../../third_party/signalsmith/signalsmith-stretch.h"

#include <cmath>

#include <thread>

namespace vital {

  namespace {
    const std::string kDefaultName = "White Noise";
    const mono_float kUpsampleCoefficients[SampleSource::kNumUpsampleTaps] = {
      -0.000159813115702086552469274316479186382f,
      0.000225405365781280835058009159865832771f,
      -0.000378616814007205900686342525673921955f,
      0.000594907533596884547516525643118256994f,
      -0.000890530515941817101682742574553230952f,
      0.001284040046393844676508866342601322685f,
      -0.001796543223638378920792302295694753411f,
      0.002451862103068884121692683208948437823f,
      -0.003276873018553504678107568537370752892f,
      0.004302012661141991003987961050825106213f,
      -0.005561976429934398571952591794342879439f,
      0.007097105459677621741576558633823879063f,
      -0.008955232561651555595050311353588767815f,
      0.011195057708851860467369476737076183781f,
      -0.013890548104646217864033275191104621626f,
      0.017139719620821350365424962092220084742f,
      -0.021077036318492142763503238711564335972f,
      0.025897497908177177783350941808748757467f,
      -0.03189749744607761616776997470878995955f,
      0.039555400754278852160084056777122896165f,
      -0.049699764879031965714162311087420675904f,
      0.063901297378209126476278356676630210131f,
      -0.08553732517833501081128133591846562922f,
      0.123410206086688845061871688812971115112f,
      -0.209837893291539345774765479291090741754f,
      0.63582677174146173815216798175242729485f,
      0.63582677174146173815216798175242729485f,
      -0.209837893291539345774765479291090741754f,
      0.123410206086688845061871688812971115112f,
      -0.08553732517833501081128133591846562922f,
      0.063901297378209126476278356676630210131f,
      -0.049699764879031965714162311087420675904f,
      0.039555400754278852160084056777122896165f,
      -0.03189749744607761616776997470878995955f,
      0.025897497908177177783350941808748757467f,
      -0.021077036318492142763503238711564335972f,
      0.017139719620821350365424962092220084742f,
      -0.013890548104646217864033275191104621626f,
      0.011195057708851860467369476737076183781f,
      -0.008955232561651555595050311353588767815f,
      0.007097105459677621741576558633823879063f,
      -0.005561976429934398571952591794342879439f,
      0.004302012661141991003987961050825106213f,
      -0.003276873018553504678107568537370752892f,
      0.002451862103068884121692683208948437823f,
      -0.001796543223638378920792302295694753411f,
      0.001284040046393844676508866342601322685f,
      -0.000890530515941817101682742574553230952f,
      0.000594907533596884547516525643118256994f,
      -0.000378616814007205900686342525673921955f,
      0.000225405365781280835058009159865832771f,
      -0.000159813115702086552469274316479186382f
    };

    const mono_float kDownsampleCoefficients[SampleSource::kNumDownsampleTaps] = {
      -0.0013796309221920304f,
      -0.0008322130675804714f,
      0.0030100376204235577f,
      0.00666031332700994f,
      0.0040620073330527315f,
      -0.003019073425031439f,
      -0.004450269579432283f,
      0.0030526281279541555f,
      0.007614361286489334f,
      -0.000546514301955849f,
      -0.010099270019478761f,
      -0.003465846383906444f,
      0.011760981765402261f,
      0.009402148654924303f,
      -0.011429260748035207f,
      -0.016935843679984037f,
      0.008026778073943279f,
      0.025557280950428782f,
      -0.0002093220301655805f,
      -0.03448379812688787f,
      -0.013983156365753766f,
      0.04279770831566429f,
      0.03889228625534586f,
      -0.049566024787935245f,
      -0.09025827224454164f,
      0.05398926693924448f,
      0.31285587793730246f,
      0.4444714418837066f,
      0.31285587793730246f,
      0.05398926693924448f,
      -0.09025827224454164f,
      -0.049566024787935245f,
      0.03889228625534586f,
      0.04279770831566429f,
      -0.013983156365753766f,
      -0.03448379812688787f,
      -0.0002093220301655805f,
      0.025557280950428782f,
      0.008026778073943279f,
      -0.016935843679984037f,
      -0.011429260748035207f,
      0.009402148654924303f,
      0.011760981765402261f,
      -0.003465846383906444f,
      -0.010099270019478761f,
      -0.000546514301955849f,
      0.007614361286489334f,
      0.0030526281279541555f,
      -0.004450269579432283f,
      -0.003019073425031439f,
      0.0040620073330527315f,
      0.00666031332700994f,
      0.0030100376204235577f,
      -0.0008322130675804714f,
      -0.0013796309221920304f
    };

    force_inline mono_float getFilteredSample(const mono_float* buffer, int index, int size) {
      int radius = SampleSource::kNumDownsampleTaps / 2;
      int start = std::max(0, index - radius);
      int end = std::min(size - 1, index + radius);
      mono_float total = 0.0f;
      for (int i = start; i <= end; ++i) {
        mono_float coefficient = kDownsampleCoefficients[i - index + radius];
        total += coefficient *  buffer[i];
      }
      return total;
    }

    force_inline mono_float getFilteredLoopSample(const mono_float* buffer, int index, int size) {
      int radius = SampleSource::kNumDownsampleTaps / 2;
      int start = index - radius;
      int end = index + radius;
      mono_float total = 0.0f;
      for (int i = start; i <= end; ++i) {
        int buffer_index = (i + size * radius) % size;
        mono_float coefficient = kDownsampleCoefficients[i - index + radius];
        total += coefficient * buffer[buffer_index];
      }
      return total;
    }


    force_inline mono_float getInterpolatedSample(const mono_float* buffer, int index, int size) {
      int radius = SampleSource::kNumUpsampleTaps / 2;
      int start = std::max(0, index - radius + 1);
      int end = std::min(size - 1, index + radius);
      mono_float total = 0.0f;
      for (int i = start; i <= end; ++i) {
        int coefficient_index = i - index + radius - 1;
        VITAL_ASSERT(coefficient_index >= 0 && coefficient_index < SampleSource::kNumUpsampleTaps);
        mono_float coefficient = kUpsampleCoefficients[coefficient_index];
        total += coefficient * buffer[i];
      }
      return total;
    }

    void upsample(const mono_float* original, mono_float* dest, int original_size, int dest_size) {
      for (int i = 0; i < original_size; ++i) {
        float value1 = original[i];
        float value2 = getInterpolatedSample(original, i, original_size);
        dest[2 * i] = value1;
        dest[2 * i + 1] = value2;
      }
    }

    void downsample(const mono_float* original, mono_float* dest, int original_size, int dest_size) {
      for (int i = 0; i < dest_size; ++i)
        dest[i] = getFilteredSample(original, 2 * i, original_size);
    }

    void downsampleLoop(const mono_float* original, mono_float* dest, int original_size, int dest_size) {
      for (int i = 0; i < dest_size; ++i)
        dest[i] = getFilteredLoopSample(original, 2 * i, original_size);
    }

    void createBandLimitedBuffers(std::vector<std::unique_ptr<mono_float[]>>& destination,
                                  std::vector<std::unique_ptr<mono_float[]>>& loop_destination,
                                  const mono_float* buffer, int size) {
      destination.push_back(std::make_unique<mono_float[]>(size + 2 * Sample::kBufferSamples));
      loop_destination.push_back(std::make_unique<mono_float[]>(size + 2 * Sample::kBufferSamples));

      mono_float* play_buffer = destination.back().get();
      mono_float* loop_buffer = loop_destination.back().get();
      memcpy(play_buffer + Sample::kBufferSamples, buffer, size * sizeof(mono_float));
      memcpy(loop_buffer + Sample::kBufferSamples, buffer, size * sizeof(mono_float));

      for (int i = 0; i < Sample::kBufferSamples; ++i) {
        play_buffer[i] = 0.0f;
        play_buffer[size + Sample::kBufferSamples + i] = 0.0f;

        loop_buffer[i] = loop_buffer[size + i];
        loop_buffer[size + Sample::kBufferSamples + i] = loop_buffer[Sample::kBufferSamples + i];
      }

      int current_size = size;
      for (int i = 0; i < Sample::kUpsampleTimes; ++i) {
        int upsampled_size = current_size * 2;
        int num_samples = upsampled_size + 2 * Sample::kBufferSamples;
        destination.insert(destination.begin(), std::make_unique<mono_float[]>(num_samples));
        loop_destination.insert(loop_destination.begin(), std::make_unique<mono_float[]>(num_samples));

        mono_float* next_buffer = destination.front().get();
        mono_float* next_loop_buffer = loop_destination.front().get();

        upsample(play_buffer + Sample::kBufferSamples, next_buffer + Sample::kBufferSamples,
                 current_size, upsampled_size);
        memcpy(next_loop_buffer, next_buffer, num_samples * sizeof(mono_float));
        current_size = upsampled_size;
      }

      play_buffer = destination.back().get();
      loop_buffer = loop_destination.back().get();
      current_size = size;

      while (current_size >= Sample::kMinSize) {
        int next_size = (current_size + 1) / 2;
        destination.push_back(std::make_unique<mono_float[]>(next_size + 2 * Sample::kBufferSamples));
        loop_destination.push_back(std::make_unique<mono_float[]>(next_size + 2 * Sample::kBufferSamples));

        mono_float* next_buffer = destination.back().get();
        mono_float* next_loop_buffer = loop_destination.back().get();

        downsample(play_buffer + Sample::kBufferSamples, next_buffer + Sample::kBufferSamples,
                   current_size, next_size);
        downsampleLoop(loop_buffer + Sample::kBufferSamples, next_loop_buffer + Sample::kBufferSamples,
                       current_size, next_size);

        for (int i = 0; i < Sample::kBufferSamples; ++i) {
          next_buffer[i] = 0.0f;
          next_buffer[next_size + Sample::kBufferSamples + i] = 0.0f;

          next_loop_buffer[i] = loop_buffer[next_size + i];
          next_loop_buffer[next_size + Sample::kBufferSamples + i] = next_loop_buffer[Sample::kBufferSamples + i];
        }

        play_buffer = next_buffer;
        loop_buffer = next_loop_buffer;
        current_size = next_size;
      }
    }
  }

  // The stretcher is configured for 48 kHz; at other rates its window just covers a little more or less time.
  namespace {
    constexpr int kStretchConfigRate = 48000;
    // Stretched output is kept in a ring: unison voices read it at their detune, up to kStretchAhead samples
    // ahead of the note (computed in advance) and the rest of the ring behind it.
    constexpr int kStretchRingSize = 1 << 14;
    constexpr int kStretchRingMask = kStretchRingSize - 1;
    constexpr int kStretchAhead = 4096;
    constexpr int kStretchChunk = 512;
    // A detuned reader that drifts to the edge of the ring crossfades back to the note's position over this.
    constexpr int kTapFadeSamples = 480;
    // A reader far from unison pitch (a stack's octaves and harmonics) starts each pass at least this far from
    // the edge it is heading for, rather than at the note's position.
    constexpr int kTapMinGrain = 4096;
    // Two per parallel-voice copy of the engine, with a spare pair.
    constexpr int kStretchSlots = 2 * (kMaxPolyphony / 2 + 2);
  }

  struct StretchSlot {
    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    std::vector<float> ring[2];
    std::vector<float> input[2];
    std::vector<float> output[2];
    // Where the note was in the sample for each input sample; the stretcher's output lines up with its input, so
    // this tells the display where the sound being heard comes from.
    std::vector<double> positions;
    long long read = 0;
    // Samples of stretched output written so far this note; the note's "now" is kStretchAhead behind.
    long long written = 0;
    long long present = 0;
    const void* owner = nullptr;
  };

  class StretchPool {
    public:
      StretchPool() : next_(0) {
        for (int i = 0; i < kStretchSlots; ++i) {
          std::unique_ptr<StretchSlot> slot = std::make_unique<StretchSlot>();
          slot->stretcher.presetCheaper(2, kStretchConfigRate);
          int scratch = std::max(kStretchChunk, slot->stretcher.outputSeekLength(1.0f));
          for (int c = 0; c < 2; ++c) {
            slot->ring[c].assign(kStretchRingSize, 0.0f);
            slot->positions.assign(kStretchRingSize, 0.0);
            slot->input[c].assign(scratch, 0.0f);
            slot->output[c].assign(scratch, 0.0f);
          }
          slots_.push_back(std::move(slot));
        }
      }

      StretchSlot* claim() {
        int index = next_.fetch_add(1);
        if (index >= static_cast<int>(slots_.size()))
          return nullptr;
        return slots_[index].get();
      }

    private:
      std::vector<std::unique_ptr<StretchSlot>> slots_;
      std::atomic<int> next_;
  };

  Sample::Sample() : name_(kDefaultName), current_data_(nullptr), active_audio_data_(nullptr),
                     stretch_pool_(nullptr), stretch_requested_(false) {
    init();
  }

  Sample::~Sample() = default;

  void Sample::prepareStretch() {
    if (stretch_pool_.load())
      return;
    owned_stretch_pool_ = std::make_unique<StretchPool>();
    stretch_pool_ = owned_stretch_pool_.get();
  }

  StretchSlot* Sample::claimStretchSlot() {
    StretchPool* pool = stretch_pool_.load();
    if (pool == nullptr) {
      stretch_requested_ = true;
      return nullptr;
    }
    return pool->claim();
  }

  void Sample::loadSample(const mono_float* buffer, int size, int sample_rate) {
    static constexpr int kMaxSize = 1764000;

    VITAL_ASSERT(active_audio_data_.is_lock_free());

    size = std::min(size, kMaxSize);
    std::unique_ptr<SampleData> old_data = std::move(data_);
    data_ = std::make_unique<SampleData>(size, sample_rate, false);
    createBandLimitedBuffers(data_->left_buffers, data_->left_loop_buffers, buffer, size);

    current_data_ = data_.get();
    while (active_audio_data_.load())
      std::this_thread::yield(); // Wait for audio thread to finish using old_data.
  }

  void Sample::loadSample(const mono_float* left_buffer, const mono_float* right_buffer, int size, int sample_rate) {
    std::unique_ptr<SampleData> old_data = std::move(data_);
    data_ = std::make_unique<SampleData>(size, sample_rate, true);
    createBandLimitedBuffers(data_->left_buffers, data_->left_loop_buffers, left_buffer, size);
    createBandLimitedBuffers(data_->right_buffers, data_->right_loop_buffers, right_buffer, size);

    current_data_ = data_.get();
    while (active_audio_data_.load())
      std::this_thread::yield(); // Wait for audio thread to finish using old_data.
  }

  void Sample::init() {
    name_ = kDefaultName;
    mono_float buffer[kDefaultSampleLength];
    utils::RandomGenerator random_generator(-0.9f, 0.9f);

    for (int i = 0; i < kDefaultSampleLength; ++i)
      buffer[i] = random_generator.next();

    loadSample(buffer, kDefaultSampleLength, kDefaultSampleRate);
  }

  json Sample::stateToJson() {
    json data;
    data["name"] = name_;
    data["length"] = data_->length;
    data["sample_rate"] = data_->sample_rate;
    std::unique_ptr<int16_t[]> pcm_data = std::make_unique<int16_t[]>(data_->length);
    utils::floatToPcmData(pcm_data.get(), data_->left_buffers[kUpsampleTimes].get(), data_->length);
    String encoded = Base64::toBase64(pcm_data.get(), sizeof(int16_t) * data_->length);
    data["samples"] = encoded.toStdString();
    if (data_->stereo) {
      utils::floatToPcmData(pcm_data.get(), data_->right_buffers[kUpsampleTimes].get(), data_->length);
      String encoded_stereo = Base64::toBase64(pcm_data.get(), sizeof(int16_t) * data_->length);
      data["samples_stereo"] = encoded_stereo.toStdString();
    }
    return data;
  }

  void Sample::jsonToState(json data) {
    name_ = "";
    if (data.count("name"))
      name_ = data["name"].get<std::string>();

    int length = data["length"];
    int sample_rate = data["sample_rate"];

    MemoryOutputStream decoded(length * sizeof(int16_t));
    std::string wave_data = data["samples"];
    Base64::convertFromBase64(decoded, wave_data);
    std::unique_ptr<int16_t[]> pcm_data = std::make_unique<int16_t[]>(length);
    memcpy(pcm_data.get(), decoded.getData(), length * sizeof(int16_t));
    std::unique_ptr<mono_float[]> buffer = std::make_unique<mono_float[]>(length);
    utils::pcmToFloatData(buffer.get(), pcm_data.get(), length);

    if (data.count("samples_stereo")) {
      MemoryOutputStream decoded_stereo(length * sizeof(int16_t));
      std::string wave_data_stereo = data["samples_stereo"];
      Base64::convertFromBase64(decoded_stereo, wave_data_stereo);
      std::unique_ptr<int16_t[]> pcm_data_stereo = std::make_unique<int16_t[]>(length);
      memcpy(pcm_data_stereo.get(), decoded_stereo.getData(), length * sizeof(int16_t));

      std::unique_ptr<mono_float[]> buffer_stereo = std::make_unique<mono_float[]>(length);
      utils::pcmToFloatData(buffer_stereo.get(), pcm_data_stereo.get(), length);
      loadSample(buffer.get(), buffer_stereo.get(), length, sample_rate);
    }
    else
      loadSample(buffer.get(), length, sample_rate);
  }

  SampleSource::SampleSource() : Processor(kNumInputs, kNumOutputs), pan_amplitude_(0.0f), phase_inc_(0.0f),
                                 random_generator_(0.0f, 1.0f), unison_(1), blend_stereo_multiply_(1.0f),
                                 blend_center_multiply_(0.0f) {
    transpose_quantize_ = 0;
    last_quantized_transpose_ = 0.0f;
    sample_index_ = 0;
    sample_fraction_ = 0.0f;

    sample_ = std::make_shared<Sample>();
    phase_output_ = std::make_shared<cr::Output>();
  }

  namespace {
    // Catmull-Rom between the 2nd and 3rd of four samples, read from a padded band-limited buffer.
    force_inline mono_float readSample(const mono_float* buffer, double position, int max_index) {
      int index = static_cast<int>(position);
      mono_float t = static_cast<mono_float>(position - index);
      index = std::max(0, std::min(index, max_index));
      mono_float p0 = buffer[index];
      mono_float p1 = buffer[index + 1];
      mono_float p2 = buffer[index + 2];
      mono_float p3 = buffer[index + 3];
      return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 +
                                             t * (3.0f * (p1 - p2) + p3 - p0)));
    }

  }

  void SampleSource::startHead(Head& head, double position, bool reverse, int wait) {
    head.direction = reverse ? -1 : 1;
    head.position = position;
    head.wait = wait;
    head.playing = true;
    head.started = true;
    head.fading = false;
    head.turning = false;
  }

  // One step of a head through its region: fills in where to read now, then moves on. Returns false once the
  // head has left the region (it doesn't loop), after which it plays nothing.
  mono_float SampleSource::endGain(double position, int direction, const Region& region, bool loop,
                                   double fade_frames) {
    // Not looping, frames left before the head stops: at END forwards and START backwards, or the edge of the
    // sample for a head START or END has been moved past (it plays on). A looping head jumps (or turns) before
    // reaching the edge, so it never fades here.
    if (loop)
      return 1.0f;
    double left = direction > 0 ? region.length - 1.0 - position : position;
    double to_end = direction > 0 ? region.end - position : position - region.start;
    if (to_end >= 0.0)
      left = std::min(left, to_end);
    if (left >= fade_frames)
      return 1.0f;
    // Quarter sine, so the fade leaves the full level without a kink.
    double t = std::max(0.0, left) / std::max(1e-9, fade_frames);
    return static_cast<mono_float>(std::sin(0.5 * kPi * t));
  }

  bool SampleSource::stepHead(Head& head, const Region& region, bool loop, bool bounce, double increment,
                              Step* step) {
    double position = head.position;
    step->position = position;
    step->fade = -1.0f;
    step->linear = false;
    if (loop && bounce)
      return bounceStep(head, region, increment, step);
    head.turning = false;

    // Crossfading into the other end of the loop as the head nears the point where it jumps. The fade's span
    // and the jump are fixed when the head enters it, so moving the loop or crossfade controls mid-fade can't
    // make the blend or the second read point jump; changes apply from the next pass.
    if (!loop)
      head.fading = false;
    // A head past the loop's far end (the markers moved under it, or the note started there) crossfades back into
    // the loop like one arriving at it.
    if (loop && !head.fading && region.crossfade > 0.0) {
      if (head.direction > 0 && position >= region.loop_end - region.crossfade) {
        // Always a full crossfade's length (also when the loop end was moved onto the head, which otherwise fades
        // in a few samples), but never past the end of the sample, where the head would stop.
        head.fade_from = position;
        head.fade_to = std::max(position, std::min(position + region.crossfade, region.length - 1.0));
        head.jump = head.fade_to - region.loop_start - region.crossfade;
        head.fading = true;
      }
      else if (head.direction < 0 && position <= region.loop_start + region.crossfade) {
        head.fade_from = position;
        head.fade_to = std::min(position, std::max(position - region.crossfade, 0.0));
        head.jump = region.loop_end - region.crossfade - head.fade_to;
        head.fading = true;
      }
    }
    if (head.fading) {
      double span = head.fade_to - head.fade_from;
      double fade = std::abs(span) > 1e-9 ? (position - head.fade_from) / span : 1.0;
      step->fade = static_cast<mono_float>(std::max(0.0, std::min(1.0, fade)));
      step->other = position - head.direction * head.jump;
    }

    double jump = region.loop_end - region.crossfade - region.loop_start;
    double next = position + increment * head.direction;
    if (head.fading) {
      if ((head.direction > 0 && next >= head.fade_to) || (head.direction < 0 && next <= head.fade_to)) {
        next -= head.direction * head.jump;
        head.fading = false;
      }
    }
    else if (loop && head.direction > 0 && next >= region.loop_end)
      next = std::max(region.loop_start, std::min(next - jump, region.loop_end));
    else if (loop && head.direction < 0 && next <= region.loop_start)
      next = std::max(region.loop_start, std::min(next + jump, region.loop_end));
    head.position = next;

    // Only passing END (forwards) or START (backwards) ends a note; one that START or END has moved past keeps
    // playing, so moving or modulating them (also differently per channel) can't cut it off.
    bool ended = !loop && (head.direction > 0 ? next >= region.end : next < region.start);
    if (ended || next < 0.0 || next >= region.length) {
      head.playing = false;
      return false;
    }
    return true;
  }

  bool SampleSource::bounceStep(Head& head, const Region& region, double increment, Step* step) {
    // A turn is fixed when the head comes within a crossfade of the end it's heading for: where it turns and over
    // how long it eases, so loop markers moved during it (even past the head) can't make it jump. Easing, the
    // head blends with its mirror image about the turning point, the same sample there, so the sound reverses
    // without flipping its slope in one sample. A head already past the end (moved under it) turns where it is.
    head.fading = false;
    double position = head.position;
    double next = position + increment * head.direction;
    if (!head.turning) {
      double edge = head.direction > 0 ? region.loop_end : region.loop_start;
      double distance = (edge - position) * head.direction;
      if (distance < region.crossfade || (next - edge) * head.direction >= 0.0) {
        head.turning = true;
        head.turned = false;
        head.turn_point = distance > 0.0 ? edge : position;
        head.turn_span = std::max(0.0, std::min(region.crossfade, distance));
      }
    }
    if (head.turning) {
      if (head.turn_span > 0.0) {
        double distance = std::abs(head.turn_point - position);
        step->other = 2.0 * head.turn_point - position;
        step->fade = static_cast<mono_float>(0.5 * (1.0 - std::min(1.0, distance / head.turn_span)));
        step->linear = true;
      }
      if (!head.turned && (next - head.turn_point) * head.direction >= 0.0) {
        next = 2.0 * head.turn_point - next;
        head.direction = -head.direction;
        head.turned = true;
      }
      else if (head.turned && std::abs(next - head.turn_point) >= head.turn_span)
        head.turning = false;
    }
    head.position = next;
    if (next < 0.0 || next >= region.length) {
      head.playing = false;
      return false;
    }
    return true;
  }

  void SampleSource::process(int num_samples) {
    sample_->markUsed();

    poly_float current_pan_amplitude = pan_amplitude_;
    poly_float input_pan = utils::clamp(input(kPan)->at(0), -1.0f, 1.0f);
    pan_amplitude_ = futils::panAmplitude(input_pan);

    poly_float input_midi = 0.0f;
    if (input(kKeytrack)->at(0)[0])
      input_midi = input(kMidi)->at(0) - kMidiTrackCenter;

    int transpose_quantize = static_cast<int>(input(kTransposeQuantize)->at(0)[0]);
    poly_float transpose = snapTranspose(input_midi, input(kTranspose)->at(0), transpose_quantize);
    transpose = utils::clamp(transpose + input(kTune)->at(0), kMinTranspose, kMaxTranspose);

    mono_float sample_rate_ratio = (1.0f * sample_->activeSampleRate()) / getSampleRate();
    double frames_per_sample = sample_rate_ratio * (1 << Sample::kUpsampleTimes);
    poly_float current_phase_inc = phase_inc_;
    phase_inc_ = utils::centsToRatio(transpose * kCentsPerNote) * frames_per_sample;

    int audio_length = sample_->activeLength();
    poly_mask reset_mask = getResetMask(kReset);
    poly_float reset_offset = utils::toFloat(input(kReset)->source->trigger_offset);
    current_pan_amplitude = utils::maskLoad(current_pan_amplitude, pan_amplitude_, reset_mask);
    current_phase_inc = utils::maskLoad(current_phase_inc, phase_inc_, reset_mask);

    // Unison, with the group's settings folded in exactly as for the oscillator.
    int mode = std::roundf(input(kGroupUnisonMode)->at(0)[0]);
    int engine_unison = 1, group_unison = 1;
    unison_ = SynthOscillator::computeGroupedUnisonCount(mode, std::roundf(input(kUnisonVoices)->at(0)[0]),
                                                         std::roundf(input(kGroupUnisonVoices)->at(0)[0]),
                                                         &engine_unison, &group_unison);
    int num_entries = (unison_ + 1) / 2;
    poly_float detunings[kMaxEntries];
    SynthOscillator::computeGroupedDetunings(mode, unison_, engine_unison, group_unison,
                                             input(kDetuneRange)->at(0), input(kDetunePower)->at(0),
                                             input(kUnisonDetune)->at(0), std::roundf(input(kStackStyle)->at(0)[0]),
                                             input(kGroupUnisonDetune)->at(0),
                                             std::roundf(input(kGroupStackStyle)->at(0)[0]), detunings);
    // Each voice's gain into its own channel and into the other (only Nested sends any across: it places each
    // voice itself; otherwise the channels are folded together afterwards by STEREO, as the oscillator does).
    nested_ = mode == constants::kGroupUnisonNested + 1;
    if (nested_) {
      poly_float own[kMaxEntries], cross[kMaxEntries];
      SynthOscillator::computeNestedMix(engine_unison, group_unison, input(kBlend)->at(0), input(kGroupBlend)->at(0),
                                        input(kStereoSpread)->at(0), input(kGroupStereoSpread)->at(0), own, cross);
      // cross[e] holds, per lane, what reaches it from the partner lane; here it's kept by the sending lane.
      for (int e = 0; e < kMaxEntries; ++e) {
        own_target_[e] = own[e];
        cross_target_[e] = utils::swapStereo(cross[e]);
      }
    }
    else {
      poly_float blend = SynthOscillator::combinedUnisonValue(mode, input(kBlend)->at(0), input(kGroupBlend)->at(0));
      poly_float center_amplitude, detuned_amplitude;
      SynthOscillator::computeUnisonAmplitudes(unison_, blend, &center_amplitude, &detuned_amplitude);
      for (int e = 0; e < kMaxEntries; ++e) {
        own_target_[e] = e == 0 ? center_amplitude : detuned_amplitude;
        cross_target_[e] = 0.0f;
      }
    }
    poly_float stereo_spread = nested_ ? poly_float(1.0f) :
        SynthOscillator::combinedUnisonValue(mode, input(kStereoSpread)->at(0), input(kGroupStereoSpread)->at(0));

    bool loop = input(kLoop)->at(0)[0] != 0.0f;
    bool bounce = loop && input(kBounce)->at(0)[0] != 0.0f;
    bool reverse = input(kReverse)->at(0)[0] != 0.0f;
    bool random_start = input(kRandomPhase)->at(0)[0] != 0.0f;
    bool tempo_sync = input(kTempoSync)->at(0)[0] != 0.0f;
    bool stretch = tempo_sync || input(kStretch)->at(0)[0] != 0.0f;
    // Synced, the sample plays at host tempo / its own tempo; stretched or not, at its own speed.
    double speed = 1.0;
    if (tempo_sync) {
      double host_bpm = 60.0 * input(kBeatsPerSecond)->at(0)[0];
      double sample_bpm = std::max(1.0f, input(kSampleBpm)->at(0)[0]);
      speed = host_bpm > 0.0 ? host_bpm / sample_bpm : 1.0;
    }

    poly_float start_input = utils::clamp(input(kStart)->at(0), 0.0f, 1.0f);
    poly_float end_input = utils::clamp(input(kEnd)->at(0), 0.0f, 1.0f);
    poly_float loop_start_input = utils::clamp(input(kLoopStart)->at(0), 0.0f, 1.0f);
    poly_float loop_end_input = utils::clamp(input(kLoopEnd)->at(0), 0.0f, 1.0f);
    poly_float crossfade_input = utils::clamp(input(kCrossfade)->at(0), 0.0f, 1.0f);
    poly_float start_spread = utils::clamp(input(kStartSpread)->at(0), 0.0f, 1.0f);
    poly_float loop_spread = utils::clamp(input(kLoopSpread)->at(0), 0.0f, 1.0f);
    poly_float loop_end_spread = utils::clamp(input(kLoopEndSpread)->at(0), 0.0f, 1.0f);

    double length = audio_length;
    mono_float sample_inc = 1.0f / num_samples;
    double end_fade_samples = kEndFadeSeconds * getSampleRate();
    poly_float delta_phase_inc = (phase_inc_ - current_phase_inc) * sample_inc;
    poly_float* raw_output = output(kRaw)->buffer;
    for (int i = 0; i < num_samples; ++i)
      raw_output[i] = 0.0f;

    // A lane's region: START..END (at least a minimal loop long), the loop kept inside it, and LOOP SPREAD
    // moving the loop later for the outer unison voices (t from 0 to 1), keeping its length.
    auto placementFor = [&](int lane, double t) {
      return placeVoice(start_input[lane], end_input[lane], loop_start_input[lane], loop_end_input[lane],
                        crossfade_input[lane], start_spread[lane], loop_spread[lane], loop_end_spread[lane], loop,
                        reverse, t, kMinLoopFrames / std::max(1.0, length));
    };
    // A lane's region, in upsampled frames: see placeVoice.
    auto regionFor = [&](int lane, double t) {
      Placement placement = placementFor(lane, t);
      Region region;
      region.length = length;
      region.start = placement.start * length;
      region.end = placement.end * length;
      region.loop_start = placement.loop_start * length;
      region.loop_end = placement.loop_end * length;
      region.crossfade = placement.crossfade * length;
      return region;
    };
    // Where a note begins: START forwards, END backwards, moved in by START SPREAD (and RANDOM), never past
    // where the voice's loop crossfade begins.
    auto startPosition = [&](int lane, const Region& region, double t) {
      Placement placement = placementFor(lane, t);
      double position = placement.voice_start * length;
      double limit = placement.start_limit * length;
      if (random_start) {
        double offset = random_generator_.next() * (region.end - region.start);
        position = reverse ? std::max(limit, position - offset) : std::min(limit, position + offset);
      }
      return std::max(0.0, std::min(position, length - 1.0));
    };

    // How far a unison voice's level moves per sample as voices are added or removed: over kVoiceFadeSeconds.
    mono_float gain_step = 1.0f / std::max(1.0f, kVoiceFadeSeconds * getSampleRate());

    for (int voice = 0; voice < kVoicesPerSource; ++voice) {
      int left = 2 * voice;
      if (stretch) {
        Region region = regionFor(left, 0.0);
        double increment = frames_per_sample * speed;
        // The stretcher pitches by the note's transpose; synced, it also undoes the pitch the speed change gives.
        double ratio = utils::centsToRatio(transpose[left] * kCentsPerNote) / speed;
        ratio = std::max(1.0 / 16.0, std::min(16.0, ratio));
        if (reset_mask[left]) {
          startHead(heads_[0][left], startPosition(left, region, 0.0), reverse, static_cast<int>(reset_offset[left]));
          stretched_note_[voice] = beginStretchedNote(voice, region, loop, bounce, increment, ratio);
        }
        if (stretched_note_[voice]) {
          processStretched(voice, num_samples, region, loop, bounce, increment, ratio, num_entries, detunings,
                           gain_step, reset_mask[left] != 0, raw_output);
          // Unstretched heads pick up from here if STRETCH is switched off mid-note.
          heads_[0][left + 1] = heads_[0][left];
          for (int lane = left; lane < left + 2; ++lane) {
            for (int e = 1; e < kMaxEntries; ++e)
              heads_[e][lane].started = false;
          }
          continue;
        }
      }
      else
        stretched_note_[voice] = false;

      for (int lane = left; lane < left + 2; ++lane) {
        bool lane_reset = reset_mask[lane] != 0;
        double start_inc = current_phase_inc[lane];
        double inc_delta = delta_phase_inc[lane];

        for (int e = 0; e < kMaxEntries; ++e) {
          Head& head = heads_[e][lane];
          // Voices beyond the current count fade out (with the detune and spread they had) rather than stop, and
          // voices added fade in, so scrubbing VOICES during a note doesn't click.
          bool active = e < num_entries;
          mono_float& gain = entry_gain_[e][lane];
          if (active) {
            entry_t_[e][lane] = num_entries > 1 ? e / (num_entries - 1.0) : 0.0;
            entry_detune_[e][lane] = detunings[e][lane];
          }
          double t = entry_t_[e][lane];
          double detune = entry_detune_[e][lane];
          mono_float target = active ? own_target_[e][lane] : 0.0f;
          mono_float cross_target = active ? cross_target_[e][lane] : 0.0f;
          mono_float& cross_gain = entry_cross_gain_[e][lane];
          Region region = regionFor(lane, t);

          // The note begins partway into this block; the head waits until then rather than starting early,
          // which put it outside the sample (and stopped it) when START was at an end.
          if (lane_reset) {
            gain = target;
            cross_gain = cross_target;
            if (active)
              startHead(head, startPosition(lane, region, t), reverse, static_cast<int>(reset_offset[lane]));
            else {
              head.playing = false;
              head.started = false;
            }
          }
          else if (active && e > 0 && !head.started && heads_[0][lane].playing) {
            // Unison raised during a note: the new voice joins where the first one is, silent at first.
            head = heads_[0][lane];
            head.started = true;
            head.fading = false;
            head.wait = 0;
            gain = 0.0f;
            cross_gain = 0.0f;
          }
          if (!active && gain <= 0.0f && cross_gain <= 0.0f) {
            head.playing = false;
            head.started = false;
            continue;
          }
          if (!head.playing)
            continue;

          int index = sample_->getActiveIndex(start_inc * detune);
          const mono_float* buffer = lane % 2 ? sample_->getActiveRightBuffer(index) :
                                                sample_->getActiveLeftBuffer(index);
          double phase_mult = 1.0 / (1 << index);
          int max_index = (audio_length >> index) + 3;
          auto read = [&](double position) { return readSample(buffer, position * phase_mult, max_index); };

          double inc = start_inc;
          for (int i = 0; i < num_samples; ++i) {
            inc += inc_delta;
            if (head.wait > 0) {
              head.wait--;
              continue;
            }
            gain += utils::clamp(target - gain, -gain_step, gain_step);
            cross_gain += utils::clamp(cross_target - cross_gain, -gain_step, gain_step);
            Step step;
            bool playing = stepHead(head, region, loop, bounce, inc * detune, &step);
            mono_float value = read(step.position);
            if (step.fade >= 0.0f) {
              mono_float into = read(step.other);
              value = blendStep(value, into, step);
            }
            value *= endGain(step.position, head.direction, region, loop, end_fade_samples * inc * detune);
            raw_output[i].set(lane, raw_output[i][lane] + gain * value);
            raw_output[i].set(lane ^ 1, raw_output[i][lane ^ 1] + cross_gain * value);
            if (!playing)
              break;
          }
          if (!active && gain <= 0.0f && cross_gain <= 0.0f) {
            head.playing = false;
            head.started = false;
          }
        }
      }
    }

    stereoBlend(raw_output, num_samples, reset_mask, stereo_spread);
    if (reset_mask.anyMask())
      clearOutputBufferForReset(reset_mask, kReset, kRaw);

    const poly_float* level_input = input(kLevel)->source->buffer;
    poly_float* levelled_output = output(kLevelled)->buffer;
    poly_float zero = 0.0f;
    poly_float max = kMaxAmplitude;
    poly_float delta_pan_amplitude = (pan_amplitude_ - current_pan_amplitude) * sample_inc;
    for (int i = 0; i < num_samples; ++i) {
      current_pan_amplitude += delta_pan_amplitude;
      poly_float level = utils::clamp(level_input[i], zero, max);
      levelled_output[i] = current_pan_amplitude * level * level * raw_output[i];
    }

    poly_float phase = 0.0f;
    for (int lane = 0; lane < poly_float::kSize; ++lane) {
      double position = heads_[0][lane].position;
      int voice = lane / 2;
      if (stretched_note_[voice])
        position = stretch_slots_[voice]->positions[stretch_slots_[voice]->present & kStretchRingMask];
      phase.set(lane, static_cast<mono_float>(position / std::max(1.0, length)));
    }
    phase_output_->buffer[0] = utils::encodePhaseAndVoice(phase, input(kNoteCount)->at(0));

    sample_->markUnused();
  }

  bool SampleSource::beginStretchedNote(int voice, const Region& region, bool loop, bool bounce,
                                        double increment, double transpose) {
    double end_fade_samples = kEndFadeSeconds * getSampleRate();
    StretchSlot*& slot = stretch_slots_[voice];
    // A copy of this processor (another voice) starts with its original's pointer; it needs its own.
    if (slot == nullptr || slot->owner != this) {
      slot = sample_->claimStretchSlot();
      if (slot == nullptr)
        return false;
      slot->owner = this;
    }

    int left = 2 * voice;
    Head& head = heads_[0][left];
    const mono_float* buffers[2] = { sample_->getActiveLeftBuffer(0), sample_->getActiveRightBuffer(0) };
    int max_index = sample_->activeLength() + 3;
    // Reads the next count samples of the note (both channels) into the slot's input.
    auto readAhead = [&](int count) {
      for (int i = 0; i < count; ++i) {
        float values[2] = { 0.0f, 0.0f };
        slot->positions[slot->read++ & kStretchRingMask] = head.position;
        if (head.playing) {
          Step step;
          stepHead(head, region, loop, bounce, increment, &step);
          for (int c = 0; c < 2; ++c) {
            mono_float value = readSample(buffers[c], step.position, max_index);
            if (step.fade >= 0.0f) {
              mono_float into = readSample(buffers[c], step.other, max_index);
              value = blendStep(value, into, step);
            }
            values[c] = value * endGain(step.position, head.direction, region, loop, end_fade_samples * increment);
          }
        }
        slot->input[0][i] = values[0];
        slot->input[1][i] = values[1];
      }
    };

    // The whole sample is in memory, so the stretcher is primed with what follows START and its output lines
    // up with the note (no latency); then kStretchAhead more is computed for detuned unison voices to read.
    slot->stretcher.reset();
    slot->stretcher.setTransposeFactor(transpose);
    slot->read = 0;
    int seek = slot->stretcher.outputSeekLength(1.0f);
    readAhead(seek);
    float* inputs[2] = { slot->input[0].data(), slot->input[1].data() };
    float* outputs[2] = { slot->output[0].data(), slot->output[1].data() };
    slot->stretcher.outputSeek(inputs, seek);
    slot->written = 0;
    for (int done = 0; done < kStretchAhead; done += kStretchChunk) {
      readAhead(kStretchChunk);
      slot->stretcher.process(inputs, kStretchChunk, outputs, kStretchChunk);
      for (int i = 0; i < kStretchChunk; ++i) {
        int index = static_cast<int>(slot->written++ & kStretchRingMask);
        slot->ring[0][index] = slot->output[0][i];
        slot->ring[1][index] = slot->output[1][i];
      }
    }
    slot->present = 0;
    for (int e = 0; e < kMaxEntries; ++e) {
      for (int lane = left; lane < left + 2; ++lane)
        taps_[e][lane] = Tap();
    }
    return true;
  }

  void SampleSource::processStretched(int voice, int num_samples, const Region& region, bool loop, bool bounce,
                                      double increment, double transpose, int num_entries,
                                      const poly_float* detunings, mono_float gain_step, bool note_start,
                                      poly_float* raw_output) {
    double end_fade_samples = kEndFadeSeconds * getSampleRate();
    StretchSlot* slot = stretch_slots_[voice];
    int left = 2 * voice;
    Head& head = heads_[0][left];
    int wait = std::min(num_samples, head.wait);
    head.wait = 0;
    int count = num_samples - wait;
    if (count <= 0)
      return;

    const mono_float* buffers[2] = { sample_->getActiveLeftBuffer(0), sample_->getActiveRightBuffer(0) };
    int max_index = sample_->activeLength() + 3;
    slot->stretcher.setTransposeFactor(transpose);
    float* inputs[2] = { slot->input[0].data(), slot->input[1].data() };
    float* outputs[2] = { slot->output[0].data(), slot->output[1].data() };

    // Feed the stretcher this block's worth of the note (silence once the head has stopped) and keep its output.
    for (int done = 0; done < count; done += kStretchChunk) {
      int chunk = std::min(kStretchChunk, count - done);
      for (int i = 0; i < chunk; ++i) {
        float values[2] = { 0.0f, 0.0f };
        slot->positions[slot->read++ & kStretchRingMask] = head.position;
        if (head.playing) {
          Step step;
          stepHead(head, region, loop, bounce, increment, &step);
          for (int c = 0; c < 2; ++c) {
            mono_float value = readSample(buffers[c], step.position, max_index);
            if (step.fade >= 0.0f) {
              mono_float into = readSample(buffers[c], step.other, max_index);
              value = blendStep(value, into, step);
            }
            values[c] = value * endGain(step.position, head.direction, region, loop, end_fade_samples * increment);
          }
        }
        slot->input[0][i] = values[0];
        slot->input[1][i] = values[1];
      }
      slot->stretcher.process(inputs, chunk, outputs, chunk);
      for (int i = 0; i < chunk; ++i) {
        int index = static_cast<int>(slot->written++ & kStretchRingMask);
        slot->ring[0][index] = slot->output[0][i];
        slot->ring[1][index] = slot->output[1][i];
      }
    }

    // Each unison voice reads the stretched stream at its detune. The centre voice (no detune) reads the note's
    // own position; detuned ones drift from it, and crossfade back to it before running off either end of the
    // ring.
    auto readRing = [&](int channel, double position) {
      long long index = static_cast<long long>(std::floor(position));
      float t = static_cast<float>(position - index);
      const std::vector<float>& ring = slot->ring[channel];
      float p0 = ring[(index - 1) & kStretchRingMask];
      float p1 = ring[index & kStretchRingMask];
      float p2 = ring[(index + 1) & kStretchRingMask];
      float p3 = ring[(index + 2) & kStretchRingMask];
      return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 +
                                             t * (3.0f * (p1 - p2) + p3 - p0)));
    };
    long long present = slot->present;
    // Readable, relative to the note's position: up to what the stretcher has written, and back as far as the
    // ring still holds (this block's output has already overwritten its oldest kMaxBufferSize samples).
    double ahead_limit = kStretchAhead - 4;
    double behind_limit = kStretchRingSize - kStretchAhead - kMaxBufferSize - 8;
    for (int e = 0; e < kMaxEntries; ++e) {
      for (int lane = left; lane < left + 2; ++lane) {
        // As unstretched: voices beyond the count fade out at their last detune, added ones fade in.
        Tap& tap = taps_[e][lane];
        bool active = e < num_entries;
        mono_float& gain = entry_gain_[e][lane];
        mono_float& cross_gain = entry_cross_gain_[e][lane];
        if (active)
          entry_detune_[e][lane] = detunings[e][lane];
        mono_float target = active ? own_target_[e][lane] : 0.0f;
        mono_float cross_target = active ? cross_target_[e][lane] : 0.0f;
        if (note_start) {
          gain = target;
          cross_gain = cross_target;
        }
        if (!active && gain <= 0.0f && cross_gain <= 0.0f) {
          tap = Tap();
          tap.read = static_cast<double>(present + count);
          continue;
        }
        int channel = lane % 2;
        double detune = entry_detune_[e][lane];
        // A reader moves away from the note's position at `rate` samples per sample. When it is about to leave the
        // readable part of the ring it crossfades to a new position. Both readers keep moving through the fade, so
        // it starts early enough that the old one is still readable at its end (reading past what's written gave
        // a click every pass), and is shortened only when a fast reader couldn't fit a whole fade in the ring.
        double rate = detune - 1.0;
        double speed = std::abs(rate);
        int fade_length = 1;
        double trigger_ahead = 0.0, trigger_behind = 0.0, restart = 0.0;
        // `behind` is how far back the ring holds this note: all of the ring once the note is long enough.
        auto plan = [&](double behind) {
          fade_length = static_cast<int>(std::max(1.0, std::min<double>(kTapFadeSamples,
                                                  (ahead_limit + behind) / (2.0 * speed + 1e-9))));
          double fade_travel = speed * fade_length;
          trigger_ahead = ahead_limit - fade_travel;
          trigger_behind = -behind + fade_travel;
          // The new pass starts at the note's position unless that leaves less than kTapMinGrain before the next
          // reset; then it starts further back (or ahead, for a slow reader), as far as the ring allows.
          double grain_travel = speed * (fade_length + kTapMinGrain);
          restart = 0.0;
          if (rate > 0.0)
            restart = std::max(-behind, std::min(0.0, ahead_limit - grain_travel));
          else if (rate < 0.0)
            restart = std::min(ahead_limit, std::max(0.0, -behind + grain_travel));
        };
        plan(behind_limit);
        for (int i = 0; i < count; ++i) {
          double now = static_cast<double>(present + i);
          // Early in the note the ring holds nothing from before it began (only the previous note).
          if (now - 2.0 < behind_limit)
            plan(std::max(0.0, now - 2.0));
          double drift = tap.read - now;
          if (tap.fade_left == 0 && (drift > trigger_ahead || drift < trigger_behind)) {
            tap.old_read = tap.read;
            tap.read = now + restart;
            tap.fade_left = fade_length;
            tap.fade_length = fade_length;
          }
          float value = readRing(channel, tap.read);
          if (tap.fade_left > 0) {
            float fade = tap.fade_left / static_cast<float>(tap.fade_length);
            float angle = (1.0f - fade) * (0.5f * kPi);
            value = value * std::sin(angle) + readRing(channel, tap.old_read) * std::cos(angle);
            tap.old_read += detune;
            tap.fade_left--;
          }
          tap.read += detune;
          gain += utils::clamp(target - gain, -gain_step, gain_step);
          cross_gain += utils::clamp(cross_target - cross_gain, -gain_step, gain_step);
          poly_float& out = raw_output[wait + i];
          out.set(lane, out[lane] + gain * value);
          out.set(lane ^ 1, out[lane ^ 1] + cross_gain * value);
        }
      }
    }
    slot->present = present + count;
  }

  void SampleSource::stereoBlend(poly_float* audio_out, int num_samples, poly_mask reset_mask,
                                 poly_float stereo_spread) {
    // As the oscillator does it: at full spread each unison voice stays in its own channel; less spread folds
    // the left and right voices together.
    stereo_spread = utils::clamp(stereo_spread, 0.0f, 1.0f);
    poly_float current_stereo_mult = blend_stereo_multiply_;
    poly_float current_center_mult = blend_center_multiply_;
    blend_stereo_multiply_ = futils::equalPowerFade(stereo_spread * 0.5f + 0.5f);
    blend_center_multiply_ = futils::equalPowerFadeInverse(stereo_spread * 0.5f + 0.5f);

    current_stereo_mult = utils::maskLoad(current_stereo_mult, blend_stereo_multiply_, reset_mask);
    current_center_mult = utils::maskLoad(current_center_mult, blend_center_multiply_, reset_mask);
    if (utils::equal(stereo_spread, 1.0f) && utils::equal(current_stereo_mult, blend_stereo_multiply_))
      return;

    poly_float delta_stereo_mult = (blend_stereo_multiply_ - current_stereo_mult) * (1.0f / num_samples);
    poly_float delta_center_mult = (blend_center_multiply_ - current_center_mult) * (1.0f / num_samples);
    for (int i = 0; i < num_samples; ++i) {
      current_stereo_mult += delta_stereo_mult;
      current_center_mult += delta_center_mult;
      poly_float value = audio_out[i];
      audio_out[i] = value * current_stereo_mult + utils::swapStereo(value) * current_center_mult;
    }
  }

  force_inline poly_float SampleSource::snapTranspose(poly_float input_midi, poly_float transpose, int quantize) {
    if (quantize == 0)
      return input_midi + transpose;

    bool global_transpose = utils::isTransposeQuantizeGlobal(quantize);
    poly_float pre_add = 0.0f;
    poly_float post_add = input_midi;
    if (global_transpose) {
      pre_add = input_midi;
      post_add = 0.0f;
    }

    poly_float snapped = utils::snapTranspose(pre_add + transpose, quantize);

    if (transpose_quantize_)
      phase_inc_ *= utils::noteOffsetToRatio(snapped - last_quantized_transpose_);

    last_quantized_transpose_ = snapped;
    transpose_quantize_ = quantize;
    return post_add + snapped;
  }
} // namespace vital
