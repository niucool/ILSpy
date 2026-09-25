# Decompiler engine sources. Mirrors ICSharpCode.Decompiler/.
# Subdirectories attach their translation units to the parent `ilspy` library.

# Root-level sources (the C# files that live at ICSharpCode.Decompiler/ itself,
# not under a subdirectory).
target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/SingleFileBundle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DecompilerSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NRExtensions.cpp
)

# DecompileRun is header-only (the C# class carries no behavior beyond the field
# surface the builders consume).

add_subdirectory(Util)
add_subdirectory(Metadata)
add_subdirectory(TypeSystem)
add_subdirectory(Xml)
add_subdirectory(IL)
add_subdirectory(CSharp)
add_subdirectory(FlowAnalysis)
add_subdirectory(Output)
add_subdirectory(Disassembler)
add_subdirectory(DebugInfo)
