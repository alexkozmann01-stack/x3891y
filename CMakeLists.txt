cmake_minimum_required(VERSION 3.20)
project(NasakiClient CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Nasaki client targets Windows (Win32 + Direct3D 11) only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Source files are UTF-8 (Slovak UI strings with diacritics) — without
    # this, MSVC decodes narrow string literals using the system code page
    # and diacritics come out mangled.
    add_compile_options(/utf-8)
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_executable(NasakiClient WIN32
    src/main.cpp
    src/App.cpp
    src/Theme.cpp
    src/UI.cpp
    src/ApiClient.cpp
    src/ApiWorker.cpp
    src/SystemStats.cpp
    src/SystemInfo.cpp
    src/ProcessBoost.cpp
    src/Autostart.cpp
    src/WinTweaks.cpp
    src/GameLibrary.cpp

    src/optim/RegistryValue.cpp
    src/optim/RegistryOptimization.cpp
    src/optim/BackupStore.cpp
    src/optim/Catalog.cpp
    src/optim/OptimizationService.cpp
    src/LicenseStore.cpp

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(NasakiClient PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    third_party/json
)

target_link_libraries(NasakiClient PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    winhttp.lib
    pdh.lib
    dwmapi.lib
    advapi32.lib
    powrprof.lib  # PowerGet/SetActiveScheme
    winmm.lib     # timeBeginPeriod
)

target_compile_definitions(NasakiClient PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# NasakiClient.exe is fully self-contained — both UI fonts are compiled in
# (src/ManropeFont.h, src/UnboundedFont.h), so nothing needs to be copied
# next to the binary after building.

# Tests for the optimization framework. Separate executable so the app has no
# test dependency: cmake --build build --config Release --target NasakiTests
add_executable(NasakiTests
    tests/OptimTests.cpp
    src/optim/RegistryValue.cpp
    src/optim/RegistryOptimization.cpp
    src/optim/BackupStore.cpp
)
target_include_directories(NasakiTests PRIVATE src third_party/json)
target_link_libraries(NasakiTests PRIVATE advapi32.lib shell32.lib)
target_compile_definitions(NasakiTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
