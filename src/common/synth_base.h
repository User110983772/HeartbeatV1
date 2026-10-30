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
#include "concurrentqueue/concurrentqueue.h"
#include "line_generator.h"
#include "synth_constants.h"
#include "synth_types.h"
#include "midi_manager.h"
#include "tuning.h"

#include <set>
#include "wavetable_creator.h"

#include <set>
#include <string>

namespace vital {
  class SoundEngine;
  struct Output;
  class StatusOutput;
  class StereoMemory;
  class Sample;
  class WaveFrame;
  class Wavetable;
}

class SynthGuiInterface;

class SynthBase : public MidiManager::Listener {
  public:
    static constexpr float kOutputWindowMinNote = 16.0f;
    static constexpr float kOutputWindowMaxNote = 128.0f;

    SynthBase();
    virtual ~SynthBase();

    void valueChanged(const std::string& name, vital::mono_float value);
    void valueChangedThroughMidi(const std::string& name, vital::mono_float value) override;
    void pitchWheelMidiChanged(vital::mono_float value) override;
    void modWheelMidiChanged(vital::mono_float value) override;
    void pitchWheelGuiChanged(vital::mono_float value);
    void modWheelGuiChanged(vital::mono_float value);
    void presetChangedThroughMidi(File preset) override;
    void valueChangedExternal(const std::string& name, vital::mono_float value);
    // Oscillators only hold their spectral buffers while on; runs on the message thread with processing locked.
    void updateOscillatorMemory();
    void valueChangedInternal(const std::string& name, vital::mono_float value);
    bool connectModulation(const std::string& source, const std::string& destination);
    void connectModulation(vital::ModulationConnection* connection);
    void disconnectModulation(const std::string& source, const std::string& destination);
    void disconnectModulation(vital::ModulationConnection* connection);
    void clearModulations();
    void forceShowModulation(const std::string& source, bool force);
    bool isModSourceEnabled(const std::string& source);
    int getNumModulations(const std::string& destination);
    int getConnectionIndex(const std::string& source, const std::string& destination);
    vital::CircularQueue<vital::ModulationConnection*> getModulationConnections() { return mod_connections_; }
    std::vector<vital::ModulationConnection*> getSourceConnections(const std::string& source);
    bool isSourceConnected(const std::string& source);
    std::vector<vital::ModulationConnection*> getDestinationConnections(const std::string& destination);

    const vital::StatusOutput* getStatusOutput(const std::string& name);

    vital::Wavetable* getWavetable(int index);
    WavetableCreator* getWavetableCreator(int index);
    vital::Sample* getSample(int index);
    vital::EngineOrder getEngineOrder();
    bool setEngineOrder(const vital::EngineOrder& order);

    // Which sound engines are in the voice tab's list and which groups it shows. Not host parameters.
    bool isEngineAdded(int engine) const { return engines_added_[engine]; }
    bool isGroupShown(int group) const { return groups_shown_[group]; }
    int getNumEnginesAdded() const;
    void setEngineLayout(const std::array<bool, vital::kNumOscillators>& engines_added,
                         const std::array<bool, vital::kNumOscillatorGroups>& groups_shown);
    // Adds a fresh engine (default settings, no modulations) at the bottom of `group`. Returns -1 if all are in use.
    int addSoundEngine(int group, int source);
    void removeSoundEngine(int engine);
    // Moves an engine into `group` before the engine at `position` in that group (or to the bottom).
    void moveSoundEngine(int engine, int group, int position);
    // Shows the first hidden group. Returns -1 if all are shown.
    int addGroup();
    // Removes the group's engines, resets its settings and hides it. The last shown group stays.
    void removeGroup(int group);
    int getNumGroupsShown() const;
    // Groups are numbered by where they sit, top to bottom: the top group is Group 1 whichever slot it uses.
    // 0 for a group that isn't shown.
    int getGroupDisplayNumber(int group) const;
    // An engine's name as shown: "OSCn" or "SMPn", numbered per type within its group from the top of the
    // chain down.
    std::string getEngineDisplayName(int engine);

    // Undo and redo for removing engines and groups. Each step restores exactly what was removed (settings,
    // wavetable, sample, modulations, position); other edits are left alone. Loading a patch clears both.
    // Undo and redo cover removed engines and groups, and loaded patches (a preset or init).
    bool undoRemoval();
    bool redoRemoval();
    void clearUndoHistory();

    // Undo steps recorded from the interface. Control changes to the same control in quick succession (a drag)
    // make one step, as do changes to several controls at once.
    void recordControlChange(const std::string& name, vital::mono_float old_value, vital::mono_float new_value);
    // Between these (a mouse press and its release anywhere in the interface), every change is one step: the
    // values before the press and after the release, however long the drag and however many controls it moves.
    void beginUndoGesture();
    void endUndoGesture();
    // A modulation's amount and switches, recorded for undo.
    void setModulationSettings(int index, vital::mono_float amount, bool bipolar, bool stereo, bool bypass);
    void recordConnection(const std::string& source, const std::string& destination);
    // Call before disconnecting, so the connection's settings can be restored.
    void recordDisconnection(const std::string& source, const std::string& destination);
    void recordAddition(bool is_group, int id);
    void recordMove(bool is_group, int id, int old_group, int old_position);
    int getEnginePosition(int engine);
    int getGroupPosition(int group);
    std::string getUndoRemovalName();
    std::string getRedoRemovalName();

    // Order the voice tab shows the groups in. Display only: groups play in parallel.
    using GroupOrder = std::array<int, vital::kNumOscillatorGroups>;
    const GroupOrder& getGroupOrder() const { return group_order_; }
    void setGroupOrder(const GroupOrder& order);
    // Moves a group to before the position-th other shown group (or after the last one).
    void moveGroup(int group, int position);
    // Copies an engine (settings, wavetable, sample, modulations) into a free slot at `position` in `group`.
    // FM/RM from the engine above is reset unless keep_engine_above_modulation. Returns -1 if none are free.
    int duplicateSoundEngine(int engine, int group, int position, bool keep_engine_above_modulation);
    // Copies a group with its settings, modulations and engines (keeping their FM/RM chain) to `position`.
    // Returns -1 without a free group or enough free engines.
    int duplicateGroup(int group, int position);
    LineGenerator* getLfoSource(int index);

    int getSampleRate();
    void initEngine();
    void loadTuningFile(const File& file);
    void loadInitPreset();
    bool loadFromFile(File preset, std::string& error);
    void renderAudioToFile(File file, float seconds, float bpm, std::vector<int> notes, bool render_images);
    void renderAudioForResynthesis(float* data, int samples, int note);
    bool saveToFile(File preset);
    bool saveToActiveFile();
    void clearActiveFile() { active_file_ = File(); }
    File getActiveFile() { return active_file_; }

    void setMpeEnabled(bool enabled);
    virtual void beginChangeGesture(const std::string& name) { }
    virtual void endChangeGesture(const std::string& name) { }
    virtual void setValueNotifyHost(const std::string& name, vital::mono_float value) { }

    void armMidiLearn(const std::string& name);
    void cancelMidiLearn();
    void clearMidiLearn(const std::string& name);
    bool isMidiMapped(const std::string& name);

    void setAuthor(const String& author);
    void setComments(const String& comments);
    void setStyle(const String& comments);
    void setPresetName(const String& preset_name);
    void setMacroName(int index, const String& macro_name);
    String getAuthor();
    String getComments();
    String getStyle();
    String getPresetName();
    String getMacroName(int index);

    vital::control_map& getControls() { return controls_; }
    vital::SoundEngine* getEngine() { return engine_.get(); }
    MidiKeyboardState* getKeyboardState() { return keyboard_state_.get(); }
    const vital::poly_float* getOscilloscopeMemory() { return oscilloscope_memory_; }
    const vital::StereoMemory* getAudioMemory() { return audio_memory_.get(); }
    const vital::StereoMemory* getEqualizerMemory();
    vital::ModulationConnectionBank& getModulationBank();
    void notifyOversamplingChanged();
    void checkOversampling();
    virtual const CriticalSection& getCriticalSection() = 0;
    virtual void pauseProcessing(bool pause) = 0;
    Tuning* getTuning() { return &tuning_; }

    // Builds the stretch pools sample engines need (STRETCH or SYNC on); message thread only.
    void prepareSampleStretch();
    void requestStretchPreparation();

    // Posted from the audio thread when a sample engine wants stretching before its pool exists.
    struct PrepareStretchCallback : public CallbackMessage {
      PrepareStretchCallback(std::shared_ptr<SynthBase*> listener) : listener(listener) { }
      void messageCallback() override {
        if (auto synth = listener.lock())
          (*synth)->prepareSampleStretch();
      }
      std::weak_ptr<SynthBase*> listener;
    };

    struct ValueChangedCallback : public CallbackMessage {
      ValueChangedCallback(std::shared_ptr<SynthBase*> listener, std::string name, vital::mono_float val) :
          listener(listener), control_name(std::move(name)), value(val) { }

      void messageCallback() override;

      std::weak_ptr<SynthBase*> listener;
      std::string control_name;
      vital::mono_float value;
    };

  protected:
    vital::modulation_change createModulationChange(vital::ModulationConnection* connection);
    bool isInvalidConnection(const vital::modulation_change& change);
    virtual SynthGuiInterface* getGuiInterface() = 0;
    json saveToJson();
    bool loadFromJson(const json& state);
    vital::ModulationConnection* getConnection(const std::string& source, const std::string& destination);

    inline bool getNextModulationChange(vital::modulation_change& change) {
      return modulation_change_queue_.try_dequeue_non_interleaved(change);
    }
  
    inline void clearModulationQueue() {
      vital::modulation_change change;
      while (modulation_change_queue_.try_dequeue_non_interleaved(change))
        ;
    }

    void processAudio(AudioSampleBuffer* buffer, int channels, int samples, int offset);
    void processAudioWithInput(AudioSampleBuffer* buffer, const vital::poly_float* input_buffer,
                               int channels, int samples, int offset);
    void writeAudio(AudioSampleBuffer* buffer, int channels, int samples, int offset);
    void processMidi(MidiBuffer& buffer, int start_sample = 0, int end_sample = 0);
    void processKeyboardEvents(MidiBuffer& buffer, int num_samples);
    void processModulationChanges();
    void updateMemoryOutput(int samples, const vital::poly_float* audio);

    std::unique_ptr<vital::SoundEngine> engine_;
    std::unique_ptr<MidiManager> midi_manager_;
    std::unique_ptr<MidiKeyboardState> keyboard_state_;

    std::unique_ptr<WavetableCreator> wavetable_creators_[vital::kNumOscillators];
    std::shared_ptr<SynthBase*> self_reference_;
    std::atomic<bool> stretch_prepare_posted_ { false };

    File active_file_;
    vital::poly_float oscilloscope_memory_[2 * vital::kOscilloscopeMemoryResolution];
    vital::poly_float oscilloscope_memory_write_[2 * vital::kOscilloscopeMemoryResolution];
    std::unique_ptr<vital::StereoMemory> audio_memory_;
    vital::mono_float last_played_note_;
    int last_num_pressed_;
    vital::mono_float memory_reset_period_;
    vital::mono_float memory_input_offset_;
    int memory_index_;
    bool expired_;

    std::map<std::string, String> save_info_;
    vital::control_map controls_;
    vital::CircularQueue<vital::ModulationConnection*> mod_connections_;
    std::array<bool, vital::kNumOscillators> engines_added_;
    std::array<bool, vital::kNumOscillatorGroups> groups_shown_;
    GroupOrder group_order_;

    struct ModulationSnapshot {
      std::string source;
      std::string destination_suffix;
      std::map<std::string, vital::mono_float> settings;
      json line_mapping;
    };

    struct EngineSnapshot {
      int engine;
      int group;
      int position;
      std::map<std::string, vital::mono_float> controls;
      json wavetable;
      json sample;
      std::vector<ModulationSnapshot> modulations;
    };

    // One undo step: a removed engine or group, the whole patch replaced by a load (is_patch), changed controls
    // (is_controls), a modulation connected or disconnected, an engine or group added (is_add) or moved (is_move).
    // On the redo stack, restores_on_redo marks an undone addition: redoing it restores the snapshot.
    struct RemovalSnapshot {
      bool is_controls = false;
      std::map<std::string, vital::mono_float> old_values;
      std::map<std::string, vital::mono_float> new_values;
      double time = 0.0;
      bool is_connect = false;
      bool is_disconnect = false;
      ModulationSnapshot connection;
      bool is_add = false;
      bool restores_on_redo = false;
      bool is_move = false;
      int old_group = 0;
      int old_position = 0;
      int new_group = 0;
      int new_position = 0;
      bool is_patch = false;
      json patch;
      std::string patch_name;
      std::string patch_file;
      std::string loaded_name;
      bool is_group;
      int id;
      int display_number;
      int group_position;
      std::map<std::string, vital::mono_float> group_controls;
      std::vector<ModulationSnapshot> group_modulations;
      std::vector<EngineSnapshot> engines;
    };

    void removeSoundEngineInternal(int engine);
    void removeGroupInternal(int group);
    std::map<std::string, vital::mono_float> captureControls(const std::string& prefix);
    void restoreControls(const std::string& prefix, const std::map<std::string, vital::mono_float>& values);
    std::vector<ModulationSnapshot> captureModulations(const std::string& prefix);
    void restoreModulations(const std::string& prefix, const std::vector<ModulationSnapshot>& modulations);
    EngineSnapshot captureEngine(int engine);
    int restoreEngine(const EngineSnapshot& snapshot, int group);
    RemovalSnapshot captureEngineRemoval(int engine);
    RemovalSnapshot captureGroupRemoval(int group);
    int restoreRemoval(const RemovalSnapshot& removal);
    static std::string removalName(const RemovalSnapshot& removal);
    RemovalSnapshot capturePatch();
    ModulationSnapshot captureConnection(const std::string& source, const std::string& destination);
    void pushUndoStep(const RemovalSnapshot& step);
    RemovalSnapshot applyStep(const RemovalSnapshot& step, bool undo);
    void applyControlValues(const std::map<std::string, vital::mono_float>& values);
    bool recording_suspended_ = false;
    bool gesture_active_ = false;
    // Started by a change made while a mouse button was down, before the press listener could start it.
    bool implicit_gesture_ = false;
    RemovalSnapshot gesture_step_;
    // Controls this gesture opened a host change gesture for, so the host's own undo also takes the drag as one
    // step (knobs open their own; editors like the EQ set their sliders directly).
    std::set<std::string> gesture_host_controls_;
    void restorePatch(const RemovalSnapshot& patch);
    void pushPatchUndo(RemovalSnapshot previous, const std::string& loaded_name);

    std::vector<RemovalSnapshot> undo_removals_;
    std::vector<RemovalSnapshot> redo_removals_;

    void copyControls(const std::string& from_prefix, const std::string& to_prefix);
    void copyModulations(const std::string& from_prefix, const std::string& to_prefix);
    moodycamel::ConcurrentQueue<vital::control_change> value_change_queue_;
    moodycamel::ConcurrentQueue<vital::modulation_change> modulation_change_queue_;
    Tuning tuning_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthBase)
};

class HeadlessSynth : public SynthBase {
  public:
    virtual const CriticalSection& getCriticalSection() override {
      return critical_section_;
    }

    virtual void pauseProcessing(bool pause) override {
      if (pause)
        critical_section_.enter();
      else
        critical_section_.exit();
    }

  protected:
    virtual SynthGuiInterface* getGuiInterface() override { return nullptr; }

  private:
    CriticalSection critical_section_;
};
