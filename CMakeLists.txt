cmake_minimum_required(VERSION 3.21)

project(jarvis VERSION 0.8.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Network Widgets)
qt_standard_project_setup(REQUIRES 6.4)

include(GNUInstallDirs)
configure_file(build_config.h.in build_config.h @ONLY)

function(jarvis_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 1. jarvis_sys — pure C11: reads /proc and /sys. No malloc, no globals.
# ---------------------------------------------------------------------------
add_library(jarvis_sys STATIC jv_sys.c jv_sys.h)
target_include_directories(jarvis_sys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(jarvis_sys PROPERTIES POSITION_INDEPENDENT_CODE ON)
jarvis_warnings(jarvis_sys)

# Diagnostic tool: prints a system snapshot without any Qt involved.
add_executable(jv_sysinfo jv_sysinfo_cli.c)
target_link_libraries(jv_sysinfo PRIVATE jarvis_sys)
jarvis_warnings(jv_sysinfo)

# ---------------------------------------------------------------------------
# 2. jarvis_core — headless C++ (Core + Network): rules, Claude client, router.
# ---------------------------------------------------------------------------
qt_add_library(jarvis_core STATIC
    language.h
    language.cpp
    memory_text.h
    memory_text.cpp
    knowledge_store.h
    knowledge_store.cpp
    activity_store.h
    curiosity.h
    curiosity.cpp
    activity_store.cpp
    skill_store.h
    skill_store.cpp
    learning_store.h
    learning_store.cpp
    chat_engine.h
    chat_engine.cpp
    system_info.h
    system_info.cpp
    config.h
    config.cpp
    claude_client.h
    claude_client.cpp
    assistant.h
    assistant.cpp
)
target_include_directories(jarvis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
qt_add_resources(jarvis_core bundled_skills PREFIX "/" FILES skills/philosophy.json skills/electronics.json skills/games.json skills/conversation.json skills/writing.json)
target_link_libraries(jarvis_core PUBLIC Qt6::Core Qt6::Network jarvis_sys)
jarvis_warnings(jarvis_core)

# Russian translation of the English source strings (C++ and QML). Only
# lrelease is needed (package qt6-l10n-tools); the .qm is embedded in the core
# library so the GUI and the daemon share it. Refresh the .ts after changing
# strings: lupdate -locations none -no-obsolete . -ts i18n/jarvis_ru.ts
find_program(JARVIS_LRELEASE NAMES lrelease lrelease-qt6
    HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}" /usr/lib/qt6/bin
    REQUIRED)
set(JARVIS_QM "${CMAKE_CURRENT_BINARY_DIR}/i18n/jarvis_ru.qm")
add_custom_command(OUTPUT "${JARVIS_QM}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/i18n"
    COMMAND "${JARVIS_LRELEASE}" -silent "${CMAKE_CURRENT_SOURCE_DIR}/i18n/jarvis_ru.ts" -qm "${JARVIS_QM}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/jarvis_ru.ts"
    VERBATIM)
set_source_files_properties("${JARVIS_QM}" PROPERTIES GENERATED TRUE)
qt_add_resources(jarvis_core translations PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}/i18n" FILES "${JARVIS_QM}")

# ---------------------------------------------------------------------------
# 3. jarvisd — the daemon: owns the engine, speaks D-Bus (org.jarvis.Daemon1).
# ---------------------------------------------------------------------------
qt_add_executable(jarvisd
    jarvisd_main.cpp
    daemon_service.h
    daemon_service.cpp
    activity_bridge.h
    activity_bridge.cpp
    runtime_paths.h
    dbus_names.h
)
target_link_libraries(jarvisd PRIVATE jarvis_core Qt6::DBus)
jarvis_warnings(jarvisd)

# ---------------------------------------------------------------------------
# 4. jarvis — Qt Quick GUI. Talks to jarvisd, falls back to a local engine.
# ---------------------------------------------------------------------------
qt_add_executable(jarvis main.cpp)

# Theme.qml is a QML singleton (Theme.accent, Theme.bg, ...).
set_source_files_properties(Theme.qml Texts.qml QuickCommands.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(jarvis
    URI Jarvis
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        Main.qml
        QuickBar.qml
        ControlCenter.qml
        SynapseGraph.qml
        MemoryPanel.qml
        Theme.qml
        Texts.qml
        QuickCommands.qml
        DarkButton.qml
        DarkMenuItem.qml
        SettingsDialog.qml
    SOURCES
        voice_controller.h
        voice_controller.cpp
        desktop_controller.h
        desktop_controller.cpp
        runtime_paths.h
        chat_model.h
        chat_model.cpp
        daemon_client.h
        daemon_client.cpp
        dbus_names.h
        app_settings.h
        app_settings.cpp
)

target_link_libraries(jarvis PRIVATE
    jarvis_core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
)
jarvis_warnings(jarvis)
qt_add_resources(jarvis app_icons PREFIX "/" FILES icons/org.jarvis.Jarvis.svg)

# ---------------------------------------------------------------------------
# Install: binaries, launcher, systemd user unit, D-Bus activation file.
# The unit/service files need the *install-time* prefix (e.g. ~/.local), so
# they are generated by install-time code, not at configure time.
# ---------------------------------------------------------------------------
install(TARGETS jarvis jarvisd jv_sysinfo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES scripts/voice.py scripts/update.py scripts/speak.py DESTINATION ${CMAKE_INSTALL_DATADIR}/jarvis/scripts)
install(FILES kwin/activity.js DESTINATION ${CMAKE_INSTALL_DATADIR}/jarvis/kwin)
install(FILES icons/org.jarvis.Jarvis.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES org.jarvis.Jarvis.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(CODE "set(JARVIS_SOURCE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")")
install(CODE "set(JARVIS_BINDIR_REL \"${CMAKE_INSTALL_BINDIR}\")")
install(CODE "set(JARVIS_DATADIR_REL \"${CMAKE_INSTALL_DATADIR}\")")
install(CODE [[
    set(JARVIS_BINDIR "${CMAKE_INSTALL_PREFIX}/${JARVIS_BINDIR_REL}")
    configure_file("${JARVIS_SOURCE_DIR}/jarvis.service.in"
        "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${JARVIS_DATADIR_REL}/systemd/user/jarvis.service" @ONLY)
    configure_file("${JARVIS_SOURCE_DIR}/org.jarvis.Daemon1.service.in"
        "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${JARVIS_DATADIR_REL}/dbus-1/services/org.jarvis.Daemon1.service" @ONLY)
]])

include(packaging/CPackConfig.cmake)

include(CTest)
if(BUILD_TESTING)
    add_executable(jarvis_core_tests tests/core_tests.cpp)
    target_link_libraries(jarvis_core_tests PRIVATE jarvis_core)
    add_test(NAME core COMMAND jarvis_core_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME helpers COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_helpers.py)
    add_test(NAME translations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_translations.py
        ${CMAKE_CURRENT_SOURCE_DIR}/i18n/jarvis_ru.ts)
endif()
