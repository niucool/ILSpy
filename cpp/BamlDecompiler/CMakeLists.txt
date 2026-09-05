# ICSharpCode.BamlDecompiler (BAML -> XAML). Phase 9 of PORT_PLAN.md.
# The port begins with the Baml/ binary-format layer: the record classes
# (BamlRecords), the reader (BamlReader/BamlBinaryReader), the document, the
# round-trip writer (BamlWriter/BamlBinaryWriter), and the decompose step
# (BamlNode, the flat record list folded into the block tree), plus the
# type-system-free Xaml-layer leaves: NamespaceMap (Xaml/), the scoped xmlns
# bookkeeping (XmlnsDictionary/XmlnsScope), and the self-contained XamlUtils
# members plus the binary path decoder (Xaml/XamlUtils, Xaml/XamlPathDeserializer
# -- the PropertyCustomHandler serializer slice) -- plus the KnownThings id
# enums (KnownTypes/KnownMembers: the BAML wire-format value tables WPF's
# schema context fixes) and the KnownThings data tables (KnownThingsTables:
# the assembly/type/member/string/resource rows KnownThings.g.cs seeds its
# dictionaries with, header-only pending the type-system resolution slice),
# plus the synthetic stand-in module (SyntheticWpfModule: the artificial
# IModule standing in for an unresolvable well-known BAML assembly,
# materializing exactly the types RegisterType seeds -- the piece that lets
# KnownThings work without the full MetadataModule back end; includes the
# TypeSystem Implementation DefaultAttribute IAttribute class the
# XmlnsDefinitionAttribute reconstruction builds),
# plus the KnownThings class itself (KnownThings: the ctor resolving the table
# rows through the compilation's modules -- the SyntheticWpfModule seeding arm
# and the GetTypeDefinition arm -- with the KnownMember row class and the
# Func-shaped accessors).
# The IDecompilerTypeSystem interface (the MetadataModule MainModule narrowing)
# and the Handlers/Rewrite/Xaml layers follow in later slices.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlRecords.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/KnownThings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SyntheticWpfModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlPathDeserializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlnsDictionary.cpp
)
