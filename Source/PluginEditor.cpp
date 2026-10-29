#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Presets.h"
#include "Synth2Theme.h"

#include <array>
#include <cmath>

using namespace Synth2;

namespace
{
    constexpr float kPanelPad = 24.0f;
    constexpr float kBenchTop = 60.0f;   // the bench sits under the header

    float paramValue (const Synth2AudioProcessor& p, const char* id) { return p.getState().getRawParameterValue (id)->load(); }

    // The signal-flow choices, in the parameter's order.
    const std::array<ChoiceTray::Choice, 3> flowChoices {
        ChoiceTray::Choice { "Fixed order",   "By kind, wherever the modules sit" },
        ChoiceTray::Choice { "Follow layout", "Row by row, left to right" },
        ChoiceTray::Choice { "Patch cables",  "Along cables patched to the output" } };

    // How much of each end of the preset picker steps to the previous / next.
    constexpr float kPresetArrowWidth = 28.0f;
}

Synth2AudioProcessorEditor::Synth2AudioProcessorEditor (Synth2AudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      model (p.getState()),
      bench (p.getState(), looks),
      keyboard (p.getKeyboardState())
{
    addAndMakeVisible (bench);
    addAndMakeVisible (keyboard);

    BenchStyle::setUpDial (outputDial, BenchStyle::dialColour (1.0f), looks, p.getState(), ParamID::output);
    outputDial.onValueChange = [this] { repaint (layout.outputColumn); };
    addAndMakeVisible (outputDial);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), ParamID::output, outputDial);

    addAndMakeVisible (cables);

    // A click on an empty spot offers modules to add there.
    bench.onEmptyClick ([this] (juce::Point<float> benchAt, juce::Point<int> viewAt)
    {
        openAddTray (benchAt, viewAt + bench.getPosition());
    });

    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        auto& module = bench.getOscillator (slot);
        auto& menu   = waveMenus[(size_t) slot];
        auto& picker = module.getPicker();
        auto& well   = module.getDisplay();

        // Each well opens its tray of shapes under it; hovering one previews
        // it in the well, picking one sets that oscillator's shape.
        picker.onOpenMenu ([this, &menu, &picker, &well]
        {
            const auto wellBounds = getLocalArea (&well, well.getLocalBounds());
            const auto row = wellBounds.withSizeKeepingCentre (juce::jmax (wellBounds.getWidth(), juce::roundToInt (440.0f * uiScale)),
                                                               wellBounds.getHeight())
                                       .constrainedWithin (getLocalBounds().reduced (juce::roundToInt (kPanelPad * uiScale)));
            menu.setAnchor (wellBounds, row);
            menu.open (picker.getSelected());
            picker.setMenuOpen (true);
        });
        menu.onChoose ([&picker] (int wave) { picker.choose (wave); });
        menu.onHover  ([&well]   (int wave) { well.setPreview (wave); });
        menu.onClose  ([&picker] { picker.setMenuOpen (false); });

        module.onJack ([this, id = module.getId()] (const juce::MouseEvent& e, int phase) { patch (id, e, phase); });
    }

    addTray.onChoose ([this] (int choice)
    {
        constexpr std::array<Bench::Kind, 3> kinds { Bench::Kind::oscillator, Bench::Kind::unison, Bench::Kind::envelope };
        bench.add (kinds[(size_t) choice], addAt);
        lastCables.clear();
    });
    settingsTray.onChoose ([this] (int choice)
    {
        auto* flow = processorRef.getState().getParameter (ParamID::signalFlow);
        flow->beginChangeGesture();
        flow->setValueNotifyingHost (flow->convertTo0to1 ((float) choice));
        flow->endChangeGesture();
        showFlow();
    });
    presetTray.onChoose ([this] (int choice) { processorRef.loadPreset (choice); });

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setResizeLimits ((int) (designWidth * 0.75f), (int) (designHeight * 0.75f),
                     (int) (designWidth * 2.0f), (int) (designHeight * 2.0f));

    setSize ((int) designWidth, (int) designHeight);
    for (auto& menu : waveMenus)
        addChildComponent (menu);   // last, so the trays open over everything
    addChildComponent (addTray);
    addChildComponent (settingsTray);
    addChildComponent (presetTray);
    resized();
    timerCallback();
    startTimerHz (30);
}

Synth2AudioProcessorEditor::~Synth2AudioProcessorEditor()
{
    stopTimer();
    outputDial.setLookAndFeel (nullptr);
}

// ---- What the modules show --------------------------------------------------------

VoiceDisplayState Synth2AudioProcessorEditor::voiceState (const Synth2Engine::Settings& settings) const
{
    // The stack as it plays: one plain voice while unison is off the bench,
    // drawn in the shape of the first oscillator on it.
    VoiceDisplayState state;
    state.wave = Waveform::saw;
    for (const auto& osc : settings.oscillators)
        if (osc.on)
        {
            state.wave = osc.wave;
            break;
        }
    state.voices = settings.unisonOn ? Unison::clampVoices (settings.voices) : 1;
    state.detune = settings.detuneCents;
    state.width  = settings.width;
    return state;
}

AmpDisplayState Synth2AudioProcessorEditor::ampState (const Synth2Engine::Settings& settings) const
{
    const auto& parameters = processorRef.getState();

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

Synth2AudioProcessor::SignalFlow Synth2AudioProcessorEditor::signalFlow() const
{
    return (Synth2AudioProcessor::SignalFlow) juce::jlimit (0, 2, juce::roundToInt (paramValue (processorRef, ParamID::signalFlow)));
}

// The modules' headers follow the signal flow: their place in the order when
// sound follows the layout, OUT jacks (and greyed out until patched) when it
// follows the cables.
void Synth2AudioProcessorEditor::showFlow()
{
    const auto flow = signalFlow();
    const bool followLayout = flow == Synth2AudioProcessor::SignalFlow::layout;
    const bool followCables = flow == Synth2AudioProcessor::SignalFlow::cables;

    std::array<int, Bench::allModules.size()> places {};
    if (followLayout)
    {
        int place = 0;
        for (const auto& id : model.readingOrder())
            for (size_t i = 0; i < Bench::allModules.size(); ++i)
                if (Bench::allModules[i] == id)
                    places[i] = ++place;
    }
    for (size_t i = 0; i < Bench::allModules.size(); ++i)
        bench.getModule (Bench::allModules[i]).setOrder (places[i]);

    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        auto& module = bench.getOscillator (slot);
        const bool patched = model.isPatchedToOutput (module.getId());
        module.setJackShown (followCables);
        module.setPlugged (followCables && patched);
        module.setDimmed (followCables && ! patched);
    }

    if ((int) flow != lastFlow)
    {
        lastFlow = (int) flow;
        repaint();
    }
}

void Synth2AudioProcessorEditor::timerCallback()
{
    // The modules follow the parameters, however they change (dials, host
    // automation, a restored session), and the envelope the notes as they
    // play; each display repaints only when its state moved.
    bench.sync();
    const auto settings = processorRef.currentSettings();
    const auto voice    = voiceState (settings);
    for (int slot = 0; slot < Bench::numOscillators; ++slot)
        bench.getOscillator (slot).refresh (settings.oscillators[(size_t) slot], voice);
    bench.getUnison().refresh (voice);
    bench.getEnvelope().refresh (ampState (settings));
    showFlow();

    if (const auto signature = cableSignature(); signature != lastCables)
    {
        lastCables = signature;
        cables.repaint();
        repaint (layout.outputColumn);
    }
    if (const int preset = processorRef.getCurrentProgram(); preset != lastPreset)
    {
        lastPreset = preset;
        repaint (layout.preset);
    }
    if (! juce::exactlyEqual (settings.outputDb, lastOutput))
    {
        lastOutput = settings.outputDb;
        repaint (layout.outputColumn);
    }

    processorRef.showHostNotesOnKeyboard();
}

// ---- Trays ---------------------------------------------------------------------------

void Synth2AudioProcessorEditor::openAddTray (juce::Point<float> benchAt, juce::Point<int> editorAt)
{
    addAt = benchAt;
    const int  left      = model.oscillatorsLeft();
    const bool unisonFree   = model.canAdd (Bench::Kind::unison);
    const bool envelopeFree = model.canAdd (Bench::Kind::envelope);
    std::vector<ChoiceTray::Choice> choices {
        { "Oscillator", left > 0 ? juce::String (left) + " of 4 left: a shape at its own pitch and level"
                                 : juce::String ("All four are on the bench"), left > 0 },
        { "Unison",   unisonFree   ? "Stack, detune and spread every note" : "Already on the bench", unisonFree },
        { "Envelope", envelopeFree ? "Shape each note's attack and release" : "Already on the bench", envelopeFree } };
    addTray.setBounds (getLocalBounds());
    addTray.open ("ADD TO THE BENCH", std::move (choices), editorAt);
}

void Synth2AudioProcessorEditor::openSettings()
{
    std::vector<ChoiceTray::Choice> choices (flowChoices.begin(), flowChoices.end());
    choices[(size_t) signalFlow()].ticked = true;
    settingsTray.setBounds (getLocalBounds());
    const auto gear = layout.gear;
    settingsTray.open ("SIGNAL FLOW", std::move (choices),
                       { gear.getRight() - juce::roundToInt (260.0f * uiScale), gear.getBottom() + juce::roundToInt (8.0f * uiScale) });
    repaint (layout.gear);
}

void Synth2AudioProcessorEditor::openPresets()
{
    std::vector<ChoiceTray::Choice> choices;
    for (const auto& preset : Presets::factory)
        choices.push_back ({ preset.name, preset.detail });
    choices[(size_t) processorRef.getCurrentProgram()].ticked = true;
    presetTray.setBounds (getLocalBounds());
    const auto picker = layout.preset;
    presetTray.open ("PRESETS", std::move (choices),
                     { picker.getCentreX() - juce::roundToInt (130.0f * uiScale), picker.getBottom() + juce::roundToInt (8.0f * uiScale) });
    repaint (layout.preset);
}

void Synth2AudioProcessorEditor::stepPreset (int delta)
{
    processorRef.loadPreset ((processorRef.getCurrentProgram() + delta + Presets::numPresets) % Presets::numPresets);
}

// ---- Cables ----------------------------------------------------------------------------

juce::Rectangle<float> Synth2AudioProcessorEditor::outputJackBounds() const noexcept
{
    const float s = uiScale;
    const auto column = layout.outputColumn.toFloat();
    return { column.getX() + 4.0f * s, column.getY() + 17.0f * s, 18.0f * s, 18.0f * s };
}

juce::Point<float> Synth2AudioProcessorEditor::jackCentre (const Bench::ModuleId& id)
{
    auto& module = bench.getModule (id);
    return getLocalPoint (&module, module.jackBounds().getCentre());
}

void Synth2AudioProcessorEditor::patch (const Bench::ModuleId& from, const juce::MouseEvent& e, int phase)
{
    const auto at = e.getEventRelativeTo (this).position;
    if (phase == BenchModule::jackDown)
    {
        // Picking up a patched jack unplugs its cable, to drop elsewhere.
        model.disconnect (from);
        patchingFrom = from;
        patchingTo   = at;
    }
    else if (phase == BenchModule::jackDrag)
    {
        patchingTo = at;
    }
    else
    {
        if (outputJackBounds().expanded (10.0f * uiScale).contains (at))
            model.connect (from);
        patchingFrom.reset();
    }
    showFlow();
    cables.repaint();
    repaint (layout.outputColumn);
}

void Synth2AudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (layout.gear.toFloat().contains (e.position))
    {
        openSettings();
        return;
    }
    if (layout.preset.toFloat().contains (e.position))
    {
        const float arrow = kPresetArrowWidth * uiScale;
        if (e.position.x < (float) layout.preset.getX() + arrow)
            stepPreset (-1);
        else if (e.position.x > (float) layout.preset.getRight() - arrow)
            stepPreset (1);
        else
            openPresets();
        return;
    }

    // Grabbing the output's IN jack takes the last cable patched into it.
    if (signalFlow() == Synth2AudioProcessor::SignalFlow::cables && outputJackBounds().expanded (4.0f * uiScale).contains (e.position))
    {
        const auto patched = model.patchedToOutput();
        if (! patched.empty())
        {
            const auto from = patched.back();
            model.disconnect (from);
            patchingFrom = from;
            patchingTo   = e.position;
            showFlow();
            cables.repaint();
        }
    }
}

void Synth2AudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! patchingFrom.has_value())
        return;
    patchingTo = e.position;
    cables.repaint();
}

void Synth2AudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! patchingFrom.has_value())
        return;
    if (outputJackBounds().expanded (10.0f * uiScale).contains (e.position))
        model.connect (*patchingFrom);
    patchingFrom.reset();
    showFlow();
    cables.repaint();
    repaint (layout.outputColumn);
}

juce::String Synth2AudioProcessorEditor::cableSignature()
{
    juce::String signature (signalFlow() == Synth2AudioProcessor::SignalFlow::cables ? "cables" : "none");
    for (const auto& id : model.patchedToOutput())
        if (bench.getModule (id).isVisible())
            signature << " " << id.toString() << "@" << jackCentre (id).toString();
    return signature;
}

void Synth2AudioProcessorEditor::paintCable (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour) const
{
    // A cable hangs: a curve that sags below both ends, more the longer it is.
    const float s   = uiScale;
    const float sag = 40.0f * s + 0.25f * from.getDistanceFrom (to);
    juce::Path cable;
    cable.startNewSubPath (from);
    cable.cubicTo (from.translated (0.0f, sag), to.translated (0.0f, sag), to);

    const juce::PathStrokeType::JointStyle curved = juce::PathStrokeType::curved;
    const juce::PathStrokeType::EndCapStyle round = juce::PathStrokeType::rounded;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (cable, juce::PathStrokeType (6.0f * s, curved, round), juce::AffineTransform::translation (0.0f, 3.0f * s));
    g.setColour (colour.withAlpha (0.25f));
    g.strokePath (cable, juce::PathStrokeType (7.0f * s, curved, round));
    g.setColour (colour);
    g.strokePath (cable, juce::PathStrokeType (3.0f * s, curved, round));
    g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre (from));
    g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre (to));
}

void Synth2AudioProcessorEditor::paintCables (juce::Graphics& g)
{
    if (signalFlow() != Synth2AudioProcessor::SignalFlow::cables)
        return;

    const auto in = outputJackBounds().getCentre();
    for (const auto& id : model.patchedToOutput())
        if (bench.getModule (id).isVisible())
            paintCable (g, jackCentre (id), in, BenchStyle::dialColour (0.25f * (float) id.slot));

    if (patchingFrom.has_value())
        paintCable (g, jackCentre (*patchingFrom), patchingTo, BenchStyle::dialColour (0.25f * (float) patchingFrom->slot).withAlpha (0.8f));
}

// ---- Painting ---------------------------------------------------------------------------

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

    // Hairline above the keyboard and OUTPUT.
    g.setColour (Palette::hairline);
    g.fillRect (juce::Rectangle (kPanelPad * s, (float) layout.stripSeparatorY, w - 2.0f * kPanelPad * s, juce::jmax (1.0f, s)));

    paintOutput (g, s);
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

    // The preset picker: the loaded preset's name in a pill, with arrows to
    // step either way, lit while its tray is open.
    const auto picker = layout.preset.toFloat();
    g.setColour (presetTray.isOpen() ? Palette::idleFill.withMultipliedAlpha (1.6f) : Palette::idleFill);
    g.fillRoundedRectangle (picker, picker.getHeight() * 0.5f);
    g.setColour (presetTray.isOpen() ? Palette::strongLine : Palette::hairline);
    g.drawRoundedRectangle (picker, picker.getHeight() * 0.5f, juce::jmax (1.0f, s));

    const float arrowWidth = kPresetArrowWidth * s;
    const auto  arrow = [&g, s] (juce::Rectangle<float> box, float direction)
    {
        const auto  c = box.getCentre();
        const float h = 3.5f * s;
        juce::Path chevron;
        chevron.startNewSubPath (c.x + h * 0.5f * direction, c.y - h);
        chevron.lineTo (c.x - h * 0.5f * direction, c.y);
        chevron.lineTo (c.x + h * 0.5f * direction, c.y + h);
        g.strokePath (chevron, juce::PathStrokeType (juce::jmax (1.0f, 1.6f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    g.setColour (Palette::textDim);
    arrow (picker.withWidth (arrowWidth), 1.0f);
    arrow (picker.withTrimmedLeft (picker.getWidth() - arrowWidth), -1.0f);
    g.setFont (Fonts::sans (12.0f * s, true));
    g.setColour (Palette::textStrong);
    g.drawText (processorRef.getProgramName (processorRef.getCurrentProgram()), picker.reduced (arrowWidth, 0.0f),
                juce::Justification::centred, true);

    // The mode badge: a small accent pill.
    const auto badge = layout.badge.toFloat();
    g.setColour (Palette::accentSoft);
    g.fillRoundedRectangle (badge, badge.getHeight() * 0.5f);
    g.setFont (Fonts::sans (10.0f * s, true, 0.14f));   // --tracking-label
    g.setColour (Palette::accent);
    g.drawText ("POLY", badge, juce::Justification::centred, false);

    // The gear: eight teeth round a ring, lit while its settings are open.
    const auto  gear   = layout.gear.toFloat();
    const float outer  = gear.getWidth() * 0.46f;
    const float inner  = gear.getWidth() * 0.30f;
    juce::Path teeth;
    for (int tooth = 0; tooth < 8; ++tooth)
    {
        teeth.addRectangle (-1.7f * s, -outer, 3.4f * s, outer - inner + 1.0f * s);
        teeth.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi / 8.0f));
    }
    teeth.applyTransform (juce::AffineTransform::translation (gear.getCentre()));
    g.setColour (settingsTray.isOpen() ? Palette::textStrong : Palette::textDim);
    g.fillPath (teeth);
    g.drawEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (gear.getCentre()), juce::jmax (1.0f, 2.2f * s));
}

void Synth2AudioProcessorEditor::paintOutput (juce::Graphics& g, float s) const
{
    // OUTPUT's name and value under its dial, and, when sound follows the
    // cables, the output's IN jack beside it.
    const auto column = outputDial.getBounds().toFloat().withSizeKeepingCentre (72.0f * s, (float) outputDial.getHeight());
    const float textTop = column.getBottom() + 6.0f * s;

    g.setFont (Fonts::sans (10.0f * s, true, 0.10f));
    g.setColour (Palette::textMid);
    g.drawText ("OUTPUT", juce::Rectangle (column.getX(), textTop, column.getWidth(), 12.0f * s), juce::Justification::centredTop, false);
    g.setFont (Fonts::mono (10.0f * s));
    g.setColour (BenchStyle::dialColour (1.0f));
    g.drawText (BenchStyle::dialText (ParamID::output, paramValue (processorRef, ParamID::output)),
                juce::Rectangle (column.getX(), textTop + 13.0f * s, column.getWidth(), 13.0f * s), juce::Justification::centredTop, false);

    if (signalFlow() == Synth2AudioProcessor::SignalFlow::cables)
    {
        const auto jack = outputJackBounds();
        BenchStyle::paintJack (g, jack, Palette::accent, ! model.patchedToOutput().empty(), false, s);
        g.setFont (Fonts::sans (8.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText ("IN", jack.translated (0.0f, jack.getHeight() + 2.0f * s).withHeight (10.0f * s), juce::Justification::centredTop, false);
    }
}

void Synth2AudioProcessorEditor::resized()
{
    // A fade still running (a tray opening) would put its component back
    // where it started: finish it before laying out afresh.
    auto& animator = juce::Desktop::getInstance().getAnimator();
    for (auto& menu : waveMenus)
        animator.cancelAnimation (&menu, true);
    for (auto* tray : { &addTray, &settingsTray, &presetTray })
        animator.cancelAnimation (tray, true);

    uiScale = (float) getWidth() / designWidth;
    const float s = uiScale;
    const auto px = [s] (float v) { return juce::roundToInt (v * s); };

    looks.setUiScale (s);
    keyboard.setUiScale (s);
    for (auto& menu : waveMenus)
    {
        menu.setUiScale (s);
        menu.setBounds (getLocalBounds());
    }
    for (auto* tray : { &addTray, &settingsTray, &presetTray })
    {
        tray->setUiScale (s);
        tray->setBounds (getLocalBounds());
    }

    auto area = getLocalBounds().reduced (px (kPanelPad));

    // Header: wordmark on the left; the badge and the gear on the right.
    layout.header = area.removeFromTop (px (20.0f));
    layout.gear   = layout.header.withTrimmedLeft (layout.header.getWidth() - px (22.0f)).withSizeKeepingCentre (px (22.0f), px (22.0f));
    layout.badge  = layout.header.withTrimmedLeft (layout.header.getWidth() - px (22.0f + 12.0f + 54.0f))
                                 .withWidth (px (54.0f)).withSizeKeepingCentre (px (54.0f), px (20.0f));
    layout.preset = layout.header.withSizeKeepingCentre (px (240.0f), px (26.0f));

    // The bench.
    bench.setUiScale (s);
    bench.setBounds (px (kPanelPad), px (kBenchTop), px (Bench::areaWidth), px (Bench::areaHeight));

    // Hairline, then the keyboard with OUTPUT (and, patching, its IN jack) at its right.
    area.setTop (bench.getBottom() + px (12.0f));
    layout.stripSeparatorY = area.getY();
    area.removeFromTop (px (14.0f));
    layout.outputColumn = area.removeFromRight (px (108.0f));
    outputDial.setBounds (layout.outputColumn.withTrimmedLeft (px (28.0f)).withHeight (px (52.0f))
                                             .withSizeKeepingCentre (px (52.0f), px (52.0f)));
    area.removeFromRight (px (12.0f));
    keyboard.setBounds (area);

    cables.setBounds (getLocalBounds());
    lastCables.clear();
}
