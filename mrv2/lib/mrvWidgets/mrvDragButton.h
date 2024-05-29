// SPDX-License-Identifier: BSD-3-Clause
// mrv2
// Copyright Contributors to the mrv2 Project. All rights reserved.

#pragma once

#include <FL/Fl_Box.H>

class Fl_Window;

namespace mrv
{

    class DragButton : public Fl_Box
    {
    private:
        int x1, y1;     // click posn., used for dragging and docking checks
        int xoff, yoff; // origin used for dragging calcs
        int was_docked; // used in handle to note that we have just undocked

    public:
        // basic constructor
        DragButton(int x, int y, int w, int h, const char* l = 0);

        // override handle method to catch drag/dock operations
        int handle(int event) override;

    protected:
        int would_dock();

        void color_dock_group(Fl_Color c);
        void show_dock_group();
        void hide_dock_group();
    };

} // namespace mrv
