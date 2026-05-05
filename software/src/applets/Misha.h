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

// Misha: Interval-based MIDI step sequencer inspired by Eventide Misha.
//
// Incoming MIDI notes are interpreted as diatonic scale-degree jumps through
// the active quantizer scale. The reference note (default MIDI 60 = C4)
// represents unison / repeat.
//
// White keys map to scale degrees relative to the reference note's white key:
//   C=0, D=1, E=2, F=3, G=4, A=5, B=6 (repeating across octaves)
// Each octave of white keys adds the scale size to the degree delta.
// Black keys use the degree of the white key immediately below them, plus a
// +1 semitone chromatic nudge applied after quantization (same as real Misha).
//
// Example with ref=C4, C Major scale:
//   C4 → Δ0 (repeat)   D4 → Δ+1   E4 → Δ+2   C5 → Δ+7 (one octave up)
//   C#4 → Δ0 +1 semitone nudge
//
// Digital 1: Unused (reserved)
// Digital 2: Reset — snap cursor to scale degree 0 (root)
// CV 1:      Semitone transpose offset applied to output CV
// CV 2:      Unused (reserved)
// Out A:     Quantized pitch CV
// Out B:     Gate (held while any MIDI note is held) or Trigger (pulse per note-on)

class Misha : public HemisphereApplet {
public:
    enum MishaCursor : uint8_t {
        CURSOR_QSELECT,
        CURSOR_MIDI_CH,
        CURSOR_REF_NOTE,
        CURSOR_OUT2_MODE,
        NUM_CURSORS
    };
    enum Out2Mode : uint8_t { O2_GATE, O2_TRIG };

    const char* applet_name() { return "Misha"; }
    const uint8_t* applet_icon() { return PhzIcons::strum; }

    void Start() {
        cursor        = 0;
        qselect       = io_offset;  // default: hemisphere's own quantizer channel
        midi_channel  = 0;          // 0-indexed → MIDI ch 1
        ref_note      = 60;         // C4 in MIDI numbering
        out2_mode     = O2_GATE;
        current_note_index = 64;    // mid-range starting point
        last_midi_note     = 0xFF;  // sentinel: no note yet
        note_active        = false;
        last_out_note      = 60;
        last_interval      = 0;
        chromatic_nudge    = false;
        output_dirty       = false;
    }

    void Controller() {
        // --- Reset via TR2 ---
        if (Clock(1)) {
            current_note_index = 64;  // snap to neutral position (maps to root at 0-offset)
            output_dirty = true;
        }

        // --- MIDI note detection ---
        auto &midi = HS::frame.MIDIState;
        auto &buf  = midi.note_buffer[midi_channel];

        if (!buf.empty()) {
            uint8_t newest = buf.back().note;
            if (newest != last_midi_note) {
                // New note arrived — compute diatonic scale-degree delta
                last_interval = DiatonicDelta(newest, ref_note);
                chromatic_nudge = ChromaticNudge(newest);
                current_note_index += last_interval;
                current_note_index = constrain(current_note_index, 0, 127);
                last_midi_note = newest;
                output_dirty   = true;
                note_active    = true;
                if (out2_mode == O2_TRIG) ClockOut(1);
            }
        } else {
            if (note_active) {
                // All notes released
                note_active = false;
                midi.SendNoteOff(midi_channel, last_out_note, 0);
            }
            last_midi_note = 0xFF;
        }

        // --- CV1 transpose ---
        int transpose_cv = In(0);

        // --- Output ---
        if (output_dirty) {
            // chromatic_nudge shifts by one semitone (128 = ONE_OCTAVE/12) for black keys
            int cv = HS::QuantizerLookup(qselect, current_note_index)
                     + (chromatic_nudge ? (ONE_OCTAVE / 12) : 0)
                     + transpose_cv;
            Out(0, cv);
            // MIDI out
            uint8_t midi_out = MIDIQuantizer::NoteNumber(cv, 0);
            midi.SendNoteOff(midi_channel, last_out_note, 0);
            midi.SendNoteOn(midi_channel, midi_out, 100);
            last_out_note = midi_out;
            output_dirty  = false;
        }

        // --- Out B gate/trig ---
        if (out2_mode == O2_GATE) {
            GateOut(1, note_active);
        }
        // O2_TRIG pulse is fired in the note-on branch above via ClockOut(1)
    }

    void View() {
        // --- Row 1: Quantizer + MIDI channel + reference note ---
        int scale_size = OC::Scales::GetScale(HS::GetScale(qselect)).num_notes;
        gfxPrint(0, 15, "Q");
        gfxPrint(qselect + 1);
        gfxPrint(14, 15, "Ch");
        gfxPrint(midi_channel + 1);
        // Reference note: show as note name + octave
        const char* note_name = OC::Strings::note_names_unpadded[ref_note % 12];
        int ref_octave = (int)(ref_note / 12) - 1;  // MIDI 60 → C4 (octave 4)
        gfxPrint(38, 15, note_name);
        gfxPrint(ref_octave);

        // Cursor underlines for row 1
        switch (cursor) {
            case CURSOR_QSELECT:   gfxCursor(0,  23, 11); break;
            case CURSOR_MIDI_CH:   gfxCursor(14, 23, 22); break;
            case CURSOR_REF_NOTE:  gfxCursor(38, 23, 25); break;
            default: break;
        }

        // --- Row 2: last interval jump ---
        gfxPos(18, 30);
        if (last_interval > 0)       graphics.printf("+%d", last_interval);
        else if (last_interval < 0)  graphics.printf("%d",  last_interval);
        else                         gfxPrint(18, 30, "=");

        // --- Row 3: scale degree dots ---
        // Show up to 16 degrees. Active degree = current_note_index % scale_size
        int active_deg = ((current_note_index % scale_size) + scale_size) % scale_size;
        int max_dots = min(scale_size, 16);
        int dot_spacing = 62 / max_dots;
        for (int i = 0; i < max_dots; i++) {
            int x = 1 + i * dot_spacing;
            if (i == active_deg)
                gfxRect(x, 40, 4, 5);
            else
                gfxFrame(x, 40, 4, 5);
        }

        // --- Row 4: Out2 mode label + cursor underline ---
        const char* mode_str = (out2_mode == O2_GATE) ? "GATE" : "TRIG";
        gfxPrint(0, 52, OutputLabel(0));
        gfxPrint(8, 52, "  ");
        gfxPrint(30, 52, OutputLabel(1));
        gfxPrint(38, 52, mode_str);
        if (cursor == CURSOR_OUT2_MODE) gfxCursor(30, 63, 33);
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
                midi_channel = constrain((int)midi_channel + direction, 0, 15);
                break;
            case CURSOR_REF_NOTE:
                ref_note = constrain((int)ref_note + direction, 0, 127);
                break;
            case CURSOR_OUT2_MODE:
                out2_mode = (Out2Mode)constrain((int)out2_mode + direction, 0, 1);
                break;
        }
    }

    void OnButtonPress() {
        CursorToggle();
    }

    void AuxButton() {
        if (cursor == CURSOR_QSELECT) {
            HS::QuantizerEdit(qselect);
        }
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation{ 0, 4}, qselect);
        Pack(data, PackLocation{ 4, 4}, midi_channel);
        Pack(data, PackLocation{ 8, 7}, ref_note);
        Pack(data, PackLocation{15, 1}, out2_mode);
        Pack(data, PackLocation{16, 7}, constrain(current_note_index, 0, 127));
        return data;
    }

    void OnDataReceive(uint64_t data) {
        qselect            = Unpack(data, PackLocation{ 0, 4});
        midi_channel       = Unpack(data, PackLocation{ 4, 4});
        ref_note           = Unpack(data, PackLocation{ 8, 7});
        out2_mode          = (Out2Mode)Unpack(data, PackLocation{15, 1});
        current_note_index = Unpack(data, PackLocation{16, 7});

        // Clamp restored values to valid ranges
        qselect      = constrain(qselect, 0, QUANT_CHANNEL_COUNT - 1);
        midi_channel = constrain(midi_channel, 0, 15);
        ref_note     = constrain(ref_note, 0, 127);
    }

protected:
    void SetHelp() {
        //               "------------------" <-- 18 chars
        help[HEMISPHERE_HELP_DIGITALS] = "1=unused  2=Reset";
        help[HEMISPHERE_HELP_CVS]      = "1=Transpose 2=n/a";
        help[HEMISPHERE_HELP_OUTS]     = "A=Pitch   B=Gt/Tr";
        help[HEMISPHERE_HELP_ENCODER]  = "Qsel MiCh Ref O2";
    }

private:
    int       cursor;
    int       qselect;
    uint8_t   midi_channel;
    uint8_t   ref_note;
    Out2Mode  out2_mode;

    int       current_note_index;  // absolute walk position (0–127)
    uint8_t   last_midi_note;      // 0xFF = no note active; detects new note-on
    bool      note_active;         // true while any MIDI note is held
    uint8_t   last_out_note;       // for SendNoteOff on release
    int       last_interval;       // most recent scale-degree delta (for display)
    bool      chromatic_nudge;     // true if incoming note was a black key (+1 semitone)
    bool      output_dirty;        // true when CV needs to be recomputed

    // White-key index within an octave: C=0 D=1 E=2 F=3 G=4 A=5 B=6
    // Black keys return the index of the white key immediately below them.
    // pitch class: 0  1  2  3  4  5  6  7  8  9 10 11
    //              C  C# D  D# E  F  F# G  G# A  A# B
    static int WhiteKeyIndex(uint8_t pitch_class) {
        static const int8_t wk[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        return wk[pitch_class];
    }

    // True if pitch_class is a black key (C#/Db D#/Eb F#/Gb G#/Ab A#/Bb)
    static bool IsBlackKey(uint8_t pitch_class) {
        static const bool bk[12] = { false,true,false,true,false,false,true,false,true,false,true,false };
        return bk[pitch_class];
    }

    // Compute diatonic scale-degree delta between note and ref.
    // Each octave of white-key distance contributes ±7 degrees.
    static int DiatonicDelta(uint8_t note, uint8_t ref) {
        int note_oct = note / 12;
        int ref_oct  = ref  / 12;
        int note_wk  = WhiteKeyIndex(note % 12);
        int ref_wk   = WhiteKeyIndex(ref  % 12);
        return (note_oct - ref_oct) * 7 + (note_wk - ref_wk);
    }

    // Returns true if 'note' is a black key (caller adds +1 semitone nudge).
    static bool ChromaticNudge(uint8_t note) {
        return IsBlackKey(note % 12);
    }
};
