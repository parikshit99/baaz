cmake_minimum_required(VERSION 3.20)
project(Baaz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Baaz's own code builds warning-free at /W4; DevKit headers are SYSTEM (external)
    add_compile_options(/W4 /external:W0)
    option(BAAZ_WARNINGS_AS_ERRORS "Fail the build on compiler warnings (on in CI)" OFF)
    if(BAAZ_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
endif()

set(BAAZ_AC_VERSION 30 CACHE STRING "Archicad version the add-on is built for (29 or 30)")
set(ARCHICAD_DEVKIT_DIR "$ENV{ARCHICAD_DEVKIT_DIR}" CACHE PATH "Archicad ${BAAZ_AC_VERSION} DevKit folder (the one that contains Support/Inc)")

# Sources that build both inside Archicad and, without the API, in the tests
set(BAAZ_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/addon/src/BinSchemeReader.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/CommandExecutor.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/CommandHint.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/CommandSource.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/HudWindow.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/MenuLink.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/MessageHook.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/NavigatorIndexer.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/SchemeReader.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/SearchEngine.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/Settings.cpp
    ${CMAKE_SOURCE_DIR}/addon/src/SettingsDialog.cpp
)
set(BAAZ_WIN32_LIBS user32 gdi32 gdiplus comctl32)

enable_testing()

if(EXISTS "${ARCHICAD_DEVKIT_DIR}/Support/Inc/ACAPinc.h")
    add_subdirectory(addon)
else()
    message(WARNING "ARCHICAD_DEVKIT_DIR is not set to an Archicad ${BAAZ_AC_VERSION} DevKit; building the tests only")
endif()
add_subdirectory(tests)
