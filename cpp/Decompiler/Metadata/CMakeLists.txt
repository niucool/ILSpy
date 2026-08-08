# Metadata layer (Phase 1). Wraps the vendored microsoft/winmd ECMA-335 reader
# (Ecma335/winmd/) and adds the method-body / debug-table / WebCIL / bundle
# gap fills on top of it (see PORT_PLAN.md section 5.1).
#
# winmd is header-only and is included only from MetadataFile.cpp, so the
# <windows.h> it pulls in on Windows stays out of MetadataFile.hpp (pimpl).

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/MetadataFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MethodBody.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SignatureDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ILOpCodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ILDisassembler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ILTextEmitter.cpp
)

# winmd headers use #include "impl/base.h" relative to their own src/ directory;
# adding that directory makes both "winmd_reader.h" and the internal "impl/..."
# includes resolve.
target_include_directories(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Ecma335/winmd/src
)
