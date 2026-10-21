# Included only by the pinned vendored Darwin project's top-level project().
# The upstream shared target contains the core objects but omits its base archive.
function(maxps4_complete_fexcore_linkage)
  set(NATIVE "${MAXPS4_ROOT}/MaxPS4-iOS/Native")
  # Propagate allocator failures through FEX constructors to the C bridge.
  target_compile_options(FEXCore_object PRIVATE -fexceptions)
  target_compile_options(FEXCore_Base PRIVATE -fexceptions)
  target_sources(FEXCore_Base PRIVATE "${NATIVE}/maxps4_jit_memory.cpp")
  target_include_directories(FEXCore_Base PRIVATE "${NATIVE}")
  target_include_directories(FEXCore_object PRIVATE "${NATIVE}")
  target_include_directories(FEXCore PRIVATE "${NATIVE}")
  target_include_directories(FEXCore_shared PRIVATE "${NATIVE}")
  target_link_libraries(FEXCore_shared PRIVATE FEXCore_Base)
  target_link_options(FEXCore_shared PRIVATE "LINKER:-undefined,error")
  if(MAXPS4_BUILD_RUNTIME)
    set(NATIVE "${MAXPS4_ROOT}/MaxPS4-iOS/Native")
    # One coherent static FEX runtime lives inside this native library. No
    # generic headers/objects or second FEX shared ABI are loaded alongside it.
    add_library(MaxPS4FEXRuntime SHARED
      "${NATIVE}/maxps4_jit_memory.cpp"
      "${NATIVE}/maxps4_fex_runtime.cpp"
      "${NATIVE}/maxps4_game_session.cpp"
      "${NATIVE}/maxps4_guest_elf.cpp"
      "${NATIVE}/maxps4_ps4_libkernel.cpp"
      "${NATIVE}/maxps4_ps4_virtual_memory.cpp"
      "${NATIVE}/maxps4_ps4_threads.cpp"
      "${NATIVE}/maxps4_ps4_linker.cpp"
      "${NATIVE}/maxps4_native.cpp"
      "${NATIVE}/aether_jit_allocator_abi.cpp"
      "${NATIVE}/aether_guest_cpu_adapter.cpp"
      "${NATIVE}/shadps4_bridge.cpp"
      "${NATIVE}/shadps4/common/string_util.cpp"
      "${MAXPS4_AETHER_SOURCE}/src/core/guest_cpu/guest_cpu.cpp"
      "${CMAKE_SOURCE_DIR}/Source/Common/HostFeatures.cpp"
      "${CMAKE_SOURCE_DIR}/Source/Common/CPUInfo.cpp")
    target_compile_features(MaxPS4FEXRuntime PRIVATE cxx_std_23)
    target_compile_definitions(MaxPS4FEXRuntime PRIVATE
      ARCHITECTURE_arm64=1 MAXPS4_PLATFORM_JIT_ONLY=1)
    target_include_directories(MaxPS4FEXRuntime PRIVATE
      "${NATIVE}" "${NATIVE}/shadps4" "${MAXPS4_AETHER_SOURCE}/src"
      "${CMAKE_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}/Source")
    target_link_libraries(MaxPS4FEXRuntime PRIVATE
      FEXCore FEXCore_Base JemallocLibs range-v3::range-v3)
    target_link_options(MaxPS4FEXRuntime PRIVATE "LINKER:-undefined,error")
    add_executable(maxps4-fex-runtime-test
      "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/FEXRuntimeIntegrationTest.cpp")
    target_compile_features(maxps4-fex-runtime-test PRIVATE cxx_std_23)
    target_include_directories(maxps4-fex-runtime-test PRIVATE "${NATIVE}"
      "${CMAKE_SOURCE_DIR}/FEXCore/include" "${CMAKE_BINARY_DIR}/include")
    target_link_libraries(maxps4-fex-runtime-test PRIVATE MaxPS4FEXRuntime fmt::fmt)
  add_executable(maxps4-fex-translation-test "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/FEXGuestTranslationTest.cpp")
  target_compile_features(maxps4-fex-translation-test PRIVATE cxx_std_23)
  target_include_directories(maxps4-fex-translation-test PRIVATE "${NATIVE}" "${CMAKE_SOURCE_DIR}/Source")
  target_sources(maxps4-fex-translation-test PRIVATE "${CMAKE_SOURCE_DIR}/Source/Common/HostFeatures.cpp" "${CMAKE_SOURCE_DIR}/Source/Common/CPUInfo.cpp")
  target_link_libraries(maxps4-fex-translation-test PRIVATE FEXCore FEXCore_Base JemallocLibs fmt::fmt range-v3::range-v3)
    add_executable(maxps4-ps4-linked-test
      "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/PS4FEXLinkedExecutionTest.cpp")
    target_compile_features(maxps4-ps4-linked-test PRIVATE cxx_std_23)
    target_link_libraries(maxps4-ps4-linked-test PRIVATE MaxPS4FEXRuntime fmt::fmt)
    add_executable(maxps4-v4-c-abi-smoke "${MAXPS4_ROOT}/MaxPS4-iOS/Tests/V4PRXLinkSmoke.c")
    set_target_properties(maxps4-v4-c-abi-smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO LINKER_LANGUAGE CXX)
    target_link_libraries(maxps4-v4-c-abi-smoke PRIVATE MaxPS4FEXRuntime)
  target_include_directories(maxps4-ps4-linked-test PRIVATE "${CMAKE_SOURCE_DIR}/FEXCore/include" "${CMAKE_BINARY_DIR}/include")
    # Host-only profile: macOS rejected a low-profile thread/TLS reservation.
    # iOS retains the default low profile; this does not fix device error -48.
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      target_compile_definitions(MaxPS4FEXRuntime PRIVATE MAXPS4_M1_HIGH_VA_HOST_TEST=1)
      target_compile_definitions(maxps4-ps4-linked-test PRIVATE MAXPS4_M1_HIGH_VA_HOST_TEST=1)
    endif()
  endif()
endfunction()
cmake_language(DEFER CALL maxps4_complete_fexcore_linkage)


