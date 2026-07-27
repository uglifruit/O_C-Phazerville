// Copyright (c) 2026, Andy Jenkinson (uglifruit)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// A-Mish: Interval-based melodic sequencer inspired by the Eventide Misha.
//
// Incoming MIDI notes are interpreted as diatonic scale-degree jumps through
// the active quantizer scale, rather than absolute pitches. The keyboard
// becomes a navigation device: which key you press determines how far to step
// up or down from the current position.
//
// MIDI input handling:
//   - HS::frame.MIDIState.note_buffer[ch] holds the currently-held notes on
//     channel ch (0-indexed). The applet watches buf.back().note each tick.
//   - A new note is detected when buf.back().note differs from last_midi_note.
//   - Note-off is detected when the buffer empties (all notes released).
//   - White keys map to diatonic degree deltas relative to ref_note:
//       same white key as ref → delta 0 (repeat)
//       each white key step up/down → ±1 scale degree per step
//       each keyboard octave → ±7 degrees (full diatonic span)
//   - Black keys carry the same delta as the white key immediately below them,
//     plus a +1 semitone chromatic nudge applied after quantizer lookup.
//
// Digital 1: Unused
// Digital 2: Reset — snap position to scale degree 0 (root)
// CV 1:      Semitone transpose offset added to output CV after quantization
// CV 2:      Q-select offset — positive CV shifts active quantizer channel up
// Out A:     Quantized pitch CV
// Out B:     Gate (held while any note is held) or Trigger (pulse per note-on)

class AMish : public HemisphereApplet {
public:
    enum AMishCursor : uint8_t {
        CURSOR_QSELECT,
        CURSOR_MIDI_CH,
        CURSOR_REF_NOTE,
        CURSOR_OUT2_MODE,
        NUM_CURSORS
    };
    enum Out2Mode : uint8_t { O2_GATE, O2_TRIG };

    const char* applet_name() { return "A-Mish"; }
    const uint8_t* applet_icon() { return PhzIcons::strum; }

    void Start() {
        cursor             = 0;
        qselect            = io_offset;
        midi_ch            = 0;
        ref_note           = 60;
        out2_mode          = O2_GATE;
        current_note_index = 64;
        last_midi_note     = 0xFF;
        note_active        = false;
        last_out_note      = 60;
        last_interval      = 0;
        chromatic_nudge    = false;
        output_dirty       = false;
    }

    void Controller() {
        // CV 2: positive voltage offsets the active quantizer channel above qselect
        int q_offset    = Proportion(max(0, In(1)), HEMISPHERE_MAX_INPUT_CV, QUANT_CHANNEL_COUNT - 1);
        int effective_q = constrain(qselect + q_offset, 0, QUANT_CHANNEL_COUNT - 1);

        // TR2 rising edge: snap position back to root (degree 0)
        if (Clock(1)) {
            current_note_index = 64;
            output_dirty = true;
        }

        // Watch note_buffer[midi_ch] for new note-ons and note-offs.
        // buf holds all currently-held notes; buf.back() is the most recent.
        auto &buf = HS::frame.MIDIState.note_buffer[midi_ch];

        if (!buf.empty()) {
            uint8_t newest = buf.back().note;
            if (newest != last_midi_note) {
                // New note-on: compute diatonic degree delta and advance position
                last_interval      = DiatonicDelta(newest, ref_note);
                chromatic_nudge    = ChromaticNudge(newest);
                current_note_index = constrain(current_note_index + last_interval, 0, 127);
                last_midi_note     = newest;
                output_dirty       = true;
                note_active        = true;
                if (out2_mode == O2_TRIG) ClockOut(1);
            }
        } else {
            // All notes released: send MIDI note-off and drop the gate
            if (note_active) {
                note_active = false;
                HS::frame.MIDIState.SendNoteOff(midi_ch, last_out_note, 0);
            }
            last_midi_note = 0xFF;
        }

        // CV 1: raw semitone transpose added after quantization
        int transpose_cv = In(0);

        if (output_dirty) {
            int cv = HS::QuantizerLookup(effective_q, current_note_index)
                     + (chromatic_nudge ? (ONE_OCTAVE / 12) : 0)
                     + transpose_cv;
            Out(0, cv);
            // Mirror output as MIDI note on the input channel
            uint8_t midi_out = MIDIQuantizer::NoteNumber(cv, 0);
            HS::frame.MIDIState.SendNoteOff(midi_ch, last_out_note, 0);
            HS::frame.MIDIState.SendNoteOn(midi_ch, midi_out, 100);
            last_out_note = midi_out;
            output_dirty  = false;
        }

        if (out2_mode == O2_GATE) GateOut(1, note_active);
    }

    void View() {
        int scale_size       = OC::Scales::GetScale(HS::GetScale(qselect)).num_notes;
        const char* mode_str = (out2_mode == O2_GATE) ? "GATE" : "TRIG";

        // Row 1: Q channel | MIDI channel | reference note
        gfxPrint(0, 15, "Q");
        gfxPrint(qselect + 1);
        gfxPrint(14, 15, "Ch");
        gfxPrint(25, 15, midi_ch + 1);
        const char* note_name = OC::Strings::note_names_unpadded[ref_note % 12];
        gfxPrint(44, 15, note_name);
        gfxPrint((int)(ref_note / 12) - 1);

        switch (cursor) {
            case CURSOR_QSELECT:  gfxCursor(0,  23, 11); break;
            case CURSOR_MIDI_CH:  gfxCursor(14, 23, 27); break;
            case CURSOR_REF_NOTE: gfxCursor(44, 23, 20); break;
            default: break;
        }

        // Row 2: most recent degree delta
        if (last_interval > 0)      { gfxPos(8, 30); graphics.printf("+%d", last_interval); }
        else if (last_interval < 0) { gfxPos(8, 30); graphics.printf("%d",  last_interval); }
        else                          gfxPrint(8, 30, "=");

        // Row 3: scale degree dots — filled dot marks current position
        int active_deg  = ((current_note_index % scale_size) + scale_size) % scale_size;
        int max_dots    = min(scale_size, 16);
        int dot_spacing = 62 / max_dots;
        for (int i = 0; i < max_dots; i++) {
            int x = 1 + i * dot_spacing;
            if (i == active_deg) gfxRect(x, 40, 4, 5);
            else                 gfxFrame(x, 40, 4, 5);
        }

        // Row 4: output A label + current note name, output B label + mode
        gfxPrint(0,  52, OutputLabel(0));
        gfxPrint(8,  52, OC::Strings::note_names_unpadded[last_out_note % 12]);
        gfxPrint((int)(last_out_note / 12) - 1);
        gfxPrint(30, 52, OutputLabel(1));
        gfxPrint(38, 52, mode_str);
        if (cursor == CURSOR_OUT2_MODE) gfxCursor(30, 60, 33);
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor, direction, NUM_CURSORS - 1);
            return;
        }
        switch (cursor) {
            case CURSOR_QSELECT:
                qselect = constrain(qselect + direction, 0, QUANT_CHANNEL_COUNT - 1);
                HS::qview = qselect;
                HS::PokePopup(QUANTIZER_POPUP);
                break;
            case CURSOR_MIDI_CH:
                midi_ch = (uint8_t)constrain((int)midi_ch + direction, 0, 15);
                break;
            case CURSOR_REF_NOTE:
                ref_note = constrain((int)ref_note + direction, 0, 127);
                break;
            case CURSOR_OUT2_MODE:
                out2_mode = (Out2Mode)constrain((int)out2_mode + direction, 0, 1);
                break;
        }
    }

    void OnButtonPress() { CursorToggle(); }

    void AuxButton() {
        if (cursor == CURSOR_QSELECT) HS::QuantizerEdit(qselect);
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation{ 0, 4}, qselect);
        Pack(data, PackLocation{ 4, 4}, midi_ch);
        Pack(data, PackLocation{ 8, 7}, ref_note);
        Pack(data, PackLocation{15, 1}, out2_mode);
        Pack(data, PackLocation{16, 7}, constrain(current_note_index, 0, 127));
        return data;
    }

    void OnDataReceive(uint64_t data) {
        qselect            = Unpack(data, PackLocation{ 0, 4});
        midi_ch            = (uint8_t)Unpack(data, PackLocation{ 4, 4});
        ref_note           = Unpack(data, PackLocation{ 8, 7});
        out2_mode          = (Out2Mode)Unpack(data, PackLocation{15, 1});
        current_note_index = Unpack(data, PackLocation{16, 7});

        qselect  = constrain(qselect, 0, QUANT_CHANNEL_COUNT - 1);
        midi_ch  = constrain(midi_ch, 0, 15);
        ref_note = constrain(ref_note, 0, 127);
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Unused";
        help[HELP_DIGITAL2] = "Reset";
        help[HELP_CV1]      = "Transp";
        help[HELP_CV2]      = "Qsel";
        help[HELP_OUT1]     = "Pitch";
        help[HELP_OUT2]     = "Gt/Tr";
    }

private:
    int      cursor;
    int      qselect;
    uint8_t  midi_ch;
    uint8_t  ref_note;
    Out2Mode out2_mode;

    int     current_note_index;
    uint8_t last_midi_note;   // 0xFF = no note active
    bool    note_active;
    uint8_t last_out_note;
    int     last_interval;
    bool    chromatic_nudge;
    bool    output_dirty;

    // White-key index within an octave (C=0 … B=6).
    // Black keys return the index of the white key immediately below them.
    // pitch class: 0  1  2  3  4  5  6  7  8  9 10 11
    //              C  C# D  D# E  F  F# G  G# A  A# B
    static int WhiteKeyIndex(uint8_t pitch_class) {
        static const int8_t wk[12] = { 0,0,1,1,2,3,3,4,4,5,5,6 };
        return wk[pitch_class];
    }

    static bool IsBlackKey(uint8_t pitch_class) {
        static const bool bk[12] = { false,true,false,true,false,false,true,false,true,false,true,false };
        return bk[pitch_class];
    }

    // Diatonic degree delta: each white-key step = 1 degree; each octave = 7 degrees.
    static int DiatonicDelta(uint8_t note, uint8_t ref) {
        return (note/12 - ref/12) * 7 + (WhiteKeyIndex(note%12) - WhiteKeyIndex(ref%12));
    }

    // Returns true if note is a black key; caller adds +1 semitone nudge after quantization.
    static bool ChromaticNudge(uint8_t note) { return IsBlackKey(note % 12); }
};
