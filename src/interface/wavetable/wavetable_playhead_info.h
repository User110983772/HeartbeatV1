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

#include "wavetable_playhead.h"

// Shows which frame the playhead is on; right-click to type a frame to go to.
class WavetablePlayheadInfo : public Component, public WavetablePlayhead::Listener {
  public: 
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void frameEntryRequested() = 0;
    };

    WavetablePlayheadInfo() : playhead_position_(0) { }

    void paint(Graphics& g) override;
    void resized() override;
    void playheadMoved(int position) override;
    void mouseDown(const MouseEvent& e) override;

    void addListener(Listener* listener) { listeners_.push_back(listener); }
    int position() const { return playhead_position_; }

  protected:  
    int playhead_position_;
    std::vector<Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetablePlayheadInfo)
};

