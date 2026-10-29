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

#include "group_settings_section.h"

#include "fonts.h"
#include "skin.h"
#include "synth_base.h"
#include "synth_gui_interface.h"
#include "synth_oscillator.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

namespace groups_page {
  constexpr float kBarTint = 0.3f;

  // Drift and humanise share the first column, one above the other, as separate panels.
  enum Panel { kDriftPanel, kHumanizePanel, kVoicePanel, kUnisonPanel, kNumPanels };
  enum Column { kDriftColumn, kVoiceColumn, kUnisonColumn, kNumColumns };
  constexpr float kColumnWidths[kNumColumns] = { 0.36f, 0.20f, 0.44f };
  constexpr int kPanelColumns[kNumPanels] = { kDriftColumn, kDriftColumn, kVoiceColumn, kUnisonColumn };

  const char* const kPanelTitles[kNumPanels] = { "DRIFT", "HUMANISE", "VOICE", "UNISON" };

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

  String joined(const std::vector<std::string>& items) {
    String text;
    for (size_t i = 0; i < items.size(); ++i)
      text += String(i ? ", " : "") + String(items[i]);
    return text;
  }
}

using namespace groups_page;

class GroupSettingsContainer : public SynthSection {
  public:
    GroupSettingsContainer(String name) : SynthSection(name) { }

    void paintBackground(Graphics& g) override {
      g.fillAll(findColour(Skin::kBackground, true));
      paintChildrenBackgrounds(g);
    }
};

GroupSettingsSection::GroupSettingsSection(int group) : SynthSection(String("GROUP ") + String(group + 1)),
                                                        group_(group) {
  std::string prefix = "group_" + std::to_string(group + 1) + "_";
  auto knob = [&](std::unique_ptr<SynthSlider>& slider, const std::string& name) {
    slider = std::make_unique<SynthSlider>(prefix + name);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  };
  auto selector = [&](std::unique_ptr<TextSelector>& slider, const std::string& name, const std::string* lookup) {
    slider = std::make_unique<TextSelector>(prefix + name);
    addSlider(slider.get());
    slider->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    slider->setLookAndFeel(TextLookAndFeel::instance());
    slider->setLongStringLookup(lookup);
  };

  knob(drift_, "drift");
  knob(drift_rate_, "drift_rate");
  knob(drift_spread_, "drift_spread");
  knob(humanize_pitch_, "humanize_pitch");
  knob(humanize_level_, "humanize_level");
  knob(humanize_pan_, "humanize_pan");
  knob(humanize_phase_, "humanize_phase");

  selector(voice_mode_, "voice_mode", strings::kGroupVoiceModeNames);
  knob(glide_, "glide");
  knob(glide_slope_, "glide_slope");
  glide_slope_->setBipolar(true);

  knob(unison_voices_, "unison_voices");
  knob(unison_detune_, "unison_detune");
  knob(unison_blend_, "unison_blend");
  knob(stereo_spread_, "stereo_spread");
  selector(stack_style_, "stack_style", strings::kUnisonStackNames);
}

int GroupSettingsSection::getBarHeight() {
  return getTitleWidth() + findValue(Skin::kWidgetMargin);
}

Rectangle<int> GroupSettingsSection::panelBounds(int panel) {
  int padding = findValue(Skin::kPadding);
  int top = getBarHeight() + padding;
  int column = kPanelColumns[panel];
  int x = 0;
  for (int c = 0; c < column; ++c)
    x += getWidth() * kColumnWidths[c];
  int right = column == kNumColumns - 1 ? getWidth() : x + getWidth() * kColumnWidths[column] - padding;
  Rectangle<int> bounds(x, top, right - x, getHeight() - top);

  if (column != kDriftColumn)
    return bounds;
  int half = (bounds.getHeight() - padding) / 2;
  if (panel == kDriftPanel)
    return bounds.withHeight(half);
  return bounds.withTop(bounds.getBottom() - half);
}

int GroupSettingsSection::getPanelTitleHeight() {
  return getTitleWidth() * 0.7f;
}

void GroupSettingsSection::paintBackground(Graphics& g) {
  float rounding = findValue(Skin::kBodyRounding);
  int widget_margin = findValue(Skin::kWidgetMargin);

  // The bar matches the voice tab's group bar.
  Rectangle<int> bar(0, 0, getWidth(), getBarHeight());
  Colour body = findColour(Skin::kBody, true);
  g.setColour(body.interpolatedWith(findColour(Skin::kWidgetPrimary1, true), groups_page::kBarTint));
  g.fillRoundedRectangle(bar.toFloat(), rounding);

  g.setColour(findColour(Skin::kHeadingText, true));
  g.setFont(Fonts::instance()->proportional_regular().withPointHeight(findValue(Skin::kLabelHeight) * 1.2f));
  int name_width = bar.getHeight() * 3;
  g.drawText(getName(), widget_margin * 2, 0, name_width, bar.getHeight(), Justification::centredLeft, false);

  setLabelFont(g);
  g.setColour(findColour(Skin::kBodyText, true));
  String engines = engines_.empty() ? TRANS("NO SOUND ENGINES") : joined(engines_);
  int engines_x = widget_margin * 2 + name_width;
  g.drawText(engines, engines_x, 0, getWidth() - engines_x - widget_margin * 2, bar.getHeight(),
             Justification::centredLeft, true);

  for (int p = 0; p < kNumPanels; ++p) {
    g.setColour(body);
    g.fillRoundedRectangle(panelBounds(p).toFloat(), rounding);
  }

  paintKnobShadows(g);
  paintOpenGlChildrenBackgrounds(g);

  setLabelFont(g);
  g.setColour(findColour(Skin::kHeadingText, true));
  for (int p = 0; p < kNumPanels; ++p) {
    if (p == kUnisonPanel)
      continue;
    Rectangle<int> panel = panelBounds(p);
    g.drawText(TRANS(kPanelTitles[p]), panel.getX() + widget_margin * 2, panel.getY(),
               panel.getWidth() - widget_margin * 4, getPanelTitleHeight(), Justification::centredLeft, false);
  }

  // UNISON takes the engine drawer's format: a sideways title on a heading-colored strip.
  Rectangle<int> unison = panelBounds(kUnisonPanel);
  int strip_width = getTitleWidth();
  g.saveState();
  g.reduceClipRegion(unison.withWidth(strip_width));
  g.setColour(findColour(Skin::kBodyHeading, true));
  g.fillRoundedRectangle(unison.withWidth(strip_width * 2).toFloat(), findValue(Skin::kBodyRounding));
  g.restoreState();

  g.saveState();
  g.setColour(findColour(Skin::kHeadingText, true));
  g.setFont(Fonts::instance()->proportional_light().withPointHeight(getSizeRatio() * 14.0f));
  g.setOrigin(Point<int>(unison.getX(), unison.getBottom()));
  g.addTransform(AffineTransform::rotation(-vital::kPi / 2.0f));
  g.drawText(TRANS(kPanelTitles[kUnisonPanel]), Rectangle<int>(0, 0, unison.getHeight(), strip_width),
             Justification::centred, false);
  g.restoreState();

  drawTextComponentBackground(g, voice_mode_->getBounds(), true);
  drawTextComponentBackground(g, stack_style_->getBounds(), true);

  setLabelFont(g);
  drawLabelForComponent(g, TRANS("AMOUNT"), drift_.get());
  drawLabelForComponent(g, TRANS("RATE"), drift_rate_.get());
  drawLabelForComponent(g, TRANS("SPREAD"), drift_spread_.get());
  drawLabelForComponent(g, TRANS("PITCH"), humanize_pitch_.get());
  drawLabelForComponent(g, TRANS("LEVEL"), humanize_level_.get());
  drawLabelForComponent(g, TRANS("PAN"), humanize_pan_.get());
  drawLabelForComponent(g, TRANS("PHASE"), humanize_phase_.get());

  drawLabelForComponent(g, TRANS("MODE"), voice_mode_.get(), true);
  drawLabelForComponent(g, TRANS("GLIDE"), glide_.get());
  drawLabelForComponent(g, TRANS("SLOPE"), glide_slope_.get());

  drawLabelForComponent(g, TRANS("VOICES"), unison_voices_.get());
  drawLabelForComponent(g, TRANS("DETUNE"), unison_detune_.get());
  drawLabelForComponent(g, TRANS("BLEND"), unison_blend_.get());
  drawLabelForComponent(g, TRANS("STEREO"), stereo_spread_.get());
  drawLabelForComponent(g, TRANS("STACK"), stack_style_.get(), true);

}

void GroupSettingsSection::resized() {
  SynthSection::resized();

  int widget_margin = findValue(Skin::kWidgetMargin);
  int knob_height = getKnobSectionHeight();
  int title_height = getPanelTitleHeight();

  auto rows = [&](int panel) {
    Rectangle<int> area = panelBounds(panel);
    int row_height = (area.getHeight() - title_height) / 2;
    Rectangle<int> first(area.getX(), area.getY() + title_height, area.getWidth(), row_height);
    return std::make_pair(first, first.translated(0, row_height));
  };
  auto knobRow = [&](int panel) { return panelBounds(panel).withTrimmedTop(title_height); };


  placeKnobsInArea(knobRow(kDriftPanel), { drift_.get(), drift_rate_.get(), drift_spread_.get() });
  placeKnobsInArea(knobRow(kHumanizePanel), { humanize_pitch_.get(), humanize_level_.get(), humanize_pan_.get(),
                                              humanize_phase_.get() });

  // Text boxes are a knob row tall with their label inside, under the value, as in the engine drawer.
  auto textBox = [&](Component* component, Rectangle<int> row) {
    int height = knob_height - 2 * widget_margin;
    int y = row.getY() + (row.getHeight() - height) / 2;
    component->setBounds(row.getX() + widget_margin, y, row.getWidth() - 2 * widget_margin, height);
  };

  // The mode box fills the panel's width.
  auto voice = rows(kVoicePanel);
  textBox(voice_mode_.get(), voice.first);
  placeKnobsInArea(voice.second, { glide_.get(), glide_slope_.get() });

  // UNISON: the four knobs in a row beside the title strip, the stack box under them spanning all four.
  Rectangle<int> unison = panelBounds(kUnisonPanel).withTrimmedLeft(getTitleWidth());
  Rectangle<int> knob_row = unison.withHeight(unison.getHeight() / 2);
  Rectangle<int> stack_row = unison.withTrimmedTop(knob_row.getHeight());
  placeKnobsInArea(knob_row, { unison_voices_.get(), unison_detune_.get(), unison_blend_.get(),
                               stereo_spread_.get() });
  textBox(stack_style_.get(), stack_row.withLeft(unison_voices_->getX() - widget_margin)
                                       .withRight(stereo_spread_->getRight() + widget_margin));
}

GroupSettingsList::GroupSettingsList() : SynthSection("group_settings") {
  container_ = std::make_unique<GroupSettingsContainer>("group_settings_container");
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(container_.get(), false);
  viewport_.addListener(this);
  viewport_.setScrollBarsShown(false, false, true, false);

  for (int g = 0; g < vital::kNumOscillatorGroups; ++g) {
    sections_[g] = std::make_unique<GroupSettingsSection>(g);
    container_->addSubSection(sections_[g].get());
    sections_[g]->setVisible(false);
  }
  addSubSection(container_.get(), false);

  scroll_bar_ = std::make_unique<groups_page::HoverScrollBar>();
  scroll_bar_->setShrinkLeft(true);
  addAndMakeVisible(scroll_bar_.get());
  addOpenGlComponent(scroll_bar_->getGlComponent());
  scroll_bar_->addListener(this);
  scroll_bar_->setAlwaysOnTop(true);

  setOpaque(false);
}

GroupSettingsList::~GroupSettingsList() = default;

void GroupSettingsList::paintBackground(Graphics& g) {
  redoBackgroundImage();
}

void GroupSettingsList::redoBackgroundImage() {
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

void GroupSettingsList::resized() {
  ScopedLock lock(open_gl_critical_section_);

  viewport_.setBounds(getLocalBounds());
  int large_padding = findValue(Skin::kLargePadding);
  scroll_bar_->setBounds(getWidth() - large_padding + 1, 0, large_padding - 2, getHeight());
  static_cast<groups_page::HoverScrollBar*>(scroll_bar_.get())->setVisibleColor(findColour(Skin::kLightenScreen, true));
  updateLayout();

  SynthSection::resized();
}

void GroupSettingsList::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  updateLayout();
}

void GroupSettingsList::visibilityChanged() {
  SynthSection::visibilityChanged();
  // Groups and engines are added on the voice tab, so catch up whenever the page is shown.
  if (isShowing())
    updateLayout();
}

void GroupSettingsList::updateLayout() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr || getWidth() <= 0 || getHeight() <= 0)
    return;

  SynthBase* synth = parent->getSynth();
  vital::control_map& controls = synth->getControls();
  vital::EngineOrder order = synth->getEngineOrder();

  {
    ScopedLock lock(open_gl_critical_section_);
    int padding = findValue(Skin::kPadding);
    int width = viewport_.getWidth();
    // Drift and humanise each need a title and a row of knobs, one panel above the other.
    int title_height = sections_[0]->getPanelTitleHeight();
    int height = sections_[0]->getBarHeight() + 2 * padding + 2 * title_height + 2 * getKnobSectionHeight();

    for (auto& section : sections_)
      section->setVisible(false);

    int y = 0;
    for (int g : synth->getGroupOrder()) {
      if (!synth->isGroupShown(g))
        continue;

      std::vector<std::string> engines;
      for (int engine : order) {
        std::string prefix = "osc_" + std::to_string(engine + 1) + "_";
        if (!synth->isEngineAdded(engine) || static_cast<int>(controls[prefix + "group"]->value()) != g)
          continue;
        bool sample = static_cast<int>(controls[prefix + "source"]->value()) == vital::SynthOscillator::kSampleSource;
        std::string name = (sample ? "SMP " : "OSC ") + std::to_string(engine + 1);
        engines.push_back(name);
      }

      if (y > 0)
        y += 2 * padding;
      sections_[g]->setName(String("GROUP ") + String(synth->getGroupDisplayNumber(g)));
      sections_[g]->setEngines(engines);
      sections_[g]->setBounds(0, y, width, height);
      sections_[g]->setVisible(true);
      y += height;
    }

    Point<int> position = viewport_.getViewPosition();
    container_->setBounds(0, 0, width, std::max(y, 1));
    viewport_.setViewPosition(position);
  }

  redoBackgroundImage();
  setScrollBarRange();
  for (SoundEngineList::Listener* listener : listeners_)
    listener->soundEnginesMoved();
}

void GroupSettingsList::initOpenGlComponents(OpenGlWrapper& open_gl) {
  background_.init(open_gl);
  SynthSection::initOpenGlComponents(open_gl);
}

void GroupSettingsList::renderOpenGlComponents(OpenGlWrapper& open_gl, bool animate) {
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
  for (SoundEngineList::Listener* listener : listeners_)
    listener->renderSoundEngineMeters(open_gl, animate, &viewport_);
  SynthSection::renderOpenGlComponents(open_gl, animate);
}

void GroupSettingsList::destroyOpenGlComponents(OpenGlWrapper& open_gl) {
  background_.destroy(open_gl);
  SynthSection::destroyOpenGlComponents(open_gl);
}

void GroupSettingsList::scrollBarMoved(ScrollBar* scroll_bar, double range_start) {
  viewport_.setViewPosition(Point<int>(0, range_start));
}

void GroupSettingsList::soundEnginesScrolled(int position) {
  setScrollBarRange();
  scroll_bar_->setCurrentRange(position, viewport_.getHeight());

  for (SoundEngineList::Listener* listener : listeners_)
    listener->soundEnginesMoved();
}

void GroupSettingsList::setScrollBarRange() {
  scroll_bar_->setRangeLimits(0.0, container_->getHeight());
  scroll_bar_->setCurrentRange(scroll_bar_->getCurrentRangeStart(), viewport_.getHeight(), dontSendNotification);
}
