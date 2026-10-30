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

#include "sample_section.h"

#include "load_save.h"
#include "skin.h"
#include "fonts.h"
#include "paths.h"
#include "sample_viewer.h"
#include "synth_button.h"
#include "synth_gui_interface.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"

namespace {
  // Two left-pointing triangles: play backwards.
  Path reverseShape() {
    Path path;
    path.addTriangle(0.5f, 0.3f, 0.5f, 0.7f, 0.22f, 0.5f);
    path.addTriangle(0.78f, 0.3f, 0.78f, 0.7f, 0.5f, 0.5f);
    path.addLineSegment(Line<float>(0.0f, 0.0f, 0.0f, 0.0f), 0.2f);
    path.addLineSegment(Line<float>(1.0f, 1.0f, 1.0f, 1.0f), 0.2f);
    return path;
  }
}

SampleSection::SampleSection(String name, int engine, const vital::output_map& mono_modulations,
                             const vital::output_map& poly_modulations) :
    SynthSection(std::move(name)), engine_(engine), sample_(nullptr) {
  engine_prefix_ = "osc_" + std::to_string(engine + 1) + "_";
  const std::string& kEngine = engine_prefix_;
  transpose_quantize_button_ = std::make_unique<TransposeQuantizeButton>();
  addOpenGlComponent(transpose_quantize_button_.get());
  transpose_quantize_button_->addQuantizeListener(this);

  transpose_ = std::make_unique<SynthSlider>(kEngine + "transpose");
  addSlider(transpose_.get());
  transpose_->setLookAndFeel(TextLookAndFeel::instance());
  transpose_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  transpose_->setSensitivity(kTransposeMouseSensitivity);
  transpose_->setTextEntrySizePercent(1.0f, 0.7f);
  transpose_->setShiftIndexAmount(vital::kNotesPerOctave);
  transpose_->overrideValue(Skin::kTextComponentOffset, 0.0f);
  transpose_->setModulationBarRight(false);

  tune_ = std::make_unique<SynthSlider>(kEngine + "tune");
  addSlider(tune_.get());
  tune_->setLookAndFeel(TextLookAndFeel::instance());
  tune_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  tune_->setMaxDisplayCharacters(3);
  tune_->setMaxDecimalPlaces(0);
  tune_->setTextEntrySizePercent(1.0f, 0.7f);
  tune_->overrideValue(Skin::kTextComponentOffset, 0.0f);

  level_ = std::make_unique<SynthSlider>(kEngine + "level");
  addSlider(level_.get());
  level_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  pan_ = std::make_unique<SynthSlider>(kEngine + "pan");
  addSlider(pan_.get());
  pan_->setBipolar();
  pan_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  sample_viewer_ = std::make_unique<SampleViewer>(engine);
  sample_viewer_->setModulationOutputs(mono_modulations, poly_modulations, "osc_" + std::to_string(engine + 1) + "_");
  addOpenGlComponent(sample_viewer_.get());
  addAndMakeVisible(sample_viewer_.get());
  sample_viewer_->addListener(this);

  preset_selector_ = std::make_unique<PresetSelector>();
  addSubSection(preset_selector_.get());
  preset_selector_->addListener(this);
  setPresetSelector(preset_selector_.get());

  destination_selector_ = std::make_unique<ShapeButton>("Destination", Colour(0xff666666),
                                                        Colour(0xffaaaaaa), Colour(0xff888888));

  current_destination_ = 0;
  destination_control_name_ = kEngine + "destination";
  destination_text_ = std::make_unique<PlainTextComponent>("Destination Text", "---");
  addOpenGlComponent(destination_text_.get());

  addAndMakeVisible(destination_selector_.get());
  destination_selector_->addListener(this);
  destination_selector_->setTriggeredOnMouseDown(true);
  destination_selector_->setShape(Path(), true, true, true);

  prev_destination_ = std::make_unique<OpenGlShapeButton>("Prev Destination");
  addAndMakeVisible(prev_destination_.get());
  addOpenGlComponent(prev_destination_->getGlComponent());
  prev_destination_->addListener(this);
  prev_destination_->setShape(Paths::prev());

  next_destination_ = std::make_unique<OpenGlShapeButton>("Next Destination");
  addAndMakeVisible(next_destination_.get());
  addOpenGlComponent(next_destination_->getGlComponent());
  next_destination_->addListener(this);
  next_destination_->setShape(Paths::next());

  keytrack_ = std::make_unique<OpenGlShapeButton>(kEngine + "sample_keytrack");
  keytrack_->useOnColors(true);
  keytrack_->setClickingTogglesState(true);
  addButton(keytrack_.get());
  keytrack_->addListener(this);
  keytrack_->setShape(Paths::keyboard());

  random_phase_ = std::make_unique<OpenGlShapeButton>(kEngine + "random_phase");
  random_phase_->useOnColors(true);
  random_phase_->setClickingTogglesState(true);
  addButton(random_phase_.get());
  random_phase_->addListener(this);
  random_phase_->addListener(this);
  random_phase_->setShape(Paths::shuffle());

  loop_ = std::make_unique<OpenGlShapeButton>(kEngine + "sample_loop");
  loop_->useOnColors(true);
  loop_->setClickingTogglesState(true);
  addButton(loop_.get());
  loop_->addListener(this);
  loop_->setShape(Paths::loop());

  bounce_ = std::make_unique<OpenGlShapeButton>(kEngine + "sample_bounce");
  bounce_->useOnColors(true);
  bounce_->setClickingTogglesState(true);
  addButton(bounce_.get());
  bounce_->addListener(this);
  bounce_->setShape(Paths::bounce());

  reverse_ = std::make_unique<OpenGlShapeButton>(kEngine + "sample_reverse");
  reverse_->useOnColors(true);
  reverse_->setClickingTogglesState(true);
  addButton(reverse_.get());
  reverse_->addListener(this);
  reverse_->setShape(reverseShape());

  auto knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& name) {
    slider = std::make_unique<SynthSlider>(kEngine + name);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  };
  knob(start_, "sample_start");
  knob(end_, "sample_end");

  // The display's corners: STRETCH, SYNC and the sample's tempo at the bottom left (like the wavetable card's
  // WT / 2D buttons); snapping to zero crossings and zoom at the bottom right.
  auto cornerButton = [&](std::unique_ptr<SynthButton>& button, const std::string& name, const String& text) {
    button = std::make_unique<SynthButton>(name);
    if (name.rfind(kEngine, 0) == 0)
      addButton(button.get());
    else {
      addAndMakeVisible(button.get());
      addOpenGlComponent(button->getGlComponent());
      button->addListener(this);
    }
    button->setNoBackground();
    button->setLookAndFeel(TextLookAndFeel::instance());
    button->setText(text);
  };
  cornerButton(stretch_, kEngine + "sample_stretch", "STRETCH");
  cornerButton(tempo_sync_, kEngine + "sample_tempo_sync", "SYNC");
  cornerButton(snap_zero_, "Snap To Zero Crossings", "ZC");
  cornerButton(zoom_out_, "Zoom Out", "-");
  cornerButton(zoom_in_, "Zoom In", "+");
  zoom_out_->setClickingTogglesState(false);
  zoom_in_->setClickingTogglesState(false);
  zoom_out_->setShowOnColors(false);
  zoom_in_->setShowOnColors(false);

  bpm_ = std::make_unique<SynthSlider>(kEngine + "sample_bpm");
  addSlider(bpm_.get());
  bpm_->setLookAndFeel(TextLookAndFeel::instance());
  bpm_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  bpm_->setMaxDecimalPlaces(1);
  bpm_->setTextEntrySizePercent(1.0f, 0.7f);
  bpm_->overrideValue(Skin::kTextComponentOffset, 0.0f);


  knob(loop_start_, "sample_loop_start");
  knob(loop_end_, "sample_loop_end");
  knob(crossfade_, "sample_crossfade");
  knob(unison_voices_, "unison_voices");
  knob(unison_detune_, "unison_detune");
  knob(stereo_, "stereo_spread");

  on_ = std::make_unique<SynthButton>(kEngine + "on");
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kSample);
}

SampleSection::~SampleSection() = default;

void SampleSection::parentHierarchyChanged() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();

  if (sample_ == nullptr && parent) {
    sample_ = parent->getSynth()->getSample(engine_);
    sample_viewer_->setSample(sample_);
    sample_viewer_->repaintAudio();
    reset();
  }
}

void SampleSection::paintBackground(Graphics& g) {
  if (getWidth() == 0)
    return;

  paintContainer(g);
  paintHeadingText(g);

  paintKnobShadows(g);

  setLabelFont(g);
  drawLabelForComponent(g, TRANS("START"), start_.get());
  drawLabelForComponent(g, TRANS("END"), end_.get());
  drawLabelForComponent(g, TRANS("LOOP START"), loop_start_.get());
  drawLabelForComponent(g, TRANS("LOOP END"), loop_end_.get());
  drawLabelForComponent(g, TRANS("XFADE"), crossfade_.get());
  drawLabelForComponent(g, TRANS("VOICES"), unison_voices_.get());
  drawLabelForComponent(g, TRANS("DETUNE"), unison_detune_.get());
  drawLabelForComponent(g, TRANS("STEREO"), stereo_.get());
  drawLabelForComponent(g, TRANS("LEVEL"), level_.get());
  drawLabelForComponent(g, TRANS("PAN"), pan_.get());

  int widget_margin = findValue(Skin::kWidgetMargin);
  int pitch_x = findValue(Skin::kTitleWidth);
  int component_width = getWidth() * kPitchWidthRatio - 2 * widget_margin;
  int label_height = findValue(Skin::kLabelBackgroundHeight);
  int top_row_y = widget_margin;
  paintJointControl(g, pitch_x + widget_margin, top_row_y, component_width, pitchBoxHeight(), "PITCH");

  float label_rounding = findValue(Skin::kLabelBackgroundRounding);
  g.setColour(findColour(Skin::kTextComponentBackground, true));
  g.fillRoundedRectangle(destination_selector_->getBounds().toFloat(), label_rounding);

  // The sample name, its toggles and the waveform share one panel.
  g.setColour(findColour(Skin::kWidgetBackground, true));
  g.fillRoundedRectangle(sample_viewer_->getX(), widget_margin,
                         sample_viewer_->getWidth(), getHeight() - 2 * widget_margin,
                         findValue(Skin::kWidgetRoundedCorner));

  paintChildrenBackgrounds(g);
  paintBorder(g);
}

void SampleSection::setActive(bool active) {
  sample_viewer_->setActive(active);
  SynthSection::setActive(active);
  bpm_->setActive(active && tempo_sync_->getToggleState());
  updateLoopControls();
}

void SampleSection::resized() {
  SynthSection::resized();

  preset_selector_->setColour(Skin::kIconButtonOff, findColour(Skin::kUiButton, true));
  preset_selector_->setColour(Skin::kIconButtonOffHover, findColour(Skin::kUiButtonHover, true));
  preset_selector_->setColour(Skin::kIconButtonOffPressed, findColour(Skin::kUiButtonPressed, true));

  destination_text_->setColor(findColour(Skin::kBodyText, true));

  // Pitch on the left, the sample in the middle, and two rows of four knobs on the right (start, loop start,
  // loop end, crossfade above unison voices, detune, level and pan), as tall as a wavetable card.
  int title_width = getTitleWidth();
  int widget_margin = findValue(Skin::kWidgetMargin);
  int pitch_x = title_width;
  int pitch_width = getWidth() * kPitchWidthRatio;
  int sample_x = pitch_x + pitch_width;
  int knobs_width = getWidth() * kKnobsWidthRatio;
  int knobs_x = getWidth() - knobs_width;
  int sample_width = knobs_x - sample_x - widget_margin;
  int label_height = findValue(Skin::kLabelBackgroundHeight);
  int top_row_y = widget_margin;

  int destination_x = pitch_x + widget_margin;
  int destination_y = getHeight() - label_height - widget_margin;
  destination_selector_->setBounds(destination_x, destination_y, pitch_width - 2 * widget_margin, label_height);
  destination_text_->setBounds(destination_selector_->getBounds());
  destination_text_->setTextSize(findValue(Skin::kLabelHeight));

  prev_destination_->setBounds(destination_x, destination_y, label_height, label_height);
  next_destination_->setBounds(destination_selector_->getRight() - label_height, destination_y,
                               label_height, label_height);

  placeJointControls(pitch_x + widget_margin, top_row_y, pitch_width - 2 * widget_margin, pitchBoxHeight(),
                     transpose_.get(), tune_.get(), transpose_quantize_button_.get());

  // LEVEL and PAN sit under PITCH, as on the wavetable card.
  int level_y = top_row_y + pitchBoxHeight();
  placeKnobsInArea(Rectangle<int>(pitch_x, level_y, pitch_width, destination_y - level_y), { level_.get(), pan_.get() });

  // The right: two rows of four, wide enough for every label: START, END, LOOP START, LOOP END above XFADE,
  // VOICES, DETUNE and STEREO.
  int row_height = getHeight() / 2;
  placeKnobsInArea(Rectangle<int>(knobs_x, 0, knobs_width, row_height),
                   { start_.get(), end_.get(), loop_start_.get(), loop_end_.get() });
  placeKnobsInArea(Rectangle<int>(knobs_x, row_height, knobs_width, getHeight() - row_height),
                   { crossfade_.get(), unison_voices_.get(), unison_detune_.get(), stereo_.get() });

  // The header holds the sample's name, then its toggles: keytrack, random start, loop, bounce, reverse.
  int header_height = title_width - 2 * widget_margin;
  std::vector<Component*> toggles = { keytrack_.get(), random_phase_.get(), loop_.get(), bounce_.get(),
                                      reverse_.get() };
  int toggle_size = header_height;
  int toggles_width = static_cast<int>(toggles.size()) * toggle_size;
  int toggles_x = sample_x + sample_width - toggles_width - widget_margin;
  for (int i = 0; i < static_cast<int>(toggles.size()); ++i)
    toggles[i]->setBounds(toggles_x + i * toggle_size, widget_margin, toggle_size, toggle_size);

  preset_selector_->setBounds(sample_x, widget_margin, toggles_x - sample_x, header_height);
  // The waveform sits between the name bar and a control bar of the same kind along the bottom: STRETCH, SYNC
  // and the BPM on the left, ZC, - and + on the right. The bar is part of the panel, not the display, so no
  // marker crosses it.
  int text_height = findValue(Skin::kLabelBackgroundHeight);
  int bar_y = getHeight() - widget_margin - text_height;
  int viewer_y = title_width - widget_margin;
  sample_viewer_->setBounds(sample_x, viewer_y, sample_width, bar_y - viewer_y);

  // The buttons draw their text in the monospace font at the button size, the BPM box in the light font; each
  // control is measured in its own font, with an even gap between them. If the bar is too narrow for them all at
  // the button size, the text shrinks to fit rather than overlap.
  value_lookup_.erase(Skin::kButtonFontSize);
  float button_font_size = findValue(Skin::kButtonFontSize);
  Font button_font = Fonts::instance()->monospace().withPointHeight(button_font_size);
  Font value_font = Fonts::instance()->proportional_light().withPointHeight(button_font_size);
  int gap = 2 * widget_margin;
  float content = button_font.getStringWidthFloat("STRETCH") + button_font.getStringWidthFloat("SYNC") +
                  value_font.getStringWidthFloat("000.0 BPM") + button_font.getStringWidthFloat("ZC") +
                  2.0f * text_height;
  float available = sample_width - 6.0f * gap;
  float scale = std::min(1.0f, available / content);
  float stored_ratio = Skin::shouldScaleValue(Skin::kButtonFontSize) ? getSizeRatio() : 1.0f;
  if (scale < 1.0f)
    setSkinValue(Skin::kButtonFontSize, button_font_size * scale / stored_ratio);
  bpm_->setTextHeightPercentage(0.0f);
  bpm_->overrideValue(Skin::kTextComponentFontSize, button_font_size * scale);
  bpm_->overrideValue(Skin::kTextComponentOffset, 0.0f);
  auto width = [&](const Font& font, const String& text) {
    return static_cast<int>(std::ceil(font.getStringWidthFloat(text) * scale)) + gap;
  };

  int x = sample_x + gap / 2;
  stretch_->setBounds(x, bar_y, width(button_font, "STRETCH"), text_height);
  tempo_sync_->setBounds(stretch_->getRight(), bar_y, width(button_font, "SYNC"), text_height);
  bpm_->setBounds(tempo_sync_->getRight(), bar_y, width(value_font, "000.0 BPM"), text_height);
  int right = sample_x + sample_width - gap / 2;
  zoom_in_->setBounds(right - text_height, bar_y, text_height, text_height);
  zoom_out_->setBounds(zoom_in_->getX() - text_height, bar_y, text_height, text_height);
  snap_zero_->setBounds(zoom_out_->getX() - width(button_font, "ZC"), bar_y, width(button_font, "ZC"), text_height);

  destination_text_->setBounds(destination_selector_->getBounds());
  destination_text_->setTextSize(findValue(Skin::kLabelHeight));
}

int SampleSection::pitchBoxHeight() {
  // As on the wavetable card: the PITCH box is as tall as the space above that card's LEVEL and PAN knobs.
  int widget_margin = findValue(Skin::kWidgetMargin);
  int label_height = findValue(Skin::kLabelBackgroundHeight);
  return getHeight() - label_height - 2 * widget_margin - getKnobSectionHeight();
}

void SampleSection::updateMarkers() {
  sample_viewer_->setMarkers(start_->getValue(), end_->getValue(), loop_start_->getValue(), loop_end_->getValue(),
                             crossfade_->getValue(), loop_->getToggleState(), reverse_->getToggleState());
}

void SampleSection::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
  keepMarkersInOrder(changed_slider);
  if (changed_slider == start_.get() || changed_slider == end_.get() || changed_slider == loop_start_.get() ||
      changed_slider == loop_end_.get() || changed_slider == crossfade_.get())
    updateMarkers();
}

void SampleSection::keepMarkersInOrder(Slider* changed_slider) {
  // With LOOP on, START and END can't cross into the loop: START stops at the loop's start, END at its end, and
  // the loop's markers stop at START and END. (Each correction comes back through here, already in order.)
  if (!loop_->getToggleState())
    return;
  double start = start_->getValue();
  double end = end_->getValue();
  double loop_low = std::min(loop_start_->getValue(), loop_end_->getValue());
  double loop_high = std::max(loop_start_->getValue(), loop_end_->getValue());
  if (changed_slider == start_.get() && start > loop_low)
    start_->setValue(loop_low, sendNotificationSync);
  else if (changed_slider == end_.get() && end < loop_high)
    end_->setValue(loop_high, sendNotificationSync);
  else if (changed_slider == loop_start_.get() || changed_slider == loop_end_.get()) {
    double value = changed_slider->getValue();
    if (value < start || value > end)
      changed_slider->setValue(std::max(start, std::min(end, value)), sendNotificationSync);
  }
  else if (changed_slider == nullptr) {
    // LOOP switched on: bring START and END outside the loop.
    if (start > loop_low)
      start_->setValue(loop_low, sendNotificationSync);
    if (end < loop_high)
      end_->setValue(loop_high, sendNotificationSync);
  }
}

vital::SampleSource::Placement SampleSection::placeVoice(double t, double start_spread, double loop_spread,
                                                          double loop_end_spread) const {
  // Runs on the GL thread: read the loaded data, never activeLength(), which is null outside the audio block.
  double length = sample_ ? std::max(1, sample_->upsampleLength()) : 1.0;
  return vital::SampleSource::placeVoice(start_->getValue(), end_->getValue(), loop_start_->getValue(),
                                         loop_end_->getValue(), crossfade_->getValue(), start_spread, loop_spread,
                                         loop_end_spread, loop_->getToggleState(), reverse_->getToggleState(), t,
                                         std::min(0.5, vital::SampleSource::kMinLoopFrames / length));
}

void SampleSection::sampleMarkerMoved(int marker, float value) {
  // Dragging a marker moves its knob, which sets the parameter (and records the undo step).
  // The display shows a loop set end-before-start the right way round; dragging one of its markers puts the knobs
  // the right way round too, so each marker moves its own knob.
  bool loop_marker = marker == SampleViewer::kLoopStartMarker || marker == SampleViewer::kLoopEndMarker;
  if (loop_marker && loop_end_->getValue() < loop_start_->getValue()) {
    double low = loop_end_->getValue();
    double high = loop_start_->getValue();
    loop_start_->setValue(low, sendNotificationSync);
    loop_end_->setValue(high, sendNotificationSync);
  }
  SynthSlider* sliders[SampleViewer::kNumMarkers] = { start_.get(), loop_start_.get(), loop_end_.get(), end_.get() };
  if (marker >= 0 && marker < SampleViewer::kNumMarkers)
    sliders[marker]->setValue(value, sendNotificationSync);
}

void SampleSection::reset() {
  SynthSection::reset();
  preset_selector_->setText(sample_viewer_->getName());
  sample_viewer_->repaintAudio();
}

void SampleSection::loadFile(const File& file) {
  static constexpr int kMaxFileSamples = 17640000;
  preset_selector_->setText(file.getFileNameWithoutExtension());
  sample_->setLastBrowsedFile(file.getFullPathName().toStdString());

  std::unique_ptr<AudioFormatReader> format_reader(sample_viewer_->formatManager().createReaderFor(file));

  if (format_reader) {
    int num_samples = (int)std::min<long long>(format_reader->lengthInSamples, kMaxFileSamples);
    sample_buffer_.setSize(format_reader->numChannels, num_samples);
    format_reader->read(&sample_buffer_, 0, num_samples, 0, true, true);
    if (sample_buffer_.getNumChannels() > 1) {
      sample_->loadSample(sample_buffer_.getReadPointer(0), sample_buffer_.getReadPointer(1),
                          num_samples, format_reader->sampleRate);
    }
    else
      sample_->loadSample(sample_buffer_.getReadPointer(0), num_samples, format_reader->sampleRate);
    sample_->setName(file.getFileNameWithoutExtension().toStdString());
    guessTempo();
  }

  preset_selector_->setText(sample_viewer_->getName());
  sample_viewer_->resetZoom();
  sample_viewer_->repaintAudio();
}

void SampleSection::setAllValues(vital::control_map& controls) {
  preset_selector_->setText(sample_viewer_->getName());
  transpose_quantize_button_->setValue(static_cast<int>(controls[engine_prefix_ + "transpose_quantize"]->value()));
  SynthSection::setAllValues(controls);

  current_destination_ = controls[destination_control_name_]->value();
  setupDestination();
  updateMarkers();
  updateStretchControls(nullptr);
  updateLoopControls();
}

void SampleSection::updateStretchControls(Button* clicked) {
  // SYNC needs STRETCH: switching SYNC on turns STRETCH on, and switching STRETCH off turns SYNC off. Notifying,
  // so the parameter follows and the drawer (listening to these buttons) dims its spreads. The BPM only matters
  // while synced.
  if (clicked == tempo_sync_.get() && tempo_sync_->getToggleState() && !stretch_->getToggleState())
    stretch_->setToggleState(true, sendNotificationSync);
  else if (clicked == stretch_.get() && !stretch_->getToggleState() && tempo_sync_->getToggleState())
    tempo_sync_->setToggleState(false, sendNotificationSync);
  bpm_->setActive(isActive() && tempo_sync_->getToggleState());
}

void SampleSection::updateLoopControls() {
  // With LOOP off the loop's controls do nothing, so they're dimmed.
  bool loop = isActive() && loop_->getToggleState();
  loop_start_->setActive(loop);
  loop_end_->setActive(loop);
  crossfade_->setActive(loop);
}

void SampleSection::guessTempo() {
  // The sample's tempo from its length: the number of beats (1 to 32, doubling) that gives a tempo nearest 120 in
  // 70-180 BPM. Typing or dragging the BPM box overrides it.
  if (sample_ == nullptr || sample_->sampleRate() <= 0 || sample_->originalLength() <= 0)
    return;
  double seconds = sample_->originalLength() / static_cast<double>(sample_->sampleRate());
  double best = 0.0;
  for (int beats = 1; beats <= 32; beats *= 2) {
    double bpm = 60.0 * beats / seconds;
    if (bpm >= 70.0 && bpm <= 180.0 && (best == 0.0 || std::abs(bpm - 120.0) < std::abs(best - 120.0)))
      best = bpm;
  }
  if (best > 0.0)
    bpm_->setValue(best, sendNotificationSync);
}

void SampleSection::buttonClicked(Button* clicked_button) {
  if (clicked_button == destination_selector_.get()) {
    PopupItems options;
    int num_engine_outputs = vital::constants::kNumEngineOutputs;
    for (int i = 0; i < num_engine_outputs; ++i)
      options.addItem(i, strings::kEngineOutputMenuNames[i]);

    showPopupSelector(this, Point<int>(clicked_button->getX(), clicked_button->getBottom()),options,
                      [=](int selection) { setDestinationSelected(selection); });
  }
  else if (clicked_button == prev_destination_.get()) {
    int new_destination = current_destination_ - 1 + vital::constants::kNumEngineOutputs;
    setDestinationSelected(new_destination % vital::constants::kNumEngineOutputs);
  }
  else if (clicked_button == next_destination_.get()) {
    int new_destination = (current_destination_ + 1) % vital::constants::kNumEngineOutputs;
    setDestinationSelected(new_destination);
  }
  else if (clicked_button == zoom_in_.get() || clicked_button == zoom_out_.get()) {
    sample_viewer_->zoomAroundCenter(clicked_button == zoom_in_.get() ? 2.0f : 0.5f);
  }
  else if (clicked_button == snap_zero_.get())
    sample_viewer_->setSnapToZeroCrossings(snap_zero_->getToggleState());
  else {
    SynthSection::buttonClicked(clicked_button);
    if (clicked_button == loop_.get() || clicked_button == reverse_.get())
      updateMarkers();
    if (clicked_button == loop_.get()) {
      // A loop without a crossfade clicks where it jumps, so engaging LOOP brings XFADE to 5% if it's at zero
      // (in the same undo step as the click).
      if (loop_->getToggleState() && crossfade_->getValue() <= 0.0)
        crossfade_->setValue(kDefaultLoopCrossfade, sendNotificationSync);
      keepMarkersInOrder(nullptr);
      updateLoopControls();
    }
    if (clicked_button == stretch_.get() || clicked_button == tempo_sync_.get())
      updateStretchControls(clicked_button);
  }
}

void SampleSection::setDestinationSelected(int selection) {
  current_destination_ = selection;
  setupDestination();

  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent)
    parent->getSynth()->valueChangedInternal(destination_control_name_, current_destination_);
}

void SampleSection::setupDestination() {
  for (Listener* listener : listeners_)
    listener->sampleDestinationChanged(this, current_destination_);

  destination_text_->setText(strings::kEngineOutputNames[current_destination_]);
}


void SampleSection::prevClicked() {
  File sample_file = LoadSave::getShiftedFile(LoadSave::kSampleFolderName, vital::kSampleExtensionsList,
                                              LoadSave::kAdditionalSampleFoldersName, getCurrentFile(), -1);
  if (sample_file.exists())
    loadFile(sample_file);
  
  updatePopupBrowser(this);
}

void SampleSection::nextClicked() {
  File sample_file = LoadSave::getShiftedFile(LoadSave::kSampleFolderName, vital::kSampleExtensionsList,
                                              LoadSave::kAdditionalSampleFoldersName, getCurrentFile(), 1);
  if (sample_file.exists())
    loadFile(sample_file);
  
  updatePopupBrowser(this);
}

void SampleSection::textMouseDown(const MouseEvent& e) {
  static constexpr int kBrowserWidth = 450;
  static constexpr int kBrowserHeight = 300;

  Rectangle<int> bounds(preset_selector_->getRight(), preset_selector_->getY(),
                        kBrowserWidth * size_ratio_, kBrowserHeight * size_ratio_);
  bounds = getLocalArea(this, bounds);
  showPopupBrowser(this, bounds, LoadSave::getSampleDirectories(), vital::kSampleExtensionsList,
                   LoadSave::kSampleFolderName, LoadSave::kAdditionalSampleFoldersName);
}

void SampleSection::quantizeUpdated() {
  int value = transpose_quantize_button_->getValue();
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent)
    parent->getSynth()->valueChangedInternal(engine_prefix_ + "transpose_quantize", value);
}
