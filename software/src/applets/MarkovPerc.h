// MarkovPerc: Rhythmic Markov State Machine
// The rhythmic sibling to MarkoV. States are hit types, not pitches.
// A Markov chain chooses the next hit pattern based on the current one,
// creating a drummer with evolving style and internal memory.
//
// Digital 1: Clock — advance to next state
// Digital 2: Reset — short press = return to seed state, long press = new seed
// CV In 1: Chaos — flattens the transition distribution (more erratic fills)
// CV In 2: Density — biases toward hits vs. rests (positive V = more hits)
// Out A: Trigger — fires sub-triggers for ratchets/flams within the beat
// Out B: Accent CV — 0-5V scaled to the strength of each individual hit

// ---------------------------------------------------------------------------
// Hit States
// ---------------------------------------------------------------------------
// REST        (0): silence
// HIT         (1): single trigger, moderate accent
// ACC_HIT     (2): single trigger, full accent
// FLAM        (3): grace note + main hit, medium accent on main
// ACC_FLAM    (4): grace note + accented main (accent on downbeat)
// RATCHET_2   (5): 2 evenly-spaced triggers, accent on first
// RATCHET_3   (6): 3 evenly-spaced triggers, accent on first
// RATCHET_4   (7): 4 evenly-spaced triggers, accent on first
// ---------------------------------------------------------------------------

namespace MarkovPercData {

// Accent levels per hit event (0-5, multiplied by ONE_OCTAVE = ~1V each)
// Used by ScheduleHit() to set Output B for each sub-trigger.
static constexpr uint8_t ACCENT_NONE   = 0;
static constexpr uint8_t ACCENT_GRACE  = 1; // soft grace note
static constexpr uint8_t ACCENT_SOFT   = 2; // hit
static constexpr uint8_t ACCENT_MED    = 3; // accented hit
static constexpr uint8_t ACCENT_HARD   = 4; // ratchet lead
static constexpr uint8_t ACCENT_FULL   = 5; // maximum accent

// profiles[profile][from_state][to_state]
// Weights use strong ratios so each profile has a clearly distinct feel.
// At chaos=0 the dominant weights dominate hard; at chaos=100 all → 8 (flat).
//
//               R    H   AH    F   AF   R2   R3   R4
static const uint8_t profiles[3][8][8] = {
    // 0: Steady (Rock/Pop)
    // Gravitates toward hits and accented hits. Rests are brief.
    // Flams appear as ornaments. Ratchets are rare fills.
    {
        {  2, 18, 12,  4,  2,  3,  1,  1 }, // from REST   — rebound to hit
        {  4, 16, 10,  6,  2,  4,  1,  1 }, // from HIT    — likely another hit
        {  4, 14, 10,  4,  4,  4,  2,  1 }, // from ACC_HIT
        {  4, 16, 10,  4,  2,  5,  1,  1 }, // from FLAM
        {  6, 14, 10,  4,  2,  4,  2,  1 }, // from ACC_FLAM
        {  8, 14,  8,  4,  2,  4,  1,  1 }, // from RATCHET_2 — rest likely after
        { 12, 12,  6,  2,  2,  4,  2,  2 }, // from RATCHET_3
        { 14, 12,  6,  2,  2,  2,  2,  2 }, // from RATCHET_4 — exhausted, rest
    },
    // 1: Syncopated (Funk/Latin)
    // Rests are structurally meaningful. Flams are very common.
    // Ratchet_2 is a common syncopation device. Higher average energy.
    {
        {  4, 10,  6, 12,  8, 10,  2,  1 }, // from REST   — many options
        { 10,  6,  4, 14,  8, 10,  4,  2 }, // from HIT    — flam very likely
        {  8,  8,  4, 12, 10,  8,  4,  2 }, // from ACC_HIT
        {  8, 10,  4,  8,  8, 12,  4,  2 }, // from FLAM   — ratchet_2 common
        {  8,  8,  6, 10,  8, 10,  4,  2 }, // from ACC_FLAM
        { 12,  8,  4,  8,  6,  8,  4,  2 }, // from RATCHET_2 — rest follows
        { 14,  8,  4,  6,  4,  6,  4,  2 }, // from RATCHET_3
        { 16,  6,  4,  4,  4,  6,  4,  2 }, // from RATCHET_4
    },
    // 2: Jazz/Free
    // Complex patterns, ratchets are common fills. Accented flams are the
    // signature gesture. Extended rests followed by dense bursts.
    {
        {  4,  6,  4,  8, 14, 12,  8,  6 }, // from REST   — burst likely
        {  6,  4,  4,  8, 12, 14, 10,  6 }, // from HIT
        {  4,  4,  4,  8, 14, 12, 10,  6 }, // from ACC_HIT
        {  6,  6,  4,  6, 14, 12, 10,  6 }, // from FLAM
        {  4,  4,  4,  6, 12, 14, 12,  8 }, // from ACC_FLAM — more complexity
        {  8,  6,  4,  8, 10, 10, 12,  8 }, // from RATCHET_2
        { 10,  4,  4,  6,  8,  8, 14, 10 }, // from RATCHET_3 — R4 very likely
        { 12,  4,  4,  4,  6,  6, 12, 14 }, // from RATCHET_4 — self-reinforcing
    },
};

static const char* const profile_names[3] = { "S", "T", "J" };
static const char* const cursor_labels[3] = { "Style", "Chaos" };

// State name abbreviations for display
static const char* const state_names[8] = {
    "Rst", "Hit", "AcH", "Flm", "AcF", "Rc2", "Rc3", "Rc4"
};

} // namespace MarkovPercData


class MarkovPerc : public HemisphereApplet {
public:
    static constexpr int      NUM_STATES       = 8;
    static constexpr int      NUM_PROFILES     = 3;
    static constexpr int      HISTORY_SIZE     = 8;
    static constexpr uint32_t LONG_PRESS_TICKS = 5000;
    static constexpr int      MAX_SCHED        = 4; // max sub-triggers per beat

    // Cursor positions
    static constexpr int CURSOR_STYLE = 0;
    static constexpr int CURSOR_CHAOS = 1;
    static constexpr int CURSOR_LAST  = 1;

    // Hit state indices
    static constexpr int STATE_REST      = 0;
    static constexpr int STATE_HIT       = 1;
    static constexpr int STATE_ACC_HIT   = 2;
    static constexpr int STATE_FLAM      = 3;
    static constexpr int STATE_ACC_FLAM  = 4;
    static constexpr int STATE_RATCHET_2 = 5;
    static constexpr int STATE_RATCHET_3 = 6;
    static constexpr int STATE_RATCHET_4 = 7;

    const char* applet_name() { return "MarkovPrc"; }

    void Start() {
        cursor          = 0;
        profile         = 0;
        chaos_base      = 0;
        chaos_pct       = 0;
        hit_state       = STATE_HIT;
        seed            = STATE_HIT;
        clock_period    = 1000; // safe default ~60ms
        sub_tick        = 0;
        sched_count     = 0;
        accent_cv       = 0;
        accent_countdown = 0;
        gate2_high      = false;
        gate2_ticks     = 0;
        randomized      = false;
        for (int i = 0; i < HISTORY_SIZE; i++) history[i] = STATE_REST;
        history_head    = 0;
    }

    void Controller() {
        // --- Digital In 2: short = return to seed, long = new random seed ---
        bool g2 = Gate(1);
        if (g2) {
            if (!gate2_high) gate2_high = true;
            gate2_ticks++;
            if (gate2_ticks >= LONG_PRESS_TICKS && !randomized) {
                seed       = random(NUM_STATES - 1) + 1; // never seed on REST
                hit_state  = seed;
                randomized = true;
            }
        } else if (gate2_high) {
            gate2_high = false;
            if (!randomized) hit_state = seed; // short press: return to seed
            randomized  = false;
            gate2_ticks = 0;
        }

        // --- Digital In 1: Clock ---
        if (Clock(0)) {
            StartADCLag(0);
            clock_period = ClockCycleTicks(0);
            if (clock_period < 1) clock_period = 1000; // guard against zero
            sub_tick = 0;
        }

        if (EndOfADCLag(0)) {
            // CV 1: Chaos — adds to chaos_base, clamped to 0-100
            int cv1      = constrain(In(0), 0, HEMISPHERE_MAX_INPUT_CV);
            int cv_chaos = Proportion(cv1, HEMISPHERE_MAX_INPUT_CV, 100);
            chaos_pct    = constrain(chaos_base + cv_chaos, 0, 100);
            int chaos    = (chaos_pct * 256) / 100;

            // CV 2: Density — biases hit vs rest (0V = neutral, +V = more hits)
            // Map to 0-256; 128 = neutral (no bias)
            int cv2     = constrain(In(1), 0, HEMISPHERE_MAX_INPUT_CV);
            int density = Proportion(cv2, HEMISPHERE_MAX_INPUT_CV, 256);

            // Advance Markov chain and schedule sub-triggers
            hit_state = NextState(hit_state, chaos, density);
            ScheduleHit(hit_state, clock_period);

            // Fire tick-0 events immediately
            for (int i = 0; i < sched_count; i++) {
                if (schedule[i].tick == 0) {
                    FireTrig(schedule[i].accent);
                }
            }

            // Update scrolling history
            history[history_head] = hit_state;
            history_head = (history_head + 1) % HISTORY_SIZE;
        }

        // Count ISR ticks and fire scheduled sub-triggers
        sub_tick++;
        for (int i = 0; i < sched_count; i++) {
            if (schedule[i].tick > 0 && sub_tick == schedule[i].tick) {
                FireTrig(schedule[i].accent);
            }
        }

        // Zero accent CV after trig_length expires
        if (accent_countdown > 0 && --accent_countdown == 0) {
            Out(1, 0);
        }
    }

    void View() {
        // --- Cursor label at top ---
        gfxPrint(0, 6, MarkovPercData::cursor_labels[cursor]);

        // --- Parameter line: [S/T/J]     [Chaos%] ---
        gfxPrint(1, 15, MarkovPercData::profile_names[profile]);
        gfxPos(36, 15);
        graphics.printf("%d%%", chaos_pct);

        // Cursor underlines
        switch (cursor) {
            case CURSOR_STYLE: gfxCursor(1,  23, 7);  break;
            case CURSOR_CHAOS: gfxCursor(36, 23, 22); break;
        }

        // Separator
        gfxLine(0, 25, 63, 25);

        // --- Scrolling hit-type history ---
        // Each 8px slot shows a glyph representing the hit type.
        // history_head = next write slot = oldest entry.
        const int GY = 27; // graph top y
        const int GH = 34; // graph height in pixels
        const int GH_TALL = GH;        // full height (for hits/accents)
        const int GH_MED  = (GH * 2) / 3;
        const int GH_SHORT = GH / 3;

        for (int i = 0; i < HISTORY_SIZE; i++) {
            int idx  = (history_head + i) % HISTORY_SIZE;
            int s    = history[idx];
            int x    = i * 8;
            int ybot = GY + GH; // bottom of graph area

            switch (s) {
                case STATE_REST:
                    // Empty — just draw a small dot at baseline
                    gfxRect(x + 3, ybot - 1, 1, 1);
                    break;

                case STATE_HIT:
                    // Single narrow bar, full height
                    gfxRect(x + 3, ybot - GH_TALL, 1, GH_TALL);
                    break;

                case STATE_ACC_HIT:
                    // Wide bar, full height — "louder" = thicker
                    gfxRect(x + 2, ybot - GH_TALL, 3, GH_TALL);
                    break;

                case STATE_FLAM: {
                    // Grace note (short, narrow) + main hit (full, narrow)
                    int gh = GH_MED;
                    gfxRect(x + 1, ybot - gh / 2, 1, gh / 2);  // grace: half height
                    gfxRect(x + 4, ybot - gh,     1, gh);       // main: medium height
                    break;
                }

                case STATE_ACC_FLAM: {
                    // Grace note (short, narrow) + accented main (full, wide)
                    int gh = GH_TALL;
                    gfxRect(x + 1, ybot - gh / 3, 1, gh / 3);  // grace: short
                    gfxRect(x + 3, ybot - gh,     2, gh);       // main: tall & wide
                    break;
                }

                case STATE_RATCHET_2:
                    // Two evenly-spaced bars, medium height
                    gfxRect(x + 1, ybot - GH_MED, 1, GH_MED);
                    gfxRect(x + 5, ybot - GH_MED, 1, GH_MED);
                    break;

                case STATE_RATCHET_3:
                    // Three evenly-spaced bars, shorter
                    gfxRect(x + 0, ybot - GH_SHORT, 1, GH_SHORT);
                    gfxRect(x + 3, ybot - GH_SHORT, 1, GH_SHORT);
                    gfxRect(x + 6, ybot - GH_SHORT, 1, GH_SHORT);
                    break;

                case STATE_RATCHET_4:
                    // Four bars, shortest — dense
                    gfxRect(x + 0, ybot - GH_SHORT, 1, GH_SHORT);
                    gfxRect(x + 2, ybot - GH_SHORT, 1, GH_SHORT);
                    gfxRect(x + 4, ybot - GH_SHORT, 1, GH_SHORT);
                    gfxRect(x + 6, ybot - GH_SHORT, 1, GH_SHORT);
                    break;
            }
        }

        // Baseline
        gfxLine(0, ybot + 1, 63, ybot + 1);
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor, direction, CURSOR_LAST);
            return;
        }
        switch (cursor) {
            case CURSOR_STYLE:
                profile = constrain(profile + direction, 0, NUM_PROFILES - 1);
                break;
            case CURSOR_CHAOS:
                chaos_base = constrain((int)chaos_base + direction, 0, 100);
                break;
        }
    }

    void AuxButton() {
        // Jump to seed state (useful in performance to reset feel)
        hit_state = seed;
        CancelEdit();
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation{0,  2}, profile);
        Pack(data, PackLocation{2,  3}, hit_state);
        Pack(data, PackLocation{5,  7}, chaos_base);
        Pack(data, PackLocation{12, 3}, seed);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        profile    = constrain((int)Unpack(data, PackLocation{0,  2}), 0, NUM_PROFILES - 1);
        hit_state  = constrain((int)Unpack(data, PackLocation{2,  3}), 0, NUM_STATES - 1);
        chaos_base = constrain((int)Unpack(data, PackLocation{5,  7}), 0, 100);
        seed       = constrain((int)Unpack(data, PackLocation{12, 3}), 0, NUM_STATES - 1);
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Clock";
        help[HELP_DIGITAL2] = "Rst/Seed";
        help[HELP_CV1]      = "Chaos+";
        help[HELP_CV2]      = "Density";
        help[HELP_OUT1]     = "Trigger";
        help[HELP_OUT2]     = "Accent CV";
        help[HELP_EXTRA1]   = "Enc:Params";
        help[HELP_EXTRA2]   = "Aux:Seed";
    }

private:
    // --- Scheduled sub-trigger entry ---
    struct SchedEntry {
        int16_t tick;   // ISR ticks after clock edge (0 = fire immediately)
        uint8_t accent; // 0-5, multiplied by ONE_OCTAVE for Output B
    };

    uint8_t   cursor;
    uint8_t   profile;
    uint8_t   chaos_base;    // encoder-set baseline chaos 0-100
    int       chaos_pct;     // live chaos for display
    uint8_t   hit_state;     // current Markov state
    uint8_t   seed;          // reset target state
    int32_t   clock_period;  // measured clock period in ISR ticks
    int32_t   sub_tick;      // ticks elapsed since last clock edge
    SchedEntry schedule[MAX_SCHED];
    int       sched_count;
    int       accent_cv;     // current accent CV level (raw DAC units)
    int       accent_countdown; // ticks until accent CV resets to 0
    uint8_t   history[HISTORY_SIZE];
    uint8_t   history_head;
    bool      gate2_high;
    uint32_t  gate2_ticks;
    bool      randomized;
    int       ybot;          // cached bottom of graph for baseline line

    // Fire Output A trigger and set Output B accent CV.
    // ClockOut() uses global trig_length automatically.
    void FireTrig(uint8_t accent_level) {
        ClockOut(0);
        int cv = (int)accent_level * ONE_OCTAVE;
        Out(1, cv);
        accent_cv        = cv;
        accent_countdown = HEMISPHERE_CLOCK_TICKS * trig_length;
    }

    // Build the sub-trigger schedule for the given hit state.
    // All tick values are relative to the clock edge (tick 0 = immediate).
    // Flam grace note fires at tick 0; main hit fires at period/8.
    // Ratchet hits are evenly divided across the period.
    void ScheduleHit(uint8_t state, int32_t period) {
        sched_count = 0;
        int32_t p8  = max((int32_t)5, period / 8); // flam gap (≥5 ticks)

        switch (state) {
            case STATE_REST:
                break; // nothing scheduled

            case STATE_HIT:
                schedule[0] = {0, MarkovPercData::ACCENT_SOFT};
                sched_count = 1;
                break;

            case STATE_ACC_HIT:
                schedule[0] = {0, MarkovPercData::ACCENT_FULL};
                sched_count = 1;
                break;

            case STATE_FLAM:
                // Grace note (soft) immediately, main hit slightly after
                schedule[0] = {0,              MarkovPercData::ACCENT_GRACE};
                schedule[1] = {(int16_t)p8,    MarkovPercData::ACCENT_MED};
                sched_count = 2;
                break;

            case STATE_ACC_FLAM:
                // Grace note immediately, full accent on the downbeat hit
                schedule[0] = {0,              MarkovPercData::ACCENT_GRACE};
                schedule[1] = {(int16_t)p8,    MarkovPercData::ACCENT_FULL};
                sched_count = 2;
                break;

            case STATE_RATCHET_2: {
                int32_t step = period / 2;
                schedule[0] = {0,              MarkovPercData::ACCENT_HARD};
                schedule[1] = {(int16_t)step,  MarkovPercData::ACCENT_SOFT};
                sched_count = 2;
                break;
            }

            case STATE_RATCHET_3: {
                int32_t step = period / 3;
                schedule[0] = {0,                    MarkovPercData::ACCENT_HARD};
                schedule[1] = {(int16_t)step,         MarkovPercData::ACCENT_SOFT};
                schedule[2] = {(int16_t)(step * 2),   MarkovPercData::ACCENT_SOFT};
                sched_count = 3;
                break;
            }

            case STATE_RATCHET_4: {
                int32_t step = period / 4;
                schedule[0] = {0,                    MarkovPercData::ACCENT_HARD};
                schedule[1] = {(int16_t)step,         MarkovPercData::ACCENT_SOFT};
                schedule[2] = {(int16_t)(step * 2),   MarkovPercData::ACCENT_SOFT};
                schedule[3] = {(int16_t)(step * 3),   MarkovPercData::ACCENT_SOFT};
                sched_count = 4;
                break;
            }
        }
    }

    // Advance the Markov chain with chaos flattening and density bias.
    // chaos:   0-256 fixed-point (0=pure profile, 256=flat uniform)
    // density: 0-256 (128=neutral, >128=more hits, <128=more rests)
    // All integer arithmetic, no floats.
    int NextState(uint8_t from, int chaos, int density) {
        int weights[NUM_STATES];
        int total     = 0;
        int inv_chaos = 256 - chaos;

        for (int j = 0; j < NUM_STATES; j++) {
            int pw = MarkovPercData::profiles[profile][from][j];

            // Chaos interpolation: blend profile weight toward flat (8)
            int w  = (pw * inv_chaos + 8 * chaos) >> 8;

            // Density bias: scale REST down, all other states up
            // density=128 → neutral. density=256 → Rest halved, hits doubled.
            // density=0   → Rest doubled, hits halved.
            if (j == STATE_REST) {
                // REST weight scales inversely with density
                // At density=128: w unchanged. At density=256: w * 0. At density=0: w * 2.
                w = (w * (256 - density)) >> 7; // >> 7 = divide by 128
            } else {
                // Hit weights scale with density
                // At density=128: w unchanged. At density=256: w * 2.
                w = (w * (128 + (density >> 1))) >> 7;
            }

            weights[j] = max(1, w);
            total     += weights[j];
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
