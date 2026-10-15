cmake_minimum_required(VERSION 3.19)
project(FileManager LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network LinguistTools)

qt_standard_project_setup()

set(CMAKE_AUTORCC ON)


qt_add_executable(FileManager
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    FileNode.h
    FileNode.cpp
    FileSystem.h
    FileSystem.cpp
    Operation.h
    Operation.cpp

    icons.qrc
    logindialog.h logindialog.cpp logindialog.ui
)

target_compile_features(FileManager PRIVATE cxx_std_17)

qt_add_translations(
    TARGETS FileManager
    TS_FILES FileManager_zh_CN.ts
)

target_link_libraries(FileManager
    PRIVATE
        Qt::Core
        Qt::Widgets Qt::Network
)

install(TARGETS FileManager
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET FileManager
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
# FileManager AES-256-GCM sources
target_sources(FileManager PRIVATE PrivacyCrypto.cpp PrivacyCrypto.h)

# FileManager AES-256-GCM crypto dependency
find_package(OpenSSL REQUIRED)
target_link_libraries(FileManager PRIVATE OpenSSL::Crypto)
target_sources(FileManager PRIVATE AdvancedTools.cpp AiTools.cpp AiSearchAgent.cpp AiSearchAgent.h)
# Compile the production AI settings into the application itself.
set(FILEMANAGER_AI_SETTINGS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/AiCredentialStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AiSettingsDialog.cpp"
)
target_sources(FileManager PRIVATE ${FILEMANAGER_AI_SETTINGS_SOURCES})
if(WIN32)
    target_link_libraries(FileManager PRIVATE advapi32)
endif()

# CTest normally defaults to ON; keep normal IDE builds application-only.
option(BUILD_TESTING "Build FileManager regression tests" OFF)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()