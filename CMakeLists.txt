cmake_minimum_required(VERSION 3.22)

project(Synth2 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE acquisition
#
# Default: pull a pinned JUCE release via FetchContent (single source of truth,
# no submodules to keep in sync).
#
# Override: pass -DJUCE_PATH=/path/to/JUCE to use a local/vendored checkout
# instead. Useful in locked-down CI / containers where cloning from GitHub is
# blocked by network policy.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    message(STATUS "Synth2: using vendored JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Synth2
    COMPANY_NAME                "ZandersAudio"
    BUNDLE_ID                   com.zandersaudio.synth2
    PLUGIN_MANUFACTURER_CODE    Zand
    PLUGIN_CODE                 Syn2
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "ZandersSynth2"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

juce_generate_juce_header(Synth2)

# Plugin translation units, shared with the headless console drivers (rt_check /
# perf_bench) so they exercise the exact same code the host runs. The editor
# sources are included because PluginProcessor::createEditor() references them at
# link time.
set(SYNTH2_APP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Synth2LookAndFeel.cpp)

target_sources(Synth2 PRIVATE ${SYNTH2_APP_SOURCES})

target_compile_definitions(Synth2
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Synth2
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Unit tests for the DSP core (Synth2Engine). Headless and framework-free,
# so they run on every platform without a display. Run with: ctest --test-dir build
# ---------------------------------------------------------------------------
option(SYNTH2_BUILD_TESTS "Build Synth2Engine unit tests" ON)
option(SYNTH2_ENABLE_COVERAGE "Instrument the unit tests for gcov/llvm coverage" OFF)

# Opt-in performance / real-time-safety targets. OFF by default so normal and
# release builds stay lean; CI flips them on for the dedicated jobs.
option(SYNTH2_RT_SANITIZE "Build rt_check with clang RealtimeSanitizer (needs clang>=20)" OFF)
option(SYNTH2_BUILD_BENCH "Build the perf_bench micro-benchmark (fetches nanobench)"        OFF)

if(SYNTH2_BUILD_TESTS)
    enable_testing()

    add_executable(Synth2Tests Tests/Synth2EngineTests.cpp)
    target_include_directories(Synth2Tests PRIVATE Source)
    target_compile_definitions(Synth2Tests PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)
    target_link_libraries(Synth2Tests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Coverage instrumentation for the DSP core, fed to SonarQube from CI. Only
    # the test target is instrumented, and -O0 keeps gcov's line mapping honest.
    if(SYNTH2_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(Synth2Tests PRIVATE --coverage -O0 -g)
        target_link_options(Synth2Tests PRIVATE --coverage)
    endif()

    add_test(NAME Synth2Engine COMMAND Synth2Tests)

    # The oscillator shapes and the unison spread are pure and framework-free,
    # so they get their own tiny test target with no JUCE dependency at all.
    add_executable(Synth2VisualModelTests Tests/Synth2VisualModelTests.cpp)
    target_include_directories(Synth2VisualModelTests PRIVATE Source)

    if(SYNTH2_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(Synth2VisualModelTests PRIVATE --coverage -O0 -g)
        target_link_options(Synth2VisualModelTests PRIVATE --coverage)
    endif()

    add_test(NAME Synth2VisualModel COMMAND Synth2VisualModelTests)

    # Host-facing wrapper + editor: the real processor and UI, rendered offscreen.
    # Built from the same translation units as the plugin, like rt_check below.
    juce_add_console_app(Synth2PluginTests PRODUCT_NAME "Synth2PluginTests")
    juce_generate_juce_header(Synth2PluginTests)
    target_sources(Synth2PluginTests PRIVATE Tests/PluginTests.cpp ${SYNTH2_APP_SOURCES})
    target_include_directories(Synth2PluginTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions(Synth2PluginTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1        # lets the test pump the message loop
        JucePlugin_Name="ZandersSynth2")
    target_link_libraries(Synth2PluginTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    if(SYNTH2_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(Synth2PluginTests PRIVATE --coverage -O0 -g)
        target_link_options(Synth2PluginTests PRIVATE --coverage)
    endif()

    # JUCE's GUI layer needs an X display on Linux; CI has no screen, so run the
    # editor tests inside a virtual one when xvfb-run is available.
    set(SYNTH2_GUI_TEST_LAUNCHER "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(XVFB_RUN xvfb-run)
        if(XVFB_RUN)
            set(SYNTH2_GUI_TEST_LAUNCHER ${XVFB_RUN} -a)
        endif()
    endif()
    add_test(NAME Synth2Plugin COMMAND ${SYNTH2_GUI_TEST_LAUNCHER} $<TARGET_FILE:Synth2PluginTests>)
endif()

# ---------------------------------------------------------------------------
# Real-time-safety check (clang RealtimeSanitizer). A headless console app that
# runs the real audio callback under -fsanitize=realtime so any malloc / lock /
# syscall on the audio thread fails the build. Needs clang >= 20; CI runs it on
# Linux only. Defining SYNTH2_RT_NONBLOCKING as the clang attribute here is what
# arms the [[clang::nonblocking]] annotation on Synth2Engine::process.
# ---------------------------------------------------------------------------
if(SYNTH2_RT_SANITIZE)
    juce_add_console_app(rt_check PRODUCT_NAME "rt_check")
    juce_generate_juce_header(rt_check)
    set_target_properties(rt_check PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources(rt_check PRIVATE Tests/rt_check.cpp ${SYNTH2_APP_SOURCES})
    target_include_directories(rt_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions(rt_check PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="ZandersSynth2"
        "SYNTH2_RT_NONBLOCKING=[[clang::nonblocking]]")   # CMake passes brackets fine inside quotes
    target_compile_options(rt_check PRIVATE -fsanitize=realtime)
    target_link_options   (rt_check PRIVATE -fsanitize=realtime)
    target_link_libraries(rt_check PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Performance micro-benchmark (nanobench). A headless console app that measures
# the per-block cost of a full chord of unison voices and writes a
# github-action-benchmark JSON file for the perf workflow's trend dashboard.
# ---------------------------------------------------------------------------
if(SYNTH2_BUILD_BENCH)
    include(FetchContent)
    FetchContent_Declare(nanobench
        GIT_REPOSITORY https://github.com/martinus/nanobench.git
        GIT_TAG        v4.3.11
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(nanobench)

    juce_add_console_app(perf_bench PRODUCT_NAME "perf_bench")
    juce_generate_juce_header(perf_bench)
    set_target_properties(perf_bench PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources(perf_bench PRIVATE Tests/perf_bench.cpp ${SYNTH2_APP_SOURCES})
    target_include_directories(perf_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${nanobench_SOURCE_DIR}/src/include)   # we define ANKERL_NANOBENCH_IMPLEMENT ourselves
    target_compile_definitions(perf_bench PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="ZandersSynth2")
    target_link_libraries(perf_bench PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
