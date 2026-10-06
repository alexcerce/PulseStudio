cmake_minimum_required(VERSION 3.22)

project(PulseStudio VERSION 0.99.39 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the .exe runs without the VC++ redistributable.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

# JUCE + Tracktion Engine (git submodule-style checkout in external/)
add_subdirectory(external/tracktion_engine/modules/juce)
# ARA SDK (Melodyne and other ARA plugins on audio clips)
juce_set_ara_sdk_path (${CMAKE_CURRENT_SOURCE_DIR}/external/ARA_SDK)
add_subdirectory(external/tracktion_engine/modules)

# libusb (static) - drives the Ableton Push 2 display over USB
set(LIBUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/libusb)
add_library(pulse_libusb STATIC
    ${LIBUSB_DIR}/libusb/core.c ${LIBUSB_DIR}/libusb/descriptor.c ${LIBUSB_DIR}/libusb/hotplug.c
    ${LIBUSB_DIR}/libusb/io.c ${LIBUSB_DIR}/libusb/strerror.c ${LIBUSB_DIR}/libusb/sync.c
    ${LIBUSB_DIR}/libusb/os/events_windows.c ${LIBUSB_DIR}/libusb/os/threads_windows.c
    ${LIBUSB_DIR}/libusb/os/windows_common.c ${LIBUSB_DIR}/libusb/os/windows_usbdk.c
    ${LIBUSB_DIR}/libusb/os/windows_winusb.c)
target_include_directories(pulse_libusb PUBLIC ${LIBUSB_DIR}/libusb PRIVATE ${LIBUSB_DIR}/msvc)

juce_add_gui_app(PulseStudio
    PRODUCT_NAME "Pulse Studio"
    COMPANY_NAME "Pulse"
    BUNDLE_ID "com.pulse.studio"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Pulse Studio records audio from your audio interface."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(PulseStudio)

# All app code is header-only and compiled through Main.cpp.
file(GLOB PULSE_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(PulseStudio PRIVATE Source/Main.cpp Source/Push2Usb.cpp Source/CrashGuardWin.cpp Source/Mp3EncoderWin.cpp Source/VideoPlayerWin.cpp ${PULSE_HEADERS})

target_compile_definitions(PulseStudio PRIVATE
    # Audio devices: ASIO for pro interfaces, WASAPI/DirectSound as fallbacks
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    # Plugin hosting
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_ARA=1
    TRACKTION_ENABLE_ARA=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PulseStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PulseStudio,JUCE_VERSION>"
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1
    # Ableton Link (tempo / phase / start-stop sync over the network)
    TRACKTION_ENABLE_ABLETON_LINK=1)

target_include_directories(PulseStudio PRIVATE
    external/ARA_SDK
    external/link/include
    external/link/modules/asio-standalone/asio/include)

target_link_libraries(PulseStudio PRIVATE
    pulse_libusb
    ws2_32 iphlpapi winmm
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

if (MSVC)
    target_compile_options(PulseStudio PRIVATE /bigobj /MP)
    # Symbols for release builds (a .pdb next to the build, never installed): crash reports can name functions.
    target_compile_options(PulseStudio PRIVATE /Zi)
    target_link_options(PulseStudio PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()
