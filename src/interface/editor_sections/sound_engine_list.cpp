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

#include "sound_engine_list.h"

#include "fonts.h"
#include "paths.h"
#include "skin.h"
#include "synth_base.h"
#include "synth_button.h"
#include "synth_gui_interface.h"
#include "synth_oscillator.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

namespace {
  enum AddMenuItems {
    kAddOscillator,
    kAddSample
  };

  // Only drawn while the mouse is over it.
  class HoverScrollBar : public OpenGlScrollBar {
    public:
      void setVisibleColor(Colour color) {
        visible_color_ = color;
        setColor(isMouseOver() || dragging_ ? visible_color_ : Colours::transparentBlack);
      }

      void mouseEnter(const MouseEvent& e) override {
        OpenGlScrollBar::mouseEnter(e);
        setColor(visible_color_);
      }

      void mouseExit(const MouseEvent& e) override {
        OpenGlScrollBar::mouseExit(e);
        if (!dragging_)
          setColor(Colours::transparentBlack);
      }

      void mouseDown(const MouseEvent& e) override {
        dragging_ = true;
        OpenGlScrollBar::mouseDown(e);
      }

      void mouseUp(const MouseEvent& e) override {
        dragging_ = false;
        OpenGlScrollBar::mouseUp(e);
        if (!isMouseOver())
          setColor(Colours::transparentBlack);
      }

    private:
      Colour visible_color_;
      bool dragging_ = false;
  };

  std::string enginePrefix(int engine) {
    return "osc_" + std::to_string(engine + 1) + "_";
  }
}

class SoundEngineContainer : public SynthSection {
  public:
    SoundEngineContainer(String name) : SynthSection(name) { }

    void paintBackground(Graphics& g) override {
      g.fillAll(findColour(Skin::kBackground, true));
      paintChildrenBackgrounds(g);

      float rounding = findValue(Skin::kBodyRounding);
      setLabelFont(g);
      for (const Rectangle<int>& hint : empty_group_hints_) {
        g.setColour(findColour(Skin::kLightenScreen, true));
        Path border;
        border.addRoundedRectangle(hint.toFloat().reduced(0.5f), rounding);
        float dashes[] = { 4.0f * size_ratio_, 3.0f * size_ratio_ };
        PathStrokeType(1.0f).createDashedStroke(border, border, dashes, 2);
        g.fillPath(border);
        g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.6f));
        g.drawText(TRANS("Empty group: click + to add a sound engine"), hint, Justification::centred, false);
      }

      if (!drop_slot_.isEmpty()) {
        Colour accent = findColour(Skin::kWidgetPrimary1, true);
        g.setColour(accent.withAlpha(0.06f));
        g.fillRoundedRectangle(drop_slot_.toFloat(), rounding);
        Path outline;
        outline.addRoundedRectangle(drop_slot_.toFloat().reduced(1.0f), rounding);
        float dashes[] = { 5.0f * size_ratio_, 4.0f * size_ratio_ };
        PathStrokeType(2.0f).createDashedStroke(outline, outline, dashes, 2);
        g.setColour(accent);
        g.fillPath(outline);
        setLabelFont(g);
        g.drawText(drop_slot_text_, drop_slot_, Justification::centred, false);
      }

      // The add group bar is painted here; its button on top only adds the hover highlight.
      if (!add_group_bounds_.isEmpty()) {
        g.setColour(findColour(Skin::kLightenScreen, true));
        g.fillRoundedRectangle(add_group_bounds_.toFloat(), rounding);
        g.setColour(findColour(Skin::kBodyText, true));
        g.setFont(Fonts::instance()->proportional_regular().withPointHeight(findValue(Skin::kLabelHeight) * 1.2f));
        g.drawText(TRANS("+   GROUP"), add_group_bounds_, Justification::centred, false);
      }
    }

    void setEmptyGroupHints(std::vector<Rectangle<int>> hints) { empty_group_hints_ = std::move(hints); }
    void setDropSlot(Rectangle<int> slot, String text) { drop_slot_ = slot; drop_slot_text_ = std::move(text); }
    void setAddGroupBounds(Rectangle<int> bounds) { add_group_bounds_ = bounds; }

    void addGlButton(OpenGlShapeButton* button) {
      addAndMakeVisible(button);
      addOpenGlComponent(button->getGlComponent());
    }

    void addOverlay(OpenGlComponent* overlay) {
      addOpenGlComponent(overlay);
      overlay->setAlwaysOnTop(true);
      overlay->setInterceptsMouseClicks(false, false);
      overlay->setVisible(false);
    }

    void addGlButton(OpenGlToggleButton* button) {
      addAndMakeVisible(button);
      addOpenGlComponent(button->getGlComponent());
    }

  private:
    std::vector<Rectangle<int>> empty_group_hints_;
    Rectangle<int> add_group_bounds_;
    Rectangle<int> drop_slot_;
    String drop_slot_text_;
};

namespace {
  // The header row is shorter than a knob section, so its knobs are drawn smaller.
  constexpr float kHeaderKnobScale = 0.6f;
  constexpr float kBarTint = 0.3f;
}

namespace engine_list {
  // The drawer chevron: Paths::downTriangle's shape, filling more of its box so it reads at title-strip size.
  Path drawerChevron() {
    Path path;
    path.startNewSubPath(0.18f, 0.3f);
    path.lineTo(0.82f, 0.3f);
    path.lineTo(0.5f, 0.72f);
    path.closeSubPath();

    path.addLineSegment(Line<float>(0.0f, 0.0f, 0.0f, 0.0f), 0.2f);
    path.addLineSegment(Line<float>(1.0f, 1.0f, 1.0f, 1.0f), 0.2f);
    return path;
  }
}
using engine_list::drawerChevron;

GroupHeader::GroupHeader(int group) : SynthSection(String("GROUP ") + String(group + 1)), group_(group),
                                      num_engines_(0) {
  std::string prefix = "group_" + std::to_string(group + 1);

  collapse_ = std::make_unique<OpenGlShapeButton>("collapse");
  addAndMakeVisible(collapse_.get());
  addOpenGlComponent(collapse_->getGlComponent());
  collapse_->addListener(this);
  collapse_->setShape(drawerChevron());

  on_ = std::make_unique<SynthButton>(prefix + "_on");
  addButton(on_.get());
  setActivator(on_.get());

  amp_source_ = std::make_unique<TextSelector>(prefix + "_amp_source");
  addSlider(amp_source_.get());
  amp_source_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  amp_source_->setLookAndFeel(TextLookAndFeel::instance());
  amp_source_->setLongStringLookup(strings::kGroupAmpSourceNames);

  level_ = std::make_unique<SynthSlider>(prefix + "_level");
  addSlider(level_.get());
  level_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  level_->setKnobSizeScale(kHeaderKnobScale);

  smoothing_ = std::make_unique<SynthSlider>(prefix + "_smoothing");
  addSlider(smoothing_.get());
  smoothing_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  smoothing_->setKnobSizeScale(kHeaderKnobScale);

  output_ = std::make_unique<TextSelector>(prefix + "_output");
  addSlider(output_.get());
  output_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  output_->setLookAndFeel(TextLookAndFeel::instance());
  output_->setLongStringLookup(strings::kGroupOutputNames);

  add_ = std::make_unique<OpenGlShapeButton>("add_engine");
  addAndMakeVisible(add_.get());
  addOpenGlComponent(add_->getGlComponent());
  add_->addListener(this);
  add_->setTriggeredOnMouseDown(true);

  remove_ = std::make_unique<OpenGlShapeButton>("remove_group");
  addAndMakeVisible(remove_.get());
  addOpenGlComponent(remove_->getGlComponent());
  remove_->addListener(this);
  remove_->setShape(Paths::exitX());
}

// Widths come from the fonts the text is drawn with, so labels never run into the controls.
Font GroupHeader::getNameFont() {
  return Fonts::instance()->proportional_regular().withPointHeight(findValue(Skin::kLabelHeight) * 1.2f);
}

int GroupHeader::getNameRight() {
  // Wide enough for "GROUP 8".
  int widget_margin = findValue(Skin::kWidgetMargin);
  return on_->getRight() + widget_margin + getNameFont().getStringWidth("GROUP 8") + widget_margin;
}

int GroupHeader::getCountWidth() {
  // Wide enough for "16 ENGINES" and a margin either side.
  return getLabelFont().getStringWidth(TRANS("16 ENGINES")) + 2 * findValue(Skin::kWidgetMargin);
}

String GroupHeader::labelText(Label label) {
  static const String kTexts[] = { "AMP", "SMOOTH", "LEVEL", "OUT" };
  return TRANS(kTexts[label]);
}

int GroupHeader::labelWidth(Label label) {
  // The label's text and a margin either side.
  return getLabelFont().getStringWidth(labelText(label)) + 2 * findValue(Skin::kWidgetMargin);
}

void GroupHeader::paintBackground(Graphics& g) {
  // A dark tint of the skin's accent sets each group's bar apart from the engine cards below it.
  Colour body = findColour(Skin::kBody, true);
  Colour bar = body.interpolatedWith(findColour(Skin::kWidgetPrimary1, true), kBarTint);
  g.setColour(bar);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), findValue(Skin::kBodyRounding));
  paintBorder(g);
  paintKnobShadows(g);
  paintOpenGlChildrenBackgrounds(g);

  int widget_margin = findValue(Skin::kWidgetMargin);
  int height = getHeight();
  int name_x = on_->getRight() + widget_margin;

  g.setColour(findColour(Skin::kHeadingText, true));
  g.setFont(getNameFont());
  g.drawText(getName(), name_x, 0, getNameRight() - name_x, height, Justification::centredLeft, false);

  drawTextComponentBackground(g, amp_source_->getBounds(), false);
  drawTextComponentBackground(g, output_->getBounds(), false);

  setLabelFont(g);
  g.setColour(findColour(Skin::kBodyText, true));
  auto label = [&](Label which, Component* control) {
    int width = labelWidth(which);
    g.drawText(labelText(which), control->getX() - width, 0, width - widget_margin, height,
               Justification::centredRight, false);
  };
  label(kAmpLabel, amp_source_.get());
  label(kSmoothLabel, smoothing_.get());
  label(kLevelLabel, level_.get());
  label(kOutLabel, output_.get());

  String count = String(num_engines_) + (num_engines_ == 1 ? TRANS(" ENGINE") : TRANS(" ENGINES"));
  int count_x = add_->getX() - getCountWidth();
  g.drawText(count, count_x, 0, getCountWidth() - widget_margin, height, Justification::centredRight, false);
}

void GroupHeader::setSkinValues(const Skin& skin, bool top_level) {
  SynthSection::setSkinValues(skin, top_level);

  // Vital lifts knobs and selector text to leave room for a label underneath; the header's labels sit
  // to the left instead, so everything is centred in the row.
  for (SynthSlider* slider : { static_cast<SynthSlider*>(amp_source_.get()), level_.get(), smoothing_.get(),
                               static_cast<SynthSlider*>(output_.get()) }) {
    slider->overrideValue(Skin::kKnobOffset, 0.0f);
    slider->overrideValue(Skin::kTextComponentOffset, 0.0f);
  }
}

void GroupHeader::resized() {
  // SynthSection places the power button in the title's corner; the header lays it out itself.
  SynthSection::resized();

  int widget_margin = findValue(Skin::kWidgetMargin);
  int height = getHeight();
  int button_size = height - 2 * widget_margin;

  collapse_->setBounds(widget_margin, widget_margin, button_size, button_size);
  on_->setBounds(collapse_->getRight(), widget_margin, button_size, button_size);
  remove_->setBounds(getWidth() - widget_margin - button_size, widget_margin, button_size, button_size);
  add_->setBounds(remove_->getX() - button_size, widget_margin, button_size, button_size);

  // The four controls, each with its label on its left, share the space between the name and the engine
  // count with equal gaps around them.
  int start = getNameRight() + widget_margin;
  int end = add_->getX() - getCountWidth();
  int knob_width = height;
  int labels = labelWidth(kAmpLabel) + labelWidth(kSmoothLabel) + labelWidth(kLevelLabel) + labelWidth(kOutLabel);
  int min_gap = 2 * widget_margin;
  // The two selectors take what's left after the labels, knobs and minimum gaps, within sensible bounds.
  int selector_width = (end - start - labels - 2 * knob_width - 4 * min_gap) / 2;
  selector_width = std::max<int>(height * 1.8f, std::min<int>(height * 2.4f, selector_width));

  int widths[] = { labelWidth(kAmpLabel) + selector_width, labelWidth(kSmoothLabel) + knob_width,
                   labelWidth(kLevelLabel) + knob_width, labelWidth(kOutLabel) + selector_width };
  int total = 0;
  for (int width : widths)
    total += width;
  int gap = std::max(0, (end - start - total) / 4);
  int x = start + gap / 2;

  amp_source_->setBounds(x + labelWidth(kAmpLabel), widget_margin, selector_width, button_size);
  x += widths[0] + gap;
  smoothing_->setBounds(x + labelWidth(kSmoothLabel), 0, knob_width, height);
  x += widths[1] + gap;
  level_->setBounds(x + labelWidth(kLevelLabel), 0, knob_width, height);
  x += widths[2] + gap;
  output_->setBounds(x + labelWidth(kOutLabel), widget_margin, selector_width, button_size);

  add_->setShape(Paths::plus(button_size));
}

void GroupHeader::buttonClicked(Button* clicked_button) {
  if (clicked_button == collapse_.get()) {
    for (Listener* listener : listeners_)
      listener->groupCollapseToggled(group_);
  }
  else if (clicked_button == add_.get()) {
    for (Listener* listener : listeners_)
      listener->addEngineClicked(group_, add_.get());
  }
  else if (clicked_button == remove_.get()) {
    for (Listener* listener : listeners_)
      listener->removeGroupClicked(group_);
  }
  else {
    SynthSection::buttonClicked(clicked_button);
    if (clicked_button == on_.get()) {
      for (Listener* listener : listeners_)
        listener->groupPowerToggled(group_);
    }
  }
}

void GroupHeader::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
  if (changed_slider != amp_source_.get())
    return;

  // The smoothing follows the source's default unless it has been set by hand.
  static constexpr float kDefaults[] = { vital::constants::kGroupEnvelopeSmoothingDefault,
                                         vital::constants::kGroupLfoSmoothingDefault,
                                         vital::constants::kGroupRandomSmoothingDefault };
  int source = static_cast<int>(std::round(amp_source_->getValue()));
  int kind = source < vital::kNumEnvelopes ? 0 : (source < vital::kNumEnvelopes + vital::kNumLfos ? 1 : 2);
  float current = smoothing_->getValue();
  bool at_a_default = std::any_of(std::begin(kDefaults), std::end(kDefaults),
                                  [=](float value) { return std::abs(current - value) < 1e-4f; });
  if (at_a_default && std::abs(current - kDefaults[kind]) >= 1e-4f)
    smoothing_->setValue(kDefaults[kind], sendNotificationSync);

  for (Listener* listener : listeners_)
    listener->groupAmpSourceChanged(group_);
}

void GroupHeader::setCollapsed(bool collapsed) {
  Path arrow = drawerChevron();
  if (collapsed)
    arrow.applyTransform(AffineTransform::rotation(-MathConstants<float>::halfPi, 0.5f, 0.5f));
  collapse_->setShape(arrow);
}

void GroupHeader::setCanAdd(bool can_add) {
  add_->setEnabled(can_add);
  add_->setAlpha(can_add ? 1.0f : 0.3f);
}

SoundEngineList::SoundEngineList(Authentication* auth, const vital::output_map& mono_modulations,
                                 const vital::output_map& poly_modulations) :
    SynthSection("sound_engines"), hovered_engine_(-1), drag_type_(kNoDrag), dragging_(false),
    drag_engine_(-1), drag_group_(-1), drag_grab_offset_(0), drag_source_(nullptr), drag_copy_(false),
    slot_shift_(0) {
  container_ = std::make_unique<SoundEngineContainer>("sound_engine_container");
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(container_.get(), false);
  viewport_.addListener(this);
  viewport_.setScrollBarsShown(false, false, true, false);

  for (int g = 0; g < vital::kNumOscillatorGroups; ++g) {
    headers_[g] = std::make_unique<GroupHeader>(g);
    container_->addSubSection(headers_[g].get());
    headers_[g]->addListener(this);
    collapsed_[g] = false;
  }

  for (int i = 0; i < vital::kNumOscillators; ++i) {
    oscillators_[i] = std::make_unique<OscillatorSection>(auth, i, mono_modulations, poly_modulations);
    container_->addSubSection(oscillators_[i].get());
    oscillators_[i]->addListener(this);
    oscillators_[i]->setVisible(false);

    samples_[i] = std::make_unique<SampleSection>(String("SMP") + String(i + 1), i, mono_modulations, poly_modulations);
    container_->addSubSection(samples_[i].get());
    samples_[i]->addListener(this);
    samples_[i]->setVisible(false);

    remove_buttons_[i] = std::make_unique<OpenGlShapeButton>("remove_engine");
    container_->addGlButton(remove_buttons_[i].get());
    remove_buttons_[i]->addListener(this);
    remove_buttons_[i]->setShape(Paths::exitX());
    remove_buttons_[i]->setVisible(false);

    advanced_[i] = std::make_unique<OscillatorAdvancedSection>(i + 1, mono_modulations, poly_modulations);
    container_->addSubSection(advanced_[i].get());
    advanced_[i]->passOscillatorSection(oscillators_[i].get());
    advanced_[i]->passSampleSection(samples_[i].get());
    advanced_[i]->setVisible(false);

    drawer_buttons_[i] = std::make_unique<OpenGlShapeButton>("engine_drawer");
    container_->addGlButton(drawer_buttons_[i].get());
    drawer_buttons_[i]->addListener(this);
    drawer_buttons_[i]->setShape(drawerChevron());
    // Drawn in the accent color, like other active controls.
    drawer_buttons_[i]->useOnColors(true);
    drawer_buttons_[i]->setToggleState(true, dontSendNotification);
    drawer_buttons_[i]->setTooltip(TRANS("Advanced: options and unison"));
    drawer_buttons_[i]->setVisible(false);
    drawer_open_[i] = false;

    // The drawer chevron dims with its engine's power button (and its group's, through the layout).
    for (SynthSection* card : { (SynthSection*)oscillators_[i].get(), (SynthSection*)samples_[i].get() }) {
      if (card->activator())
        card->activator()->addListener(this);
    }
  }

  add_group_ = std::make_unique<OpenGlToggleButton>("add_group");
  container_->addGlButton(add_group_.get());
  add_group_->addListener(this);
  add_group_->setLightenButton();

  drag_ghost_ = std::make_unique<OpenGlQuad>(Shaders::kRoundedRectangleFragment);
  container_->addOverlay(drag_ghost_.get());
  dragged_dim_ = std::make_unique<OpenGlQuad>(Shaders::kColorFragment);
  container_->addOverlay(dragged_dim_.get());
  copy_label_ = std::make_unique<PlainTextComponent>("copy", "+ COPY");
  container_->addOverlay(copy_label_.get());
  copy_label_->setFontType(PlainTextComponent::kRegular);
  copy_label_->setJustification(Justification::centredRight);

  addSubSection(container_.get(), false);
  container_->addMouseListener(this, true);
  setWantsKeyboardFocus(true);

  scroll_bar_ = std::make_unique<HoverScrollBar>();
  scroll_bar_->setShrinkLeft(true);
  addAndMakeVisible(scroll_bar_.get());
  addOpenGlComponent(scroll_bar_->getGlComponent());
  scroll_bar_->addListener(this);
  scroll_bar_->setAlwaysOnTop(true);

  setOpaque(false);
}

SoundEngineList::~SoundEngineList() = default;

int SoundEngineList::getOscillatorHeight() {
  return 2 * getKnobSectionHeight() - findValue(Skin::kWidgetMargin);
}

void SoundEngineList::paintBackground(Graphics& g) {
  redoBackgroundImage();
}

void SoundEngineList::redoBackgroundImage() {
  if (container_->getWidth() <= 0)
    return;

  int height = std::max(container_->getHeight(), getHeight());
  int mult = getPixelMultiple();
  Image background_image = Image(Image::ARGB, container_->getWidth() * mult, height * mult, true);

  Graphics background_graphics(background_image);
  background_graphics.addTransform(AffineTransform::scale(mult));
  container_->paintBackground(background_graphics);
  background_.setOwnImage(background_image);
}

void SoundEngineList::resized() {
  ScopedLock lock(open_gl_critical_section_);

  viewport_.setBounds(getLocalBounds());
  int large_padding = findValue(Skin::kLargePadding);
  scroll_bar_->setBounds(getWidth() - large_padding + 1, 0, large_padding - 2, getHeight());
  static_cast<HoverScrollBar*>(scroll_bar_.get())->setVisibleColor(findColour(Skin::kLightenScreen, true));
  updateLayout();

  SynthSection::resized();
}

bool SoundEngineList::isSampleEngine(int engine) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return false;
  float source = parent->getSynth()->getControls()[enginePrefix(engine) + "source"]->value();
  return static_cast<int>(source) == vital::SynthOscillator::kSampleSource;
}

int SoundEngineList::engineGroup(int engine) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return 0;
  float group = parent->getSynth()->getControls()[enginePrefix(engine) + "group"]->value();
  return vital::utils::iclamp(static_cast<int>(group), 0, vital::kNumOscillatorGroups - 1);
}

Rectangle<int> SoundEngineList::engineBounds(int engine) {
  Rectangle<int> bounds = engineCard(engine)->getBounds();
  if (advanced_[engine]->isVisible())
    bounds = bounds.getUnion(advanced_[engine]->getBounds());
  return bounds;
}

SynthSection* SoundEngineList::engineCard(int engine) {
  if (isSampleEngine(engine))
    return samples_[engine].get();
  return oscillators_[engine].get();
}

void SoundEngineList::updateLayout() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr || getWidth() <= 0 || getHeight() <= 0)
    return;

  ScopedLock lock(open_gl_critical_section_);
  SynthBase* synth = parent->getSynth();
  vital::EngineOrder order = synth->getEngineOrder();

  int padding = getPadding();
  int width = viewport_.getWidth();
  int header_height = getTitleWidth() + findValue(Skin::kWidgetMargin);
  int oscillator_height = getOscillatorHeight();
  // Sample cards are as tall as wavetable cards.
  int sample_height = oscillator_height;
  int title_width = getTitleWidth();
  bool can_add = synth->getNumEnginesAdded() < vital::kNumOscillators;

  for (int i = 0; i < vital::kNumOscillators; ++i) {
    oscillators_[i]->setVisible(false);
    samples_[i]->setVisible(false);
    remove_buttons_[i]->setVisible(false);
    advanced_[i]->setVisible(false);
    drawer_buttons_[i]->setVisible(false);
  }

  std::vector<Rectangle<int>> hints;
  rows_.clear();
  shown_groups_.clear();
  int y = 0;
  bool all_groups_shown = true;
  for (int g : synth->getGroupOrder()) {
    if (!synth->isGroupShown(g)) {
      headers_[g]->setVisible(false);
      all_groups_shown = false;
      continue;
    }
    shown_groups_.push_back(g);

    std::vector<int> engines;
    for (int engine : order) {
      if (synth->isEngineAdded(engine) && engineGroup(engine) == g)
        engines.push_back(engine);
    }

    // Groups after the first get a wider gap above them.
    if (y > 0)
      y += 2 * padding;
    headers_[g]->setVisible(true);
    headers_[g]->setName(String("GROUP ") + String(synth->getGroupDisplayNumber(g)));
    headers_[g]->setNumEngines(static_cast<int>(engines.size()));
    headers_[g]->setCollapsed(collapsed_[g]);
    headers_[g]->setCanAdd(can_add);
    headers_[g]->setCanRemove(synth->getNumGroupsShown() > 1);
    bool group_on = synth->getControls()["group_" + std::to_string(g + 1) + "_on"]->value() != 0.0f;
    headers_[g]->setBounds(0, y, width, header_height);
    rows_.push_back({ Row::kHeader, g, -1, 0, headers_[g]->getBounds() });
    y += header_height + padding;

    if (collapsed_[g])
      continue;

    if (engines.empty()) {
      hints.emplace_back(0, y, width, header_height);
      rows_.push_back({ Row::kEmptyGroup, g, -1, 0, hints.back() });
      y += header_height + padding;
    }

    for (int engine : engines) {
      bool sample = isSampleEngine(engine);
      SynthSection* card = sample ? static_cast<SynthSection*>(samples_[engine].get()) : oscillators_[engine].get();
      int height = sample ? sample_height : oscillator_height;
      card->setBounds(0, y, width, height);
      card->setVisible(true);
      // Named by position: per type, within the group, from the top.
      String name = synth->getEngineDisplayName(engine);
      if (card->getName() != name) {
        card->setName(name);
        card->repaintBackground();
      }
      int index = 0;
      while (engines[index] != engine)
        index++;

      int remove_size = title_width * 0.6f;
      int inset = (title_width - remove_size) / 2;
      bool drawer = drawer_open_[engine];
      {
        Path arrow = drawerChevron();
        if (drawer)
          arrow.applyTransform(AffineTransform::rotation(MathConstants<float>::pi, 0.5f, 0.5f));
        drawer_buttons_[engine]->setShape(arrow);
        // At the very bottom of the title strip, clear of the engine's name.
        int drawer_size = title_width * 0.8f;
        int drawer_inset = (title_width - drawer_size) / 2;
        drawer_buttons_[engine]->setBounds(drawer_inset, y + height - drawer_size - drawer_inset,
                                           drawer_size, drawer_size);
        drawer_buttons_[engine]->setVisible(true);
      }
      if (drawer) {
        float source = synth->getControls()[enginePrefix(engine) + "source"]->value();
        advanced_[engine]->setSample(sample);
        advanced_[engine]->setNoise(static_cast<int>(source) == vital::SynthOscillator::kNoiseSource);
        advanced_[engine]->setBounds(0, y + height + padding, width, oscillator_height);
        advanced_[engine]->setVisible(true);
      }
      rows_.push_back({ Row::kCard, g, engine, index, engineBounds(engine) });
      // The drawer first, then the card: a control on both (a sample's STEREO) is the card's.
      for (Listener* listener : listeners_) {
        if (drawer)
          listener->soundEngineCardShown(advanced_[engine].get());
        listener->soundEngineCardShown(card);
      }
      // Engines in a switched off group are dimmed like switched off engines.
      bool engine_on = synth->getControls()[enginePrefix(engine) + "on"]->value() != 0.0f;
      card->setParentActive(group_on);
      advanced_[engine]->setGroupActive(group_on);
      card->setActive(engine_on && group_on);
      // An open drawer dims with its engine and group. Back on, it re-applies its own rules (unison knobs that
      // need more than one voice, the group unison mode).
      // The drawer itself is switched too, not just its panels: setActive skips a section already in that state,
      // so a drawer left marked off would ignore the next switch-off.
      if (drawer && engine_on && group_on) {
        advanced_[engine]->setActive(true);
        advanced_[engine]->setAllValues(synth->getControls());
      }
      else if (drawer)
        advanced_[engine]->setActive(false);
      if (SynthButton* power = dynamic_cast<SynthButton*>(card->activator()))
        power->getGlComponent()->setDimmed(!group_on);
      updateDrawerButton(engine);

      // The remove button shows on hover, just under the engine's power button.
      remove_buttons_[engine]->setBounds(inset, y + title_width, remove_size, remove_size);
      // The engine's name centres between the power button and the drawer chevron (or the bottom of the strip
      // for samples); the remove button, shown on hover, stays clear of it.
      int name_top = title_width;
      int name_bottom = y + height - drawer_buttons_[engine]->getY();
      card->setSidewaysHeadingInsets(name_top, name_bottom);
      remove_buttons_[engine]->setVisible(engine == hovered_engine_);
      y += height + padding;
      if (drawer)
        y += oscillator_height + padding;
    }
  }

  add_group_->setVisible(!all_groups_shown);
  if (!all_groups_shown) {
    add_group_->setBounds(0, y, width, header_height);
    container_->setAddGroupBounds(add_group_->getBounds());
    y += header_height;
  }
  else {
    container_->setAddGroupBounds(Rectangle<int>());
    y -= padding;
  }

  // While dragging, a dashed slot opens where the item will land and everything below it moves down.
  placeDropSlot(width);
  if (!slot_.isEmpty()) {
    auto shift = [&](Component* component) {
      if (component->isVisible() && component->getY() >= slot_.getY())
        component->setTopLeftPosition(component->getX(), component->getY() + slot_shift_);
    };
    for (int g = 0; g < vital::kNumOscillatorGroups; ++g)
      shift(headers_[g].get());
    for (int i = 0; i < vital::kNumOscillators; ++i) {
      shift(oscillators_[i].get());
      shift(samples_[i].get());
      shift(remove_buttons_[i].get());
      shift(advanced_[i].get());
      shift(drawer_buttons_[i].get());
    }
    shift(add_group_.get());
    if (add_group_->isVisible())
      container_->setAddGroupBounds(add_group_->getBounds());

    std::vector<Rectangle<int>> shifted_hints;
    for (const Rectangle<int>& hint : hints) {
      bool replaced = drag_type_ == kEngineDrag && hint.getY() == slot_.getY();
      if (!replaced)
        shifted_hints.push_back(hint.getY() >= slot_.getY() ? hint.translated(0, slot_shift_) : hint);
    }
    hints = shifted_hints;
    y += slot_shift_;
  }
  container_->setDropSlot(slot_, drag_copy_ ? TRANS("DROP COPY HERE") : TRANS("DROP HERE"));

  {
    ScopedLock dim_lock(open_gl_critical_section_);
    bool dim = dragging_ && !drag_copy_;
    dragged_dim_->setVisible(dim);
    if (dim) {
      Rectangle<int> original = drag_type_ == kGroupDrag ? headers_[drag_group_]->getBounds() :
                                                           engineBounds(drag_engine_);
      if (drag_type_ == kGroupDrag) {
        for (int i = 0; i < vital::kNumOscillators; ++i) {
          if (engineCard(i)->isVisible() && engineGroup(i) == drag_group_)
            original = original.getUnion(engineBounds(i));
        }
      }
      dragged_dim_->setBounds(original);
      dragged_dim_->setColor(findColour(Skin::kBackground, true).withAlpha(0.7f));
    }
  }

  Point<int> position = viewport_.getViewPosition();
  container_->setEmptyGroupHints(hints);
  container_->setBounds(0, 0, width, std::max(y, 1));
  viewport_.setViewPosition(position);
  container_->setScrollWheelEnabled(container_->getHeight() <= viewport_.getHeight());
  setScrollBarRange();

  for (Listener* listener : listeners_)
    listener->soundEnginesMoved();

  repaintBackground();
}

void SoundEngineList::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  updateLayout();
}

void SoundEngineList::buttonClicked(Button* clicked_button) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;

  if (clicked_button == add_group_.get()) {
    parent->getSynth()->recordAddition(true, parent->getSynth()->addGroup());
    updateLayout();
    return;
  }

  for (int i = 0; i < vital::kNumOscillators; ++i) {
    if (clicked_button == oscillators_[i]->activator() || clicked_button == samples_[i]->activator()) {
      updateDrawerButton(i);
      return;
    }
    if (clicked_button == drawer_buttons_[i].get()) {
      // Clicking flips the button's toggle state; keep it on so the chevron stays red both ways.
      drawer_buttons_[i]->setToggleState(true, dontSendNotification);
      drawer_open_[i] = !drawer_open_[i];
      updateLayout();
      return;
    }
    if (clicked_button == remove_buttons_[i].get()) {
      hovered_engine_ = -1;
      parent->getSynth()->removeSoundEngine(i);
      parent->updateFullGui();
      return;
    }
  }

  SynthSection::buttonClicked(clicked_button);
}

void SoundEngineList::updateDrawerButton(int engine) {
  // Red while its engine and group are on, grey (like other switched off controls) otherwise.
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;
  vital::control_map& controls = parent->getSynth()->getControls();
  bool engine_on = controls[enginePrefix(engine) + "on"]->value() != 0.0f;
  bool group_on = controls["group_" + std::to_string(engineGroup(engine) + 1) + "_on"]->value() != 0.0f;
  drawer_buttons_[engine]->useOnColors(engine_on && group_on);
}

void SoundEngineList::showRemoveButton(int engine) {
  if (engine == hovered_engine_)
    return;

  if (hovered_engine_ >= 0)
    remove_buttons_[hovered_engine_]->setVisible(false);
  hovered_engine_ = engine;
  if (hovered_engine_ >= 0)
    remove_buttons_[hovered_engine_]->setVisible(true);
}

void SoundEngineList::mouseMove(const MouseEvent& e) {
  if (dragging_)
    return;
  Point<int> position = e.getEventRelativeTo(container_.get()).getPosition();
  int engine = -1;
  for (int i = 0; i < vital::kNumOscillators && engine < 0; ++i) {
    if ((oscillators_[i]->isVisible() && oscillators_[i]->getBounds().contains(position)) ||
        (samples_[i]->isVisible() && samples_[i]->getBounds().contains(position))) {
      engine = i;
    }
  }
  showRemoveButton(engine);
}

bool SoundEngineList::keyPressed(const KeyPress& key) {
  // Undo and redo are handled by FullInterface for the whole window.
  if (key.getModifiers().isCommandDown())
    hovered_engine_ = -1;
  return false;
}

bool SoundEngineList::undoRemoval() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr || !parent->getSynth()->undoRemoval())
    return false;
  parent->updateFullGui();
  return true;
}

bool SoundEngineList::redoRemoval() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr || !parent->getSynth()->redoRemoval())
    return false;
  hovered_engine_ = -1;
  parent->updateFullGui();
  return true;
}

void SoundEngineList::showUndoMenu(Component* source, Point<int> position) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;

  enum { kUndo = 1, kRedo };
  std::string undo = parent->getSynth()->getUndoRemovalName();
  std::string redo = parent->getSynth()->getRedoRemovalName();
  if (undo.empty() && redo.empty())
    return;

  PopupItems options;
  if (!undo.empty())
    options.addItem(kUndo, "Undo " + undo);
  if (!redo.empty())
    options.addItem(kRedo, "Redo " + redo);
  showPopupSelector(this, getLocalPoint(source, position), options, [=](int selection) {
    if (selection == kUndo)
      undoRemoval();
    else if (selection == kRedo)
      redoRemoval();
  });
}

void SoundEngineList::mouseDown(const MouseEvent& e) {
  drag_type_ = kNoDrag;
  dragging_ = false;
  Component* source = e.originalComponent;

  bool background = source == container_.get();
  for (int g = 0; g < vital::kNumOscillatorGroups; ++g)
    background = background || source == headers_[g].get();

  // Take keyboard focus (for undo and redo) only from surfaces that open nothing. Controls such as the + and the
  // selectors open popups on mouse down, and a popup closes as soon as it loses focus.
  bool card = false;
  for (int i = 0; i < vital::kNumOscillators; ++i)
    card = card || source == engineCard(i);
  if (background || card)
    grabKeyboardFocus();
  if (e.mods.isPopupMenu()) {
    if (background)
      showUndoMenu(source, e.getPosition());
    return;
  }

  Point<int> position = e.getEventRelativeTo(container_.get()).getPosition();

  // Groups are dragged by their bar, engines by the title strip down their left side.
  Rectangle<int> dragged;
  for (int g = 0; g < vital::kNumOscillatorGroups; ++g) {
    if (source == headers_[g].get()) {
      drag_type_ = kGroupDrag;
      drag_group_ = g;
      dragged = groupBlockBounds(g);
    }
  }
  for (int i = 0; i < vital::kNumOscillators; ++i) {
    SynthSection* card = engineCard(i);
    if (source == card && e.getEventRelativeTo(card).x < getTitleWidth()) {
      drag_type_ = kEngineDrag;
      drag_engine_ = i;
      drag_group_ = engineGroup(i);
      dragged = engineBounds(i);
    }
  }

  drag_bounds_ = dragged;
  drag_grab_offset_ = position.y - dragged.getY();
  drag_start_ = position;
  drag_source_ = source;
}

Rectangle<int> SoundEngineList::groupBlockBounds(int group) {
  Rectangle<int> block;
  for (const Row& row : rows_) {
    if (row.group == group)
      block = block.isEmpty() ? row.bounds : block.getUnion(row.bounds);
  }
  return block;
}

bool SoundEngineList::canCopyDragged() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return false;
  SynthBase* synth = parent->getSynth();
  if (drag_type_ == kEngineDrag)
    return synth->getNumEnginesAdded() < vital::kNumOscillators;
  return synth->getNumGroupsShown() < vital::kNumOscillatorGroups &&
         synth->getNumEnginesAdded() + headers_[drag_group_]->getNumEngines() <= vital::kNumOscillators;
}

void SoundEngineList::mouseDrag(const MouseEvent& e) {
  if (drag_type_ == kNoDrag)
    return;

  Point<int> position = e.getEventRelativeTo(container_.get()).getPosition();
  if (!dragging_) {
    static constexpr int kDragStartDistance = 5;
    if (position.getDistanceFrom(drag_start_) < kDragStartDistance)
      return;
    dragging_ = true;
    showRemoveButton(-1);
    drag_source_->setMouseCursor(MouseCursor::DraggingHandCursor);
  }

  Point<int> in_viewport = e.getEventRelativeTo(&viewport_).getPosition();
  viewport_.autoScroll(in_viewport.x, in_viewport.y, getTitleWidth(), getTitleWidth() / 2);
  updateDrag(e);
}

int SoundEngineList::gapFreeY(int y) const {
  // Targets are worked out on the layout without the open slot, so the list doesn't jitter as it moves.
  if (!slot_.isEmpty()) {
    if (y >= slot_.getY() + slot_shift_)
      return y - slot_shift_;
    if (y >= slot_.getY())
      return slot_.getY();
  }
  return y;
}

void SoundEngineList::updateDrag(const MouseEvent& e) {
  int y = e.getEventRelativeTo(container_.get()).y;
  bool copy = e.mods.isCommandDown();
  DropTarget target = drag_type_ == kGroupDrag ? groupDropTarget(gapFreeY(y)) : engineDropTarget(gapFreeY(y));
  if (copy && !canCopyDragged())
    target.valid = false;

  if (target.valid != drop_target_.valid || target.group != drop_target_.group ||
      target.position != drop_target_.position || copy != drag_copy_) {
    drop_target_ = target;
    drag_copy_ = copy;
    updateLayout();
  }

  ScopedLock lock(open_gl_critical_section_);
  Rectangle<int> ghost = drag_bounds_.withY(y - drag_grab_offset_);
  drag_ghost_->setBounds(ghost);
  drag_ghost_->setColor(copy ? findColour(Skin::kWidgetPrimary1, true).withAlpha(0.35f) :
                               findColour(Skin::kLightenScreen, true));
  drag_ghost_->setRounding(findValue(Skin::kBodyRounding));
  drag_ghost_->setVisible(true);

  int label_height = getTitleWidth() * 0.6f;
  int label_width = label_height * 3.5f;
  copy_label_->setBounds(ghost.getRight() - label_width - getPadding(), ghost.getY() + getPadding(),
                         label_width, label_height);
  copy_label_->setText(canCopyDragged() ? TRANS("+ COPY") : TRANS("FULL"));
  copy_label_->setTextSize(label_height * 0.7f);
  copy_label_->setColor(findColour(Skin::kWidgetPrimary1, true));
  copy_label_->setVisible(copy);
}

void SoundEngineList::mouseUp(const MouseEvent& e) {
  if (!dragging_) {
    drag_type_ = kNoDrag;
    return;
  }

  bool copy = e.mods.isCommandDown();
  DragType type = drag_type_;
  DropTarget target = drop_target_;
  endDrag();

  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr || !target.valid) {
    updateLayout();
    return;
  }

  SynthBase* synth = parent->getSynth();
  if (type == kEngineDrag) {
    if (copy)
      synth->recordAddition(false, synth->duplicateSoundEngine(drag_engine_, target.group, target.position, false));
    else {
      int old_group = static_cast<int>(synth->getControls()[enginePrefix(drag_engine_) + "group"]->value());
      int old_position = synth->getEnginePosition(drag_engine_);
      // The target counts the dragged engine, which the move takes out first.
      int position = target.position;
      int index = 0;
      for (const Row& row : rows_) {
        if (row.type == Row::kCard && row.engine == drag_engine_)
          index = row.index;
      }
      if (target.group == drag_group_ && index < position)
        position--;
      synth->moveSoundEngine(drag_engine_, target.group, position);
      synth->recordMove(false, drag_engine_, old_group, old_position);
    }
  }
  else {
    if (copy)
      synth->recordAddition(true, synth->duplicateGroup(drag_group_, target.position));
    else {
      int old_position = synth->getGroupPosition(drag_group_);
      int position = target.position;
      int index = static_cast<int>(std::find(shown_groups_.begin(), shown_groups_.end(), drag_group_) -
                                   shown_groups_.begin());
      if (index < position)
        position--;
      synth->moveGroup(drag_group_, position);
      synth->recordMove(true, drag_group_, 0, old_position);
    }
  }
  parent->updateFullGui();
}

void SoundEngineList::endDrag() {
  {
    ScopedLock lock(open_gl_critical_section_);
    if (drag_source_)
      drag_source_->setMouseCursor(MouseCursor::NormalCursor);
    drag_ghost_->setVisible(false);
    copy_label_->setVisible(false);
    dragged_dim_->setVisible(false);
  }
  drag_type_ = kNoDrag;
  dragging_ = false;
  drop_target_ = DropTarget();
}

SoundEngineList::DropTarget SoundEngineList::engineDropTarget(int y) {
  DropTarget target;
  if (rows_.empty())
    return target;

  int padding = getPadding();

  // The row the mouse is over, or the one above the gap it's in. Below the last group isn't a group.
  const Row* over = nullptr;
  const Row* last = nullptr;
  for (const Row& row : rows_) {
    if (row.type == Row::kAddGroup)
      continue;
    last = &row;
    if (y >= row.bounds.getY() - padding)
      over = &row;
  }
  if (last == nullptr)
    return target;
  if (over == nullptr)
    over = &rows_.front();
  if (y > last->bounds.getBottom() + padding)
    return target;

  target.valid = true;
  target.group = over->group;
  if (over->type == Row::kHeader)
    target.position = collapsed_[over->group] ? headers_[over->group]->getNumEngines() : 0;
  else if (over->type == Row::kEmptyGroup)
    target.position = 0;
  else
    target.position = y < over->bounds.getCentreY() ? over->index : over->index + 1;
  return target;
}

SoundEngineList::DropTarget SoundEngineList::groupDropTarget(int y) {
  DropTarget target;
  if (shown_groups_.empty())
    return target;

  // The drop goes before the first group whose middle is below the mouse.
  int position = 0;
  for (int group : shown_groups_) {
    if (y > groupBlockBounds(group).getCentreY())
      position++;
  }
  target.valid = true;
  target.position = position;
  return target;
}

void SoundEngineList::placeDropSlot(int width) {
  slot_ = Rectangle<int>();
  slot_shift_ = 0;
  if (!dragging_ || !drop_target_.valid)
    return;

  int padding = getPadding();
  int group_gap = 3 * padding;
  int height = drag_bounds_.getHeight();
  int slot_y = 0;
  int replaced = 0;

  if (drag_type_ == kGroupDrag) {
    height = std::min(height, viewport_.getHeight() / 3);
    if (drop_target_.position < static_cast<int>(shown_groups_.size()))
      slot_y = groupBlockBounds(shown_groups_[drop_target_.position]).getY();
    else
      slot_y = groupBlockBounds(shown_groups_.back()).getBottom() + group_gap;
    slot_shift_ = height + group_gap;
  }
  else {
    int group = drop_target_.group;
    const Row* header = nullptr;
    const Row* hint = nullptr;
    std::vector<const Row*> cards;
    for (const Row& row : rows_) {
      if (row.group != group)
        continue;
      if (row.type == Row::kHeader)
        header = &row;
      else if (row.type == Row::kEmptyGroup)
        hint = &row;
      else if (row.type == Row::kCard)
        cards.push_back(&row);
    }
    if (header == nullptr)
      return;

    if (hint) {
      slot_y = hint->bounds.getY();
      replaced = hint->bounds.getHeight() + padding;
    }
    else if (collapsed_[group] || cards.empty())
      slot_y = header->bounds.getBottom() + padding;
    else if (drop_target_.position < static_cast<int>(cards.size()))
      slot_y = cards[drop_target_.position]->bounds.getY();
    else
      slot_y = cards.back()->bounds.getBottom() + padding;
    slot_shift_ = height + padding - replaced;
  }
  slot_ = Rectangle<int>(0, slot_y, width, height);
}

void SoundEngineList::mouseExit(const MouseEvent& e) {
  if (!getLocalBounds().contains(e.getEventRelativeTo(this).getPosition()))
    showRemoveButton(-1);
}

void SoundEngineList::groupCollapseToggled(int group) {
  collapsed_[group] = !collapsed_[group];
  updateLayout();
}

void SoundEngineList::removeGroupClicked(int group) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;

  hovered_engine_ = -1;
  collapsed_[group] = false;
  parent->getSynth()->removeGroup(group);
  parent->updateFullGui();
}

void SoundEngineList::addEngineClicked(int group, Component* source) {
  PopupItems options;
  options.addItem(kAddOscillator, TRANS("Wavetable / Noise").toStdString());
  options.addItem(kAddSample, TRANS("Sample").toStdString());

  Point<int> position = getLocalPoint(source, Point<int>(0, source->getHeight()));
  showPopupSelector(this, position, options, [=](int selection) {
    SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
    if (parent == nullptr)
      return;

    int source_type = selection == kAddSample ? vital::SynthOscillator::kSampleSource :
                                                vital::SynthOscillator::kWavetableSource;
    collapsed_[group] = false;
    int engine = parent->getSynth()->addSoundEngine(group, source_type);
    parent->getSynth()->recordAddition(false, engine);
    parent->updateFullGui();
    if (engine >= 0) {
      SynthSection* card = engineCard(engine);
      viewport_.setViewPosition(0, std::max(0, card->getBottom() - viewport_.getHeight()));
    }
  });
}

void SoundEngineList::initOpenGlComponents(OpenGlWrapper& open_gl) {
  background_.init(open_gl);
  SynthSection::initOpenGlComponents(open_gl);
}

void SoundEngineList::renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) {
  ScopedLock lock(open_gl_critical_section_);

  OpenGlComponent::setViewPort(&viewport_, open_gl);

  float image_width = vital::utils::nextPowerOfTwo(background_.getImageWidth());
  float image_height = vital::utils::nextPowerOfTwo(background_.getImageHeight());
  int mult = getPixelMultiple();
  float width_ratio = image_width / (container_->getWidth() * mult);
  float height_ratio = image_height / (viewport_.getHeight() * mult);
  float y_offset = (2.0f * viewport_.getViewPositionY()) / getHeight();

  background_.setTopLeft(-1.0f, 1.0f + y_offset);
  background_.setTopRight(-1.0 + 2.0f * width_ratio, 1.0f + y_offset);
  background_.setBottomLeft(-1.0f, 1.0f - 2.0f * height_ratio + y_offset);
  background_.setBottomRight(-1.0 + 2.0f * width_ratio, 1.0f - 2.0f * height_ratio + y_offset);

  background_.setColor(Colours::white);
  background_.drawImage(open_gl);
  for (Listener* listener : listeners_)
    listener->renderSoundEngineMeters(open_gl, animate, &viewport_);
  SynthSection::renderOpenGlComponents(open_gl, animate);
}

void SoundEngineList::destroyOpenGlComponents(OpenGlWrapper& open_gl) {
  background_.destroy(open_gl);
  SynthSection::destroyOpenGlComponents(open_gl);
}

void SoundEngineList::scrollBarMoved(ScrollBar* scroll_bar, double range_start) {
  viewport_.setViewPosition(Point<int>(0, range_start));
}

void SoundEngineList::soundEnginesScrolled(int position) {
  setScrollBarRange();
  scroll_bar_->setCurrentRange(position, viewport_.getHeight());

  for (Listener* listener : listeners_)
    listener->soundEnginesMoved();
}

void SoundEngineList::setScrollBarRange() {
  scroll_bar_->setRangeLimits(0.0, container_->getHeight());
  scroll_bar_->setCurrentRange(scroll_bar_->getCurrentRangeStart(), viewport_.getHeight(), dontSendNotification);
}
