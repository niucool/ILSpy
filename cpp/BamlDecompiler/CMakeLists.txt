# ICSharpCode.BamlDecompiler (BAML -> XAML). Phase 9 of PORT_PLAN.md.
# The port begins with the Baml/ binary-format layer: the record classes
# (BamlRecords), the reader (BamlReader/BamlBinaryReader), the document, the
# round-trip writer (BamlWriter/BamlBinaryWriter), and the decompose step
# (BamlNode, the flat record list folded into the block tree), plus the
# type-system-free Xaml-layer leaves: NamespaceMap (Xaml/), the scoped xmlns
# bookkeeping (XmlnsDictionary/XmlnsScope), and the self-contained XamlUtils
# members plus the binary path decoder (Xaml/XamlUtils, Xaml/XamlPathDeserializer
# -- the PropertyCustomHandler serializer slice). The KnownThings tables and
# the Handlers/Rewrite/Xaml layers follow in later slices.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlRecords.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlPathDeserializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlnsDictionary.cpp
)
