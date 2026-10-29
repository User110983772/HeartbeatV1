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

#include "wavetable_playhead_info.h"

#include "skin.h"

void WavetablePlayheadInfo::paint(Graphics& g) {
  // "FRAME" in quieter text, then the playhead's frame.
  g.fillAll(findColour(Skin::kBody, true));
  Rectangle<int> bounds = getLocalBounds();
  bounds.setWidth(bounds.getWidth() - bounds.getHeight() * 0.5f);

  Font font = g.getCurrentFont();
  String position_text(playhead_position_);
  int number_width = font.getStringWidth("000") + 2;
  g.setColour(findColour(Skin::kBodyText, true));
  g.drawText(position_text, bounds.removeFromRight(number_width), Justification::centredRight);

  g.setColour(findColour(Skin::kBodyText, true).withMultipliedAlpha(0.6f));
  bounds.removeFromRight(font.getStringWidth(" "));
  g.drawText(TRANS("FRAME"), bounds, Justification::centredRight);
}

void WavetablePlayheadInfo::mouseDown(const MouseEvent& e) {
  if (!e.mods.isPopupMenu())
    return;
  for (Listener* listener : listeners_)
    listener->frameEntryRequested();
}

void WavetablePlayheadInfo::resized() {
  repaint();
}

void WavetablePlayheadInfo::playheadMoved(int position) {
  playhead_position_ = position;
  repaint();
}
