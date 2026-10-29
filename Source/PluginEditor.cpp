#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Synth2Theme.h"

#include <array>
#include <cmath>

using namespace Synth2;

namespace
{
    constexpr float kDesignWidth  = 480.0f;
    constexpr float kDesignHeight = 548.0f;
    constexpr float kPanelPad     = 24.0f;

    // The dials, in display order: three for the unison stack, three for the amp.
    constexpr std::array<const char*, Synth2AudioProcessorEditor::numDials> dialParamIds
        { ParamID::voices, ParamID::detune, ParamID::width, ParamID::attack, ParamID::release, ParamID::output };
    constexpr std::array<const char*, Synth2AudioProcessorEditor::numDials> dialNames
        { "VOICES", "DETUNE", "WIDTH", "ATTACK", "RELEASE", "OUTPUT" };

    // The row runs the spectrum left to right, cyan to amber.
    juce::Colour dialColour (int i)
    {
        return ColourMath::spectrum ((float) i / (float) (Synth2AudioProcessorEditor::numDials - 1));
    }

    float paramValue (const Synth2AudioProcessor& p, const char* id) { return p.getState().getRawParameterValue (id)->load(); }

    juce::String dialValueText (const Synth2AudioProcessor& p, size_t index)
    {
        const float value = paramValue (p, dialParamIds[index]);
        switch (index)
        {
            case 0:  return juce::String (juce::roundToInt (value));
            case 1:  return Format::cents (value);
            case 2:  return Format::percent (value);
            case 3:
            case 4:  return Format::seconds (value);
            default: break;
        }
        return Format::decibels (value);
    }
}

Synth2AudioProcessorEditor::Synth2AudioProcessorEditor (Synth2AudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      waveSelector (*p.getState().getParameter (ParamID::wave)),
      keyboard (p.getKeyboardState())
{
    addAndMakeVisible (waveDisplay);
    addAndMakeVisible (unisonDisplay);
    addAndMakeVisible (ampDisplay);
    ampDisplay.setColours (dialColour (3), dialColour (4), dialColour (5));   // ATTACK, RELEASE, OUTPUT
    addAndMakeVisible (waveSelector);
    addAndMakeVisible (keyboard);

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
        dial.setColour (juce::Slider::rotarySliderFillColourId, dialColour ((int) i));
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

    setSize ((int) kDesignWidth, (int) kDesignHeight);
    timerCallback();
    startTimerHz (30);
}

Synth2AudioProcessorEditor::~Synth2AudioProcessorEditor()
{
    stopTimer();
    for (auto& dial : dials)
        dial.setLookAndFeel (nullptr);
}

VoiceDisplayState Synth2AudioProcessorEditor::displayState() const
{
    const auto settings = processorRef.currentSettings();
    VoiceDisplayState state;
    state.wave   = settings.wave;
    state.voices = Unison::clampVoices (settings.voices);
    state.detune = settings.detuneCents;
    state.width  = settings.width;
    return state;
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
    const auto state = displayState();
    waveDisplay.setState (state);
    unisonDisplay.setState (state);
    ampDisplay.setAmpState (ampState());

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
    for (const int y : { layout.dialsSeparatorY, layout.keyboardSeparatorY })
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

        g.setFont (nameFont);
        g.setColour (Palette::textMid);
        g.drawText (dialNames[i], juce::Rectangle (column.getX(), textTop, column.getWidth(), 12.0f * s),
                    juce::Justification::centredTop, false);

        g.setFont (valueFont);
        g.setColour (dialColour ((int) i));
        g.drawText (dialValueText (processorRef, i),
                    juce::Rectangle (column.getX(), textTop + 13.0f * s, column.getWidth(), 13.0f * s),
                    juce::Justification::centredTop, false);
    }
}

void Synth2AudioProcessorEditor::resized()
{
    uiScale = (float) getWidth() / kDesignWidth;
    const float s = uiScale;
    const auto px = [s] (float v) { return (int) std::round (v * s); };

    looks.setUiScale (s);
    waveDisplay.setUiScale (s);
    unisonDisplay.setUiScale (s);
    waveSelector.setUiScale (s);
    keyboard.setUiScale (s);

    ampDisplay.setUiScale (s);

    auto area = getLocalBounds().reduced (px (kPanelPad));

    // Header: wordmark on the left, the badge on the right.
    layout.header = area.removeFromTop (px (20.0f));
    layout.badge  = layout.header.withTrimmedLeft (layout.header.getWidth() - px (54.0f))
                                 .withSizeKeepingCentre (px (54.0f), px (20.0f));

    // The WAVE well across the panel, the wave chips under it.
    area.removeFromTop (px (16.0f));
    waveDisplay.setBounds (area.removeFromTop (px (84.0f)));
    area.removeFromTop (px (10.0f));
    waveSelector.setBounds (area.removeFromTop (px (28.0f)));

    // Hairline, then two groups side by side, each a display over its three
    // dials: UNISON on the left, AMP on the right.
    area.removeFromTop (px (16.0f));
    layout.dialsSeparatorY = area.getY();
    area.removeFromTop (px (14.0f));

    auto groups = area.removeFromTop (px (190.0f));
    const int groupGap   = px (12.0f);
    auto unisonGroup = groups.removeFromLeft ((groups.getWidth() - groupGap) / 2);
    auto ampGroup    = groups.withTrimmedLeft (groupGap);

    unisonDisplay.setBounds (unisonGroup.removeFromTop (px (96.0f)));
    ampDisplay.setBounds (ampGroup.removeFromTop (px (96.0f)));
    unisonGroup.removeFromTop (px (12.0f));
    ampGroup.removeFromTop (px (12.0f));

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
    layDials (unisonGroup, 0);
    layDials (ampGroup, 3);

    // Hairline, then the keyboard fills the rest.
    area.removeFromTop (px (12.0f));
    layout.keyboardSeparatorY = area.getY();
    area.removeFromTop (px (14.0f));
    keyboard.setBounds (area);
}
