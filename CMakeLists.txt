cmake_minimum_required(VERSION 3.21)
project(loom VERSION 0.1.0 LANGUAGES CXX) # x-release-please-version

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Multimedia)
qt_standard_project_setup(REQUIRES 6.5)

find_package(tomlplusplus REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C REQUIRED IMPORTED_TARGET md4c md4c-html)

set(LOOM_SOURCES
    src/app/Application.cpp
    src/core/AtomicFile.cpp
    src/core/Buffer.cpp
    src/core/BufferManager.cpp
    src/core/LineDiff.cpp
    src/core/MarkdownExporter.cpp
    src/core/Paths.cpp
    src/core/SessionStore.cpp
    src/core/Settings.cpp
    src/core/SlashCommand.cpp
    src/markdown/CodeSyntax.cpp
    src/markdown/DocumentOutline.cpp
    src/markdown/MarkdownHighlighter.cpp
    src/markdown/MarkdownRules.cpp
    src/markdown/RevealController.cpp
    src/markdown/TableFormat.cpp
    src/theme/ChromeStyle.cpp
    src/theme/Fonts.cpp
    src/theme/OmarchyThemeSource.cpp
    src/theme/Theme.cpp
    src/theme/ThemeManager.cpp
    src/ui/CheatSheet.cpp
    src/ui/CrtWipe.cpp
    src/ui/Editor.cpp
    src/ui/FindBar.cpp
    src/ui/MainWindow.cpp
    src/ui/OutlineOverlay.cpp
    src/ui/SettingsDialog.cpp
    src/ui/StatusBar.cpp
    src/ui/TabStrip.cpp
    src/ui/ThemedDialogs.cpp
    src/ui/TabSwitcher.cpp
    src/ui/ThemeSwitcher.cpp
    src/ui/Weather.cpp
    src/ui/WeatherSound.cpp
)

add_library(loom_lib STATIC ${LOOM_SOURCES})
target_include_directories(loom_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(loom_lib PUBLIC
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    tomlplusplus::tomlplusplus
    PkgConfig::MD4C
)
target_compile_options(loom_lib PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(loom src/main.cpp)
qt_add_resources(loom loom_res
    PREFIX "/"
    BASE resources
    FILES
        resources/fonts/DepartureMono-Regular.otf
        resources/icons/loom.svg
)
qt_add_resources(loom loom_sounds
    PREFIX "/sounds"
    BASE sounds
    FILES
        sounds/rain_fx.wav
)
target_link_libraries(loom PRIVATE loom_lib)
set_target_properties(loom PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

install(TARGETS loom RUNTIME DESTINATION bin)
configure_file(resources/loom.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/loom.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/loom.desktop DESTINATION share/applications)
install(FILES resources/icons/loom.svg DESTINATION share/icons/hicolor/scalable/apps)

find_package(GTest REQUIRED)
enable_testing()
add_executable(loom_tests
    tests/main.cpp
    tests/markdown_rules_test.cpp
    tests/session_store_test.cpp
    tests/line_diff_test.cpp
    tests/slash_command_test.cpp
    tests/theme_parse_test.cpp
    tests/edit_history_test.cpp
    tests/document_outline_test.cpp
    tests/table_format_test.cpp
    tests/editor_table_test.cpp
    tests/cheat_sheet_test.cpp
)
target_link_libraries(loom_tests PRIVATE loom_lib GTest::gtest)
target_compile_options(loom_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME loom_tests COMMAND loom_tests)
