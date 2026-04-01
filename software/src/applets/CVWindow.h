// Copyright (c) 2026, Andy Jenkinson (uglifruit)
// MIT License
//
// CVWindow — window comparator with hysteresis.
// Monitors CV 1 against a user-defined voltage window (low–high thresholds).
// Out A = gate high while CV is inside the window.
// Out B = gate high while CV is outside the window (complement).
// CV 2 shifts the entire window up/down (transpose).
// Dig 1 latches the current gate state (ignores input while held).
// Hysteresis prevents chatter at threshold crossings.

class CVWindow : public HemisphereApplet {
public:
    const char* applet_name() { return "CVWindow"; }

    void Start() {
        // Default: window centred around 0V, ±1 octave
        low  = 2 * ONE_OCTAVE;   // -2V (below centre of 0–5V input range)
        high = 4 * ONE_OCTAVE;   // +2V
        hyst = 64;               // ~50mV hysteresis
        inside = false;
        latched = false;
    }

    void Controller() {
        // Dig 1 latches: freeze output while gate is high
        if (Clock(0) || Gate(0)) {
            latched = true;
        } else {
            latched = false;
        }

        if (!latched) {
            // CV 2 shifts the window centre
            int shift = In(1);
            int lo = constrain(low  + shift, 0, HEMISPHERE_MAX_CV);
            int hi = constrain(high + shift, 0, HEMISPHERE_MAX_CV);

            // Hysteresis: widen threshold slightly to snap out, narrow to snap in
            int cv = In(0);
            if (inside) {
                // Already inside — must fall below lo-hyst or above hi+hyst to exit
                if (cv < lo - hyst || cv > hi + hyst) inside = false;
            } else {
                // Outside — must enter lo+hyst..hi-hyst to trigger inside
                if (cv > lo + hyst && cv < hi - hyst) inside = true;
            }
        }

        GateOut(0, inside);   // Out A: inside window
        GateOut(1, !inside);  // Out B: outside window
    }

    void View() {
        // Parameter row: cursor underlines at y=23
        // [Low] [High] [Hyst]  — three params
        gfxPrint(1,  15, "Lo:");
        PrintVoltage(19, 15, low);
        gfxPrint(33, 15, "Hi:");
        PrintVoltage(51, 15, high);

        gfxPrint(1,  25, "Hy:");
        PrintVoltage(19, 25, hyst);

        // Cursor underlines
        if (cursor == 0) gfxCursor(19, 23, 12);
        if (cursor == 1) gfxCursor(51, 23, 12);
        if (cursor == 2) gfxCursor(19, 33, 12);

        // Separator
        gfxLine(0, 35, 63, 35);

        // Draw the window visualiser: a bar spanning the 0–HEMISPHERE_MAX_CV range
        // showing the window position and current CV
        int bar_w = 62;
        int bar_x = 1;
        int bar_y = 42;

        int shift = ViewIn(1);
        int lo_px = Proportion(constrain(low  + shift, 0, HEMISPHERE_MAX_CV), HEMISPHERE_MAX_CV, bar_w);
        int hi_px = Proportion(constrain(high + shift, 0, HEMISPHERE_MAX_CV), HEMISPHERE_MAX_CV, bar_w);
        int cv_px = Proportion(constrain(ViewIn(0),    0, HEMISPHERE_MAX_CV), HEMISPHERE_MAX_CV, bar_w);

        // Full range trough
        gfxFrame(bar_x, bar_y, bar_w, 8);

        // Window fill
        if (hi_px > lo_px)
            gfxRect(bar_x + lo_px, bar_y, hi_px - lo_px, 8);

        // CV marker — invert a 3px wide column
        gfxInvert(bar_x + constrain(cv_px - 1, 0, bar_w - 3), bar_y, 3, 8);

        // Gate indicators
        gfxPrint(1,  55, "A:");
        if (inside)  gfxRect(13, 56, 6, 6); else gfxFrame(13, 56, 6, 6);
        gfxPrint(33, 55, "B:");
        if (!inside) gfxRect(45, 56, 6, 6); else gfxFrame(45, 56, 6, 6);

        if (latched) gfxPrint(25, 55, "LCH");
    }

    void OnButtonPress() {
        if (++cursor > 2) cursor = 0;
    }

    void OnEncoderMove(int direction) {
        int step = HEMISPHERE_MAX_CV / 120;  // ~50mV per click (5V / 120 steps)
        switch (cursor) {
            case 0:
                low = constrain(low + direction * step, 0, high - hyst * 2);
                break;
            case 1:
                high = constrain(high + direction * step, low + hyst * 2, HEMISPHERE_MAX_CV);
                break;
            case 2:
                hyst = constrain(hyst + direction * (step / 4), 0, (high - low) / 2);
                break;
        }
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation {0,  13}, low);
        Pack(data, PackLocation {13, 13}, high);
        Pack(data, PackLocation {26, 10}, hyst);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        low  = Unpack(data, PackLocation {0,  13});
        high = Unpack(data, PackLocation {13, 13});
        hyst = Unpack(data, PackLocation {26, 10});
        low  = constrain(low,  0, HEMISPHERE_MAX_CV);
        high = constrain(high, 0, HEMISPHERE_MAX_CV);
        hyst = constrain(hyst, 0, (high - low) / 2);
    }

protected:
    void SetHelp() {
        //                    "-------" <-- Label size guide
        help[HELP_DIGITAL1] = "Latch";
        help[HELP_DIGITAL2] = "";
        help[HELP_CV1]      = "CV In";
        help[HELP_CV2]      = "Shift";
        help[HELP_OUT1]     = "Inside";
        help[HELP_OUT2]     = "Outside";
        help[HELP_EXTRA1]   = "";
        help[HELP_EXTRA2]   = "";
    }

private:
    int8_t cursor = 0;
    int low, high;  // window thresholds in CV units
    int hyst;       // hysteresis half-band in CV units
    bool inside;    // current comparator state
    bool latched;   // true while Dig1 is held

    void PrintVoltage(int x, int y, int cv) {
        // Display as tenths of a volt: e.g. 1536 = 1.0V → "1.0"
        int millivolts = (cv * 1000) / ONE_OCTAVE;  // ONE_OCTAVE = 1V
        int v_int = millivolts / 1000;
        int v_dec = (millivolts % 1000) / 100;
        graphics.setPrintPos(x + gfx_offset, y);
        graphics.printf("%d.%d", v_int, v_dec);
    }
};
