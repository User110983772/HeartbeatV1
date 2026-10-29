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

#include "synth_base.h"

#include "sample_source.h"
#include "sound_engine.h"
#include "load_save.h"
#include "memory.h"
#include "modulation_connection_processor.h"
#include "startup.h"
#include "synth_gui_interface.h"
#include "synth_oscillator.h"
#include "synth_parameters.h"
#include "utils.h"

#include <algorithm>

SynthBase::SynthBase() : expired_(false) {
  expired_ = LoadSave::isExpired();
  self_reference_ = std::make_shared<SynthBase*>();
  *self_reference_ = this;

  engine_ = std::make_unique<vital::SoundEngine>();
  engine_->setTuning(&tuning_);

  mod_connections_.reserve(vital::kMaxModulationConnections);

  for (int i = 0; i < vital::kNumOscillators; ++i) {
    vital::Wavetable* wavetable = engine_->getWavetable(i);
    if (wavetable) {
      wavetable_creators_[i] = std::make_unique<WavetableCreator>(wavetable);
      wavetable_creators_[i]->init();
    }
  }

  keyboard_state_ = std::make_unique<MidiKeyboardState>();
  midi_manager_ = std::make_unique<MidiManager>(this, keyboard_state_.get(), &save_info_, this);

  last_played_note_ = 0.0f;
  last_num_pressed_ = 0;
  audio_memory_ = std::make_unique<vital::StereoMemory>(vital::kAudioMemorySamples);
  memset(oscilloscope_memory_, 0, 2 * vital::kOscilloscopeMemoryResolution * sizeof(vital::poly_float));
  memset(oscilloscope_memory_write_, 0, 2 * vital::kOscilloscopeMemoryResolution * sizeof(vital::poly_float));
  memory_reset_period_ = vital::kOscilloscopeMemoryResolution;
  memory_input_offset_ = 0;
  memory_index_ = 0;

  controls_ = engine_->getControls();
  engines_added_.fill(false);
  groups_shown_.fill(false);
  groups_shown_[0] = true;
  for (int g = 0; g < vital::kNumOscillatorGroups; ++g)
    group_order_[g] = g;

  Startup::doStartupChecks(midi_manager_.get());
}

SynthBase::~SynthBase() { }

namespace {
  // Controls that decide whether a sound engine needs its spectral buffers.
  bool isEngineMemoryControl(const std::string& name) {
    static const std::string kPrefix = "osc_";
    static const std::string kSuffixes[] = { "_on", "_source" };
    if (name.compare(0, kPrefix.size(), kPrefix) != 0)
      return false;
    for (const std::string& suffix : kSuffixes) {
      if (name.size() > kPrefix.size() + suffix.size() &&
          name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        return true;
    }
    return false;
  }
}

void SynthBase::valueChanged(const std::string& name, vital::mono_float value) {
  controls_[name]->set(value);
  // Changes from other threads post a ValueChangedCallback, which does this on the message thread.
  if (isEngineMemoryControl(name) && MessageManager::existsAndIsCurrentThread())
    updateOscillatorMemory();
}

void SynthBase::updateOscillatorMemory() {
  ScopedLock lock(getCriticalSection());
  engine_->updateOscillatorMemory();
}

void SynthBase::valueChangedInternal(const std::string& name, vital::mono_float value) {
  if (controls_.count(name))
    recordControlChange(name, controls_[name]->value(), value);
  valueChanged(name, value);
  setValueNotifyHost(name, value);
}

void SynthBase::valueChangedThroughMidi(const std::string& name, vital::mono_float value) {
  controls_[name]->set(value);
  ValueChangedCallback* callback = new ValueChangedCallback(self_reference_, name, value);
  setValueNotifyHost(name, value);
  callback->post();
}

void SynthBase::pitchWheelMidiChanged(vital::mono_float value) {
  ValueChangedCallback* callback = new ValueChangedCallback(self_reference_, "pitch_wheel", value);
  callback->post();
}

void SynthBase::modWheelMidiChanged(vital::mono_float value) {
  ValueChangedCallback* callback = new ValueChangedCallback(self_reference_, "mod_wheel", value);
  callback->post();
}

void SynthBase::pitchWheelGuiChanged(vital::mono_float value) {
  engine_->setZonedPitchWheel(value, 0, vital::kNumMidiChannels - 1);
}

void SynthBase::modWheelGuiChanged(vital::mono_float value) {
  engine_->setModWheelAllChannels(value);
}

void SynthBase::presetChangedThroughMidi(File preset) {
  SynthGuiInterface* gui_interface = getGuiInterface();
  if (gui_interface) {
    gui_interface->updateFullGui();
    gui_interface->notifyFresh();
  }
}

void SynthBase::valueChangedExternal(const std::string& name, vital::mono_float value) {
  valueChanged(name, value);
  if (name == "mod_wheel")
    engine_->setModWheelAllChannels(value);
  else if (name == "pitch_wheel")
    engine_->setZonedPitchWheel(value, 0, vital::kNumMidiChannels - 1);

  ValueChangedCallback* callback = new ValueChangedCallback(self_reference_, name, value);
  callback->post();
}

vital::ModulationConnection* SynthBase::getConnection(const std::string& source, const std::string& destination) {
  for (vital::ModulationConnection* connection : mod_connections_) {
    if (connection->source_name == source && connection->destination_name == destination)
      return connection;
  }
  return nullptr;
}

int SynthBase::getConnectionIndex(const std::string& source, const std::string& destination) {
  vital::ModulationConnectionBank& modulation_bank = getModulationBank();
  for (int i = 0; i < vital::kMaxModulationConnections; ++i) {
    vital::ModulationConnection* connection = modulation_bank.atIndex(i);
    if (connection->source_name == source && connection->destination_name == destination)
      return i;
  }
  return -1;
}

vital::modulation_change SynthBase::createModulationChange(vital::ModulationConnection* connection) {
  vital::modulation_change change;
  change.source = engine_->getModulationSource(connection->source_name);
  change.mono_destination = engine_->getMonoModulationDestination(connection->destination_name);
  change.mono_modulation_switch = engine_->getMonoModulationSwitch(connection->destination_name);
  VITAL_ASSERT(change.source != nullptr);
  VITAL_ASSERT(change.mono_destination != nullptr);
  VITAL_ASSERT(change.mono_modulation_switch != nullptr);

  change.destination_scale = vital::Parameters::getParameterRange(connection->destination_name);
  change.poly_modulation_switch = engine_->getPolyModulationSwitch(connection->destination_name);
  change.poly_destination = engine_->getPolyModulationDestination(connection->destination_name);
  change.modulation_processor = connection->modulation_processor.get();

  int num_audio_rate = 0;
  vital::ModulationConnectionBank& modulation_bank = getModulationBank();
  for (int i = 0; i < vital::kMaxModulationConnections; ++i) {
    if (modulation_bank.atIndex(i)->source_name == connection->source_name &&
        modulation_bank.atIndex(i)->destination_name != connection->destination_name &&
        !modulation_bank.atIndex(i)->modulation_processor->isControlRate()) {
      num_audio_rate++;
    }
  }
  change.num_audio_rate = num_audio_rate;
  return change;
}

bool SynthBase::isInvalidConnection(const vital::modulation_change& change) {
  return change.poly_destination && change.poly_destination->router() == change.modulation_processor;
}

void SynthBase::connectModulation(vital::ModulationConnection* connection) {
  vital::modulation_change change = createModulationChange(connection);
  if (isInvalidConnection(change)) {
    connection->destination_name = "";
    connection->source_name = "";
  }
  else if (mod_connections_.count(connection) == 0) {
    change.disconnecting = false;
    mod_connections_.push_back(connection);
    modulation_change_queue_.enqueue(change);
  }
}

bool SynthBase::connectModulation(const std::string& source, const std::string& destination) {
  vital::ModulationConnection* connection = getConnection(source, destination);
  bool create = connection == nullptr;
  if (create)
    connection = getModulationBank().createConnection(source, destination);

  if (connection)
    connectModulation(connection);

  return create;
}

void SynthBase::disconnectModulation(vital::ModulationConnection* connection) {
  if (mod_connections_.count(connection) == 0)
    return;

  vital::modulation_change change = createModulationChange(connection);
  connection->source_name = "";
  connection->destination_name = "";

  mod_connections_.remove(connection);
  change.disconnecting = true;
  modulation_change_queue_.enqueue(change);
}

void SynthBase::disconnectModulation(const std::string& source, const std::string& destination) {
  vital::ModulationConnection* connection = getConnection(source, destination);
  if (connection)
    disconnectModulation(connection);
}

void SynthBase::clearModulations() {
  clearModulationQueue();
  
  while (mod_connections_.size()) {
    vital::ModulationConnection* connection = *mod_connections_.begin();
    mod_connections_.remove(connection);
    vital::modulation_change change = createModulationChange(connection);
    change.disconnecting = true;
    engine_->disconnectModulation(change);
    connection->source_name = "";
    connection->destination_name = "";
  }

  int num_connections = static_cast<int>(getModulationBank().numConnections());
  for (int i = 0; i < num_connections; ++i)
    getModulationBank().atIndex(i)->modulation_processor->lineMapGenerator()->initLinear();

  engine_->disableUnnecessaryModSources();
}

void SynthBase::forceShowModulation(const std::string& source, bool force) {
  if (force)
    engine_->enableModSource(source);
  else if (!isSourceConnected(source))
    engine_->disableModSource(source);
}

bool SynthBase::isModSourceEnabled(const std::string& source) {
  return engine_->isModSourceEnabled(source);
}

int SynthBase::getNumModulations(const std::string& destination) {
  int connections = 0;
  for (vital::ModulationConnection* connection : mod_connections_) {
    if (connection->destination_name == destination)
      connections++;
  }
  return connections;
}

std::vector<vital::ModulationConnection*> SynthBase::getSourceConnections(const std::string& source) {
  std::vector<vital::ModulationConnection*> connections;
  for (vital::ModulationConnection* connection : mod_connections_) {
    if (connection->source_name == source)
      connections.push_back(connection);
  }
  return connections;
}

bool SynthBase::isSourceConnected(const std::string& source) {
  for (vital::ModulationConnection* connection : mod_connections_) {
    if (connection->source_name == source)
      return true;
  }
  return false;
}

std::vector<vital::ModulationConnection*> SynthBase::getDestinationConnections(const std::string& destination) {
  std::vector<vital::ModulationConnection*> connections;
  for (vital::ModulationConnection* connection : mod_connections_) {
    if (connection->destination_name == destination)
      connections.push_back(connection);
  }
  return connections;
}

const vital::StatusOutput* SynthBase::getStatusOutput(const std::string& name) {
  return engine_->getStatusOutput(name);
}

vital::Wavetable* SynthBase::getWavetable(int index) {
  return engine_->getWavetable(index);
}

WavetableCreator* SynthBase::getWavetableCreator(int index) {
  return wavetable_creators_[index].get();
}

vital::Sample* SynthBase::getSample(int index) {
  return engine_->getSample(index);
}

vital::EngineOrder SynthBase::getEngineOrder() {
  return engine_->getEngineOrder();
}

bool SynthBase::setEngineOrder(const vital::EngineOrder& order) {
  ScopedLock lock(getCriticalSection());
  return engine_->setEngineOrder(order);
}

namespace {
  std::string enginePrefix(int engine) {
    return "osc_" + std::to_string(engine + 1) + "_";
  }

  bool startsWith(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
  }
}

int SynthBase::getNumEnginesAdded() const {
  int total = 0;
  for (bool added : engines_added_)
    total += added ? 1 : 0;
  return total;
}

void SynthBase::setEngineLayout(const std::array<bool, vital::kNumOscillators>& engines_added,
                                const std::array<bool, vital::kNumOscillatorGroups>& groups_shown) {
  engines_added_ = engines_added;
  groups_shown_ = groups_shown;
  for (int i = 0; i < vital::kNumOscillators; ++i) {
    if (engines_added_[i]) {
      int group = static_cast<int>(controls_[enginePrefix(i) + "group"]->value());
      groups_shown_[vital::utils::iclamp(group, 0, vital::kNumOscillatorGroups - 1)] = true;
    }
  }
  if (getNumGroupsShown() == 0)
    groups_shown_[0] = true;
}

int SynthBase::getNumGroupsShown() const {
  int total = 0;
  for (bool shown : groups_shown_)
    total += shown ? 1 : 0;
  return total;
}

void SynthBase::removeGroupInternal(int group) {

  for (int i = 0; i < vital::kNumOscillators; ++i) {
    int engine_group = static_cast<int>(controls_[enginePrefix(i) + "group"]->value());
    if (engines_added_[i] && engine_group == group)
      removeSoundEngineInternal(i);
  }

  // A group shown again later starts from its defaults.
  std::string prefix = "group_" + std::to_string(group + 1) + "_";
  for (auto& control : controls_) {
    if (startsWith(control.first, prefix)) {
      vital::mono_float default_value = vital::Parameters::getDetails(control.first).default_value;
      {
        ScopedLock lock(getCriticalSection());
        control.second->set(default_value);
      }
      setValueNotifyHost(control.first, default_value);
    }
  }
  groups_shown_[group] = false;
}

void SynthBase::removeSoundEngine(int engine) {
  if (!engines_added_[engine])
    return;
  undo_removals_.push_back(captureEngineRemoval(engine));
  redo_removals_.clear();
  removeSoundEngineInternal(engine);
}

void SynthBase::removeGroup(int group) {
  if (!groups_shown_[group] || getNumGroupsShown() <= 1)
    return;
  undo_removals_.push_back(captureGroupRemoval(group));
  redo_removals_.clear();
  removeGroupInternal(group);
}

std::vector<SynthBase::ModulationSnapshot> SynthBase::captureModulations(const std::string& prefix) {
  std::vector<ModulationSnapshot> snapshots;
  vital::ModulationConnectionBank& bank = getModulationBank();
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (!startsWith(connection->destination_name, prefix))
      continue;
    int index = getConnectionIndex(connection->source_name, connection->destination_name);
    if (index < 0)
      continue;

    ModulationSnapshot snapshot;
    snapshot.source = connection->source_name;
    snapshot.destination_suffix = connection->destination_name.substr(prefix.size());
    for (const std::string& setting : { "amount", "power", "bipolar", "stereo", "bypass" }) {
      std::string name = "modulation_" + std::to_string(index + 1) + "_" + setting;
      if (controls_.count(name))
        snapshot.settings[setting] = controls_[name]->value();
    }
    snapshot.line_mapping = bank.atIndex(index)->modulation_processor->lineMapGenerator()->stateToJson();
    snapshots.push_back(snapshot);
  }
  return snapshots;
}

void SynthBase::restoreModulations(const std::string& prefix, const std::vector<ModulationSnapshot>& modulations) {
  vital::ModulationConnectionBank& bank = getModulationBank();
  for (const ModulationSnapshot& modulation : modulations) {
    std::string destination = prefix + modulation.destination_suffix;
    connectModulation(modulation.source, destination);
    int index = getConnectionIndex(modulation.source, destination);
    if (index < 0)
      continue;

    for (const auto& setting : modulation.settings) {
      std::string name = "modulation_" + std::to_string(index + 1) + "_" + setting.first;
      {
        ScopedLock lock(getCriticalSection());
        controls_[name]->set(setting.second);
      }
      setValueNotifyHost(name, setting.second);
    }
    bank.atIndex(index)->modulation_processor->lineMapGenerator()->jsonToState(modulation.line_mapping);
  }
}

void SynthBase::restoreControls(const std::string& prefix, const std::map<std::string, vital::mono_float>& values) {
  {
    ScopedLock lock(getCriticalSection());
    for (const auto& value : values) {
      if (controls_.count(prefix + value.first))
        controls_[prefix + value.first]->set(value.second);
    }
  }
  for (const auto& value : values) {
    if (controls_.count(prefix + value.first))
      setValueNotifyHost(prefix + value.first, value.second);
  }
}

std::map<std::string, vital::mono_float> SynthBase::captureControls(const std::string& prefix) {
  std::map<std::string, vital::mono_float> values;
  for (auto& control : controls_) {
    if (startsWith(control.first, prefix))
      values[control.first.substr(prefix.size())] = control.second->value();
  }
  return values;
}

SynthBase::EngineSnapshot SynthBase::captureEngine(int engine) {
  EngineSnapshot snapshot;
  std::string prefix = enginePrefix(engine);
  snapshot.engine = engine;
  snapshot.group = static_cast<int>(controls_[prefix + "group"]->value());
  snapshot.position = 0;
  for (int index : getEngineOrder()) {
    if (index == engine)
      break;
    if (engines_added_[index] && static_cast<int>(controls_[enginePrefix(index) + "group"]->value()) == snapshot.group)
      snapshot.position++;
  }
  snapshot.controls = captureControls(prefix);
  snapshot.wavetable = wavetable_creators_[engine]->stateToJson();
  snapshot.sample = engine_->getSample(engine)->stateToJson();
  snapshot.modulations = captureModulations(prefix);
  return snapshot;
}

int SynthBase::restoreEngine(const EngineSnapshot& snapshot, int group) {
  int engine = engines_added_[snapshot.engine] ? -1 : snapshot.engine;
  for (int i = 0; i < vital::kNumOscillators && engine < 0; ++i) {
    if (!engines_added_[i])
      engine = i;
  }
  if (engine < 0)
    return -1;

  std::string prefix = enginePrefix(engine);
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, prefix))
      disconnectModulation(connection);
  }
  restoreControls(prefix, snapshot.controls);
  wavetable_creators_[engine]->jsonToState(snapshot.wavetable);
  wavetable_creators_[engine]->render();
  engine_->getSample(engine)->jsonToState(snapshot.sample);
  restoreModulations(prefix, snapshot.modulations);

  engines_added_[engine] = true;
  moveSoundEngine(engine, group, snapshot.position);
  updateOscillatorMemory();
  return engine;
}

SynthBase::RemovalSnapshot SynthBase::captureEngineRemoval(int engine) {
  RemovalSnapshot removal;
  removal.is_group = false;
  removal.id = engine;
  removal.display_number = 0;
  removal.group_position = 0;
  removal.engines.push_back(captureEngine(engine));
  return removal;
}

SynthBase::RemovalSnapshot SynthBase::captureGroupRemoval(int group) {
  RemovalSnapshot removal;
  removal.is_group = true;
  removal.id = group;
  removal.display_number = getGroupDisplayNumber(group);
  removal.group_position = 0;
  for (int g : group_order_) {
    if (g == group)
      break;
    if (groups_shown_[g])
      removal.group_position++;
  }

  std::string prefix = "group_" + std::to_string(group + 1) + "_";
  removal.group_controls = captureControls(prefix);
  removal.group_modulations = captureModulations(prefix);
  for (int engine : getEngineOrder()) {
    if (engines_added_[engine] && static_cast<int>(controls_[enginePrefix(engine) + "group"]->value()) == group)
      removal.engines.push_back(captureEngine(engine));
  }
  return removal;
}

int SynthBase::restoreRemoval(const RemovalSnapshot& removal) {
  if (!removal.is_group) {
    const EngineSnapshot& engine = removal.engines.front();
    groups_shown_[engine.group] = true;
    return restoreEngine(engine, engine.group);
  }

  int needed = static_cast<int>(removal.engines.size());
  if (getNumEnginesAdded() + needed > vital::kNumOscillators)
    return -1;
  int group = groups_shown_[removal.id] ? -1 : removal.id;
  for (int g = 0; g < vital::kNumOscillatorGroups && group < 0; ++g) {
    if (!groups_shown_[g])
      group = g;
  }
  if (group < 0)
    return -1;

  std::string prefix = "group_" + std::to_string(group + 1) + "_";
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, prefix))
      disconnectModulation(connection);
  }
  restoreControls(prefix, removal.group_controls);
  restoreModulations(prefix, removal.group_modulations);
  groups_shown_[group] = true;
  for (const EngineSnapshot& engine : removal.engines)
    restoreEngine(engine, group);
  moveGroup(group, removal.group_position);
  return group;
}

int SynthBase::getGroupDisplayNumber(int group) const {
  int number = 0;
  for (int g : group_order_) {
    if (!groups_shown_[g])
      continue;
    number++;
    if (g == group)
      return number;
  }
  return 0;
}

std::string SynthBase::removalName(const RemovalSnapshot& removal) {
  if (removal.is_patch)
    return "Load " + removal.loaded_name;
  if (removal.is_controls) {
    if (removal.new_values.size() == 1) {
      const std::string& name = removal.new_values.begin()->first;
      return "Change " + (vital::Parameters::isParameter(name) ? vital::Parameters::getDisplayName(name) : name);
    }
    return "Change Parameters";
  }
  if (removal.is_connect || removal.is_disconnect)
    return std::string(removal.is_connect ? "Connect " : "Disconnect ") + removal.connection.source + " to " +
           removal.connection.destination_suffix;
  if (removal.is_move)
    return removal.is_group ? "Move Group" : "Move Sound Engine " + std::to_string(removal.id + 1);
  if (removal.is_add || removal.restores_on_redo) {
    if (removal.is_group)
      return "Add Group " + std::to_string(removal.display_number);
    return "Add Sound Engine " + std::to_string(removal.id + 1);
  }
  if (removal.is_group)
    return "Remove Group " + std::to_string(removal.display_number);
  return "Remove Sound Engine " + std::to_string(removal.id + 1);
}

void SynthBase::clearUndoHistory() {
  undo_removals_.clear();
  redo_removals_.clear();
}

void SynthBase::pushUndoStep(const RemovalSnapshot& step) {
  // Patch steps hold a whole patch, so the history is bounded.
  static constexpr int kMaxUndoSteps = 200;
  undo_removals_.push_back(step);
  if (static_cast<int>(undo_removals_.size()) > kMaxUndoSteps)
    undo_removals_.erase(undo_removals_.begin());
  redo_removals_.clear();
}

void SynthBase::recordControlChange(const std::string& name, vital::mono_float old_value, vital::mono_float new_value) {
  // A drag sends a stream of changes to one control; several controls changed together (a preset-like selector)
  // arrive within a few milliseconds. Each becomes one step.
  static constexpr double kSameControlMs = 1000.0;
  static constexpr double kTogetherMs = 30.0;
  if (recording_suspended_ || old_value == new_value || name == "pitch_wheel" || name == "mod_wheel")
    return;

  double now = Time::getMillisecondCounterHiRes();
  if (!undo_removals_.empty() && undo_removals_.back().is_controls) {
    RemovalSnapshot& last = undo_removals_.back();
    bool same_control = last.new_values.size() == 1 && last.new_values.count(name);
    if ((same_control && now - last.time < kSameControlMs) || now - last.time < kTogetherMs) {
      if (last.old_values.count(name) == 0)
        last.old_values[name] = old_value;
      last.new_values[name] = new_value;
      last.time = now;
      redo_removals_.clear();
      return;
    }
  }

  RemovalSnapshot step;
  step.is_controls = true;
  step.old_values[name] = old_value;
  step.new_values[name] = new_value;
  step.time = now;
  step.is_group = false;
  step.id = -1;
  step.display_number = 0;
  step.group_position = 0;
  pushUndoStep(step);
}

SynthBase::ModulationSnapshot SynthBase::captureConnection(const std::string& source, const std::string& destination) {
  for (const ModulationSnapshot& snapshot : captureModulations(destination)) {
    if (snapshot.source == source && snapshot.destination_suffix.empty()) {
      ModulationSnapshot result = snapshot;
      result.destination_suffix = destination;
      return result;
    }
  }
  ModulationSnapshot empty;
  empty.source = source;
  empty.destination_suffix = destination;
  return empty;
}

void SynthBase::recordConnection(const std::string& source, const std::string& destination) {
  if (recording_suspended_)
    return;
  RemovalSnapshot step;
  step.is_connect = true;
  step.connection = captureConnection(source, destination);
  step.is_group = false;
  step.id = -1;
  step.display_number = 0;
  step.group_position = 0;
  pushUndoStep(step);
}

void SynthBase::recordDisconnection(const std::string& source, const std::string& destination) {
  if (recording_suspended_ || getConnection(source, destination) == nullptr)
    return;
  RemovalSnapshot step;
  step.is_disconnect = true;
  step.connection = captureConnection(source, destination);
  step.is_group = false;
  step.id = -1;
  step.display_number = 0;
  step.group_position = 0;
  pushUndoStep(step);
}

void SynthBase::recordAddition(bool is_group, int id) {
  if (recording_suspended_ || id < 0)
    return;
  RemovalSnapshot step;
  step.is_add = true;
  step.is_group = is_group;
  step.id = id;
  step.display_number = is_group ? getGroupDisplayNumber(id) : 0;
  step.group_position = 0;
  pushUndoStep(step);
}

int SynthBase::getEnginePosition(int engine) {
  return captureEngine(engine).position;
}

int SynthBase::getGroupPosition(int group) {
  int position = 0;
  for (int g : group_order_) {
    if (g == group)
      break;
    if (groups_shown_[g])
      position++;
  }
  return position;
}

void SynthBase::recordMove(bool is_group, int id, int old_group, int old_position) {
  if (recording_suspended_ || id < 0)
    return;
  RemovalSnapshot step;
  step.is_move = true;
  step.is_group = is_group;
  step.id = id;
  step.old_group = old_group;
  step.old_position = old_position;
  step.new_group = is_group ? 0 : static_cast<int>(controls_[enginePrefix(id) + "group"]->value());
  step.new_position = is_group ? getGroupPosition(id) : getEnginePosition(id);
  if (step.old_group == step.new_group && step.old_position == step.new_position)
    return;
  step.display_number = 0;
  step.group_position = 0;
  pushUndoStep(step);
}

void SynthBase::applyControlValues(const std::map<std::string, vital::mono_float>& values) {
  for (const auto& value : values) {
    if (controls_.count(value.first) == 0)
      continue;
    valueChanged(value.first, value.second);
    setValueNotifyHost(value.first, value.second);
  }
}

SynthBase::RemovalSnapshot SynthBase::applyStep(const RemovalSnapshot& step, bool undo) {
  // Returns the step to push on the other stack.
  RemovalSnapshot other = step;
  if (step.is_controls)
    applyControlValues(undo ? step.old_values : step.new_values);
  else if (step.is_connect || step.is_disconnect) {
    bool reconnect = step.is_connect != undo;
    const std::string& source = step.connection.source;
    const std::string& destination = step.connection.destination_suffix;
    if (reconnect)
      restoreModulations("", { step.connection });
    else {
      other.connection = captureConnection(source, destination);
      disconnectModulation(source, destination);
    }
  }
  else if (step.is_move) {
    if (step.is_group)
      moveGroup(step.id, undo ? step.old_position : step.new_position);
    else
      moveSoundEngine(step.id, undo ? step.old_group : step.new_group, undo ? step.old_position : step.new_position);
  }
  else if (step.is_add) {
    // Undoing an addition removes it; the snapshot lets redo bring it back as it was.
    other = step.is_group ? captureGroupRemoval(step.id) : captureEngineRemoval(step.id);
    other.restores_on_redo = true;
    if (step.is_group)
      removeGroupInternal(step.id);
    else
      removeSoundEngineInternal(step.id);
  }
  else if (step.restores_on_redo) {
    int id = restoreRemoval(step);
    other = RemovalSnapshot();
    other.is_add = true;
    other.is_group = step.is_group;
    other.id = id;
    other.display_number = step.is_group ? getGroupDisplayNumber(id) : 0;
    other.group_position = 0;
  }
  return other;
}

SynthBase::RemovalSnapshot SynthBase::capturePatch() {
  RemovalSnapshot patch;
  patch.is_patch = true;
  patch.patch = saveToJson();
  patch.patch_name = getPresetName().toStdString();
  patch.patch_file = active_file_.getFullPathName().toStdString();
  patch.is_group = false;
  patch.id = -1;
  patch.display_number = 0;
  patch.group_position = 0;
  return patch;
}

void SynthBase::restorePatch(const RemovalSnapshot& patch) {
  loadFromJson(patch.patch);
  setPresetName(patch.patch_name);
  active_file_ = patch.patch_file.empty() ? File() : File(patch.patch_file);
}

void SynthBase::pushPatchUndo(RemovalSnapshot previous, const std::string& loaded_name) {
  previous.loaded_name = loaded_name;
  pushUndoStep(previous);
}

std::string SynthBase::getUndoRemovalName() {
  return undo_removals_.empty() ? "" : removalName(undo_removals_.back());
}

std::string SynthBase::getRedoRemovalName() {
  return redo_removals_.empty() ? "" : removalName(redo_removals_.back());
}

bool SynthBase::undoRemoval() {
  if (undo_removals_.empty())
    return false;

  RemovalSnapshot removal = undo_removals_.back();
  if (removal.is_controls || removal.is_connect || removal.is_disconnect || removal.is_move || removal.is_add) {
    undo_removals_.pop_back();
    recording_suspended_ = true;
    RemovalSnapshot other = applyStep(removal, true);
    recording_suspended_ = false;
    redo_removals_.push_back(other);
    return true;
  }
  if (removal.is_patch) {
    // Swap the patch for the one it replaced; redo swaps back.
    RemovalSnapshot current = capturePatch();
    current.loaded_name = removal.loaded_name;
    undo_removals_.pop_back();
    restorePatch(removal);
    redo_removals_.push_back(current);
    return true;
  }

  int id = restoreRemoval(removal);
  if (id < 0)
    return false;

  undo_removals_.pop_back();
  removal.id = id;
  redo_removals_.push_back(removal);
  return true;
}

bool SynthBase::redoRemoval() {
  if (redo_removals_.empty())
    return false;

  RemovalSnapshot removal = redo_removals_.back();
  redo_removals_.pop_back();
  if (removal.is_controls || removal.is_connect || removal.is_disconnect || removal.is_move ||
      removal.restores_on_redo) {
    recording_suspended_ = true;
    RemovalSnapshot other = applyStep(removal, false);
    recording_suspended_ = false;
    undo_removals_.push_back(other);
    return true;
  }
  if (removal.is_patch) {
    RemovalSnapshot current = capturePatch();
    current.loaded_name = removal.loaded_name;
    restorePatch(removal);
    undo_removals_.push_back(current);
    return true;
  }
  // Whatever was restored may have been edited since, so the new undo step captures it as it is now.
  if (removal.is_group) {
    if (!groups_shown_[removal.id] || getNumGroupsShown() <= 1)
      return false;
    undo_removals_.push_back(captureGroupRemoval(removal.id));
    removeGroupInternal(removal.id);
  }
  else {
    if (!engines_added_[removal.id])
      return false;
    undo_removals_.push_back(captureEngineRemoval(removal.id));
    removeSoundEngineInternal(removal.id);
  }
  return true;
}

int SynthBase::addSoundEngine(int group, int source) {
  int engine = -1;
  for (int i = 0; i < vital::kNumOscillators && engine < 0; ++i) {
    if (!engines_added_[i])
      engine = i;
  }
  if (engine < 0)
    return -1;

  std::string prefix = enginePrefix(engine);
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, prefix))
      disconnectModulation(connection);
  }

  std::vector<std::string> changes;
  {
    ScopedLock lock(getCriticalSection());
    for (auto& control : controls_) {
      if (startsWith(control.first, prefix)) {
        control.second->set(vital::Parameters::getDetails(control.first).default_value);
        changes.push_back(control.first);
      }
    }
    controls_[prefix + "group"]->set(group);
    controls_[prefix + "source"]->set(source);
    if (source == vital::SynthOscillator::kSampleSource)
      controls_[prefix + "random_phase"]->set(0.0f);
    controls_[prefix + "on"]->set(1.0f);

    vital::EngineOrder order = engine_->getEngineOrder();
    std::rotate(std::find(order.begin(), order.end(), engine), std::find(order.begin(), order.end(), engine) + 1,
                order.end());
    engine_->setEngineOrder(order);
  }

  for (const std::string& name : changes)
    setValueNotifyHost(name, controls_[name]->value());

  wavetable_creators_[engine]->init();
  engine_->getSample(engine)->init();
  engines_added_[engine] = true;
  groups_shown_[group] = true;
  updateOscillatorMemory();
  return engine;
}

void SynthBase::removeSoundEngineInternal(int engine) {
  std::string prefix = enginePrefix(engine);
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, prefix))
      disconnectModulation(connection);
  }

  {
    ScopedLock lock(getCriticalSection());
    controls_[prefix + "on"]->set(0.0f);
  }
  setValueNotifyHost(prefix + "on", 0.0f);
  engines_added_[engine] = false;
  updateOscillatorMemory();
}

void SynthBase::moveSoundEngine(int engine, int group, int position) {
  std::string group_control = enginePrefix(engine) + "group";
  {
    ScopedLock lock(getCriticalSection());
    controls_[group_control]->set(group);

    vital::EngineOrder old_order = engine_->getEngineOrder();
    std::vector<int> order;
    for (int index : old_order) {
      if (index != engine)
        order.push_back(index);
    }

    // Insert before the position-th engine shown in the group, otherwise after the group's last engine.
    int seen = 0;
    int insert_at = -1;
    int after_last = -1;
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
      int index = order[i];
      int index_group = static_cast<int>(controls_[enginePrefix(index) + "group"]->value());
      if (!engines_added_[index] || index_group != group)
        continue;
      if (seen == position && insert_at < 0)
        insert_at = i;
      after_last = i + 1;
      seen++;
    }
    if (insert_at < 0)
      insert_at = after_last >= 0 ? after_last : static_cast<int>(order.size());
    order.insert(order.begin() + insert_at, engine);

    vital::EngineOrder new_order;
    std::copy(order.begin(), order.end(), new_order.begin());
    engine_->setEngineOrder(new_order);
  }
  setValueNotifyHost(group_control, group);
  groups_shown_[group] = true;
}

int SynthBase::addGroup() {
  for (int g = 0; g < vital::kNumOscillatorGroups; ++g) {
    if (!groups_shown_[g]) {
      groups_shown_[g] = true;
      moveGroup(g, vital::kNumOscillatorGroups);
      return g;
    }
  }
  return -1;
}

void SynthBase::setGroupOrder(const GroupOrder& order) {
  bool seen[vital::kNumOscillatorGroups] = {};
  for (int group : order) {
    if (group < 0 || group >= vital::kNumOscillatorGroups || seen[group])
      return;
    seen[group] = true;
  }
  group_order_ = order;
}

void SynthBase::moveGroup(int group, int position) {
  std::vector<int> order;
  for (int g : group_order_) {
    if (g != group)
      order.push_back(g);
  }

  int seen = 0;
  int insert_at = -1;
  int after_last = 0;
  for (int i = 0; i < static_cast<int>(order.size()); ++i) {
    if (!groups_shown_[order[i]])
      continue;
    if (seen == position && insert_at < 0)
      insert_at = i;
    after_last = i + 1;
    seen++;
  }
  if (insert_at < 0)
    insert_at = after_last;
  order.insert(order.begin() + insert_at, group);
  std::copy(order.begin(), order.end(), group_order_.begin());
}

void SynthBase::copyModulations(const std::string& from_prefix, const std::string& to_prefix) {
  std::vector<vital::ModulationConnection*> sources;
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, from_prefix))
      sources.push_back(connection);
  }

  vital::ModulationConnectionBank& bank = getModulationBank();
  for (vital::ModulationConnection* connection : sources) {
    std::string destination = to_prefix + connection->destination_name.substr(from_prefix.size());
    connectModulation(connection->source_name, destination);
    int from_index = getConnectionIndex(connection->source_name, connection->destination_name);
    int to_index = getConnectionIndex(connection->source_name, destination);
    if (from_index < 0 || to_index < 0)
      continue;

    static const std::string kSettings[] = { "amount", "power", "bipolar", "stereo", "bypass" };
    for (const std::string& setting : kSettings) {
      std::string from = "modulation_" + std::to_string(from_index + 1) + "_" + setting;
      std::string to = "modulation_" + std::to_string(to_index + 1) + "_" + setting;
      if (controls_.count(from) == 0 || controls_.count(to) == 0)
        continue;
      vital::mono_float value = controls_[from]->value();
      {
        ScopedLock lock(getCriticalSection());
        controls_[to]->set(value);
      }
      setValueNotifyHost(to, value);
    }
    json line_mapping = bank.atIndex(from_index)->modulation_processor->lineMapGenerator()->stateToJson();
    bank.atIndex(to_index)->modulation_processor->lineMapGenerator()->jsonToState(line_mapping);
  }
}

void SynthBase::copyControls(const std::string& from_prefix, const std::string& to_prefix) {
  std::vector<std::string> changed;
  {
    ScopedLock lock(getCriticalSection());
    for (auto& control : controls_) {
      if (!startsWith(control.first, from_prefix))
        continue;
      std::string to = to_prefix + control.first.substr(from_prefix.size());
      if (controls_.count(to)) {
        controls_[to]->set(control.second->value());
        changed.push_back(to);
      }
    }
  }
  for (const std::string& name : changed)
    setValueNotifyHost(name, controls_[name]->value());
}

int SynthBase::duplicateSoundEngine(int engine, int group, int position, bool keep_engine_above_modulation) {
  int copy = -1;
  for (int i = 0; i < vital::kNumOscillators && copy < 0; ++i) {
    if (!engines_added_[i])
      copy = i;
  }
  if (copy < 0)
    return -1;

  std::string from = enginePrefix(engine);
  std::string to = enginePrefix(copy);
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, to))
      disconnectModulation(connection);
  }

  copyControls(from, to);
  vital::mono_float group_value = group;
  {
    ScopedLock lock(getCriticalSection());
    controls_[to + "group"]->set(group_value);
  }
  setValueNotifyHost(to + "group", group_value);

  // FM and RM read the engine above, which a copy doesn't keep.
  int distortion = static_cast<int>(controls_[to + "distortion_type"]->value());
  bool reads_engine_above = distortion >= vital::SynthOscillator::kFmOscillatorA &&
                            distortion <= vital::SynthOscillator::kRmSample;
  if (reads_engine_above && !keep_engine_above_modulation) {
    {
      ScopedLock lock(getCriticalSection());
      controls_[to + "distortion_type"]->set(vital::SynthOscillator::kNone);
    }
    setValueNotifyHost(to + "distortion_type", vital::SynthOscillator::kNone);
  }

  wavetable_creators_[copy]->jsonToState(wavetable_creators_[engine]->stateToJson());
  wavetable_creators_[copy]->render();
  engine_->getSample(copy)->jsonToState(engine_->getSample(engine)->stateToJson());
  copyModulations(from, to);

  engines_added_[copy] = true;
  groups_shown_[group] = true;
  moveSoundEngine(copy, group, position);
  updateOscillatorMemory();
  return copy;
}

int SynthBase::duplicateGroup(int group, int position) {
  int copy = -1;
  for (int g = 0; g < vital::kNumOscillatorGroups && copy < 0; ++g) {
    if (!groups_shown_[g])
      copy = g;
  }
  if (copy < 0)
    return -1;

  std::vector<int> engines;
  for (int engine : getEngineOrder()) {
    if (engines_added_[engine] && static_cast<int>(controls_[enginePrefix(engine) + "group"]->value()) == group)
      engines.push_back(engine);
  }
  if (getNumEnginesAdded() + static_cast<int>(engines.size()) > vital::kNumOscillators)
    return -1;

  std::string from = "group_" + std::to_string(group + 1) + "_";
  std::string to = "group_" + std::to_string(copy + 1) + "_";
  for (vital::ModulationConnection* connection : getModulationConnections()) {
    if (startsWith(connection->destination_name, to))
      disconnectModulation(connection);
  }
  copyControls(from, to);
  copyModulations(from, to);

  groups_shown_[copy] = true;
  // The copied chain keeps its order, so FM/RM still reads the same engine above.
  for (int engine : engines)
    duplicateSoundEngine(engine, copy, vital::kNumOscillators, true);
  moveGroup(copy, position);
  return copy;
}

LineGenerator* SynthBase::getLfoSource(int index) {
  return engine_->getLfoSource(index);
}

json SynthBase::saveToJson() {
  return LoadSave::stateToJson(this, getCriticalSection());
}

int SynthBase::getSampleRate() {
  return engine_->getSampleRate();
}

void SynthBase::initEngine() {
  clearModulations();
  if (getWavetableCreator(0)) {
    for (int i = 0; i < vital::kNumOscillators; ++i)
      getWavetableCreator(i)->init();

    for (int i = 0; i < vital::kNumOscillators; ++i)
      engine_->getSample(i)->init();
  }

  vital::EngineOrder order;
  for (int i = 0; i < vital::kNumOscillators; ++i)
    order[i] = i;
  setEngineOrder(order);

  std::array<bool, vital::kNumOscillators> no_engines;
  no_engines.fill(false);
  std::array<bool, vital::kNumOscillatorGroups> first_group;
  first_group.fill(false);
  first_group[0] = true;
  setEngineLayout(no_engines, first_group);
  GroupOrder group_order;
  for (int g = 0; g < vital::kNumOscillatorGroups; ++g)
    group_order[g] = g;
  setGroupOrder(group_order);

  for (int i = 0; i < vital::kNumLfos; ++i)
    getLfoSource(i)->initTriangle();

  vital::control_map controls = engine_->getControls();
  for (auto& control : controls) {
    vital::ValueDetails details = vital::Parameters::getDetails(control.first);
    control.second->set(details.default_value);
  }
  checkOversampling();
  updateOscillatorMemory();

  clearActiveFile();
}

void SynthBase::loadTuningFile(const File& file) {
  tuning_.loadFile(file);
}

void SynthBase::loadInitPreset() {
  RemovalSnapshot previous = capturePatch();
  pauseProcessing(true);
  engine_->allSoundsOff();
  initEngine();
  LoadSave::initSaveInfo(save_info_);
  pauseProcessing(false);
  pushPatchUndo(previous, "Init");
}

bool SynthBase::loadFromJson(const json& data) {
  pauseProcessing(true);
  engine_->allSoundsOff();
  try {
    bool result = LoadSave::jsonToState(this, save_info_, data);
    pauseProcessing(false);
    return result;
  }
  catch (const json::exception& e) {
    pauseProcessing(false);
    throw e;
  }
}

bool SynthBase::loadFromFile(File preset, std::string& error) {
  if (!preset.exists())
    return false;
  
  RemovalSnapshot previous = capturePatch();
  try {
    json parsed_json_state = json::parse(preset.loadFileAsString().toStdString(), nullptr);
    if (!loadFromJson(parsed_json_state)) {
      error = "Preset was created with a newer version.";
      return false;
    }

    active_file_ = preset;
  }
  catch (const json::exception& e) {
    error = "Preset file is corrupted.";
    return false;
  }
  
  setPresetName(preset.getFileNameWithoutExtension());
  pushPatchUndo(previous, preset.getFileNameWithoutExtension().toStdString());

  SynthGuiInterface* gui_interface = getGuiInterface();
  if (gui_interface) {
    gui_interface->updateFullGui();
    gui_interface->notifyFresh();
  }

  return true;
}

void SynthBase::renderAudioToFile(File file, float seconds, float bpm, std::vector<int> notes, bool render_images) {
  static constexpr int kSampleRate = 44100;
  static constexpr int kPreProcessSamples = 44100;
  static constexpr int kFadeSamples = 200;
  static constexpr int kBufferSize = 64;
  static constexpr int kVideoRate = 30;
  static constexpr int kImageNumberPlaces = 3;
  static constexpr int kImageWidth = 500;
  static constexpr int kImageHeight = 250;
  static constexpr int kOscilloscopeResolution = 512;
  static constexpr float kFadeRatio = 0.3f;

  ScopedLock lock(getCriticalSection());

  processModulationChanges();
  engine_->setSampleRate(kSampleRate);
  engine_->setBpm(bpm);
  engine_->updateAllModulationSwitches();

  double sample_time = 1.0 / getSampleRate();
  double current_time = -kPreProcessSamples * sample_time;

  for (int samples = 0; samples < kPreProcessSamples; samples += kBufferSize) {
    engine_->correctToTime(current_time);
    current_time += kBufferSize * sample_time;
    engine_->process(kBufferSize);
  }

  for (int note : notes)
    engine_->noteOn(note, 0.7f, 0, 0);

  file.deleteFile();
  std::unique_ptr<FileOutputStream> file_stream = file.createOutputStream();
  WavAudioFormat wav_format;
  std::unique_ptr<AudioFormatWriter> writer(wav_format.createWriterFor(file_stream.get(), kSampleRate, 2, 16, {}, 0));

  int on_samples = seconds * kSampleRate;
  int total_samples = on_samples + seconds * kSampleRate * kFadeRatio;
  std::unique_ptr<float[]> left_buffer = std::make_unique<float[]>(kBufferSize);
  std::unique_ptr<float[]> right_buffer = std::make_unique<float[]>(kBufferSize);
  float* buffers[2] = { left_buffer.get(), right_buffer.get() };
  const vital::mono_float* engine_output = (const vital::mono_float*)engine_->output(0)->buffer;

#if JUCE_MODULE_AVAILABLE_juce_graphics
  int current_image_index = -1;
  PNGImageFormat png;
  File images_folder = File::getCurrentWorkingDirectory().getChildFile("images");
  if (!images_folder.exists() && render_images)
    images_folder.createDirectory();
  const vital::poly_float* memory = getOscilloscopeMemory();
#endif

  for (int samples = 0; samples < total_samples; samples += kBufferSize) {
    engine_->correctToTime(current_time);
    current_time += kBufferSize * sample_time;
    engine_->process(kBufferSize);
    updateMemoryOutput(kBufferSize, engine_->output(0)->buffer);

    if (on_samples > samples && on_samples <= samples + kBufferSize) {
      for (int note : notes)
        engine_->noteOff(note, 0.5f, 0, 0);
    }

    for (int i = 0; i < kBufferSize; ++i) {
      vital::mono_float t = (total_samples - samples) / (1.0f * kFadeSamples);
      t = vital::utils::min(t, 1.0f);
      left_buffer[i] = t * engine_output[vital::poly_float::kSize * i];
      right_buffer[i] = t * engine_output[vital::poly_float::kSize * i + 1];
    }

    writer->writeFromFloatArrays(buffers, 2, kBufferSize);

  #if JUCE_MODULE_AVAILABLE_juce_graphics
    int image_index = (samples * kVideoRate) / kSampleRate;
    if (image_index > current_image_index && render_images) {
      current_image_index = image_index;
      String number(image_index);
      while (number.length() < kImageNumberPlaces)
        number = "0" + number;

      File image_file = images_folder.getChildFile("rendered_image" + number + ".png");
      FileOutputStream image_file_stream(image_file);
      Image image(Image::RGB, kImageWidth, kImageHeight, true);
      Graphics g(image);
      g.fillAll(Colour(0xff1d2125));

      Path left_path;
      Path right_path;
      left_path.startNewSubPath(-2.0f, kImageHeight / 2);
      right_path.startNewSubPath(-2.0f, kImageHeight / 2);

      for (int i = 0; i < kOscilloscopeResolution; ++i) {
        float t = i / (kOscilloscopeResolution - 1.0f);
        float memory_spot = (1.0f * i * vital::kOscilloscopeMemoryResolution) / kOscilloscopeResolution;
        int memory_index = memory_spot;
        float remainder = memory_spot - memory_index;
        vital::poly_float from = memory[memory_index];
        vital::poly_float to = memory[memory_index + 1];
        vital::poly_float y = -vital::utils::interpolate(from, to, remainder) * kImageHeight / 2.0f + kImageHeight / 2;
        left_path.lineTo(t * kImageWidth, y[0]);
        right_path.lineTo(t * kImageWidth, y[1]);
      }
      left_path.lineTo(kImageWidth + 2.0f, kImageHeight / 2.0f);
      right_path.lineTo(kImageWidth + 2.0f, kImageHeight / 2.0f);

      g.setColour(Colour(0x64aa88ff));
      g.fillPath(left_path);
      g.fillPath(right_path);

      g.setColour(Colour(0xffaa88ff));
      g.strokePath(left_path, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded));
      g.strokePath(right_path, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded));

      png.writeImageToStream(image, image_file_stream);
    }
  #endif
  }

  writer->flush();
  file_stream->flush();

  writer = nullptr;
  file_stream.release();
}

void SynthBase::renderAudioForResynthesis(float* data, int samples, int note) {
  static constexpr int kPreProcessSamples = 44100;
  static constexpr int kBufferSize = 64;

  ScopedLock lock(getCriticalSection());

  double sample_time = 1.0 / getSampleRate();
  double current_time = -kPreProcessSamples * sample_time;

  engine_->allSoundsOff();
  for (int s = 0; s < kPreProcessSamples; s += kBufferSize) {
    engine_->correctToTime(current_time);
    current_time += kBufferSize * sample_time;
    engine_->process(kBufferSize);
  }

  engine_->noteOn(note, 0.7f, 0, 0);
  const vital::poly_float* engine_output = engine_->output(0)->buffer;
  float max_value = 0.01f;
  for (int s = 0; s < samples; s += kBufferSize) {
    int num_samples = std::min(samples - s, kBufferSize);
    engine_->correctToTime(current_time);
    current_time += num_samples * sample_time;
    engine_->process(num_samples);

    for (int i = 0; i < num_samples; ++i) {
      float sample = engine_output[i][0];
      data[s + i] = sample;
      max_value = std::max(max_value, fabsf(sample));
    }
  }

  float scale = 1.0f / max_value;
  for (int s = 0; s < samples; ++s)
    data[s] *= scale;

  engine_->allSoundsOff();
}

bool SynthBase::saveToFile(File preset) {
  preset = preset.withFileExtension(String(vital::kPresetExtension));

  File parent = preset.getParentDirectory();
  if (!parent.exists()) {
    if (!parent.createDirectory().wasOk() || !parent.hasWriteAccess())
      return false;
  }

  setPresetName(preset.getFileNameWithoutExtension());

  SynthGuiInterface* gui_interface = getGuiInterface();
  if (gui_interface)
    gui_interface->notifyFresh();

  if (preset.replaceWithText(saveToJson().dump())) {
    active_file_ = preset;
    return true;
  }
  return false;
}

bool SynthBase::saveToActiveFile() {
  if (!active_file_.exists() || !active_file_.hasWriteAccess())
    return false;

  return saveToFile(active_file_);
}

void SynthBase::setMpeEnabled(bool enabled) {
  midi_manager_->setMpeEnabled(enabled);
}

void SynthBase::processAudio(AudioSampleBuffer* buffer, int channels, int samples, int offset) {
  if (expired_)
    return;

  engine_->process(samples);
  writeAudio(buffer, channels, samples, offset);
}

void SynthBase::processAudioWithInput(AudioSampleBuffer* buffer, const vital::poly_float* input_buffer,
                                      int channels, int samples, int offset) {
  if (expired_)
    return;

  engine_->processWithInput(input_buffer, samples);
  writeAudio(buffer, channels, samples, offset);
}

void SynthBase::writeAudio(AudioSampleBuffer* buffer, int channels, int samples, int offset) {
  const vital::mono_float* engine_output = (const vital::mono_float*)engine_->output(0)->buffer;
  for (int channel = 0; channel < channels; ++channel) {
    float* channel_data = buffer->getWritePointer(channel, offset);

    for (int i = 0; i < samples; ++i) {
      channel_data[i] = engine_output[vital::poly_float::kSize * i + channel];
      VITAL_ASSERT(std::isfinite(channel_data[i]));
    }
  }

  updateMemoryOutput(samples, engine_->output(0)->buffer);
}

void SynthBase::processMidi(MidiBuffer& midi_messages, int start_sample, int end_sample) {
  bool process_all = end_sample == 0;
  for (const MidiMessageMetadata message : midi_messages) {
    int midi_sample = message.samplePosition;
    if (process_all || (midi_sample >= start_sample && midi_sample < end_sample))
      midi_manager_->processMidiMessage(message.getMessage(), midi_sample - start_sample);
  }
}

void SynthBase::processKeyboardEvents(MidiBuffer& buffer, int num_samples) {
  midi_manager_->replaceKeyboardMessages(buffer, num_samples);
}

void SynthBase::processModulationChanges() {
  vital::modulation_change change;
  while (getNextModulationChange(change)) {
    if (change.disconnecting)
      engine_->disconnectModulation(change);
    else
      engine_->connectModulation(change);
  }
}

void SynthBase::updateMemoryOutput(int samples, const vital::poly_float* audio) {
  for (int i = 0; i < samples; ++i)
    audio_memory_->push(audio[i]);

  vital::mono_float last_played = engine_->getLastActiveNote();
  last_played = vital::utils::clamp(last_played, kOutputWindowMinNote, kOutputWindowMaxNote);

  int num_pressed = engine_->getNumPressedNotes();
  int output_inc = std::max<int>(1, engine_->getSampleRate() / vital::kOscilloscopeMemorySampleRate);
  int oscilloscope_samples = 2 * vital::kOscilloscopeMemoryResolution;

  if (last_played && (last_played_note_ != last_played || num_pressed > last_num_pressed_)) {
    last_played_note_ = last_played;
    
    vital::mono_float frequency = vital::utils::midiNoteToFrequency(last_played_note_);
    vital::mono_float period = engine_->getSampleRate() / frequency;
    int window_length = output_inc * vital::kOscilloscopeMemoryResolution;

    memory_reset_period_ = period;
    while (memory_reset_period_ < window_length)
      memory_reset_period_ += memory_reset_period_;

    memory_reset_period_ = std::min(memory_reset_period_, 2.0f * window_length);
    memory_index_ = 0;
    vital::utils::copyBuffer(oscilloscope_memory_, oscilloscope_memory_write_, oscilloscope_samples);
  }
  last_num_pressed_ = num_pressed;

  for (; memory_input_offset_ < samples; memory_input_offset_ += output_inc) {
    int input_index = vital::utils::iclamp(memory_input_offset_, 0, samples);
    memory_index_ = vital::utils::iclamp(memory_index_, 0, oscilloscope_samples - 1);
    VITAL_ASSERT(input_index >= 0);
    VITAL_ASSERT(input_index < samples);
    VITAL_ASSERT(memory_index_ >= 0);
    VITAL_ASSERT(memory_index_ < oscilloscope_samples);
    oscilloscope_memory_write_[memory_index_++] = audio[input_index];

    if (memory_index_ * output_inc >= memory_reset_period_) {
      memory_input_offset_ += memory_reset_period_ - memory_index_ * output_inc;
      memory_index_ = 0;
      vital::utils::copyBuffer(oscilloscope_memory_, oscilloscope_memory_write_, oscilloscope_samples);
    }
  }

  memory_input_offset_ -= samples;
}

void SynthBase::armMidiLearn(const std::string& name) {
  midi_manager_->armMidiLearn(name);
}

void SynthBase::cancelMidiLearn() {
  midi_manager_->cancelMidiLearn();
}

void SynthBase::clearMidiLearn(const std::string& name) {
  midi_manager_->clearMidiLearn(name);
}

bool SynthBase::isMidiMapped(const std::string& name) {
  return midi_manager_->isMidiMapped(name);
}

void SynthBase::setAuthor(const String& author) {
  save_info_["author"] = author;
}

void SynthBase::setComments(const String& comments) {
  save_info_["comments"] = comments;
}

void SynthBase::setStyle(const String& style) {
  save_info_["style"] = style;
}

void SynthBase::setPresetName(const String& preset_name) {
  save_info_["preset_name"] = preset_name;
}

void SynthBase::setMacroName(int index, const String& macro_name) {
  save_info_["macro" + std::to_string(index + 1)] = macro_name;
}

String SynthBase::getAuthor() {
  return save_info_["author"];
}

String SynthBase::getComments() {
  return save_info_["comments"];
}

String SynthBase::getStyle() {
  return save_info_["style"];
}

String SynthBase::getPresetName() {
  return save_info_["preset_name"];
}

String SynthBase::getMacroName(int index) {
  String name = save_info_["macro" + std::to_string(index + 1)];
  if (name.trim().isEmpty())
    return "MACRO " + String(index + 1);
  return name;
}

const vital::StereoMemory* SynthBase::getEqualizerMemory() {
  if (engine_)
    return engine_->getEqualizerMemory();
  return nullptr;
}

vital::ModulationConnectionBank& SynthBase::getModulationBank() {
  return engine_->getModulationBank();
}

void SynthBase::notifyOversamplingChanged() {
  pauseProcessing(true);
  engine_->allSoundsOff();
  checkOversampling();
  pauseProcessing(false);
}

void SynthBase::checkOversampling() {
  return engine_->checkOversampling();
}

void SynthBase::ValueChangedCallback::messageCallback() {
  if (auto synth_base = listener.lock()) {
    if (isEngineMemoryControl(control_name))
      (*synth_base)->updateOscillatorMemory();

    SynthGuiInterface* gui_interface = (*synth_base)->getGuiInterface();
    if (gui_interface) {
      gui_interface->updateGuiControl(control_name, value);
      if (control_name != "pitch_wheel")
        gui_interface->notifyChange();
    }
  }
}
