#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Synth2Theme.h"

#include <array>
#include <cmath>

using namespace Synth2;

namespace
{
    constexpr float kDesignWidth  = 480.0f;
    constexpr float kDesignHeight = 612.0f;
    constexpr float kPanelPad     = 24.0f;

    // The dials, in display order: three for each oscillator, three for the
    // unison stack, three for the amp.
    constexpr std::array<const char*, Synth2AudioProcessorEditor::numDials> dialParamIds
        { ParamID::osc1Pitch, ParamID::osc1Fine, ParamID::osc1Level,
          ParamID::osc2Pitch, ParamID::osc2Fine, ParamID::osc2Level,
          ParamID::voices, ParamID::detune, ParamID::width,
          ParamID::attack, ParamID::release, ParamID::output };
    constexpr std::array<const char*, Synth2AudioProcessorEditor::numDials> dialNames
        { "PITCH", "FINE", "LEVEL", "PITCH", "FINE", "LEVEL",
          "VOICES", "DETUNE", "WIDTH", "ATTACK", "RELEASE", "OUTPUT" };

    constexpr size_t firstOsc2Dial = 3;
    constexpr size_t dialsPerRow   = 6;

    // Each row runs the spectrum left to right, cyan to amber.
    juce::Colour dialColour (size_t i)
    {
        return ColourMath::spectrum ((float) (i % dialsPerRow) / (float) (dialsPerRow - 1));
    }

    bool isOsc2Dial (size_t i) { return i >= firstOsc2Dial && i < firstOsc2Dial + 3; }

    float paramValue (const Synth2AudioProcessor& p, const char* id) { return p.getState().getRawParameterValue (id)->load(); }

    juce::String dialValueText (const Synth2AudioProcessor& p, size_t index)
    {
        const juce::String id (dialParamIds[index]);
        const float value = paramValue (p, dialParamIds[index]);

        if (id == ParamID::osc1Pitch || id == ParamID::osc2Pitch) return Format::semitones (juce::roundToInt (value));
        if (id == ParamID::osc1Fine  || id == ParamID::osc2Fine)  return Format::fineCents (value);
        if (id == ParamID::voices)                                return juce::String (juce::roundToInt (value));
        if (id == ParamID::detune)                                return Format::cents (value);
        if (id == ParamID::attack    || id == ParamID::release)   return Format::seconds (value);
        if (id == ParamID::output)                                return Format::decibels (value);
        return Format::percent (value);   // the levels and WIDTH
    }
}

Synth2AudioProcessorEditor::Synth2AudioProcessorEditor (Synth2AudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      osc1Picker (*p.getState().getParameter (ParamID::wave)),
      osc2Picker (*p.getState().getParameter (ParamID::osc2Wave), p.getState().getParameter (ParamID::osc2On), "OSC 2"),
      keyboard (p.getKeyboardState())
{
    waveDisplays[1].setCaption ({});   // OSC 2's power chip names it
    for (int osc = 0; osc < numOscillators; ++osc)
    {
        auto& display = waveDisplays[(size_t) osc];
        auto& picker  = getWavePicker (osc);
        auto& menu    = waveMenus[(size_t) osc];
        addAndMakeVisible (display);
        addAndMakeVisible (picker);

        // Each well opens its tray of shapes; hovering one previews it in the
        // well, picking one sets that oscillator's shape.
        picker.onOpenMenu = [&menu, &picker]
        {
            menu.open (picker.getSelected());
            picker.setMenuOpen (true);
        };
        menu.onChoose = [&picker]  (int wave) { picker.choose (wave); };
        menu.onHover  = [&display] (int wave) { display.setPreview (wave); };
        menu.onClose  = [&picker] { picker.setMenuOpen (false); };
    }

    addAndMakeVisible (unisonDisplay);
    addAndMakeVisible (ampDisplay);
    ampDisplay.setColours (dialColour (9), dialColour (10), dialColour (11));   // ATTACK, RELEASE, OUTPUT
    addAndMakeVisible (keyboard);

    // OSC 2 is out of sight until it is added.
    addChildComponent (addOsc2);
    addOsc2.onClick = [this]
    {
        osc2Picker.setOn (true);
        showOsc2 (true, true);
    };

    for (size_t i = 0; i < dials.size(); ++i)
    {
        auto& dial = dials[i];
        dial.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        // 270 degree sweep from 7:30 to 4:30. JUCE wants non-negative angles, so
        // -135..135 degrees is written as the identical 225..495.
        dial.setRotaryParameters (juce::degreesToRadians (225.0f),
                                  juce::degreesToRadians (495.0f), true);
        dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        dial.setMouseDragSensitivity (200);
        dial.setColour (juce::Slider::rotarySliderFillColourId, dialColour (i));
        dial.setLookAndFeel (&looks);
        dial.setComponentID (dialParamIds[i]);
        dial.setPaintingIsUnclipped (true);   // the arc's glow reaches past the dial

        const auto* parameter = p.getState().getParameter (dialParamIds[i]);
        dial.setDoubleClickReturnValue (true, p.getState().getParameterRange (dialParamIds[i])
                                                  .convertFrom0to1 (parameter->getDefaultValue()));
        dial.setTooltip (parameter->getName (64));

        // The names and values are painted under the dials, so keep them in
        // step during a drag rather than trailing by a timer tick.
        dial.onValueChange = [this] { repaint(); };
        addAndMakeVisible (dial);
        attachments[i] = std::make_unique<SliderAttachment> (p.getState(), dialParamIds[i], dial);
    }

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setResizeLimits ((int) kDesignWidth, (int) kDesignHeight,
                     (int) (kDesignWidth * 2.0f), (int) (kDesignHeight * 2.0f));

    showOsc2 (p.currentSettings().osc2On, false);
    setSize ((int) kDesignWidth, (int) kDesignHeight);
    for (auto& menu : waveMenus)
        addChildComponent (menu);   // last, so they open over everything
    resized();
    timerCallback();
    startTimerHz (30);
}

Synth2AudioProcessorEditor::~Synth2AudioProcessorEditor()
{
    stopTimer();
    for (auto& dial : dials)
        dial.setLookAndFeel (nullptr);
}

VoiceDisplayState Synth2AudioProcessorEditor::displayState (Waveform wave) const
{
    const auto settings = processorRef.currentSettings();
    VoiceDisplayState state;
    state.wave   = wave;
    state.voices = Unison::clampVoices (settings.voices);
    state.detune = settings.detuneCents;
    state.width  = settings.width;
    return state;
}

WaveDisplay::Osc Synth2AudioProcessorEditor::oscView (int oscillator) const
{
    const auto settings = processorRef.currentSettings();
    WaveDisplay::Osc view;
    if (oscillator == 0)
    {
        view.cycles = Synth2VisualModel::cyclesFor (settings.osc1Semitones, settings.osc1FineCents);
        view.level  = settings.osc1Level;
    }
    else
    {
        view.cycles = Synth2VisualModel::cyclesFor (settings.osc2Semitones, settings.osc2FineCents);
        view.level  = settings.osc2Level;
    }
    return view;
}

void Synth2AudioProcessorEditor::showOsc2 (bool shown, bool fadeIn)
{
    osc2Shown = shown;
    if (! shown)
        waveMenus[1].close();

    // OSC 2's well (with its picker) and its three dials come and go together;
    // OSC 1 takes the whole row while they're away.
    const std::array<juce::Component*, 5> parts { &waveDisplays[1], &osc2Picker, &dials[3], &dials[4], &dials[5] };
    for (auto* part : parts)
        part->setVisible (shown && ! fadeIn);
    addOsc2.setVisible (! shown);

    resized();
    if (shown && fadeIn)
        for (auto* part : parts)
            juce::Desktop::getInstance().getAnimator().fadeIn (part, 150);   // --dur-base
    repaint();
}

AmpDisplayState Synth2AudioProcessorEditor::ampState() const
{
    const auto& parameters = processorRef.getState();
    const auto  settings   = processorRef.currentSettings();

    AmpDisplayState state;
    state.attackPosition  = parameters.getParameter (ParamID::attack)->getValue();
    state.releasePosition = parameters.getParameter (ParamID::release)->getValue();
    state.attackSeconds   = settings.attackSeconds;
    state.releaseSeconds  = settings.releaseSeconds;
    state.outputDb        = settings.outputDb;

    static_assert (std::tuple_size_v<decltype (state.notes)> == Synth2Engine::maxNotes);
    state.notes = processorRef.getNoteActivity();
    return state;
}

void Synth2AudioProcessorEditor::timerCallback()
{
    // The displays follow the parameters, however they change (dials, host
    // automation, a restored set), and the AMP well the notes as they play;
    // each repaints only when its state moved.
    const auto settings = processorRef.currentSettings();
    waveDisplays[0].setState (displayState (settings.wave));
    waveDisplays[1].setState (displayState (settings.osc2Wave));
    for (int osc = 0; osc < numOscillators; ++osc)
        waveDisplays[(size_t) osc].setOsc (oscView (osc));
    unisonDisplay.setState (displayState (settings.wave));
    ampDisplay.setAmpState (ampState());

    // OSC 2 appears when it is added and goes when it is switched off, from
    // its chips or from the host.
    if (settings.osc2On != osc2Shown)
        showOsc2 (settings.osc2On, settings.osc2On);

    // Light the keys the host is playing.
    processorRef.showHostNotesOnKeyboard();
}

void Synth2AudioProcessorEditor::paint (juce::Graphics& g)
{
    const float s = uiScale;
    const auto w = (float) getWidth();
    const auto h = (float) getHeight();

    // --- panel: radial well like the design system's --panel ---------------
    const juce::Point centre (w * 0.5f, -0.1f * h);
    float radius = 0.0f;
    radius = juce::jmax (radius, centre.getDistanceFrom ({ 0.0f, 0.0f }));
    radius = juce::jmax (radius, centre.getDistanceFrom ({ w,    0.0f }));
    radius = juce::jmax (radius, centre.getDistanceFrom ({ 0.0f, h    }));
    radius = juce::jmax (radius, centre.getDistanceFrom ({ w,    h    }));

    juce::ColourGradient panel (Palette::panelTop, centre.x, centre.y,
                                Palette::panelBottom, centre.x, centre.y + radius, true);
    panel.addColour (0.6, Palette::panelBottom);

    // Solid dark base first so the rounded panel's corners read as clean dark
    // rather than showing whatever the host paints behind a (not-guaranteed)
    // transparent corner.
    g.fillAll (Palette::panelBottom);

    const float cornerRadius = 16.0f * s;
    const auto  panelBounds  = getLocalBounds().toFloat().reduced (juce::jmax (1.0f, s));
    g.setGradientFill (panel);
    g.fillRoundedRectangle (panelBounds, cornerRadius);

    g.setColour (Palette::hairline);
    g.drawRoundedRectangle (panelBounds, cornerRadius, juce::jmax (1.0f, s));

    paintHeader (g, s);

    // Hairlines above the dials and above the keyboard.
    g.setColour (Palette::hairline);
    for (const int y : { layout.groupsSeparatorY, layout.keyboardSeparatorY })
        g.fillRect (juce::Rectangle (kPanelPad * s, (float) y, w - 2.0f * kPanelPad * s, juce::jmax (1.0f, s)));

    paintDialLabels (g, s);
}

void Synth2AudioProcessorEditor::paintHeader (juce::Graphics& g, float s) const
{
    const auto area = layout.header.toFloat();

    // Wordmark: house brand "Zanders" in primary text, tight against the violet
    // product name — CamelCase, no space, per the design-system Wordmark.
    const auto wordmarkFont = Fonts::sans (17.0f * s, true, -0.01f);   // --tracking-title
    g.setFont (wordmarkFont);

    const juce::String house ("Zanders");
    g.setColour (Palette::textStrong);
    g.drawText (house, area, juce::Justification::centredLeft, false);

    g.setColour (Palette::violet);
    g.drawText ("Synth2", area.withTrimmedLeft (Fonts::width (wordmarkFont, house)),
                juce::Justification::centredLeft, false);

    // The mode badge: a small accent pill.
    const auto badge = layout.badge.toFloat();
    g.setColour (Palette::accentSoft);
    g.fillRoundedRectangle (badge, badge.getHeight() * 0.5f);
    g.setFont (Fonts::sans (10.0f * s, true, 0.14f));   // --tracking-label
    g.setColour (Palette::accent);
    g.drawText ("POLY", badge, juce::Justification::centred, false);
}

void Synth2AudioProcessorEditor::paintDialLabels (juce::Graphics& g, float s) const
{
    // Dial convention: name + mono value stacked *beneath* the arc — the name in
    // grey (--text-2), the value in the dial's own colour.
    const auto nameFont  = Fonts::sans (10.0f * s, true, 0.10f);   // --tracking-data
    const auto valueFont = Fonts::mono (10.0f * s);

    for (size_t i = 0; i < dials.size(); ++i)
    {
        const auto  column  = layout.dialColumns[i].toFloat();
        const float textTop = dials[i].getBounds().toFloat().getBottom() + 6.0f * s;
        if (isOsc2Dial (i) && ! osc2Shown)
            continue;

        g.setFont (nameFont);
        g.setColour (Palette::textMid);
        g.drawText (dialNames[i], juce::Rectangle (column.getX(), textTop, column.getWidth(), 12.0f * s),
                    juce::Justification::centredTop, false);

        g.setFont (valueFont);
        g.setColour (dialColour (i));
        g.drawText (dialValueText (processorRef, i),
                    juce::Rectangle (column.getX(), textTop + 13.0f * s, column.getWidth(), 13.0f * s),
                    juce::Justification::centredTop, false);
    }
}

void Synth2AudioProcessorEditor::resized()
{
    // A fade still running (OSC 2 coming in, a tray opening) would put its
    // component back where it started: finish it before laying out afresh.
    auto& animator = juce::Desktop::getInstance().getAnimator();
    for (auto* part : std::initializer_list<juce::Component*> { &waveDisplays[1], &osc2Picker, &dials[3], &dials[4], &dials[5],
                                                                &waveMenus[0], &waveMenus[1] })
        animator.cancelAnimation (part, true);

    uiScale = (float) getWidth() / kDesignWidth;
    const float s = uiScale;
    const auto px = [s] (float v) { return (int) std::round (v * s); };

    looks.setUiScale (s);
    for (int osc = 0; osc < numOscillators; ++osc)
    {
        waveDisplays[(size_t) osc].setUiScale (s);
        getWavePicker (osc).setUiScale (s);
        waveMenus[(size_t) osc].setUiScale (s);
    }
    unisonDisplay.setUiScale (s);
    keyboard.setUiScale (s);

    ampDisplay.setUiScale (s);

    auto area = getLocalBounds().reduced (px (kPanelPad));

    // Header: wordmark on the left, the badge on the right.
    layout.header = area.removeFromTop (px (20.0f));
    layout.badge  = layout.header.withTrimmedLeft (layout.header.getWidth() - px (54.0f))
                                 .withSizeKeepingCentre (px (54.0f), px (20.0f));

    // Two rows of two groups, each group a display over its three dials.
    const int groupGap = px (12.0f);
    const auto layDials = [&] (juce::Rectangle<int> row, size_t first)
    {
        const int columnWidth = row.getWidth() / 3;
        for (size_t i = first; i < first + 3; ++i)
        {
            auto column = i + 1 == first + 3 ? row : row.removeFromLeft (columnWidth);
            layout.dialColumns[i] = column;
            dials[i].setBounds (column.withHeight (px (52.0f)).withSizeKeepingCentre (px (52.0f), px (52.0f)));
        }
    };
    const auto layRow = [&] (juce::Rectangle<int> row, juce::Component& leftDisplay, juce::Component& rightDisplay, size_t firstDial)
    {
        auto left  = row.removeFromLeft ((row.getWidth() - groupGap) / 2);
        auto right = row.withTrimmedLeft (groupGap);
        leftDisplay .setBounds (left .removeFromTop (px (96.0f)));
        rightDisplay.setBounds (right.removeFromTop (px (96.0f)));
        left .removeFromTop (px (12.0f));
        right.removeFromTop (px (12.0f));

        layDials (left, firstDial);
        layDials (right, firstDial + 3);
    };

    // The oscillators: OSC 1 and OSC 2 side by side, or OSC 1 across the whole
    // row until OSC 2 is added. Each well has its picker laid over it; their
    // trays cover the editor and drop across the row.
    area.removeFromTop (px (16.0f));
    auto oscillators = area.removeFromTop (px (190.0f));
    if (osc2Shown)
    {
        layRow (oscillators, waveDisplays[0], waveDisplays[1], 0);
    }
    else
    {
        waveDisplays[0].setBounds (oscillators.removeFromTop (px (96.0f)));
        oscillators.removeFromTop (px (12.0f));
        layDials (oscillators, 0);
    }
    const auto wells = osc2Shown ? waveDisplays[0].getBounds().getUnion (waveDisplays[1].getBounds())
                                 : waveDisplays[0].getBounds();

    for (int osc = 0; osc < numOscillators; ++osc)
    {
        getWavePicker (osc).setBounds (waveDisplays[(size_t) osc].getBounds());
        waveMenus[(size_t) osc].setBounds (getLocalBounds());
        waveMenus[(size_t) osc].setAnchor (waveDisplays[(size_t) osc].getBounds(), wells);
    }

    // "+ OSC 2" sits in OSC 1's well, just left of its pill.
    const auto osc1Well = waveDisplays[0].getBounds();
    const auto pill     = osc1Picker.pillBounds();
    addOsc2.setUiScale (s);
    addOsc2.setBounds (osc1Well.getX() + (int) pill.getX() - px (6.0f + 72.0f), osc1Well.getY() + (int) pill.getY(),
                       px (72.0f), (int) pill.getHeight());

    // Hairline, then the voice: UNISON on the left, AMP on the right.
    area.removeFromTop (px (12.0f));
    layout.groupsSeparatorY = area.getY();
    area.removeFromTop (px (14.0f));
    layRow (area.removeFromTop (px (190.0f)), unisonDisplay, ampDisplay, 6);

    // Hairline, then the keyboard fills the rest.
    area.removeFromTop (px (12.0f));
    layout.keyboardSeparatorY = area.getY();
    area.removeFromTop (px (14.0f));
    keyboard.setBounds (area);
}
