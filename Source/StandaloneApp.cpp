#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

/**
    Synth2StandaloneApp — the standalone app: JUCE's own (StandaloneFilterApp),
    but listening to every MIDI keyboard plugged in, without a trip to the
    audio settings first. On a desktop JUCE leaves MIDI inputs off until they
    are ticked there; this app opens them all, and any plugged in later
    (StandalonePluginHolder looks for new ones twice a second).

    The computer's keys play the synth too: see Synth2AudioProcessorEditor.
*/
class Synth2StandaloneApp final : public juce::JUCEApplication
{
public:
    Synth2StandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                 { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        static constexpr bool openMidiInputs = true;
        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String {},
                                                                      nullptr, juce::Array<juce::StandalonePluginHolder::PluginInOuts> {},
                                                                      openMidiInputs);
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            pluginHolder = std::move (holder);   // no screen: play on, without a window
            return;
        }

        const auto background = juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (getApplicationName(), background, std::move (holder));
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow   = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        // A dialog (the audio settings) is still open: close it, then quit.
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        else
            quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow>   mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder>   pluginHolder;
};

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new Synth2StandaloneApp();   // NOSONAR(cpp:S5025): JUCE's entry point takes ownership of a raw pointer
}

#endif
