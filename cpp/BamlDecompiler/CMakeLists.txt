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
# Func-shaped accessors), plus the BAML-side resolution context (BamlContext:
# the four info-record id maps ConstructContext builds from the flat
# document, the KnownThings instance the ctor resolves, and the
# ResolveAssembly assembly resolution -- the main-module match arm,
# FindMatchingReference's highest-version pick over ReferencedModules, and
# the 0xfff id mask), plus the XAML-side context layer (BamlDecompilerSettings,
# BamlElement/XamlNode -- the handler value types over the System.Xml.Linq
# stand-in; XamlType/XamlProperty -- the resolved type/property rows; and
# XamlContext -- the Construct composition over BamlContext + BamlNode.Parse
# with the type/property/string id resolution and the XML-namespace helpers;
# the BAML-record arm of ResolveType resolves through the
# ReflectionHelper.ParseReflectionName/ResolveTypeName chain over the
# ICompilation.FindModuleByAssemblyNameInfo lookup -- the System.Reflection.Metadata
# TypeName/AssemblyNameInfo parser it composes landed in
# cpp/Decompiler/Metadata/TypeName.{hpp,cpp} and AssemblyNameInfo.{hpp,cpp}, and
# the resolution chain in Decompiler/TypeSystem/ReflectionHelper.{hpp,cpp}),
# plus the XamlExtension markup-extension value (Xaml/XamlExtension: the
# {TypeName args, name=value} render over the XamlUtils ToString extensions,
# which land with it) and the resource-key annotation (Xaml/XamlResourceKey:
# the key-node/key-element/static-resources row the key-record handlers
# attach through the node annotations, with the sibling/ancestor key lookups),
# plus the record-type-to-handler dispatch (IHandlers: the IHandler/IDeferHandler
# interfaces and the HandlerMap -- the explicit CreateBuiltinHandlers manifest
# standing in for the C# reflection walk, EMPTY until the Handlers/Blocks and
# Handlers/Records classes land (the 37-row gold inventory they must reproduce
# is pinned in tests/BamlDecompiler/HandlerMap_Test.cpp), the lazy LookupHandler
# over it, the duplicate-key InstallHandler, and the ProcessChildren walk with
# its XmlnsScope push/pop + annotation pairing).
# The IDecompilerTypeSystem interface (the MetadataModule MainModule narrowing)
# and the Handlers/Rewrite/XamlDecompiler
# layers follow in later slices.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlBinaryWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlRecords.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/BamlWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Baml/KnownThings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IHandlers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SyntheticWpfModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlExtension.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlPathDeserializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlResourceKey.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Xaml/XamlUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XamlContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlnsDictionary.cpp
)
