# Native tests: build the add-on sources without the Archicad API

add_executable(test_search_engine_native
    test_search_engine_native.cpp
    ../addon/src/SearchEngine.cpp
    ../addon/src/Settings.cpp
)
target_include_directories(test_search_engine_native PRIVATE ../addon/include)
add_test(NAME SearchEngineNative COMMAND test_search_engine_native)

# Spacebar hook edge cases, run through a real Win32 message queue. The stub
# header records synthesized keystrokes instead of typing them.
add_executable(test_message_hook
    test_message_hook.cpp
    ${BAAZ_SHARED_SOURCES}
)
target_include_directories(test_message_hook PRIVATE ../addon/include)
target_compile_definitions(test_message_hook PRIVATE UNICODE _UNICODE)
target_compile_options(test_message_hook PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/test_sendinput_stub.hpp")
target_link_libraries(test_message_hook PRIVATE ${BAAZ_WIN32_LIBS})
add_test(NAME MessageHookEdgeCases COMMAND test_message_hook)

# Command scheme reader, tested against a trimmed copy of a real export
add_executable(test_command_line_native
    test_command_line_native.cpp
    ../addon/src/SchemeReader.cpp
)
target_include_directories(test_command_line_native PRIVATE ../addon/include)
target_compile_definitions(test_command_line_native PRIVATE UNICODE _UNICODE
    BAAZ_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME CommandLineNative COMMAND test_command_line_native)

# Binary command scheme reader, built on synthetic bytes; real files only through BAAZ_TEST_SCHEME_BIN/XML
add_executable(test_bin_scheme
    test_bin_scheme.cpp
    ../addon/src/BinSchemeReader.cpp
    ../addon/src/SchemeReader.cpp
)
target_include_directories(test_bin_scheme PRIVATE ../addon/include)
target_compile_definitions(test_bin_scheme PRIVATE UNICODE _UNICODE)
add_test(NAME BinScheme COMMAND test_bin_scheme)
