cmake_minimum_required(VERSION 3.20)

project(lw_Web2Android VERSION 0.2.8 LANGUAGES CXX)

if(WIN32)
    function(lw_add_version_resource target file_description internal_name original_filename)
        set(LW_VERSION_FILE_DESCRIPTION "${file_description}")
        set(LW_VERSION_INTERNAL_NAME "${internal_name}")
        set(LW_VERSION_ORIGINAL_FILENAME "${original_filename}")
        set(LW_VERSION_RESOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.version.rc")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/windows/version.rc.in"
            "${LW_VERSION_RESOURCE}"
            @ONLY
        )
        target_sources(${target} PRIVATE "${LW_VERSION_RESOURCE}")
    endfunction()
endif()

include(FetchContent)

set(LW_WEB2ANDROID_DEPS_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/../.deps" CACHE PATH
    "Directory containing optional pinned dependency archives")
if(EXISTS "${LW_WEB2ANDROID_DEPS_CACHE}/spdlog.tar.gz")
    set(LW_WEB2ANDROID_SPDLOG_URL "${LW_WEB2ANDROID_DEPS_CACHE}/spdlog.tar.gz")
else()
    set(LW_WEB2ANDROID_SPDLOG_URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz")
endif()
FetchContent_Declare(spdlog
    URL "${LW_WEB2ANDROID_SPDLOG_URL}"
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(spdlog)

add_library(lw_packer_core
    core/ApkAssembler.cpp
    core/BuildPipeline.cpp
    core/Generators.cpp
    core/Hash.cpp
    core/IconGenerator.cpp
    core/Json.cpp
    core/Logging.cpp
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
target_link_libraries(lw_packer_core PRIVATE spdlog::spdlog)

if(WIN32)
    target_link_libraries(lw_packer_core PRIVATE bcrypt crypt32 ncrypt ole32 windowscodecs)
endif()

if(MSVC)
    target_compile_options(lw_packer_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(lw.Web2Android main.cpp)
target_link_libraries(lw.Web2Android PRIVATE lw_packer_core)
target_compile_definitions(lw.Web2Android PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
    LW_WEB2ANDROID_VERSION="${PROJECT_VERSION}"
)
if(WIN32)
    lw_add_version_resource(
        lw.Web2Android
        "lw.Web2Android 命令行打包工具"
        "lw.Web2Android"
        "lw.Web2Android.exe"
    )
endif()

add_library(lw_packer_gui_model gui/GuiProjectModel.cpp)
target_link_libraries(lw_packer_gui_model PUBLIC lw_packer_core)
target_include_directories(lw_packer_gui_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lw_packer_gui_model PUBLIC cxx_std_17)

if(WIN32)
    add_executable(lw.Web2Android.GUI WIN32 gui/GuiMain.cpp)
    target_link_libraries(lw.Web2Android.GUI PRIVATE lw_packer_gui_model comctl32 dwmapi ole32 shell32 uxtheme)
    target_compile_definitions(lw.Web2Android.GUI PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
        LW_WEB2ANDROID_VERSION="${PROJECT_VERSION}"
    )
    lw_add_version_resource(
        lw.Web2Android.GUI
        "lw.Web2Android 图形化打包工具"
        "lw.Web2Android.GUI"
        "lw.Web2Android.GUI.exe"
    )
    set(LW_SPONSOR_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/sponsor.jpg")
    set(LW_GUI_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/lw.Web2Android.GUI.resources.rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/gui-resources.rc.in"
        "${LW_GUI_RESOURCE}"
        @ONLY
    )
    target_sources(lw.Web2Android.GUI PRIVATE "${LW_GUI_RESOURCE}")
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
    add_test(NAME lw_packer_generator_tests COMMAND lw_packer_tests --generators-only)
    add_test(NAME lw_packer_logging_tests COMMAND lw_packer_tests --logging-only)
endif()
