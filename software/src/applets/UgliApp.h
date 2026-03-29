// UgliApp - reads CV inputs, displays voltages, passes through to outputs

class UgliApp : public HemisphereApplet {
public:

    const char* applet_name() {
        return "UgliApp";
    }

    void Start() {
    }

    void Controller() {
        ForEachChannel(ch) {
            cv[ch] = In(ch);
            Out(ch, cv[ch]);
        }
    }

    void View() {
        ForEachChannel(ch) {
            gfxPrint(0, 15 + (ch * 25), "In ");
            gfxPrint(OutputLabel(ch));
            gfxPrint(" ");
            gfxPrintVoltage(cv[ch]);
        }
    }

    void OnEncoderMove(int direction) { }

    uint64_t OnDataRequest() { return 0; }
    void OnDataReceive(uint64_t data) { }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "";
        help[HELP_DIGITAL2] = "";
        help[HELP_CV1]      = "CV In 1";
        help[HELP_CV2]      = "CV In 2";
        help[HELP_OUT1]     = "Thru 1";
        help[HELP_OUT2]     = "Thru 2";
        help[HELP_EXTRA1]   = "";
        help[HELP_EXTRA2]   = "";
    }

private:
    int cv[2];
};
