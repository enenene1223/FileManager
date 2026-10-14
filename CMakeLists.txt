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
target_sources(FileManager PRIVATE AdvancedTools.cpp AiTools.cpp)
qt_add_executable(acceptance_probe tests/acceptance_probe.cpp mainwindow.cpp mainwindow.h mainwindow.ui FileNode.cpp FileSystem.cpp Operation.cpp PrivacyCrypto.cpp AdvancedTools.cpp AiTools.cpp icons.qrc)
target_compile_features(acceptance_probe PRIVATE cxx_std_17)
target_include_directories(acceptance_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/acceptance_probe_autogen/include)
target_link_libraries(acceptance_probe PRIVATE Qt::Core Qt::Widgets Qt::Network OpenSSL::Crypto)
set_target_properties(acceptance_probe PROPERTIES AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_executable(core_regression tests/core_copy_move_test.cpp FileNode.cpp FileSystem.cpp Operation.cpp PrivacyCrypto.cpp)
target_compile_features(core_regression PRIVATE cxx_std_17)
target_link_libraries(core_regression PRIVATE OpenSSL::Crypto)
add_executable(crypto_regression tests/crypto_test.cpp PrivacyCrypto.cpp)
target_compile_features(crypto_regression PRIVATE cxx_std_17)
target_link_libraries(crypto_regression PRIVATE OpenSSL::Crypto)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/acceptance-data")
add_test(NAME core_operations COMMAND core_regression)
add_test(NAME aes_gcm COMMAND crypto_regression)
add_test(NAME ui_search_classification COMMAND acceptance_probe)
add_test(NAME release_startup COMMAND FileManager --smoke-test)
set_tests_properties(core_operations aes_gcm ui_search_classification release_startup PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/acceptance-data"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30)
