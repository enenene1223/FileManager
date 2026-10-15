# All test executables are opt-in through the root BUILD_TESTING option.
set(app_source_dir "${PROJECT_SOURCE_DIR}")
qt_add_executable(acceptance_probe
    acceptance_probe.cpp
    "${app_source_dir}/mainwindow.cpp"
    "${app_source_dir}/mainwindow.h"
    "${app_source_dir}/mainwindow.ui"
    "${app_source_dir}/FileNode.cpp"
    "${app_source_dir}/FileSystem.cpp"
    "${app_source_dir}/Operation.cpp"
    "${app_source_dir}/PrivacyCrypto.cpp"
    "${app_source_dir}/AdvancedTools.cpp"
    "${app_source_dir}/AiTools.cpp"
    "${app_source_dir}/icons.qrc"
    ${FILEMANAGER_AI_SETTINGS_SOURCES}
)
target_compile_features(acceptance_probe PRIVATE cxx_std_17)
target_include_directories(acceptance_probe PRIVATE
    "${app_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/acceptance_probe_autogen/include")
target_link_libraries(acceptance_probe PRIVATE Qt::Core Qt::Widgets Qt::Network OpenSSL::Crypto)
set_target_properties(acceptance_probe PROPERTIES AUTOUIC_SEARCH_PATHS "${app_source_dir}")

add_executable(core_regression core_copy_move_test.cpp
    "${app_source_dir}/FileNode.cpp" "${app_source_dir}/FileSystem.cpp"
    "${app_source_dir}/Operation.cpp" "${app_source_dir}/PrivacyCrypto.cpp")
target_compile_features(core_regression PRIVATE cxx_std_17)
target_link_libraries(core_regression PRIVATE OpenSSL::Crypto)

add_executable(crypto_regression crypto_test.cpp "${app_source_dir}/PrivacyCrypto.cpp")
target_compile_features(crypto_regression PRIVATE cxx_std_17)
target_link_libraries(crypto_regression PRIVATE OpenSSL::Crypto)

qt_add_executable(ai_settings_regression ai_settings_test.cpp ${FILEMANAGER_AI_SETTINGS_SOURCES})
target_compile_features(ai_settings_regression PRIVATE cxx_std_17)
target_include_directories(ai_settings_regression PRIVATE "${app_source_dir}")
target_link_libraries(ai_settings_regression PRIVATE Qt::Core Qt::Widgets Qt::Network)
if(WIN32)
    target_link_libraries(acceptance_probe PRIVATE advapi32)
    target_link_libraries(ai_settings_regression PRIVATE advapi32)
endif()

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/acceptance-data")
add_test(NAME core_operations COMMAND core_regression)
add_test(NAME aes_gcm COMMAND crypto_regression)
add_test(NAME ui_search_classification COMMAND acceptance_probe)
add_test(NAME release_startup COMMAND FileManager --smoke-test)
add_test(NAME ai_settings_secure_storage COMMAND ai_settings_regression)
set_tests_properties(core_operations aes_gcm ui_search_classification release_startup ai_settings_secure_storage PROPERTIES
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/acceptance-data"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30)