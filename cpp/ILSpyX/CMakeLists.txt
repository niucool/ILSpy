# ICSharpCode.ILSpyX core subset (FileLoaders, PdbProvider, Abstractions, ...).
# Phase 8 of PORT_PLAN.md. The PdbProvider port begins with the
# PortableDebugInfoProvider -- the IDebugInfoProvider implementation over the
# Phase-1 portable-PDB debug-table reader (the Cecil bridge the plan drops).
# FileLoaders/Abstractions follow in later phases.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/PdbProvider/PortableDebugInfoProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PdbProvider/DebugInfoUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Util/GuessFileType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoadedPackage.cpp
)
