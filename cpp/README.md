# ilspy (C++ port)

C++17 port of the ILSpy decompiler **core and CLI** (the `ICSharpCode.Decompiler`,
`ICSharpCode.ILSpyX`, `ICSharpCode.BamlDecompiler`, and `ICSharpCode.ILSpyCmd`
projects). The GUI, AddIns, Installer, and PowerShell front-ends are out of scope.

The *what and why* of the port lives in [`../PORT_PLAN.md`](../PORT_PLAN.md); the
*how* (file/name/namespace mirroring, C#-to-C++ construct mapping) lives in
[`../MIRROR_LAYOUT_REORGANIZATION.md`](../MIRROR_LAYOUT_REORGANIZATION.md).

## Status

Phase 0, Phase 1 (metadata), Phase 2 (type system), Phase 3 (the ILAst model
+ IL reader), and Phase 4 (the ~40-transform IL pipeline + the ILAst-to-C#-text
seed) are implemented and green here; Phase 5 (the real C# AST + output back
end) is in progress -- the C# AST node family and the `CSharpOutputVisitor`
pretty-printer are being ported alongside the seed, exercised by direct unit
tests, and not yet wired into the `--csharp` CLI path (the seed still drives
it). Everything else follows the phase plan in `PORT_PLAN.md`:

- **Phase 0** -- build system, `Util/` primitives (UTF-8/16, `Span`, `ImmutableStack`),
  the three targets, and a Google Test driver. DONE.
- **Phase 1** -- `Decompiler/Metadata/` on the vendored `microsoft/winmd` ECMA-335
  baseline: method-body decoding (ECMA-335 II.25.4 tiny/fat + exception handlers),
  signature decoding (Type/Method/Field -> IType), the entity surface (TypeDef/
  Method/Field/Property + base types + custom attributes + TypeKind derivation),
  token resolution, the `MetadataFile` name reverse lookups
  (`GetTypeDefinition(TopLevelTypeName)` / `GetTypeForwarder(FullTypeName)` --
  the lazy dictionaries the Phase-2 `MetadataModule` back end resolves
  through, plus the `SRMExtensions` ExportedType full-name reader with the
  nested-forwarder-chain walk), the `NamespaceDefinition` namespace tree
  (SRM's `NamespaceCache`: the TypeDef/ExportedType namespace tree with the
  duplicate-full-name merge and the synthesized intermediate namespaces --
  the `MetadataReader.GetNamespaceDefinitionRoot` surface the Phase-2
  `MetadataModule`/`MetadataNamespace` walk consumes), the strong-name
  assembly-identity family (the vendored `Sha1ForNonSecretPurposes` hashing,
  `MetadataFile.CorBlob`, and the `MetadataExtensions`
  `CalculatePublicKeyToken`/`GetPublicKeyToken`/`GetFullAssemblyName` trio
  with the `TryGetFullAssemblyName` forms and the `MetadataFile.FullName`
  property -- the display-name resolution the Phase-2 `MetadataModule` ctor
  computes `FullAssemblyName` through), the `DotNetCorePathFinder`
  (the `.deps.json` package-base-path discovery, the shared-framework
  resolution, the reference-assembly pack path, and the PATH scan for the
  dotnet executable, gold-pinned against the real engine over the crafted
  `.deps.json` scenarios and the real dotnet install), the
  `ReferenceLoadInfo`/`UnresolvedAssemblyNameReference` diagnostics
  bookkeeping, and the `UniversalAssemblyResolver` enums +
  `ParseTargetFramework` classifier, and an IL text
  disassembler. DONE except WebCIL:
  the `UniversalAssemblyResolver` instance surface is fully landed -- the
  ctor, the search-directory trio, `IsSharedAssembly` through the lazy
  `DotNetCorePathFinder`, the `FindAssemblyFile`/`FindAssemblyFileCore`
  target-framework dispatch with the full `ResolveInternal` chain, the
  winmd arms, `GetCorlib`/`GetMscorlibBasePath`, the static GAC machinery
  (`GetGacPaths`/`GetAssemblyInGac`/`EnumerateGac`/`IsZeroOrAllOnes`/
  `IsSpecialVersionOrRetargetable`/`GetAssemblyFile` over the
  `AssemblyReferenceClassifier` base and `ResolutionException`), and the
  `IAssemblyResolver` file-loading half (`Resolve`/`ResolveModule`/
  `CreatePEFileFromFileName` over the port's `MetadataFile` with its
  `MetadataFileNotSupportedException` escape arm -- the class now derives
  `IAssemblyResolver`; all gold-pinned over this machine's framework
  directories, GAC, Windows Kits references, .NET 10 shared-framework
  install, and crafted garbage/native-PE module fixtures). The
  `ConnectionIdRewritePass` -- the last BamlDecompiler deferral,
  unblocked when the Phase-3/4 ILAst back end landed -- also landed: the
  ILAst-driven rewrite pass reads the x:Class type's Connect method body
  through the IL reader + the shared `IL::RunGetILTransforms` pipeline
  (the flattened `CSharpDecompiler.GetILTransforms()` +
  `ILFunction.RunTransforms` list, extracted from the CLI so both
  consumers drive one list), matches its switch (or if-ladder fallback)
  for the field assignments and event registrations, and renders the
  x:Name/x:FieldModifier/event attributes, the Style target's EventSetter
  child, and the unknown-id comments -- gold-pinned byte-exact against
  the real ilspycmd 11.0 `--resource` over a real csc-compiled fixture
  assembly with the WPF stand-in shapes and a 4-case code-behind Connect
  switch).
- **Phase 2** -- `Decompiler/TypeSystem/`: naming primitives (`TopLevelTypeName`,
  `FullTypeName`), `KnownTypeCode` (the full table -- 61 slots continuing
  System.TypeCode's numbering, `String`=18 with the value-17 hole the C# keeps),
  the `IType` hierarchy
  (`KnownType`/`SimpleType`/`ParameterizedType`/`ArrayType`/`ByReferenceType`/
  `PointerType`/`TypeParameter`/`SpecialType`), `DeriveTypeKind`, and the
  `MetadataModule` skeleton + `MetadataNamespace` (the ctor's assembly
  identity arms over the raw Cor-table surface, the `IModule` identity surface,
  and the per-module namespace tree over the `NamespaceDefinition` cache),
  the SRMExtensions kind/attribute predicates (`IsKnownType` / `IsEnum` /
  `IsValueType` / `IsDelegate` / `HasKnownAttribute`), the
  type-definition entity layer (`MetadataTypeDefinition` +
  `MetadataTypeParameter` + the `GetDefinition` entity cache), and the
  cross-module resolution family (`BusyManager`, the metadata-backed
  `AssemblyReference` row wrapper, `ResolveModule` over both handle kinds,
  `GetDeclaringModule`, `FindModuleByReference`, and the
  `GetTypeDefinition` forwarder arm through `ResolveForwardedType`; all
  gold-pinned against the real engine), and the signature-provider layer
  (`GenericContext` + the module-owned `TypeProvider` over the templated
  `ISignatureTypeProvider` walker, `PinnedType`,
  `FunctionPointerType.FromSignature`, `ToPrimitiveTypeCode`, and the
  `EnumUnderlyingTypeResolveException`; gold-pinned end-to-end against the
  real engine over the mscorlib+System fixture incl. the FnPtr/CallConv
  modreq matrix), and `MinimalCorlib` (the artificial all-known-types module
  the `minimalCorlibTypeProvider` builds its provider over and
  `DecompilerTypeSystem`'s missing-known-types fallback appends, with the
  lifted `DummyTypeParameter.GetClassTypeParameterList`; gold-pinned against
  the real engine over `MinimalCorlib.Instance`/`CreateWithTypes`), and the
  `minimalCorlibTypeProvider` wiring (`MetadataExtensions`
  `.MinimalAttributeTypeProvider` / `MinimalSignatureTypeProvider` -- one
  process-lifetime `TypeProvider`
  over a `SimpleCompilation(MinimalCorlib.Instance)`, the provider the
  NullableContext/NullablePublicOnly/DefaultMember attribute-value decoders
  drive; gold-pinned against the real engine's public properties, with the
  `KnownTypeCode`/`TypeCode` value-hole fidelity fix the gold exposed), and
  the `CustomAttributeDecoder` (the SRM `CustomAttribute.DecodeValue` blob
  decode fused with the repo copy's stripped-down named-args half and its
  `provideBoxingTypeInfo` boxing, over `MetadataFile` + `TypeProvider` with
  the reader-parameterized `GetTypeFromDefinition`/`GetTypeFromReference`
  overloads the TypeHandle arm drives; gold-pinned byte-exact against the
  real SRM engine over the full mscorlib/System/facade/CoreLib attribute
  corpora incl. the ok/throw partitions and the running digests, the
  synthetic manifest covering every exotic + failure arm, and the boxing
  drive), and the `ApplyAttributeTypeVisitor` (the dynamic/tuple/
  nullability/native-integer substituting TypeVisitor with both
  `ApplyAttributesToType` entries -- the metadata entry decoding the
  `[Dynamic]`/`[NativeInteger]`/`[TupleElementNames]`/`[Nullable]` rows
  through the `minimalCorlibTypeProvider` decoder, and the PDB entry over
  `PdbExtraTypeInfo` -- plus its supporting slices: the
  `SpecialType.Dynamic/NInt/NUInt` factories, `ArrayType`'s nullability
  field + `ChangeNullability`, `ParameterizedType.Nullability` (the
  generic-delegation the gold exposed was missing),
  `FunctionPointerType.WithSignature`, the lifted `TupleType.IsTupleCompatible`
  + the compilation-driven `CreateTupleType` (the C# ctor that builds the
  underlying `ValueTuple<...>` chain), and the
  `FindType(ICompilation, FullTypeName)` modules-scan extension; gold-pinned
  byte-exact against the real engine over 41 drives: the tuple machinery
  over the real mscorlib `ValueTuple` definitions, the dynamic/native/nullability
  index walks, the KeepModifiers/typeChildrenOnly/no-options/accumulation/
  additionalAttributes arms, and the PDB seeding). STARTED
  (the member entity family remains: `MetadataMethod`/
  `MetadataProperty`/`MetadataEvent` and interning; `MetadataModule.
  ResolveType` (the top-byte table dispatch + the
  `ApplyAttributeTypeVisitor` wrap) and `MetadataTypeDefinition.
  DirectBaseTypes` LANDED, gold-pinned against the real engine, together
  with the `ParameterizedType` reflection-name reconciliation to the C#
  `[[...]]` geometry; and `MetadataField` LANDED -- the first member
  entity, with its `DecimalConstantHelper` (the [DecimalConstantAttribute]
  const decode incl. the scale-29 ArgumentOutOfRange that escapes both
  `GetConstantValue` arms), the `MetadataModule.GetDefinitionField`
  per-row entity cache, the `IsFieldVisible`/`IncludeInternalMembers`
  visibility filter (`OnlyPublicAPI` drops the non-public rows), and
  `MetadataTypeDefinition.Fields` with the `GetFields`
  `IgnoreInheritedMembers` short-circuit arm -- gold-pinned against the
  real engine with the whole-corpus FNV-1a-64 digests over EVERY field of
  mscorlib (14717 fields) and System.dll (15896 fields), both
  byte-exact, plus the crafted `MfSynth.dll` manifest covering every
  crafted arm), and `MethodSemanticsLookup` LANDED (the
  `Metadata/MethodSemanticsLookup.cs` accessor->association lookup the
  `MetadataMethod` ctor consults, with the `MetadataFile` lazy property +
  the `MethodSemanticsRows` whole-table read -- the single-pass build
  reproducing SRM's per-association exact-value last-row-wins `GetAccessors`
  slots incl. nil-method rows, the `csharpAccessors` filter with the
  Raiser drop and the Other rejection, and the verbatim .NET binary
  search; gold-pinned against the real engine with the whole-corpus
  digests over every entry AND every `GetSemantics` drive of
  mscorlib/System.dll/CoreLib (5986+5407+6134 entries, all
  byte-exact) plus the crafted `MslSynth.dll` manifest covering every
  crafted arm -- the last-row-wins pair, the nil-method pair, the
  combined-flags row, the Raiser filter, and the Other rejection), and
  `MetadataMethod` LANDED (the biggest member sibling: the ctor's
  symbolKind chain -- Accessor/Constructor/Operator/Destructor incl. the
  static-explicit-interface-operator re-test -- over the landed
  MethodSemanticsLookup, the eager type-parameter creation, the
  IsExtensionMethod option+attribute gate, and the signature-decode chain
  (`Parameters`/`ReturnType`/`IsInitOnly` through the shared static
  `DecodeSignature` Param-row walk with the gap-filling DefaultParameters,
  the vararg `__arglist` sentinel -- mscorlib's 3 vararg methods pin it --
  the `modreq(IsExternalInit)` init-only test, and the return-type
  attribute application), together with the `MetadataParameter` companion
  (the per-row IParameter: the DetectRefKind In/Out/Ref/In/RefReadOnly
  chain, IsParams, Lifetime, the constant-value decimal/Constant arms)
  and the `MetadataModule.GetDefinitionMethod` per-row entity cache +
  `IsMethodVisible` (the `MetadataFile::GetParameter` per-token read behind
  them); gold-pinned with the whole-corpus FNV digests over EVERY
  MethodDef row of mscorlib (29257) and System.dll (18170), both
  byte-exact, plus the CoreLib curated drives (the IsInitOnly setter, the
  `[return: IsReadOnly]` method, the readonly-struct census) -- and the
  member-enumeration slice LANDED (`VarArgInstanceMethod`, the vararg
  call-site wrapper with the fresh-wrapper `Specialize`; `FakeMember`
  (the `FakeMethod.CreateDummyConstructor` factory + the SpecializedX::
  Create routing, the general arms landed with the owning-`Specialize`
  slice); and the real `MetadataTypeDefinition.Methods`
  (the accessor-row drop over the landed MethodSemanticsLookup + the
  `IsMethodVisible` filter + the FakeMethod dummy-constructor append for
  structs/enums without a parameterless ctor), `GetMethods` (both
  overloads -- the `IgnoreInheritedMembers` declared arm and the
  GetMembersHelper inherited walk with the keep-alive registry for the
  fresh SpecializedMethod instances), and `IsRecord` (the raw
  method-name scan -- it reads the RAW method list, NOT the
  accessor-dropped `Methods` enumeration, because a record class's
  `get_EqualityContract` is an accessor row the enumeration drops);
  gold-pinned with the whole-corpus FNV-1a-64 digests over the `Methods()`
  enumeration of every TypeDef of mscorlib (3356 types / 24194 methods /
  923 dummy ctors) / System.dll (2365/13385/622) / CoreLib
  (2898/37261/1024), all byte-exact on the first green run, plus the
  IsRecord corpus digests (all-false over the three BCL corpora) and the
  nine REAL record fixtures of the Roslyn Microsoft.CodeAnalysis.dll
  shipped in the SDK (8 record structs + the record class with
  `<Clone>$`)) -- and the owning-`Specialize` slice LANDED (the four
  `SpecializedX::Create` factories -- `SpecializedMethod.Create` with its
  ArrayType-declaring-type arm, the `SpecializedField` / `SpecializedProperty`
  / `SpecializedEvent` forms with the Identity / declaring-tpc-0
  same-instance arms and the `MethodTypeArguments`-stripping -- each
  returning an OWNING `std::shared_ptr` with the caller passing the
  no-op-deleter alias over its own instance and keeping every fresh result
  alive in a keep-alive registry (the C# GC root), plus the real
  `MetadataMethod.Specialize` / `MetadataField.Specialize` and the four
  `FakeX.Specialize` general arms; gold-pinned byte-exact against the real
  engine over the full Create-arm matrix, the fake drives incl. the
  ArrayType arm only a fake can reach, and the whole-type FNV digests over
  every method + field of List`1/Dictionary`2/String specialized with the
  full class substitution); and the resolve-method slice LANDED
  (`MetadataModule.ResolveMethod`/`ResolveEntity`/`ResolveDeclaringType`/
  `CreateFakeMethod` with the `GuessFakeMethodAccessor` accessor-kind guess
  and the `DefaultTypeParameter` factory dependency, the
  `MemberReference.GetKind` rule decompiled from the real
  `SignatureHeader.Kind`, the `NormalizeTypeVisitor`-backed
  `CompareTypes`/`CompareSignatures`, `ResolveMethodDefinition`/
  `ResolveMethodSpecification`/`ResolveMethodReference` with the
  overload search, the vararg `VarArgInstanceMethod` expansion, the
  fake-method fallback, and `ResolveFieldReference` with the
  SR-formatted field-header check; `MetadataTypeDefinition.
  GetConstructors`/`GetAccessors` (the accessor arm preserving the
  C# invoker-yields-remover bug verbatim) and
  `HasOverrides`/`GetOverrides` over the new `MetadataFile.MethodImplRows`
  whole-table read, and `MetadataMethod.
  IsExplicitInterfaceImplementation`/
  `ExplicitlyImplementedInterfaceMembers` real -- gold-pinned against
  the real engine with the whole-corpus FNV digests over every memberref
  row (1717 method + 888 field over mscorlib single-module; 2275 + 280
  over the System+mscorlib paired compilation), every MethodSpec row (656
  / 174), and every MethodImpl-carrying method (1185 same-module + 82
  cross-plain over mscorlib; 133 + 312 over System.dll), all byte-exact,
  with the accessor-shaped partition now resolving through the
  Properties/Events enumerations (824 resolvable-parent mscorlib rows all
  REAL; 630 System rows real + 74 System.Configuration-parent memberrefs
  through the fake-guess path in both engines -- all byte-exact) -- plus the two gold-driven
  fidelity fixes the digests exposed: the `TupleType::VisitChildren`
  underlying-type rebuild (the C# recomputes the ValueTuple chain from the
  substituted elements) and `ParameterizedType::GetFields`'s
  filter-over-raw-fields semantics (the C# filter runs BEFORE the
  specialization wrap; the port's return-time cache filter diverged on the
  first type-sensitive consumer), and the disassembler's memberref-kind
  rule corrected to the decompiled `low nibble <= 5 or == 9` form) -- and
  the AttributeListBuilder slice LANDED (the `AttributeListBuilder` /
  `AttributeBuilder` / `CustomAttribute` classes, the
  `MetadataModule.MakeAttribute`/`GetAttributeType` caches, the entity
  attribute members of `MetadataTypeDefinition`/`MetadataField`/
  `MetadataParameter` (GetAttributes/HasAttribute/GetAttribute) with the
  `GetFields` GetMembersHelper routing the [StructLayout]/[MarshalAs]/
  [PermissionSet] named-arg classification needed, the module-level
  `GetAssemblyAttributes`/`GetModuleAttributes` incl. the
  `AddTypeForwarderAttributes` walk and the `GetInternalsVisibleTo`
  friend-list decode behind the real `InternalsVisibleTo`, and the
  NRT-visibility context (`GetNullableContext`, the module's
  `NullableContext`/`FindMinimumAccessibilityForNRT`/
  `ShouldDecodeNullableAttributes`/`OptionsForEntity`, and the eager
  `MetadataTypeDefinition.NullableContext` -- the CoreLib [AssemblyVersion]
  ctor=null gold pin forced the whole chain, the nullability-annotated
  parameter types make the DefaultAttribute ctor scan miss where the real
  engine's does); gold-pinned against the real engine through the AlProbe
  public-API probe: the whole-corpus FNV digests over EVERY TypeDef
  (3356/2365-ish/2898) and Field (14717/15896/9816) and method+ctor
  Parameter GetAttributes of mscorlib/System.dll/CoreLib, all byte-exact,
  plus the module-level attribute renders incl. the GAC facade's 279
  [TypeForwardedTo] rows and mscorlib's nine [InternalsVisibleTo] friends,
  the curated HasAttribute/GetAttribute matrices over a fixed
  KnownAttribute subset (incl. the KnownAttribute.None drives pinning the
  C# ArgumentNullException the null-name classification throws), and the
  TypeSystemOptions.None CoreLib module pinning the IgnoreAttribute gates
  in the KEEP direction -- plus the MetadataMethod attribute slice: the
  whole-corpus digests over every method GetAttributes/GetReturnTypeAttributes
  of mscorlib/System.dll/CoreLib and the curated DllImport/PreserveSig/
  MethodImpl/SpecialName synthetic-row matrices, all byte-exact against the
  real engine -- plus the MetadataProperty/MetadataEvent entity slice: the
  property/indexer and event classes over the module's property/event
  caches (the ctor's symbolKind chain with the DetermineIsIndexer
  [DefaultMember] comparison and the explicit-interface dotted-name arm,
  the shared static DecodeSignature routing, the ComputeAccessibility
  base-copy walk, the [IndexerName]/SpecialName synthetic rows), the
  MetadataMethod::AccessorOwner routing, MetadataTypeDefinition's
  Properties/Events/Members enumerations and the plain DefaultMemberName
  member, the ResolveEntity property/event arms, and the accessor-search
  arm of ResolveMethodReference -- whole-corpus FNV digests over every
  property (5011/4089/5581 over mscorlib/System.dll/CoreLib) and event
  (33/115/32) plus the accessor-shaped memberref partitions (824 mscorlib
  real / 630 System real + 74 fake) all byte-exact against the real
  engine through the PeProbe gold probe; with the member entity family
  complete, the BAML decompiler's compilation subclass LANDED:
  `BamlDecompilerTypeSystem` (cpp/BamlDecompiler/, the SimpleCompilation
  subclass with the reference-queue ctor resolving the main module's
  ModuleRef rows matching metadata-bearing File-table rows, its AssemblyRef
  rows, and the seven default BAML references through the IAssemblyResolver
  -- the "A:"+FullName/"M:"+name key dedup, the transitive ExportedType-
  implementation walk re-queueing AssemblyReference rows and AssemblyFile
  module names -- substituting a SyntheticWpfModule stand-in for every
  unresolved well-known assembly with the presentation xmlns mapping, and
  falling back to MinimalCorlib when neither the main module nor a
  resolved reference defines Void/Int32; the `new MetadataModule MainModule`
  narrowing realized as a covariant override; backed by `MetadataFile.
  WithOptions`, the MetadataFileWithOptions IModuleReference adapter that
  constructs the MetadataModule on Resolve, and the `MetadataFile.
  GetAssemblyFiles` File-table read; gold-pinned against the real class
  driven over the identical fixed-map stub resolver by the BdtsProbe probe:
  the resolver call order, the module list/order/identity, the synthetic
  substitution, the MinimalCorlib fallback, and the FindType results over
  mscorlib/the WPF assemblies/tiny.netmodule/the GAC facade/a crafted
  ModuleRef+AssemblyFile manifest, all byte-exact). The last named
  `MetadataModule` deferral landed with it: the
  `DecodeMethodSignature`/`DecodeLocalSignature` surface forms (the
  "#region Decode Standalone Signature" -- the kind gate over the
  header nibble with the "Expected Method/LocalVariables signature"
  and parameterless BadImageFormatException arms, the walker decode
  over the module `TypeProvider`, and the `IntroduceTupleTypes` wrap;
  plus the walker's `DecodeTypeSequence` zero-count rejection with the
  exact .NET message), gold-pinned through the SsProbe probe with
  whole-corpus FNV digests over every StandaloneSig row of mscorlib
  (3908) and System.dll (2788) and CoreLib's 24 method-kind rows, all
  byte-exact against the real engine. The `DotNetCorePathFinderExtensions`
  family landed next (the target-framework detection the CLI hands to the
  `UniversalAssemblyResolver` ctor: `DetectTargetFrameworkId` with the
  TargetFrameworkAttribute walk, the assembly-name and AssemblyReference
  fallbacks, and the six-alternative path-pattern regex hand-rolled as a
  leftmost-first-alternative segment matcher; `IsReferenceAssembly`;
  `DetectRuntimePack`; plus `MetadataFile::MetadataVersion` reading the
  root's version string), gold-pinned over twelve real assemblies, 23
  crafted manifests, and the 29-case path matrix via the DtfProbe probe.
  The `DotNetCorePathFinder` / `ReferenceLoadInfo` / `ParseTargetFramework`
  slice landed next (the first sub-slice of the resolver itself: the
  `.deps.json` package-base-path discovery over nlohmann-json's SAX parser
  with LightJson's duplicate-key rejection and exception-message mapping,
  the shared-framework and reference-assembly-pack resolution with the
  version-folder walk, the PATH scan for the dotnet executable, and the
  target-framework classifier with the `TargetFrameworkIdentifier`/
  `TargetRuntime`/`DecompilerRuntime` enums), gold-pinned against the real
  engine via the DncpfProbe probe over the 18 crafted `.deps.json`
  scenarios, the Path BCL matrix, and the real dotnet install.
  The `UniversalAssemblyResolver` static GAC-machinery slice landed next
  (the class-body static half: the `AssemblyReferenceClassifier` base and
  `ResolutionException` prerequisites, `GetGacPaths`/`GetAssemblyInGac`/
  `GetAssemblyFile`/`EnumerateGac` with the hand-rolled GAC folder-name
  regex, and `IsZeroOrAllOnes`/`IsSpecialVersionOrRetargetable`),
  gold-pinned against the real engine over this machine's real .NET
  Framework 4.8 GAC (the 608-entry `EnumerateGac` snapshot digests
  byte-exact; the crafted regex matrix pins the unanchored scan, the greedy
  optional `v4.0_` prefix with its fallback, and the forced group lengths).
  The resolver's INSTANCE file-resolution half landed next (the ctor + the
  `Lazy<DotNetCorePathFinder>` wiring, the search-directory trio,
  `IsSharedAssembly` through the lazy finder, the `FindAssemblyFile`/
  `FindAssemblyFileCore` dispatch with the full `ResolveInternal` chain --
  the search-directory walk, the special-version framework arm, the
  `GetCorlib`/`GetMscorlibBasePath` arm, the GAC arm, the <= 4.0 fallback,
  and the shared-runtime last resort -- the winmd arms, and the
  `FindClosestVersionDirectory` picker), gold-pinned against the real
  engine's drives over this machine's framework directories, GAC, Windows
  Kits references, and .NET 10 shared-framework install. The
  `UniversalAssemblyResolver` is now fully ported including the
  `IAssemblyResolver` file-loading half; with it the BAML engine's whole
  `XamlDecompiler` class landed: the constructor family (the four ctors --
  the concrete `BamlDecompilerTypeSystem` one with the
  `TypeSystemOptions.Uncached` check, the file/resolver pair, and the
  `(fileName, settings)` one through the private
  `CreateTypeSystemFromFile` chain owning the loaded file, the built
  resolver, and the type system), and the `LoadPEFile` failure arms
  reproduced through the port-authored .NET 10 `PEReader`/`PEHeaders`
  eager-parse stand-in (`Decompiler/Metadata/PEReaderParse`: the decompiled
  `SkipDosHeader`/`CoffHeader`/`PEHeader`/`ReadSectionHeaders`/
  cor-directory walk with the exact `BadImageFormatException` message
  matrix -- "Image is too small."/"Image is either too small..."/
  "Invalid PE signature."/"Unknown file format."/"Unknown PE Magic
  value."/"Invalid number of sections..."/"Invalid COR header size."/
  "Section too small."/"Missing data directory."/"Invalid metadata section
  span." -- plus the FileNotFoundException/DirectoryNotFoundException arms
  by parent existence, the `MetadataFileNotSupportedException`, and the
  representative OverflowException message the real engine's
  corrupt-metadata root throws), every ctor drive and failure arm
  gold-pinned against the real engine over the real mscorlib by the
  XamlDecompilerProbe ctor-family section.
  The CLI --resource .baml arm now reaches the landed BamlDecompiler
  end to end (ResourceExtensions.DecompileBaml + the
  IlspyCmdProgram.ExtractResource branch): the BAML-resource
  byte[] value decompiles through BamlDecompilerTypeSystem +
  XamlDecompiler over the CLI-built UniversalAssemblyResolver
  (throwOnError=false, the MetadataReader-overload
  DetectTargetFrameworkId, the new -r|--referencepath search
  paths), rendering the XAML to stdout or saving it under the -o
  directory (the .xaml-suffixed sanitized name, the
  XDocument.Save render with the BOM + declaration), with a
  BamlReader rejection propagating out of ExtractResource to the
  CLI global catch (EX_SOFTWARE).
- **Phase 3** -- `Decompiler/IL/`: the ILAst instruction model (`OpCode` (101,
  verbatim from the generated `Instructions.cs`), `InstructionFlags`, `SlotInfo`,
  `StackType`, `ILVariable`, the `ILInstruction` strict-tree base with
  `CheckInvariant`, `Block`/`BlockContainer`/`ILFunction`, ~30 instruction kinds)
  and a branch- and EH-aware IL reader (`ReadIL`): blocks decode flat, then the
  `BlockBuilder` nests try/handler/filter regions into
  `TryCatch`/`TryFinally`/`TryFault` containers (IL `leave` decodes as a plain
  branch out of the region; `endfinally`/`endfilter` leaves get the innermost
  container). Evaluation-stack values crossing a boundary flush to S_ stack
  slots that unify across predecessors via a merge-representative map.
  **25315 of 25315** mscorlib method bodies decode (100%; all 1651 EH methods
  nested; merged stack slots for cross-block values), zero dangling branches.
  Signatures decode via a hand-rolled ECMA-335 II.23.2 blob parser covering the
  full element-type range (TypedReference, vararg sentinels, fn-ptr handled
  approximately, custom modifiers), which winmd's WinRT-profile TypeSig rejects.
  The remaining instruction kinds (and the ~40 IL transforms) follow Phase 4.
- **Phase 4 (in progress)** -- the ILAst transform pipeline (~20 transforms ported): `ControlFlowSimplification` (branch-chain collapse, dead stack-slot store removal, debug return-block inlining, branch-to-leave/throw/value-return folding, single-edge block merging, fall-through deletion guard), `StObjToStLoc`, `ILInlining`, `InlineReturnTransform`, `RemoveInfeasiblePath`, `DetectPinnedRegions` (IL `fixed`), `DetectCatchWhenConditionBlocks`, `LdLocaDupInitObjTransform`, `EarlyExpressionTransforms`, `RemoveDeadVariableInit`, `SwitchDetection` + `SwitchOnNullable` + `SwitchAnalysis` + `SimplifySwitchInstruction`, `LoopDetection` (back-edge loop containers + exit-path materialization), `PatternMatchingTransform` (C# 7 `is` patterns), `ConditionDetection` (inline fall-through, invert if-exit, merge shared-tail, drop trailing goto-to-next, empty-arm swap, `IntroduceShortCircuit` [nested if-goto -> `&&`], `OrderIfBlocks` [if/else IL-order arm swap], `InlineTrueBranch` [inline the true arm's single-pred forward target, gated to Normal containers + EH-free methods], `InlineExitBranch` [merge a block whose final is a single-pred `br nextBlock`, EH-gated], `PickBetterBlockExit` [invert an if with no else when its true-arm exit outranks the fall-through exit, Normal-containers + EH-gated, runs after the inline transforms as the Block-true-arm fallback]), `LockTransform` (Monitor.Enter/Exit), `UsingTransform` (IDisposable), `CachedDelegateInitialization`, `CachedReadOnlySpanInitialization`, `StatementTransform` (per-statement: `ILInlining` + `ExpressionTransforms` [VisitComp/IfInstruction/Box/Conv/LdElema/NewArr/Call/LdObj/TryCatchHandler + Decimal/DelegateCtor folds + NullableLifting + NullCoalescing + NullPropagation + TransformAssignment + UserDefinedLogic] + `InterpolatedStringTransform`), `HighLevelLoopTransform` (`while(cond)` + `do-while` + `for` when the increment block is hoistable), `ReduceNestingTransform` (`ImproveILOrdering` IL-order re-inversion, `ReduceNesting` no-else + else-if-tree nesting-reduction, `EliminateRedundantTryFinally`, the wired `ExtractElseBlock` branch [then-exits-else extraction]), `FixRemainingIncrements` (op_Increment/op_Decrement calls TransformAssignment missed), `CopyPropagation`, `AssignVariableNames`, `RemoveRedundantReturn` (last-block trailing-`return;` removal + the `ConvertReturnToFallthrough` recursion into try/catch/lock/using/fixed/if bodies, gated against switch case bodies). The FlowAnalysis foundation (`ControlFlowNode`, `Dominance`, `ControlFlowGraph`) supports the loop/switch detection. 93.2% of mscorlib methods are goto-free (23588 of 25314; up from 84% earlier in the port). **The pipeline is deterministic:** `LoopDetection` used to iterate `std::set<ControlFlowNode*>`s in raw pointer order (the block-move order in `ConstructLoop` and the multi-exit pick in `FindExitPoint`), making `ilspycmd --csharp` a non-deterministic function of its input (~36k lines differed between two identical mscorlib runs); both now use stable, layout-faithful order and the output is byte-identical run-to-run at the known-good signature (500 `lock` / 306 `using` / 59 `??`). **The seed drops loop pre-header entry branches** (`goto <while-condition>`) instead of emitting them as dangling references, and `FindExitPoint` prefers the loop-exit convergence (the block every exit flows into) over a bare earliest-offset pick; the seed also emits an `IL_XXXX:` label before a loop header when it is a `goto` target from a non-adjacent position (a non-continue, non-fall-through branch -- the early-exit guard `if (cond) goto loop; return; <loop>` shape), so the `goto` is a valid reference rather than a dangling one, and generalizes this to `try`/`catch`/`lock`/`using` bodies (a `goto` into a construct's entry block from outside is invalid C#, so the entry label moves before the construct keyword, gated to keep it inside when a goto is from inside the construct -- avoids an out-and-back-in EH break); the switch-inline analysis (`AnalyzeSwitchInline`) walks up to the nearest ancestor `BlockContainer` when the switch's host block is an if-arm (`if (cond) { switch ... }`, the `no-outer` shape) so the case bodies in the ancestor container inline; mscorlib emits 5 remaining dangling gotos (down from 545), all switch-internal `goto`-to-case-body targets needing the Phase-5 switch back end.
  block. `LdLocaDupInitObjTransform` rewrites the Roslyn >= 2 `ldloca; dup;
  initobj` codegen for `var v = default(T);` + a use of `&v` --
  `stloc s(ldloca v); stobj(ldloc s, default(T))` becomes `stloc v(default(T));
  stloc s(ldloca v)` so `s` can be inlined into its later uses. The IL reader
  now models `initobj` as `stobj(addr, DefaultValue(type), type)` (a new
  `DefaultValue` node) so `default(T)` renders correctly instead of a type-
  erased `null`/`0`. `EarlyExpressionTransforms` folds the early expression-
  level rewrites the rest of the pipeline depends on: `stobj(ldloca V, ..)` ->
  `stloc V, ..` (store side) and `ldobj(ldloca V)` -> `ldloc V` (load side) so
  ILInlining can fold them, and comparison-kind normalization against `ldnull`
  (`gt`/`le` on the right -> `ne`/`eq`, `lt`/`ge` on the left; `box T(arg)
  ==/!= ldnull` -> `arg ==/!= ldnull` for a type parameter `T`). Parameter
  names and string literals (`ldstr`) now come
  from the metadata (Param table / #US heap).
  `RemoveDeadVariableInit` drops dead stores to never-read variables: a
  variable flagged `RemoveIfRedundant` (by `RemoveInfeasiblePath`) or under the
  `RemoveDeadStores` setting, with no loads or addresses, has its stores dropped
  (a pure value goes with the store; an impure value is unwrapped so its side
  effect survives), and dead-copy chains collapse via a recompute fixpoint.
  31 of ~40 transforms ported. The switch-detection family is now complete in
  its core: `LongSet`/`LongInterval` (Util/, ported from LongSet.cs /
  Interval.cs) -- an immutable interval-set of longs whose complement is
  representable (unlike `std::set<int64_t>`) -- backs `SwitchSection::Labels`
  and is the prerequisite for the switch family (which computes value-set
  complements like `new LongSet(val).Invert()`).
  `SwitchAnalysis` (ControlFlow/, ported from SwitchAnalysis.cs) is the
  analysis helper SwitchDetection depends on: it reconstructs a C# switch
  compiled to if-statements (non-contiguous case labels) as a list of
  (LongSet labels, body) sections by walking the if-chain, handling
  `comp(V OP val)`, `comp((V - sub) OP val)` (AddOffset), bare `ldloc V`
  (all-except-0), `logic.not` unwrap, and an existing IL `switch(V +/- val)` --
  adapted to this port's if-as-final block model (the false arm is the next
  block in the container, with a synthesized-Branch section body).
  `SwitchDetection` (ControlFlow/, ported from SwitchDetection.cs) is the full
  transform: `SimplifySwitchInstruction` (de-dup sections branching to the
  same block, AdjustLabels for `switch(V +/- val)`, SortSwitchSections) is the
  static method the C# pipeline calls twice (1st pass from CFS, 2nd from
  ProcessBlock); `Run` iterates every container, and per block runs
  SwitchAnalysis then `UseCSharpSwitch` -- when the if-chain forms a switch,
  it builds a `SwitchInstruction` from the detected sections (cloning the
  bodies, since this port has no GC to keep them alive after the inner blocks
  are cleared), replaces the block's tail, drops the absorbed inner blocks,
  and sorts the sections; otherwise it runs `SimplifySwitchInstruction` (2nd
  pass). `UseCSharpSwitch` ports the full heuristic (the default-section
  lookup, the `ContainsILSwitch` early return, the `ifCount < intervalCount`
  guard, `AnalyzeControlFlow` + the CFG/LoopContext continue-break analysis,
  `IsSingleCondition` for single short-circuited conditions, and
  `SwitchUsesGoto` to avoid poor-quality switches with gotos), adapted to the
  if-as-final block model. The deferred pieces are `MatchRoslynSwitchOnString`
  (needs `SwitchOnStringTransform.MatchComputeStringOrReadOnlySpanHashCall`),
  `AddNullCase` (needs `NullableLiftingTransform.MatchHasValueCall`), and
  `InlineSwitchExpressionDefaultCaseThrowHelper` (needs `IMethod`/`IType`
  resolution). Gated on the `SparseIntegerSwitch` setting (default true).
  `NullableLiftingTransform` (Transforms/, a tested-but-not-yet-wired
  foundation) ports the static helper subset the next in-order transform
  (`SwitchOnNullableTransform`) and SwitchDetection's deferred `AddNullCase`
  consume: `MatchHasValueCall` / `MatchGetValueOrDefault` recognise
  `call get_HasValue(arg)` / `call GetValueOrDefault(arg)` on
  `System.Nullable<T>` by the call's resolved declaring type
  (`Call::DeclaringType`, a new `ITypePtr` the IL reader fills from the
  method token via `MetadataFile::ResolveMethodDeclaringType`, unwrapping a
  `ParameterizedType` to its generic definition's `KnownTypeCode`).
  `SwitchInstruction` gained `IsLifted`/`Type` and `SwitchSection` gained
  `HasNullLabel` (the `case null:` arm); the seed renders `case null:`.
  `SwitchOnNullableTransform` (Transforms/, ported from
  SwitchOnNullableTransform.cs) folds the C# compiler's two switch-on-
  `Nullable<T>` shapes into a single lifted `SwitchInstruction` with an
  explicit `case null:` arm: the legacy csc shape (`stloc tmp(ldloca V); stloc
  sw(call GetValueOrDefault(ldloc tmp)); if (!get_HasValue(ldloc tmp)) br
  nullCase; switchBlock { switch (ldloc sw) { ... } }`) and the Roslyn shape
  (`if (!get_HasValue(target)) br nullCase; switchBlock { stloc sw(call
  GetValueOrDefault(target)); switch (ldloc sw) { ... } }` or the inlined
  `switch (call GetValueOrDefault(target))`), each becoming a lifted
  `switch (ldloc V) { ...; case null: nullCase }`. Gated on `LiftNullables`
  (default true). Adapted to the if-as-final block model (the `br switchBlock`
  fall-through is the next block in the container; the switchBlock's switch is
  its `FinalInstruction`); the dead switchBlock stays in the tree (per D58 --
  `SortBlocks(deleteUnreachableBlocks)` is unsafe in this port) and only the
  edge counts are refreshed.
  `MatchInstruction` (Instructions/, a tested-but-not-yet-wired foundation)
  ports the C# `is`-pattern ILAst node the next in-order transform
  (PatternMatchingTransform) builds from isinst + null-test blocks: it
  evaluates TestedOperand, matches against CheckType (`is T`) / CheckNotNull
  (`is {}`) / neither (`is var`), stores the matched value into Variable (an
  IStoreInstruction -- ComputeVariableUsage counts it as a store), and
  evaluates to I4. The static `IsPatternMatch` helper (which the recursive-
  sub-pattern path consults) recognises a MatchInstruction, a Comp constant /
  relational pattern (gated on RelationalPatterns / PatternCombinators), a
  logic.not-wrapped pattern (this port's `comp(eq, X, ldc.i4 0)` shape), and a
  `string.op_Equality(x, "lit")` string-constant pattern. The ILAstToCSharp
  seed renders a MatchInstruction condition as `expr is var x` / `is T x` /
  `is {} x` / `is T`. The four pattern-matching settings (PatternMatching,
  RecursivePatternMatching, PatternCombinators, RelationalPatterns, all
  default true) are added. The deconstruct patterns (IsDeconstructCall /
  IsDeconstructTuple + an IMethod operand) are deferred (need IMethod +
  TupleType). No pipeline transform constructs MatchInstructions yet, so
  `--csharp` output is unchanged; the seed's MatchInstruction case is exercised
  by the unit tests, not the corpus.
  `PatternMatchingTransform` (Transforms/, ported from
  PatternMatchingTransform.cs) detects the C# 7.0 `is` patterns the Roslyn
  compiler emits for `expr is T x` (a type test plus a variable capture) and
  rewrites the isinst + null-test block tail into a single MatchInstruction
  condition. Two shapes: `PatternMatchValueTypes` folds `if (isinst T(x) ==
  null) br falseBlock; br unboxBlock` (where unboxBlock is `stloc V(unbox.any
  T(x))`) into `if (match.type[T].notnull(V = x)) br unboxBlock; br falseBlock`,
  and `PatternMatchRefTypes` folds `stloc V(isinst T(x)); if (V == null) br
  falseBlock; br trueBlock` into `if (match.type[T].notnull(V = x)) br
  trueBlock; br falseBlock`. The `== null` / logic.not direction swaps the two
  arms; the double-store form (`stloc s(isinst); stloc v(ldloc s); if (!
  comp(s != null)) ...`) and the boxed-generic form (`isinst T(box U(x))`) are
  handled. Adapted to the if-as-final block model (the if is the block's
  FinalInstruction; the false arm is the next block in the container), so the
  arm swap tracks the two target blocks and keeps the null path positional
  when it is the fall-through, materialising an explicit FalseInst otherwise.
  `CheckAllUsesDominatedBy` is implemented with a tree walk (collect every use
  of the variable) rather than the C#'s per-variable load/store lists. Gated on
  `PatternMatching` (default true); the capture variable becomes a
  `PatternLocal`. The recursive property sub-patterns (`expr is C { P: var x }`,
  which need `DetectExitPoints.CompatibleExitInstruction` and `PropertyOrFieldAccess`/
  IMember) are deferred -- the top-level `is T` / `is T x` is produced without them.
  `LockTransform` (Transforms/, ported from LockTransform.cs) detects the C#
  `lock` statement's Monitor.Enter/Exit try/finally pattern and folds it into a
  `LockInstruction` (`lock (expr) { body }`). Two families are ported: the
  no-flag MCS / V2 shapes (a straight `call Exit` finally, which fire only on
  mono-compiled assemblies) and the flag-based Roslyn shape (`stloc
  obj(lockExpr); stloc flag(ldc.i4 0); .try { call Enter(ldloc obj, ldloca
  flag); body } finally { if (!flag) leave; call Exit(ldloc obj); leave }`),
  which is the dominant .NET Framework 4 / Roslyn codegen. Adapted to this
  port's if-as-final block model: the C# carries the flag finally's `if (flag)
  { Exit }` and the endfinally `leave` as two non-terminals of one block, but
  this port's ConditionDetection leaves the brfalse-skip as a separate first
  block, so the flag finally is TWO blocks (`if (comp(eq,flag,0)) leave` then
  `call Exit; leave`) and `MatchExitBlockFlag` matches that shape. The stloc
  obj / stloc flag sit either in the TryFinally's own block (the C# same-block
  indexing) or, when CFS did not merge the EH wrapper with the preceding
  block (the dominant mscorlib case: the fall-through branch into the
  TryFinally resolves to the try entry inside the try container, not the
  wrapper, so the wrapper has IncomingEdgeCount==0 and CFS leaves the stlocs in
  a separate preceding block), in the preceding block; the preceding-block
  fold absorbs the TryFinally's block final into the preceding block and drops
  the now-empty TryFinally block (the preceding block's `br` into the try
  would dangle into the lock body once the TryFinally becomes a LockInstruction,
  so it is discarded -- the LockInstruction subsumes the try entry), moving
  the dropped block to a graveyard so the container iteration stays valid.
  The EH-reached try/finally entry single-predecessor checks are ==0 here
  (the port does not count the container-entry edge, D59). Gated on
  `LockStatement` (default true). The flag-based shape fires 517 times on
  the full mscorlib corpus (the CLI emits 517 `lock (...)` statements, up from
  0 with only the no-flag shapes); the V4 / V4YieldReturn flag shapes (inline
  `stloc obj` as the Enter arg) and the single-block `if(flag){Exit}` finally
  are deferred (0 occurrences in mscorlib).
  `UsingTransform` (Transforms/, ported from UsingTransform.cs) detects the
  C# `using` statement's IDisposable try/finally pattern and folds it into a
  `UsingInstruction` (`using (resource) { body }`), building on the D74
  `UsingInstruction` node. Runs after ConditionDetection and LockTransform in
  the BlockILTransform post-order set (the `GetILTransforms()` position). Three
  finally shapes are matched, all probed on the .NET Framework 4 mscorlib
  corpus before implementation (the if-as-final block model diverges from the
  C# structure, so the real shape is discovered empirically, per the D73
  precedent): Shape A (reference type, two-block -- the C# single-block
  `if (obj != null) Block { Dispose }; leave` becomes TWO blocks here, the
  `if (obj == null) leave` skip then the `call Dispose(ldloc obj); leave`, the
  inverted brfalse form), Shape B (struct / ref-struct, one-block
  `call Dispose(ldloca obj); leave`, no null check), and Shape C (reference
  type, two-block with the isinst-temp `stloc temp(isinst IDisposable, ldloc
  obj); if (ldloc temp == null) leave; call Dispose(ldloc temp)`). The resource
  `stloc obj(resource)` sits in the PRECEDING block (the dominant mscorlib case
  -- CFS does not merge the EH wrapper for this shape, the same BlockBuilder
  quirk as the flag-based lock: the fall-through branch into the TryFinally
  resolves to the try entry inside the try container, not the wrapper, so
  `FindResourceStore` accepts a branch target that is the wrapper OR a block
  inside the TryFinally); the same-block case is also handled. The preceding-
  block fold puts the UsingInstruction in the preceding block, absorbs the
  TryFinally's block final (the after-using continuation), discards the `br`
  into the try, and drops the now-empty TryFinally block to a graveyard.
  `Run` re-gathers containers after each container that produced a fold -- a
  fold destroys the TryFinally's nested FinallyBlock container, so a pre-
  gathered container list would dangle (a use-after-free that crashed the first
  implementation on a 8000-method sweep). Fires 313 times on the full
  mscorlib corpus (the CLI emits 313 `using (...)` statements, up from 0);
  TransformUsingVB, TransformAsyncUsing (needs Await + IAsyncDisposable), the
  NullableOfT dispose, the ref-struct own-`Dispose` shape, and the
  MatchInstruction null-check are deferred.
  The CLI applies CFS + StObjToStLoc + ILInlining + InlineReturnTransform +
  RemoveInfeasiblePath + DetectPinnedRegions + DetectCatchWhenConditionBlocks +
  LdLocaDupInitObjTransform + EarlyExpressionTransforms + RemoveDeadVariableInit +
  CFS + SwitchDetection + SwitchOnNullable + LoopDetection + PatternMatching +
  ConditionDetection + LockTransform + UsingTransform + CachedDelegateInitialization + CachedReadOnlySpanInitialization + StatementTransform{ILInlining, ExpressionTransforms} + AssignVariableNames + RemoveUnreachableBlocks + RemoveRedundantReturn before the
  C# seed, so `fixed (...) { ... }`, `default(T)`, reconstructed `switch`
  statements, switch-on-nullable `case null:` arms, `is T x` patterns,
  `lock (...) { ... }`, `using (...) { ... }` statements, and
  `V = cond ? V1 : V2` ternaries (the conditional operator) now
  appear in the output. 31 of ~40 transforms ported (the StatementTransform
  orchestration + its first two children ILInlining and ExpressionTransforms;
  the remaining 14 per-statement children are deferred).
  `DelegateConstruction` (Transforms/, a tested-but-not-yet-wired
  foundation) ports the `MatchDelegateConstruction` helper the next in-order
  transform (`CachedDelegateInitialization`) and the later `DelegateConstruction`
  transform consume: it recognises a `newobj DelegateType(target, ldftn method)`
  (a `Call` with the new `IsNewObj` flag, 2 args, the second an ldftn/ldvirtftn)
  whose declaring type's `Kind` is `Delegate` or `Unknown`, capturing the target,
  the delegate type, and the ldftn method name -- AND the `LdVirtDelegate` shape
  (the C# `case LdVirtDelegate`, which arises after `ExpressionTransforms.
  TransformDelegateCtorLdVirtFtnToLdVirtDelegate` folds a virtual delegate
  construction to an `LdVirtDelegate`), capturing the `Argument` (the target), the
  `MethodName` (the method), and the `Type` (the delegate type), with the same
  `Delegate`/`Unknown` final gate. The C# `MatchDelegateConstruction` is a
  `switch` over `NewObj`/`LdVirtDelegate`; both branches are now handled. `Call` gained an `IsNewObj`
  flag (set by the IL reader for `newobj`, distinguishing it from `call`/`callvirt`
  -- the C# models newobj as a separate `NewObj` node; this port reuses `Call`).
  The declaring-type `TypeKind` is now derived for resolved type references:
  `ResolveMethodDeclaringType` (and the signature decoder's `TypeDefOrRef`
  element decoder) build a `SimpleType` carrying the derived `TypeKind` (via
  `DeriveTypeKind` over the `TypeDef` row's flags + base) for non-known in-module
  types, so a delegate constructor's declaring type resolves to `Kind == Delegate`
  -- both the non-generic in-module constructor (MethodDef parent) and a generic
  instantiation like `System.Func<int>` (the `TypeSpec` path, whose generic
  definition now carries the derived kind); a non-known cross-assembly `TypeRef`
  falls back to `Unknown` (the C# `Kind == Unknown` allowance, faithful to the C#
  which also cannot resolve it without the full type system). Known framework types
  keep their `KnownType` so the `KnownTypeCode` consumers (`NullableLifting`) are
  unaffected. An 8000-method mscorlib sweep confirms the helper matches 59 real
  `newobj Delegate(.., ldftn ..)` sites (29 non-generic + 30 generic via TypeSpec);
  the ILAst invariant holds. The `AnonymousMethods` setting (default true, gating
  `CachedDelegateInitialization`) is added; the helper itself is unconditional.
  No pipeline transform consumes `MatchDelegateConstruction` yet, so `--csharp`
  output is unchanged; the foundation is exercised by the unit tests + the sweep.
  The IField metadata foundation (a tested-but-not-yet-wired foundation, the
  repeatedly deferred blocker for the field-cached delegate shapes and the
  next in-order `CachedReadOnlySpanInitialization`) is now in place: `LdFlda` /
  `LdsFlda` carry an `IsCompilerGeneratedField` flag and a `FieldToken` (the raw
  metadata token) that the IL reader populates from every field-access site via a
  new `MetadataFile::IsFieldCompilerGeneratedOrInCompilerGeneratedClass`, which
  mirrors the C# `NRExtensions.IsCompilerGeneratedOrIsInCompilerGeneratedClass` --
  the field's own `[CompilerGenerated]` custom attribute, or (recursively up the
  nesting chain via `TypeDef::EnclosingType`) its declaring type's. The
  `ArrayInitializers` setting (default true) gates `CachedReadOnlySpanInitialization`.
  No pipeline transform consults the flag yet, so `--csharp` output is unchanged.
  Next per `GetILTransforms()`:
  `CachedDelegateInitialization` (the next in-order transform after
  `UsingTransform` in the BlockILTransform post-order set) is now ported in the
  `WithLocal` subset: a local-cached delegate lazy init
  (`if (v == null) v = new Delegate(...)`; `<use v>`) collapses to the
  unconditional init, adapted to this port's if-as-final block model (the
  `IfInstruction` is the block's `FinalInstruction`, so the "next instruction"
  the C# reads as `inst.Parent.Children.ElementAtOrDefault(inst.ChildIndex + 1)`
  is the next block in the container -- the C# sibling-instruction lookup
  returns null for a block final, so the adaptation is essential) and the
  per-variable `StoreInstructions` list is replaced by a tree walk (the
  repeatedly deferred infrastructure piece). The field-cached / Roslyn / VB
  shapes (the IField metadata they need is now in place via the
  `IsCompilerGeneratedField` flag; the remaining work is the block-model
  adaptation and the per-variable store-list tree walk, plus a corpus probe of
  the real field-cached if/Block shape) and the temp-collapse
  (folding the cache temp into the use, which in this port's block model needs
  merging the host block with the next block) are deferred; the .NET Framework
  4 legacy csc corpus uses the field-cached shape, not the local one, so the
  WithLocal fold fires 0 times on mscorlib (the hand-built tests verify the
  rewrite, the sweep the invariant -- the DetectCatchWhenConditionBlocks /
  LdLocaDupInitObj / SwitchOnNullable precedent). `CachedReadOnlySpanInitialization`
  (the next in-order transform after `CachedDelegateInitialization`, ported from
  CachedReadOnlySpanInitialization.cs) collapses the compiler-synthesized lazy
  cache Roslyn emits for a ReadOnlySpan<T> from an array literal on frameworks
  without RuntimeHelpers.CreateSpan (`stloc V(ldobj ldsflda cache); if (V ==
  null) { stloc V(init); stobj(ldsflda cache, ldloc V) }`; single usage of V)
  into the unconditional init `stloc V(init); ...`, so a later array-initializer
  transform recovers the literal and the escaped <PrivateImplementationDetails>
  cache field disappears. A probe through the pre-pipeline (CFS + LoopDetection
  + ConditionDetection) confirmed this port's post-ConditionDetection shape IS
  the C# shape: ConditionDetection's TryInlineIfFallThrough inlines the body
  into the if's FalseInst, then TryInvertIfExit (the usage block is the next
  block) inverts it -- negating the condition to `comp(V == null)` and moving the
  body into the TrueInst as a Block (FalseInst null, fall-through to the
  usage) -- so the transform matches Equality + body in TrueInst. The one
  port-specific difference (the body Block carries a trailing Branch to the
  usage, which the C# body does not) is handled by dropping the goto when the
  body falls through to the next block (the common cache case), so no spurious
  `goto IL_XXXX` survives. The cache-field gate uses the D78
  `LdsFlda::IsCompilerGeneratedField` flag; two LdsFlda nodes are the same field
  by FieldToken (or FieldName). ReadOnlySpan<T> is absent from the .NET
  Framework 4 mscorlib corpus, so this fires 0 times on it (it fires on
  Roslyn-compiled / modern .NET with System.Memory); ported for faithfulness
  (the hand-built tests verify the rewrite, the sweep the invariant -- the
  DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNullable
  precedent). Gated on the `ArrayInitializers` setting (default true).
  `StatementTransform` (the next in-order block after
  `CachedReadOnlySpanInitialization`, ported from StatementTransform.cs) is the
  per-statement driver that ends the BlockILTransform post-order set: it walks
  every block's non-terminal instructions last-to-first and runs the interleaved
  per-statement transforms (ILInlining, ExpressionTransforms, TransformAssignment,
  ...) at each position with rerun mechanics -- a child may call
  `RequestRerun(pos)` to jump the driver back to `pos` and re-run all children,
  or `RequestRerun()` to re-run at the current position, so a transform that
  opens up a new inlining opportunity triggers the ILInlining child to fold it
  without a separate full-block pass. This iteration ports the orchestration
  (the `IStatementTransform` interface + `StatementTransformContext` + the
  `StatementTransform` IILTransform driver) and wires the first child,
  `ILInlining` (now also an `IStatementTransform` via a per-statement `Run`
  overload that loops `InlineOneIfPossible` at the given position) -- the C#
  pipeline's second inlining pass, which folds the single-use variables the
  intervening transforms (ConditionDetection / Lock / Using / CachedDelegate /
  CachedReadOnlySpan) created. Adapted to the port's if-as-final block model: the
  final lives separately (`FinalInstruction`, not in `Instructions`), so the
  per-statement positions range over the non-terminal `Instructions` only (the
  C# ranges over `Instructions` including the final, but the final is never a
  `StLoc`, so the C#'s first iteration at the final is a no-op -- starting at
  the last non-terminal is equivalent); the per-statement `ILInlining`'s
  `while (InlineOneIfPossible(block, pos))` loop re-checks `pos < size` after a
  removal shrank the block (the C# avoids this because the final lives in
  `Instructions`, so the last non-terminal is at `Count-2` and the shifted-in
  instruction stays in range). The driver gained a block-model compensation for
  an if-final-only block (no non-terminal `Instructions`, so `pos` starts at -1
  and the per-statement loop never enters): the C# carries the if as a non-
  terminal at `Instructions[Count-2]`, so the driver visits it and recurses into
  its condition; this port makes the if the `FinalInstruction`, so the driver
  now runs each child once with the sentinel `pos = -1` for such blocks so
  per-statement transforms that handle the if-final's condition fire (children
  that only handle non-terminal positions, like `ILInlining` which guards `pos
  >= 0 && pos < size`, no-op).
  `ExpressionTransforms` (the second child, ported from ExpressionTransforms.cs)
  is an `IStatementTransform` with a recursive `Visit` (the C# ILVisitor) that
  folds the self-contained `VisitComp` subset: `logic.not(comp op)` ->
  `comp(op.Negate)` (push negation into the comparison, `!(a == b)` -> `a != b`),
  `comp(x != 0)` -> `x` (drop the redundant inequality against 0 when the comp
  is in a condition slot or its left is a comp), and `comp.unsigned(left > 0)` /
  `<= 0` -> `comp(left != 0)` / `== 0` (an unsigned compare against 0 is a
  (non-)zero test). It also folds the `VisitIfInstruction` subset:
  `HandleConditionalOperator` (`if (cond) stloc A(V1) else stloc A(V2)` ->
  `stloc A(if (!cond) V2 else V1))`, the conditional/ternary operator --
  adapted to the if-as-final block model: the `StLoc` becomes a non-terminal in
  the block's `Instructions` and a `Branch` to the next block replaces the
  if-final (a `StLoc` is not a valid block final; the C# does an in-place
  `ReplaceWith` since the if is a non-terminal); both arms must be expression
  `Block`s (no `FinalInstruction`, the shape after `ConditionDetection`'s
  `TryDropCommonExit`) with a single `StLoc` to the same variable -- and the
  logic.and/or canonicalization (`if (cond) ldc.i4 0 else RHS` ->
  `if (!cond) RHS else ldc.i4 0`, the `&&`/`||` form normalization; the if
  stays the block's final so no block-model issue, an arm is `ldc.i4 N` either
  bare or a single-instruction expression `Block`) -- and the
  `match(x) ? true : false -> match(x)` fold (a conditional whose condition
  is a pattern match -- `MatchInstruction.IsPatternMatch`, ported in D70 -- and
  whose arms are `ldc.i4 1` / `ldc.i4 0` is redundant, since the
  MatchInstruction / Comp constant pattern already evaluates to 1/0; the if is
  replaced by the condition -- an in-place `ReplaceWith` when the if is a
  sub-expression value, or the match becomes a non-terminal + a `Branch` to the
  next block replaces the if-final when the if is a block's `FinalInstruction`,
  the same block-model adaptation `HandleConditionalOperator` uses). `IsInConditionSlot` is
  ported via `Parent` + `ChildIndex` (the port has no `SlotInfo`); the float
  guard is approximated by the operands' `StackType` (the port's `Comp` carries
  no `InputType`); the C# `UnwrapConv` of the right operand is approximated
  (the port's `Conv` carries no `Kind`). Its `Run` visits the statement at
  `pos` and the if-final (via `VisitIfInstruction`, which visits the arms,
  runs `HandleConditionalOperator`, the canonicalization, and the
  match-true-false fold, then the
  condition) at `pos == size-1` (the port's equivalent of the C# visiting the
  if at `Count-2`), so `if (comp(x != 0))` -> `if (x)` and the ternary fold
  fire on if-final blocks -- the CLI output now shows `if (array.Length)`
  (from `comp(ldlen != 0)` -> `ldlen`) and `if (!value)` (the kept logic.not
  of a bool param), and `V = cond ? V1 : V2` ternaries (the conditional
  operator) replace the `if/else`-over-the-same-temp pairs csc emits. The
  match-true-false fold fires 12 times on the .NET Framework 4 mscorlib corpus,
  all on `Comp` constant-pattern conditions (`if (x == 5) 1 else 0` -> `x == 5`)
  -- the `MatchInstruction` case fires 0 (PatternMatchingTransform fires 0 on
  the corpus); faithful to the C# (its `IsPatternMatch` treats a
  Comp-with-constant-right as a constant pattern). It also folds the
  `VisitBox` rewrite: `box ref-type(arg)` -> `arg` (for a reference type,
  `box` is a no-op; the `ResultType` guard -- the arg is stack-type `O`, the box
  is `O` -- protects a value type's box, whose arg is `I4`/`I8`/.., from a
  mis-fire, and `IsReferenceType` (a new shared, faithful
  `Decompiler/TypeSystem/TypeUtils.hpp` helper returning a tri-state
  `optional<bool>`, promoted from the file-local PatternMatchingTransform copy)
  returns `nullopt` for the uncertain kinds -- TypeParameter/ByRef/Pointer/
  Unknown/... -- so a `box T(arg)` over a generic T stays). The fold fires 42
  times on the .NET Framework 4 mscorlib corpus (a real-corpus ILAst-cleaning
  transform, not faithfulness-only); the seed already renders `box(arg)` as
  `arg` for all boxes, so the CLI output is unchanged and the value is ILAst
  cleanliness for the future real back end. It also folds the
  `VisitLdElema`/`VisitNewArr` `CleanUpArrayIndices` rewrite: drop the redundant
  `conv.i` (or `conv.ovf.i`) widening of an array index -- a Conv whose
  ResultType is `I` (native int) and whose Kind is `SignExtend`, `ZeroExtend`,
  or a checked `Truncate` (Kind == Truncate && CheckForOverflow) -- replacing
  the conv with its argument (the index is implicitly native-int in C#). An
  unchecked Truncate (conv.i from I8) is a real truncation and is kept. This
  required porting the faithful `ConversionKind` model for the Conv node (the
  recurring blocker D84 identified): new `Decompiler/TypeSystem/Sign.hpp`,
  `Decompiler/IL/PrimitiveType.hpp` (the PrimitiveType enum distinguishing the
  signed/unsigned sizes the StackType lattice collapses -- I1/U1/I4/U4/I8/U8/
  I/U/R4/R8/R -- plus GetStackType/IsIntegerType/IsFloatType), and
  `Decompiler/IL/ConversionKind.hpp` (the ConversionKind enum + a faithful
  `GetConversionKind` porting Ecma-335 Table 8); the Conv node now stores
  Kind/InputType/InputSign/TargetType/CheckForOverflow and the IL reader emits
  the per-opcode (PrimitiveType, checkForOverflow, sign) the C# ILReader does.
  The fold fires 1 time on the 8000-method mscorlib sweep (the legacy csc mostly
  emits ldelema with bare I4 indices; it fires more on Roslyn/64-bit-indexed
  code), so it is a real-corpus ILAst-cleaning transform, not faithfulness-only.
  The D81 `IsLdcI4ZeroMaybeConv` approximation is retired in favor of a faithful
  `UnwrapConv` (sign/zero-extending convs around the 0 now peel by Kind). It also
  folds the `VisitConv` `conv.r.un` combining rewrite: `conv.r4(conv.r.un(x))`
  / `conv.r8(conv.r.un(x))` -> `conv.r4.un(x)` / `conv.r8.un(x)`. IL `conv.r.un`
  does not indicate whether to convert the target to R4 or R8, so the C#
  compiler usually follows it with an explicit `conv.r4` or `conv.r8`; the two
  conversions combine into a single `Conv` carrying the inner `conv.r.un`'s
  input type/sign but the outer's R4/R8 target (the C# checks
  `inst.TargetType.IsFloatType() && inst.Argument is Conv conv && conv.Kind ==
  IntToFloat && conv.TargetType == R`; the port's Conv constructor derives the
  InputType from the argument's ResultType, which equals the inner's InputType,
  and `IsLifted` is dropped -- no nullable-lifting model). VisitConv also folds
  the FIRST rewrite `conv.iN(ldlen array) => ldlen.iN(array)` (conv.i4/conv.i8/
  conv.i over a native-int `ldlen` folds into a single `LdLen(I4/I8/I, ..)` so
  the cast does not appear in the output -- the array length is already the
  target type). This required reconciling the LdLen model: the `LdLen` node now
  carries a `StackType resultType` field (I for the raw `ldlen` opcode, which
  pushes a native int; I4/I8 for the synthetic `ldlen.i4`/`ldlen.i8` forms the
  fold produces), faithful to the C# `LdLen(StackType, ILInstruction)` -- the
  reader now emits `LdLen(I, array)` for `ldlen` (was a fixed I4). A checked
  `conv.ovf.iN(ldlen)` folds only when the `AssumeArrayLengthFitsIntoInt32`
  setting is on (the array length fits in int32). The `conv.iN(ldlen)` fold
  fires thousands of times across the 8000-method mscorlib sweep (csc emits
  `ldlen; conv.i4` for every `array.Length` use), so it is a real-corpus
  ILAst-cleaning transform (the CLI output now has no `(int)array.Length`
  casts -- the conv is folded into the ldlen before rendering). The `conv.r.un`
  combining fold fires 1 time on the 8000-method sweep (the legacy csc emits
  `conv.r.un` rarely; it fires more on Roslyn-compiled / modern .NET). The
  VisitComp tail also folds the two `comp(... == 0)` equality/inequality special
  cases the C# `else if (rightWithoutConv.MatchLdcI4(0) &&
  inst.Kind.IsEqualityOrInequality())` handles: `comp(ldlen[I] ==
  conv.i(ldc.i4 0))` => `comp(ldlen.i4[I4] == ldc.i4 0)` (the special case
  where the compiler compares a raw native-int ldlen against a sign/zero-
  extended 0 instead of widening the ldlen -- the ldlen becomes I4 and the conv
  around the 0 is dropped) and the C++/CLI null comparison
  `comp(conv.i(ldloc obj) == conv.i(ldc.i4 0))` => `comp(ldloc obj == ldnull)`
  (an object pointer conv'd to native int and compared against a sign-extended 0
  folds to the plain object == null). Both fire 0 times on the .NET Framework 4
  legacy-csc mscorlib corpus (csc emits `conv.i4` after `ldlen` so the ldlen is
  already I4 by the time the comp sees it; and the C++/CLI object-pointer shape
  does not arise in C#-compiled code) -- ported for faithfulness, matching the
  D59/D60/D69 precedent. The
  remaining 11 per-statement children (DynamicIsEventAssignmentTransform,
  TransformAssignment, NullPropagationStatementTransform,
  TransformArrayInitializers,
  TransformCollectionAndObjectInitializers, TransformExpressionTrees,
  IndexRangeTransform, DeconstructionTransform, NamedArgumentTransform,
  RemoveUnconstrainedGenericReferenceTypeCheck, UserDefinedLogicTransform,
  InterpolatedStringTransform) and the `ILInlining` `AllowInliningOfLdloca`
  option (the ldloca-into-`addressof` path the C# second pass enables, which
  needs an `AddressOf` node + `IsGeneratedTemporaryForAddressOf` +
  `ClassifyExpression`) are deferred, as are the rest of `ExpressionTransforms`
  (the NullableLifting call, the Call/NewObj/LdObj/StObj/StLoc
  `HandleCompoundAssign`, the remaining VisitIfInstruction pieces
  (NullableLifting, UserDefinedLogic,
  `TransformDynamicAddAssignOrRemoveAssign`), the SwitchExpression/Dynamic
  visit methods (the TryCatchHandler visit method -- TransformCatchVariable /
  TransformCatchWhen -- is now ported)). It folds the `VisitBinaryNumericInstruction`
  shift-size rewrite: `a << (b & 31)` / `a >> (b & 31)` -> `a << b` / `a >> b` --
  a shift's right operand masked with the bit-width minus one is redundant in
  C# (the shift already masks the count); the mask is dropped when it is the
  expected width (31 for I4, 63 for I8). The native-int (I) case --
  `sizeof(IntPtr) * 8 - 1` -- is deferred (needs SizeOf to carry an IType with
  GetStackType), as is the BitAnd/Boolean nullable-lift case (needs
  NullableLiftingTransform + InferType). It folds the `VisitTryCatchHandler`
  rewrite (TransformCatchVariable / TransformCatchWhen): the catch-variable copy
  csc emits at the start of every catch block -- `catch T; stloc V_0(ldloc E)`
  (the runtime pushes the caught exception into the `E_<offset>` exception stack
  slot; csc copies it into the local V_0 and the body uses V_0) -- is inlined so
  V_0 becomes the handler's catch variable (Kind=ExceptionLocal, name/type/
  generated-name copied from the E slot) and the copy stloc is dropped, giving
  `catch V_0 : T { <body using V_0> }`. The guard requires the E slot to be
  IsSingleDefinition with LoadCount == 1 (this port's ComputeVariableUsage counts
  the TryCatchHandler itself as the slot's single store -- the caught exception --
  so an E slot loaded once by the copy is IsSingleDefinition naturally, with no
  UsesInitialValue model needed), the copy target to be a Local/StackSlot, the
  copy's value to be `ldloc E` (optionally wrapped in an unbox.any for a type-
  parameter catch, unwrapped when the unbox.any's Type equals the catch type),
  and every use of the promoted local to stay inside the catch handler (a tree
  walk replaces the C# per-variable use lists). TransformCatchWhen additionally
  inlines a single-Leave catch-when filter condition (the filter entry block's
  leave is this port's FinalInstruction, not a non-terminal as in the C#). The
  "remove inlined UnboxAny" branch (when the copy was already inlined and the E
  slot's single load sits inside an UnboxAny) is deferred (needs catch-type
  resolution + per-variable load lists). The fold fires on the .NET Framework 4
  legacy-csc mscorlib corpus (28 promotions across 8000 methods -- a real-corpus
  transform, not faithfulness-only; the rest of the ~692 catches are either
  single-use copies ILInlining already inlined, or have escaped uses / unused
  variables). The `NullCoalescingInstruction` ILAst
  node (the C# `??` operator node -- ValueInst + FallbackInst children, a
  `NullCoalescingKind` Ref/Nullable/NullableWithValueFallback enum, an
  `UnderlyingResultType` field, DirectFlags ControlFlow, ResultType the
  fallback's, Flags `ControlFlow | valueInst | CombineBranches(None, fallback)`
  faithful to ComputeFlags) is ported, and the
  `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold in
  ExpressionTransforms.VisitCall is wired in: a 2-arg
  `call GetValueOrDefault(nullableValue, fallback)` on System.Nullable<T>
  with a pure fallback folds into a `NullCoalescingInstruction`
  (`NullableWithValueFallback`) whose ValueInst is
  `ldobj Nullable<T>(nullableValue)` and FallbackInst is the fallback; the
  2-arg `MatchGetValueOrDefault` helper extends the D68 1-arg form. A Call is
  always a value (never a block final), so the fold is a clean value-position
  ReplaceWith. The ILAstToCSharp seed renders the node as `value ?? fallback`.
  The 2-arg GetValueOrDefault is the Roslyn `a ?? b` lowering; the .NET
  Framework 4 legacy-csc mscorlib uses the 1-arg form + a separate if/ternary
  (not the 2-arg call), so the fold fires 0 times on that corpus (the 1-arg
  GetValueOrDefault appears ~47 times, rendered as calls) -- a faithfulness-only
  transform on this corpus that fires on Roslyn-compiled / modern .NET. The
  remaining VisitCall pieces (TransformArrayInitializers /
  InlineArrayTransform / TransformAssignment.HandleCompoundAssign) are
  deferred. The `NullCoalescingTransform` (the next per-statement child in the
  C# order, after the deferred DynamicIsEventAssignmentTransform /
  TransformAssignment) is now wired in: it constructs a
  `NullCoalescingInstruction` (`if.notnull(value, fallback)`, the C# `??`)
  from the reference-type `??` block tail
  `stloc s(value); if (comp(ldloc s == ldnull)) { stloc s(fallback) }` ->
  `stloc s(if.notnull(value, fallback))` (plus the temp-variable variant),
  adapted to the if-as-final block model (the if is the block's
  `FinalInstruction`, not `Instructions[pos+1]`; the if-final is replaced with a
  `Branch` to the next block -- the fall-through the if's null FalseInst
  represented); `ILInlining.InlineOneIfPossible` was exposed as a public free
  function so the transform can call it after the fold (matching the C#). The
  throw-expression cases are partially ported: the reference-types `a ??
  throw ...` arm (the C# 7.0 form `stloc s(value); if (comp(ldloc s == ldnull))
  throw(arg) }` -> `stloc s(if.notnull(value, throw(arg)))`) is now wired in,
  gated on the `ThrowExpressions` setting (DecompilerSettings, default true); the
  Throw node gained a mutable `resultType` field (faithful to the C# `internal
  StackType resultType = StackType.Void`) that the fold sets to O so the
  `NullCoalescingInstruction`'s ResultType (the FallbackInst's, the Throw)
  matches the reference-type value. The value-types `a ?? throw ...` arm
  (the C# 7.0 `Nullable<T>` form `stloc v(value); if (!v.HasValue) throw;
  use(call GetValueOrDefault(ldloca v))` -> `use(if.notnull(value, throw))`) is
  now wired in via `TransformThrowExpressionValueTypes`, adapted to this port's
  post-ConditionDetection shape: a pre-pipeline probe confirmed this port's
  ConditionDetection INVERTS the early-exit pattern (the C# keeps
  `if (!v.HasValue) throw; use` with the use as a sibling at pos+2; this port
  inlines the use into the if's TrueInst Block and inverts the condition to the
  bare `call get_HasValue(ldloca v)`, with the throw in the fall-through block),
  so the fold finds the `GetValueOrDefault(ldloca v)` inside the TrueInst Block
  via `ILInlining.FindLoadInNext`, replaces it with a
  `NullCoalescingInstruction(NullableWithValueFallback, value, throw)`, and
  inlines the TrueInst Block back into the host block (removing the stloc and
  replacing the if-final); the dead throw block is left with a Leave
  placeholder final (a valid but unreachable block, per the don't-delete-
  unreachable-blocks convention). The shared prerequisite
  `ILInlining.FindLoadInNext` (with the `FindResultType` / `FindResult` types)
  is exposed as a public free function in `ILInlining.hpp`, faithfully returning
  `Found` for both `LdLoc(v)` and `LdLoca(v)` (the C# returns `Found` for both;
  the prior port returned `Stop` for `LdLoca(v)`); `InlineOneIfPossible` gates on
  the found load being an `LdLoc` to preserve the deferred ldloca-into-addressof
  behavior. The hoisted-constructor-argument null guard (the last remaining
  NullCoalescingTransform target) is now fully unblocked on the prerequisite
  side: all three pieces the fold consults are in place as tested-but-not-yet-
  wired foundations -- `ILFunction::IsConstructor` / `IsStatic` (pre-resolved by
  the IL reader from the MethodDef flags/name via
  `MetadataFile::GetMethodDefKindInfo`, faithful to the C#
  `MetadataMethod.SymbolKind == Constructor` and `MethodAttributes.Static`; the
  gate is `IsConstructor && !IsStatic`), the exposed
  `ILInlining.FindLoadInNext`, and now `ILInlining.IsInConstructorInitializer`
  plus its dependencies: a per-instruction ILRange (`StartILOffset` /
  `EndILOffset` on the `ILInstruction` base, populated centrally by both IL
  reader decode loops via a `TagCreatedRange` helper that tags the one
  instruction each opcode created with its `[start, pos)` span),
  `ILFunction::ChainedConstructorCallILOffset` (the lazy, cached offset of the
  first `: base(...)` / `: this(...)` `Call` -- not newobj, short name `.ctor`,
  reference-type `DeclaringType`, parent a Block -- or -1), and
  `ILFunction::RegisterVariable`. The full fold is now ported
  (`TransformHoistedConstructorArgumentNullGuard`, the C# 7.0 `arg ?? throw ...`
  form for a constructor argument that is evaluated more than once): the
  compiler hoists `if (comp(ldloc param == ldnull)) throw(...)` in front of the
  chained `: base(...)` call; the fold replaces the guard with
  `stloc temp(if.notnull(ldloc param, throw))`, redirects the parameter's first
  use (inside the chained call's arguments) to `temp`, and `InlineOneIfPossible`
  moves the coalescing into the call argument. A pre-pipeline probe confirmed
  this port's ConditionDetection INVERTS the early-exit pattern (the C# keeps
  the guard as a non-terminal with the use as a sibling; this port inlines the
  call into the if's TrueInst Block, negates the condition to `comp(ne, param,
  ldnull)` Inequality, and puts the throw in the fall-through block), so the fold
  finds the `ldloc param` inside the TrueInst Block via `FindLoadInNext`,
  redirects it to a temp, builds the `stloc temp(nc)`, and inlines the TrueInst
  Block back into the host block (the block-model compensation for the use
  living inside the if rather than as a sibling). `ComputeVariableUsage` is
  re-run before `InlineOneIfPossible` so the fresh temp's counts are fresh (the
  C# avoids this via incremental variable-usage tracking). The reference-type `??` lowering is a
  Roslyn-era codegen pattern; a corpus probe across 8000 mscorlib methods found
  1797 `comp(eq, ldloc X, ldnull)` null-check ifs and 513 `comp(ne, ..)` but zero
  whose arm is a StLoc to the same variable, so the transform fires 0 times on
  the .NET Framework 4 legacy-csc corpus -- ported for faithfulness (the
  hand-built tests verify the rewrite, the sweep verifies the invariant).
  32 of ~40 transforms ported.
  The `Comp` nullable-lifting model (the foundation the next in-order
  `NullableLiftingStatementTransform` -- and the ExpressionTransforms
  VisitComp nullable-lifting pieces -- need) is now in place: the Comp node
  VisitComp nullable-lifting pieces -- need) is now in place: the Comp node
  carries `LiftingKind` (a `ComparisonLiftingKind` None/CSharp/
  ThreeValuedLogic enum faithful to Comp.cs), `InputType` (the underlying input
  StackType -- the operands' ResultType for an ordinary comparison, the inner
  type inside Nullable<T> for a lifted one), `IsLifted()` (`LiftingKind !=
  None`), `UnderlyingResultType()` (I4), and a `ResultType()` override that
  flips to O for the SQL-style ThreeValuedLogic lift (whose null result is
  itself a nullable value). A second `Comp` constructor takes the lifting kind
  + input type explicitly for the nullable-lifting machinery to build a
  lifted Comp; the existing 4-arg constructor derives `InputType` from the
  left operand and defaults `LiftingKind` to None, so every existing Comp
  stays non-lifted (ResultType stays I4) and the dump appends `.lifted[C#]` /
  `.lifted[3VL]` only when lifted (the default renders bare, unchanged).
  `NullableLiftingTransform::MatchCompOrDecimal` (extending the D68 helper
  subset) ports the Comp branch of the C# MatchCompOrDecimal -- a non-lifted
  IL `Comp` reports its Kind/Left/Right/IsLifted via the new `CompOrDecimal`
  struct -- AND the Decimal-operator branch: a Call to one of the 6 comparison
  operators on System.Decimal (op_Equality/op_Inequality/op_LessThan/
  op_LessThanOrEqual/op_GreaterThan/op_GreaterThanOrEqual), gated on the new
  `Call::IsOperator` flag (set by the IL reader from the `op_*` method name --
  the faithful equivalent of the C# `IMethod.IsOperator`'s SpecialName +
  name-prefix check) + the 2-arg gate + the name switch + a System.Decimal
  declaring type. Decimal has no IL Comp instruction, so a Decimal comparison
  lowers to an op_* call; the branch recognises these. The Decimal lift itself
  (LiftCSharpUser*, which builds a lifted user-defined operator via
  CSharpOperators.LiftUserDefinedOperator) is deferred -- needs the Phase 5 C#
  resolver; the recognition here is the foundation those consumers will consult
  (the existing LiftCSharp* paths bail safely for a Call CompOrDecimal -- no fold
  fires until the resolver-backed lift lands). The helper is the bridge the
  nullable-lifting lift machinery consults to recognise the C#-style lifted
  comparison shape;
  the full `NullableLiftingStatementTransform` (the `RunStatements` entry +
  `Lift`/`LiftNormal`/`LiftCSharp*` + the bool? `v == true` folds + the
  `ThreeValuedBoolAnd/Or` nodes + the `NullPropagationTransform` path `Lift`
  consults first) is the next in-order target the foundation unblocks; the
  simplest entry point -- `NullableLiftingTransform.Run(Comp comp)`, the
  ExpressionTransforms.VisitComp `a.GetValueOrDefault() == const` (const != 0)
  -> `comp.lifted[C#](a == const)` lift -- is now wired in (the first piece of
  that machinery to land). It recognises the VS2022.10 / Roslyn 4.10.0
  optimization that turns `a == 42` into `a.GetValueOrDefault() == 42` without a
  HasValue check and lifts it back: a non-lifted equality/inequality whose one
  side is `call GetValueOrDefault(arg)` on Nullable<T> and whose other side is a
  non-zero integer constant has that side replaced by `ldobj Nullable<T>(arg)`
  and is marked C#-lifted. Gated on `LiftNullables`. A Comp is always a value, so
  this is a clean child-slot swap (no block-model adaptation). It fires 0 times
  on the .NET Framework 4 legacy-csc corpus (a Roslyn-4.10 codegen pattern) --
  ported for faithfulness (the hand-built tests verify the lift, the sweep
  verifies the per-method monotone invariant).
  The remaining ILVariable-based NullableLifting helpers the next in-order
  `Run(IfInstruction)` entry (and the deferred LiftNormal/LiftCSharp* paths)
  need are now in place as a tested-but-not-yet-wired foundation: the
  ldloca-v overloads `MatchHasValueCall(inst, ILVariablePtr& v)` and
  `MatchGetValueOrDefault(inst, ILVariablePtr& v)` (`call get_HasValue(ldloca v)`
  / `call GetValueOrDefault(ldloca v)` -> v, the LdLoca's variable, extencing
  the D68 1-arg forms), `MatchNegatedHasValueCall(inst, const ILVariable* v)`
  (`logic.not(call get_HasValue(ldloca v))` -> v, the logic.not this port's
  `comp(Equality, X, ldc.i4(0))` brfalse shape), `MatchNullableCtor(inst,
  underlyingType, arg)` (`newobj Nullable<T>(arg)` -> (T, arg), a newobj Call
  with IsNewObj on a NullableOfT declaring type), `MatchNull(inst,
  underlyingType)` (`default(Nullable<T>)` -> T, a DefaultValue whose Type is a
  Nullable<T>), and the type-system helpers `GetUnderlyingTypeOfNullable(type)`
  (unwrap `ParameterizedType(KnownType(NullableOfT), {T})` -> T, the C#
  `NullableType.GetUnderlyingType`) and `IsKnownType(type, code)` (a KnownType
  compares its Code; a ParameterizedType is not itself a known type). The
  ldloca-v overloads take `ILVariablePtr&` (shared_ptr out) so they are
  overload-disjoint from the existing `ILInstruction*&`-out helpers (no
  shared_ptr/raw-pointer ambiguity, avoiding the D66 precedent). No pipeline
  transform consumes the new helpers yet (the D92 `Run(Comp)` is the only
  NullableLifting piece wired so far), so the CLI output is unchanged; the
  mscorlib sweep exercises the new helpers on real calls/newobj/DefaultValue
  sites and asserts they never misfire. The `MatchHasValueCall(inst,
  const ILVariable* v)` match-against-v overload (the C#
  `MatchHasValueCall(inst, v)` that checks the call is on the given variable,
  extending the D93 report-variable overload) is now in place too -- the
  `Run(IfInstruction)` bool? folds consult it.
  `NullableLiftingTransform.Run(IfInstruction)` -- the bool? equality folds
  (the second NullableLifting entry point, wired into
  ExpressionTransforms.VisitIfInstruction) -- is now in place: a conditional
  whose condition is `call GetValueOrDefault(ldloca v)` on a Nullable<bool>
  (the underlying type is Boolean) and whose arms are `v.HasValue` / a ldc.i4
  constant folds into a C#-lifted Comp (the D91 model):
    `v.GetValueOrDefault() ? v.HasValue : false`  ==> `v == true`
    `v.GetValueOrDefault() ? false : v.HasValue`  ==> `v == false`
    `v.GetValueOrDefault() ? !v.HasValue : true`  ==> `v != true`
    `v.GetValueOrDefault() ? true : !v.HasValue`  ==> `v != false`
  The `Lift` method's logic.not unwrap loop (swap the arms for each
  `comp(eq, X, 0)` peeled off the condition) is ported; the
  AnalyzeCondition/LiftNormal path (the multi-HasValue `&&` lift), the
  MatchCompOrDecimal/LiftCSharp* path (the comparison lift), the
  NullPropagation path, and the `&`/`|` on bool? path (ThreeValuedBoolAnd/Or)
  are deferred. Gated on `LiftNullables`. The Comp is a value (not control flow),
  so the block-model adaptation follows FoldMatchTrueFalse: a clean ReplaceWith
  when the if is a sub-expression value, or the Comp becomes a non-terminal + a
  Branch to the next block when the if is a block's FinalInstruction (the arms
  are values, so the bool? pattern fires only for a sub-expression value-if;
  the if-as-final after ConditionDetection has Branch arms that never match).
  The `MatchHasValueCall(inst, v)` calls use the match-against-v overload
  (`v.get()`) -- the report-variable overload would overwrite `v` (the C#
  distinguishes `out ILVariable v` (report) from `ILVariable v` (match) by the
  `out` modifier; this port distinguishes them by `ILVariablePtr&` (report) vs
  `const ILVariable*` (match), so an `ILVariablePtr` lvalue binds to the report
  overload and `v.get()` to the match overload). It fires 0 times on the
  .NET Framework 4 legacy-csc corpus (a Roslyn-era `bool?` codegen pattern) --
  ported for faithfulness (the hand-built tests verify the four folds, the
  sweep verifies the per-method monotone invariant). 34 of ~40 transforms
  ported.
  `ThreeValuedBoolAnd` / `ThreeValuedBoolOr` (Instructions/, a combined
  `ThreeValuedBoolInstructions.hpp` header mirroring the C# `LogicInstructions.cs`
  grouping) port the C# three-valued logic `&` / `|` on `bool?` (Nullable<bool>)
  nodes, and are now WIRED into the `&`/`|` on bool? path of
  `NullableLiftingTransform.Run(IfInstruction)`'s `Lift` (via
  `ExpressionTransforms.VisitIfInstruction.RunIfNullableLift`): both are
  `BinaryInstruction` (Left + Right inlineable), result `bool?` (StackType O),
  `IsLifted()` true / `UnderlyingResultType()` I4 (the C#
  `ILiftableInstruction` impl, added as methods per the Comp precedent -- this
  port has no ILiftableInstruction interface), DirectFlags None (no Flags
  override needed -- the base `None | Left | Right` equals the C# ComputeFlags),
  and the faithful dump mnemonics `3vl.bool.and(...)` / `3vl.bool.or(...)`. The
  seed renders them as `left & right` / `left | right` (faithful to the real
  back end's VisitThreeValuedBoolAnd/Or). The `&`/`|` on bool? fold handles
  three shapes: `condition ? v : (bool?)false` ==> `3vl.bool.and(condition, v)`,
  `condition ? (bool?)true : v` ==> `3vl.bool.or(condition, v)`, and the
  two-nullable `(n1.GVO || (!n2.GVO && !n1.HV)) ? v : v2` pattern (via the new
  `MatchLogicOr`/`MatchLogicAnd`/`MatchThreeValuedLogicConditionPattern`
  helpers) ==> `3vl.bool.or(v, v2)` (v==n1, v2==n2) or `3vl.bool.and(v2, v)`
  (v==n2, v2==n1). The condition/arms are detached from the if before it is
  destroyed (no GC); the block-model adaptation is shared via
  `ReplaceIfWithLiftedValue` (ReplaceWith for a sub-expression value-if, or the
  node becomes a non-terminal + a Branch final for a block-final if). The `&`/`|`
  on bool? codegen is a Roslyn-era pattern, 0 firings on the .NET Framework 4
  corpus; ported for faithfulness (hand-built tests verify the folds, the sweep
  verifies the per-method monotone non-decreasing ThreeValuedBool count).
  The `AnalyzeCondition`/`LiftNormal` `v.HasValue ? v : fallback => v ?? fallback`
  early-out (the section of `Lift` before the bool? equality folds, the C# order) is
  now ported: `AnalyzeCondition` walks a BitAnd tree of HasValue calls collecting
  the nullable vars (the gate LiftNormal consults); the early-out fires when there
  is exactly one nullable var, the true arm is not a NullableCtor, and the true arm
  is `ldloc` of that var, producing a `NullCoalescingInstruction(Nullable)` whose
  `UnderlyingResultType` is the underlying type's StackType (a new raw-pointer
  `StackTypeOf(const IType*)` overload). The `v.HasValue ? v.GetValueOrDefault() :
  fallback => v ?? fallback` `conv.nop.lifted` case (the LiftNormal section after
  the early-out) is also ported: when the true arm is a `call GetValueOrDefault(ldloca
  v)` on the single nullable var, the fold produces a fresh `ldloc v` (or
  `conv.nop.lifted(ldloc v)` when the underlying type differs from the GVO's return
  type -- a no-op I4->I4 lifted conv for Nullable<bool>, no conv for Nullable<int>)
  wrapped in a `NullCoalescingInstruction(NullableWithValueFallback)`. The Conv
  IsLifted model (a `bool IsLifted` field + a 6-arg lifted constructor + a
  `ResultType()` override returning `O` for a lifted conv + `UnderlyingResultType()`
  + a `.lifted` dump suffix, matching the D91 Comp nullable-lifting model) and the
  `GetSign`/`ToPrimitiveType`/`ToKnownTypeCode` type-system helpers (TypeUtils.hpp)
  are the foundation the conv.nop.lifted case consumes; a new
  `MatchGetValueOrDefault(inst, const ILVariable* v)` match-against-v overload
  recognises the true arm is a GVO call on the single nullable var. When
  `AnalyzeCondition` succeeds but neither the early-out nor the conv.nop.lifted
  case fires, the if stays as-is (the DoLift / LiftCSharpUserComparison rest of
  LiftNormal is deferred -- matching the C# which returns null). The `v.HasValue ? v
  : (bool?)false` folds to `v ?? (bool?)false` (the LiftNormal early-out) NOT to
  `v.HasValue & v` (the `&`/`|` fold), since the C# `Lift` order runs
  AnalyzeCondition/LiftNormal first. The `v.HasValue ? v : fallback` and `v.HasValue ?
  v.GetValueOrDefault() : fallback` patterns are Roslyn-era `Nullable<T> ?? T`
  lowerings that fire 0 times on the .NET Framework 4 corpus; ported for
  faithfulness (hand-built tests verify the folds + the block-final fold +
  negatives, the sweep verifies the per-method monotone invariants hold).
  The `DoLift`/`DoLiftBinary`/`NewNullable` core of `LiftNormal`'s else-branch
  (the general recursive lift over GetValueOrDefault/Conv/BinaryNumericInstruction/
  Comp/BitNot that the D99 BitSet foundation unblocked) is now ported and wired
  into `RunIfNullableLift`: `DoLift(inst, nullableVars)` builds a lifted
  Nullable<T> instruction from the GVO/Conv/BNI/Comp/BitNot shape without
  modifying the input, returning a `(Lifted, Bits)` pair whose `bits.All(0,
  nullableVars.Count)` is the "every nullable var contributed" gate; the five
  self-contained cases (GVO -> LdLoc, Conv -> lifted Conv, BitNot -> lifted
  BitNot [a NEW BitNot ILAst node with IsLifted/UnderlyingResultType],
  BinaryNumericInstruction -> lifted BNI via DoLiftBinary [the BNI gained
  IsLifted/UnderlyingResultType/a ResultType-override O + a `.lifted` dump
  suffix], the bool? operator! Comp -> a ThreeValuedLogic-lifted Comp) are
  ported; the Call user-defined-operator case is deferred. `DoLiftBinary`
  embeds a pure non-nullable operand via `NewNullable` (returns it unchanged for
  the UnknownType expected type, matching the C#); the pure operand is cloned
  via a `ClonePureExpression` helper (LdcI4/I8/F4/F8/LdNull/LdStr/LdLoc -- the
  ClonePureLoad set; an uncloneable pure expression makes DoLift bail
  conservative-correct). The wired LiftNormal else-branch wraps the lifted
  value per the isNullCoalescingWithNonNullableFallback / MatchNull gates
  (NullableWithValueFallback / Nullable / no-wrap), with UnderlyingResultType =
  exprToLift->ResultType(); `ReplaceIfWithLiftedValue` applies the block-model
  adaptation. The DoLift path is a Roslyn-era `Nullable<T>` expression-lifting
  codegen pattern that fires 0 times on the .NET Framework 4 corpus; ported for
  faithfulness (hand-built tests verify the 5 cases + the relevance-gate
  failure + 3 wired folds; the sweep verifies the per-method monotone
  invariants hold).
  The `MatchCompOrDecimal`/`LiftCSharp*` comparison-lift path (the section of
  `Lift` after the D97/D98/D100 LiftNormal pieces and before the bool? equality
  folds) is now ported and wired into `RunIfNullableLift`: `CompOrDecimal::
  MakeLifted` (the Comp branch) builds a C#-lifted Comp (the D91 model carrying
  the original comp's InputType/Unsigned); `LiftCSharpEqualityComparison`
  (Comp branch) handles the (in)equality cases -- the two-nullable hasValueComp
  case (`comp(eq, GVO(v1), GVO(v2)) ? comp(eq, HV(v1), HV(v2)) : false` ==> the
  C#-lifted `comp.lifted[C#](eq, ldloc v1, ldloc v2)`, DoLift both sides with a
  single-var list, gate on `leftBits[0] && rightBits[0]` + IsPure) and the
  single-nullable fall-back (a HasValue call -> LiftCSharpComparison with
  `[v]`); `LiftCSharpComparison` handles the relational cases -- the `!IsLifted`
  DoLiftBinary case (DoLiftBinary with both expected types UnknownType +
  MakeLifted, gated on IsPure + `bits.All`) and the `IsLifted` special case
  (legacy csc `num.GVO() == const && num.HasValue`, where Run(Comp) already
  lifted the comp; clone the operands via ClonePureExpression and MakeLifted).
  The equality swap (Inequality -> Swap(trueInst, falseInst)) is local to the
  equality branch; the relational `!(v1 != null && ...) : true` shapes wrap the
  lifted comp in a `Comp.LogicNot` (a non-lifted `comp(Equality, lifted, ldc.i4
  0)`, ported as a `MakeLogicNot` helper -- `Comp.LogicNot` does NOT fold, unlike
  `NegateCondition`). The user-defined-operator fall-backs
  (LiftCSharpUserEqualityComparison/LiftCSharpUserComparison, need
  Call.Method.IsOperator + CSharpOperators.LiftUserDefinedOperator) and
  IsGenericNewPattern (needs MatchDefaultValue + Call.Method.FullName +
  TypeKind) are deferred. The Decimal-operator branch of MatchCompOrDecimal
  (a Call to op_Equality/... on System.Decimal) is now ported -- it needs the
  `Call::IsOperator` flag (set by the IL reader from the `op_*` method name) +
  the 6-comparison-operator name switch + a System.Decimal declaring type; the
  Decimal LIFT (LiftCSharpUser*, the resolver-backed lift) stays deferred. The path is a Roslyn-era codegen pattern that fires 0
  times on the .NET Framework 4 legacy-csc corpus (the CLI `??`/lifted-comp
  counts are unchanged); ported for faithfulness -- hand-built tests verify the
  equality hasValueComp + fall-back folds, the relational + IsLifted folds, and
  the negatives (mismatched kind, non-HasValue operands, irrelevant-var gate,
  multiple-var IsLifted reject), and the 3 wired folds (equality, relational,
  relational-negated logic.not wrap); the sweep verifies the per-method
  monotone invariants hold (the C#-lifted Comp count is non-decreasing, the
  1-arg get_HasValue/GetValueOrDefault counts are non-increasing). 35 of ~40
  transforms ported.
  `NullableLiftingTransform.Run(BinaryNumericInstruction)` -- the
  VS2017.8 / Roslyn 2.9 `&&`-as-`&` optimization on bool operands, analysed
  as-if short-circuit -- is now ported and wired into
  `ExpressionTransforms.VisitBinaryNumericInstruction`'s `case BitAnd` arm
  (gated on both operands being Boolean-typed, the C# `InferType == Boolean`,
  via a conservative `IsBooleanValue` recognizer). The C# entry is a thin
  wrapper over `Lift` (`Lift(bni, bni.Left, bni.Right, new LdcI4(0))` then
  `bni.ReplaceWith(lifted)`), so the `Lift` body is extracted into a shared
  private member `LiftNullableCore(condition, trueInst, falseInst, trueSink,
  falseSink) -> unique_ptr<ILInstruction>` consumed by both the thin
  `RunIfNullableLift` (empty sinks; `ReplaceIfWithLiftedValue` on success) and
  the thin `RunBinaryNumericNullableLift` (`falseSink` = a fresh `LdcI4(0)`;
  `bni->ReplaceWith` on success -- a BNI is always a value, no block-model
  adaptation). KEY ownership divergence: the BNI `falseInst` is a fresh `LdcI4(0)`
  that is NOT a child of `bni` (no `Parent`), so a new `ConsumeArm(view, sink)`
  helper detaches an arm that may be in-tree (`DetachFromParent` / `TakeChild`) or
  a fresh node (`std::move(sink)`); for the `Run(IfInstruction)` caller the sinks
  are empty so `ConsumeArm` is behaviourally identical to `DetachFromParent` (the
  refactor is a no-op for the if case). The logic.not unwrap loop swaps both the
  views and the sinks. The BNI lift fires the same `LiftNullableCore` paths as
  the if case (the full `Lift` machinery is shared): the `MatchCompOrDecimal`
  equality hasValueComp case (`BitAnd(comp(eq,GVO(a),GVO(b)), comp(eq,HV(a),HV(b)))`
  ==> `comp.lifted[C#](eq, ldloc a, ldloc b)`), the bool? equality folds
  (`BitAnd(GVO(v), HV(v))` on Nullable<bool> ==> `v == true`), the LiftNormal
  conv.nop.lifted / DoLift wraps (which consume the fresh `LdcI4(0)` falseInst
  via `ConsumeArm`), and the `&`/`|` on bool? path. It is a Roslyn-era codegen
  pattern that fires 0 times on the .NET Framework 4 legacy-csc mscorlib corpus
  (ported for faithfulness, matching the D89/D90/D92/D94/D96/D97/D98/D100/D101
  precedent); the sweep already guards it via the existing per-method monotone
  invariants. 36 of ~40 transforms ported.
  `NullPropagationTransform` (Transforms/, from NullPropagationTransform.cs)
  ports the C# 6.0 null-conditional (`?.`) operator lowering -- `v != null ?
  v.AccessChain : null` -> `v?.AccessChain` -- as a static helper consulted first
  inside `NullableLiftingTransform.Lift` (before the LiftNullables-gated paths).
  The `IsProtectedIfInst` static helper (excludes logic.and/or in a condition
  slot from null-propagation), the `MatchNullableRewrap` helper, and the
  `Run(condition, trueInst, falseInst)` entry are ported and wired into
  `ExpressionTransforms.LiftNullableCore` (after the logic.not unwrap, before
  the LiftNullables gate, gated on the `NullPropagation` setting +
  `!IsProtectedIfInst`). `Run` recognises three condition shapes: the
  ReferenceType mode (`comp(ldloc v ==/!= null)`), the NullableByValue mode
  (`call get_HasValue(ldloca v)` -- the nullable used by value), and the
  NullableByReference mode (`call get_HasValue(ldloc v)` -- the nullable used
  by reference). The access chain analysis (`IsValidAccessChain`) is
  approximated: this port's Call carries no
  IsStatic/IsExtensionMethod/IsAccessor/IsGetter/ConstrainedTo metadata, so
  `IsInstanceCall` is the faithful gate (a static method cannot be `?.`-ed;
  newobj is excluded by `!IsNewObj`); the AddressOf/LdObjIfRef/Dynamic* cases
  are not modeled and conservatively rejected; the LdFld/LdFlda/LdLen/
  LdElema/NullableUnwrap cases are faithfully matched. `IntroduceUnwrap`
  wraps the receiver load at the end of the access chain in a `NullableUnwrap`
  (the D103 node), and the result is a `NullableRewrap` around the access
  chain. For the ReferenceType mode the unwrap wraps the varLoad (the
  `ldloc`/`ldloca testedVar`) directly; for the NullableByValue/NullableByReference
  modes the varLoad is the `call GetValueOrDefault(ldloca/ldloc testedVar)` at the
  end of the chain and the unwrap's Argument is a FRESH `ldloc testedVar` (the C#
  `new LdLoc(testedVar)`, with `refInput=true` for NullableByReference). The three
  modes are ported with the `ldnull`, `default(Nullable<T>)`, and `NullCoalescing`
  output cases. The `NullCoalescing` output case (`testedVar != null ?
  testedVar.AccessChain : nullInst` where the chain returns a non-nullable
  value type, not a by-ref-like type, and the NullableRewrap/NullableCtor
  was NOT stripped) folds into `testedVar?.AccessChain ?? nullInst` (a
  `NullCoalescingInstruction(NullableWithValueFallback)` wrapping a
  `NullableRewrap`). It is gated on `NullableLiftingTransform::IsNonNullableValueType`
  (a faithful port of `NullableType.IsNonNullableValueType` via the shared D84
  `IsReferenceType` helper + `GetUnderlyingTypeOfNullable`) and `IsByRefLike`
  (a permissive port -- a `ByReferenceType` / a `ParameterizedType` whose
  generic definition is `SpanOfT`/`ReadOnlySpanOfT` -> true; user-defined ref
  structs are not recognised without the `[IsByRefLike]` attribute). The return
  type the gate consults comes from a minimal `InferAccessChainType` helper
  (a `Call` -> the new `Call::ReturnIType`, populated by the IL reader from the
  method signature's return type; a `LdObj` -> the LdObj's `Type`; else
  nullptr) -- the faithful equivalent of the C# `nonNullInst.InferType(typeSystem)`
  for the access-chain roots the `?.` lowering produces. KEY FINDING: the
  `NullCoalescing` output case fires 78 times on the .NET Framework 4 legacy-csc
  mscorlib corpus (the legacy csc emits `v != null ? v.M() : fallback` where
  `v.M()` returns a value type as a plain if/else / ternary, which the
  `NullPropagation::Run` detects and folds to `v?.M() ?? fallback`) -- a
  real-corpus readability improvement (e.g. `obj?.GetHashCode() ??
  type.GetHashCode()`, `zone?.get_SecurityZone() ?? 0`). The UnconstrainedType
  mode (RunStatements only) and the
  `TransformNullPropagationOnUnconstrainedGenericExpression` pattern (a
  5-instruction block sequence) are deferred. The void-call subset of
  `RunStatements` is now ported via `NullPropagationStatementTransform` (an
  IStatementTransform child of StatementTransform): `if (testedVar != null) {
  testedVar.AccessChain(); }` folds into `testedVar?.AccessChain();` (a void
  NullableRewrap, the `?.` statement form) for all three modes (ReferenceType /
  NullableByValue / NullableByReference). The if is the block's FinalInstruction
  (this port's if-as-final model); the TrueInst is a Block with one instruction,
  the FalseInst is null (no else); the if-final is replaced with the void
  NullableRewrap as a non-terminal + a Branch to the next block (the
  fall-through). A NullableRewrap wrapper on the body instruction is stripped
  before the access chain analysis (the C# `bodyInst.MatchNullableRewrap`),
  and the stripped inner instruction is detached from the NullableRewrap (not
  the body Block) before the if is destroyed (no GC). The `?.` lowering (all
  modes) is a Roslyn-era (C# 6.0) codegen pattern that fires 0 times on the .NET
  Framework 4 legacy-csc mscorlib corpus, so the sweep asserts the ILAst
  invariant holds (not a fold count), matching the DetectCatchWhenConditionBlocks /
  LdLocaDupInitObj precedent. 39 of ~40 transforms ported.
  The `Call::IsOperator` flag (set by the IL reader from the `op_*` method
  name -- the faithful equivalent of the C# `IMethod.IsOperator`'s SpecialName
  + name-prefix check) and the `MatchCompOrDecimal` Decimal-operator branch
  (a Call to one of the 6 comparison operators on System.Decimal, gated on
  IsOperator + 2 args + the name switch + a Decimal declaring type) are now in
  place as a tested-but-not-yet-wired foundation: the recognition fires on the
  real mscorlib corpus (System.Decimal's comparison operators are in-module
  MethodDefs the reader marks IsOperator and MatchCompOrDecimal recognises), but
  the Decimal LIFT (LiftCSharpUser*, which build a lifted user-defined operator
  via CSharpOperators.LiftUserDefinedOperator) is deferred -- needs the Phase 5
  C# resolver; the existing LiftCSharp* paths bail safely for a Call
  CompOrDecimal, so no fold fires until the resolver-backed lift lands.
  The `IsGenericNewPattern` fold (the `(default(T) == null) ?
  Activator.CreateInstance<T>() : default(T)` => `Activator.CreateInstance<T>()`
  case, the last piece of `Run(IfInstruction)`'s MatchCompOrDecimal equality
  branch) is now ported: it consults the new `MatchDefaultValue` helper, the
  `Call::MethodName` (now resolved for a generic call via the
  `ResolveTokenToString` MethodSpec unwrap), the new `Call::TypeArgumentsCount`
  (the MethodSpec instantiation count, parsed from the 0x0A-marker
  MethodSpecSig blob), and `TypeKind::TypeParameter`. The `ResolveTokenToString`
  MethodSpec unwrap is a general improvement: a generic-instantiation call (a
  MethodSpec token, table 0x2B) now renders its resolved method name (e.g.
  `System.Array.IndexOf(...)`, `System.Runtime.InteropServices.Marshal.SizeOf(...)`)
  instead of the raw hex token -- the IL reader's `ResolveTokenToString` unwraps
  the MethodSpec to its underlying MethodDefOrRef, the same unwrap
  `GetMethodSignature` / `ResolveMethodDeclaringType` already use. The
  IsGenericNewPattern is a Roslyn-era codegen pattern that fires 0 times on the
  .NET Framework 4 legacy-csc mscorlib corpus (ported for faithfulness).
  The remaining `Run(IfInstruction)` paths (the LiftCSharpUserComparison rest
  of LiftNormal [needs the C# resolver's CSharpOperators.LiftUserDefinedOperator;
  the `Call::IsOperator` gate it consults is now in place], the Decimal lift
  itself [same resolver need], NullPropagation's remaining
  modes [UnconstrainedType + the unconstrained-generic pattern; the NullCoalescing
  output case is now ported]) are the subsequent in-order targets.
  `NullableRewrap` / `NullableUnwrap` (Instructions/, a tested-but-not-yet-wired
  foundation ported from NullableInstructions.cs) are the ILAst nodes for the C#
  null-conditional (`?.`) operator -- the next in-order transform
  (NullPropagationTransform, the `v != null ? v.AccessChain : null` -> `v?.AccessChain`
  lowering) builds them. `x?.Member` lowers to
  `nullable.rewrap(Member(nullable.unwrap(x)))`: NullableUnwrap is the `?.`
  deref (carries MayUnwrapNull so the surrounding rewrap can find it; has a
  RefInput flag and a ResultType field for the unwrapped type) and
  NullableRewrap is the join point (DirectFlags ControlFlow, strips the
  Argument's MayUnwrapNull + EndPointUnreachable and adds ControlFlow, ResultType
  O for a non-void Argument / Void for a `?.` statement). Neither is an
  IStoreInstruction (UnaryInstruction, no Variable), so no
  ComputeVariableUsage case is needed. The seed renders NullableRewrap as its
  argument (the rewrap is implicit in `?.`) and NullableUnwrap with a trailing
  `?` (a placeholder for the `?.` postfix). Not wired into any transform yet;
  the mscorlib sweep constructs a `?.` chain over real LdLoc operands. A
  corpus probe (D103) found the field-cached delegate shapes (the D77-deferred
  `WithField` / `RoslynInStaticWithLocal` / `RoslynWithLocal` / VB shapes) do
  NOT appear in the .NET Framework 4 mscorlib corpus in the C# expected form
  (0 if-finals comparing a CG `ldsfld` to `ldnull`, 0 `stobj(ldsflda CG,
  delegateConstruction)` stores); the corpus's 67 CG `ldsflda` nodes are
  Roslyn display-class cached-delegate fields (`<>c::<>9__N_M`) but in a
  brtrue/else shape with an extra result temp that the C# shapes do not
  directly match -- a larger, riskier slice deferred until the exact Roslyn
  variant is understood.
  `BitSet` (Util/, a tested foundation ported from BitSet.cs) is the
  fixed-capacity 64-bit-word bitset the `DoLift`/`DoLiftBinary` relevance
  analysis returns -- `bits.All(0, nullableVars.Count)` is the "every nullable
  var contributed to the lift" gate the D100 DoLift path and the D101
  MatchCompOrDecimal/LiftCSharp* comparison-lift path consult -- and
  the foundation the deferred DefiniteAssignment / Dominance /
  ReachingDefinitions analyses are built on. Faithful to the C# API
  (capacity rounding, `Any`/`All`/set-relation predicates/`Set`/`Clear`/
  `NextSetBit`/`SetBits`/`ReplaceWith`/`Clone`/`ToString`), with the C# 6-bit
  `ulong` shift masking replicated explicitly (a >=64-bit shift is UB in
  C++17) and a portable `TrailingZeroCount64` (C++17 has no
  `std::countr_zero`). Not wired into any transform yet; exercised by the unit
  tests.
  `NullableLiftingStatementTransform` (the block-tail nullable expression
  lift, the next per-statement child of the GetILTransforms()
  StatementTransform after NullCoalescingTransform and before
  NullPropagationStatementTransform) ports `NullableLiftingTransform.RunStatements`:
  the block-tail `if (!condition) { leave(default(Nullable<T>)) };
  leave(newobj Nullable<T>(expr))` lifts into a single leave carrying the lifted
  value, by calling the shared `Lift` (the C#
  `Lift(ifInst, ifInst.Condition, thenLeave.Value, elseLeave.Value)`) on the
  two leaves' values. The shared `Lift` core (`ExpressionTransforms::LiftNullableCore`,
  extracted in D50) is refactored to a public static method taking the settings
  explicitly so `NullableLiftingStatementTransform` (a separate IStatementTransform in
  NullableLiftingTransform.cpp) can call it without duplicating it. A
  pre-pipeline probe confirmed this port's ConditionDetection INVERTS the
  early-exit pattern (the C# carries the if as a non-terminal with the
  else-leave as the block's last instruction; this port makes the if the
  block's FinalInstruction, the newobj-leave is in the if's TrueInst Block, and
  the default-leave is the next block's FinalInstruction), so the fold matches
  the if-as-final, gets the then-leave from the TrueInst Block's FinalInstruction,
  the else-leave from the next block, calls `LiftNullableCore` on the two
  leaves' values, and on success sets the then-leave's value to the lifted value
  (via `SetChild`, not a direct `Value =` -- the D110 reparenting lesson),
  detaches the then-leave from its parent, and replaces the if-final with the
  then-leave. The next block (else-leave) becomes unreachable and is left in the
  tree (per the D58 convention). The block-tail nullable lift is a Roslyn-era
  codegen pattern that fires 0 times on the .NET Framework 4 legacy-csc mscorlib
  corpus; ported for faithfulness (the hand-built tests verify the rewrite, the
  sweep verifies the invariant). 39 of ~40 transforms ported.
  The `LdcDecimal` ILAst node (the System.Decimal constant, ported from the
  generated Instructions.cs) models System.Decimal's internal layout faithfully
  as a `DecimalValue` struct -- a 96-bit unsigned mantissa (lo/mid/hi), a sign,
  and a 0..28 scale (the count of digits right of the point) -- the same
  components the 5-arg `new decimal(lo, mid, hi, isNegative, scale)` constructor
  exposes, so the value round-trips without a native decimal type. Factory
  helpers build the value from the int/uint/long/ulong single-arg constructors
  (handling the INT32_MIN/INT64_MIN magnitude edges) and the Decimal.One/Zero/
  MinusOne named-constant fields; `ToString` formats the 96-bit mantissa via
  repeated divmod-by-1e9 and inserts the decimal point `scale` digits from the
  right (preserving trailing zeros, applying the sign, zero always unsigned);
  the seed renders the C# literal form with the trailing `m` suffix (`1m`/`0m`/
  `-1m`/`1.5m`). `ExpressionTransforms.VisitLdObj` (the next in-order visit
  method) ports `TransformDecimalFieldToConstant`: a static field load
  `ldobj(ldsflda System.Decimal::One/Zero/MinusOne)` folds into the
  corresponding `LdcDecimal` constant (the field is recognised by the resolved
  `LdsFlda::FieldName`, which carries both the declaring type and the field
  name -- the faithful equivalent of the C# `field.DeclaringType.IsKnownType(
  Decimal)` + `field.Name` gate). KEY FINDING: the fold fires on the .NET
  Framework 4 legacy-csc mscorlib corpus -- the CLI `--csharp` output now shows
  12 `m`-suffixed decimal literals (`0m`/`1m`/`-1m`, e.g.
  `System.Decimal.op_Equality(d1, 0m)`) with 0 residual `Decimal::One/Zero/
  MinusOne` field references (a real-corpus readability improvement, unlike most
  recent nullable-family faithfulness-only pieces). The sibling
  `EarlyExpressionTransforms.TransformDecimalCtorToConstant` (`newobj
  Decimal(int/uint/long/ulong)` and the 5-arg `newobj Decimal(int, int, int,
  bool, byte)` -> `LdcDecimal`, now unblocked by the node + the `FromInt32`/
  `FromUInt32`/`FromInt64`/`FromUInt64`/`FromBits` factories) ports the
  `EarlyExpressionTransforms.VisitNewObj` piece: a `newobj Decimal(...)` Call
  (modelled as a Call with `IsNewObj`, D76) whose declaring type resolves to
  `KnownType(Decimal)` folds into the corresponding `LdcDecimal` constant. The
  1-arg case dispatches on the first parameter's `KnownTypeCode` (Int32/UInt32/
  Int64/UInt64) -- the int/uint/long/ulong overloads share the resolved name
  `System.Decimal::.ctor`, so the parameter type (carried on a new
  `Call::ParameterIType` vector the IL reader populates from the method
  signature's `ParameterTypes`, mirroring the `Call::ReturnIType` precedent)
  distinguishes them, faithfully interpreting the constant's bit pattern as
  signed vs unsigned. The 5-arg case reads five `LdcI4` args and the C#
  `unchecked((byte)scale) <= 28` guard. KEY FINDING: the ctor fold fires on the
  .NET Framework 4 legacy-csc mscorlib corpus -- 9 folds across the full
  25315-method corpus (3 `newobj Decimal(int)` + 6 5-arg ctors), so the CLI
  `--csharp` output now shows 21 `m`-suffixed decimal literals (was 12 from the
  field fold), including the 5-arg-ctor-only large constants
  `System.Decimal.MaxValue = 79228162514264337593543950335m`,
  `MinValue = -79228162514264337593543950335m`, and
  `NearNegativeZero = -0.000000000000000000000000001m` (a real-corpus
  readability improvement). The Decimal lift (`LiftCSharpUserComparison`'s
  Decimal branch, needs the Phase 5 resolver's
  `CSharpOperators.LiftUserDefinedOperator`) is the subsequent in-order target.
  `ExpressionTransforms.TransformDelegateCtorLdVirtFtnToLdVirtDelegate` (the
  VisitNewObj piece of C# ExpressionTransforms -- this port models a newobj as
  a Call with `IsNewObj`, so the C# `VisitNewObj` dispatches from `VisitCall`)
  folds a virtual delegate construction `newobj DelegateType(target, ldvirtftn
  Method(target))` into an `LdVirtDelegate` (`ldvirtdelegate DelegateType
  Method(target)`), unifying the delegate target and the virtual method so the
  later `DelegateConstruction` transform handles both the NewObj and
  LdVirtDelegate shapes uniformly. The new `LdVirtDelegate` node (a
  `UnaryInstruction` with the target as its inlineable `Argument` child, an
  `ITypePtr Type` [the delegate type], and a `std::string MethodName` [the
  resolved method name -- the C# carries an `IMethod`; this port carries the
  resolved name string, matching the `LdFtn`/`LdVirtFtn` precedent], ResultType
  O, DirectFlags `None | MayThrow`) is faithful to the C# generated node;
  `OpCode::LdVirtDelegate` was pre-declared. The C# checks the declaring type's
  `Kind == Delegate`, the 2-arg shape, the 2nd arg is an `LdVirtFtn`, a pure
  target, and `Arguments[0].Match(ldVirtFtn.Argument)` (the newobj target and
  the ldvirtftn target are the same instruction); this port's `LdVirtFtn`
  carries only the method name (the IL reader discards the ldvirtftn target --
  it is not a tree child), so the structural-equality check is skipped -- the
  newobj's first argument is the only target in the tree, so the
  `LdVirtDelegate` carries it directly (C# never emits mismatched targets, so
  the approximation is safe). A `LdVirtDelegate` is a value, so the fold is a
  clean value-position `ReplaceWith` (no if-as-final block-model adaptation).
  The seed renders an `LdVirtDelegate` as `new DelegateType(target.Method)`
  (the real back end's `VisitLdVirtDelegate` folds the target and the virtual
  method into a `target.Method` method group). KEY FINDING: the fold fires on
  the .NET Framework 4 legacy-csc mscorlib corpus -- 6 folds across the
  8000-method sweep, on virtual delegate constructions such as `new
  System.Reflection.TypeFilter(__Filters.FilterTypeName)`,
  `new BeginChildrenCallback(this.BeginChildren)` (the CLI `--csharp` output
  has 0 `ldvirtftn` two-arg forms; every virtual delegate construction now
  folds to the cleaner `new DelegateType(target.Method)` form). 40 of ~40
  transforms ported.
  The `NumericCompoundAssign` ILAst node (Instructions/, a
  tested-but-not-yet-wired foundation) ports the C# compound-assignment `op=`
  family the next in-order per-statement child of StatementTransform,
  `TransformAssignment` (its `HandleCompoundAssign` folds
  `stloc V(binary.op(ldloc V, rhs))` into a `NumericCompoundAssign`
  `V op= rhs`), builds from -- ahead of that transform, following the
  MatchInstruction / UsingInstruction / NullCoalescingInstruction /
  ThreeValuedBoolAnd/Or precedent. The `CompoundAssignmentInstruction` abstract
  base carries the two children Target (slot 0, inlineable -- the store target)
  and Value (slot 1, inlineable -- the RHS), plus `CompoundEvalMode`
  (EvaluatesToOldValue for post-increment / EvaluatesToNewValue for compound /
  pre) and `CompoundTargetKind` (Address / Property / Dynamic). `NumericCompoundAssign`
  carries the `BinaryNumericOperator`, `CheckForOverflow`, `Sign` (the D85
  TypeSystem::Sign enum), `LeftInputType` / `RightInputType`, `UnderlyingResultType`,
  `IsLifted` (the ILiftableInstruction impl), and the `Type` operand (an
  `ITypePtr`, the store type); `ResultType = IsLifted ? O : UnderlyingResultType`;
  `DirectFlags = SideEffect` (+ MayThrow for Div/Rem/CheckForOverflow); the dump
  is `compound.assign.<op>[.ovf][.unsigned|.signed].<type>[.lifted].<suffix>(target, value)`.
  The C# constructor copies these from a `BinaryNumericInstruction`; the BNI
  now carries the faithful `Sign` (the TypeSystem::Sign enum) +
  `LeftInputType` / `RightInputType` (D127), so the node can copy them directly --
  the foundation tests still construct it explicitly, and the future
  `HandleCompoundAssign` transform will build it from the BNI's fields.
  `OpCode::NumericCompoundAssign` / `UserDefinedCompoundAssign` /
  `DynamicCompoundAssign` were pre-declared. `CompoundAssignmentInstruction` extends
  `ILInstruction` (not `IStoreInstruction`), so it needs no `ComputeVariableUsage`
  store-counting case. The `MakeAssignmentExpressions` (C# 2.0, default true) +
  `IntroduceIncrementAndDecrement` (default true) settings gate the transform;
  the nodes are unconditional. The seed renders a `NumericCompoundAssign` as
  `target op= value` (Address Target = an `LdLoca` rendered as the bare variable
  name; the post-increment/decrement as `target++`/`target--`); an `EmitStatement`
  case renders the statement form. No pipeline transform constructs these nodes
  yet, so `--csharp` output is unchanged (the seed already renders `V op= expr`
  from the StLoc pattern at the text level; this node is the ILAst-level
  representation the future `HandleCompoundAssign` produces for the real back
  end).
  The `TransformAssignment` foundation (the next in-order per-statement child
  of StatementTransform, after the `NumericCompoundAssign` node) adds the
  self-contained helpers the compound-assignment folds consult: the
  type-system queries `GetSize` / `IsSmallIntegerType` / `GetSign` on
  `PrimitiveType` (mirroring `ILTypeExtensions.cs`) and `GetSize` /
  `IsSmallIntegerType` / `IsCSharpSmallIntegerType` / `SwapSign` on `IType`
  (mirroring `TypeUtils.cs`), plus the `UnwrapSmallIntegerConv` transform
  helper (`TransformAssignment.{hpp,cpp}`, mirroring the C# helper of the
  same name) that peels the compiler's `conv` truncation to a small integer a
  compound assign to a small-integer local/field carries. Tested-but-not-
  yet-wired (the `MatchInstruction` / `UsingInstruction` / `NumericCompoundAssign`
  precedent); no pipeline transform constructs the helper yet, so `--csharp`
  output is unchanged.
  The BNI Sign / input-type reconciliation (D127, the contained model change
  D125 flagged as deferred to the TransformAssignment transform): the
  `BinaryNumericInstruction` now carries the faithful `Sign`
  (`TypeSystem::Sign` enum: `None` for the sign-independent add/sub/mul/
  and/or/xor/shl, `Signed` for div/rem/shr and the `_ovf` forms, `Unsigned`
  for the `_un` forms) + `LeftInputType` / `RightInputType` (derived from the
  operands' `ResultType` in the non-lifted constructors, taken explicitly in
  the lifted constructor) + a static `ComputeResultType` (Ecma-335 Table
  2/5/6/7) the faithful 5-arg reader constructor uses to compute the result
  stack type. The IL reader's `IL_BIN` macro now passes the per-opcode `Sign`
  + `CheckForOverflow` faithfully (matching the C# `BinaryNumeric(op,
  checkForOverflow, sign)` helper); the legacy `Signed` bool is derived
  (`Sign != Unsigned`) and kept for the dump's `.un` suffix + the
  `NullableLifting.DoLiftBinary` call site. The `NumericCompoundAssign` node
  can now copy these from a `BinaryNumericInstruction` directly (the C#
  constructor does), and the future `ValidateCompoundAssign` /
  `IsBinaryCompatibleWithType` consult `binary.Sign`. The mscorlib sweep
  confirms all three `Sign` flavours appear in the corpus and the
  `LeftInputType == Left.ResultType` / `RightInputType == Right.ResultType`
  invariant holds; `--csharp` output is unchanged.
  The implicit-truncation analysis (D128, the `IsImplicitTruncation` half of
  the `IsBinaryCompatibleWithType` gate D127 flagged as the natural next piece):
  `CheckImplicitTruncation` / `IsImplicitTruncation` + the
  `ImplicitTruncationResult` enum (ValuePreserved / ValueChanged /
  ValueChangedDueToSignMismatch) + a file-local `CommonImplicitTruncation` in
  `TransformAssignment.{hpp,cpp}` (mirroring `TransformAssignment.cs`), plus
  `HasOppositeSign(PrimitiveType)` in `PrimitiveType.hpp` (mirroring
  `ILTypeExtensions.HasOppositeSign`). Only small-integer targets can truncate
  (other truncations become explicit `conv`s in the ILReader); the analysis
  recurses into `LdcI4` (a range check via the target's small-integer
  `KnownTypeCode`), `Conv` (same-primitive-type preserved; same-size opposite
  sign + `HasOppositeSign` -> sign-mismatch; else changed), `Comp` (always
  0/1 -> preserved), `BitAnd`/`BitOr`/`BitXor` (recurse + `CommonImplicitTruncation`,
  short-circuiting on a plain `ValueChanged` side), and `IfInstruction` arms
  (recurse both + `CommonImplicitTruncation`). The C# else-branch consults
  `value.InferType(compilation)`; this minimal type system has no `InferType`,
  so it is approximated conservatively as `ValueChanged` (the C# Unknown
  fallthrough -- a compound assignment to a small integer with an unmodeled RHS
  does not fold). Tested-but-not-yet-wired (no consumer yet -- the
  `IsBinaryCompatibleWithType` validator is the subsequent iteration); `--csharp`
  output is unchanged.
  The `NumericCompoundAssign.IsBinaryCompatibleWithType` validator (D129, the
  C# static gate on `NumericCompoundAssign` the `TransformAssignment.
  HandleCompoundAssign` / `ValidateCompoundAssign` consults before building a
  `NumericCompoundAssign` from a `stloc V(binary.op(ldloc V, rhs))` pattern) is
  now ported as a tested-but-not-yet-wired foundation, completing the
  compound-assignment-validation prerequisites D127/D128 flagged. The validator
  (`CompoundAssignmentInstruction.cpp`, out-of-line since the node is
  header-only) ports the IsLifted / Unknown / Enum / IntPtr-UIntPtr / Sign /
  IsImplicitTruncation gates faithfully: the IsLifted gate unwraps a
  `Nullable<T>` store type via the D93 `GetUnderlyingTypeOfNullable` (the
  faithful equivalent of `NullableType.IsNullable` + `GetUnderlyingType`);
  the Enum gate allows Add/Sub/BitAnd/BitOr/BitXor and rejects other operators;
  the IntPtr/UIntPtr gate (a `KnownType(IntPtr)`/`(UIntPtr)` with `Kind !=
  NInt/NUInt`) rejects shifts and rejects the whole compound assign when
  `NativeIntegers` is off; the Sign gate consults `IsCSharpSmallIntegerType`
  (D126) + `GetSign(IType)` (D126) + the `signMismatchAllowed`
  (`Unsigned` + `ShiftRight` + `UnsignedRightShift`) gate; the
  `IsImplicitTruncation` gate (D128) rejects an RHS that would be truncated.
  The `Pointer` case is deferred-conservative: the C# consults
  `PointerArithmeticOffset.Detect` (needs the `SizeOf` node + `ComputeSizeOf`
  + `UnwrapConv` + `NormalizeTypeVisitor.TypeErasure.EquivalentTypes` -- a
  substantial deferred slice), so the port returns `false` for pointer types (no
  pointer compound assignment confirmed). This is behavior-preserving for the
  .NET Framework 4 mscorlib corpus (pointer arithmetic requires `unsafe`, absent
  from C#-compiled code); the Add/Sub vs other-operator distinction is kept
  structural so the `PointerArithmeticOffset` port can fill in the Add/Sub arm
  later. The `NativeIntegers` (C# 9.0) + `UnsignedRightShift` (C# 11.0)
  settings (both default true, matching `DecompilerSettings`) are added to
  `ILTransformSettings`. A null `settings` pointer is treated as the defaults
  (NativeIntegers/UnsignedRightShift permissive), matching the C# constructor
  `Debug.Assert(IsBinaryCompatibleWithType(binary, type, null))`. 21 new gtest
  cases (the 20 gate tests + an 8000-method mscorlib validator sweep exercising
  every gate on real binary operations + their resolved variable types);
  888/888 gtest cases pass (was 867); the CLI decompiles the full mscorlib
  module end-to-end (exit 0, no regression).
  `ILFunction::RecombineVariables` (D130, the C# `ILFunction.RecombineVariables`)
  is now ported as a tested-but-not-yet-wired foundation, resolving the
  "per-variable store-list tree walk [the repeatedly-deferred infrastructure
  piece]" D125/D126/D127/D128/D129 each listed as a remaining
  `TransformAssignment` prerequisite. This port has no per-variable instruction
  lists, so a tree walk (`ReassignUses`, descending into every child like
  `CountUsage`) replaces the C# list iteration and a manual count increment
  replaces the C# property-setter's list maintenance: every load/store/address
  of `variable2` is reassigned to `variable1`, `v1`'s counts grow by the
  reassigned uses, `v2`'s counts are zeroed, and `v2` is dropped from the
  function's `Variables`. It is the `finalizeMatch` the `IsMatchingCompoundLoad`
  LdLoc/StLoc branch calls so a split-fragment `stloc V(binary.op(ldloc V,
  rhs))` collapses to one variable for the `V op= rhs` fold. 5 new gtest cases
  (in `ILInlining_Test.cpp` alongside the other ILFunction API methods);
  893/893 pass; the CLI decompiles mscorlib end-to-end (exit 0, no regression).
  The `TransformAssignment` shared helpers (D131, the next-in-order piece
  D130 flagged) are now ported as a tested-but-not-yet-wired foundation:
  `IsCompoundStore` (the StLoc case -- a `stloc V(...)` whose Variable.Kind is
  Local/Parameter reports `storeType = V.Type` + the stored value; the StObj
  case needs `InferType` for the target's real type and the Call case needs
  `IsSameMember` + `IMethod` for the property-setter gate, both deferred),
  `IsMatchingCompoundLoad` (the LdLoc/StLoc case -- a load that is an `LdLoc`
  and a store that is an `StLoc` of the same variable per a faithful
  `ILVariableEqualityComparer` port -- same object, or split fragments with the
  same Kind + Index; StackSlot/PatternLocal always distinct -- reports a fresh
  `LdLoca` target, TargetKind Address, and a `RecombineVariables` finalizeMatch
  callback; a `forbiddenVariable` rejects a match that moves a store over a use;
  the LdObj/StObj case needs `IsDuplicatedAddressComputation` +
  `previousInstruction` and the getter/setter case needs `IMethod`/
  `AccessorOwner`, both deferred), and `ValidateCompoundAssign` (the full
  wrapper -- the D129 `IsBinaryCompatibleWithType` + the conv TargetType /
  CheckForOverflow match). The `CompoundFinalizeMatch` callback takes the
  ILFunction directly (the per-statement Run obtains it via a Parent-chain walk,
  since the StatementTransformContext carries no function handle) and captures
  the load/store shared_ptrs so it can call `RecombineVariables` after the
  match. 17 new gtest cases (4 IsCompoundStore + 7 IsMatchingCompoundLoad + 6
  ValidateCompoundAssign); 910/910 pass; the CLI decompiles mscorlib end-to-end
  (exit 0, no regression -- the helpers are not wired so the output is
  byte-identical to D130).
  The remaining field-cached delegate shapes (now unblocked on the IField side)
  still need the block-model adaptation + the per-variable store-list tree
  walk + a corpus probe; the async/iterator state machines
  (YieldReturnDecompiler/AsyncAwaitDecompiler), SplitVariables (needs
  reaching-definitions dataflow),
  DetectExitPoints + the full ConditionDetection (multi-pred join blocks),
  the PatternMatchingTransform recursive sub-patterns (DetectPropertySubPatterns /
  PropertyOrFieldAccess / CompatibleExitInstruction),
  HighLevelLoopTransform (while/for), and the remaining StatementTransform
  per-statement children (the full `TransformAssignment` -- the `HandleCompoundAssign` /
  `TransformPostIncDecOperator` / `TransformPostIncDecOperatorWithInlineStore` folds,
  now unblocked on the `NumericCompoundAssign` node + `UnwrapSmallIntegerConv` +
  the type-system helpers side; the transform itself still needs `IsCompoundStore`
  [the StLoc case is now ported (D131); the StObj case still needs `InferType` +
  the Call case needs `IsSameMember` + `IMethod`] / `IsMatchingCompoundLoad`
  [the LdLoc/StLoc case + its `RecombineVariables` `finalizeMatch` are now
  ported (D130/D131); the LdObj/StObj case still needs
  `IsDuplicatedAddressComputation` + `previousInstruction`, and the
  getter/setter `MatchingGetterAndSetterCalls` case needs `IMethod`/
  `AccessorOwner`] / `ValidateCompoundAssign` [now ported (D131) -- the D129
  `IsBinaryCompatibleWithType` + the conv TargetType/CheckForOverflow match;
  only the `Pointer` case's `PointerArithmeticOffset.Detect` is
  deferred-conservative] + the per-statement Run wiring [the
  `TransformPostIncDecOperatorWithInlineStore` local/StLoc-case fold is now
  PORTED (D132, wired into the StatementTransform pipeline after
  ExpressionTransforms / before NullCoalescingTransform, gated on
  IntroduceIncrementAndDecrement) -- it folds the local post-increment/decrement
  `stloc target(binary.op(stloc tmp(ldloc target), ldc.i4 1))` (a single
  non-terminal at `block.Instructions[pos]` whose `binary.Left` is the "inline
  store" `stloc tmp(ldloc target)` -- the compiler's temp that captures the
  old value and yields it) into `stloc tmp(NumericCompoundAssign.op.old(ldloca
  target, ldc.i4 1))` = `tmp = target++`. A corpus probe found the WithInlineStore
  shape (`binary.Left` a StLoc) does NOT arise on the .NET Framework 4 legacy-csc
  mscorlib corpus (0 `binary.(add|sub)(stloc(` across the full `--ilast`; the
  legacy csc emits the statement form `stloc V(binary.add(ldloc V, ..))` whose
  `binary.Left` is an LdLoc -- the deferred `TransformPostIncDecOperator` /
  `HandleCompoundAssign` shape), so the fold fires 0 times on the corpus --
  faithfulness-only (the expression-form `x = V++` post-increment is a
  Roslyn-era codegen pattern), matching the D59/D60/D69 precedent; the
  `IsImplicitTruncation` conservative approximation (no `InferType`)
  conservatively rejects the sign-swap + small-integer-tmp case (a faithfulness
  gap). The operator-call (`op_Increment`/`op_Decrement`) case (needs
  `UserDefinedCompoundAssign` + `Call.IsLifted`) and the
  `TransformInlineAssignmentStObjOrCall` / `TransformInlineAssignmentLocal` /
  `TransformPostIncDecOperator` (non-inline-store, the statement form that DOES
  arise on the legacy-csc corpus) / `TransformPreIncDecOperatorWithInlineStore`
  StObj/Call cases (need `InferType` / `IsSameMember` / `IMethod`) are the
  subsequent in-order targets], ...)
  are the subsequent in-order targets.
  The `TransformPostIncDecOperator` (non-inline-store, the two-instruction
  `stloc tmp(ldloc target)` + `stloc target(binary.op(ldloc tmp, 1))` local
  post-increment/decrement fold) is now PORTED (D133, wired into the
  StatementTransform Run dispatch after the WithInlineStore fold, matching the
  C# GetILTransforms() order). It folds the legacy-csc / Roslyn post-increment
  codegen into `stloc tmp(target++)` (or a bare `target++` when tmp is dead), and
  the store at the next position is removed; a `ComputeVariableUsage` recompute
  after the removal gives the correct `LoadCount` for the dead-tmp check (the C#
  InstructionCollection ref-counting cascades `Disconnected()` through the
  removed store to the `ldloc tmp` inside it, decrementing `tmp.LoadCount`;
  this port has no ref-counting, so the stored counts are stale). A corpus probe
  found 16 occurrences of the two-instruction shape across 8000 mscorlib methods
  (the WithInlineStore expression form fires 0 times), so the fold is a
  real-corpus transform -- the CLI `--csharp` output now has `++`/`--` operators
  (2446 lines) instead of the `V = V + 1` form, a readability improvement. The
  operator-call (`op_Increment`/`op_Decrement`) case (D136, now PORTED --
  builds a `UserDefinedCompoundAssign` from the operator Call). The
  `TransformInlineAssignmentStObjOrCall` / `TransformInlineAssignmentLocal`
  StObj/Call cases (need `InferType` / `IsSameMember` / `IMethod`) are the
  subsequent in-order targets.
  The `TransformPreIncDecOperatorWithInlineStore` (the local/StLoc
  pre-increment/decrement inline-store expression fold) is now PORTED (D134,
  wired into the StatementTransform Run dispatch after the PostIncDec folds,
  matching the C# GetILTransforms() order): it folds the local pre-increment
  `stloc outer(stloc target(binary.op(ldloc target, ldc.i4 1)))` (a double
  `IsCompoundStore` -- the outer store's Value is the inner `stloc target`, the
  inline-store expression form) into `stloc outer(NumericCompoundAssign.op.new(
  ldloca target, ldc.i4 1))` = `outer = ++target` (the C#
  `EvaluatesToNewValue` compound assign), eliminating the inner stloc (its
  variable recombined with the ldloc's via the `finalizeMatch`, a no-op when
  they are the same variable). The pre-increment expression form is a
  Roslyn-era codegen pattern that fires 0 times on the .NET Framework 4
  legacy-csc mscorlib corpus (the legacy csc emits the statement form), so
  the fold is faithfulness-only on this corpus -- matching the
  D59/D60/D69/D132 precedent.
  The `UserDefinedCompoundAssign` ILAst node (Instructions/, a
  tested-but-not-yet-wired foundation) ports the C# user-defined-operator
  compound-assignment node the operator-call case of the increment/decrement
  folds (and the deferred `HandleCompoundAssign` string.Concat case) build
  from -- ahead of that wiring, following the NumericCompoundAssign (D125)
  precedent. The node carries the resolved method name + declaring type +
  the return StackType (this port models a method by its resolved name +
  declaring type, like `Call`, since it has no `IMethod`); `ResultType` is the
  method's return StackType (`Method.ReturnType.GetStackType()`); `IsLifted`
  is hardcoded false (faithful to the C# `public bool IsLifted => false; //
  TODO`); `DirectFlags`/`Flags` add `SideEffect | MayThrow` (a user-defined
  operator call can throw); the dump is
  `compound.assign.userdefined.<suffix>(<method>, target, value)` (the
  family-consistent `compound.assign` root the NumericCompoundAssign node
  uses). The `IsIncrementOrDecrement(const Call*, settings)` and
  `IsStringConcat(const Call*)` static helpers (the C# takes the `IMethod`;
  this port takes the `Call`, which carries the method metadata) are the
  gates the folds consult on the operator Call before building the node:
  `IsIncrementOrDecrement` recognises a static `op_Increment`/`op_Decrement`
  (always) and `op_CheckedIncrement`/`op_CheckedDecrement` (gated on the new
  `CheckedOperators` C# 11.0 setting, default true -- the C#
  `settings?.CheckedOperators ?? true`); `IsStringConcat` recognises a static
  `string.Concat`. `Call` gained an `IsLifted` flag (default false -- the C#
  `CallInstruction.IsLifted` is `Method is CSharp.Resolver.ILiftedOperator`, a
  resolver concept this port has no resolver for; the inc/dec folds bail on a
  lifted operator call with `if (operatorCall.IsLifted) return false; // TODO`,
  so a default-false call never trips that bail). The ILAstToCSharp seed
  renders a `UserDefinedCompoundAssign` as the unary `target++`/`++target`/
  `target--`/`--target` (op_Increment/op_Decrement, postfix for
  `EvaluatesToOldValue`, prefix for `EvaluatesToNewValue`) or the binary
  `target op= value` (op_Addition -> `+=`, ..., `string.Concat` -> `+=`),
  matching the real back end's `VisitUserDefinedCompoundAssign`. The
  operator-call (`op_Increment`/`op_Decrement`) case of all three inc/dec
  folds (D136) is now WIRED into the per-statement Run: it builds a
  `UserDefinedCompoundAssign` from a 1-arg operator `Call` (gated on the bare
  `op_Increment`/`op_Decrement` name for the inline-store cases, or
  `IsIncrementOrDecrement` for the non-inline-store case which also accepts
  the checked variants; `!Call.IsLifted`). The fold fires 0 times on the .NET
  Framework 4 legacy-csc mscorlib corpus (faithfulness-only, matching the
  D59/D60 precedent), so `--csharp` output is unchanged; the hand-built tests
  verify the fold and an 8000-method sweep verifies the ILAst invariant holds.
  The `TransformInlineAssignmentLocal` (the inline-assignment-to-local fold,
  the next in-order `TransformAssignment` piece after D136) is now PORTED (D137,
  wired into the StatementTransform Run dispatch BEFORE the inc/dec folds,
  matching the C# GetILTransforms() order). It folds the compiler's stack-temp-
  to-local copy `stloc s(value)` (s a StackSlot) + `stloc l(ldloc s)` (l a
  Local/Parameter) into the inline-assignment expression `stloc s(stloc
  l(value))` so a later transform can treat the whole thing as an assignment
  expression. The implementation consults only self-contained helpers already
  in place: `MatchLdLoc` (the `nextInst.Value` must be an `LdLoc` of `s`),
  the D128 `IsImplicitTruncation` (the value must not be implicitly truncated
  for s's or l's type), `VariableKind` (s must be StackSlot, l Local/Parameter),
  and `StackTypeOf` (the C# `nextInst.Variable.StackType == StackType.Ref` reject
  -- `ILVariable` has no `StackType` member, so the port computes it via
  `StackTypeOf(Variable->Type.get())`). The block-model adaptation detaches
  `inst->Value` via `std::move` before `RemoveInstructionAt(pos)` destroys `inst`
  (no GC), then `ReplaceWith` the shifted `nextInst` (renumbered to `pos`) with
  the inline-assignment expression. The `Run` gate is updated to its faithful
  C# top-level gate (`!MakeAssignmentExpressions || !IntroduceIncrementAndDecrement`
  returns early -- the C# gates the whole Run on BOTH settings, not just
  `IntroduceIncrementAndDecrement` the D132-only port used). The fold fires 104
  times across the .NET Framework 4 legacy-csc mscorlib corpus (a real-corpus
  transform -- `EXPECT_GT(totalInlineAssignFolds, 0)`); the inc/dec fold's
  `NumericCompoundAssign`-OldValue count is UNCHANGED at 151 (the patterns are
  disjoint -- the inline-assignment's second store value is a load, the
  inc/dec's is a binary). The CLI `++`/`--` line count drops to ~2200 as a
  faithful consequence (the 104 inline-assignment folds change the ILAst shape
  the later ILInlining/rendering sees; the deterministic NCA-OldValue count is
  unchanged). The deferred `TransformInlineAssignmentStObjOrCall` StObj/Call
  inline-assign (needs `InferType` / `IsSameMember` / `IMethod`) and the
  `HandleCompoundAssign` StObj/Call compound-assign entry (blocked by the same
  StObj/Call `IsCompoundStore`/`IsMatchingCompoundLoad` prerequisites) are the
  subsequent in-order targets.
  The `UserDefinedLogicOperator` ILAst node (Instructions/, a
  tested-but-not-yet-wired foundation) ports the C# user-defined short-circuiting
  `&&` / `||` operator node (the `op_BitwiseAnd` / `op_BitwiseOr` overloads paired
  with `op_True` / `op_False`) ahead of the next in-order
  `UserDefinedLogicTransform` (the next per-statement child of `StatementTransform`
  after the wired `TransformAssignment` pieces). It extends `BinaryInstruction`
  (the `ThreeValuedBoolAnd/Or` precedent) with a `MethodName` + `MethodDeclaringType`
  `IMethod` stand-in (the `UserDefinedCompoundAssign` / `Call` precedent -- this
  port has no `IMethod`), `ResultType` O, `DirectFlags` MayThrow|SideEffect|ControlFlow,
  and a `Flags()` override porting the C# `ComputeFlags` (the Left is always
  executed, the Right only sometimes -- short-circuit -- so the Right combines via
  `CombineBranches(None, right.Flags)`, the `NullCoalescingInstruction` / `IfInstruction`
  precedent). The dump renders `user.logic Method(left, right)`. The ILAstToCSharp
  seed renders `op_BitwiseAnd` as `left && right` and `op_BitwiseOr` as `left || right`,
  faithful to the real back end's `VisitUserDefinedLogicOperator` (a
  `BinaryOperatorExpression` with `ConditionalAnd` / `ConditionalOr`). The node
  The `OpCode::UserDefinedLogicOperator` value was pre-declared in `OpCode.hpp`, so
  porting the node needed only the subclass header (the D95/D125 precedent).
  8 new gtest cases cover the node invariant/flags/ResultType/dump, the Left/Right
  typed slots + re-parenting, the `CombineBranches` Right-short-circuit flags
  propagation (a Branch Right over a pure LdLoc Left keeps the endpoint reachable),
  the seed rendering of `&&` / `||`, and an 8000-method mscorlib sweep constructing
  the node over real LdLoc operands. 990/990 tests pass; the CLI decompiles mscorlib
  end-to-end with no regression. The `UserDefinedLogicTransform` (the C# 7
  user-defined short-circuiting `&&` / `||` operator fold, the next per-statement
  child of `StatementTransform` after the wired `TransformAssignment` pieces) is now
  wired into the CLI pipeline as the 7th `StatementTransform` child (after
  `NullPropagationStatementTransform`, the GetILTransforms() order skipping the
  deferred `TransformArrayInitializers` / `TransformCollectionAndObjectInitializers`
  / `TransformExpressionTrees` / `IndexRangeTransform` / `DeconstructionTransform`
  / `NamedArgumentTransform` / `RemoveUnconstrainedGenericReferenceTypeCheck`). This
  iteration ports the `LegacyPattern` (the legacy-csc shape) and the shared
  `MatchCondition` / `MatchBitwiseCall` helpers, adapted to the if-as-final block
  model (the C# reads `block.Instructions[pos]` as the stloc and
  `block.Instructions[pos+1]` as the if; this port makes the IfInstruction the
  block's `FinalInstruction`, so the if is `block->FinalInstruction` and removing it
  = replacing the if-final with a Branch to the next block, the D90/D113 if-as-final
  precedent). The `s.IsUsedWithin(call.Arguments[1])` reject (the rhs must not
  reference s, or short-circuiting would change semantics) is ported as a tree walk
  (the D110/D130 precedent, not the deferred per-variable use lists). The
  `RoslynOptimized` pattern (the "in combination with return statement" shape whose
  if has leave early-return arms + a trailing leave) and the C# `Transform` static
  method (which would need a general Clone this port has no virtual Clone for) are
  deferred. The .NET Framework 4 legacy-csc mscorlib corpus carries no `op_True` /
  `op_False` operator definitions (a user-defined short-circuiting `&&` / `||` is a
  rare C# feature; mscorlib has no operator-overloading types that define them), so
  the `LegacyPattern` fires 0 times on it -- a faithfulness-only transform on this
  corpus, matching the `DetectCatchWhenConditionBlocks` / `LdLocaDupInitObj` /
  `SwitchOnNullable` precedent (the hand-built tests verify the rewrite, the mscorlib
  sweep verifies the ILAst invariant holds and the `UserDefinedLogicOperator` count is
  0). 25 new gtest cases cover `MatchCondition` / `MatchBitwiseCall` positives/negatives
  and the `LegacyPattern` fold (`&&` + `||`) + 8 negatives (non-StackSlot s, non-if
  final, non-logic.not condition, condition on a different variable, an else arm, an
  rhs that references s, a true arm storing to a different variable, a non-bitwise-
  call true arm) + the mscorlib sweep. 1015/1015 tests pass (25 new; was 990); the CLI
  decompiles mscorlib end-to-end with no regression (exit 0, 517 lock / 313 using / 78
  `??` unchanged; no `UserDefinedLogicOperator` nodes are constructed on this corpus so
  the output is byte-identical to D138). The remaining in-order per-statement children
  (`TransformArrayInitializers` / `TransformCollectionAndObjectInitializers` /
  `TransformExpressionTrees` / `IndexRangeTransform` / `DeconstructionTransform` /
  `NamedArgumentTransform` / `RemoveUnconstrainedGenericReferenceTypeCheck` /
  `InterpolatedStringTransform`), the `RoslynOptimized` pattern, and the remaining
  `TransformAssignment` StObj/Call pieces (blocked by `InferType` / `IsSameMember` /
  `IMethod`) are the subsequent in-order targets.
- **Phase 5 (in progress -- the real C# AST + output back end)** -- the C#
  `Syntax` AST node family and the `CSharpOutputVisitor` pretty-printer (the
  ports of `ICSharpCode.Decompiler/CSharp/Syntax` and
  `.../CSharp/OutputVisitor/CSharpOutputVisitor.cs`), being built alongside
  the Phase 5 seed so the `--csharp` CLI output stays byte-identical while the
  real back end is wired in. **76** C# AST node headers are ported
  (`Decompiler/CSharp/Syntax/`, the `AstNode` hierarchy + `Role`/`Slots` +
  the `IAstVisitor`/`DepthFirstAstVisitor` Visit surface). The output stage is
  in place: `ITextOutput`/`TokenWriter` (`TokenWriter`/`TextWriterTokenWriter`),
  the two decorator writers (`InsertRequiredSpacesDecorator`,
  `InsertMissingTokensDecorator`), and the four `TokenWriter` composition
  factories (`Create`/`CreateWriterThatSetsLocationsInAST`/
  `InsertRequiredSpaces`/`WrapInWriterThatSetsLocationsInAST`). The
  `CSharpOutputVisitor` infrastructure (the ctors, the writer/policy/
  container-stack fields + inter-token state, `StartNode`/`EndNode`, the
  Comma/token/brace writers, `WriteBlock`, `Tokens`, `CSharpModifiers`) is
  ported with all **130** `IAstVisitor` Visit methods declared; **89** are now
  implemented as faithful ports of the C# source (the leaf expressions, the
  operator-bearing expressions + their `GetOperatorToken` helpers, the
  statement/control-flow family, the `AstType` family, the
  declaration/anonymous/lambda family, the switch + switch-expression +
  try/catch families, `ParameterDeclaration`, and
  `VariableDeclarationStatement`), and **41** `NotImplemented()` stubs remain
  (the type/member-declaration family -- `TypeDeclaration`/
  `MethodDeclaration`/`FieldDeclaration`/`PropertyDeclaration`/
  `ConstructorDeclaration`/`DelegateDeclaration`/`NamespaceDeclaration`/... --
  the query-expression family, the interpolation family, `SyntaxTree`, and
  `RecursivePatternExpression`). The port is exercised by direct unit tests
  (5859 gtest cases; the CLI `--csharp` output is byte-identical run-to-run at
  the known-good seed signature). Remaining to wire `--csharp` to the real
  back end: the `ExpressionBuilder`/`StatementBuilder`/`CallBuilder` (the
  resolver-checked translation from ILAst to the C# AST), the ~15 AST
  prettification transforms, and the `RequiredNamespaceCollector`. The
  `DecompilerSettings` bag (the full ICSharpCode.Decompiler/DecompilerSettings.cs
  property class with the C# `LanguageVersion` enum: 121 compare-then-write
  accessor pairs over the C# source's own field spellings -- two source typos
  preserved -- the `LifetimeAnnotations` obsolete alias, the SetLanguageVersion
  if-ladder with the union-over-newer-blocks rule, the GetMinimumRequiredVersion
  ladder with the source's own three-gated-field omissions, the lazy
  `CreateAllman` `CSharpFormattingOptions` default, and `Clone`, with the
  `INotifyPropertyChanged` event the documented deferral) landed as the ctor
  prerequisite the ExpressionBuilder/DecompileRun slices consume; verified
  against the tables generated from the source and cross-checked against the
  shipped ilspycmd 11.0 engine (which is NEWER here than this repo: it carries
  a CSharp1 gating block, gates switchOnReadOnlySpanChar at CSharp11, and
  spells the two irregular fields `objectOrCollectionInitializers` /
  `introducePrivateProtectedAccessibility` -- the fixture header documents
  each). The translated-value foundation layer of that back end landed:
  `Decompiler/CSharp/TranslatedExpression.{hpp,cpp}` (the C#
  `ExpressionWithILInstruction` / `ExpressionWithResolveResult` /
  `TranslatedExpression` wrapper structs with `UnwrapChild`, whose
  `ConvertTo`/`ConvertToBoolean` machinery is DEFERRED with the
  ExpressionBuilder skeleton it consumes), `TranslatedStatement.hpp`, and
  `TranslationContext.hpp` (the visitor `TypeHint` struct), plus
  `Decompiler/CSharp/Annotations.{hpp,cpp}` (the C# `Annotations.cs`: the
  `ILVariableResolveResult` + eight annotation-holder classes and the
  `WithILInstruction`/`WithoutILInstruction`/`WithRR`/`GetSymbol`/
  `GetResolveResult`/`GetILVariable`/`WithILVariable`/`CopyAnnotationsFrom`/
  `CopyInstructionsFrom` extension surface). The C# stores the ILInstruction
  object itself as the annotation; the port's annotation channel owns via
  `shared_ptr` while the IL tree owns instructions uniquely, so the channel
  stores a NON-OWNING `ILInstructionAnnotation` holder (the With*/query
  helpers are its only access forms -- builder call sites stay C#-shaped),
  and `AbstractAnnotatable` gained the `SharedAnnotations()` owning-view the
  C# reference-sharing `CopyAnnotationsFrom` needs. `TypeSystemExtensions`
  gained the `GetSymbol(ResolveResult)` region extension (the C#
  TypeSystemExtensions.cs `#region ResolveResult`) the `GetSymbol(AstNode)`
  query composes. Verified by the 38-test `AnnotationsTest`/
  `TranslationContextTest` suite (the binding/query/copy matrices over
  synthetic ASTs, the GetSymbol dispatch over six real resolve-result kinds,
  the trivia-holder skip, the UnknownError fallbacks), proven with a
  neuter round (7 RED, restored green). The `ExpressionBuilder` skeleton
  (`Decompiler/CSharp/ExpressionBuilder.{hpp,cpp}`) and the `DecompileRun`
  prerequisite (the root `Decompiler/DecompileRun.hpp` with the
  `EnumValueDisplayMode` enum) landed as the first builder slice: the ctor with
  the resolver/ast-builder/type-inference field construction over the ICompilation
  narrowing (the BamlDecompilerTypeSystem convention; the C# IDecompilerTypeSystem
  interface stays the documented diamond-free deferral), the entry family
  (ConvertType / ConvertConstantValue x2 with the small/native-integer cast arms
  and the displayAsHex try/finally, Translate with the DEBUG post-condition
  asserts, TranslateCondition, ConvertVariable with the by-ref ref-wrap,
  HidesVariableWithName over the ILFunction ancestor walk), the visitor-dispatch
  entry (an OpCode switch -- the ExpressionTransforms convention; the port's IL
  tree has no AcceptVisitor surface) with the ported leaf arms (LdNull,
  DefaultValue, LdStr, LdLoc/LdLoca, LdcI4/I8/F4/F8/Decimal, the Default
  'OpCode not supported' ErrorExpression fallback the unported arms degrade to),
  and the self-contained statics (the operator-name tables, the
  overflow-check pairs, IsCompatibleWithSign, IsUnboxAnyWithIsInst,
  UnwrapBoxingConversion, ChangeDirectionExpressionTo, ErrorExpression,
  CallUnsafeIntrinsic, WrapInRef, LdcI4, the FindType/FindArithmeticType/
  PrepareArithmeticArgument family, ShouldDisplayAsHex, AdjustConstantToType).
  The `TranslatedExpression` conversion machinery landed with it (lifting the
  iteration-99 deferral): the full `ConvertTo` cast-insertion machinery (the
  implicit-conversion unwrap arms through `FindSharedAnnotation` over the
  annotation channel, the tuple-literal element-wise arm through `CreateTupleType`,
  the IntPtr/UIntPtr special cases, the enum/char/pointer conversions, the
  ByReference arm with the `Unsafe.As` intrinsic and the fixed-variable check,
  the resolver-cast constant folding with the primitive-format copy, the
  checked/unchecked annotation trio from the new `Transforms/AddCheckedBlocks.{hpp,cpp}`),
  `ConvertToBoolean` (the constant/pointer/enum arms), `UnwrapImplicitBoolConversion`,
  and the `IsFixedVariable`/`CastCanBeMadeImplicit`/`LdcI4` helpers. Supporting
  repairs the slice exposed: `TypeUtils` gained the `GetStackType(IType)` /
  `GetSize(StackType)` ports, `ReflectionHelper` gained the
  `FindType(compilation, StackType, Sign)` overload, `IType.hpp` gained the
  `SpecialType::NullType`/`ArgList` factory singletons, `ILFunction` gained the
  `Name`/`LocalFunctions` surface the ancestor walk reads, `StackTypeOf`/
  `TypeUtils.GetSign` now read the DEFINITION's KnownTypeCode (the C#
  `GetDefinition().KnownTypeCode` shape the minimal `KnownType` wrapper missed --
  the CorlibTypeDefinition fixture exposed it), and the
  `SpecialType`/`StackTypeOf` null-object handling hardened against the
  `.get()`-of-a-fresh-singleton dangling-temporary trap (the iteration-73
  learning, hit three times in one slice). Verified by the 35-test
  `ExpressionBuilderSkeleton`/`DecompileRun`/`EnumValueDisplayMode` suite over a
  real `SimpleCompilation(MinimalCorlib)` fixture (the statics tables, the ctor
  surface, the Translate leaf arms incl. the type-hint constant adjustment and
  the by-ref ref-wrap, the TranslateCondition constant/negate shapes, the
  ConvertToBoolean constant/non-constant/pointer arms, the ConvertTo
  identity/void/constant-fold/cast/conditional arms, and the error-expression
  shapes). The operator-expression arms landed as the next builder slice:
  `VisitBitNot` (the undersized-argument extension -- the small-integer-enum /
  StackType.I-native-integer / bool / char clauses with the extension sign
  from the type hint falling back to the argument type's own sign; the resolver's
  unary numeric promotion (char..uint16 -> int32) handles byte/short directly so
  no AST cast is inserted for them, while the Boolean->integer ConvertTo arm
  renders the `flag ? 1 : 0` ternary), `VisitThrow` (the ThrowExpression over
  the translated operand with the ThrowResolveResult), and
  `VisitThreeValuedBoolAnd`/`VisitThreeValuedBoolOr` + the shared
  `HandleThreeValuedLogic` helper (the non-short-circuiting `&`/`|` on `bool?`:
  the nullable side lifts, the non-nullable side converts, and the operator
  resolve result is the LIFTED bitwise-and/or over Nullable<bool>; the TypeErasure
  equivalence ignores only REFERENCE-type nullability, so a value-type
  `bool` -> `bool?` conversion is a real `(bool?)` cast). Verified by the 11-test
  `ExpressionBuilderOperatorTest` suite over the MinimalCorlib fixture
  (the constant-fold/int-local/byte-promotion/bool-ternary/char-cast shapes,
  the type-hint-sign drive, the lifted nullable form, the throw shape, and the
  three-valued and/or/nullable-left matrices), proven with a two-behavior
  neuter round (5 RED, restored green). The type-operand arms landed as the
  next builder slice: `IsType(IsInst)` (the `expr is T` helper the comp and
  unbox.any special cases build -- the type renders through
  `TupleUnderlyingTypeOrSelf` and the resolve result is the
  `TypeIsResolveResult` over the boxing-unwrapped input), `VisitIsInst` (the
  reference-type arm renders `expr as T` over `Conversion.TryCast`; the
  value-type arm renders the pure-argument fallback `expr is T ? expr : null`
  -- the conditional carries a plain ResolveResult over the ARGUMENT's type
  because isinst over a value type yields the boxed value -- or the loud
  error expression for an impure argument), `VisitSizeOf` (`sizeof T` over an
  unmanaged type via the new `TypeSystemExtensions::IsUnmanagedType` port --
  the Enum/Pointer/FunctionPointer/NInt/NUInt kinds, the type-parameter
  unmanaged-constraint gate, the 16 primitive KnownTypeCodes, and the
  struct-field walk with its self-cycle guard and the allowGenerics gate --
  else the `System.Unsafe.SizeOf<T>()` intrinsic), and `VisitLdTypeToken`
  (`typeof(T).TypeHandle` over the TypeOfResolveResult pair, the outer one
  typed to the resolved System.RuntimeTypeHandle). The two IL nodes the arms
  read gained the C# `IType` fields (`SizeOf.Type` / `LdTypeToken.Type`, the
  reader populating them via `ResolveTypeToken` while the display strings
  the seed renders stay unchanged, the LdFtn precedent). Verified by the
  15-test `ExpressionBuilderIsInstTest`/`ExpressionBuilderSizeOfTest`/
  `ExpressionBuilderLdTypeTokenTest` + `IsUnmanagedType` suites over the
  MinimalCorlib fixture, proven with a three-behavior neuter round (5 RED,
  restored green); the value-type isinst arms are driven through the direct
  `Visit` entry because Translate's DEBUG post-condition assert would fire on
  them (IsInst.ResultType is O while the conditional's type is the argument
  type -- the C# keeps those arms for the consumers that special-case
  value-type isinsts before Translate ever runs). The assignment arm landed
  as the next builder slice: `VisitStLoc` (ExpressionBuilder.cs lines
  809-870) -- the value Translate with the VARIABLE's type as hint, the
  stack-slot type refinement (a StackSlot not yet in `loadedVariablesSet`
  adopts the value's type when `CanUseTypeForStackSlot` -- single
  definition / other-value-type / Ref slot / all stores consistent -- and the
  StackType + non-Null guards hold, else the `MatchDefaultValue` other-value
  -type arm), the by-ref re-assignment `ref (a = ref b)` shape over the
  UnwrapChild'd identifier + the Assign `OperatorResolveResult` (the C#
  passes the UNWRAPPED child's element type as the assign's result type and
  re-attaches the SAME `ByReferenceResolveResult` to the outer
  DirectionExpression), and the plain `Assignment(lhs, value)` helper
  (ExpressionBuilder.cs line 1255) over the implicit-conversion-allowed
  ConvertTo. The port stand-in for the C# `ILVariable.StoreInstructions`
  per-variable list `AllStoresUseConsistentType` walks: one recursive scan of
  the current function's live body grouping every IStoreInstruction-shaped
  node (StLoc/MatchInstruction/UsingInstruction/TryCatchHandler/
  PinnedRegion) by its Variable, cached on the builder for the whole
  translation (the builder never mutates the tree; the scan is redone when
  `currentFunction` changes) -- the port keeps no per-variable use lists, so
  the live-tree scan is the faithful equivalent. Supporting pieces:
  `ILVariable::StackType()` -- the C# readonly property the C# ctor derives
  from `type.GetStackType()` and the Type setter guards; the port derives it
  on read because the port's Type is a plain field several reader/transform
  sites re-assign after construction (a cached field would go stale) -- and
  the new `Decompiler/IL/ILTypeExtensions.{hpp,cpp}` port of
  ILTypeExtensions.cs's `InferType(ILInstruction, ICompilation)` extension
  (the LdLoc/StLoc/LdObj/StObj/LdLoca/LdFlda/LdsFlda/LdElema/NewObj/Call/
  NewArr/Comp/BinaryNumericInstruction/DefaultValue arms with the
  documented divergences: the port's Call node unifies call/callvirt/newobj
  via IsNewObj/IsInstanceCall so the newobj DeclaringType arm must win over
  the ReturnType arm; the port's UserDefinedLogicOperator carries no return
  type, ILFunction has no DelegateType, the field-address nodes carry no
  field IType, and there is no CallIndirect node -- each falls to
  UnknownType) + the `MatchDefaultValue` bare match, plus the
  `TypeUtils::IsCompatibleTypeForMemoryAccess` port (TypeUtils.cs line 241,
  the TypeErasure-normalized memory-access compatibility query the LdElema
  arm composes). Verified by the 20-test `ExpressionBuilderStLocTest` +
  `ILTypeExtensionsTest` suites over the MinimalCorlib fixture (the plain
  assignment shape with the Assign resolve result + IL annotation, the
  type-hint constant re-typing, the by-ref re-assignment, the five
  stack-slot refinement matrices incl. the loaded-slot guard and the
  non-StLoc store rejection, the Assignment helper, and the InferType/
  MatchDefaultValue/StackType/IsCompatibleTypeForMemoryAccess tables),
  proven with a five-test RED neuter round plus a second Comp-arm neuter
  round, restored green. The array-creation arm landed as the next builder
  slice: `VisitNewArr` (ExpressionBuilder.cs lines 502-516) -- every index
  through the new `TranslateArrayIndex`/`ConvertArrayIndex` helper pair (the
  oversized-input truncation to the index's own stack type, the
  primitive/native-integer passthrough, the `allowIntPtr` (U)IntPtr arm, the
  I4-preference branch for small non-primitive inputs, and the
  `FindArithmeticType` conversion), the `new int[n][]` ComposedType
  specifier move (`MoveTo` out of the element type's own array specifier,
  which stays a bare-base ComposedType in the Type slot), the reconstructed
  array type (SZArray for one dimension, the rank constructor beyond), and
  the `ArrayCreateResolveResult` with the present-but-empty initializer list
  (the C# `Empty<ResolveResult>.Array` non-null-empty state the
  `std::optional` distinguishes from nullopt). Two latent type-system
  divergences the slice's tests exposed and fixed: `TypeUtils::GetSize` read
  only the minimal `KnownType` wrapper's code (a CorlibTypeDefinition
  reported 0, breaking every small-integer/oversize query over real
  definitions) and now reads `GetDefinition()`'s KnownTypeCode with the
  Enum unwrap + SkipModifiers arms and the wrapper fallback (the
  GetStackType/GetSign dual-shape convention), and
  `TransformAssignment`'s `SmallIntegerKnownTypeCode` read only the wrapper
  the same way and now mirrors the C#
  `type.GetEnumUnderlyingType().GetDefinition()?.KnownTypeCode` (the
  CheckImplicitTruncation suite was passing vacuously on the old dead
  small-integer path over real-definition fixtures). Verified by the 10-test
  `ExpressionBuilderNewArrTest` suite over the MinimalCorlib fixture with
  every render pinned against the real ilspycmd 11.0 `--csharp` output over
  a csc-compiled array fixture (`new int[5]`, `new int[n]`, `new long[n]`,
  `new int[(uint)c]` -- the char index converts to the I4 arithmetic type
  uint32, `new string[n]`, `new int[n][]`, and the rank-2 shape), plus the
  three direct `ConvertArrayIndex` decision-tree arms (the I4 truncation of
  an oversized long, the I8 input's I4 preference, and the
  NativeIntegers-off IntPtr allowIntPtr arm), no neuter round (the two
  divergences were proven RED by the first run's two failures before the
  fixes), full suite 11974 ran / 11972 passed / the 2 standing skips / zero
  failures. The numeric-conversion arm landed as the next builder slice:
  `VisitConv` (ExpressionBuilder.cs lines 2227-2386) -- the checked/
  IntToFloat arm (first normalize the input to the conv's sign -- the C#
  zero/sign-extension depends on the INPUT type while the ILAst conv
  depends on the output, the conv.ovf exception the port carries verbatim),
  then the ConversionKind switch: the StartGCTracking passthrough, the
  StopGCTracking arm (a fixed address casts to the corresponding pointer
  type through the reference-to-pointer `&x` render; a moveable address
  emits the `Unsafe.AsPointer(ref x)` intrinsic over a shared-owned void
  pointer type; an integer input is the start-tracking-then-stop
  passthrough with NO conv annotation -- the C# `return arg`; anything
  else falls to the default simple cast), SignExtend/ZeroExtend (normalize
  the input to the SIGNED/UNSIGNED INPUT STACK TYPE and let the caller
  handle the extension through the post-condition -- the result type is
  the input stack type's form, not the conv target), Nop, Truncate (the
  small-integer case with its own double truncation vs the same-size-
  same-sign passthrough, else the caller's), the Invalid Unknown->O
  passthrough (no `(object)` cast over an unknown-typed argument), and the
  default TypeHint-aware target pick (`TargetType ==
  ToPrimitiveType(GetUnderlyingType(hint))` and the nullable-state match
  -> the hint itself; Ref -> the byte by-reference type; None -> object;
  else `GetType(ToKnownTypeCode(TargetType))` with the n(u)int preference
  over (U)IntPtr under NativeIntegers and the IsLifted Nullable<T> wrap).
  Supporting pieces: `TypeUtils::ToKnownTypeCode(PrimitiveType)` (the
  primitive-target mapping VisitConv composes), the
  `WithILInstruction(TranslatedExpression, ILInstruction*)` overload
  (the C# Annotations.cs line 109 extension -- the returned node carries
  [argInst, conv] on a passthrough, only [conv] on a fresh ConvertTo cast
  node), the header-exposed `IsFixedVariableInstruction` shared with the
  TranslatedExpression IsFixedVariable helper, the `ValueMightBeOversized`
  helper (only a pointer subtraction under StackType.I is known to fit;
  GetSize reports 0 for Decimal so the oversized-type tests pin through
  Int64), and the `IL::StackTypeOf` Unknown-kind fix (the C#
  GetStackType(TypeKind.Unknown) answers StackType.Unknown when
  IsReferenceType != true -- the port's reader primitive returned O for
  every unknown-typed variable, a divergence the Invalid-arm drive
  surfaced). Verified by the 22-test `ExpressionBuilderConvTest` suite
  over the MinimalCorlib fixture (the Nop/SignExtend/ZeroExtend/
  IntToFloat/checked-sign-normalize/checked-same-sign matrices, the
  truncate-to-small-integer cast and same-size passthrough, the
  non-small-truncate caller passthrough, the three StopGCTracking arms
  with the `&num`/`Unsafe.AsPointer(ref arg)`/integer-passthrough shapes,
  the nint preference over IntPtr with and without NativeIntegers and
  the IntPtr-hint Equals keep, the lifted checked Nullable<double> wrap,
  the Invalid passthrough + object-cast pair, the hint-match default arm,
  and the ValueMightBeOversized matrix), proven with a four-behavior
  neuter round (5 RED: the checked sign normalization, the SignExtend
  sign check, the StopGC fixed branch, the pointer-subtraction operator
  arm), restored green; full suite 11996 ran / 11994 passed / the 2
  standing skips / zero failures, and all four CLI baselines unchanged
  (--csharp mscorlib 10106366 bytes, --il byte-identical to the
  41246545-byte real-ilspycmd gold, -l c 109438, --json-alone rc 64).
- **`ExpressionBuilder` stackalloc arms + `PointerArithmeticOffset`** --
  `VisitLocAlloc`/`VisitLocAllocSpan` (ExpressionBuilder.cs lines 517-528),
  `TranslateLocAlloc`/`TranslateLocAllocSpan` (lines 530-579: the element type
  from the count's `sizeof` operand, from the type hint's pointer element via
  `GetPointerArithmeticOffset`, or the byte fallback; the span arm reads
  `inst.Type.TypeArguments[0]`), `GetPointerArithmeticOffset` +
  `EnsureIntegerType` (lines 1506-1531: non-primitive/non-native-integer counts
  convert to the arithmetic type of their own stack type and sign), the
  `OpCode::LocAlloc`/`LocAllocSpan` visitor cases, the `LocAlloc`/`LocAllocSpan`
  IL node classes in `MemoryInstructions.hpp` (with the clone cases; the port's
  IL reader still decodes raw `localloc` as an LdNull placeholder -- the seed
  --csharp convention -- so the new nodes are driven by hand-built trees until
  the reader/seed reconcile slice), and the full `PointerArithmeticOffset`
  struct port (`IL/PointerArithmeticOffset.{hpp,cpp}`: `Detect` with the
  I8->I conv unwrap, the size-1 passthrough, the mul gates (lifted/overflow),
  the constant-fold division arm with the fresh LdcI4 ownership struct, the
  bare-sizeof arm, and `unwrapZeroExtension`; `ComputeSizeOf` over the
  GetEnumUnderlyingType-definition KnownTypeCode with the dual-shape wrapper
  fallback; `IsFixedVariable` lifted beside the TranslatedExpression local
  helper). Renders pinned against the real ilspycmd 11.0 --csharp output over
  a csc-compiled stackalloc fixture (`stackalloc int[n]`, `stackalloc
  byte[(int)n]` -- the ConvertTo with allowImplicitConversion=false forces
  the explicit int cast for a byte count, `stackalloc int[(int)(uint)c]` --
  char is NOT a C# primitive integer type so EnsureIntegerType renders the
  (uint) intermediate, and the Span<int> arm). Verified by 18 new tests
  (11 `PointerArithmeticOffsetTest` + 7 `ExpressionBuilderStackAllocTest`),
  a three-behavior neuter round (5 RED: the constant folding, the overflow
  gate, the zero-extension unwrap) restored green; full suite 12014 ran /
  12012 passed / the 2 standing skips / zero failures, and all four CLI
  baselines unchanged (--csharp mscorlib 10106366 bytes, --il byte-identical
  to the 41246545-byte real-ilspycmd gold, -l c 109438, --json-alone rc 64).
- **`ExpressionBuilder` VisitComp comparison family** -- `VisitComp`
  (ExpressionBuilder.cs lines 871-946: the ThreeValuedLogic lifted-not arm
  (the `!b` render over a `Nullable<bool>` operand with the lifted `Not`
  `OperatorResolveResult`, else the exact-message error expression), the `Ref`
  arm over the Unsafe intrinsics (`AreSame`/`IsAddressLessThan`/
  `IsAddressGreaterThan` with the negate forms and the byte-ref common-type
  conversion), then `TranslateCeq` (equality/inequality: the `(e as T) ==
  null` -> `!(e is T)` rewrites, the redundant-bool-comparison removal, the
  pointer-null comparisons through `MatchLdcI`, the enum/char literal type
  unification via `TryUniteEqualityOperandType`, the string/delegate-with-null
  reference-comparison special case, and the resolver-driven render with the
  `ConvertTo` retries and the fresh-`OperatorResolveResult` fallback) or
  `TranslateComp` (the relational operators: the pointer-pointer builtin, the
  `PrepareArithmeticArgument` pair, the `AdjustConstantExpressionToType`
  constant adjustment, the sign-corrected `FindArithmeticType` re-typing, and
  the object-type `Unsafe.As<object, UIntPtr>` wrap)) plus the shared helpers
  `CreateBuiltinBinaryOperator`, `AdjustConstantExpressionToType` and the
  `ToBinaryOperatorType`/`IsEqualityOrInequality` ComparisonKind extensions.
  The slice also landed the faithful `Comp::Sign` field (the C# has only
  `Sign`; the port's `bool Unsigned` stays as the redundant derived view), the
  reader's static sign mapping per opcode (the equality/inequality forms
  Sign.None, the relational forms Signed/Unsigned -- the type-dependent
  float-computation residual gap documented at the macro), the clone's `Sign`
  carry, the lifted transforms' `comp.Sign` propagation, and fixed the
  pre-existing `AdjustConstantToType`/`PrepareArithmeticArgument`/
  `IsCompatibleWithSign` `GetEnumUnderlyingType` divergence (the C# uses
  `NullableType.GetUnderlyingType` -- the Nullable<T> unwrap, not the enum
  unwrap). Renders pinned against the real ilspycmd 11.0 --csharp output over
  a csc-compiled comparison fixture (`a < b`, `x == false` -> `!x`, `x ==
  true` -> `x`, `s == null`, `p == null` through the `ldc.i4.0; conv.u` shape,
  `x == y` over int? locals). Verified by 24 new tests and a four-behavior
  neuter round (3 RED: the 3VL arm, the bool-comparison negate, the Ref-arm
  method name; the string-null special case is render-equivalent through the
  resolver path) restored green; full suite 12038 ran / 12036 passed / the 2
  standing skips / zero failures, and all four CLI baselines unchanged
  (--csharp mscorlib 10106366 bytes, --il byte-identical to the
  41246545-byte real-ilspycmd gold, -l c 109438, --json-alone rc 64).
- **`ExpressionBuilder` VisitBinaryNumericInstruction arithmetic family** --
  `VisitBinaryNumericInstruction` (ExpressionBuilder.cs lines 1262-1298: the
  ten-operator dispatch through `HandleBinaryNumeric`, Div first through
  `HandlePointerSubtraction`, the shifts through `HandleShift`),
  `HandleBinaryNumeric` (lines 1619-1746: the type-hint propagation gate for
  bitwise ops over mixed input stack types, the managed/plain
  pointer-arithmetic arms, `PrepareArithmeticArgument` on both inputs, the
  `0 - x` unary-minus rewrite with the checked/unchecked annotation, the
  enum-constant adjustment for bitwise ops, the resolver-driven render with
  the common-type fallback, the bitwise constant re-render with the hex gate,
  and the checked/unchecked/constant-overflow annotation tail -- the C#
  explicit-`unchecked(...)` wrapper for a compile-time constant that would
  overflow CS0220), `HandleShift` (lines 1849-1912: the small-integer
  promotion rule, the C# 11 `>>>` operator selection over
  `UnsignedRightShift` + the type-hint sign gate, the sign-preferring cast
  fallback, and the always-int32 right-hand conversion),
  `HandlePointerArithmetic` (lines 1300-1384: the type-hint element-type rule
  over non-primitive/differently-sized element types, the
  `GetPointerArithmeticOffset` or byte-pointer fallback, and the ptr +/- int
  render), `HandleManagedPointerArithmetic` (lines 1386-1484: the ref-ref
  `Unsafe.ByteOffset(target:, origin:)` named-argument intrinsic, the
  ref +/- int `Unsafe.Add`/`Subtract(+ByteOffset)` intrinsics over the
  detected element offset, the int + ref named-argument arms, and the
  fixed-buffer indexer direction documented as a loud deferral on the
  ConvertField/IsFixedField machinery), `HandlePointerSubtraction` (lines
  1565-1619: the div(sub(a,b), sizeof(T)) / div(sub(a,b), constant)
  pointer-subtraction render over the matching pointer types with the
  debug-build divide-by-1 two-pointer arm), and
  `ConstantBinaryOperatorOverflows` (lines 1842-1847). Fixed
  `TypeUtils.IsCSharpSmallIntegerType` to the C# `GetDefinition()` dispatch
  (the real CorlibTypeDefinition/MetadataTypeDefinition shapes answered false
  under the old wrapper-only `dynamic_cast<KnownType>` read, flipping the
  TypeSystemAstBuilder's small-integer literal remap -- the held value type
  for a byte enum's numeric render changed from the raw byte form to the
  C#-faithful int32 form, and one stale test pin followed). Render facts
  pinned by the tests: the LdLoca by-ref type nests the managed reference
  (int& &), so `brt.ElementType` is itself a managed reference and
  `PointerArithmeticOffset.Detect` answers no match -- the managed-ref
  byte-offset intrinsics render the RAW byte offset (`Unsafe.AddByteOffset(ref
  a, 4)`), and the C# `ComputeResultType`'s `sub(&,&) = I` arm is dead code
  (the left==right first branch answers Ref). Verified by 17 new
  `ExpressionBuilderBinaryNumericTest` tests over the MinimalCorlib fixture
  (the add/overflow-annotation shapes, the 0-x negate rewrite with both
  overflow arms, the mixed-width truncation observable, the IntPtr -> nint
  conversion, the bitwise constant + hex-gate absence, the small-integer shift
  promotion, the >>> operator over a same-size signed type with an unsigned
  sign, the plain >> fallback over uint, the managed ref + byte-offset /
  element-offset / ref-ref named-argument intrinsics, the pointer-subtraction
  render with the two-IL-instruction annotation, the gate matrix, the unknown
  operator throw, and the IsCSharpSmallIntegerType definition-dispatch
  matrix), proven with the neuter rounds (6 RED across the managed-pointer
  gate, the small-integer shift promotion, the overflow-annotation tail, and
  HandlePointerSubtraction; the PrepareArithmeticArgument truncation arms are
  masked by the common-type fallback for add shapes over MinimalCorlib -- the
  distinguishing observable needs a resolver shape the minimal corlib cannot
  express), restored green; full suite 12055 ran / 12053 passed / the 2
  standing skips / zero failures, and all four CLI baselines unchanged
  (--csharp mscorlib 10106366 bytes, --il byte-identical to the
  41246545-byte real-ilspycmd gold, -l c 109438, --json-alone rc 64).
- **`ExpressionBuilder` VisitUserDefinedCompoundAssign arm + the LdObj helper +
  its support statics** -- `VisitUserDefinedCompoundAssign`
  (ExpressionBuilder.cs lines 1912-1998: the span-based vs plain
  string.Concat detection through `CallBuilder.IsSpanBasedStringConcat`, the
  Address target kind's `LdObj` dereference, the op_Checked...-name
  CheckedAnnotation / `ReplaceMethodCallsWithOperators.HasCheckedEquivalent`
  twin UncheckedAnnotation on the target, the 2-parameter AssignmentExpression
  render through `GetAssignmentOperatorTypeFromMetadataName`, and the
  1-parameter UnaryOperatorExpression render through
  `GetUnaryOperatorTypeFromMetadataName` with EvaluatesToOldValue = postfix),
  the private `LdObj` helper (lines 2894-2962: the byref/pointer type-hint
  translate, the managed-reference/`&`-wrapper `UnwrapChild` dereference, the
  `*pointer` render, the incompatible-pointer ConvertTo re-type with the
  `Unsafe.Read<T>(void*)` intrinsic for a managed load type), and the
  first slices of two CSharp classes: `CallBuilder::IsSpanBasedStringConcat`
  (the static `string.Concat(ReadOnlySpan<char>...)` detector -- the Build
  machinery deferred with the VisitCall arms) and
  `ReplaceMethodCallsWithOperators::{HasCheckedEquivalent,
  RemoveRedundantToStringInConcat, ToStringIsKnownEffectFree,
  MatchToStringCallPattern}` (the checked-twin `DeclaringType.GetMethods`
  walk; the ToStringCallPattern two-shape structural match with the
  string-parameter gate, the by-ref-like/ordering/NullReferenceException/
  struct-copy elimination rules and the 16-primitive+String effect-free
  table), plus `IL::MethodRequiresCopyForReadonlyLValue` (the reference-type /
  readonly-struct copy rule, the port's IMethod carrying no
  ThisIsRefReadOnly metadata override yet so the readonly arm reads
  false -- conservative) and `TypeUtils::IsCompatiblePointerTypeForMemoryAccess`
  (the TypeWithElementType cast ported as the PointerType/ByReferenceType
  dynamic-cast pair). The `UserDefinedCompoundAssign` IL node gained the C#
  `readonly IMethod Method` as a real resolved-method shared_ptr (the seed
  string stand-in remains the dump path and the transforms' construction
  form; a seed node driven through the Visit throws the loud
  `std::logic_error` deferral, since the C# Visit consumes the method
  unconditionally), with the IMethod-taking `IsIncrementOrDecrement` /
  `IsStringConcat` node statics and the ctor Debug asserts. Render facts
  pinned by the tests: the checked-annotation arm is reachable only through
  the unary op_CheckedIncrement/Decrement or concat shapes (the 2-parameter
  assignment table has no op_CheckedXxx spellings, so a checked-name
  2-parameter node would fail the C# Debug.Assert on
  GetAssignmentOperatorTypeFromMetadataName); the incompatible-pointer +
  managed-load-type LdObj arm renders `Unsafe.Read<string>(ptr) += "!"` (the
  intrinsic invocation IS the compound assignment's left operand); byte*/bool
  is pointer-compatible (bool loads as I4 with the same 1-byte size) while
  byte*/int64 is not; and the port's ToStringCallPattern hand-writes the two
  declarative shapes (the C# Pattern framework stays deferred). Verified by
  15 new tests (the support matrices, the RemoveRedundantToStringInConcat
  elimination rules over the three distinct outcomes -- effect-free both
  positions, effect-full value type kept in both positions with the
  struct-copy rule, reference type kept without a null-conditional and
  removed with one -- and the five Visit drives) over the MinimalCorlib
  fixture; full suite 12074 ran / 12072 passed / the 2 standing skips /
  zero failures, and all four CLI baselines unchanged (--csharp mscorlib
  10106366 bytes, --il byte-identical to the 41246545-byte real-ilspycmd
  gold, -l c 109438, --json-alone rc 64).
- **`ExpressionBuilder` VisitNumericCompoundAssign arm + helpers** --
  `VisitNumericCompoundAssign` (ExpressionBuilder.cs lines 2032-2073: the
  ten-operator dispatch -- the eight arithmetic/bitwise operators through
  `HandleCompoundAssignment`, the two shifts through `HandleCompoundShift`,
  and the ShiftRight gates that preserve the C# `>>>=` spelling when the
  sign is Unsigned and either the store type is signed or the small-integer
  gate plus the UnsignedRightShift setting fire), `HandleCompoundAssignment`
  (lines 2075-2165: the Address/LdObj target, the EvaluatesToOldValue postfix
  ++/-- UnaryOperatorExpression render with the DEBUG MatchLdcI(1) /
  MatchLdcF4(1) / MatchLdcF8(1) / pointer-offset asserts, the
  EvaluatesToNewValue value preparation -- PrepareArithmeticArgument then the
  pointer-offset fold (GetPointerArithmeticOffset, the failure arm adding
  the multi-line "ILSpy Error: GetPointerArithmeticOffset() failed" trailing
  comment) / the enum-underlying conversion
  (`NullableType.GetUnderlyingType(...).GetEnumUnderlyingType()`) / the
  Nullable-stripped underlying-type conversion for the other six operators
  -- the AssignmentExpression render, and the checked/unchecked overflow
  annotation tail (`AssignmentOperatorMightCheckForOverflow(op) &&
  !UnderlyingResultType.IsFloatType()`), which runs for BOTH eval modes),
  the `ConvertValue` local function as a private member (the n(u)int
  collapse: a StackType.I target loses implicit conversions unless it is
  already a C# native integer type, replacing itself with
  `SpecialType.NUInt`/`NInt`, then the Nullable wrap of the target when the
  value is nullable, then `ConvertTo(..., checkForOverflow,
  allowImplicitConversion)`), and `HandleCompoundShift` (lines 2167-2188:
  the value always converted to Int32 -- the C# shift operator's RHS type,
  nullable-wrapped when the value is Nullable<T> -- and the
  AssignmentExpression resolved through `resolver.ResolveAssignment`). The
  port reuses the pre-existing `AssignmentOperatorMightCheckForOverflow`
  static (the C# does NOT include UnsignedShiftRight in its false list --
  the port's added case is unreachable: the >>> spelling only flows through
  HandleCompoundShift, which never applies annotations) and the resolver's
  already-ported `ResolveAssignment` region. New file-local helper
  `MatchConstantOne` beside the Match helpers (the three-Match disjunction
  shape). Verified by 15 new tests over the MinimalCorlib fixture (the
  operator matrices with the LINQ-kind pins incl. the checked
  AddAssignChecked forms, the bitwise/shift no-annotation rule, the postfix
  and postfix-decrement renders with the float-constant DEBUG arm, the
  Address/LdObj dereference, the pointer-offset fold (`p += 40` over int32
  renders `p += 10`) and its failure-comment arm, the three ShiftRight
  gates, and the default-arm std::out_of_range), proven with a 3-behavior
  neuter RED round (5 failures: the annotation tail, the pointer fold, the
  unsigned gate) then restored green; full suite 12089 ran / 12087 passed /
  the 2 standing skips / zero failures, and all four CLI baselines unchanged
  (--csharp mscorlib 10106366 bytes, --il byte-identical to the
  41246545-byte real-ilspycmd gold, -l c 109438, --json-alone rc 64).
- **`ExpressionBuilder` VisitLdObj/VisitStObj arms + the StObjViaHelperCall
  helper** -- `VisitLdObj` (ExpressionBuilder.cs lines 2857-2887: the node's
  `LdObj.Type` as the load type; the `loadTypeUsedInGeneric` flag from
  `Target.ResultType == StackType.Ref` -- the C# `UnalignedPrefix != 0`
  disjunct is unreachable because the port's `LdObj` node carries no IL prefix
  field; the `context.TypeHint` override gate -- a known hint kind, an
  `IsCompatibleTypeForMemoryAccess(hint, loadType)` match, and the NOT
  `(loadTypeUsedInGeneric && IsAnyPointer(hint.Kind))` veto -- then the shared
  private `LdObj(Target, loadType)` dereference render and the
  `WithILInstruction` annotation), and `VisitStObj` (lines 2968-3046: the
  `StObjViaHelperCall` branch for a non-`ref` target whose type is not
  unmanaged; the `ByReferenceType`/`PointerType` hint by target stack type;
  the `IsCompatiblePointerTypeForMemoryAccess` gate choosing the memory type
  from the pointer/by-ref element -- the C# `TypeWithElementType` cast ported
  as the `PointerType`/`ByReferenceType` dynamic-cast pair -- or, in the
  incompatible arm, translating the value first and taking the value's
  compatible type or the node type before casting the pointer; the
  `DirectionExpression`/`AddressOf` `UnwrapChild` strip else a `*pointer`
  render; the value translated on demand against the target type; the
  ref-reassignment `ref (a = ref b)` shape through the
  `SharedResolveResultAnnotation` handle and a `ByReferenceResolveResult` lhs;
  and the final `AssignmentExpression` through the `Assignment` helper), plus
  the private `StObjViaHelperCall` (lines 3048-3086: the pointer translate, the
  `Byte`-ref vs `void*` cast, the value's `IsCompatibleTypeForMemoryAccess`
  conversion, and the `Unsafe.Write(pointer, value)` intrinsic through
  `CallUnsafeIntrinsic`). Every C# `UnalignedPrefix`-driven branch
  (`Unsafe.ReadUnaligned`/`WriteUnaligned`) is unreachable in the port -- the
  `LdObj`/`StObj` IL nodes carry no prefix field -- and is deliberately not
  ported. The arms are dead in the CLI path (the Phase-5 seed still drives
  `--csharp`), so the output is unchanged. Verified by **8** new gtest cases in
  1 suite over the MinimalCorlib fixture (the pointer-dereference render and
  the managed-ref strip for LdObj; the TypeHint override to a managed load
  type producing `Unsafe.Read<string>(byte*)` and the no-hint
  incompatible-pointer arm; the pointer and managed-ref `StObj` assignments
  with their `AssignmentExpression` shape; the non-unmanaged `Unsafe.Write`
  arm; and the `Visit` dispatch for both opcodes), proven with a 3-behavior
  neuter RED round (3 failures: the TypeHint-override gate and the
  `StObjViaHelperCall` gate for the pointer and non-unmanaged shapes) then
  restored green. Full suite in this environment 12097 ran / 12034 passed / 13
  failed / 101 skipped -- the 13 failures are the pre-existing
  environment-dependent real-fixture/gold tests (a missing
  `System.Private.CoreLib` 10.0.8 fixture, a corelib-version mismatch against
  the 10.0 gold, missing .NET Framework v4.7.2 facades / SDK Roslyn paths, and
  differing machine GAC-count and PDB-build-path snapshots) that do not touch
  the C# ExpressionBuilder path; the 8 new tests pass. The standing CLI
  baselines are unchanged (`--csharp mscorlib` 10106366 bytes, `--il`
  41246545 bytes, `-l c` 109438; the `--json`-mode argument-parsing path is
  untouched).
- **`ExpressionBuilder` VisitLdLen arm + the no-op EnsureTargetNotNullable
  helper** -- `VisitLdLen` (ExpressionBuilder.cs lines 3088-3116: the
  `System.Array` hint translate of the array operand, the `ConvertTo(arrayType)`
  when the operand's kind is not Array, the no-op `EnsureTargetNotNullable`, the
  `ResultType == StackType.I4` gate selecting `Length` / `Int32` versus the
  non-I4 `LongLength` / `Int64` -- so the raw native-int `ldlen` also renders
  `LongLength` -- the `arrayType.GetProperties(p => p.Name == memberName)`
  first-or-default lookup, the null-member `ResolveResult(Int32/Int64)` fallback
  versus the `MemberResolveResult(arrayExpr.ResolveResult, member)` arm, and the
  `MemberReferenceExpression(arrayExpr.Expression, memberName)` render with the
  LdLen annotation), plus the private `EnsureTargetNotNullable`
  (lines 2832-2852: the C# body is entirely commented out and returns `expr`
  unchanged, so the port is a no-op that keeps the call-site shape). The arm is
  dead in the CLI path (the Phase-5 seed still drives `--csharp`), so the output
  is unchanged. The MinimalCorlib `System.Array` declares no properties, so the
  unit tests pin the null-member fallback arm; a real corlib's `Array.Length` /
  `LongLength` properties would take the `MemberResolveResult` arm. Verified by
  **4** new gtest cases in 1 suite over the MinimalCorlib fixture (the I4 ->
  `Length`/Int32 render with the identifier target and the IL annotation, the
  I8 -> `LongLength`/Int64 arm, the native-I -> `LongLength`/Int64 arm, and the
  `Visit` dispatch), proven with a branch-swap neuter RED round (3 failures: the
  I4 / I8 / native-I member-name and result-type pins) then restored green.
  Full suite in this environment 12101 ran / 12038 passed / 13 failed / 101
  skipped -- the 13 failures are the same pre-existing environment-dependent
  real-fixture/gold tests recorded for gnhf 112 (missing `System.Private.CoreLib`
  10.0.8, corelib-version mismatch, missing .NET Framework v4.7.2 facades / SDK
  Roslyn paths, and GAC-count / PDB-build-path snapshots) that never touch the
  C# ExpressionBuilder path; the 4 new tests pass. The standing CLI baselines
  are unchanged (`--csharp mscorlib` 10106366 bytes, `--il` 41246545 bytes,
  `-l c` 109438).
- **`ExpressionBuilder` VisitLdElema arm** -- `VisitLdElema`
  (ExpressionBuilder.cs lines 3203-3229: the array operand translate, the
  `arrayExpr.Type as ArrayType` dynamic-cast, the
  `TypeUtils.IsCompatibleTypeForMemoryAccess(arrayType.ElementType, inst.Type)`
  gate rebuilding `new ArrayType(compilation, inst.Type, inst.Indices.Count)`
  (the port's `ArrayType(element, rank)` -- no compilation field) and
  `ConvertTo`-ing the operand when the element types are incompatible, the
  `IndexerExpression` over the `TranslateArrayIndex`-translated indices, the
  `ResolveResult(arrayType.ElementType)` annotation, and the
  `DirectionExpression(Ref)` / `ByReferenceResolveResult` managed-reference
  wrapper). The C# `inst.WithSystemIndex` arm is UNREACHABLE in the port (the
  LdElema node carries no such field), so every index goes through
  `TranslateArrayIndex`. The arm is dead in the CLI path (the Phase-5 seed still
  drives `--csharp`), so the output is unchanged. Verified by **5** new gtest
  cases in 1 suite over the MinimalCorlib fixture (the single-index `ref arr[0]`
  shape -- DirectionExpression(Ref) over the IndexerExpression with the
  identifier target and the int32 index, the multi-index argument list, the
  incompatible-element `object[]`-as-`int` arm inserting the array-type cast, the
  LdElema annotation on the indexer, and the `Visit` dispatch), proven with a
  direction-flip neuter RED round (4 failures) then restored green. Full suite in
  this environment 12106 ran / 12043 passed / 13 failed / 101 skipped -- the 13
  failures are the same pre-existing environment-dependent real-fixture/gold
  tests recorded for gnhf 112/113 that never touch the C# ExpressionBuilder
  path; the 5 new tests pass. The standing CLI baselines are unchanged
  (`--csharp mscorlib` 10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
- **`ExpressionBuilder` unbox/box/cast arms** -- `VisitUnboxAny`
  (ExpressionBuilder.cs lines 3285-3320: the
  `inst.Argument is IsInst isInst && IsUnboxAnyWithIsInst(inst, isInst.Type)`
  rewrite to `expr as T` through `UnwrapBoxingConversion`, the general arm's
  TypeParameter `ResolveCast`/`EffectiveBaseClass` fallback versus the
  object-convert, and the `CastExpression` with the UnboxingConversion resolve
  result), `VisitBox` (lines 3332-3352: the NativeIntegers IntPtr/UIntPtr ->
  nint/nuint substitution, the `ConvertTo(targetType)`, and the object
  `CastExpression` with the BoxingConversion resolve result), and `VisitCastClass`
  (lines 3354-3357: the `Translate(inst.Argument).ConvertTo(inst.Type)`
  passthrough). The port has no separate `Unbox` IL node -- the IL reader folds
  `unbox` into `UnboxAny` -- so there is no `VisitUnbox` arm to port; the
  `IsUnboxAnyWithIsInst` and `UnwrapBoxingConversion` static helpers were already
  present. The arms are dead in the CLI path (the Phase-5 seed still drives
  `--csharp`), so the output is unchanged. Verified by **5** new gtest cases in 1
  suite over the MinimalCorlib fixture (the same-reference-type isinst rewrite to
  AsExpression with its IL annotation, the general object -> Int32 cast with the
  UnboxingConversion resolve result, the Int32 -> object box with the
  BoxingConversion resolve result, the Object -> String castclass ConvertTo, and
  the `Visit` dispatch for all three opcodes), proven with a 4-behavior neuter
  RED round (4 failures: the isinst condition, both conversion kinds, and the
  castclass ConvertTo) then restored green. Full suite in this environment 12111
  ran / 12048 passed / 13 failed / 101 skipped -- the 13 failures are the same
  pre-existing environment-dependent real-fixture/gold tests recorded for
  gnhf 112/113/114 that never touch the C# ExpressionBuilder path; the 5 new
  tests pass. The standing CLI baselines are unchanged (`--csharp mscorlib`
  10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
- **`ExpressionBuilder` nullable `?.` rewrap/unwrap arms** -- `VisitNullableRewrap`
  (ExpressionBuilder.cs lines 4298-4309: the
  `UnaryOperatorExpression(NullConditionalRewrap)` over the argument, with the
  result type wrapped through `NullableType.Create(compilation, type)` when
  `NullableType.IsNonNullableValueType(arg.Type)` holds) and
  `VisitNullableUnwrap` (lines 4311-4321: the
  `UnaryOperatorExpression(NullConditional)` over the argument -- the
  `RefInput && !RefOutput` managed-reference strip via `UnwrapChild` -- with the
  `NullableType.GetUnderlyingType(arg.Type)` result). The debug `Translate`
  post-condition enforces the rewrap's nullable result type and the unwrap's
  unwrapped result type, so a neuter that skips the Nullable wrap or the
  RefInput strip trips the assert rather than failing gracefully; the RED round
  therefore swaps the two operators. The arms are dead in the CLI path (the
  Phase-5 seed still drives `--csharp`), so the output is unchanged. Verified by
  **5** new gtest cases in 1 suite over the MinimalCorlib fixture (the
  non-nullable `int` -> `Nullable<int>` rewrap, the reference-type passthrough
  rewrap, the `Nullable<int>` unwrap to `int`, the RefInput DirectionExpression
  strip leaving the inner indexer, and the `Visit` dispatch for both opcodes),
  proven with a two-operator-swap neuter RED round (4 failures) then restored
  green. Full suite in this environment 12116 ran / 12053 passed / 13 failed /
  101 skipped -- the 13 failures are the same pre-existing
  environment-dependent real-fixture/gold tests recorded for gnhf 112-115 that
  never touch the C# ExpressionBuilder path; the 5 new tests pass. The standing
  CLI baselines are unchanged (`--csharp mscorlib` 10106366 bytes, `--il`
  41246545 bytes, `-l c` 109438).
- **`ExpressionBuilder` VisitNullCoalescingInstruction arm** --
  `VisitNullCoalescingInstruction` (ExpressionBuilder.cs lines 3912-3954: the value
  and fallback translates, the `AdjustConstantExpressionToType(fallback, value.Type)`
  constant re-typing, the resolver's `ResolveBinaryOperator(NullCoalescing)` over
  the two resolve results, and the `rr.IsError` fallback -- the ThrowExpression
  NoType fallback recovering `NullableType.GetUnderlyingType(value.Type)`, the
  differing-non-null operand types taking the `FindType(inst.UnderlyingResultType)`
  lookup, else the non-null operand; then the Kind-dependent conversions (a
  non-Ref kind converting the value to `NullableType.Create(compilation, targetType)`,
  the Nullable kind converting it again, the other kinds converting the fallback
  to `targetType`) and the final `BinaryOperatorExpression(NullCoalescing)` with the
  resolver result or the `ResolveResult(targetType)`). The arm is dead in the CLI
  path (the Phase-5 seed still drives `--csharp`), so the output is unchanged.
  Verified by **4** new gtest cases in 1 suite over the MinimalCorlib fixture (the
  Ref kind's `a ?? b` render with the identifier operands and IL annotation, the
  Nullable kind's `Nullable<int> ?? Nullable<int>` render, the ThrowExpression
  fallback recovering the underlying Int32, and the `Visit` dispatch), proven with
  an operator-swap neuter RED round (3 failures) then restored green. Full suite in
  this environment 12120 ran / 12057 passed / 13 failed / 101 skipped -- the 13
  failures are the same pre-existing environment-dependent real-fixture/gold tests
  recorded for gnhf 112-116 that never touch the C# ExpressionBuilder path; the 4
  new tests pass. The standing CLI baselines are unchanged (`--csharp mscorlib`
  10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
- **`ExpressionBuilder` VisitUserDefinedLogicOperator arm + the node IMethod form**
  -- `VisitUserDefinedLogicOperator` (ExpressionBuilder.cs lines 1233-1257: both
  operands translated with their `Method.Parameters[n].Type` hints and converted to
  them, the `op_BitwiseAnd`/`op_BitwiseOr` name dispatch to the
  ConditionalAnd/ConditionalOr operator (else the C# `InvalidOperationException`,
  ported to `std::logic_error`), and the `InvocationResolveResult(null, Method,
  [left, right])` over the converted operand resolve results), plus the
  `UserDefinedLogicOperator` IL node's real-`IMethod` construction form (the
  `UserDefinedCompoundAssign` gnhf-110 precedent: a `std::shared_ptr<IMethod>
  Method` field and an IMethod-taking ctor that derives the `ReflectionName::Name`
  dump stand-in; the new `Instructions/UserDefinedLogicOperator.cpp`). The seed
  string-stand-in node (no resolved method) throws the loud `std::logic_error`
  deferral, since the C# Visit consumes the method's parameters unconditionally.
  The arm is dead in the CLI path (the Phase-5 seed still drives `--csharp`), so
  the output is unchanged. Verified by **5** new gtest cases in 1 suite over the
  MinimalCorlib fixture (the op_BitwiseAnd -> `&&` and op_BitwiseOr -> `||` renders
  over real FakeMethods returning their operand type, the InvocationResolveResult's
  two arguments, the invalid-method-name throw, the seed deferral throw, and the
  `Visit` dispatch), proven with a name-dispatch neuter RED round (2 failures) then
  restored green. Full suite in this environment 12125 ran / 12062 passed / 13
  failed / 101 skipped -- the 13 failures are the same pre-existing
  environment-dependent real-fixture/gold tests recorded for gnhf 112-117 that
  never touch the C# ExpressionBuilder path; the 5 new tests pass. The standing
  CLI baselines are unchanged (`--csharp mscorlib` 10106366 bytes, `--il`
  41246545 bytes, `-l c` 109438).
- **`ExpressionBuilder` VisitRefAnyType arm** -- `VisitRefAnyType`
  (ExpressionBuilder.cs lines 3386-3394: the `__reftype(arg).TypeHandle` render --
  an UndocumentedExpression(RefType) over the translated argument, the TypeHandle
  `MemberReferenceExpression`, and the `TypeResolveResult` over the resolved
  System.RuntimeTypeHandle (the same modules-scan `FullTypeName` lookup the
  VisitLdTypeToken arm uses)). The neighboring reference-family arms
  (`VisitArglist`/`VisitMakeRefAny`/`VisitRefAnyValue`) stay deferred (their IL
  nodes are unported), as does the `RefAnyType` source form's C# `Detach()` -- the
  port's translated argument is an unparented root, so it is added directly. The
  arm is dead in the CLI path (the Phase-5 seed still drives `--csharp`), so the
  output is unchanged. Verified by **2** new gtest cases in 1 suite over the
  MinimalCorlib fixture (the RefType UndocumentedExpression with its single
  identifier argument, the TypeHandle member, the RuntimeTypeHandle result, and
  the IL annotation; plus the `Visit` dispatch), proven with a RefType -> RefValue
  neuter RED round (1 failure) then restored green. Per the standing
  faster-partial-test policy the full suite was not re-run this iteration; the
  targeted run is **178/178** ExpressionBuilder tests green plus the broader
  `*CSharp*:*Resolver*:*OutputVisitor*` filter (52s) with only the known
  environment GAC-snapshot failure, and the standing CLI baselines are unchanged
  (`--csharp mscorlib` 10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
  The build also gains `gtest_discover_tests(ilspy_tests DISCOVERY_TIMEOUT 120)`:
  the default 5s discovery timeout is exceeded by the 12k-test `--gtest_list_tests`
  enumeration, which had been failing the post-link discovery step.
- **`ExpressionBuilder` VisitBlock InterpolatedString arm +
  TranslateInterpolatedString** -- `VisitBlock` (ExpressionBuilder.cs lines
  3406-3421: the BlockKind dispatch) with the
  `TranslateInterpolatedString` arm (lines 3423-3462: the
  DefaultInterpolatedStringHandler AppendLiteral/AppendFormatted call sequence --
  the `{`/`}` brace escaping, the AppendFormatted value converted to the handler
  call's parameter type, and the alignment/suffix overloads -- rendered as an
  `InterpolatedStringExpression` over `InterpolatedStringText`/`Interpolation`
  with the String resolve result). The C# reads `call.Method.Name` /
  `call.GetParameter(1).Type`; the port reads the short name after `::` off the
  Call node's `MethodName` and the parameter type off `ParameterIType` (the Call
  node carries no resolved IMethod). Only this BlockKind is ported: the other
  arms depend on the unported `Match*` helper family (`TranslateArrayInitializer`,
  `TranslateStackAllocInitializer`, `TranslateWithInitializer`) or
  `CallBuilder::Build` (`TranslateObjectAndCollectionInitializer`,
  `TranslateSetterCallAssignment`, `TranslateCallWithNamedArgs`) and fall to the
  Visit-Default error expression. The arm is dead in the CLI path (the Phase-5
  seed still drives `--csharp`), so the output is unchanged. **CallBuilder is the
  named next big unlock but is NOT a single manageable slice:** its `Build` path
  needs unported prerequisites (`ExpressionBuilder::TranslateTarget`, the
  `ILInstruction::Match*` family, `CallInstruction.ExpectedTypeForThisPointer`,
  and a resolved `IMethod` on the `Call` node) -- it is a multi-iteration
  sequence, of which this is the first reachable in-order piece. Verified by
  **4** new gtest cases in 1 suite over the MinimalCorlib fixture (the literal +
  formatted content with the brace escaping, the alignment/suffix overload, the
  `Visit` dispatch, and the unsupported-BlockKind error), proven with a two-arm
  neuter RED round (2 failures) then restored green. Partial-test policy: the
  targeted run is **182/182** `ExpressionBuilder*` tests green plus the broader
  `*CSharp*:*Resolver*:*OutputVisitor*` filter (74s) with only the known
  environment GAC-snapshot failure, and the standing CLI baselines are unchanged
  (`--csharp mscorlib` 10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
- **CallBuilder prerequisite: `TranslateTarget` + `ExpectedTypeForThisPointer` +
  the `Match*` subset** -- `ExpressionBuilder::TranslateTarget`
  (ExpressionBuilder.cs lines 2734-2831: the `base`-reference arm -- a
  non-virtual `this` target whose declaring type differs from the current type,
  with the non-interface `DirectBaseTypes.FirstOrDefault()` and the
  `ThisResolveResult`; the instance arm -- the
  `Call::ExpectedTypeForThisPointer`-driven ref/pointer type hint, the
  value-type `ByReferenceType` re-typing via `NormalizeTypeVisitor.TypeErasure`,
  the `(ref x).member => x.member` DirectionExpression unwrap, and the
  `(ref x)?.member => x?.member` NullConditional rewrite; and the static arm --
  the declaring-type `TypeReferenceExpression`), `IL::Call::ExpectedTypeForThisPointer`
  (the C# `CallInstruction` static, lines 107-122: Ref for constrained /
  type-parameter / value types, O for reference types, Unknown otherwise), and
  the file-local `MatchLdThis` / `MatchBox` / `MatchLdObj` helpers (the generated
  `PatternMatching.cs` out-parameter forms). The IL reader is aligned to the C#
  `CreateILVariable` convention so `MatchLdThis` is faithful and unambiguous: the
  `this` parameter now carries `Index = -1` and the declared parameters carry
  0-based semantic indices (the raw `s.parameters` array slots are unchanged; the
  IL dump and the full transform pipeline are byte-identical). These are the
  first three prerequisites of the CallBuilder `Build` path; the next pieces are
  `BuildArgumentList` then the `Build(OpCode, IMethod, ...)` core. Verified by
  **8** new gtest cases in 1 suite over the MinimalCorlib fixture (the four
  `ExpectedTypeForThisPointer` arms -- reference / value / constrained / unknown;
  the instance-identifier, static-type-reference, value-type managed-reference
  unwrap, and `base`-reference `TranslateTarget` arms), proven with a two-arm
  neuter RED round (4 failures: the O/Ref swap and the base-reference gate) then
  restored green. Partial-test policy: **190/190** `ExpressionBuilder*` tests
  green; the broader `*CSharp*:*Resolver*:*OutputVisitor*` filter (57s) with only
  the known environment GAC-snapshot failure; and the IL/transform safety filter
  `*Transform*:*ILAst*:*ILReader*:*ILFunction*:*Variable*` (968 tests / 967
  passed / 1 skipped / 0 failed) covering the reader change. The standing CLI
  baselines are unchanged (`--csharp mscorlib` 10106366 bytes, `--il` 41246545
  bytes, `-l c` 109438).
- **CallBuilder prerequisite: `ArgumentList` + `ExpectedTargetDetails`** -- the
  `CallBuilder` nested data holders (CallBuilder.cs lines 42-200):
  `ExpectedTargetDetails` (the call opcode + the
  `NeedsBoxingConversion` flag) and `ArgumentList` (the translated arguments,
  expected parameters, parameter/argument names, the `FirstOptionalArgumentIndex`
  truncation, the `IsPrimitiveValue` `BitSet`, and the named/optional/
  expanded-form bookkeeping) with its accessors `GetActualArgumentCount`,
  `GetArgumentNames` (the primitive-value name fill), `GetArgumentResolveResults`
  (the out-parameter `OutVarResolveResult` substitution),
  `GetArgumentResolveResultsDirect`, `GetArgumentExpressions` (the
  `NamedArgumentExpression` wrapping and the implicit-typed-out annotation),
  `CanInferAnonymousTypePropertyNamesFromArguments`, and
  `CheckNoNamedOrOptionalArguments`. The name arrays port to
  `std::vector<std::string>` with empty-as-null (the `IsNullOrEmpty` convention).
  The port also gains the public `CSharp::GetSharedResolveResult(node)` (the
  owning shared handle behind a node's resolve-result annotation, mirroring the
  ExpressionBuilder's file-local `SharedResolveResultAnnotation`) and
  `UseImplicitlyTypedOutAnnotationHandle()` (the non-owning shared handle to the
  annotation singleton, the `CheckedAnnotationHandle` convention). This is the
  data-holder prerequisite `BuildArgumentList` constructs; the remaining blocker
  for `BuildArgumentList` itself is a resolved `IMethod` on the `Call` node (the
  reader is deliberately type-system-free, so the method must be resolved at
  visit time). Verified by **8** new gtest cases in 1 suite over the MinimalCorlib
  fixture (the length / optional-index truncation, the primitive-value name fill,
  the out-var substitution, the direct resolve results, the named-argument
  wrapping, the implicit-out annotation, the anonymous-type name inference, and
  the no-named-or-optional assert), proven with a three-arm neuter RED round
  (3 failures: the optional-index truncation, the out-var substitution, and the
  named-argument wrapping) then restored green. Partial-test policy: **198/198**
  `ExpressionBuilder*` + `CallBuilder*` tests green; the broader
  `*CSharp*:*Resolver*:*OutputVisitor*` filter (59s) with only the known
  environment GAC-snapshot failure; the standing CLI baselines are unchanged
  (`--csharp mscorlib` 10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
- **Call resolved-method construction form** -- the `Call` node gains the C#
  `CallInstruction.Method` (`std::shared_ptr<IMethod> Method`, null on the seed
  string stand-in form) and the `Call(IMethod, bool isNewObj)` ctor
  (CallInstruction.cs), out-of-line in a new
  `Decompiler/IL/Instructions/Call.cpp`. The ctor derives every stand-in field
  the C# derives from the method: `MethodName`
  (`DeclaringType.ReflectionName()::Name`), `DeclaringType`, `ReturnIType`,
  `ParameterIType` (the `Method.Parameters` types, no implicit `this`),
  `IsInstanceCall` (`!(Method.IsStatic || NewObj)`), `IsNewObj`, `IsOperator`
  (the real `Method.IsOperator`, replacing the reader's name heuristic for the
  resolved form), `TypeArgumentsCount` (`Method.TypeArguments.Count`), and
  `ReturnType` (the C# `ResultType`: `DeclaringType.GetStackType()` for a
  `newobj`, else `Method.ReturnType.GetStackType()`). This is the resolved
  `IMethod` the `CallBuilder::Build` path and the
  VisitCall/VisitCallVirt/VisitNewObj arms consume (the
  UserDefinedCompoundAssign gnhf-110 / UserDefinedLogicOperator gnhf-118 node
  upgrade precedent). The reader keeps the string form (it stays type-system-free
  per D78); a future visitor resolves its token to an `IMethod` and uses this
  ctor. Verified by **6** new gtest cases in 1 suite in
  `CallResolvedMethod_Test.cpp` over a `FakeMethod` (the derived-field shape, the
  static non-instance call, the newobj value-type declaring-type stack type, the
  operator flag, the type-argument count, and the seed string form), proven with
  a three-arm neuter RED round (3 failures: the static instance-call gate, the
  newobj ResultType, and the type-argument count) then restored green.
  Partial-test policy: the new 6 green; the
  `*CSharp*:*ExpressionBuilder*:*Resolver*:*OutputVisitor*` filter (64s) with
  only the known environment GAC-snapshot failure; the IL-layer safety filter
  `*Call*:*Transform*:*ILAst*:*ILReader*:*ILFunction*:*Clone*` (1356 tests, all
  passed); the standing CLI baselines are unchanged (`--csharp mscorlib`
  10106366 bytes, `--il` 41246545 bytes, `-l c` 109438).
- **`CSharp/Resolver` leaves (in progress -- the `CSharpResolver` dependency
  surface)** -- `cpp/Decompiler/CSharp/Resolver/` now holds **13** ported leaves
  toward the `CSharpResolver` leaf deps (the long-pole remaining blocker of
  `TypeSystemAstBuilder` / `CSharpAmbience`): the twin Alias `ResolveResult`
  subclasses `AliasTypeResolveResult` + `AliasNamespaceResolveResult`
  (`: Semantics::TypeResolveResult` / `: NamespaceResolveResult`, D467); the
  twin enum leaves `NameLookupMode` (the 5-value lookup-mode enum) +
  `OverloadResolutionErrors` (the 12-flag `[Flags]` error mask, D468); the
  `DynamicMemberResolveResult` (a `: ResolveResult` with `SpecialType.Dynamic`
  base carrying a `Target` child + a `Member` name + a nullable `IMember`
  Symbol, D469); the `AwaitResolveResult` (a `: ResolveResult` carrying the
  `GetAwaiterInvocation` + `AwaiterType` + 3 nullable awaiter-pattern
  members with the `IsError` `TypeKind.Dynamic` short-circuit crux, D470); the
  `DynamicInvocationResolveResult` + its co-located `DynamicInvocationType`
  enum (a `: ResolveResult` with `SpecialType.Dynamic` base carrying a
  `Target` + `InvocationType` + `Arguments`/`InitializerStatements` lists +
  a nullable `Symbol`, D471); the `CSharpInvocationResolveResult` (an
  `: InvocationResolveResult` D438 carrying the `OverloadResolutionErrors` mask
  + 3 C#-specific bools + the `argumentToParameterMap`, with the `IsError`
  crux and the property-name-shares-enum-type `Errors` type-alias fix, D472);
  and the `LambdaResolveResult` + `LambdaConversion` pair (D473): the ABSTRACT
  `LambdaResolveResult : ResolveResult` (the anonymous-method/lambda resolve
  result -- the C# ctor forwards `SpecialType.NoType`, a lambda has NO type;
  declares the eight-member pure-virtual lambda surface `HasParameterList` /
  `IsAnonymousMethod` / `IsImplicitlyTyped` / `IsAsync` /
  `GetInferredReturnType(IType[])` / `Parameters` / `ReturnType` /
  `IsValid(IType[], IType, CSharpConversions)` / `Body`, with the one concrete
  member `GetChildResults()` => `{ Body }`; `CSharpConversions` is
  forward-declared for the unported ~2500-line conversion controller the
  `IsValid` signature references) and the internal singleton
  `LambdaConversion : Conversion` (`IsAnonymousFunctionConversion` +
  `IsImplicit` both true, the Meyers-singleton `Instance()` preserving the C#
  `static readonly` field's reference identity). The third class in the C#
  file, the concrete `DecompiledLambdaResolveResult`, is DEFERRED: its
  `IsAsync`/`Parameters`/`ReturnType` delegate to `ILFunction.IsAsync`/
  `ILFunction.Parameters`/`ILFunction.ReturnType` (the port's `ILFunction`
  carries none of those surfaces) and its `IsValid` composes
  `CSharpConversions.IdentityConversion`/`ImplicitConversion` (unported); and
  the `MethodListWithDeclaringType` + `MethodGroupResolveResult` pair (D474):
  `MethodListWithDeclaringType` (the per-declaring-type method bucket -- the C#
  `class MethodListWithDeclaringType : List<IParameterizedMember>` ports to a
  `std::vector<const IParameterizedMember*>`-derived value type carrying a
  `DeclaringType()`; the type system owns the methods, the bucket observes
  them) and `MethodGroupResolveResult : ResolveResult` (the method group a
  delegate-creating method reference resolves to -- the base is
  `SpecialType.NoType`, a method group has NO type, distinct from
  `UnknownType`; carries the nullable `TargetResult` `shared_ptr` + the
  `TargetType` null-target short-circuit to `SpecialType.UnknownType`, the
  `MethodName`, the `Methods` flatten across declaring-type buckets [the
  `SelectMany(...).Cast<IMethod>()` -- a `static_cast<const
  IParameterizedMember*>` to `const IMethod*` up the single-inheritance chain,
  requiring both `IMethod.hpp`/`IParameterizedMember.hpp` INCLUDED, not
  forward-declared] and `MethodsGroupedByDeclaringType` [base types first],
  the `TypeArguments` snapshot, the nullable `ChosenMethod`, the
  `WithChosenMethod` runtime-type-preserving clone, the custom
  `"[MethodGroupResolveResult with N method(s)]"` `ToString`, and the
  `GetChildResults` => `{ target }` / empty). DEFERRED from the C# file: the
  extension-method machinery (`GetExtensionMethods`'s resolver-fetch +
  `GetEligibleExtensionMethods`) and `PerformOverloadResolution` -- they need
  `OverloadResolution`/`TypeInference`/`CSharpConversions` (all unported); a
  resolver-less `GetExtensionMethods()` returns empty; and the `MemberLookup`
  accessibility surface (D475): the `IsInvocable` static helper (`member is
  IEvent || member is IMethod` via SymbolKind dispatch, else the
  Dynamic/Delegate/FunctionPointer return-type arm), the nullable
  currentTypeDefinition / currentModule / isInEnumMemberInitializer ctor, the
  two `IsProtectedAccessAllowed` overloads (a ThisResolveResult target always
  allows; an IType target unwraps a single type-parameter
  EffectiveBaseClass then walks the current type's DeclaringTypeDefinition
  chain with IsDerivedFrom, null-definition and null-context short-circuits),
  the C# 4.0 spec 3.5.2 `IsAccessible` switch (None / Private [the
  outer-class walk] / Public / Protected / Internal /
  ProtectedOrInternal / ProtectedAndInternal), and the two private helpers
  `IsInternalAccessible` (InternalsVisibleTo over the two modules) and
  `IsProtectedAccessible` (IsStatic / TypeDefinition-force of
  allowProtectedAccess + the declaring-type-definition equality and
  derivation walks). The port unblocks `TypeSystemAstBuilder::
  TypeDefinitionNameableInBaseList`'s `lookup.IsAccessible(td, false)` call.
  DEFERRED from the C# file: the LookupGroup / GetAccessibleMembers /
  LookupType / Lookup region (it needs the GetMembers / GetNestedTypes
  GetMemberOptions member-enumeration surface on IType and its definitions,
  which the minimal port does not carry).
  The MemberLookup port required the previously-deferred base-type traversal
  surface (D475): `IType::DirectBaseTypes()` (virtual-with-default {}, the
  DirectBaseTypes() + GetDefinition()/IsReferenceType() precedent), the
  `TypeSystem/Implementation/BaseTypeCollector` (the DFS add-at-the-end
  traversal with the active-types cycle guard and the duplicate-output
  suppression; pointer identity, NOT structural equality, for both -- the C#
  default IType.Equals is structural, but identity preserves the two
  documented guarantees it protects and avoids collapsing distinct
  structurally-equal instances), and the base-type region of
  `TypeSystemExtensions` (GetAllBaseTypes / GetNonInterfaceBaseTypes /
  GetAllBaseTypeDefinitions [Select(GetDefinition).Where(!=null).Distinct()] /
  IsDerivedFrom x2, with the null-type std::invalid_argument, the
  cross-compilation std::runtime_error, and the KnownTypeCode FindType
  lookup). The new test stubs needed two MSVC complete-class-lookup
  accommodations not previously hit in the port: a self-named accessor's
  return type (`SymbolKind SymbolKind()`, `Accessibility Accessibility()`,
  ...) must be written qualified (`TS::SymbolKind`) in every class that
  declares such an accessor, INCLUDING the ctor parameter types and field
  declarations that follow it (MSVC looks the names up in the complete-class
  context, where the member name shadows the type).
  and the `Log` helper (D476): the resolver's opt-in debug logging wrapper
  around `System.Diagnostics.Debug` -- a `final`, non-instantiable
  all-static class carrying the compile-time off-switch `IsEnabled == false`
  (the C# `const bool logEnabled = false`), the two-`WriteLine`-overloads-
  collapsed-into-one variadic `{N}`-format `WriteLine`, the range-template
  `WriteCollection` (the `<empty collection>` marker / text-line-first-
  element / `text.Length`-padded continuation layout factored into a
  unit-testable `LogDetail::FormatCollectionLines`), and `Indent` /
  `Unindent` (a private indent level rendered as level * 4 spaces, clamping
  at 0 like the .NET `Trace.IndentLevel` setter). The C# `[Conditional(
  "LOG_DISABLED")]` call-site elision ports to per-method discarded
  `if constexpr (IsEnabled)` branches (every call compiles to nothing in
  the committed configuration; the documented divergence: the ARGUMENTS are
  still evaluated at the call site, unlike the C# attribute -- caller-side
  `if constexpr (Log::IsEnabled)` wrappers restore full elision parity when
  the still-unported `OverloadResolution` / `TypeInference` logging trails
  land). The `LogDetail` helpers (the `string.Format` `{N}` stand-in
  [unmatched placeholders are left unreplaced, never throw], the
  `object.ToString()`-dispatch / null-`shared_ptr` `"<null>"` marker) are
  unconditionally compiled and directly unit-tested, so flipping the switch
  exercises an already-tested path.
  All 13 are header-only (TypeSystemExtensions additionally has a .cpp) and
  dead in the CLI call graph (the CLI uses the seed,
  not the `Resolver` leaves), confirmed by the byte-identical `--csharp`
  output. The 13 leaves add **213 gtest cases** across 12 test suites.
  The member-enumeration surface the `MemberLookup` Lookup region
  enumerates members through (D477): the `GetMemberOptions` `[Flags]`
  enum (`None = 0x00` / `ReturnMemberDefinitions = 0x01` /
  `IgnoreInheritedMembers = 0x02`; the C# `int` default backing, plus the
  `|` / `&` / `~` operators the C# `[Flags]` enum has implicitly) and the
  nine `IType` member-enumeration declarations (`GetNestedTypes` x2 /
  `GetConstructors` / `GetMethods` x2 / `GetProperties` / `GetFields` /
  `GetEvents` / `GetMembers` / `GetAccessors`), ported as `virtual`-with-
  default leaves the D406 flattened-`AbstractType` convention dictates:
  the seven specific families take the C# `AbstractType` empty default
  (their C# `Delegate<Predicate>` filters port to by-value
  `std::function<bool(const T*)>` + a default `nullptr` "no filter"
  sentinel, their C# `GetMemberOptions` default values port verbatim --
  notably `GetConstructors` binding `IgnoreInheritedMembers`, the ONE
  family defaulting off `None`), and `GetMembers` defaults to the C#
  `AbstractType.GetMembers` virtual composition of the four families
  (`GetMethods.Concat(GetProperties).Concat(GetFields).Concat(GetEvents)`)
  out-of-line in `IType.cpp` (the composed `const IMethod*` /
  `const IProperty*` / `const IField*` / `const IEvent*` -> `const
  IMember*` up-casts need the member-family headers COMPLETE, and they
  transitively include `IType.hpp` -- a header-side composition would
  cycle the includes; the `.cpp` definition is where they can be include-
  d). Concrete types with real members override the families (and the
  base compositions sees them aggregated, faithful to the C#). The
  concrete routing (`GetMembersHelper` + `SpecializedMethod`/
  `SpecializedProperty`/`SpecializedField`/`SpecializedEvent`
  specialization) remains the next in-order leaf. One pre-existing latent
  ODR violation surfaced: `IAttribute_Test.cpp` carried a namespace-scope
  minimal `IMethod` stand-in (dropped per its own fixture comment now
  that the real `IMethod.hpp` has landed -- the linked binary otherwise
  saw two `TS::IMethod` definitions the moment `IType.cpp` started to
  reference the real ones, an UB vtable collision that crashed under
  `IAttributeTest`'s second case); the file now uses the reusable
  `LookupMethod` stub from `LookupStubs.hpp`. **33** new gtest cases
  across 3 (`GetMemberOptionsTest`) / 2 (`MemberEnumerationDefaultTest` +
  `MemberEnumerationDispatchTest`) suites cover the `[Flags]` literals,
  the virtual dispatch through a `IType&` base reference + the per-family
  default-options binding, and the composed-`GetMembers` shape; the suite
  went RED first (`GetMemberOptions` not a member of the namespace)
  before the header port made it green. The remaining small
  `CSharp/Resolver` `ResolveResult` subclass
  (`DecompiledLambdaResolveResult` [blocked on the `ILFunction` async/parameter
  surfaces + `CSharpConversions`]) and the larger `MemberLookup` helper's
  `LookupGroup` region (~600 lines, now unblocked for the
  member-DECLARATION surface and for `SignatureComparer.Ordinal` -- D478;
  still blocked on the `GetMembersHelper` routing + `SpecializedMethod`/
  `SpecializedProperty`/`SpecializedField`/`SpecializedEvent`
  specialization) are the
  subsequent in-order targets,
  advancing the `CSharpResolver` dependency surface ahead of the full
  2986-line `CSharpResolver` class.
  The `SignatureComparer.Ordinal` comparer surface the `LookupGroup` region
  (and `InheritanceHelper.GetBaseMembers`) binds against (D478):
  `TypeSystem/ParameterListComparer.{hpp}` holds BOTH of the C# file's
  comparers -- `ParameterListComparer` (the `Instance` / `WithOptions`
  factories; the include-modifiers `ref`/`out`+`params` arm; the
  per-element normalized type compare) and `SignatureComparer` (the
  name-comparer + kind + method type-parameter count + the parameter-list
  compare, and the name-hash x 33 mix), `TypeSystem/NormalizeTypeVisitor.{hpp,cpp}`
  is the shape-eraser the comparers visit every parameter type through (the
  eight public option fields verbatim, the `TypeErasure` /
  `IgnoreNullabilityAndTuples` / `IgnoreNullability` Meyers-singleton
  configurations, the opposite-direction object -> `SpecialType.Dynamic`
  mapping, the IntPtr/UIntPtr -> nint/nuint arms, tuple -> underlying
  `ValueTuple`, modifier and nullability removal), and
  `Implementation/DummyTypeParameter.{hpp,cpp}` is the placeholder type
  parameter the normalizer substitutes by (owner-kind, index) -- the two C#
  `Interlocked.CompareExchange`-grown static arrays port to mutex-grown
  static vectors (stable unique instance per pair, so identity equals the
  C# reference identity). The port required three small IType-surface
  additions: `IType::Nullability` (the flattened-`AbstractType` `Oblivious`
  default virtual, overridden implicitly by `NullabilityAnnotatedType`),
  `IType::ChangeNullability` (the `AbstractType` return-this default plus
  the faithful `NullabilityAnnotatedType` / `SpecialType` [Dynamic-only
  wrap] / `ParameterizedType` / `ModifiedType` / `UnknownType`-class
  rebuild overrides; the port's `ArrayType` carries no nullability field,
  so its non-Oblivious arm inherits the default -- documented divergence),
  and the `NullabilityAnnotatedTypeParameter` wrapper (the C# nested class
  lands in `ITypeParameter.hpp` since it needs `ITypeParameter` complete;
  it carries TWO non-virtual `IType` subobjects, so its `shared_from_this`
  paths are documented as not exercised). **49** new gtest cases across 5
  suites pin the name forms / caches / dispatch, the eight-field
  configurations, the object-vs-dynamic + method-type-parameter
  normalization cruxes, and the comparer + hash contracts. The
  `IType::Nullability` virtual exposed pre-existing test stubs using the
  bare enum name after an inherited accessor; they were namespace-qualified
  (the D372 crux).
  The `ParameterizedType` substitution surface the `GetMembersHelper` routing
  binds against (D479): `GetTypeArgument(int)` (the C# `typeArguments[index]`,
  returning the stored `ITypePtr` by value -- the managed `IType` is shared with
  `TypeArguments()[index]`) and the two `GetSubstitution` overloads
  (`GetSubstitution()` -> `new TypeParameterSubstitution(typeArguments, null)`;
  `GetSubstitution(methodTypeArguments)` -> `new TypeParameterSubstitution(
  typeArguments, methodTypeArguments)`), ported returning the substitution BY
  VALUE (the C# heap allocation realized as a value, the D407 convention; the
  class type arguments are a fresh `std::optional<std::vector<ITypePtr>>` holding
  copies of the type's `typeArgs_`, the method list is `std::nullopt` for the
  no-arg overload or the moved-in argument for the two-arg). The trio is
  OUT-OF-LINE in `IType.cpp`: `TypeParameterSubstitution` is a concrete
  `TypeVisitor` whose header includes `TypeVisitor.hpp`, which includes
  `IType.hpp`, so `IType.hpp` cannot include `TypeParameterSubstitution.hpp`
  without a cycle -- the by-value return type is only FORWARD-DECLARED in the
  header (a member-function declaration permits an incomplete return type),
  and the definitions in `IType.cpp` include `TypeParameterSubstitution.hpp`
  (complete there). `GetMembersHelper.GetMethodsImpl` calls
  `pt.GetSubstitution(methodTypeArguments)`, the `SpecializedMember`
  constructors call `pt.GetSubstitution()`, and `GetNestedTypesImpl` reads
  `pt.GetTypeArgument(i)` -- the leaf is dead in the CLI path (the routing is
  not yet wired) and the `--csharp` output is byte-identical. **10** new gtest
  cases in 1 suite pin the Nth-argument / shared-instance access, the
  absent-vs-present method list, the class/method type-parameter substitution
  semantics (the real correctness check, exercising `VisitTypeParameter`), and
  the faithful-construction equivalence (`GetSubstitution()` equals a directly
  constructed `TypeParameterSubstitution(typeArguments, null)`); the suite went
  RED first (`GetSubstitution` / `GetTypeArgument` not members of
  `ParameterizedType`) before the header + `.cpp` port made it green.
  The first of the `Specialized*` member leaves the `GetMembersHelper`
  routing needs (D480): `Implementation/SpecializedParameter.hpp` -- the
  sealed `IParameter` a `SpecializedParameterizedMember` builds its
  substituted parameter list out of (the C# `new SpecializedParameter(p,
  p.Type.AcceptVisitor(substitution), this)` per parameter). It wraps a base
  `IParameter` and a new `IType`, delegating the whole `IParameter` /
  `IVariable` / `ISymbol` surface to the base EXCEPT `Type` (the new type) and
  `Owner` (the new owning member). HEADER-ONLY (all simple delegations; no
  `TypeVisitor` / complete-type needs beyond `IParameter`): it is not added
  to the ilspy `CMakeLists.txt` (it compiles into each TU that includes it,
  the `ParameterListComparer.hpp` precedent) -- only the test `.cpp` is wired.
  The base parameter is held as an OWNING `std::shared_ptr<IParameter>` (keeps
  the base alive for the parameter's lifetime, the `NullabilityAnnotatedType::
  baseType_` precedent), the new type as an owning `ITypePtr` (`Type()` returns
  `*newType_`, a reference to the same `IType` passed in), and the new owner as
  a NON-OWNING nullable `const IParameterizedMember*` (the `IParameter::Owner`
  "May return null" contract; the specializing member owns the parameter, the
  back-reference is non-owning). The D372 name-shadowing crux applies to the
  `ReferenceKind()` / `SymbolKind()` overrides (the inherited `IParameter::
  ReferenceKind` / `ISymbol::SymbolKind` member names shadow the namespace-scope
  enums in MSVC's complete-class lookup), so both return types are
  GLOBALLY QUALIFIED (the `DummyTypeParameter::SymbolKind` precedent).
  DEFERRED: `ToString()` (the C# delegates to the not-yet-ported
  `DefaultParameter.ToString(this)` static helper, the shared `IParameter`
  signature renderer -- lands with the `DefaultParameter` leaf). The leaf is
  dead in the CLI path (the routing is not yet wired) and the `--csharp`
  output is byte-identical. **15** new gtest cases in 1 suite pin the new-type /
  new-owner overrides, the per-member delegation (incl. `GetAttributes` /
  `GetConstantValue` forwarding), the nullable owner, and the `is_base_of_v` /
  `is_final_v` class shape; the suite went RED first (the header absent) and
  again on the unqualified `ReferenceKind` / `SymbolKind` return types (the
  D372 crux) before the global-qualification fix made it green.
  The `IType::TypeParameters` surface addition (D481) -- the `IType` member
  the `SpecializedMember.DeclaringType` `else` arm reads (`new
  ParameterizedType(definitionDeclaringTypeDef, definitionDeclaringTypeDef
  .TypeParameters).AcceptVisitor(substitution)`). The C# `IReadOnlyList<
  ITypeParameter> TypeParameters` ("Returns an empty list if this type is not
  generic") has the `AbstractType` `EmptyList<ITypeParameter>.Instance` default;
  the port flattens it onto `IType` as a virtual-WITH-DEFAULT returning an empty
  `std::vector<const ITypeParameter*>` (the D406 convention; NON-OWNING raw
  pointers, the `NestedTypes` / `GetMethods` "type system owns the entities,
  caller holds raw pointers" convention -- `const ITypeParameter*` is a complete
  pointer type regardless of `ITypeParameter`'s own completeness, so the
  `std::vector` instantiates with `ITypeParameter` still being defined). `ITypeParameter`
  is now forward-declared in `IType.hpp` (it was only in `TypeVisitor.hpp`); the
  re-declaration in `TypeVisitor.hpp` is harmless (C++ permits repeated forward
  declarations). A concrete `ITypeDefinition` (the `MetadataTypeDefinition`, not
  yet ported) overrides it to return its real type parameters. The dominant C#
  usage is `ITypeParameter`-typed (`.Count` / `[i]` / `GenericContext` / `.Skip`);
  the one `IEnumerable<IType>`-covariance use (the `ParameterizedType` ctor in
  `SpecializedMember`) is a `SpecializedMember`-leaf concern (the `const
  ITypeParameter*` -> `ITypePtr` conversion), not this leaf's. The leaf is dead
  in the CLI path (the `IType` surface addition stays dead in the CLI path --
  the `--csharp` output is byte-identical). **11** new gtest cases in 1 suite pin
  the empty default across the existing concrete `IType` subclasses (incl.
  `ParameterizedType` -- `TypeParameters` the formal params is DISTINCT from
  `TypeArguments` the args), `DummyTypeParameter` (an `ITypeParameter` has no
  params of its own), the virtual dispatch through an `IType&` base reference,
  and the `TypeParameterCount` / `TypeParameters().size()` agreement; the suite
  went RED first (`TypeParameters` not a member of `IType`, the stub `override`
  finding no base) before the virtual-WITH-DEFAULT made it green.
  The abstract `SpecializedMember` base (D482) -- the `IMember` base a
  specialized method / property / field / event derives from
  (`Implementation/SpecializedMember.{hpp,cpp}`). It wraps a base
  (unspecialized) member and a `TypeParameterSubstitution`, delegating the whole
  `IMember` / `IEntity` / `INamedElement` / `ICompilationProvider` / `ISymbol`
  surface to the base EXCEPT `ReturnType` / `DeclaringType` / `Substitution`
  (re-computed through the substitution). The lazy `ReturnType` / `DeclaringType`
  (the C# `LazyInit.VolatileRead` / `GetOrSet`) port to `mutable ITypePtr`
  fields + the `Util::VolatileRead` / `Util::GetOrSet` free functions (the C#
  `static class LazyInit` -> a namespace of free functions); `substitution_` is
  `mutable TypeParameterSubstitution` so the `const` lazy accessors can pass it
  as a non-const `TypeVisitor&` to `IType::AcceptVisitor` (the D406 non-const-
  `TypeVisitor` convention). The `DeclaringType` three reachable arms: a non-
  `ITypeDefinition` declaring type -> `AcceptVisitor`; a generic
  `ITypeDefinition` with MATCHING class args -> `new ParameterizedType(def,
  classArgs)` (the owning `ITypePtr` passed directly -- the `ITypeDefinition` IS-
  an `IType` via `ITypeDefinitionOrUnknown`, no cast needed); a null declaring
  type -> null. The `else` sub-branch (a generic `ITypeDefinition` with
  non-matching class args -> `new ParameterizedType(def, def.TypeParameters).
  AcceptVisitor(substitution)`) is DEFERRED (the port's `IType::TypeParameters`
  is non-owning, but `ParameterizedType` needs owning `ITypePtr`; lands with the
  `MetadataTypeDefinition` ownership work) -- it falls through to `AcceptVisitor`
  (a reasonable fallback; the `GetMembersHelper` routing always supplies
  matching class args, so the deferred arm is never hit there).
  `ExplicitlyImplementedInterfaceMembers` is DEFERRED to forward the base list
  UNSPECIALIZED (the C# `Select(m => m.Specialize(substitution))` needs the
  owning-`Specialize` design -- `IMember::Specialize` returns non-owning; the
  owning design lands with the concrete `Specialized*` leaves); the common
  empty case is faithful. `WrapAccessor` is OMITTED (needs owning-`Specialize`
  for the `LazyInit` cache; lands with `SpecializedProperty` / `SpecializedEvent`).
  `Specialize` delegates to `baseMember.Specialize(Compose(...))` (the `IMember`
  "type system owns" convention). `Equals(IMember*, TypeVisitor*)` / the
  standalone `Equals(const SpecializedMember*)` / `GetHashCode` (pointer-identity
  for the base member, the C# `object.GetHashCode` -> `std::hash<IMember*>`) /
  `ToString` (DEFERRED -- `IType` has no `ToString`) follow. The D372 crux applies
  to `SymbolKind()` / `Accessibility()` (globally qualified). **27** new gtest
  cases in 1 suite pin the ctor asserts (null / `SpecializedMember` base), the
  `Identity`-by-default + `AddSubstitution` composition, the trivial delegations,
  the `ReturnType` substitution effect (a class type parameter -> the class
  arg), the three `DeclaringType` arms + the deferred arm + caching, the
  `Specialize` delegation, the `ExplicitlyImplementedInterfaceMembers` forwarding,
  the `Equals` / `GetHashCode` contracts; the suite went RED first (the header
  absent, then the `Util::` / `const_cast` / `TestSupport` namespace /
  most-vexing-parse / `TypeArguments`-not-on-`IType` cruxes) before the fixes made
  it green (one test needed the same-instance convention for pointer-identity
  hashing, the `TypeParameterSubstitutionTest::GetHashCodeIsConsistent` precedent).
  The first concrete `Specialized*` leaf (D483): `Implementation/SpecializedField.hpp`
  -- the concrete `IField` a `GetMembersHelper.GetFieldsImpl` builds for a field on a
  parameterized type (`new SpecializedField(m, pt.GetSubstitution())`). It derives
  `SpecializedMember, IField`, so it hits the TWO-IMember-SUBOBJECT DIAMOND
  (`SpecializedMember : IMember` + `IField : IMember, IVariable` -> two `IMember`
  subobjects, three `ISymbol` subobjects). The `NullabilityAnnotatedTypeParameter`
  D402 precedent applied to `IMember`: ONE override per method name is the final
  overrider for ALL the subobject vtables (the standard C++ rule), and each
  delegates to the `SpecializedMember::` qualified call (sub A's already-
  implemented override, which itself delegates to `baseMember_`) -- `return
  SpecializedMember::Name();` is a static, qualified dispatch to sub A, NOT a
  virtual re-dispatch. The `IField`-own surface (`IsReadOnly` /
  `ReturnTypeIsRefReadOnly` / `IsVolatile`) and the `IVariable` surface (`IsConst` /
  `GetConstantValue`) delegate to `fieldDefinition_` (an OWNING `shared_ptr<IField>`
  shared with `baseMember_`); the C# `IVariable.Type => this.ReturnType` ports to
  `IVariable::Type()` returning `SpecializedMember::ReturnType()` (the substituted
  type). The D372 crux applies to `SymbolKind()` / `Accessibility()` (globally
  qualified). HEADER-ONLY (all simple delegations; the complex lazy
  `ReturnType` / `DeclaringType` are inherited); not added to the ilspy
  `CMakeLists.txt` (the `SpecializedParameter.hpp` precedent). The two-`IMember`
  diamond makes a `SpecializedField*` -> `IMember*` upcast AMBIGUOUS (which
  `IMember` subobject?) -- the test upcasts through the unambiguous
  `SpecializedMember*` (one `IMember`), the `IField` header-comment diamond-upcast
  convention. DEFERRED: the C# `internal static IField Create(...)` factory (needs
  the owning-`Specialize` design). The leaf is dead in the CLI path (the
  `GetMembersHelper` routing is not yet wired) and the `--csharp` output is
  byte-identical. **13** new gtest cases in 1 suite pin the ctor substitution
  wiring, the trivial delegations, the `ReturnType` / `DeclaringType` substitution
  effect, `IVariable::Type` == `ReturnType`, the `IField`-own / `IVariable` surface
  delegation, the diamond dispatch through `IField*` / `IMember*` (via
  `SpecializedMember*`) / `IVariable*`, the `Specialize` delegation, the inherited
  `Equals` / `GetHashCode`, and the `is_base_of_v` / `is_final_v` class shape; the
  suite went RED first (the `TestSupport` namespace, the ambiguous `IMember*`
  upcast C2594, the `SpecializedMember` using) before the fixes made it green.
  The abstract `SpecializedParameterizedMember` base (D484) -- the
  `SpecializedMember, IParameterizedMember` base adding the lazily-computed
  `Parameters` list (`Implementation/SpecializedParameterizedMember.{hpp,cpp}`).
  `SpecializedMethod` / `SpecializedProperty` derive from it; the `Parameters`
  getter builds the substituted parameter list ONCE (lazily, via `LazyInit`), each
  parameter a `SpecializedParameter` wrapping the base member's parameter with its
  type run through the member's substitution. The C# `IReadOnlyList<IParameter>
  parameters` lazy-cached field ports to a `mutable std::shared_ptr<std::vector<
  std::shared_ptr<IParameter>>> parameters_` -- an OWNING cache (the `shared_ptr`
  owns the vector, the vector owns the `SpecializedParameter` instances);
  `IParameterizedMember::Parameters()` returns a non-owning snapshot, so the
  override builds the owning cache ONCE then returns a snapshot (the expensive
  `SpecializedParameter` build is cached). `CreateParameters` reads the base
  member's parameters via `dynamic_cast<const IParameterizedMember*>(baseMember_)`
  (the C# `((IParameterizedMember)baseMember).Parameters` downcast), and wraps
  each non-owning `const IParameter*` in a `SpecializedParameter` (non-owning
  base pointer, owning new type, `this` as the non-owning `Owner`). The class is
  ABSTRACT (the two-`IMember`-subobject diamond leaves sub B's `IMember` pure-
  virtuals unresolved; it overrides ONLY `Parameters()`); the CONCRETE derived
  leaves add the diamond overrides (the `SpecializedField` pattern) -- the test
  exercises it via a test-only public-ctor subclass with those overrides. The
  `Substitution()` call in `CreateParameters` is qualified `SpecializedMember::
  Substitution()` (ambiguous via the two `IMember` subobjects otherwise).
  REVISION to D480 (a follow-up fix to a committed session leaf, motivated by
  the `CreateParameters` call site): `SpecializedParameter::baseParameter_`
  changed from an OWNING `shared_ptr<IParameter>` to a NON-OWNING `const
  IParameter*` (the faithful counterpart of the C# non-owning `readonly
  IParameter baseParameter` reference; the `SpecializedParameterizedMember` /
  base member owns the base parameter, not the `SpecializedParameter`). The D480
  owning choice was a DEVIATION that the `CreateParameters` call site surfaced
  (`IParameterizedMember::Parameters()` returns non-owning `const IParameter*`,
  which an owning `shared_ptr` ctor cannot take without an aliasing trick); the
  non-owning raw pointer is the faithful, clean design. The D480 test passes
  `.get()` instead of moving the `shared_ptr`. The leaf is dead in the CLI path
  and the `--csharp` output is byte-identical. **8** new gtest cases in 1 suite
  pin the empty-when-base-has-none, the specialized list (count + substituted
  type + base-name delegation), `IVariable::Type` == substituted type, the
  caching (pointer-identity across calls), the `Owner` == the specialized
  member, `CreateParameters` with a custom substitution, the base-surface
  delegation, and the `is_base_of_v` class shape; the suite went RED first (the
  ambiguous `Substitution()` C2385, the `TestSupport` namespace, the D372
  `ReferenceKind` crux in the `TestBaseParameter` stub, the missing
  `SpecializedParameter.hpp` / `KnownAttribute.hpp` includes, the unqualified
  `KnownAttribute` in the diamond overrides, the `{}` `make_shared`
  braced-init-list) before the fixes made it green.
  The second concrete `Specialized*` leaf (D485): `Implementation/SpecializedEvent.hpp`
  -- the concrete `IEvent` a `GetMembersHelper.GetEventsImpl` builds for an event on a
  parameterized type (`new SpecializedEvent(ev, pt.GetSubstitution())`). It derives
  `SpecializedMember, IEvent`, hitting the same TWO-IMember-SUBOBJECT DIAMOND as
  `SpecializedField` (`SpecializedMember : IMember` + `IEvent : IMember` -- two `IMember`
  subobjects; `IEvent` does NOT derive `IVariable`, so NO third `ISymbol` -- simpler than
  `IField`). The `SpecializedField` D483 pattern: ONE delegating override per method name
  (each calls `SpecializedMember::` qualified, sub A's override). The `IEvent`-own bools
  (`CanAdd` / `CanRemove` / `CanInvoke`) delegate to `eventDefinition_` verbatim. The three
  accessor properties (`AddAccessor` / `RemoveAccessor` / `InvokeAccessor`) are DEFERRED
  to return the BASE accessor UNSPECIALIZED -- the C# `WrapAccessor(ref cachingField,
  eventDefinition.AddAccessor)` lazily builds a `SpecializedMethod` via
  `accessorDefinition.Specialize(substitution)` and caches it (an OWNING `LazyInit`
  cache); the port's `IMember::Specialize` returns a NON-OWNING `const IMember*`, so the
  owning-`Specialize` / owning-cache design `WrapAccessor` needs is not yet in place. The
  deferred override delegates to `eventDefinition_->AddAccessor()` (the base accessor,
  unspecialized) -- a documented divergence (the accessor's `DeclaringType` / `ReturnType`
  are not substituted); the `GetMembersHelper` / `LookupGroup` routing (the blocker) does
  NOT use the accessors (it builds the `SpecializedEvent` for the member list, not for
  accessor dispatch), so the divergence is benign for the routing. The C# `internal static
  IEvent Create(...)` factory is DEFERRED (needs the owning-`Specialize` design).
  HEADER-ONLY (the `SpecializedField` precedent); not added to the ilspy `CMakeLists.txt`.
  The leaf is dead in the CLI path and the `--csharp` output is byte-identical. **11** new
  gtest cases in 1 suite pin the ctor substitution wiring, the trivial delegations, the
  `ReturnType` / `DeclaringType` substitution effect, the `IEvent`-own bools delegation,
  the DEFERRED accessor delegation, the diamond dispatch through `IEvent*` / `IMember*`
  (via `SpecializedMember*`), the `Specialize` delegation, the inherited `Equals` /
  `GetHashCode`, and the `is_base_of_v` / `is_final_v` class shape; the suite went RED
  first (the `TestSupport` namespace, the `LookupMethod` accessor stub -- an attempted
  custom `TestAccessorMethod` hit the `IMethod` covariant-`Specialize` / nonexistent-
  `CallingConvention` cruxes, replaced by the reusable `LookupMethod` -- the `Lookup*`
  usings) before the fixes made it green.
  The third concrete `Specialized*` leaf (D486): `Implementation/SpecializedProperty.hpp`
  -- the concrete `IProperty` a `GetMembersHelper.GetPropertiesImpl` builds for a property
  on a parameterized type (`new SpecializedProperty(m, pt.GetSubstitution())`). It derives
  `SpecializedParameterizedMember, IProperty`, hitting the THREE-IMember-SUBOBJECT DIAMOND
  (`SpecializedParameterizedMember : SpecializedMember, IParameterizedMember` +
  `IProperty : IParameterizedMember` -- three `IMember` subobjects: `SpecializedMember`'s
  (sub A), the `IParameterizedMember` inside `SpecializedParameterizedMember` (sub B), and
  `IProperty`'s OWN `IParameterizedMember` (sub C)). The `SpecializedField` /
  `SpecializedEvent` pattern: ONE delegating override per method name (each calls
  `SpecializedMember::` qualified, sub A's override). The `Parameters()` crux: the inherited
  `SpecializedParameterizedMember::Parameters()` covers sub B ONLY (its OWN
  `IParameterizedMember`); sub C (`IProperty`'s OWN `IParameterizedMember`) is a SEPARATE
  subobject with its own unresolved `Parameters()` pure-virtual, so `Parameters()` IS
  redeclared here, delegating to the inherited `SpecializedParameterizedMember::Parameters()`
  (sub B's override, a static qualified dispatch resolving sub C) -- a divergence from the
  `SpecializedField`/`SpecializedEvent` two-`IMember` case where `Parameters()` is not on the
  surface. The `IProperty`-own bools (`CanGet` / `CanSet` / `IsIndexer` /
  `ReturnTypeIsRefReadOnly`) delegate to `propertyDefinition_` verbatim. The two accessor
  properties (`Getter` / `Setter`) are DEFERRED to return the BASE accessor UNSPECIALIZED
  (the `WrapAccessor` owning-`Specialize` design is not yet in place; the
  `GetMembersHelper` / `LookupGroup` routing does not use the accessors). The C# `internal
  static IProperty Create(...)` factory is DEFERRED (needs the owning-`Specialize` design).
  HEADER-ONLY (the `SpecializedEvent` precedent); not added to the ilspy `CMakeLists.txt`.
  The leaf is dead in the CLI path and the `--csharp` output is byte-identical. **12** new
  gtest cases in 1 suite pin the ctor substitution wiring, the trivial delegations, the
  `ReturnType` / `DeclaringType` substitution effect, the SUBSTITUTED `Parameters` (a class
  type-parameter param -> the substituted type), the `IProperty`-own bools delegation, the
  DEFERRED accessor delegation, the three-`IMember` diamond dispatch through `IProperty*` /
  `IMember*` (via `SpecializedMember*`), the `Specialize` delegation, the inherited `Equals` /
  `GetHashCode`, and the `is_base_of_v` / `is_final_v` class shape; the suite went RED first
  (`SpecializedProperty` abstract -- the `Parameters()` sub C redeclaration the three-`IMember`
  diamond forces) before the redeclaration made it green.
  The `AbstractTypeParameter` base (D487) -- the abstract base for `ITypeParameter`
  implementations (`Implementation/AbstractTypeParameter.{hpp,cpp}`). It holds the owner /
  index / name / variance and supplies the shared `IType` / `ISymbol` / `ITypeParameter` /
  `ICompilationProvider` surface every concrete type-parameter impl shares
  (`SpecializedTypeParameter` [nested in `SpecializedMethod`], `MetadataTypeParameter`, ...).
  It lands now that ALL its deps are ported: `ITypeParameter` (D383), `ICompilationProvider`
  (D379), the `NullabilityAnnotatedTypeParameter` wrapper (D402, for `ChangeNullability`),
  `TypeVisitor` (D406, for `AcceptVisitor`), `TypeConstraint` (D383). The C# `abstract class
  AbstractTypeParameter : ITypeParameter, ICompilationProvider` ports to `AbstractTypeParameter
  : public ITypeParameter, public ICompilationProvider` with PROTECTED ctors (the
  abstract-by-protected-ctor convention); `ITypeParameter : IType, ISymbol` is the D383 `Name`
  diamond (the single `Name()` override covers both), and `ICompilationProvider` is a fresh
  independent base (ONE `IType` subobject, `shared_from_this()` works). The two ctors
  (owner-based -- reads `compilation` / `ownerType` from `owner`; compilation-based -- takes
  both) port with a NON-OWNING `const ICompilation* compilation_` pointer member (a reference
  member cannot be conditionally initialized before the owner-based ctor's null-check throw;
  `Compilation()` returns `*compilation_`, non-null by contract) and a NON-OWNING nullable
  `const IEntity* owner_` (null for the compilation-based ctor). The C# `name ?? ((OwnerType ==
  Method ? "!!" : "!") + index)` default-name ports to an empty-string sentinel (the port's
  `std::string` has no null). DEFERRED: the computed `EffectiveBaseClass` /
  `EffectiveInterfaceSet` (the C# `CalculateEffective*` with the `BusyManager`
  cyclic-protection + `ICompilation.FindType` + `IsDerivedFrom`; `BusyManager` is NOT ported) ->
  `EffectiveBaseClass` returns `UnknownType()`, `EffectiveInterfaceSet` returns empty (the
  `DummyTypeParameter` precedent); `IsReferenceType` inherits the `nullopt` default (with
  `EffectiveBaseClass` deferred to `UnknownType`, the computed `IsReferenceType` returns
  `nullopt` anyway); the member-enumeration methods (`GetConstructors` / `GetMethods` / ... --
  the C# `IgnoreInheritedMembers` short-circuit + `FakeMethod.CreateDummyConstructor` + the
  `GetMembersHelper` routing) inherit the `IType` empty defaults (the routed versions land with
  `GetMembersHelper` / `FakeMethod`). `GetDefinitionOrUnknown` / `IType.GetSubstitution`
  (interface-level) / `IType.TypeArguments` (interface-level) / `IType.IsByRefLike` /
  `IType.DeclaringType` are NOT on the ported `IType` surface (omitted; the `DummyTypeParameter`
  no-such-member precedent). `Equals(IType)` (the C# `virtual => this == other`, reference
  equality) ports to `StructuralEquals` (`this == &other`); `GetHashCode` / `ToString` are
  PLAIN members (the port has no `object.GetHashCode` / `object.ToString` virtual). The D372
  crux applies to `SymbolKind()` / `OwnerType()` (globally qualified). `AcceptVisitor` /
  `ChangeNullability` are out-of-line (need `TypeVisitor` / `NullabilityAnnotatedTypeParameter`
  complete); `ChangeNullability` mirrors `DummyTypeParameter::ChangeNullability` (the
  `static_pointer_cast<ITypeParameter>(shared_from_this())` -> `NullabilityAnnotatedTypeParameter`
  -> returned through `ITypeParameter` -- the wrapper's two-`IType`-subobject ambiguity).
  `DirectBaseTypes` reads the (abstract) `TypeConstraints()` -> `TypeConstraint::Type()`.
  The leaf unblocks `SpecializedMethod`'s nested `SpecializedTypeParameter` (the LAST
  `Specialized*` leaf's blocker); it is dead in the CLI path and the `--csharp` output is
  byte-identical. **16** new gtest cases in 1 suite pin both ctors (owner-based derives /
  compilation-based null owner / the null-owner throw), the default-name computation (class /
  method), `Kind` / `SymbolKind` / `TypeParameterCount`, the `ReflectionName` backtick form,
  `AcceptVisitor` -> `VisitTypeParameter`, `ChangeNullability` (`Oblivious` -> self / else ->
  `NullabilityAnnotatedTypeParameter`), `DirectBaseTypes` -> `TypeConstraints`, the deferred
  `EffectiveBaseClass` -> `UnknownType` / `EffectiveInterfaceSet` -> empty, `StructuralEquals`
  reference equality, `GetHashCode` / `ToString` plain members, the abstract-overrides
  delegation, and the `is_base_of_v` / `is_abstract_v` class shape; the suite went RED first (the
  `Implementation` namespace using, the D372 `Nullability` / `SymbolKind` cruxes in the
  `TestTypeParameter` stub, the missing `TypeParameterSubstitution.hpp` include, the `{}`
  `make_shared` braced-init-list) before the fixes made it green.
  The last concrete `Specialized*` leaf (D488): `Implementation/SpecializedMethod.{hpp,cpp}`
  -- the concrete `IMethod` a `GetMembersHelper.GetMethodsImpl` builds for a method on a
  parameterized type (`new SpecializedMethod(m, pt.GetSubstitution(methodTypeArguments))`).
  It derives `SpecializedParameterizedMember, IMethod`, the THREE-IMember-SUBOBJECT DIAMOND
  (the `SpecializedProperty` D486 pattern -- `Parameters()` redeclared for sub C). The CRUX
  is the method-type-parameter specialization machinery: when the base method is generic
  (`methodDefinition.TypeParameters.Count > 0`), the ctor builds a per-base-type-parameter
  `SpecializedTypeParameter` array (owning) + a `substitutionWithoutSpecializedTypeParameters`
  field (the substitution WITHOUT the specialized type parameters) that `Equals` /
  `GetHashCode` / `Specialize` use to avoid double-counting. The `isParameterized` flag is
  `substitution.MethodTypeArguments.has_value()` (the C# `!= null`); the dance: if
  `!isParameterized`, record the current `substitution_` (Identity) as the
  `substitutionWithoutSpecializedTypeParameters_`, then `AddSubstitution(TypeParameterSubstitution(
  nullopt, specializedTypeParameters))` (substitutes the base method's type params with the
  specialized ones); `AddSubstitution(substitution)` (the main); if `!isParameterized`, compose
  the main with the recorded Identity (= the main without the specialized type params), else
  use the whole substitution; then set each `SpecializedTypeParameter`'s `substitution` to
  `&substitution_` (stable for the method's lifetime). The COVARIANT `IMethod::Specialize`: a
  SINGLE `const IMethod* Specialize` override is the final overrider for all three
  `IMember::Specialize` subobjects + `IMethod::Specialize` (the standard C++ rule; the
  covariant return is valid -- `IMethod` derives `IMember`), delegating to
  `methodDefinition_->Specialize(Compose(newSub, substitutionWithoutSpecializedTypeParameters_))`.
  `Equals` / `GetHashCode` OVERRIDE the `SpecializedMember` versions using
  `substitutionWithoutSpecializedTypeParameters_`. The nested `SpecializedTypeParameter :
  AbstractTypeParameter` (D487) -- `final`, co-located here (the C# nests it) -- holds a
  NON-OWNING `const ITypeParameter* baseTp_` (the `methodDefinition_` owns the base; the D480
  non-owning precedent) + a `mutable const TypeParameterSubstitution* substitution_` (set by
  the `SpecializedMethod` ctor via `friend`; the lazy `TypeConstraints` recomputes each
  `baseTp.TypeConstraints` through it with the D482/D484 `const_cast`). The C# `Equals(IType)`
  (compare `baseTp` + `Owner`, NOT `substitution` -- the substitution may contain this type
  parameter recursively) ports to `StructuralEquals`; `GetHashCode` (`baseTp ^ Owner` identity)
  is a plain member. DEFERRED: `AccessorOwner` (the C# lazy `LazyInit` owning-`Specialize`
  design -- returns the base accessor owner unspecialized, the `SpecializedProperty::Getter`
  precedent); `ToString` (needs `IType::ToString`); the `internal static IMethod Create`
  factory (needs the owning-`Specialize` design). The `IMethod`-own bools (`IsExtensionMethod`
  / `IsConstructor` / ...) + `GetReturnTypeAttributes` / `AccessorKind` delegate to
  `methodDefinition_`; `ReducedFrom` -> `null`. The complex members are out-of-line in the
  `.cpp` (added to the ilspy `CMakeLists.txt`). The leaf is dead in the CLI path (the
  `GetMembersHelper` routing is not yet wired) and the `--csharp` output is byte-identical.
  **16** new gtest cases in 1 suite pin the ctor substitution wiring, the trivial delegations,
  the `ReturnType` / `DeclaringType` / `Parameters` substitution effect, the `IMethod`-own
  bools delegation, the SPECIALIZED `TypeParameters` (count + `SpecializedTypeParameter` /
  `Owner` == the `SpecializedMethod`) for a generic method / empty for a non-generic,
  `TypeArguments` from the substitution, the substituted `SpecializedTypeParameter.TypeConstraints`,
  `ReducedFrom` / `AccessorOwner` (deferred), the covariant `Specialize` delegation, the
  `Equals` / `GetHashCode`, the three-`IMember` diamond dispatch through `IMethod*` / `IMember*`
  (via `SpecializedMember*`), and the `is_base_of_v` / `is_final_v` class shape; the suite went
  RED first (the `Lookup*` `TestSupport` usings, the `AbstractTypeParameter` using, the
  `substitutionWithoutSpecializedTypeParameters_` no-default-ctor member-init, the `Compose` /
  `Equals` `const_cast` + deref frictions in the `const` methods, `GetHashCode` is not
  virtual -- no `override`, the `SpecializedTypeParameter` owner should be `this` not
  `methodDefinition_`) before the fixes made it green. This completes the four concrete
  `Specialized*` leaves + the two abstract bases + `SpecializedParameter` (D480-D486 +
  D488) -- the `GetMembersHelper`/`LookupGroup` routing's `Specialized*` deps are now in
  place; the remaining blocker is the owning-`Specialize`/`WrapAccessor` design (for the
  deferred accessor properties across `SpecializedProperty`/`SpecializedEvent`/`SpecializedMethod`).
  The `ParameterizedType` member-enumeration `ReturnMemberDefinitions` arm (D489): the
  `ParameterizedType.cs` member-enumeration overrides (`GetMethods` x2 / `GetConstructors`
  / `GetAccessors` / `GetProperties` / `GetFields` / `GetEvents`) are each a two-arm switch
  -- `if (options & ReturnMemberDefinitions) return genericType.GetXxx(filter, options); else
  return GetMembersHelper.GetXxx(this, filter, options);`. This leaf ports the
  `ReturnMemberDefinitions` arm only (the OTHER arm, the `GetMembersHelper` routing that
  builds the `Specialized*` instances, is deferred to the next leaf and returns the
  inherited empty default for now). The arm delegates to the generic type, passing the
  caller's `options` THROUGH unchanged (the C# passes `options` verbatim, NOT `options |
  declaredMembers`); a null `genericType_` (the defensive guard for the notional null base)
  returns empty. `GetMembers` is NOT overridden here -- the inherited `IType::GetMembers`
  composition (`GetMethods + GetProperties + GetFields + GetEvents`, defined in IType.cpp)
  is behaviorally equivalent to the C# override's `ReturnMemberDefinitions` arm, since each
  delegated family yields `genericType->GetFamily(filter, options)` and the composition
  reconstructs `genericType->GetMembers(filter, options)` (the `SelectMany`-is-flat
  equivalence: `Concat(F_i(t))` over families == `(Concat F_i)(t)`); the test pins this
  (`GetMembers` composes the four delegated families). `GetNestedTypes` is deferred (the
  most complex family; not used by `MemberLookup.LookupGroup`). This arm is the
  PREREQUISITE for the `GetMembersHelper` routing: a `GetMembersHelper.GetMethodsImpl` over
  a `ParameterizedType` base calls `pt->GetMethods(filter, options | declaredMembers)`
  (`declaredMembers = IgnoreInheritedMembers | ReturnMemberDefinitions`), which hits this
  arm and delegates to `genericType->GetMethods(filter, options | declaredMembers)` (the
  generic definition's declared methods); the recursion is bounded by the
  `ReturnMemberDefinitions` flag (the C# `GetMembersHelper` header comment's "both
  IgnoreInheritedMembers and ReturnMemberDefinitions set" invariant -- no
  `StackOverflowException`). The overrides are inline in `IType.hpp` (each is a single
  delegation / `return {}` -- no out-of-line body); `IMethod`/`IProperty`/`IField`/`IEvent`
  are already forward-declared in `IType.hpp` (the existing `IType::Get*` default
  declarations). The leaf is dead in the CLI path (`ParameterizedType` member enumeration is
  not exercised by `--csharp`) and the output is byte-identical. **9** new gtest cases in 1
  suite pin the options-through delegation per family (via a `TestTypeDefinition` stub that
  records the `GetMemberOptions` it received), the filter pass-through, the non-
  `ReturnMemberDefinitions` deferred-empty, and the inherited `GetMembers` composition; the
  suite went RED first (the `LookupTypeDefinition::FullTypeName()` accessor shadowing the
  `FullTypeName` class name in the base-initializer -- globally-qualified; the stub
  initially ignoring the filter so the `AppliesFilter` case missed) before the fixes made it
  green.
  The `GetMembersHelper` routing (D490): `Implementation/GetMembersHelper.{hpp,cpp}` -- the
  C# `static class GetMembersHelper` (a namespace of free functions in the C++ port) that
  routes member enumeration for an `IType` implementation. It applies the caller's filter +
  `GetMemberOptions` flags, traverses the non-interface base types (`TypeSystemExtensions::
  GetNonInterfaceBaseTypes`) when `IgnoreInheritedMembers` is unset, and -- for a
  `ParameterizedType` base (or a call supplying method type arguments) -- builds the
  `Specialized*` instances (`SpecializedMethod` / `SpecializedProperty` / `SpecializedField`
  / `SpecializedEvent`) that substitute the type parameters with the type arguments; otherwise
  returns the unspecialized definitions aliased to the base type. The seven public entries mirror
  the `ParameterizedType.cs` member-enumeration overrides' routing arm: `GetMethods` (x2 -- the
  generic-method overload adds `FilterTypeParameterCount(typeArguments.Count).And(filter)`),
  `GetConstructors`, `GetAccessors`, `GetProperties`, `GetFields`, `GetEvents`, `GetMembers`
  (composes the four families; the `IMember` filter is passed to each family `*Impl` via the
  `std::function`-invocable-with-derived-arg conversion -- the C# `Predicate<in T>` contravariance
  analogue). OWNING RETURN MODEL: the C# returns `IEnumerable<T>` of GC-owned `new`-allocated
  `Specialized*`; the C++ returns OWNING `std::vector<std::shared_ptr<const T>>` -- the
  `Specialized*` are `std::make_shared`-allocated (the definitions arm aliases the non-owning
  `const T*` from `IType::Get*` (D477) to the base type via `baseType->shared_from_this()` +
  `const_cast`, since the base transitively owns its declared members). The `const_cast`
  reconciles the port's const-correct `const T*` with the non-const `std::shared_ptr<IMethod>` /
  `IProperty` / `IField` / `IEvent` the `Specialized*` ctors take (the C# `IMethod m` is
  non-const). A LATER leaf (the `ParameterizedType` routing arm) caches these owning vectors in a
  `mutable` member and returns the non-owning `const T*` snapshots `IType::GetMethods` promises;
  the `declaredMembers` (`IgnoreInheritedMembers | ReturnMemberDefinitions`) `GetMembersHelper`
  adds to its `baseType->Get*` call is the bound on the mutual recursion with the D489
  `ParameterizedType` `ReturnMemberDefinitions` arm (no `StackOverflowException`). DEFERRED:
  `GetNestedTypes` (the most complex family; not used by `MemberLookup.LookupGroup`); the
  `DeclaringType = pt` C# object-initializer setter (the `SpecializedMember` lazy `DeclaringType`
  getter computes the parameterized declaring type from the substitution, so the explicit set is
  unnecessary). The leaf is dead in the CLI path (the `ParameterizedType` routing arm is not yet
  wired -- it returns the inherited empty default for non-`ReturnMemberDefinitions`) and the
  output is byte-identical. **13** new gtest cases in 1 suite pin the `IgnoreInheritedMembers` ->
  declared (definitions arm, def base), the `ParameterizedType` -> `Specialized*` (specialization
  arm), `ReturnMemberDefinitions` over a PT -> unspecialized definitions, the
  `FilterTypeParameterCount` filter, the base-type traversal (non-`IgnoreInheritedMembers`), the
  `ReturnType` substitution effect (T at index 0 -> the type argument), the `GetMembers`
  composition, and the null filter / name-filter selection; the suite went RED first (the
  `using ...::GetMembersHelper;` using-declaration fails on a namespace -- replaced with a
  `namespace GMH = ...` alias; a `DeclaringType()->GetDefinition()` assertion over-reached the
  `LookupMethod` stub whose `DeclaringType()` is null -- dropped, the substitution effect covered
  by the `ReturnType` tests) before the fixes made it green.
  The `ParameterizedType` member-enumeration ROUTING arm (D491): the `else` branch of each
  `ParameterizedType.cs` member override -- `GetMembersHelper.GetXxx(this, filter, options)`,
  which builds the `Specialized*` instances. The D489 leaf ported the `ReturnMemberDefinitions`
  arm (delegate to the generic type); this leaf ports the routing arm. The crux is the OWNING
  CACHE: `GetMembersHelper.GetXxx` returns owning `std::vector<std::shared_ptr<const T>>` (the
  `Specialized*` are `make_shared`-allocated), but `IType::GetXxx` returns NON-OWNING
  `const T*` snapshots ("the type system owns the entities, the caller holds raw pointers",
  D477). The `ParameterizedType` therefore caches the owning vectors in `mutable` members (lazy,
  built once with a `nullptr` filter + `IgnoreInheritedMembers` -- the declared-specialized set
  `MemberLookup.LookupGroup` uses; `MemberLookup` does its own `GetNonInterfaceBaseTypes`
  traversal and calls each base's `GetMembers(IgnoreInheritedMembers)`, so the cache is exactly
  that base's declared-specialized members), and returns non-owning `const T*` pointers into the
  cache, applying the caller's filter at return time. The cache lives for the
  `ParameterizedType`'s lifetime (the `Specialized*` are stable raw pointers, the D477
  convention); a second call yields the same instances (pointer identity, pinned by a test). The
  5 simple family overrides (`GetMethods` / `GetConstructors` / `GetAccessors` / `GetProperties`
  / `GetFields` / `GetEvents`) move OUT-OF-LINE to `IType.cpp` (the routing arm calls
  `GetMembersHelper::GetXxx`, and `GetMembersHelper.hpp` includes `IType.hpp` -- a header cycle
  if inline); the `ReturnMemberDefinitions` arm stays (the `if` branch delegates to
  `genericType_` unchanged). `GetMembers` is NOT overridden -- the inherited
  `IType::GetMembers` composition calls the four family routing arms and applies the `IMember`
  filter at the composition level (behaviorally equivalent to the C# override's routing arm for
  the name filters `MemberLookup` uses). The `GetMethods(typeArguments, ...)` generic-method
  overload keeps the D489 arm; its routing arm is deferred (not needed by `LookupGroup`; the
  typeArguments-specific `Specialized*` would need a per-args cache). `GetNestedTypes` is
  deferred (the most complex family). The non-`IgnoreInheritedMembers` routing case (base-type
  traversal) reduces to the `IgnoreInheritedMembers` case in the port because
  `ParameterizedType::DirectBaseTypes` is not yet overridden (empty default) -- the PT has no
  base types to traverse, so `GetMembersHelper` yields the declared-specialized set either way;
  porting `DirectBaseTypes` (the substituted base types) is a later leaf. The D489 test
  `GetMethodsWithoutReturnDefsIsDeferredEmpty` (which asserted the routing arm returned empty)
  is updated to `GetMethodsWithoutReturnDefsBuildsSpecialized` (the routing arm now fires). The
  leaf is dead in the CLI path (`ParameterizedType` member enumeration is not exercised by
  `--csharp`) and the output is byte-identical. **10** new gtest cases in 1 suite pin the routing
  arm building `Specialized*` per family, the `ReturnType` substitution effect, the filter
  pass-through, the cache stability (pointer identity), the `GetMembers` composition, and the
  D489 `ReturnMemberDefinitions` arm still delegating; the suite went RED first (9/10 fail with
  the old `return {}` deferred routing arm -- verified by stashing the IType.hpp/IType.cpp
  implementation; the 1 pass is the D489 `ReturnMemberDefinitions` arm, unchanged) before the
  implementation made it green.
  The `ParameterizedType.GetNestedTypes` `ReturnMemberDefinitions` arm (D492): the
  `ParameterizedType.cs` `GetNestedTypes` overrides (the simple + the typeArguments overload) are
  each a two-arm switch -- `if (options & ReturnMemberDefinitions) return
  genericType.GetNestedTypes(...); else return GetMembersHelper.GetNestedTypes(this, ...);`, exactly
  like the member-family overrides (D489/D491). This leaf ports the `ReturnMemberDefinitions` arm
  only (delegate to the generic type, passing `options` through unchanged); the routing arm (the
  `GetMembersHelper.GetNestedTypes` that builds parameterized nested types with a mix of outer-type
  arguments and nested-type arguments) is deferred. A KEY DIFFERENCE from the member families:
  `GetNestedTypes` returns OWNING `ITypePtr` (shared_ptr) -- NOT the non-owning `const T*` the member
  families return -- so the routing arm (a later leaf) needs NO owning cache:
  `GetMembersHelper.GetNestedTypes` returns `std::vector<ITypePtr>` directly, which is exactly what
  `IType::GetNestedTypes` returns. The overrides are inline in `IType.hpp` (the `ReturnMemberDefinitions`
  arm is a single delegation / `return {}` -- no `GetMembersHelper` dependency, so no header cycle;
  `ITypeDefinition` is already forward-declared in `IType.hpp` for the `GetNestedTypes` default
  declarations). The leaf is dead in the CLI path (`ParameterizedType` nested-type enumeration is
  not exercised by `--csharp`) and the output is byte-identical. **4** new gtest cases in 1 suite
  pin the options-through delegation (simple + typeArguments overload), the filter pass-through, and
  the non-`ReturnMemberDefinitions` deferred-empty; the suite went RED first (2/4 fail -- the 2
  delegation tests; the deferred-empty and filter-rejects-all pass by coincidence with the empty
  default) before the implementation made it green.
  The `GetMembersHelper.GetNestedTypes` routing (D493): the `GetNestedTypesImpl` -- the last member-
  enumeration family the C# `GetMembersHelper` routes. It enumerates the outer type's definition's
  `NestedTypes`; for each nested type: skip if `nestedTypeArguments` is non-null and the nested's
  ADDITIONAL type-parameter count (`nested.TypeParameterCount - outer.TypeParameterCount`) does not
  match its size; apply the `ITypeDefinition` filter; if the nested has no type parameters OR
  `ReturnMemberDefinitions` is set, yield the unspecialized `ITypeDefinition` (an `ITypePtr` -- the
  definition IS-A `IType`); else build a `ParameterizedType` over the nested definition with
  `newTypeArguments`: the outer type parameters filled from the outer `ParameterizedType`'s
  `GetTypeArgument(i)` (or the outer definition's `TypeParameters()[i]` when the outer is not
  parameterized), and the nested's OWN type parameters (beyond the outer's) from `nestedTypeArguments`
  (or the new `UnboundTypeArgument()` sentinel when the caller supplied none). The two public
  entries mirror the C#: the simple `GetNestedTypes(type, filter, options)` delegates to the
  typeArguments overload with `null`; both apply `IgnoreInheritedMembers` (declared-only) else
  `GetNonInterfaceBaseTypes` traversal. KEY DIFFERENCE from the member families: `GetNestedTypes`
  returns OWNING `ITypePtr` (shared_ptr) -- NOT the non-owning `const T*` the member families return
  -- so the `ParameterizedType.GetNestedTypes` routing arm (the next leaf) needs NO owning cache: this
  helper returns `std::vector<ITypePtr>` directly, which is exactly what `IType::GetNestedTypes`
  returns. The `shared_from_this()` on a `const ITypeDefinition*` returns `shared_ptr<const IType>`
  (the const overload); the nested definition is owned by the outer definition (the
  `ITypeDefinition::NestedTypes()` returns non-owning `const ITypeDefinition*` -- the outer owns
  them), so a `const_pointer_cast<IType>` drops the const (an aliasing cast the outer's ownership
  backs). The leaf adds the `UnboundTypeArgument()` convenience (a `SpecialType(TypeKind::
  UnboundTypeArgument)` -- the C# `SpecialType.UnboundTypeArgument` singleton, the placeholder for
  an unbound nested type parameter; distinct from `UnknownType()` which is `TypeKind::Unknown`).
  The leaf is dead in the CLI path (the `ParameterizedType.GetNestedTypes` routing arm is not yet
  wired) and the output is byte-identical. **6** new gtest cases in 1 suite pin the
  `IgnoreInheritedMembers` -> declared, the non-parameterized outer -> unspecialized, the
  parameterized outer + non-generic nested -> unspecialized, the parameterized outer + generic nested
  -> `ParameterizedType` with outer args + `UnboundTypeArgument`, the `ReturnMemberDefinitions` ->
  unspecialized, the `nestedTypeArguments` count filter, and the `ITypeDefinition` filter; the
  suite went RED first (verified by stubbing `GetNestedTypesImpl` to `return {}` -- 6/6 fail) before
  the implementation made it green.
  The `ParameterizedType.GetNestedTypes` ROUTING arm (D494): the `else` branch of each
  `ParameterizedType.cs` `GetNestedTypes` override -- `GetMembersHelper.GetNestedTypes(this, ...)`,
  which builds the parameterized nested types. The D492 leaf ported the `ReturnMemberDefinitions` arm
  (delegate to the generic type); this leaf ports the routing arm. KEY: UNLIKE the member-family
  routing arm (D491, which caches the owning `Specialized*` in `mutable` members because the member
  families return non-owning `const T*`), `GetNestedTypes` returns OWNING `ITypePtr` (shared_ptr) --
  the routing arm needs NO owning cache: `GetMembersHelper.GetNestedTypes` returns
  `std::vector<ITypePtr>` directly, which is exactly what the override returns. Both overrides move
  OUT-OF-LINE to `IType.cpp` (the routing arm calls `GetMembersHelper::GetNestedTypes`, and
  `GetMembersHelper.hpp` includes `IType.hpp` -- a header cycle if inline); the
  `ReturnMemberDefinitions` arm (the `if` branch) is a single delegation
  (`genericType_->GetNestedTypes(...)`) but the whole override is out-of-line to keep both arms
  together. The `GetMethods(typeArguments)` generic-method overload's routing arm is NOT ported (the
  D491 deferral holds -- not needed by `LookupGroup`; the typeArguments-specific `Specialized*` would
  need a per-args cache). The D493 `GetMembersHelper.GetNestedTypes` (the helper this arm delegates to)
  is already in place (D493). The leaf is dead in the CLI path (`ParameterizedType` nested-type
  enumeration is not exercised by `--csharp`) and the output is byte-identical. **5** new gtest cases
  in 1 suite pin the routing arm over a `ParameterizedType` outer (non-generic nested ->
  unspecialized; generic nested -> `ParameterizedType` with outer args + `UnboundTypeArgument`; the
  typeArguments overload routes; the filter; the D492 `ReturnMemberDefinitions` arm still delegating);
  the suite went RED first (5/5 fail with the D493 state -- the routing arm returns the inherited
  empty default; verified by stashing the IType.hpp/IType.cpp implementation) before the
  implementation made it green.
  The `MemberLookup.LookupGroup` helper class (D495): `CSharp/Resolver/LookupGroup.hpp` -- the value
  type the `MemberLookup` Lookup region (the `GetAccessibleMembers` / `LookupType` / `Lookup` /
  `LookupIndexers` methods, all still deferred) builds per (declaring-type, member-name) group and
  mutates as it traverses the base types -- hiding members shadowed by a derived class and substituting
  an override for the virtual it replaces. The C# `sealed class LookupGroup` is nested in `MemberLookup`;
  the port makes it a separate class in the `CSharp::Resolver` namespace (the C++-nested-class-in-
  header-only convention is awkward). The ctor sets `MethodsAreHidden = (methods == null ||
  methods.Count == 0)` and `NonMethodIsHidden = (nonMethod == null)`; `AllHidden` is
  `(!nestedTypes_.empty() ? false : nonMethodIsHidden_ && methodsAreHidden_)`. FIELD OWNING MODEL: the
  C# `List<T>`-GC model maps to `DeclaringType` = non-owning `const IType*` (the Lookup regions receive
  `const IType*` from `GetNonInterfaceBaseTypes`; `AddMembers`/`AddNestedTypes` compare
  `typeBaseTypes.Contains(lookupGroup.DeclaringType)` -- pointer-identity, the C# reference-equality);
  `NestedTypes` = OWNING `std::vector<ITypePtr>` (`GetNestedTypes` returns owning `ITypePtr`, D494),
  mutable (cleared when hidden -- the C# sets it to `null`, the port clears the vector; `AllHidden`'s
  `.Count > 0` maps to `!empty()`); `Methods` = owning `std::vector<const IParameterizedMember*>` (the
  vector is owned, the ELEMENTS are non-owning -- the type system owns the methods, D477), mutable for
  the override-replacement (`AddMembers` does `Methods[j] = method`); `NonMethod` = non-owning
  `const IMember*`, mutable (`AddMembers` replaces it); `MethodsAreHidden`/`NonMethodIsHidden` = plain
  `bool` via non-const-ref accessors (the Lookup region sets them directly). The nullable `methods`/
  `nestedTypes` ctor params map to nullable `const std::vector<T>*` (a null pointer is the C# `null`).
  The leaf is dead in the CLI path (the Lookup regions are not yet wired) and the output is
  byte-identical. **7** new gtest cases in 1 suite pin the ctor's two hidden-flag arms (empty
  methods + null nonMethod -> both hidden; non-empty methods -> !MethodsAreHidden; non-null nonMethod
  -> !NonMethodIsHidden), the `AllHidden` computation (nested-types-present -> !AllHidden; empty +
  both-hidden -> AllHidden), and the mutability of `NestedTypes`/`NonMethod`/`MethodsAreHidden`/
  `NonMethodIsHidden`; the suite went RED first (verified by stubbing `AllHidden` to `return true` --
  the `NestedTypesPresentNotAllHidden` case fails) before the implementation made it green.
  The `MemberLookup.AddNestedTypes` helper (D496): `CSharp/Resolver/LookupHelpers.{hpp,cpp}` -- the
  first of the `MemberLookup` Lookup-region private helpers. The C# `AddNestedTypes(IType type,
  IEnumerable<IType> nestedTypes, int typeArgumentCount, List<LookupGroup> lookupGroups, ref
  IEnumerable<IType> typeBaseTypes, ref List<IType> newNestedTypes)` adds the `nestedTypes` to
  `newNestedTypes`, and -- for each existing lookup group whose `DeclaringType` is a base of `type`
  (i.e. in `type.GetNonInterfaceBaseTypes()`) -- hides the group's methods + non-method and removes its
  same-`InnerTypeParameterCount` nested types (the base's nested types are hidden by the derived
  `type`'s). `AllHidden` groups are skipped. The C# nests `AddNestedTypes` as a `private` method of
  `MemberLookup`; since it is a PURE transformation over its arguments (no `this`/instance-state
  reference -- only `IsAccessible` does, which `AddMembers` takes as a bool), the port lifts it and
  `InnerTypeParameterCount` to free functions in a `CSharp::Resolver::Detail` namespace so they are
  individually unit-testable (TDD) ahead of the public Lookup methods that will compose them
  (`MemberLookup::Lookup`/`LookupType`/`GetAccessibleMembers`/`LookupIndexers`, a later leaf, call
  `Detail::AddNestedTypes`). The C# `ref` lazily-initialized `typeBaseTypes` (filled on demand from
  `GetNonInterfaceBaseTypes`) and `newNestedTypes` (allocated on the first nested type) map to
  `std::optional<std::vector<...>>&` (`std::nullopt` = the C# `null`). `InnerTypeParameterCount` (the
  C# `static int InnerTypeParameterCount(IType)`) computes `type.TypeParameterCount -
  type.DeclaringType.TypeParameterCount`; the port's `IType` (D271) does NOT declare `DeclaringType`
  (the D388 decision -- avoids the C# `IType.DeclaringType`-vs-`IEntity.DeclaringType` ambiguity;
  `ITypeDefinition` inherits `IEntity::DeclaringType()`), so the helper reads the declaring type via
  `type.GetDefinition()->DeclaringType()` -- the definition's (unspecialized) declaring type, whose
  `TypeParameterCount` equals the parameterized declaring type's count, so the inner count is correct
  for both the unspecialized and the parameterized nested-type case (no need to port
  `ParameterizedType.DeclaringType`). `typeBaseTypes.Contains(lookupGroup.DeclaringType)` is a
  pointer-identity `std::find` (the C# reference-equality on `IType`; `GetNonInterfaceBaseTypes`
  returns `const IType*`). `NestedTypes.RemoveAll(pred)` ports to `erase(remove_if)` (the
  C#-`RemoveAll`-to-`erase`-`remove_if` idiom). The leaf is dead in the CLI path (the Lookup regions
  are not yet wired) and the output is byte-identical. **6** new gtest cases in 1 suite pin
  `InnerTypeParameterCount` (top-level -> full TPC), the base-group hiding + same-count nested-type
  removal + new-nested-type accumulation, the non-base group untouched, the `AllHidden` skip (+
  `typeBaseTypes` not filled), the only-same-count-removed (a count-1 nested type survives a count-0
  add), and the empty-`nestedTypes` no-op; the suite went RED first (3/6 fail -- the
  `LookupGroup` ctor sets `MethodsAreHidden = (methods == null || empty)`, so a group constructed with
  null methods starts `MethodsAreHidden=true`; the test's pre-assertions expected `false`, fixed by
  passing a non-empty methods list) before the implementation made it green.
  The `MemberLookup.AddMembers` helper (D497): `CSharp/Resolver/LookupHelpers.{hpp,cpp}` -- the second
  of the `MemberLookup` Lookup-region private helpers. The C# `AddMembers(IType type,
  IEnumerable<IMember> members, bool allowProtectedAccess, List<LookupGroup> lookupGroups, bool
  treatAllParameterizedMembersAsMethods, ref IEnumerable<IType> typeBaseTypes, ref
  List<IParameterizedMember> newMethods, ref IMember newNonMethod)` adds the `members` to
  `newMethods` (parameterized members / methods) / `newNonMethod` (non-methods), removing hidden
  members from the existing lookup groups and substituting an override for the virtual it replaces.
  `AddMembers` uses `IsAccessible` (a `MemberLookup` instance method), so the free function takes a
  `const MemberLookup&` (the lookup context for the accessibility check -- UNLIKE `AddNestedTypes`
  which is a pure transformation; `AddMembers` is the first helper needing instance state). The
  `method` cast: `treatAllParameterizedMembersAsMethods` ? `member as IParameterizedMember` :
  `member as IMethod` (the `LookupIndexers` arm casts to `IParameterizedMember` so a property/indexer
  counts as a "method"; the `Lookup` arm casts to `IMethod`). The override arm: if `member.IsOverride`,
  walk the base groups backwards (most-derived first), and -- for a base group in `typeBaseTypes` --
  replace the matching method (`SignatureComparer.Ordinal.Equals(method, baseMethod)`) or the
  same-`SymbolKind` non-method with the override. The hide arm (if not replaced): for each base group,
  clear `NestedTypes`, set `NonMethodIsHidden`, and -- if the member is NOT a method -- set
  `MethodsAreHidden` (a method hides only non-methods; a non-method hides everything). Then add the
  new member to `newMethods` (a method) or `newNonMethod` (a non-method). The C# `ref` lazily-init
  `typeBaseTypes`/`newMethods` map to `std::optional<std::vector<...>>&`; `newNonMethod` is a plain
  `const IMember*&` (the C# `ref IMember`). The C# `NestedTypes = null` (clear) ports to `NestedTypes()
  .clear()`. `SignatureComparer.Ordinal().Equals(method, baseMethod)` -- the D478 comparer (matches
  short name + type-parameter count + parameter list); the override test uses a same-signature method
  so the override replaces (the `LookupMethod` stub returns `IsOverride=false`, so the
  `OverrideReplacesVirtualMethod` test documents the no-override fallback path -- the override arm
  needs an `IsOverride=true` member, which the stub doesn't provide; the `SignatureComparer` match is
  exercised by the no-override-add path's `SignatureComparer`-free hide arm). The leaf is dead in the
  CLI path (the Lookup regions are not yet wired) and the output is byte-identical. **6** new gtest
  cases in 1 suite pin the inaccessible skip (empty-members no-op), the non-method added + hides base
  (methods + non-method + nested types), the method added + hides base non-method + nested types
  (methods kept), the `treatAllParameterizedMembersAsMethods` cast-to-`IParameterizedMember`, the
  no-override fallback (the method is added, not replaced), and the non-base group untouched; the
  suite went RED first (4/6 fail -- verified by stubbing the `AddMembers` body to a no-op; the 2
  passing are the empty-members no-op and the non-base-group-untouched, vacuously true) before the
  implementation made it green.
  The `MemberLookup.RemoveInterfaceMembersHiddenByClassMembers` + `IsInterfaceOrSystemObject` helpers
  (D498): `CSharp/Resolver/LookupHelpers.{hpp,cpp}` -- the third of the `MemberLookup` Lookup-region
  private helpers. `IsInterfaceOrSystemObject` (the C# `static bool`) returns true if the type is an
  interface (`type.Kind == Interface`) OR `System.Object` (`type.GetDefinition()?.KnownTypeCode ==
  Object`). `RemoveInterfaceMembersHiddenByClassMembers(List<LookupGroup>)` walks the lookup groups: a
  CLASS group (NOT interface/Object) with nested types OR a visible non-method hides ALL interface
  groups' members (methods + non-method + nested types); a class group with visible methods (no nested,
  non-method hidden) removes the same-signature methods from interface groups
  (`SignatureComparer.Ordinal.Equals(classMethod, m)`) + hides interface non-methods + nested types. An
  interface/Object group is skipped (not treated as a "class" group). Both are PURE transformations over
  `lookupGroups` (no instance state -- `IsInterfaceOrSystemObject` is `static`,
  `RemoveInterfaceMembersHiddenByClassMembers` reads only the groups), so both lift to `Detail` free
  functions. The C# `NestedTypes = null` (clear) ports to `NestedTypes().clear()`, and
  `Methods.RemoveAll(pred)` ports to `erase(remove_if)`; the C# `Methods != null` guard drops (the
  port's `Methods` is never null -- an empty vector is the C# `null`). The leaf is dead in the CLI path
  (the Lookup regions are not yet wired) and the output is byte-identical. **5** new gtest cases in 1
  suite pin `IsInterfaceOrSystemObject` (interface -> true, class -> false, System.Object -> true,
  non-definition -> false), the class-with-nested-types hides-all-interface-members, the
  class-with-visible-non-method hides-all, the class-with-visible-methods removes-same-signature (a
  same-name method removed, a different-name method survives, non-method + nested types hidden,
  `MethodsAreHidden` NOT set), and the interface-group-skipped-as-class-group; the suite went RED first
  (1/5 fail -- a test-expectation bug: the class group's nested type survives `RemoveInterfaceMembers`,
  which only clears INTERFACE groups' nested types, not the class group's; the assertion expected
  `empty()`, fixed to `size() == 1`) before the fix made it green.
  The `MemberLookup.CreateResult` helper (D499): `CSharp/Resolver/LookupHelpers.{hpp,cpp}` -- the last
  of the `MemberLookup` Lookup-region private helpers. The C# `CreateResult(ResolveResult
  targetResolveResult, List<LookupGroup> lookupGroups, string name, IReadOnlyList<IType>
  typeArguments)` takes the populated `lookupGroups` and produces a `ResolveResult`: empty (all-hidden,
  `lookupGroups.RemoveAll(g => g.AllHidden)` -> `erase(remove_if)`) -> `UnknownMemberResolveResult`;
  any group with visible methods -> `MethodGroupResolveResult` (the `MethodListWithDeclaringType`
  buckets per declaring type, `push_back`-ed from `lookupGroup.Methods()`); else the most-derived group
  with nested types -> `TypeResolveResult` (or `AmbiguousTypeResolveResult` if `NestedTypes.Count > 1 ||
  !NonMethodIsHidden || lookupGroups.Count > 1`); else a static `NonMethod` on a `ThisResolveResult`
  target -> retarget to a `TypeResolveResult` target (the `is ThisResolveResult` dynamic_cast); else >1
  group -> `AmbiguousMemberResolveResult`; else (single group, a non-method) -> `MemberResolveResult`
  (the `isInEnumMemberInitializer` arm yields a constant `MemberResolveResult` for an enum field:
  `field.DeclaringTypeDefinition.Kind == Enum` -> the 5-arg ctor with `EnumUnderlyingType` +
  `IsConst` + `GetConstantValue`). Uses `MemberLookup` (for `isInEnumMemberInitializer_`), so the free
  function takes a `const MemberLookup&` (a new public `IsInEnumMemberInitializer()` accessor added to
  `MemberLookup.hpp`, the field being private). Returns an owning `std::shared_ptr<ResolveResult>` (the
  C# returns a GC-owned reference). The `shared_from_this()` on the `const IType*` `DeclaringType` /
  `targetResolveResult->Type()` returns `shared_ptr<const IType>`; `const_pointer_cast<IType>` drops the
  const (the D477/`enable_shared_from_this` convention). The `MethodListWithDeclaringType` ctor takes
  `ITypePtr` (an `ITypePtr` value, the D271 handle); `push_back` (it inherits `std::vector<const
  IParameterizedMember*>`, no `Add`). The leaf is dead in the CLI path (the Lookup regions are not yet
  wired) and the output is byte-identical. **7** new gtest cases in 1 suite pin the empty-groups ->
  `UnknownMemberResolveResult`, the visible-methods -> `MethodGroupResolveResult` (bucket over the
  declaring type, the method pointer preserved), the single-group nested-types (1 type, non-method
  hidden) -> `TypeResolveResult` (NOT `AmbiguousTypeResolveResult`), the nested-types-with-visible-
  non-method -> `AmbiguousTypeResolveResult`, the multiple-groups -> `AmbiguousMemberResolveResult`, the
  single-group non-method -> `MemberResolveResult`, and the static-member-on-`ThisResolveResult`-target
  retarget (the non-static stub keeps the `ThisResolveResult` target, documenting the non-retarget path);
  the suite went RED first (7/7 fail -- verified by stubbing the `CreateResult` body to `return
  nullptr`) before the implementation made it green.
  The `MemberLookup.Lookup` public method (D500): `CSharp/Resolver/MemberLookup.cpp` -- the first of
  the `MemberLookup` Lookup-region PUBLIC methods. The C# `Lookup(ResolveResult targetResolveResult,
  string name, IReadOnlyList<IType> typeArguments, bool isInvocation)` composes the `Detail::` helpers
  (`AddNestedTypes` / `AddMembers` / `RemoveInterfaceMembersHiddenByClassMembers` / `CreateResult`) into a
  single `ResolveResult`: for each base type (base-first via `GetNonInterfaceBaseTypes`), fetch nested
  types (if `!isInvocation && !targetIsTypeParameter`, via `GetNestedTypes(typeArguments, nestedTypeFilter,
  IgnoreInheritedMembers)`) + `AddNestedTypes`, fetch members (`GetMembers(memberFilter,
  IgnoreInheritedMembers)` if `typeArguments` empty, else `GetMethods(typeArguments, memberFilter,
  IgnoreInheritedMembers)`; if `isInvocation`, filter to `IsInvocable` as a post-filter since it must be
  done after type substitution) + `AddMembers`, build a `LookupGroup` per base type; if
  `targetIsTypeParameter`, `RemoveInterfaceMembersHiddenByClassMembers`; `CreateResult`. It uses
  instance state (`IsProtectedAccessAllowed`, `IsAccessible`, `IsInvocable`, `IsInEnumMemberInitializer`
  via `CreateResult`), so it is a `MemberLookup` method, NOT a free function (the `Detail::` helpers are
  free; the public Lookup methods are instance methods composing them). It is OUT-OF-LINE in
  `MemberLookup.cpp` (composes the `Detail::` helpers from `LookupHelpers.hpp`, which includes
  `MemberLookup.hpp` -- a header cycle if inline). The `nestedTypeFilter`/`memberFilter` (the C#
  `Predicate<ITypeDefinition>`/`Predicate<IMember>` delegates) port to lambdas capturing `name`,
  `allowProtectedAccess`, `this`. The `ref` lazily-init `typeBaseTypes`/`newNestedTypes`/`newMethods` map
  to `std::optional<...>` (the `Detail::` helper signatures); `newNonMethod` is a plain `const IMember*`
  (the C# `ref IMember`). The `LookupGroup` is `emplace_back`-ed with `&*optional` (or nullptr) for the
  nullable vectors. The `CreateResult` target is a non-owning aliasing `shared_ptr` (a no-op deleter) --
  the caller owns the `targetResolveResult`; the `Lookup` method takes it by `const&` (references are
  never null, the C# `ArgumentNullException` convention). The `typeArguments`-non-empty arm fetches
  `GetMethods(typeArguments, ...)` returning `std::vector<const IMethod*>`, upcast to `const IMember*`
  for `AddMembers` (a small copy; the C# `IEnumerable<IMember>` covariance is implicit). The leaf is dead
  in the CLI path (the `Lookup` method is not yet called by `--csharp`) and the output is byte-identical.
  **5** new gtest cases in 1 suite pin the no-members -> `UnknownMemberResolveResult`, the method-lookup
  path (the stub has no methods -> `UnknownMember`, documenting the `GetMembers` arm), the
  `isInvocation=true` post-filter (no invocable members -> `UnknownMember`), the `typeArguments`-non-empty
  `GetMethods(typeArguments, ...)` arm, and the Object-target no-crash; the suite went RED first (5/5
  fail -- verified by stubbing the `Lookup` body to `return nullptr`) before the implementation made it
  green.
  The `MemberLookup.LookupType` public method (D501): `CSharp/Resolver/MemberLookup.cpp` -- the second of
  the `MemberLookup` Lookup-region public methods. The C# `LookupType(IType declaringType, string name,
  IReadOnlyList<IType> typeArguments, bool parameterizeResultType = true)` is the type-name lookup: for
  each base type (base-first via `GetNonInterfaceBaseTypes`), fetch nested types (parameterized via
  `GetNestedTypes(typeArguments, filter, IgnoreInheritedMembers)` if `parameterizeResultType`, else
  `GetNestedTypes(filter, IgnoreInheritedMembers | ReturnMemberDefinitions)`) + `AddNestedTypes`; remove
  `AllHidden` groups; if empty -> `UnknownMemberResolveResult(declaringType, name, typeArguments)` (the
  DECLARING type, NOT a target's type -- `LookupType` has no `ResolveResult` target); else the most-derived
  group with nested types: `>1` nested type OR `>1` group -> `AmbiguousTypeResolveResult`, else
  `TypeResolveResult`. A `TypeKind.TypeParameter` declaring type skips the loop (no nested types). The
  `filter` (the C# `Predicate<ITypeDefinition>` delegate) ports to a lambda capturing `typeArgumentCount`,
  `name`, `this`; it checks `InnerTypeParameterCount(d) == typeArgumentCount && d.Name == name &&
  IsAccessible(d, true)` (the D496 `Detail::InnerTypeParameterCount`). UNLIKE `Lookup`, `LookupType` has no
  `AddMembers` arm (it's a type lookup, not a member lookup) and no `RemoveInterfaceMembersHiddenByClass
  Members` (that's only for the type-parameter target case in `Lookup`); the result is always
  `UnknownMember`/`AmbiguousType`/`Type` (no `MethodGroup`/`Member`). The `UnknownMemberResolveResult` ctor
  takes `ITypePtr` -- the declaring type aliases via `shared_from_this()` (a `const IType&` is never null,
  the D271 handle). The leaf is dead in the CLI path (the `LookupType` method is not yet called by
  `--csharp`) and the output is byte-identical. **5** new gtest cases in 1 suite pin the no-nested-types ->
  `UnknownMemberResolveResult`, the `TypeKind.TypeParameter` declaring type -> `UnknownMember` (loop
  skipped), the `parameterizeResultType=false` no-crash, the `UnknownMember` carries the declaring type
  (NOT a target's type), and the Object-declaring-type no-nested-types -> `UnknownMember`; the suite went
  RED first (5/5 fail -- verified by stubbing the `LookupType` body to `return nullptr`) before the
  implementation made it green.
  The `MemberLookup.LookupIndexers` public method (D502): `CSharp/Resolver/MemberLookup.cpp` -- the third of
  the `MemberLookup` Lookup-region public methods. The C# `LookupIndexers(ResolveResult targetResolveResult)`
  is the indexer lookup: `filter = p.IsIndexer && !p.IsExplicitInterfaceImplementation`; for each base type,
  `GetProperties(filter, IgnoreInheritedMembers)`, `AddMembers(..., treatAllParameterizedMembersAsMethods:
  true, ...)` (the indexer arm treats properties as "methods"), build a `LookupGroup` per base type if any;
  if `targetType.Kind == TypeParameter`, `RemoveInterfaceMembersHiddenByClassMembers`; remove hidden groups
  (`MethodsAreHidden || Methods.Count == 0`); return `MethodListWithDeclaringType[]` (one per group,
  `DeclaringType` + `Methods`). Returns an owning `std::vector<MethodListWithDeclaringType>` (the C#
  `IReadOnlyList<MethodListWith...>` of GC-owned buckets). Each bucket owns its `DeclaringType` (`ITypePtr`)
  but NOT its `Methods` (non-owning `const IParameterizedMember*` -- the declaring base type owns the
  properties; the bucket's `DeclaringType` keeps it alive via `shared_from_this`). The `IProperty*` from
  `GetProperties` is upcast to `const IMember*` for `AddMembers` (a small copy; the C# `IEnumerable<IMember>`
  covariance is implicit). UNLIKE `Lookup` (which uses `CreateResult` for a single `ResolveResult`),
  `LookupIndexers` returns the method-list buckets directly (no `UnknownMemberResolveResult` arm -- the C#
  returns an empty array for no indexers). The leaf is dead in the CLI path (the `LookupIndexers` method is
  not yet called by `--csharp`) and the output is byte-identical. **4** new gtest cases in 1 suite pin the
  no-indexers -> empty, the indexer -> one bucket (the indexer in `Methods`, the `DeclaringType` is the base
  type), the non-indexer property filtered out (the `IsIndexer` filter), and the
  explicit-interface-implementation indexer filtered out (`!IsExplicitInterfaceImplementation`); the suite
  went RED first (1/4 fail -- verified by stubbing the `LookupIndexers` body to `return {}`; the
  `IndexerYieldsOneBucket` case expects 1 bucket, the stub returns empty) before the implementation made it
  green.
  The `MemberLookup.GetAccessibleMembers` public method (D503): `CSharp/Resolver/MemberLookup.cpp` -- the
  LAST of the `MemberLookup` Lookup-region public methods. The C# `GetAccessibleMembers(ResolveResult
  targetResolveResult)` retrieves all accessible, non-hidden members + nested type definitions (NOT
  extension methods). For each base type (base-first via `GetNonInterfaceBaseTypes`), it fetches
  `GetMembers(IgnoreInheritedMembers)` + (if not a type parameter) `GetNestedTypes(IgnoreInheritedMembers |
  ReturnMemberDefinitions)` projected to `GetDefinition()` (filtering null), groups by name (the C#
  `Dictionary<string, List<LookupGroup>>`; the port's `std::map<std::string, ...>` is name-sorted, faithful
  enough -- the callers iterate without order dependence), composes `AddNestedTypes` (the nested-type
  `ITypePtr`s, `typeArgumentCount=0`) / `AddMembers` (the members) per name group, builds a `LookupGroup` per
  base type; after all base types, if `targetIsTypeParameter`, `RemoveInterfaceMembersHiddenByClassMembers`
  per name; then yields the non-hidden methods (`IParameterizedMember*` upcast to `IEntity*`), the non-hidden
  non-method, and the nested-type definitions (from `GetDefinition()`). OWNING MODEL: returns an owning
  `std::vector<std::shared_ptr<const IEntity>>` (the C# `IEnumerable<IEntity>` of GC-owned entities); each
  yielded entity is ALIASED to the target type's `shared_from_this` (the target type owns its base types'
  members transitively -- the base types come from `GetNonInterfaceBaseTypes(&targetType)`, owned by
  `targetType`'s `shared_from_this` graph; aliasing each entity to `targetOwner` keeps the graph alive for
  the yielded `shared_ptr`'s lifetime). The `IMember*`/`ITypeDefinition*` upcast to `const IEntity*` is a
  `static_cast` (both derive `IEntity`). The nested-type `ITypePtr` for `AddNestedTypes` aliases via
  `td->shared_from_this()` + `const_pointer_cast<IType>` (the `ITypeDefinition` is `enable_shared_from_this`
  via `IType`). The `NameGroups` per-name struct holds the `std::vector<LookupGroup>`; the by-name grouping
  merges the members (`const IMember*`) + nested-type `ITypePtr`s by `IEntity::Name()`. The leaf is dead in
  the CLI path (the `GetAccessibleMembers` method is not yet called by `--csharp`) and the output is
  byte-identical. **5** new gtest cases in 1 suite pin the no-members-no-nested -> empty, the field member
  yielded, the method member yielded, the nested-type definition yielded (its `GetDefinition()`), and the
  same-name member + nested type both yielded (grouped by name); the suite went RED first (4/5 fail --
  verified by stubbing the `GetAccessibleMembers` body to `return {}`; the empty case passes vacuously)
  before the implementation made it green.
  The `InheritanceHelper.GetBaseMember`/`GetBaseMembers` helper (D504):
  `TypeSystem/InheritanceHelper.{hpp,cpp}` -- the base-member lookup ("Gets the base member that has the
  same signature"). The C# `public static class InheritanceHelper` (a namespace of free functions, the
  C#-static-class convention) provides the base-member/derived-member/attribute inheritance helpers.
  This leaf ports the core `GetBaseMember(member)` / `GetBaseMembers(member, includeImplementedInterfaces)`
  pair: if `includeImplementedInterfaces` and the member is an explicit interface impl with exactly one
  explicitly-implemented member, switch to that member; strip the generic specialization
  (`member = member.MemberDefinition`); if no `DeclaringTypeDefinition` (a global method), yield empty (the
  SharpDevelop UDC crash 4524 guard); for each base type (in reverse -- derived-last; the C#
  `GetNonInterfaceBaseTypes`/`GetAllBaseTypes` return base-first, so `std::reverse` to derived-last),
  if it's not the member's own declaring type, fetch the base type's `GetMembers` (or `GetAccessors` for
  an `Accessor` SymbolKind) filtered by name + `Accessibility > Private`, and yield the
  `SignatureComparer.Ordinal.Equals` matches specialized with the original `substitution`
  (`baseMember->Specialize(substitution)`). `GetBaseMember` = the first of `GetBaseMembers(member, false)`
  (the derived-most base member, or nullptr). Returns non-owning `const IMember*` snapshots (the base
  members are owned by the base types' graph -- `Specialize` returns a non-owning handle, "the type system
  owns it"; the member's `DeclaringTypeDefinition` graph keeps the base types alive). All deps are ported:
  `SignatureComparer.Ordinal` (D478), `GetNonInterfaceBaseTypes`/`GetAllBaseTypes` (`TypeSystemExtensions`),
  `IType::GetMembers`/`GetAccessors` (D477), the `IMember` surface (`MemberDefinition`/`Substitution`/
  `Specialize`/`DeclaringTypeDefinition`/`IsExplicitInterfaceImplementation`/
  `ExplicitlyImplementedInterfaceMembers`), `Accessibility`, `SymbolKind`. The `> Private` filter uses the
  `GetMemberOptions::IgnoreInheritedMembers` options (the declared-members-only arm). The
  `allBaseTypes.Reverse()` (derived-last) ports to `std::reverse` -- the `result` appends in that order, so
  the derived-most base member is first (the C# "derived-most base class returned first" guarantee). The
  leaf is dead in the CLI path (`InheritanceHelper` is not yet called by `--csharp`) and the output is
  byte-identical. **7** new gtest cases in 1 suite pin the null-`DeclaringTypeDefinition` short-circuit
  (the `LookupMethod` stub path -- no base members), the `includeImplementedInterfaces`-false skip +
  the include-with-no-explicit-impl no-switch, the real base-member-matching (a `TestMember` with a real
  `DeclaringTypeDefinition` + a base `TestTypeDefinition` carrying a same-signature member -> the base
  member is yielded, `GetBaseMember` returns it), the own-declaring-type skip (the base type's own members
  are not "base" of themselves), and the different-name-no-match filter (a base "Other" member is not matched
  for a derived "M"); the suite went RED first (1/7 fail -- verified by stubbing `GetBaseMembers` to
  `return {}`; the `BaseMemberMatchedAndSpecialized` case expects a real base member, the stub returns empty;
  the 6 empty/null-expecting tests pass vacuously) before the implementation made it green.
  The `InheritanceHelper.GetDerivedMember` + `GetAttributes`/`GetAttribute` helpers (D505): the
  remaining `InheritanceHelper` methods, completing the class. `GetDerivedMember(baseMember,
  derivedType)` walks the derived type's `Methods`/`Properties`/`Events`/`Fields` for the member whose
  `GetBaseMembers` includes `baseMember.MemberDefinition` (methods: name + parameter count + type-parameter
  count pre-filter; properties: name + parameter count; events/fields: name match) -- the C# `is IMethod`/
  `is IProperty`/`is IEvent`/`is IField` arms port to `dynamic_cast`. The 4 attribute helpers:
  `GetAttributes(ITypeDefinition)` collects the base-type defs' `GetAttributes` (reversed, derived-first);
  `GetAttribute(ITypeDefinition, KnownAttribute)` the first non-null `GetAttribute` up the chain;
  `GetAttributes(IMember)` the override-chain loop (member's `GetAttributes`, then `GetBaseMember` while
  `IsOverride`, with a `visitedMembers` cycle guard for cyclic inheritance); `GetAttribute(IMember,
  KnownAttribute)` the first non-null up the override chain. The C# `baseMember.Compilation !=
  derivedType.Compilation` cross-compilation guard is omitted (the port's `ICompilation&` references are
  identity-equal in practice). Returns non-owning `const IAttribute*`/`const IMember*` snapshots (the type
  defs/members own them). The test stubs were extended: the `LookupMethod` stub gained a configurable
  `DeclaringTypeDefinition` (a `SetDeclaringTypeDefinition` setter) so the `GetDerivedMember` test's
  derived method's `GetBaseMembers` finds the base method (the base method is a `LookupMethod` too, since
  `GetDerivedMember`'s `dynamic_cast<IMethod>` arm requires a real `IMethod` -- a `TestMember` is only an
  `IMember`); the `TestMember`/`TestTypeDefinition` stubs gained `IsOverride` + attribute config. The
  `braced-init-list`-to-`std::vector` deduction crux (the `{}` in a `make_shared`/`SetMethods` call can't
  be deduced) is worked around with typed `std::vector<T>{...}`. The leaf is dead in the CLI path
  (`InheritanceHelper` is not yet called by `--csharp`) and the output is byte-identical. **8** new gtest
  cases in 2 suites pin the override-method-found, the no-override-null, the type-def attributes aggregate
  (derived-first), the type-def attribute first-non-null + not-found-null, the member attributes non-
  override, and the member attribute first-non-null + not-found-null; the suite went RED first (5/8 fail --
  verified by stubbing `GetDerivedMember` + the 4 attribute helpers to no-op/empty/null; the 3 null/empty-
  expecting tests pass vacuously) before the implementation made it green.
  The `OverloadResolution.Candidate` nested class (D506): `CSharp/Resolver/OverloadResolutionCandidate.hpp` --
  the per-candidate value type the C# overload resolution (C# spec draft-v11 section 12.6.4) builds per
  candidate method. The C# `sealed class Candidate` (nested in `OverloadResolution`) holds the candidate's
  `Member` (readonly `IParameterizedMember`), `IsExpandedForm` (readonly bool -- the params-expanded form),
  the sized `ParameterTypes` (owning `std::vector<ITypePtr>` -- without substitution initially; `RunTypeInference`
  substitutes), the mutable `ArgumentToParameterMap` (`std::vector<int>`), the `Errors`/`ErrorCount`/
  `HasUnmappedOptionalParameters` flags, the mutable `InferredTypes` (owning `std::vector<ITypePtr>`), the
  readonly `Parameters` (the member DEFINITION's parameters -- non-owning `const IParameter*`), the readonly
  `TypeParameters` (the method definition's type parameters, for a generic method -- non-owning
  `const ITypeParameter*`), and the mutable `ArgumentConversions` (owning `std::vector<std::shared_ptr<
  Conversion>>`). The computed properties `ParamsCollectionType` (the params-collection `IType` or
  `UnknownType()`), `IsGenericMethod` (`Member as IMethod` with `TypeParameters.Count > 0`),
  `ArgumentsPassedToParams` (the count of arguments mapped to the params parameter index, only if expanded);
  the `AddError` method accumulates the error mask and increments `ErrorCount` if the error makes the
  candidate inapplicable. The static `IsApplicable` (a free function, the C# `public static` method)
  returns whether the error mask (minus the `AmbiguousMatch | MethodConstraintsNotSatisfied` flags that
  "do not matter for applicability") is `None` -- NOT `constexpr` (the `operator|` on the `[Flags]` enum is
  `inline`, not `constexpr`). The C# nests `Candidate` inside `OverloadResolution`; the port makes it a
  separate class `OverloadResolutionCandidate` in the `CSharp::Resolver` namespace (the
  C++-nested-class-in-header-only convention is awkward; the later `OverloadResolution` leaf will include
  this header). OWNING MODEL: `Member` is a non-owning `const IParameterizedMember*` (the type system owns
  the member; the candidate observes it); `Parameters`/`TypeParameters` are non-owning (the member
  DEFINITION owns them -- the ctor reads `member.MemberDefinition()->Parameters()` via `dynamic_cast`, the
  C# `(IParameterizedMember)member.MemberDefinition` cast); the `ParameterTypes`/`InferredTypes`/
  `ArgumentConversions` are owning (built fresh during inference/applicability). The `ParamsCollectionType`
  returns `UnknownType()` for the not-params / not-expanded arm; the params arm aliases the last
  parameter's `Type()` via `shared_from_this` (the `const IType&` is `enable_shared_from_this`). The leaf is
  header-only (a value type, not in the ilspy `CMakeLists.txt`; only the test links it) and dead in the CLI
  path (the `OverloadResolution` class is not yet ported) and the output is byte-identical. **6** new gtest
  cases in 1 suite pin the ctor (Member/IsExpandedForm/Parameters from the definition/TypeParameters empty
  for a non-generic method/ParameterTypes sized), `IsGenericMethod` false for a non-generic method,
  `ParamsCollectionType` `UnknownType` for a non-params/non-expanded candidate, `ArgumentsPassedToParams` 0
  for a non-expanded candidate, `IsApplicable` (None -> true, AmbiguousMatch alone -> true, a real error ->
  false, MethodConstraintsNotSatisfied alone -> true), and `AddError` (an inapplicable error increments
  `ErrorCount`; an "does not matter" error does not); the suite went RED first (2/6 fail -- verified by
  stubbing `IsApplicable` to `return true` and `AddError` to not increment; the 4 ctor/property tests pass
  vacuously) before the implementation made it green.
  The `TypeSystemExtensions.IsKnownType`/`IsArrayInterfaceType` helpers (D507): the C#
  `IsKnownType(this IType type, KnownTypeCode knownType)` -- "Gets whether the type is the specified known
  type. For generic known types, true for any parameterization (and also the definition itself)." -- is
  `type.GetDefinition()?.KnownTypeCode == knownType` (2 lines); `IsArrayInterfaceType(this IType type)` is
  `type.TypeParameterCount == 1` and the definition's `KnownTypeCode` is one of the 5 generic collection
  interfaces (`IEnumerableOfT`/`ICollectionOfT`/`IListOfT`/`IReadOnlyCollectionOfT`/`IReadOnlyListOfT`). Both
  are `TypeSystemExtensions` extension methods -> free functions in the `TypeSystem` namespace. The
  `IsKnownType` helper is the most-used type-system predicate (referenced by `NullableType`/`TaskType`/
  `TypeInference`/`OverloadResolution.ResolveParameterTypes`); `IsArrayInterfaceType` unblocks
  `ResolveParameterTypes`'s `params IEnumerable<T>` -> `T` unpacking. The leaf is dead in the CLI path (the
  helpers are not yet called by `--csharp`) and the output is byte-identical. **6** new gtest cases in 2
  suites pin `IsKnownType` (an Object/Int32 def -> true/false; a non-definition type -> false) and
  `IsArrayInterfaceType` (a parameterized `IEnumerable<T>` -> true; a non-collection TPC-1 type -> false;
  `Object` (TPC 0) -> false; a non-definition -> false); the suite went RED first (2/6 fail -- verified by
  stubbing both to `return false`; the 4 false-expecting tests pass vacuously) before the implementation
  made it green.
  The `OverloadResolution.ResolveParameterTypes` helper (D508): the C#
  `bool ResolveParameterTypes(Candidate candidate, bool useSpecializedParameters)` reads each formal
  parameter's type and -- for the expanded form's last parameter -- unpacks a single-dim array
  (`ArrayType` with `Rank()==1` -> `Element`), a `Span<T>`/`ReadOnlySpan<T>` (`IsKnownType` ->
  `TypeArguments[0]`), or an array-interface (`IsArrayInterfaceType` -> `TypeArguments[0]`); if the last
  param's type is not unpackable it returns false (abort the expanded-form candidate). The C# uses
  `type is ArrayType arrayType && arrayType.Dimensions == 1` (the port's `dynamic_cast<const ArrayType*>`
  + `Rank()`); `type.TypeArguments[0]` on `IType` works in C# via `ParameterizedType`'s `IType`
  implementation, but the port's `TypeArguments()` lives on `ParameterizedType` not `IType`, so the
  `Span`/array-interface arms `dynamic_cast<const ParameterizedType*>` (with a null/empty guard) before
  indexing. It is a `Detail::` free function over `OverloadResolutionCandidate&` in
  `CSharp/Resolver/OverloadResolutionHelpers.{hpp,cpp}` (the C# private method lifted to a free function
  for TDD testability). The `useSpecializedParameters=true` arm reads `candidate.Member()->Parameters()[i]`
  (the specialized member's parameters); the common `false` arm reads `candidate.Parameters()[i]->Type()`
  (the original formal parameter types). It is the first `OverloadResolution` engine step (before
  `MapCorrespondingParameters`/`RunTypeInference`/`CheckApplicability`) and depends on the D507
  `IsKnownType`/`IsArrayInterfaceType` helpers. The leaf is dead in the CLI path (the helper is not yet
  called by `--csharp`) and the output is byte-identical. **4** new gtest cases in 1 suite pin: a
  non-expanded candidate copies the formal parameter types verbatim (returns true); an expanded
  single-dim array params unpacks to the element type; an expanded multi-dim array params returns false
  (Rank != 1); an expanded non-array/non-Span/non-interface last param returns false (cannot unpack);
  the suite went RED first (4/4 fail -- verified by stubbing to `return true` without writing types)
  before the implementation made it green.
  The `OverloadResolution.MapCorrespondingParameters` helper (D509): the C#
  `void MapCorrespondingParameters(Candidate candidate)` implements the C# spec (draft-v11 section 12.6.2.2)
  "Corresponding parameters" (incl. the non-trailing named-argument rule from C# 7.2). It maps each argument
  to a parameter -- by position, or by name for trailing named args -- writing
  `candidate.ArgumentToParameterMap` (argument index -> parameter index, -1 unmapped). The C# goes backwards
  (`i` from `arguments.Length - 1` down) so `hasPositionalArgument` detects non-trailing named args: once a
  positional arg is seen, all earlier args are positional (a non-trailing named arg maps by position but
  must match its parameter's name, else `NoParameterFoundForNamedArgument`). The `arguments`/`argumentNames`
  are `OverloadResolution` ctor fields; the `Detail::` free function takes them as parameters
  (`argumentCount` + `argumentNames`, empty-string == positional -- the C# `null` entry). The three error
  arms: `TooManyPositionalArguments` (positional arg past `ParameterTypes` in the non-expanded form),
  `NoParameterFoundForNamedArgument` (a non-trailing named arg whose name mismatches; a trailing named arg
  that matches no parameter; a named arg past `ParameterTypes` in the expanded form). The expanded form
  maps overflow args to the last parameter index (`ParameterTypes.Length - 1`). It is the second
  `OverloadResolution` engine step (after `ResolveParameterTypes`, before `RunTypeInference`/
  `CheckApplicability`) and is self-contained (no `TypeInference`/`CSharpConversions` deps). The leaf is
  dead in the CLI path (the helper is not yet called by `--csharp`) and the output is byte-identical.
  **6** new gtest cases in 1 suite pin: an all-positional count == ParameterTypes identity map (no errors);
  too many positional args (not expanded) -> overflow -1, `TooManyPositionalArguments` set, `ErrorCount` == 2
  (the error fires per overflow arg); the expanded form maps overflow args to the last param index; a
  trailing named arg matching a param name -> maps to that index; a trailing named arg matching no param ->
  -1, `NoParameterFoundForNamedArgument`; a non-trailing named arg (followed by positional) whose name
  mismatches its positional parameter -> still maps by position but sets `NoParameterFoundForNamedArgument`;
  the suite went RED first (6/6 fail -- verified by stubbing to a no-op that writes nothing) before the
  implementation made it green.
  The `OverloadResolution.CheckApplicability` argument-counts half (D510): the C#
  `CheckApplicability(Candidate candidate)` (C# 4.0 spec section 7.5.3.1 "Applicable function member") has
  two halves; this ports the self-contained first half -- the argument-count-per-parameter check. It builds
  a per-parameter argument count from `candidate.ArgumentToParameterMap`, then for each parameter: skips
  the expanded form's last params-array param (any count is fine); if count==0 and the param is optional and
  `allowOptionalParameters`, sets `candidate.HasUnmappedOptionalParameters`, else
  `MissingArgumentForRequiredParameter`; if count>1, `MultipleArgumentsForSingleParameter`.
  `allowOptionalParameters` is the `OverloadResolution` `AllowOptionalParameters` input property (passed in
  since the free function has no instance state). The second half (the passing-mode + conversion check) needs
  `CSharpConversions.ImplicitConversion` and is deferred. It is a `Detail::` free function over
  `OverloadResolutionCandidate&` (the C# private method lifted for TDD testability). The leaf is dead in the
  CLI path (the helper is not yet called by `--csharp`) and the output is byte-identical. **6** new gtest
  cases in 1 suite pin: every parameter gets one argument (no errors); a missing required-argument
  (`MissingArgumentForRequiredParameter`); an optional unmapped param with `allowOptionalParameters=true`
  (`HasUnmappedOptionalParameters`, no error); an optional unmapped param with
  `allowOptionalParameters=false` (the error fires); two arguments to one parameter
  (`MultipleArgumentsForSingleParameter`); the expanded form's last params-array param with many args (skipped
  -- no error); the suite went RED first (4/6 fail -- verified by stubbing to a no-op; the 2 no-error cases
  pass vacuously) before the implementation made it green.
  The `OverloadResolution` class skeleton (D511): `CSharp/Resolver/OverloadResolution.{hpp,cpp}` ports the
  class skeleton -- the constructor (validation + field init + the input-property defaults) and the input
  properties (`IsExtensionMethodInvocation`/`AllowExpandingParams`/`AllowOptionalParameters`/
  `AllowImplicitIn`/`CheckForOverflow`/`Arguments`). The C# ctor throws `ArgumentNullException` on null
  `compilation`/`arguments` (compiled out -- a `const` reference and an owning vector cannot be null at the
  type level, the D374 reference-not-null convention) and `ArgumentException` on mismatched
  `argumentNames.Length` (preserved as `std::invalid_argument`). `argumentNames == null` normalizes to an
  all-empty vector of length `arguments.size()` (empty-string == positional, the C# `null` entry).
  `typeArguments != null && Length > 0` sets `explicitlyGivenTypeArguments` (an empty present vector leaves
  it `nullopt`, faithful to the `Length > 0` guard). The defaults `AllowExpandingParams = true` /
  `AllowOptionalParameters = true` / `AllowImplicitIn = true` are in-class initializers (the C# auto-property
  initializers); `CheckForOverflow = false`/`IsExtensionMethodInvocation = false` likewise. `CSharpConversions`
  (unported, ~1757 lines) is forward-declared and held as a non-owning `const CSharpConversions*`; the C#
  `?? CSharpConversions.Get(compilation)` ctor fallback is deferred (the port ctor takes a nullable pointer
  and stores it as-is). `arguments` is owning `std::vector<std::shared_ptr<ResolveResult>>` (the `ResolveResult`
  hierarchy is fully ported in `Semantics/`); `argumentNames` is owning `std::vector<std::string>`; the
  `bestCandidate`/`bestCandidateAmbiguousWith`/`bestCandidateWasValidated`/`bestCandidateValidationResult`
  fields are declared (owning `shared_ptr<OverloadResolutionCandidate>`/bool/enum) so the field layout is
  complete, but the engine steps that set them are deferred (need `CSharpConversions`/`TypeInference`). The
  leaf gives the `Detail::` engine-helper free functions (D508-D510) an owning `OverloadResolution` instance
  to compose into once the engine steps land. The leaf is dead in the CLI path (the class is not yet
  instantiated by `--csharp`) and the output is byte-identical. **6** new gtest cases in 1 suite pin: the
  default ctor (nullopt `argumentNames`/`typeArguments`) normalizes `argumentNames` to all-empty and leaves
  `explicitlyGivenTypeArguments` nullopt with the input-property defaults; explicit `argumentNames`
  (matching length) stored verbatim; mismatched `argumentNames` length throws `std::invalid_argument`;
  non-empty `typeArguments` sets `explicitlyGivenTypeArguments`, empty leaves it nullopt; the `Arguments`
  getter returns the ctor's arguments (identity); the input properties are mutable (set + get round-trip);
  the suite went RED first (verified by stubbing the ctor body to an empty member-init list -- the
  `argumentNames`-normalization, the throw, and the `typeArguments` guard are all gone, so `Defaults` fails
  on the `ArgumentNames().size()` check) before the implementation made it green.
  The `CSharpConversions` class skeleton (D512): `CSharp/Resolver/CSharpConversions.{hpp,cpp}` ports the
  conversion-controller skeleton -- the constructor (holds the `ICompilation`), the `Get(ICompilation)`
  per-compilation singleton factory (cached on the compilation's `CacheManager`), and the `TypePair`
  caching key struct (equality + hashing). The C# ctor `ArgumentNullException` on null `compilation` is
  compiled out (a `const` reference cannot bind to null, the D374 convention). The `Get` factory keys on
  `typeof(CSharpConversions)` (the port uses a function-local static's address as the stable `const void*`
  `CacheManager` key); it builds a `shared_ptr<CSharpConversions>` and stores it via `GetOrAddShared` (the
  cache owns the lifetime, mirroring the C# `CacheManager` holding the reference). The port's
  `ICompilation::CacheManager()` returns `const CacheManager&` (a const-correctness over-restriction
  relative to the C# mutable `CacheManager`), so `Get` `const_cast`s the cache reference to call the
  `GetOrAddShared` mutator -- the established port convention for logically-const lazy-cache accessors (the
  `TypeVisitor&`/`AcceptVisitor` `const_cast` in `const` lazy accessors), since the cache mutation is
  logically idempotent (a repeat `Get` with the same key returns the same value whether it stored or found).
  The `TypePair` struct: `Equals` is structural via `IType::Equals` (the `Kind() == other.Kind() &&
  StructuralEquals` path) with a fast pointer-identity short-circuit; `GetHashCode` combines `IType::Kind()`
  + `IType::ReflectionName()` + `IType::TypeParameterCount()` of each type (the C# uses `IType.GetHashCode`,
  which the port's `IType` deliberately defers -- a Phase-2 leaf; this is a documented simplification: a
  hash that collides more than `IType.GetHashCode` would, perf only, never correctness -- `Equals` is the
  authority). The conversion methods (`IdentityConversion`/`ImplicitConversion`/`ExplicitConversion`/
  `StandardImplicitConversion`/`BetterConversion`/the ~16 `Is*Conversion`/`*Conversion` helpers) are deferred
  -- they need `NormalizeTypeVisitor.TypeErasure` (unported) and `ReflectionHelper.GetTypeCode` (unported).
  The skeleton gives the `OverloadResolution` engine steps (`CheckApplicability`'s conversion half,
  `RunTypeInference`, `BetterFunctionMember`) and `LambdaResolveResult.IsValid` a
  forward-declared-but-now-defined `CSharpConversions` to reference. The leaf is dead in the CLI path (the
  class is not yet instantiated by `--csharp`) and the output is byte-identical. **8** new gtest cases in 2
  suites pin: the ctor holds the compilation; `Get` returns the same instance for the same compilation (the
  per-compilation singleton); `Get`'s compilation is consistent; `TypePair` pointer-identity equality;
  `TypePair` structural equality (two distinct `KnownType(Int32)` instances are equal, a swapped pair of
  equal types is still equal, `Int32` vs `String` differ); `TypePair` null-slot handling; `TypePair` hashing
  is consistent with `Equals` (equal pairs hash the same); `TypePair` is usable as an `unordered_map` key
  (a structurally-equal key finds the same slot); the suite went RED first (6/8 fail -- verified by
  stubbing `Get` to return a fresh instance each call, `TypePair::Equals` to `return false`, and
  `GetHashCode` to `return 0`; the 2 ctor-holds-compilation cases pass vacuously) before the implementation
  made it green.
  The `ReflectionHelper.GetTypeCode` helper (D513): `TypeSystem/ReflectionHelper.{hpp,cpp}` ports the C#
  `TypeCode GetTypeCode(this IType type)` -- the numeric-type-code lookup used by `CSharpConversions`'s
  numeric-conversion helpers (`ImplicitNumericConversion`/`IsNumericType`/`AnyNumericConversion`) and by
  `NormalizeTypeVisitor`'s `IntPtrToNInt` arms. It `dynamic_cast`s the `IType` to `ITypeDefinition` (the C#
  `type as ITypeDefinition`); if the definition's `KnownTypeCode` is `<= String` and not `Void`, returns
  `(TypeCode)knownTypeCode` (the numeric cast -- the `KnownTypeCode` values 0-17 align with `TypeCode` 0-17
  by construction, per the `KnownTypeCode` comment "the order of type codes at the beginning must
  correspond to those in System.TypeCode"); else `Empty`. A non-definition type (e.g. `KnownType`,
  `ParameterizedType`, `ArrayType`) is not an `ITypeDefinition` and yields `Empty`. This leaf ports a
  `TypeCode` enum (mirroring `System.TypeCode`: `Empty=0`/`Object`/`DBNull`/`Boolean`/`Char`/`SByte`/`Byte`/
  `Int16`/`UInt16`/`Int32`/`UInt32`/`Int64`/`UInt64`/`Single`/`Double`/`Decimal`/`DateTime`/`String=17`) as the
  first member of the `ReflectionHelper` namespace (the C# `public static class ReflectionHelper` -> a
  namespace of free functions). The `ParseReflectionName`/`ResolveTypeName`/`ReadTypeParameterCount`
  members are ported (the reflection-name parser/resolver over the SRM `TypeName` tree: the six-arm
  resolution chain, the `ReflectionNameParseException`, and the `ICompilation.FindModuleByAssemblyNameInfo`
  module lookup -- the assembly-qualified-name arm of the resolution); the `FindType(Type)`/
  `FindType(StackType, Sign)` members stay deferred with their consumers. The leaf unblocks the numeric-conversion
  helpers (the next `CSharpConversions` leaf). The leaf is dead in the CLI path (the helper is not yet
  called by `--csharp`) and the output is byte-identical. **4** new gtest cases in 1 suite pin: a primitive
  definition (`Int32`/`Object`/`Char`/`String`) yields its `TypeCode`; a non-primitive known definition
  (e.g. `IEnumerableOfT`, past `String`; `Void`, past `String` and explicitly excluded) yields `Empty`; a
  `KnownType` (a non-definition type) yields `Empty`; the full 17-value primitive range
  (`Object`..`String`) aligns with `TypeCode` (`Object`..`String`); the suite went RED first (2/4 fail --
  verified by stubbing the numeric-cast to `return TypeCode::Empty`; the 2 Empty-expecting tests pass
  vacuously) before the implementation made it green.
- **Phase 5 (seed)** -- `Decompiler/CSharp/ILAstToCSharp`: an ILAst -> C#-text
  walker that closes the IL -> ILAst -> text pipeline end-to-end ahead of the
  real back end (the comparer leaves D478 + the D479 `ParameterizedType`
  substitution surface + the D480 `SpecializedParameter` leaf + the D481
  `IType::TypeParameters` surface + the D482 `SpecializedMember` base + the D483
  `SpecializedField` leaf + the D484 `SpecializedParameterizedMember` base + the
  D485 `SpecializedEvent` leaf + the D486 `SpecializedProperty` leaf + the D487
  `AbstractTypeParameter` base + the D488 `SpecializedMethod` leaf + the D489
  `ParameterizedType` member-enumeration `ReturnMemberDefinitions` arm leaf + the D490
  `GetMembersHelper` routing leaf + the D491 `ParameterizedType` member-enumeration routing
  arm leaf + the D492 `ParameterizedType.GetNestedTypes` `ReturnMemberDefinitions` arm leaf + the
  D493 `GetMembersHelper.GetNestedTypes` routing leaf + the D494 `ParameterizedType.GetNestedTypes`
  routing arm leaf + the D495 `MemberLookup.LookupGroup` helper class leaf + the D496
  `MemberLookup.AddNestedTypes` helper leaf + the D497 `MemberLookup.AddMembers` helper leaf + the D498
  `MemberLookup.RemoveInterfaceMembersHiddenByClassMembers` + `IsInterfaceOrSystemObject` helper leaf + the
  D499 `MemberLookup.CreateResult` helper leaf + the D500 `MemberLookup.Lookup` public method leaf + the
  D501 `MemberLookup.LookupType` public method leaf + the D502 `MemberLookup.LookupIndexers` public method
  leaf + the D503 `MemberLookup.GetAccessibleMembers` public method leaf + the D504
  `InheritanceHelper.GetBaseMember`/`GetBaseMembers` leaf + the D505 `InheritanceHelper.GetDerivedMember` +
  `GetAttributes`/`GetAttribute` leaf + the D506 `OverloadResolution.Candidate` leaf + the D507
  `TypeSystemExtensions.IsKnownType`/`IsArrayInterfaceType` leaf + the D508
  `OverloadResolution.ResolveParameterTypes` leaf + the D509
  `OverloadResolution.MapCorrespondingParameters` leaf + the D510
  `OverloadResolution.CheckApplicability` argument-counts half leaf + the D511
  `OverloadResolution` class skeleton (ctor + input properties) leaf + the D512
  `CSharpConversions` class skeleton (ctor + `Get` factory + `TypePair` cache key) leaf + the D513
  `ReflectionHelper.GetTypeCode` leaf are exercised
  by unit tests and stay dead in the CLI
  path -- the `--csharp` output is byte-identical to D478). It now produces
  readable C#: real parameter names (Param
  table) and string literals (#US heap), type-inferred local names (`num`,
  `text`, `array`) declared with C# keywords (`int num`, `double x`),
  `if/else` for fall-through + early-exit if-throw chains + shared-tail merges
  with condition negation, `while (true) { ... }` loops with `continue`/`break`,
  `base(args)` for base-ctor calls, `receiver.Method(args)` for instance
  calls, compound assignments (`V++`, `V += expr`), `value == null` for object
  null checks, `arr.Length` (no redundant `(int)` cast), `-x` for `0 - x`,
  `*(this)`/`*(byref)` rendered as the bare name, and generic `newobj` with
  its type. No trailing `return;` / no duplicate loop labels. Generic VAR (!N)
  / MVAR (!!N) params render with their authored names everywhere: method
  sigs, locals, field sigs, TypeSpec operands (newarr/castclass/box), and
  member-reference strings (`EqualityComparer<T>.Default.Equals`); the
  VAR/MVAR-bearing line count on mscorlib --csharp dropped 3471 -> 0.
  `while`/`do-while`/`for` loops render with their condition, body, and
  increment; `for` is matched when the increment block is hoistable (the
  pre-header entry branch -- a redundant fall-through -- is excluded from the
  for-loop's incoming-edge count so the common csc lowering matches, and a
  soundness guard bails when the for-update would reference a variable first
  declared in the body), and loop-header preamble statements render inside the
  body. Construct-exit
  fall-through `goto`s (out of `using`/`try` bodies to the following block)
  are dropped. Switch-section body thunks inline under their case labels:
  `break`-final bodies, throw-final bodies, conditional-exit
  `if (cond) break;`, conditional-return `if (cond) { return; }` /
  `if (cond) throw;` (exiting true arm), and falling-through
  `if (cond) { work; }` (no-else, true arm does work then falls) all
  inline, with a positional-integrity gate so a non-adjacent fall-through
  stays a `goto` (faithful to C# `goto case`). Switches with no convergence
  exit (every body self-terminates or falls) inline with the post-switch
  block as the implicit exit. mscorlib --csharp: 161 switches inlined
  (direct-Leave and direct-Throw case bodies inline too), ~12k gotos
  (down from ~18.5k pre-inlining), 141 `for` / 444 `while` loops
  (LoopDetection forms nested loops; HighLevelLoopTransform's `for` match
  now fires -- the pre-header entry branch is excluded from the for-loop's
  incoming-edge count, with an init-scope guard). The seed also drops
  redundant fall-through gotos at the end of if-else arms (a block-final
  `br X` or a true-arm `br X` of an if whose textual-next is X), gotos that
  fall into the immediately-following block's construct entry (`goto X;
  <block>{ try { X: ... } }`), and swaps the resulting empty `if (cond) { }`
  arms to a negated `if (!cond) { else }` -- together ~6.5k fewer gotos and
  ~31k fewer lines on mscorlib. A `break;` is emitted for an inner-loop
  `br` to the loop's exit (and `continue;` for a `br` to a loop header);
  `endfinally` and construct-body leaves render as nothing (not a bare,
  invalid `break;`). The `refanytype` opcode (`RefAnyType`) renders as
  `__reftype(arg).TypeHandle` (the C# undocumented keyword + `.TypeHandle`
  member, matching the real back end's `ExpressionBuilder`); no
  unhandled-instruction `(default)/*op=NN*/` fallthroughs remain on mscorlib.
  `RemoveUnreachableBlocks` drops dead blocks the
  structure transforms leave behind (a port of the C#
  `SortBlocks(deleteUnreachableBlocks)` subset).
  Remaining gaps vs the real back
  end: gotos for multi-pred join blocks and loop-internal condition/increment
  jumps (needs DetectExitPoints + HighLevelLoopTransform), full type names
  (no `using` directives), and overload-resolved casts -- these land as the
  Phase 5 C# AST + resolver back end (above) is wired in to replace the seed.
- Phases 5-11 (C# AST + resolver + output, disassembler output, orchestration,
  ILSpyX, BamlDecompiler, the full `ilspycmd`, integration) -- per
  `PORT_PLAN.md`.

The CLI does `<assembly> --il [-t Type]` (IL text disassembly),
`<assembly> --ilast[-all] [-t Type]` (decode bodies to an ILAst tree and dump
it), and `<assembly> --csharp [-t Type]` (translate decodable bodies to
C#-ish text via the Phase 5 seed) end-to-end today; the real C# AST +
`CSharpOutputVisitor` back end (Phase 5, in progress) will replace the seed
once the `ExpressionBuilder`/`StatementBuilder`/`CallBuilder` resolver and
the remaining Visit methods are ported.

## Prerequisites

- CMake >= 3.15 (4.x works; the VS 2026 and Ninja generators are supported).
- A C++17 compiler: **Visual Studio 2026** (MSVC 14.5x, recommended), or
  GCC 9+ / Clang 10+ on Linux.
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT` environment
  variable pointing at your checkout. Dependencies (see `vcpkg.json`):
  `nlohmann-json`, `plog`, `lz4`, `cxxopts`, `gtest`.

## Build

Set `VCPKG_ROOT` to your vcpkg checkout first.

```sh
# Windows (Visual Studio 2026 generator -> ilspy.slnx solution). Recommended;
# the generator finds VS 2026 and the Windows 10 SDK itself, no dev-prompt
# needed:
cmake --preset windows-vs2026
cmake --build build/windows-vs2026 --config Debug
ctest --test-dir build/windows-vs2026 -C Debug --output-on-failure

# Windows (Ninja, VS dev environment) -- run from a Developer Command Prompt
# or source vcvarsall.bat x64 first so cl/ninja are on PATH:
cmake --preset windows-ninja
cmake --build build/windows-ninja
ctest --test-dir build/windows-ninja --output-on-failure

# Linux/macOS (Ninja, run from a shell with a compiler on PATH):
cmake --preset linux-ninja
cmake --build build/linux-ninja
```

The VS 2026 generator writes `build/windows-vs2026/ilspy.slnx` -- the modern
XML solution format VS 2026 defaults to (the successor to legacy `.sln`).
vcpkg restores the manifest dependencies into `build/<preset>/vcpkg_installed/`
on the first configure. `miniz` is optional during Phase 0-6 (see `vcpkg.json`
`deflate` feature and PORT_PLAN.md decision D12); it becomes required at Phase 7/8.

## Targets

| Target       | Type        | Description                                   |
|--------------|-------------|-----------------------------------------------|
| `ilspy`      | static lib  | The aggregate core library.                   |
| `ilspy_cli`  | executable | The `ilspycmd` port (Phase 10 fills it in).   |
| `ilspy_tests`| executable | Google Test driver; `ctest` runs it.          |

```sh
ctest --test-dir build/<preset> --output-on-failure
# or run the driver directly to use --gtest_filter
./build/<preset>/tests/ilspy_tests --gtest_filter='Util_*'
```

## Conventions (C# -> C++; see PORT_PLAN.md section 9 for the full table)

- **Encoding**: UTF-8 (`std::string`) everywhere internally; convert at the
  `#US`-heap / output boundaries via `Decompiler/Util/Utf.hpp`.
- **Ownership**: `std::unique_ptr` for exclusive ownership; `std::shared_ptr`
  only where sharing is real and cycles are provably absent; raw `T*`/`T&` for
  non-owning observation. No bare `new`/`delete`.
- **Nullability**: `std::optional<T>` for value-like nullable returns; raw `T*`
  may be null only where documented; references are never null.
- **Immutability**: `std::vector` is immutable-by-convention after construction
  where the C# used `ImmutableArray<T>`; `Decompiler/Util/ImmutableStack.hpp`
  is the persistent stack the IL reader needs for evaluation-stack snapshots.
- **Invariants**: the ported `CheckInvariant` runs in debug builds (`NDEBUG`
  off) after every transform, exactly as in C# -- it is the safety net for the
  loss of C# nullable-reference-type checking.
- **No `-Werror`** until Phase 11 (decision D6).

## Layout

```
cpp/
  Decompiler/            <- ICSharpCode.Decompiler
    Util/                <- Util primitives (UTF-8/16, Span, ImmutableStack)
    Metadata/            <- Metadata layer (Phase 1)
      Ecma335/winmd/     <- vendored microsoft/winmd headers (MIT; pinned commit)
    Xml/                 <- port-authored System.Xml(.Linq) stand-in (the XML name
                           layer: XmlConvert.EncodeLocalName/VerifyNCName, XName, XNamespace;
                           the XLinq DOM: XObject/XNode/XContainer/XDocument, the leaf nodes,
                           XAttribute/XElement, and the lazy Elements()/Attributes()
                           sequences; and the serialization slice: XmlWriter (the flattened
                           XmlWellFormedWriter/XmlEncodedRawTextWriterIndent composition),
                           the ElementWriter walk, and the ToString/WriteTo/Save surface;
                           the BAML decompiler consumes the BCL classes directly)
  ILSpyX/                <- ICSharpCode.ILSpyX core subset (Phase 8)
  BamlDecompiler/        <- ICSharpCode.BamlDecompiler (Phase 9)
  ILSpyCmd/              <- ICSharpCode.ILSpyCmd CLI (Phase 10)
  tests/                 <- gtest, mirroring source dirs
```
