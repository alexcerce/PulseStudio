#pragma once

#include "Common.h"

/** Typing a value: turning what somebody types under a knob ("2k", "-6", "35%", "0.02 s", "notch") into the
    parameter's value.

    A parameter shows its value in its own way (Hz that become kHz, milliseconds that become seconds, a share
    of one shown as a percentage, names for a choice). Rather than know every such way, the text is read as a
    plain number in base units, the parameter's own text is read the same way, and the value is found whose
    text says the number that was typed. */
namespace ValueEntry
{
    /** What a unit written after a number multiplies it by, to give base units (Hz, seconds): 1 when there is no
        such unit. `known` says whether a unit was written at all. */
    inline double unitScale (const String& unitText, bool& known)
    {
        const auto u = unitText.trim().toLowerCase();
        known = u.isNotEmpty();
        if (u.startsWith ("khz") || u == "k") return 1000.0;
        if (u.startsWith ("ms"))              return 0.001;
        if (u.startsWith ("us"))              return 0.000001;
        if (u == "s" || u.startsWith ("sec")) return 1.0;
        return 1.0;
    }

    /** Reads a typed or shown value as a number in base units: "1.5 kHz" is 1500, "250 ms" is 0.25, "35%" is 35,
        "25L" is -25, "C" is 0, "-inf dB" is very low, "4.0:1" is 4. `hasUnit` says whether a unit followed the
        number. False when there is no number to be had. */
    inline bool parse (const String& text, double& out, bool* hasUnit = nullptr)
    {
        auto t = text.trim();
        if (hasUnit != nullptr) *hasUnit = false;
        if (t.isEmpty()) return false;
        const auto lower = t.toLowerCase();
        if (lower == "c" || lower == "centre" || lower == "center") { out = 0.0; return true; }
        if (lower.startsWith ("-inf") || lower.startsWith ("-oo")) { out = -1.0e9; if (hasUnit != nullptr) *hasUnit = true; return true; }
        if (lower.startsWith ("inf") || lower.startsWith ("+inf")) { out = 1.0e9; return true; }
        t = t.replaceCharacter (',', '.');
        int i = 0;
        if (i < t.length() && (t[i] == '+' || t[i] == '-')) ++i;
        const int digitsFrom = i;
        bool dot = false, any = false;
        while (i < t.length() && (CharacterFunctions::isDigit (t[i]) || (t[i] == '.' && ! dot))) { dot = dot || t[i] == '.'; any = any || CharacterFunctions::isDigit (t[i]); ++i; }
        ignoreUnused (digitsFrom);
        if (! any) return false;
        double v = t.substring (0, i).getDoubleValue();
        const auto rest = t.substring (i).trim();
        const auto restLower = rest.toLowerCase();
        if (restLower == "l" || restLower.startsWith ("l ") || restLower == "left") { out = -std::abs (v); if (hasUnit != nullptr) *hasUnit = true; return true; }   // a pan to the left
        bool known = false;
        v *= unitScale (rest, known);
        if (hasUnit != nullptr) *hasUnit = known;
        out = v;
        return true;
    }

    /** The parameter's own text for a value, read as a number in base units. */
    inline bool shown (te::AutomatableParameter& p, float value, double& out, bool* hasUnit = nullptr)
    {
        return parse (p.valueToString (value), out, hasUnit);
    }

    /** The value a typed text means for this parameter. False when the text means nothing for it. A number typed
        without a unit is taken in the unit the parameter is showing now (type 2 under "1.50 kHz" for 2 kHz). */
    inline bool valueFor (te::AutomatableParameter& p, const String& typed, float& out)
    {
        const auto text = typed.trim();
        if (text.isEmpty()) return false;
        const auto& range = p.valueRange;

        // a choice: its name, or the start of it
        const bool stepped = range.interval >= 0.999f && (range.end - range.start) <= 64.0f;
        double number = 0.0;
        bool typedUnit = false;
        const bool numeric = parse (text, number, &typedUnit);
        if (stepped)
        {
            double asNumber = 0.0;
            const bool namesAreNumbers = shown (p, range.start, asNumber);
            if (! numeric || ! namesAreNumbers)
            {
                for (int pass = 0; pass < 2; ++pass)   // the whole name first, then the start of one
                    for (float v = range.start; v <= range.end + 0.001f; v += jmax (1.0f, range.interval))
                    {
                        const auto name = p.valueToString (v).trim();
                        if (pass == 0 ? name.equalsIgnoreCase (text) : name.startsWithIgnoreCase (text)) { out = v; return true; }
                    }
                if (! numeric) return false;
            }
        }
        if (! numeric) return false;

        // a bare number is in the unit shown now
        if (! typedUnit)
        {
            const auto now = p.valueToString (p.getCurrentBaseValue()).trim().replaceCharacter (',', '.');
            int i = 0;
            while (i < now.length() && (CharacterFunctions::isDigit (now[i]) || now[i] == '.' || now[i] == '-' || now[i] == '+')) ++i;
            bool known = false;
            number *= unitScale (now.substring (i), known);
        }

        // the value whose own text says that number: the text rises (or falls) with the value, so halve the range
        double atStart = 0.0, atEnd = 0.0;
        if (! shown (p, range.start, atStart) || ! shown (p, range.end, atEnd)) return false;
        const bool rising = atEnd >= atStart;
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 40; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            double at = 0.0;
            if (! shown (p, range.convertFrom0to1 (mid), at)) return false;
            if ((at < number) == rising) lo = mid; else hi = mid;
        }
        float found = range.convertFrom0to1 (hi);
        // the text is rounded: when the value itself is the number in a plain unit, take the number as typed
        for (double factor : { 1.0, 0.01, 1000.0, 0.001, 100.0 })
        {
            const double exact = number * factor;
            if (std::abs (exact - found) <= jmax (1.0e-6, std::abs ((double) found) * 0.02) + (double) (range.end - range.start) * 0.0005)
            {
                double check = 0.0;
                if (shown (p, (float) exact, check) && std::abs (check - number) <= jmax (1.0e-9, std::abs (number) * 0.011)) { found = (float) exact; break; }
            }
        }
        out = range.snapToLegalValue (jlimit (range.start, range.end, found));
        return true;
    }

    /** A small box beside a control to type its value into (for controls with no text of their own to click:
        bars, small dials). Enter sets it, Escape or a click elsewhere leaves it. */
    void ask (Component& beside, te::AutomatableParameter& p);

    /** Sets a parameter from typed text, as one undo step. Returns false (and changes nothing) when the text means nothing. */
    inline bool apply (te::AutomatableParameter& p, const String& typed)
    {
        float v = 0.0f;
        if (! valueFor (p, typed, v)) return false;
        p.getEdit().getUndoManager().beginNewTransaction ("Type " + p.getParameterName());
        p.parameterChangeGestureBegin();
        p.setParameter (v, sendNotificationSync);
        p.parameterChangeGestureEnd();
        return true;
    }

    /** The box of ask(). */
    struct Box : public Component
    {
        explicit Box (te::AutomatableParameter& p) : param (&p)
        {
            title.setText (p.getParameterName(), dontSendNotification);
            title.setFont (FontOptions (11.5f, Font::bold));
            title.setColour (Label::textColourId, Theme::muted);
            title.setJustificationType (Justification::centred);
            addAndMakeVisible (title);
            box.setText (p.getCurrentValueAsStringWithLabel(), false);
            box.setJustification (Justification::centred);
            box.setInputRestrictions (32);
            box.setSelectAllWhenFocused (true);
            box.onReturnKey = [this]
            {
                if (apply (*param, box.getText())) dismiss();
                else { box.setColour (TextEditor::outlineColourId, Theme::red); box.setColour (TextEditor::focusedOutlineColourId, Theme::red); box.repaint(); }   // (it stays open: try again)
            };
            box.onEscapeKey = [this] { dismiss(); };
            addAndMakeVisible (box);
            setSize (150, 58);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 4);
            title.setBounds (r.removeFromTop (16));
            r.removeFromTop (3);
            box.setBounds (r);
        }
        void parentHierarchyChanged() override { if (isShowing()) box.grabKeyboardFocus(); }
        void dismiss() { if (auto* c = findParentComponentOfClass<CallOutBox>()) c->dismiss(); }

        te::AutomatableParameter::Ptr param;
        Label title;
        TextEditor box;
    };

    inline void ask (Component& beside, te::AutomatableParameter& p)
    {
        CallOutBox::launchAsynchronously (std::make_unique<Box> (p), beside.getScreenBounds(), nullptr);
    }
}
