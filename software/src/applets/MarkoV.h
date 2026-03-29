// MarkoV: A Finite State Melodic Generator
// First-order Markov chain over 8 scale degrees with Tendency Profiles.
//
// Digital 1: Clock — advance to next state
// Digital 2: Reset — short press = root, long press = randomize state
// CV 1: Chaos — 0V = pure profile weights, +V = flatten toward uniform
// CV 2: Transpose — V/Oct offset added to output
// Out A: Quantized pitch
// Out B: Trigger pulse when state changes (pitch moves)

// Transition weight tables live outside the class to avoid C++ static
// member definition issues across translation units.
namespace MarkoVData {

// profiles[profile][from_state][to_state]
// Values are relative weights (higher = more probable).
static const uint8_t profiles[3][8][8] = {
    // 0: Pentatonic Stability
    // Strong gravitational pull toward root (0), fifth (4), and octave (7).
    {
        { 4, 2, 2, 1, 4, 1, 1, 3 }, // from 0 (root)
        { 4, 1, 4, 1, 3, 1, 1, 1 }, // from 1 (2nd)
        { 3, 1, 3, 1, 4, 1, 1, 2 }, // from 2 (3rd)
        { 2, 1, 2, 2, 4, 2, 1, 1 }, // from 3 (4th)
        { 4, 1, 2, 1, 4, 1, 1, 3 }, // from 4 (5th)
        { 3, 1, 2, 1, 3, 2, 1, 2 }, // from 5 (6th)
        { 4, 1, 1, 1, 3, 1, 1, 1 }, // from 6 (7th) — resolves to root
        { 4, 2, 2, 1, 3, 1, 1, 2 }, // from 7 (oct) — falls back down
    },
    // 1: Chromatic Tension
    // Strong preference for adjacent stepwise motion (±1 state).
    {
        { 2, 8, 1, 1, 1, 1, 1, 2 }, // from 0
        { 8, 2, 8, 1, 1, 1, 1, 1 }, // from 1
        { 1, 8, 2, 8, 1, 1, 1, 1 }, // from 2
        { 1, 1, 8, 2, 8, 1, 1, 1 }, // from 3
        { 1, 1, 1, 8, 2, 8, 1, 1 }, // from 4
        { 1, 1, 1, 1, 8, 2, 8, 1 }, // from 5
        { 1, 1, 1, 1, 1, 8, 2, 8 }, // from 6
        { 2, 1, 1, 1, 1, 1, 8, 2 }, // from 7
    },
    // 2: Jazz Tendencies
    // Frequent leaps to the 7th, tritone pull from 4th, strong root resolution.
    {
        { 2, 1, 2, 1, 3, 1, 6, 2 }, // from 0 — leap to 7th
        { 2, 1, 4, 1, 2, 1, 5, 2 }, // from 1 — go to 3rd or 7th
        { 2, 1, 2, 1, 3, 1, 6, 2 }, // from 2 — leap to 7th
        { 1, 1, 2, 1, 2, 1, 8, 2 }, // from 3 — tritone pull to 7th
        { 3, 1, 2, 1, 2, 1, 4, 4 }, // from 4 — root or octave
        { 2, 1, 3, 1, 2, 1, 5, 3 }, // from 5 — 3rd or 7th
        { 6, 1, 2, 2, 2, 1, 2, 2 }, // from 6 — resolve to root
        { 4, 1, 2, 1, 3, 1, 4, 2 }, // from 7 — fall back, leap to 7th
    },
};

static const char* const profile_names[3] = {
    "Stability", "Tension", "Jazz"
};

} // namespace MarkoVData


class MarkoV : public HemisphereApplet {
public:
    static constexpr int      NUM_STATES        = 8;
    static constexpr int      NUM_PROFILES      = 3;
    static constexpr int      HISTORY_SIZE      = 8;
    static constexpr uint32_t LONG_PRESS_TICKS  = 5000;
    // CV units per state step: ONE_OCTAVE / 7 spans root to octave in 8 steps
    static constexpr int      STATE_CV_STEP     = ONE_OCTAVE / 7;

    const char* applet_name() { return "MarkoV"; }

    void Start() {
        profile      = 0;
        state        = 0;
        prev_state   = 0;
        gate2_high   = false;
        gate2_ticks  = 0;
        randomized   = false;
        chaos_pct    = 0;
        for (int i = 0; i < HISTORY_SIZE; i++) history[i] = 0;
        history_head = 0;
    }

    void Controller() {
        // --- Digital In 2: short press = reset to root, long press = randomize ---
        bool g2 = Gate(1);
        if (g2) {
            if (!gate2_high) gate2_high = true;
            gate2_ticks++;
            if (gate2_ticks >= LONG_PRESS_TICKS && !randomized) {
                state      = random(NUM_STATES);
                randomized = true;
            }
        } else if (gate2_high) {
            gate2_high = false;
            if (!randomized) state = 0; // short press: return to root
            randomized  = false;
            gate2_ticks = 0;
        }

        // --- Digital In 1: Clock ---
        if (Clock(0)) StartADCLag(0);

        if (EndOfADCLag(0)) {
            // CV 1: Chaos — scale to 0-256 integer (fixed-point, no floats)
            int cv1   = constrain(In(0), 0, HEMISPHERE_MAX_INPUT_CV);
            int chaos = Proportion(cv1, HEMISPHERE_MAX_INPUT_CV, 256);
            chaos_pct = Proportion(cv1, HEMISPHERE_MAX_INPUT_CV, 100);

            // CV 2: Transpose (V/Oct raw value)
            int transpose = In(1);

            // Advance the Markov chain
            prev_state = state;
            state      = NextState(state, chaos);

            // Output A: quantized pitch + V/Oct transpose
            Out(0, Quantize(0, state * STATE_CV_STEP) + transpose);

            // Output B: trigger only when state (pitch) changed
            if (state != prev_state) ClockOut(1);

            // Update scrolling note history
            history[history_head] = state;
            history_head = (history_head + 1) % HISTORY_SIZE;
        }
    }

    void View() {
        // Profile name — encoder adjusts this directly
        gfxPrint(0, 15, MarkoVData::profile_names[profile]);

        // Chaos % — right-aligned on the same row
        gfxPos(40, 15);
        graphics.printf("C:%d%%", chaos_pct);

        // Separator
        gfxLine(0, 24, 63, 24);

        // Scrolling bar graph: oldest note on left, newest on right.
        // history_head points to the next write slot = the oldest entry.
        const int GRAPH_BOTTOM = 62;
        const int GRAPH_H      = 36; // available height in pixels
        const int BAR_W        = 6;
        const int BAR_STRIDE   = 8;  // 8 bars × 8px = 64px

        for (int i = 0; i < HISTORY_SIZE; i++) {
            int idx   = (history_head + i) % HISTORY_SIZE;
            int note  = history[idx];
            int bar_h = max(2, (note * GRAPH_H) / (NUM_STATES - 1));
            int x     = i * BAR_STRIDE;
            gfxRect(x, GRAPH_BOTTOM - bar_h + 1, BAR_W, bar_h);
        }

        // Baseline
        gfxLine(0, GRAPH_BOTTOM + 1, 63, GRAPH_BOTTOM + 1);
    }

    void OnEncoderMove(int direction) {
        profile = constrain(profile + direction, 0, NUM_PROFILES - 1);
    }

    void AuxButton() {
        // Open the quantizer editor for this hemisphere's channel
        HS::QuantizerEdit(io_offset);
        CancelEdit();
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation{0, 2}, profile);
        Pack(data, PackLocation{2, 3}, state);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        profile = constrain((int)Unpack(data, PackLocation{0, 2}), 0, NUM_PROFILES - 1);
        state   = constrain((int)Unpack(data, PackLocation{2, 3}), 0, NUM_STATES - 1);
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Clock";
        help[HELP_DIGITAL2] = "Rst/Rnd";
        help[HELP_CV1]      = "Chaos";
        help[HELP_CV2]      = "Transp";
        help[HELP_OUT1]     = "Pitch";
        help[HELP_OUT2]     = "Trigger";
        help[HELP_EXTRA1]   = "Enc:Profil";
        help[HELP_EXTRA2]   = "Aux:Q Edit";
    }

private:
    uint8_t  profile;
    uint8_t  state;
    uint8_t  prev_state;
    uint8_t  history[HISTORY_SIZE];
    uint8_t  history_head;
    bool     gate2_high;
    uint32_t gate2_ticks;
    bool     randomized;
    int      chaos_pct;   // cached chaos level as 0-100% for display

    // Compute the next Markov state given the current state and chaos level.
    // chaos is 0-256 (fixed-point fraction of 256).
    // chaos=0   → pure profile weights
    // chaos=256 → flat uniform distribution (all weights = 8)
    // Interpolation: w = (pw * (256 - chaos) + 8 * chaos) >> 8
    // Max intermediate value: 8 * 256 = 2048 — fits in int, no floats needed.
    int NextState(uint8_t from, int chaos) {
        int weights[NUM_STATES];
        int total         = 0;
        int inv_chaos     = 256 - chaos;

        for (int j = 0; j < NUM_STATES; j++) {
            int pw       = MarkoVData::profiles[profile][from][j];
            int w        = (pw * inv_chaos + 8 * chaos) >> 8;
            weights[j]   = max(1, w);
            total       += weights[j];
        }

        int r   = random(total);
        int cum = 0;
        for (int j = 0; j < NUM_STATES; j++) {
            cum += weights[j];
            if (r < cum) return j;
        }
        return NUM_STATES - 1;
    }
};
