#include "PluginEditor.h"
#include "ParamIDs.h"
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
        ChoiceTray::Choice { "Fixed order",   "ENV 1 shapes OSC 1, and so on" },
        ChoiceTray::Choice { "Follow layout", "Row by row: each ENV after its OSC" },
        ChoiceTray::Choice { "Patch cables",  "OSC to ENV to OUT, as patched" } };
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
    }

    for (const auto& id : Bench::allModules)
        bench.getModule (id).onJack ([this, id] (BenchModule::Jack which, const juce::MouseEvent& e, int phase)
        {
            patch (id, which, e, phase);
        });

    addTray.onChoose ([this] (int choice)
    {
        bench.add (choice == 0 ? Bench::Kind::oscillator : Bench::Kind::envelope, addAt);
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

// Envelope `envelope`'s shape, and a dot for each note of the oscillator it
// shapes (none while it shapes nothing).
AmpDisplayState Synth2AudioProcessorEditor::ampState (const Synth2Engine::Settings& settings, int envelope,
                                                      const std::array<int, Bench::numOscillators>& shapedBy) const
{
    const auto& parameters = processorRef.getState();
    const auto& ids        = ParamID::envelopes[envelope];
    const auto& times      = settings.envelopes[(size_t) envelope];

    const auto position = [&parameters] (const char* id) { return parameters.getParameter (id)->getValue(); };
    AmpDisplayState state;
    state.dials          = { position (ids.delay), position (ids.attack), position (ids.hold),
                             position (ids.decay), times.sustainLevel, position (ids.release) };
    state.attackSeconds  = times.attackSeconds;
    state.releaseSeconds = times.releaseSeconds;
    state.outputDb       = settings.outputDb;

    static_assert (std::tuple_size_v<decltype (state.notes)> == Synth2Engine::maxNotes);
    for (int k = 0; k < Bench::numOscillators; ++k)
        if (shapedBy[(size_t) k] == envelope)
            state.notes = processorRef.getNoteActivity (k);
    return state;
}

Synth2AudioProcessor::SignalFlow Synth2AudioProcessorEditor::signalFlow() const
{
    return (Synth2AudioProcessor::SignalFlow) juce::jlimit (0, 2, juce::roundToInt (paramValue (processorRef, ParamID::signalFlow)));
}

// The modules' headers follow the signal flow: their place in the order when
// sound follows the layout, and which oscillator each envelope shapes; with
// the cables, their jacks instead (see showCables).
void Synth2AudioProcessorEditor::showFlow()
{
    const auto flow = signalFlow();
    const bool followLayout = flow == Synth2AudioProcessor::SignalFlow::layout;

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

    if (flow == Synth2AudioProcessor::SignalFlow::cables)
    {
        showCables();
    }
    else
    {
        const auto shapedBy = model.envelopeAssignment (flow);
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
        {
            auto& osc = bench.getOscillator (slot);
            osc.setJacksShown (false, false);
            osc.setDimmed (false);
        }
        for (int e = 0; e < Bench::numEnvelopes; ++e)
        {
            const auto shaped = std::find (shapedBy.begin(), shapedBy.end(), e);
            auto& env = bench.getEnvelope (e);
            env.setJacksShown (false, false);
            env.setTag (shaped != shapedBy.end() ? "> OSC " + juce::String (shaped - shapedBy.begin() + 1) : juce::String());
            env.setDimmed (shaped == shapedBy.end(), "Not shaping an oscillator: put one before it, or add the one with its number");
        }
    }

    if ((int) flow != lastFlow)
    {
        lastFlow = (int) flow;
        repaint();
    }
}

// With the cables: every module's jacks, lit where a cable is plugged in, and
// greyed out what isn't heard (an oscillator that doesn't reach the output,
// an envelope that shapes nothing or isn't patched to it).
void Synth2AudioProcessorEditor::showCables()
{
    const auto heard = model.heardThroughCables();
    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        auto& osc = bench.getOscillator (slot);
        osc.setJacksShown (true, false);
        osc.setPlugged (model.destinationOf (osc.getId()).has_value(), false);
        osc.setDimmed (! heard[(size_t) slot], "Not connected: patch a cable from OUT to an envelope or the output to hear it");
    }
    for (int e = 0; e < Bench::numEnvelopes; ++e)
    {
        auto& env = bench.getEnvelope (e);
        const bool fed  = model.inputOf (env.getId()).has_value();
        const bool out  = model.isPatchedToOutput (env.getId());
        env.setJacksShown (true, true);
        env.setPlugged (out, fed);
        env.setTag ({});
        env.setDimmed (! fed || ! out, "Not connected: patch an oscillator into IN, and OUT to the output");
    }
}

void Synth2AudioProcessorEditor::timerCallback()
{
    // The modules follow the parameters, however they change (dials, host
    // automation, a restored session), and the envelope the notes as they
    // play; each display repaints only when its state moved.
    bench.sync();
    const auto settings = processorRef.currentSettings();
    std::array<int, Bench::numOscillators> shapedBy {};
    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        bench.getOscillator (slot).refresh (settings.oscillators[(size_t) slot]);
        shapedBy[(size_t) slot] = settings.oscillators[(size_t) slot].envelope;
    }
    for (int e = 0; e < Bench::numEnvelopes; ++e)
        bench.getEnvelope (e).refresh (ampState (settings, e, shapedBy));
    showFlow();

    if (const auto signature = cableSignature(); signature != lastCables)
    {
        lastCables = signature;
        cables.repaint();
        repaint (layout.outputColumn);
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
    const int oscillatorsLeft = model.left (Bench::Kind::oscillator);
    const int envelopesLeft   = model.left (Bench::Kind::envelope);
    std::vector<ChoiceTray::Choice> choices {
        { "Oscillator", oscillatorsLeft > 0 ? juce::String (oscillatorsLeft) + " of 4 left: a shape, stacked and spread"
                                            : juce::String ("All four are on the bench"), oscillatorsLeft > 0 },
        { "Envelope",   envelopesLeft > 0 ? juce::String (envelopesLeft) + " of 4 left: one oscillator's attack and release"
                                          : juce::String ("All four are on the bench"), envelopesLeft > 0 } };
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

// ---- Cables ----------------------------------------------------------------------------

juce::Rectangle<float> Synth2AudioProcessorEditor::outputJackBounds() const noexcept
{
    const float s = uiScale;
    const auto column = layout.outputColumn.toFloat();
    return { column.getX() + 4.0f * s, column.getY() + 17.0f * s, 18.0f * s, 18.0f * s };
}

juce::Point<float> Synth2AudioProcessorEditor::jackCentre (const Bench::ModuleId& id, BenchModule::Jack which)
{
    auto& module = bench.getModule (id);
    return getLocalPoint (&module, module.jackBounds (which).getCentre());
}

void Synth2AudioProcessorEditor::patch (const Bench::ModuleId& module, BenchModule::Jack which, const juce::MouseEvent& e, int phase)
{
    const auto at = e.getEventRelativeTo (this).position;
    if (phase == BenchModule::jackDown)
    {
        // Picking up a patched jack unplugs its cable, to drop elsewhere: an
        // OUT jack's own, or the one coming into an envelope's IN (which
        // then hangs from the oscillator it came from).
        auto from = std::optional<Bench::ModuleId> (module);
        if (which == BenchModule::Jack::in)
            from = model.inputOf (module);
        if (! from.has_value())
            return;
        model.disconnect (*from);
        patchingFrom = from;
        patchingTo   = at;
    }
    else if (phase == BenchModule::jackDrag)
    {
        patchingTo = at;
    }
    else
    {
        dropCable (at);
    }
    showFlow();
    cables.repaint();
    repaint (layout.outputColumn);
}

// A cable being dragged lets go at `at`: on the output's IN, or an
// envelope's IN (from an oscillator), it is patched there; anywhere else, it
// stays unplugged.
void Synth2AudioProcessorEditor::dropCable (juce::Point<float> at)
{
    if (! patchingFrom.has_value())
        return;
    const auto from  = *patchingFrom;
    const auto reach = 10.0f * uiScale;
    patchingFrom.reset();

    if (outputJackBounds().expanded (reach).contains (at))
    {
        model.connect (from);
        return;
    }
    for (int e = 0; e < Bench::numEnvelopes; ++e)
    {
        auto& env = bench.getEnvelope (e);
        if (env.isVisible() && env.isJackShown (BenchModule::Jack::in)
            && getLocalArea (&env, env.jackBounds (BenchModule::Jack::in)).expanded (reach).contains (at))
        {
            model.connect (from, env.getId());
            return;
        }
    }
}

void Synth2AudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (layout.gear.toFloat().contains (e.position))
    {
        openSettings();
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
    dropCable (e.position);
    showFlow();
    cables.repaint();
    repaint (layout.outputColumn);
}

juce::String Synth2AudioProcessorEditor::cableSignature()
{
    juce::String signature (signalFlow() == Synth2AudioProcessor::SignalFlow::cables ? "cables" : "none");
    for (const auto& [from, to] : model.cables())
    {
        signature << " " << from.toString() << "@" << jackCentre (from).toString();
        if (to.has_value())
            signature << ">" << to->toString() << "@" << jackCentre (*to, BenchModule::Jack::in).toString();
    }
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

    // Each cable in the colour of the module it comes from.
    const auto output = outputJackBounds().getCentre();
    for (const auto& [from, to] : model.cables())
    {
        auto& source = bench.getModule (from);
        if (! source.isVisible() || (to.has_value() && ! bench.getModule (*to).isVisible()))
            continue;
        const auto end = to.has_value() ? jackCentre (*to, BenchModule::Jack::in) : output;
        paintCable (g, jackCentre (from), end, source.getJackColour());
    }

    if (patchingFrom.has_value())
        paintCable (g, jackCentre (*patchingFrom), patchingTo, bench.getModule (*patchingFrom).getJackColour().withAlpha (0.8f));
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
    for (auto* tray : { &addTray, &settingsTray })
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
    for (auto* tray : { &addTray, &settingsTray })
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
