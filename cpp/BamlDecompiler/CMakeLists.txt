# ICSharpCode.BamlDecompiler (BAML -> XAML). Phase 9 of PORT_PLAN.md.
# The port begins with the Baml/ binary-format layer: the record classes
# (BamlRecords), the reader (BamlReader/BamlBinaryReader), the document, and
# the round-trip writer (BamlWriter/BamlBinaryWriter). The KnownThings tables
# and the Handlers/Rewrite/Xaml layers follow in later slices.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlRecords.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlWriter.cpp
)
