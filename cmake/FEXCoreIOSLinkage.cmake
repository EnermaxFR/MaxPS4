# Included only by the pinned vendored Darwin project's top-level project().
# The upstream shared target contains the core objects but omits its base archive.
function(maxps4_complete_fexcore_linkage)
  target_link_libraries(FEXCore_shared PRIVATE FEXCore_Base)
  target_link_options(FEXCore_shared PRIVATE "LINKER:-undefined,error")
endfunction()
cmake_language(DEFER CALL maxps4_complete_fexcore_linkage)
