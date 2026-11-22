set(CYHWINFO_CYUI_SOURCE_DIR "" CACHE PATH "Path to the CyImgui_Interface checkout")
if(NOT CYHWINFO_CYUI_SOURCE_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CyImgui_Interface/include/cyui/Core.h")
    set(CYHWINFO_CYUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../CyImgui_Interface")
endif()

if(NOT TARGET CyUI::Core)
    if(NOT EXISTS "${CYHWINFO_CYUI_SOURCE_DIR}/include/cyui/Core.h")
        message(FATAL_ERROR "Set CYHWINFO_CYUI_SOURCE_DIR to the CyImgui_Interface checkout or provide CyUI::Core")
    endif()
    function(cyhwinfo_add_cyui)
        set(CYUI_BUILD_DEMO OFF)
        set(CYUI_BUILD_PORTABLE_DEMO OFF)
        set(CYUI_BUILD_MARKDOWN_DEMO OFF)
        set(CYUI_BUILD_GALLERY OFF)
        set(CYUI_BUILD_AUDIO OFF)
        set(CYUI_BUILD_TESTS OFF)
        set(CYUI_MODULES "")
        add_subdirectory("${CYHWINFO_CYUI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/cyui" EXCLUDE_FROM_ALL)
    endfunction()
    cyhwinfo_add_cyui()
endif()

find_package(Threads REQUIRED)
add_library(cyhwinfo_ui STATIC src/ui/monitor.cpp src/ui/dashboard.cpp src/ui/translations.cpp)
add_library(CyHWInfo::UI ALIAS cyhwinfo_ui)
set_target_properties(cyhwinfo_ui PROPERTIES EXPORT_NAME UI POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_link_libraries(cyhwinfo_ui PUBLIC cyhwinfo_core PRIVATE Threads::Threads "$<BUILD_INTERFACE:CyUI::Core>")
foreach(component IN ITEMS cpu gpu gpu_telemetry gpu_processes memory storage storage_health motherboard battery network sensors)
    if(TARGET cyhwinfo_${component})
        string(TOUPPER "${component}" upper)
        target_compile_definitions(cyhwinfo_ui PRIVATE CYHWINFO_UI_HAS_${upper})
        target_link_libraries(cyhwinfo_ui PRIVATE cyhwinfo_${component})
    endif()
endforeach()
if(MSVC)
    target_compile_options(cyhwinfo_ui PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(cyhwinfo_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS cyhwinfo_ui EXPORT CyHWInfoUITargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(EXPORT CyHWInfoUITargets NAMESPACE CyHWInfo:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyHWInfo")
install(FILES include/cyhwinfo/ui.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cyhwinfo")

if(CYHWINFO_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR "The CyHWInfo monitoring application uses Win32 and Direct3D 11")
    endif()
    if(NOT TARGET cyui_imgui)
        message(FATAL_ERROR "The standalone application needs CyUI's bundled ImGui and its matching backends")
    endif()
    if(NOT TARGET nlohmann_json::nlohmann_json)
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        FetchContent_Declare(nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
            URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        )
        FetchContent_MakeAvailable(nlohmann_json)
        install(FILES "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/licenses" RENAME nlohmann-json-LICENSE.txt)
    endif()
    set(imgui_backends "${CYHWINFO_CYUI_SOURCE_DIR}/third_party/imgui/backends")
    add_executable(cyhwinfo_monitor WIN32
        app/main_win32.cpp
        app/update_panel.cpp
        app/updates.cpp
        "${imgui_backends}/imgui_impl_win32.cpp"
        "${imgui_backends}/imgui_impl_dx11.cpp")
    set_target_properties(cyhwinfo_monitor PROPERTIES OUTPUT_NAME CyHWInfo)
    target_include_directories(cyhwinfo_monitor PRIVATE "${imgui_backends}")
    target_compile_definitions(cyhwinfo_monitor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
        CYHWINFO_APP_VERSION="${PROJECT_VERSION}")
    target_link_libraries(cyhwinfo_monitor PRIVATE CyHWInfo::UI CyUI::Core nlohmann_json::nlohmann_json
        d3d11 dxgi d3dcompiler dwmapi shell32 winhttp bcrypt ole32 advapi32)
    if(MSVC)
        target_compile_options(cyhwinfo_monitor PRIVATE /utf-8 /EHsc)
    endif()
    if(TARGET cyhwinfo_sensors)
        cyhwinfo_deploy_libre_hardware_monitor(cyhwinfo_monitor)
    endif()
    install(TARGETS cyhwinfo_monitor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Documentation/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/Documentation")
    install(FILES "${CYHWINFO_CYUI_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/licenses" RENAME CyUI-LICENSE.txt)
    install(FILES "${CYHWINFO_CYUI_SOURCE_DIR}/third_party/imgui/LICENSE.txt"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/licenses" RENAME Dear-ImGui-LICENSE.txt)
    install(FILES "${CYHWINFO_CYUI_SOURCE_DIR}/third_party/jetbrains-mono/OFL.txt"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/licenses" RENAME JetBrainsMono-OFL.txt)
endif()
