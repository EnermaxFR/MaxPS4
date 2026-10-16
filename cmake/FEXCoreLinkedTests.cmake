# Build the same native bridge against the pinned coherent FEX dependency on
# Linux ARM64, or its explicit VIXL ARM64-instruction simulator on x86 CI.
# No replacement FEX or system-service symbols are supplied.
function(maxps4_linked_execution_tests)
  set(NATIVE "${MAXPS4_ROOT}/MaxPS4-iOS/Native")
  # The shared dispatcher patch unwinds failed construction on every host.
  # Match the Darwin build: do not compile its try/throw with -fno-exceptions.
  target_compile_options(FEXCore_object PRIVATE -fexceptions)
  target_compile_options(FEXCore_Base PRIVATE -fexceptions)
  add_library(MaxPS4LinkedRuntime STATIC
    "${NATIVE}/maxps4_fex_runtime.cpp"
    "${NATIVE}/maxps4_jit_memory.cpp"
    "${NATIVE}/maxps4_guest_elf.cpp"
    "${NATIVE}/maxps4_ps4_linker.cpp"
    "${NATIVE}/maxps4_ps4_libkernel.cpp"
    "${NATIVE}/maxps4_ps4_virtual_memory.cpp"
    "${NATIVE}/maxps4_ps4_threads.cpp"
    "${NATIVE}/shadps4_bridge.cpp"
    "${NATIVE}/shadps4/common/string_util.cpp"
    "${CMAKE_SOURCE_DIR}/Source/Common/HostFeatures.cpp"
    "${CMAKE_SOURCE_DIR}/Source/Common/CPUInfo.cpp")
  target_compile_features(MaxPS4LinkedRuntime PRIVATE cxx_std_23)
  target_include_directories(MaxPS4LinkedRuntime PRIVATE
    "${NATIVE}" "${NATIVE}/shadps4" "${CMAKE_BINARY_DIR}/generated")
  target_link_libraries(MaxPS4LinkedRuntime PRIVATE FEXCore FEXCore_Base JemallocLibs range-v3::range-v3)
  add_executable(maxps4-fex-translation-test "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/FEXGuestTranslationTest.cpp")
  target_compile_features(maxps4-fex-translation-test PRIVATE cxx_std_23)
  target_include_directories(maxps4-fex-translation-test PRIVATE "${NATIVE}" "${CMAKE_SOURCE_DIR}/Source")
  target_sources(maxps4-fex-translation-test PRIVATE "${CMAKE_SOURCE_DIR}/Source/Common/HostFeatures.cpp" "${CMAKE_SOURCE_DIR}/Source/Common/CPUInfo.cpp")
  target_link_libraries(maxps4-fex-translation-test PRIVATE FEXCore FEXCore_Base JemallocLibs fmt::fmt range-v3::range-v3)
  add_executable(maxps4-ps4-linked-test "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/PS4FEXLinkedExecutionTest.cpp")
  target_compile_features(maxps4-ps4-linked-test PRIVATE cxx_std_23)
  target_link_libraries(maxps4-ps4-linked-test PRIVATE MaxPS4LinkedRuntime fmt::fmt)
  add_executable(maxps4-v4-c-abi-smoke "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/V4PRXLinkSmoke.c")
  set_target_properties(maxps4-v4-c-abi-smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX)
  target_link_libraries(maxps4-v4-c-abi-smoke PRIVATE MaxPS4LinkedRuntime fmt::fmt)
  target_include_directories(maxps4-ps4-linked-test PRIVATE "${CMAKE_SOURCE_DIR}/FEXCore/include" "${CMAKE_BINARY_DIR}/include")
endfunction()
cmake_language(DEFER CALL maxps4_linked_execution_tests)

