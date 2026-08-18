cmake_minimum_required(VERSION 3.20)

project(lw_Web2Android VERSION 0.1.0 LANGUAGES CXX)

add_library(lw_packer_core
    core/ApkAssembler.cpp
    core/BuildPipeline.cpp
    core/Generators.cpp
    core/Hash.cpp
    core/Json.cpp
    core/ProcessRunner.cpp
    core/ProjectConfig.cpp
    core/ProjectValidator.cpp
    core/ReleaseMetadata.cpp
    core/SigningKeyManager.cpp
    core/Toolchain.cpp
)

target_include_directories(lw_packer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lw_packer_core PUBLIC cxx_std_17)
target_compile_definitions(lw_packer_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

if(WIN32)
    target_link_libraries(lw_packer_core PRIVATE bcrypt crypt32 ncrypt)
endif()

if(MSVC)
    target_compile_options(lw_packer_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(lw.Web2Android main.cpp)
target_link_libraries(lw.Web2Android PRIVATE lw_packer_core)
target_compile_definitions(lw.Web2Android PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_library(lw_packer_gui_model gui/GuiProjectModel.cpp)
target_link_libraries(lw_packer_gui_model PUBLIC lw_packer_core)
target_include_directories(lw_packer_gui_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lw_packer_gui_model PUBLIC cxx_std_17)

if(WIN32)
    add_executable(lw.Web2Android.GUI WIN32 gui/GuiMain.cpp)
    target_link_libraries(lw.Web2Android.GUI PRIVATE lw_packer_gui_model comctl32 dwmapi ole32 shell32 uxtheme)
    target_compile_definitions(lw.Web2Android.GUI PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
    target_compile_options(lw.Web2Android PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(lw_packer_gui_model PRIVATE /W4 /permissive- /utf-8 /EHsc)
    if(WIN32)
        target_compile_options(lw.Web2Android.GUI PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(lw_packer_tests tests/PackerTests.cpp)
    target_link_libraries(lw_packer_tests PRIVATE lw_packer_gui_model)
    target_compile_definitions(lw_packer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(lw_packer_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME lw_packer_tests COMMAND lw_packer_tests)
endif()
