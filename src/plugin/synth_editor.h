/* Copyright 2013-2019 Matt Tytel
 *
 * pylon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pylon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pylon.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"
#include "border_bounds_constrainer.h"
#include "synth_plugin.h"
#include "full_interface.h"
#include "synth_gui_interface.h"

class SynthEditor : public AudioProcessorEditor, public SynthGuiInterface {
  public:
    SynthEditor(SynthPlugin&);

    void paint(Graphics&) override { }
    void resized() override;
    void setScaleFactor(float newScale) override;

    void updateFullGui() override;
    // With nothing focused (the focused control was hidden, or the window has just opened) keys come here rather
    // than to the interface: undo and redo still reach it.
    bool keyPressed(const KeyPress& key) override { return gui_ && gui_->keyPressed(key); }

  private:
    SynthPlugin& synth_;
    bool was_animating_;
    BorderBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditor)
};

