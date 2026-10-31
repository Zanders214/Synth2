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
        ChoiceTray::Choice { "Fixed order",   "ENV 1 shapes OSC 1, and so on" },
        ChoiceTray::Choice { "Follow layout", "Row by row: each ENV after its OSC" },
        ChoiceTray::Choice { "Patch cables",  "OSC to ENV to OUT, as patched" } };

    // The gear menu's view choices, in Bench::EnvelopeView's and
    // Bench::OscillatorView's order, after the signal-flow choices.
    const std::array<ChoiceTray::Choice, 3> envelopeViewChoices {
        ChoiceTray::Choice { "Knobs", "Just the dials, on a shorter card", true, false, "ENVELOPES" },
        ChoiceTray::Choice { "Graph", "A big graph to drag, without dials" },
        ChoiceTray::Choice { "Both",  "The graph to drag, over the dials" } };
    const std::array<ChoiceTray::Choice, 2> oscillatorViewChoices {
        ChoiceTray::Choice { "Full",    "The live wave over the dials", true, false, "OSCILLATORS" },
        ChoiceTray::Choice { "Compact", "Just the shape's icon and name" } };
    constexpr int firstEnvelopeView   = 3;
    constexpr int firstOscillatorView = firstEnvelopeView + 3;

    // How much of each end of the preset picker steps to the previous / next.
    constexpr float kPresetArrowWidth = 28.0f;
}

Synth2AudioProcessorEditor::Synth2AudioProcessorEditor (Synth2AudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      model (p.getState()),
      benchView (p.getState(), looks),
      effectsView (p.getState(), looks),
      keyboard (p.getKeyboardState())
{
    addAndMakeVisible (benchView);
    addChildComponent (effectsView);
    addAndMakeVisible (keyboard);
    setWantsKeyboardFocus (playsTypedKeys());
    keyboard.setShowsTypingKeys (playsTypedKeys());

    BenchStyle::setUpDial (outputDial, BenchStyle::dialColour (1.0f), looks, p.getState(), ParamID::output);
    outputDial.onValueChange = [this] { repaint (layout.outputColumn); };
    addAndMakeVisible (outputDial);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), ParamID::output, outputDial);

    addAndMakeVisible (cables);

    // A click on an empty spot offers modules to add there.
    benchView.onEmptyClick ([this] (juce::Point<float> benchAt, juce::Point<int> viewAt)
    {
        trays.openAdd (benchAt, viewAt + benchView.getPosition());
    });
    // And on the EFFECTS tab, offers the effects not yet in the chain.
    effectsView.onEmptyClick ([this] (juce::Point<float> rackAt, juce::Point<int> viewAt)
    {
        trays.openEffect (rackAt, viewAt + effectsView.getPosition());
    });

    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        auto& card = benchView.getOscillator (slot);
        auto& menu   = waveMenus[(size_t) slot];
        auto& picker = card.getPicker();
        auto& well   = card.getDisplay();

        // Each well opens its tray of shapes under it; hovering one previews
        // it in the well, picking one sets that oscillator's shape.
        picker.onOpenMenu ([this, &menu, &picker]
        {
            // The picker covers the well (or, compact, is the pill).
            const auto wellBounds = getLocalArea (&picker, picker.getLocalBounds());
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
        benchView.getModule (id).onJack ([this, id] (BenchModule::Jack which, const juce::MouseEvent& e, BenchModule::JackPhase phase)
        {
            cables.patch (id, which, e, phase);
        });

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setResizeLimits ((int) (designWidth * 0.75f), (int) (designHeight * 0.75f),
                     (int) (designWidth * 2.0f), (int) (designHeight * 2.0f));

    setSize ((int) designWidth, (int) designHeight);
    for (auto& menu : waveMenus)
        addChildComponent (menu);   // last, so the trays open over everything
    for (auto* tray : trays.all())
        addChildComponent (*tray);
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
    const auto& ids        = ParamID::envelope (envelope);
    const auto& times      = settings.envelopes[(size_t) envelope];

    const auto position = [&parameters] (const char* id) { return parameters.getParameter (id)->getValue(); };
    AmpDisplayState state;
    state.dials          = { position (ids.delay), position (ids.attack), position (ids.hold),
                             position (ids.decay), times.sustainLevel, position (ids.release),
                             times.attackCurve, times.decayCurve, times.releaseCurve };
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
// the cables, their jacks instead (see CableLayer::showOnModules).
void Synth2AudioProcessorEditor::showView()
{
    if (const auto view = model.view(); view != shown.view)
    {
        shown.view = view;
        benchView.setView (view);
    }
}

void Synth2AudioProcessorEditor::showFlow()
{
    const auto flow = signalFlow();
    const bool followLayout = flow == Synth2AudioProcessor::SignalFlow::layout;

    std::array<int, Bench::allModules.size()> places {};
    if (followLayout)
    {
        int place = 0;
        for (const auto& id : model.readingOrder())
        {
            const auto found = std::find (Bench::allModules.begin(), Bench::allModules.end(), id);
            places[(size_t) std::distance (Bench::allModules.begin(), found)] = ++place;
        }
    }
    for (size_t i = 0; i < Bench::allModules.size(); ++i)
        benchView.getModule (Bench::allModules[i]).getHeader().setOrder (places[i]);

    if (flow == Synth2AudioProcessor::SignalFlow::cables)
    {
        cables.showOnModules();
    }
    else
    {
        const auto shapedBy = model.envelopeAssignment (flow);
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
        {
            auto& osc = benchView.getOscillator (slot);
            osc.getHeader().setJacksShown (false, false);
            osc.setDimmed (false);
        }
        for (int e = 0; e < Bench::numEnvelopes; ++e)
        {
            const auto shaped = std::find (shapedBy.begin(), shapedBy.end(), e);
            auto& env = benchView.getEnvelope (e);
            env.getHeader().setJacksShown (false, false);
            env.getHeader().setTag (shaped != shapedBy.end() ? "> OSC " + juce::String (shaped - shapedBy.begin() + 1) : juce::String());
            env.setDimmed (shaped == shapedBy.end(), "Not shaping an oscillator: put one before it, or add the one with its number");
        }
    }

    if ((int) flow != shown.flow)
    {
        shown.flow = (int) flow;
        repaint();
    }
}

void Synth2AudioProcessorEditor::timerCallback()
{
    // The modules follow the parameters, however they change (dials, host
    // automation, a restored session), and the envelope the notes as they
    // play; each display repaints only when its state moved.
    benchView.sync();
    effectsView.sync();
    const auto settings = processorRef.currentSettings();
    effectsView.refresh (settings.effects);
    std::array<int, Bench::numOscillators> shapedBy {};
    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        benchView.getOscillator (slot).refresh (settings.oscillators[(size_t) slot]);
        shapedBy[(size_t) slot] = settings.oscillators[(size_t) slot].envelope;
    }
    for (int e = 0; e < Bench::numEnvelopes; ++e)
        benchView.getEnvelope (e).refresh (ampState (settings, e, shapedBy));
    showView();
    showFlow();

    if (const auto signature = cables.signature(); signature != shown.cables)
    {
        shown.cables = signature;
        cables.repaint();
        repaint (layout.outputColumn);
    }
    if (const int preset = processorRef.getCurrentProgram(); preset != shown.preset)
    {
        shown.preset = preset;
        repaint (layout.preset);
    }
    if (! juce::exactlyEqual (settings.outputDb, shown.output))
    {
        shown.output = settings.outputDb;
        repaint (layout.outputColumn);
    }

    processorRef.showHostNotesOnKeyboard();

    if (playsTypedKeys())
    {
        // Keys let go while another window had them never reach us: ask
        // which are still down. And with nothing in the window focused (it
        // just opened, or a tray just closed), take the keys.
        if (keyboard.holdsTypedNotes())
            keyboard.typedKeysChanged();
        if (const auto* peer = getPeer(); peer != nullptr && peer->isFocused() && isShowing()
                                              && juce::Component::getCurrentlyFocusedComponent() == nullptr)
            grabKeyboardFocus();
    }
}

// ---- The computer's keys -------------------------------------------------------------

bool Synth2AudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    return playsTypedKeys() && keyboard.typedKeyPressed (key);
}

bool Synth2AudioProcessorEditor::keyStateChanged (bool)
{
    return playsTypedKeys() && keyboard.typedKeysChanged();
}

// ---- Tabs ----------------------------------------------------------------------------

// The bench and the effects take turns over the keyboard; the cables and the
// output's IN jack belong to the bench.
void Synth2AudioProcessorEditor::showTab (Tab which)
{
    if (which == tab)
        return;
    tab = which;
    benchView.setVisible (tab == Tab::bench);
    effectsView.setVisible (tab == Tab::effects);
    cables.repaint();
    repaint();
}

// ---- Trays ---------------------------------------------------------------------------

Synth2AudioProcessorEditor::Trays::Trays (Synth2AudioProcessorEditor& ownerToUse) : owner (ownerToUse)
{
    addTray.onChoose ([this] (int choice)
    {
        owner.benchView.add (choice == 0 ? Bench::Kind::oscillator : Bench::Kind::envelope, addAt);
        owner.shown.cables.clear();
    });
    settingsTray.onChoose ([this] (int choice)
    {
        if (choice >= firstEnvelopeView)
        {
            auto view = owner.model.view();
            if (choice >= firstOscillatorView)
                view.oscillators = (Bench::OscillatorView) (choice - firstOscillatorView);
            else
                view.envelopes = (Bench::EnvelopeView) (choice - firstEnvelopeView);
            owner.model.setView (view);
            owner.showView();
            return;
        }
        auto* flow = owner.processorRef.getState().getParameter (ParamID::signalFlow);
        flow->beginChangeGesture();
        flow->setValueNotifyingHost (flow->convertTo0to1 ((float) choice));
        flow->endChangeGesture();
        owner.showFlow();
    });
    effectTray.onChoose ([this] (int choice) { owner.effectsView.add (Rack::allEffects[(size_t) choice], effectAt); });
    presetTray.onChoose ([this] (int choice) { owner.processorRef.loadPreset (choice); });
}

void Synth2AudioProcessorEditor::Trays::openAdd (juce::Point<float> benchAt, juce::Point<int> editorAt)
{
    addAt = benchAt;
    const int oscillatorsLeft = owner.model.left (Bench::Kind::oscillator);
    const int envelopesLeft   = owner.model.left (Bench::Kind::envelope);
    std::vector<ChoiceTray::Choice> choices {
        { "Oscillator", oscillatorsLeft > 0 ? juce::String (oscillatorsLeft) + " of 4 left: a shape, stacked and spread"
                                            : juce::String ("All four are on the bench"), oscillatorsLeft > 0 },
        { "Envelope",   envelopesLeft > 0 ? juce::String (envelopesLeft) + " of 4 left: one oscillator's attack and release"
                                          : juce::String ("All four are on the bench"), envelopesLeft > 0 } };
    addTray.setBounds (owner.getLocalBounds());
    addTray.open ("ADD TO THE BENCH", std::move (choices), editorAt);
}

void Synth2AudioProcessorEditor::Trays::openEffect (juce::Point<float> rackAt, juce::Point<int> editorAt)
{
    effectAt = rackAt;
    const Rack::Model rack (owner.processorRef.getState());
    std::vector<ChoiceTray::Choice> choices {
        { "Drive",  "Grit and warmth: WARM, TUBE or HARD" },
        { "Delay",  "Repeats, free or on the beat" },
        { "Reverb", "A room, from a small one to a hall" } };
    for (size_t i = 0; i < choices.size(); ++i)
        if (rack.isInChain (Rack::allEffects[i]))
        {
            choices[i].detail  = "Already in the chain";
            choices[i].enabled = false;
        }
    effectTray.setBounds (owner.getLocalBounds());
    effectTray.open ("ADD AN EFFECT", std::move (choices), editorAt);
}

void Synth2AudioProcessorEditor::Trays::openSettings()
{
    const auto view = owner.model.view();
    std::vector<ChoiceTray::Choice> choices (flowChoices.begin(), flowChoices.end());
    choices.insert (choices.end(), envelopeViewChoices.begin(), envelopeViewChoices.end());
    choices.insert (choices.end(), oscillatorViewChoices.begin(), oscillatorViewChoices.end());
    choices[(size_t) owner.signalFlow()].ticked = true;
    choices[(size_t) (firstEnvelopeView + (int) view.envelopes)].ticked     = true;
    choices[(size_t) (firstOscillatorView + (int) view.oscillators)].ticked = true;
    settingsTray.setBounds (owner.getLocalBounds());
    const auto gear = owner.layout.gear;
    settingsTray.open ("SIGNAL FLOW", std::move (choices),
                       { gear.getRight() - juce::roundToInt (260.0f * owner.uiScale), gear.getBottom() + juce::roundToInt (8.0f * owner.uiScale) });
    owner.repaint (owner.layout.gear);
}

void Synth2AudioProcessorEditor::Trays::openPresets()
{
    std::vector<ChoiceTray::Choice> choices;
    for (const auto& preset : Presets::factory)
        choices.push_back ({ preset.name, preset.detail });
    choices[(size_t) owner.processorRef.getCurrentProgram()].ticked = true;
    presetTray.setBounds (owner.getLocalBounds());
    const auto picker = owner.layout.preset;
    presetTray.open ("PRESETS", std::move (choices),
                     { picker.getCentreX() - juce::roundToInt (130.0f * owner.uiScale), picker.getBottom() + juce::roundToInt (8.0f * owner.uiScale) });
    owner.repaint (owner.layout.preset);
}

void Synth2AudioProcessorEditor::Trays::stepPreset (int delta)
{
    owner.processorRef.loadPreset ((owner.processorRef.getCurrentProgram() + delta + Presets::numPresets) % Presets::numPresets);
}

// ---- Cables ----------------------------------------------------------------------------

juce::Rectangle<float> Synth2AudioProcessorEditor::CableLayer::outputJackBounds() const noexcept
{
    const float s = owner.uiScale;
    const auto column = owner.layout.outputColumn.toFloat();
    return { column.getX() + 4.0f * s, column.getY() + 17.0f * s, 18.0f * s, 18.0f * s };
}

juce::Point<float> Synth2AudioProcessorEditor::CableLayer::jackCentre (const Bench::ModuleId& id, BenchModule::Jack which) const
{
    const auto& card = owner.benchView.getModule (id);
    return owner.getLocalPoint (&card, card.jackBounds (which).getCentre());
}

void Synth2AudioProcessorEditor::CableLayer::patch (const Bench::ModuleId& id, BenchModule::Jack which, const juce::MouseEvent& e, BenchModule::JackPhase phase)
{
    const auto at = e.getEventRelativeTo (&owner).position;
    if (phase == BenchModule::JackPhase::down)
    {
        // Picking up a patched jack unplugs its cable, to drop elsewhere: an
        // OUT jack's own, or the one coming into an envelope's IN (which
        // then hangs from the oscillator it came from).
        auto from = std::optional<Bench::ModuleId> (id);
        if (which == BenchModule::Jack::in)
            from = owner.model.inputOf (id);
        if (! from.has_value())
            return;
        owner.model.disconnect (*from);
        patching.from = from;
        patching.to   = at;
    }
    else if (phase == BenchModule::JackPhase::drag)
    {
        patching.to = at;
    }
    else
    {
        dropCable (at);
    }
    owner.showFlow();
    repaint();
    owner.repaint (owner.layout.outputColumn);
}

// A cable being dragged lets go at `at`: on the output's IN, or an
// envelope's IN (from an oscillator), it is patched there; anywhere else, it
// stays unplugged.
void Synth2AudioProcessorEditor::CableLayer::dropCable (juce::Point<float> at)
{
    if (! patching.from.has_value())
        return;
    const auto from  = *patching.from;
    const auto reach = 10.0f * owner.uiScale;
    patching.from.reset();

    if (outputJackBounds().expanded (reach).contains (at))
    {
        owner.model.connect (from);
        return;
    }
    for (int e = 0; e < Bench::numEnvelopes; ++e)
    {
        const auto& env = owner.benchView.getEnvelope (e);
        if (env.isVisible() && env.getHeader().isJackShown (BenchModule::Jack::in)
            && owner.getLocalArea (&env, env.jackBounds (BenchModule::Jack::in)).expanded (reach).contains (at))
        {
            owner.model.connect (from, env.getId());
            return;
        }
    }
}

juce::String Synth2AudioProcessorEditor::CableLayer::signature() const
{
    juce::String text (owner.signalFlow() == Synth2AudioProcessor::SignalFlow::cables ? "cables" : "none");
    for (const auto& [from, to] : owner.model.cables())
    {
        text << " " << from.toString() << "@" << jackCentre (from).toString();
        if (to.has_value())
            text << ">" << to->toString() << "@" << jackCentre (*to, BenchModule::Jack::in).toString();
    }
    return text;
}

void Synth2AudioProcessorEditor::CableLayer::paintCable (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour) const
{
    // A cable hangs: a curve that sags below both ends, more the longer it is.
    const float s   = owner.uiScale;
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

void Synth2AudioProcessorEditor::CableLayer::paint (juce::Graphics& g)
{
    if (owner.tab != Tab::bench || owner.signalFlow() != Synth2AudioProcessor::SignalFlow::cables)
        return;

    // Each cable in the colour of the module it comes from.
    const auto output = outputJackBounds().getCentre();
    for (const auto& [from, to] : owner.model.cables())
    {
        const auto& source = owner.benchView.getModule (from);
        if (! source.isVisible() || (to.has_value() && ! owner.benchView.getModule (*to).isVisible()))
            continue;
        const auto end = to.has_value() ? jackCentre (*to, BenchModule::Jack::in) : output;
        paintCable (g, jackCentre (from), end, source.getHeader().getJackColour());
    }

    if (patching.from.has_value())
        paintCable (g, jackCentre (*patching.from), patching.to, owner.benchView.getModule (*patching.from).getHeader().getJackColour().withAlpha (0.8f));
}

// With the cables: every module's jacks, lit where a cable is plugged in, and
// greyed out what isn't heard (an oscillator that doesn't reach the output,
// an envelope that shapes nothing or isn't patched to it).
void Synth2AudioProcessorEditor::CableLayer::showOnModules()
{
    const auto heard = owner.model.heardThroughCables();
    for (int slot = 0; slot < Bench::numOscillators; ++slot)
    {
        auto& osc = owner.benchView.getOscillator (slot);
        osc.getHeader().setJacksShown (true, false);
        osc.getHeader().setPlugged (owner.model.destinationOf (osc.getId()).has_value(), false);
        osc.setDimmed (! heard[(size_t) slot], "Not connected: patch a cable from OUT to an envelope or the output to hear it");
    }
    for (int e = 0; e < Bench::numEnvelopes; ++e)
    {
        auto& env = owner.benchView.getEnvelope (e);
        const bool fed  = owner.model.inputOf (env.getId()).has_value();
        const bool out  = owner.model.isPatchedToOutput (env.getId());
        env.getHeader().setJacksShown (true, true);
        env.getHeader().setPlugged (out, fed);
        env.getHeader().setTag ({});
        env.setDimmed (! fed || ! out, "Not connected: patch an oscillator into IN, and OUT to the output");
    }
}

void Synth2AudioProcessorEditor::CableLayer::pickUpFromOutput (juce::Point<float> at)
{
    // Grabbing the output's IN jack takes the last cable patched into it.
    if (owner.tab == Tab::bench && owner.signalFlow() == Synth2AudioProcessor::SignalFlow::cables && outputJackBounds().expanded (4.0f * owner.uiScale).contains (at))
    {
        const auto patched = owner.model.patchedToOutput();
        if (! patched.empty())
        {
            const auto from = patched.back();
            owner.model.disconnect (from);
            patching.from = from;
            patching.to   = at;
            owner.showFlow();
            repaint();
        }
    }
}

void Synth2AudioProcessorEditor::CableLayer::dragTo (juce::Point<float> at)
{
    if (! patching.from.has_value())
        return;
    patching.to = at;
    repaint();
}

void Synth2AudioProcessorEditor::CableLayer::letGo (juce::Point<float> at)
{
    if (! patching.from.has_value())
        return;
    dropCable (at);
    owner.showFlow();
    repaint();
    owner.repaint (owner.layout.outputColumn);
}

void Synth2AudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    for (const auto which : { Tab::bench, Tab::effects })
        if (tabBounds (which).contains (e.position))
        {
            showTab (which);
            return;
        }
    if (layout.gear.toFloat().contains (e.position))
    {
        trays.openSettings();
        return;
    }
    if (layout.preset.toFloat().contains (e.position))
    {
        if (const float arrow = kPresetArrowWidth * uiScale; e.position.x < (float) layout.preset.getX() + arrow)
            trays.stepPreset (-1);
        else if (e.position.x > (float) layout.preset.getRight() - arrow)
            trays.stepPreset (1);
        else
            trays.openPresets();
        return;
    }
    cables.pickUpFromOutput (e.position);
}

void Synth2AudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    cables.dragTo (e.position);
}

void Synth2AudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    cables.letGo (e.position);
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

    // The tabs: two halves of a pill, the one showing lit.
    const auto tabs = layout.benchTab.getUnion (layout.effectsTab).toFloat();
    g.setColour (Palette::idleFill);
    g.fillRoundedRectangle (tabs, tabs.getHeight() * 0.5f);
    g.setColour (Palette::hairline);
    g.drawRoundedRectangle (tabs, tabs.getHeight() * 0.5f, juce::jmax (1.0f, s));
    for (const auto which : { Tab::bench, Tab::effects })
    {
        const auto half    = tabBounds (which);
        const bool showing = which == tab;
        if (showing)
        {
            g.setColour (Palette::accentSoft);
            g.fillRoundedRectangle (half.reduced (2.0f * s), (half.getHeight() - 4.0f * s) * 0.5f);
        }
        g.setFont (Fonts::sans (10.0f * s, true, 0.14f));
        g.setColour (showing ? Palette::accent : Palette::textDim);
        g.drawText (which == Tab::bench ? "BENCH" : "EFFECTS", half, juce::Justification::centred, false);
    }

    // The preset picker: the loaded preset's name in a pill, with arrows to
    // step either way, lit while its tray is open.
    const auto picker = layout.preset.toFloat();
    g.setColour (trays.isPresetsOpen() ? Palette::idleFill.withMultipliedAlpha (1.6f) : Palette::idleFill);
    g.fillRoundedRectangle (picker, picker.getHeight() * 0.5f);
    g.setColour (trays.isPresetsOpen() ? Palette::strongLine : Palette::hairline);
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
    g.setColour (trays.isSettingsOpen() ? Palette::textStrong : Palette::textDim);
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

    if (tab == Tab::bench && signalFlow() == Synth2AudioProcessor::SignalFlow::cables)
    {
        const auto jack = cables.outputJackBounds();
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
    for (auto* tray : trays.all())
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
    for (auto* tray : trays.all())
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
    layout.benchTab   = layout.header.withX (layout.header.getX() + px (148.0f)).withWidth (px (84.0f))
                                     .withSizeKeepingCentre (px (84.0f), px (26.0f));
    layout.effectsTab = layout.benchTab.translated (px (84.0f), 0);

    // The bench, and in the same place the effects.
    benchView.setUiScale (s);
    benchView.setBounds (px (kPanelPad), px (kBenchTop), px (Bench::areaWidth), px (Bench::areaHeight));
    effectsView.setUiScale (s);
    effectsView.setBounds (benchView.getBounds());

    // Hairline, then the keyboard with OUTPUT (and, patching, its IN jack) at its right.
    area.setTop (benchView.getBottom() + px (12.0f));
    layout.stripSeparatorY = area.getY();
    area.removeFromTop (px (14.0f));
    layout.outputColumn = area.removeFromRight (px (108.0f));
    outputDial.setBounds (layout.outputColumn.withTrimmedLeft (px (28.0f)).withHeight (px (52.0f))
                                             .withSizeKeepingCentre (px (52.0f), px (52.0f)));
    area.removeFromRight (px (12.0f));
    keyboard.setBounds (area);

    cables.setBounds (getLocalBounds());
    shown.cables.clear();
}
