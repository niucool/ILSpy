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
    ${CMAKE_CURRENT_SOURCE_DIR}/LoadedAssembly.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AssemblyListSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AssemblyListManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoadedAssemblyExtensions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AssemblyList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/MetadataFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/PEFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/WebCilFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/BundleFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/ArchiveFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/XamarinCompressedFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileLoaders/FileLoaderRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Settings/ILSpySettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Settings/MutexProtector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Settings/SettingsServiceBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Settings/DecompilerSettings.cpp
)
