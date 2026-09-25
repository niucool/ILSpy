# ILSpy C++ Port -- Session Handoff (written after the master merge)

Read this + `PORT_PLAN.md` + `cpp/README.md` (and the sibling
`cpp/PORT_LOG_BAML.md` / `cpp/PORT_LOG_DISASM.md` logs) at the start of a
fresh session.
Standing baseline: **connid_csharp sha256 `abf6a844eba7c0b3`** (re-pinned
DELIBERATELY at the facade-transform-wiring slice: the facade's
GetAstTransforms now registers the eight merged-but-unwired transforms
(IntroduceUsingDeclarations among them), so the attribute sections carry the
`using` lines and the FullyQualifyAmbiguousTypeNamesVisitor qualifies the
nested enum; the render passes the settings' Allman-derived formatting
options, so no space precedes the attribute argument lists. The whole
attribute block now byte-matches the repo C#'s
DecompileModuleAndAssemblyAttributesAsString over the identical fixture,
modulo the environment-gated Debuggable decode. Prior re-pin:
`db7d7500a7246958` at the master merge (the eventSetter collision fix);
`7c269b8e61993d80` at the event-member-surface slice). Sweep discipline: passed + skipped MUST
equal ran, and the exit code is the gate.

## THE MASTER MERGE (read first -- this branch's shape changed)

The parallel gnhf lineage (master, tip `f1c236623`, merge-base
`c35f4a861`) was merged into cpp wholesale: it carries ALL remaining
GetAstTransforms slots (TransformFieldAndConstructorInitializers `42f01d491`,
IntroduceUsingDeclarations `79173ff63`, AddXmlDocumentationTransform,
CombineQueryExpressions, IntroduceExtensionMethods, IntroduceQueryExpressions,
RenameVisualBasicAnonymousTypes, ...) and the recorded sub-deferrals
(InsertDeconstructionVariableDeclarations, UseImplicitlyTypedOutAnnotation,
IsRefReadOnly). GetOptions remains user-deferred (three times -- do NOT pick
it up).

Complementary split resolved by the merge: cpp keeps its unique facade
(CSharpDecompiler.cpp whole-module decompiler + InstanceState, the -o
writer, DecompilerTypeSystem, RequiredNamespaceCollector*, the reference-set
wiring); master contributed everything else. 95 conflicted files resolved
`--theirs` (master's are the complete continuations), with these
facade-side adaptations (all in this merge commit):
- The facade's IL pipeline entry re-pointed: master inlined the old
  GetILTransforms()+RunTransforms() pair into
  `IL::RunGetILTransforms(function, context)`; the facade's
  `RunILTransforms` routes there and the list-returning static
  (CSharpDecompiler::GetILTransforms) was removed with its alias test.
- `TransformContext` (master's) is ctor-built (4 args, accessor methods
  TypeSystem()/TypeSystemAstBuilder()/Settings()); the facade's
  RunAstTransforms now requires a decompilation context (no more
  default-null form) and builds the context through master's ctor.
- The facade's delegate-body hook lives on: `ILTransformContext::
  DelegateBodyResolver` + `ILFunction::DelegateType` re-grafted onto
  master's headers (additive members).
- MSVC vs GCC box divergence FIXED (CSharpPrimitiveCast): on LP64 GCC
  `long long` and `std::int64_t` (= `long`) are distinct types while they
  coincide on MSVC (the gnhf lineage's environment). The type-code table
  now recognizes both 64-bit spellings; `UnboxInt64`/`UnboxUInt64`
  (CSharpPrimitiveCast.hpp) are THE way to read a 64-bit box; the
  Int64->Int64 identity cast re-boxes canonically. This un-broke 20
  StatementBuilderTest + 2 ExpressionBuilderBinaryNumericTest failures.
- DeclareVariables::Run RESTORED (master shipped only the analysis half):
  the mutation phase (EnsureExpressionStatementsAreValid /
  InsertVariableDeclarations / UpdateAnnotations / the SkipInit forms) was
  re-grafted onto master's surface -- VariableToDeclare keeps master's RAW
  `IL::ILVariable*` handle; the shared handle for annotations is read off
  the first use's existing ILVariableResolveResult (the file-local
  VariableHandleOf helper). CombineDeclarationAndInitializer /
  CanBeDeclaredAsOutVariable / IsReferencedWithinDeclaringCall are
  MASTER's (context-taking) versions. This fixed 12 CSharpDecompilerTest +
  1 AstTransformPipeline failure.
- GetSharedResolveResult + GetILFunctionAnnotation re-implemented in
  Annotations.cpp (master's header declared them but no TU defined them --
  the stub Run never called them).

### Merge gates (what "green" means on this box)

- Full suite: `12852 ran` with the env-exclusion filter
  (`--gtest_filter=-$(cat /tmp/excl2.txt)`-equivalent, no env vars) --
  `12822 passed + 30 failed`, where ALL 30 are the mono-profile family
  (MetadataTypeDefinitionTest 16, TypeProviderTest 12,
  MetadataModuleResolutionTest 12, StandaloneSignatureTest 8,
  MetadataNamespaceTest 6, ResolveTypeDirectBaseTypesTest 4,
  MetadataModuleTest 2 -- the tests' own default fixture
  `/usr/lib/mono/4.5/mscorlib.dll` does not exist on this box; the golds
  are pinned to that profile. With ILSPY_TEST_MSCORLIB pointing at net48
  they fail differently -- wrong-profile golds). The gnhf lineage's CI
  environment must have had mono; running them green here needs that file.
- The classic subset filter (below) is green: 216 ran = 201 passed +
  15 skipped, exit 0 (some suites were absorbed/renamed by the merge;
  GetILTransformsTest/RunTransformsTest no longer exist).
- connid: re-pinned (see the baseline note above).
- The 30-family + the older 283-name exclusion lists live in
  /tmp/excl2.txt / /tmp/fall.txt (regenerate from a full no-env run if
  /tmp was wiped: every failure lists itself; the crashers abort -- add
  the last [ RUN ] test to the filter and re-run).

### Follow-ups out of the merge (do NOT redo what already landed)

- ~~The DelegateConstruction embedding re-port~~ **DONE** (the slice after
  the merge): `DelegateConstruction_impl.cpp` is registered and adapted --
  the nested pipeline is the merged-driver prefix up to the transform's
  position (`RunILTransformsThroughBlockTransforms` + HighLevelLoopTransform
  + FixRemainingIncrements + CopyPropagation) plus CombineExits (the C#
  TakeWhile + GetTransforms concat, sliced against the MERGED order since
  master runs HighLevelLoop before FixRemainingIncrements). The driver calls
  it after CopyPropagation (the C# slot, CSharpDecompiler.cs line 168).
  `ILTransformContext::Metadata` (the C# PEFile slot) is re-grafted next to
  DelegateBodyResolver and set by the facade's WireTransformContext; the
  bare CLI path leaves it null and the transform no-ops on the null
  resolver. The merged matcher's `DelegateConstructionMatch::method` is
  now populated (`targetMethodRef.get()`) -- master set only the shared
  handle. The 3 Run tests are restored (RED = the link error on the
  undefined Run). Gates: DelegateConstruction.* 17 passed + 1 env-skip;
  classic subset 219 = 204 + 15; full env-excluded suite 12855 ran with the
  same 30 mono-profile failures; connid byte-identical (named methods gate
  out -- the corpus has no anonymous-method bodies).
- `ILVariablePtr`-taking ctors (ILVariableResolveResult) vs master's raw
  `IL::ILVariable*` in VariableToDeclare: bridged via VariableHandleOf
  (first-use annotation). If another surface needs handles, follow that
  pattern; do NOT alias-construct shared_ptrs from raws.

## Current position (pre-merge history -- the merge supersedes the queue)

- **PatternStatementTransform: five arms landed** (`93f1f9bd2`,
  `dfcba6e08`, `01dd3aea7`): the logic arms, TransformFor, and the
  foreach-on-array reshape (forOnArrayPattern + VisitForStatement +
  VariableCanBeUsedAsForeachLocal over the analysis; the
  AddressUsedForSingleCall special case deferred on the address-use list +
  the resolved-IMethod surface). The Annotation<BlockContainer>/
  Annotation<ILFunction> queries now live on the shared Annotations surface
  (CS::GetBlockContainerAnnotation / CS::GetILFunctionAnnotation).
- **PatternStatementTransform: COMPLETE** (through `0073d82c7`): all nine
  families landed -- the logic arms (`93f1f9bd2`), TransformFor
  (`dfcba6e08`), foreach-on-array (`01dd3aea7`), foreach-on-multi-dim
  (`b4e892ad2`), foreach-on-inline-array (`df98f5c45`), the automatic
  property declaration half (`b31a10a2f`), the destructor (`77a8695af`),
  try-catch-finally (`b40d74d57`), C# 8.0 enhanced using (`e87a68ff8`),
  C# 7.3 pattern-based fixed (`6f27d7110`), and the backing-field
  identifier rewrite (`0073d82c7`). Pattern-placeholder surfaces landed
  along the way: BlockStatement and AttributeSection (`77a8695af`) and
  CatchClause (`b40d74d57`) -- each the C# generator's `implicit operator
  X(Pattern)` + nested PatternPlaceholder. MemberResolveResult grew
  SharedTargetResult (the re-point call site). CSharpDecompiler gained
  RemoveAttribute (the KnownAttribute section stripper). Remaining
  sub-deferrals, loud in the .cpp header: AddressUsedForSingleCall,
  the anonymous-type `var` decision, the automatic-events family.
- **DeclareVariables: COMPLETE** (`e6ee1d822` analysis, `d936b1f81`
  mutation): the IAstTransform Run with EnsureExpressionStatementsAreValid
  (direction unwrap + discard; the temporary arm deferred on
  AssignVariableNames.GenerateVariableName), InsertVariableDeclarations
  (combine / out-var / separate arms), UpdateAnnotations, and the
  GetAstTransforms slot. The TransformContext carries the
  TypeSystemAstBuilder slot (RunAstTransforms builds it via the new
  CSharpDecompiler::CreateAstBuilder -- the C# line-722 static).
  Sub-deferrals, loud in DeclareVariables.cpp:
  InsertDeconstructionVariableDeclarations (needs
  TranslateDeconstructionDesignation), the SkipInit forms (a live
  context.TypeSystem), the anonymous-type `var` decision (NRExtensions
  ContainsAnonymousType), IsRefReadOnly (the ILVariable flag).
- RED discipline held (16 DeclareVariables cases total; 60+ across the
  AST-transform suites after the six arms above).
- **Design notes for the landed code (read before extending):**
  - The void-visitor re-visit loop carries the C# `ContextTrackingVisitor<
    AstNode>` return value in the visitor's `lastResult` slot.
  - Patterns are lazily built process-lifetime singletons with pattern
    children embedded through `Expression::ToExpression` /
    `Statement::ToStatement`.
  - The C# Dictionary<ILVariable, VariableToDeclare> ports as an
    insertion-ordered vector + a reference-identity index; VariableToDeclare
    holds a NON-OWNING shared-handle alias (no-op deleter) over the
    IL-function-tree-owned variable so the annotation constructions copy it
    safely.
  - Trees handed to RunAstTransforms/DeclareVariables.Run need the root
    ILFunction annotation for the invalid-statement fixup (the C# contract;
    the pipeline driver test attaches one now).
- Remaining PatternStatementTransform work: none (the sub-deferrals above
  are tracked in place).

## Next steps (in order)

1. TransformFieldAndConstructorInitializers -- the next GetAstTransforms
   slot after DeclareVariables (915 lines: the ThisCallClass/Struct
   patterns, the InitializerSequence analysis, the field/constructor
   initializer movement). A multi-slice project.
2. IntroduceUsingDeclarations (439 lines: the FindRequiredImports
   visitor, the namespace resolution through the compilation, the
   using-statement insertion) -- the high-value one: the `using`
   lines at the top of every render.
3. GetOptions (the DecompilerSettings -> TypeSystemOptions mapping in
   DecompilerTypeSystem.cs) -- deliberately deferred: the settings
   defaults could shift the render; port with its own baseline
   evaluation.
4. The remaining loud sub-deferrals: UseImplicitlyTypedOutAnnotation,
   DeclareVariables' InsertDeconstructionVariableDeclarations,
   IsRefReadOnly.

## RESOLVED this session (do NOT re-open)

- **AddCheckedBlocks**: ALREADY PORTED on the parallel session lineage
  (commit `90b8fdfe5`, "gnhf 168", NOT an ancestor of this branch --
  it arrives via a future merge). An in-progress duplicate was reverted
  cleanly this session (the working tree was restored before any
  commit; the link breakage the revert exposed was the parallel
  lineage's annotation-handle definitions living in that file). Before
  porting anything here, check `git log --all --oneline -- '<path>'`
  for parallel-lineage commits.
- **The ReplaceMethodCallsWithOperators methodof arm** (the
  VisitCastExpression pattern over LdTokenPattern/TypePattern): DEAD
  CODE UPSTREAM. The LdTokenAnnotation those patterns read is only ever
  read, never attached anywhere in the C# (the GetFieldFromHandle
  comment documents the mechanism), so the pattern never matches.
  Resolved as not-porting (the .cpp deferral comment updated).

## LANDED this session (the follow-up arms + two transform slots)

- `a81b94216` -- the ReplaceMethodCallsWithOperators follow-up arms: the
  String.Concat reduction (the params-array flattening, the
  CheckArgumentsForStringConcat gates, the ToString-elimination chain,
  the expression-tree suppression), the System.Type.GetTypeFromHandle
  typeof unwrap (the typeHandleOnTypeOfPattern -- the port's first
  pattern over a Choice of expression nodes), the
  Activator.CreateInstance<T>() rewrite, the GetSubArray range indexer,
  and the decimal op_Increment reverse optimization. The C#
  GetFieldFromHandle arm is dead code upstream; the methodof
  VisitCastExpression arm stays deferred (needs LdTokenPattern /
  TypePattern). The test-support LookupTypeDefinition grew the
  kind-derived IsReferenceType the ToString null-safety gate reads.
- The IntroduceUnsafeModifier port (the next slot): the bool-visitor
  OR-reduce + the `unsafe` modifier add at the member boundary, the
  pointer/sizeof/pointer-rank/function-pointer/dereference/address-of/
  fixed detections, the resolve-result arms, and the two rewrites
  (`*(ptr + i)` -> `ptr[i]`, `(*p).M` -> `p->M`). DeclareVariables moved
  to the fourth pipeline slot.
- Earlier this session (the reference-set wiring + the SkipInit forms +
  the operator core): see the previous handoff sections and git log.

- `a07f7c6e9` -- the SkipInit forms in DeclareVariables (the C# lines
  717-770): a local whose initial value is read before any store gets
  the System.Runtime.CompilerServices.Unsafe.SkipInit call (the
  out-variables form folds the declaration into the call's argument;
  the plain form declares then calls over the out-direction
  identifier). The enabler: TransformContext::TypeSystem wired
  (RunAstTransforms reads the compilation off its decompilation-context
  parameter -- the C# TransformContext ctor's IDecompilerTypeSystem; the
  attribute path's caller passes a SimpleTypeResolveContext over the
  module; a caller passing no context leaves the slot null and the arm
  degrades to the plain declaration).
- `aa94eceac` -- ReplaceMethodCallsWithOperators' user-defined-operator
  core, the first GetAstTransforms slot: the instance IAstTransform
  (Run + VisitInvocationExpression + ProcessInvocationExpression), the
  op_ metadata-name tables, and the four arms (binary, unary, the
  op_Explicit cast, op_True in a condition). UnwrapInDirectionExpression
  ports into SyntaxExtensions (it was on the deferred list). Fallout
  fixes: the TypeSystemAstBuilder's UseKeywordsForBuiltinTypes default
  corrected to the C# initializer (true -- the header documented it but
  the member said false; the connid baseline is unaffected), and the
  transform's .cpp qualified two sibling-namespace references (the
  nested CSharp::TypeSystem its new includes open). DeclareVariables
  moved to the third pipeline slot (the C# order).
- Earlier this session (the reference-set wiring): `b3cc5f1f4` the
  DecompilerTypeSystem over the resolver-loaded reference set, `9691fdf08`
  the KnownTypeCache FindType + the MinimalCorlib net, `46a269370` the
  SimpleCompilation derivation. A port-baml merge (`748421cdd`) landed
  mid-session; the gates were verified after it.

- `b3cc5f1f4` -- the reference-set wiring: the DecompilerTypeSystem
  (Decompiler/TypeSystem/) resolves every AssemblyReference row
  through the existing UniversalAssemblyResolver (the C# CLI
  GetDecompiler shape: the main file's own directory as the first
  search path, throwOnError FALSE -- an unresolved reference degrades
  to the name-only fallback, never a ResolutionException), dedups
  same-name to the highest version, and fills the compilation's
  Modules()/ReferencedModules(). A TypeRef scoped to an AssemblyRef now
  lands on the real entity: PresentationFramework's
  FrameworkContentElement.Loaded resolves to the PresentationCore
  RoutedEventHandler delegate. The CSharpDecompiler instance state
  and the static scaffold entries construct it (the resolver rides
  FIRST in InstanceState -- its keep-alive registry owns the loaded
  files); the SingleModuleCompilation placeholder is DELETED. The
  connid renders are unchanged (its references resolve to nothing in
  the temp dir -- the baseline holds at 7c269b8e). The deferred queue
  arms are loud on the ctor: the ExportedTypes BFS, the
  implicit-references set, GetOptions.
- `9691fdf08` -- FindType through the KnownTypeCache + the
  missing-known-types MinimalCorlib net (the ported synthetic module's
  first production consumer): the connid primitives resolve (the
  attribute literals keep the uncast form -- without the net,
  Int32/Boolean/String args render as ((Int32)8) casts through the
  ConvertValue unknown-underlying arm; the real tool avoids them
  through this exact net).
- `46a269370` -- derive DecompilerTypeSystem from the ported
  SimpleCompilation (the C# hierarchy): the ctor resolves the refs and
  calls the inherited Init through FileModuleReference adapters (the
  C# file.WithOptions shape); the merged root namespace, the
  KnownTypeCache FindType, and the module snapshots now come from the
  base class.
- Earlier this session (the facade completion): the event member
  surface `5be3dc557` (the connid re-pin to 7c269b8e -- the corpus's
  Button.Click), the -o file-writer `7b6396247`, the CSharpDecompiler
  instance surface `ab5ed80f0`, the mscorlib test env-gate
  `e70667c46`.

- `5be3dc557` -- the event member surface in DecompileTypeToString:
  `event Type Name;` via the module's event entity (the C#
  entity.ReturnType), the add/remove accessor methods folded into the
  skip set; the connid re-pin above (the corpus's Button.Click).
- `7b6396247` -- the -o file-writer: DecompiledOutputFilePath (the
  assembly base name, or the TYPE NAME VERBATIM with -t) +
  `WriteOutputFile`; the --csharp path renders to a buffer and routes
  through the per-file output, stdout bytes unchanged.
- `ab5ed80f0` -- the CSharpDecompiler INSTANCE surface: the ctor wires
  the type system once (the pimpl InstanceState: compilation + module +
  the settings copy + the partial-types registry); the instance entries
  (DecompileWholeModuleToString / DecompileTypeToString /
  AddPartialTypeDefinition / FindPartialTypeInfo) consult the instance's
  own map, so one decompiler's registrations are invisible to another.
  C++ forbids static + non-static members with the same signature, so
  the static registry shims renamed: RegisterPartialTypeDefinition /
  FindRegisteredPartialType (the C# names belong to the instance). The
  render body is shared (DecompileTypeToStringBody). The CLI's --csharp
  constructs one decompiler per run (byte-identical output).
- `e70667c46` -- ExtractResourceMscorlib env-gate fix (the sweep
  filter grew IlspyCmdProgramTest and surfaced the missing
  fs::exists + GTEST_SKIP guard).
- Earlier this session (see git log / the previous handoff): the
  automatic-events arm `376a0b299` completing the
  PatternStatementTransform family set, AddressUsedForSingleCall
  `d52d163a8`, the anonymous-type var decision `7060a585b` +
  `fe5d33413`, PropertyAndEventBackingFieldLookup `a0c5631cc`.

- `d52d163a8` -- AddressUsedForSingleCall: the single-call this-pointer
  address use is acceptable as the foreach item variable (the last
  VariableCanBeUsedAsForeachLocal deferral; both blockers had landed).
- `7060a585b` + `fe5d33413` -- the anonymous-type `var` decision
  everywhere it was noted: HasGeneratedName / HasOnlyReadOnlyProperties /
  IsAnonymousType / ContainsAnonymousType in TypeSystemExtensions (the
  NRExtensions family), the three foreach arms, and DeclareVariables'
  combine + out-var arms (the OutVarResolveResult re-annotation; the
  UseImplicitlyTypedOutAnnotation sub-case stays deferred -- the
  annotation surface is not ported).
- `a0c5631cc` -- PropertyAndEventBackingFieldLookup (the metadata
  surface for the events arm above).

The remaining known deferrals, loud in place: the automatic-events ARM
(the item above), UseImplicitlyTypedOutAnnotation, the DeclareVariables
sub-deferrals (InsertDeconstructionVariableDeclarations, the SkipInit
forms, IsRefReadOnly), the event member surface in the facade, and the
Facade instance surface.

## Hazard-ledger highlights (keep)

- **bash cwd is the REPO ROOT in a fresh session:** every build/test
  invocation needs `cd /home/jim/source/ilspy/cpp &&` (or an absolute
  `ninja -C /home/jim/source/ilspy/cpp/build/linux-ninja`). Without it
  ninja dies on a chdir error that does NOT contain the word "error", so
  error-greps come back empty and the retry loop looks like a hang. Do not
  re-run the same unprefixed command.
- **Include order is a second face of the TypeSystem-shadowing hazard:**
  `Annotations.hpp` pulls `TranslatedExpression.hpp`, which writes
  unqualified `TypeSystem::IType` while inside `namespace ...CSharp`. In a
  TU that has ALREADY opened `ILSpy::Decompiler::CSharp::TypeSystem` (via
  `CSharpTypeResolveContext.hpp` / `UsingScope.hpp` /
  `DecompileRun.hpp`-carrying headers) that lookup hits the nested
  `CSharp::TypeSystem` and fails to compile. Include `Annotations.hpp`
  BEFORE those (the `Annotations_Test.cpp` order).
- **Include hygiene:** a CSharp-namespace header must NOT include
  DecompileRun.hpp; fwd-declare `ILSpy::Decompiler::DecompileRun` (sibling)
  and let the .cpp include it.
- **Unqualified sibling-namespace names inside `namespace CSharp { ... }` do
  not see TypeSystem/other siblings** -- fully qualify
  (`::ILSpy::Decompiler::TypeSystem::X`) in code living inside the CSharp
  namespace; namespace ALIASES are safe (they bind at the alias).
- **Fwd-decl placement:** sibling-namespace fwd-decls must sit OUTSIDE
  `namespace ILSpy::Decompiler::CSharp { ... }` (fully-qualified statements),
  never nested inside it (the nested form creates a shadowing
  `CSharp::TypeSystem`).
- **Partial-write trap on REAL files:** after ANY scripted-edit abort,
  `git status` + `git diff` BEFORE staging; prefer in-place `edit` over
  scripted rewrite for files that already exist.
- Stale-binary trap: rebuild `ilspy_cli` separately before any connid run
  (test-target builds leave it stale).
- Stash `stash@{0}` is the port-baml WIP from another session -- DO NOT touch.
- The full ilspy_tests run has a PRE-EXISTING abort (a vector OOB in
  TypeSystem after ~252 env-pinned failures) -- verified identical pre/post
  both slices via a stash-and-rerun; it is the known cross-test corruption
  cascade, not a regression signal. The sweep filter below is the gate.
- Merge commits from the port-baml / port-disassembler sessions land on the
  branch independently mid-session -- do not touch them; verify after any
  merge lands.

## Deferred (loud, documented in code)

- ~~ConvertCoalesce / ConvertComparison (TransformExpressionTrees)~~ MOSTLY
  DONE: the blocker analysis was stale -- the merge completed both the
  CSharpConversions and CSharpResolver ports. The bigger discovery: the
  merge had ORPHANED the whole TransformExpressionTrees port -- neither
  TransformExpressionTrees.cpp nor Call.cpp was in the IL CMakeLists (the
  stale _Match/_Run object files in the build dir are pre-merge remnants),
  so the transform was dead code and its 23 tests silently not running.
  The slice re-registered both files and re-grafted five IL-layer surfaces
  the merge had dropped (ILTransformContext::TypeSystem, the C# context
  member; BlockContainer::ExpectedResultType; IfInstruction::LogicAnd/Or;
  the LdMemberToken node; Call's resolved-method ctor family +
  ExpectedTypeForThisPointer) -- all C#-present members, verified against
  the reference source. ConvertCoalesce is COMPLETE;
  ConvertComparison is COMPLETE (all four arms: the 4-arg lifted form,
  the resolver user-defined-operator arm, the String
  op_Equality/op_Inequality arm, and the builtin Comp fallback).
  The shared-method-handle surface landed as
  `TypeSystemExtensions.h(pp)::AliasMethod(const IMethod*)` -- the
  consolidated no-op-deleter borrow (the C# holds every IMethod as a GC
  reference; the owner -- the module's methodDefs_ cache, a stub registry,
  the fake's creator -- keeps the instance alive, the SnapshotType
  convention (c) for methods). The four sites that had re-invented the
  inline lambda (CSharpResolver::GetUserDefinedOperatorCandidates,
  MetadataModule's file-local AliasMethod, MetadataMethod::Specialize,
  FakeMethod::Specialize) now route through it. THE RESOLVER LIFETIME BUG
  THE NEW rr-arm TEST EXPOSED (fixed in the same slice): the
  user-defined-operator candidate collection kept only RAW pointers to
  the freshly-built LiftedUserDefinedOperator wrappers, which the C#
  HashSet holds as strong references -- the wrappers freed at each scan's
  end (the second scan's fresh lift REUSED the freed block, which is why
  the pointer-identity dedup "worked" and the resolver's own tests passed
  by UB luck; the TET fixture's heap state segfaulted). The fix:
  ResolveUnaryOperator and ResolveBinaryOperator hold every candidate
  handle in a local keep-alive vector, wrap it in a shared holder after
  the AddCandidate loop, and thread it through
  CreateResolveResultForUserDefinedOperator into the
  OperatorResolveResult's new ctor-2 `methodKeepAlive` (a type-erased
  `shared_ptr<const void>`; null for the module-cached methods that need
  no keep-alive) -- the rr keeps its method raw valid for its own
  lifetime, the C# GC-reference field made explicit. TEST DISCIPLINE: the
  resolver-backed
  arms need the test compilation to register the FULL KnownTypeCode range
  (Object..String + NullableOfT) -- an unregistered code falls to the
  non-shared unknownType_ stub whose shared_from_this throws bad_weak_ptr
  (the CSharpResolverBinaryOperator_Test fixture precedent).
- ~~ProxyCallReplacer~~ DONE: ported with the established bridges. The C#'s
  `context.CreateILReader().ReadIL(...)` mid-transform body decode routes
  through the context's DelegateBodyResolver hook (the CreateILReader
  bridge the delegate-construction path established; the facade already
  wires it to ReadIL over the module); `EarlyILTransforms` is the ported
  triple (ControlFlowSimplification + SplitVariables + ILInlining) run
  over the decoded proxy with a copied context; the specialized target's
  `shared_ptr<IMethod>` comes from the last slice's AliasMethod surface
  (Specialize returns the module-registry-owned raw). ONE documented
  divergence: the C# decodes with the callee's GENERIC CONTEXT
  (`new GenericContext(inst.Method)`), which the port's reader has no
  surface for -- the hook decodes uninstantiated bodies (the same
  limitation DelegateConstruction records for generic targets). Wired at
  its C# driver slot (after the last per-block group, before
  FixRemainingIncrements). TEST TRAP (the D66/D94 precedent): the C#
  out-form `MatchLdLoc(out var)` must stay FILE-LOCAL -- adding it to
  PatternMatching.hpp alongside the match-against-v overload hijacks
  every non-const compare caller (an ILVariable* lvalue binds the out-form
  identity-better); a full-suite sweep caught it as a
  BlockMatchInlineAssignBlock regression.
- ~~THE MERGE'S ORPHANED TRANSFORM CLUSTER (SplitVariables + 9 more)~~
  PARTIALLY DONE -- a systematic merge gap discovered while porting
  ProxyCallReplacer: the merge's wholesale --theirs on the shared
  CMakeLists and GetILTransforms.hpp dropped ~10 of this lineage's
  transform registrations, their tests, AND their driver calls (master
  never had them; the links stayed green because the merged driver never
  calls them; every post-merge baseline was pinned under the reduced
  pipeline). The audit command: `for f in $(git ls-files
  'Decompiler/**/*.cpp'); do base=$(basename "$f"); grep -rq "$base"
  --include=CMakeLists.txt . || echo "ORPHAN: $f"; done`. SplitVariables
  is RE-REGISTERED and adapted (this slice -- its
  GetAddressLoadForRefLocalUse now walks the tree for the single store,
  the CachedDelegateInitialization precedent for the missing
  StoreInstructions list; AddressInstructions iterates the merged
  generic-ILInstruction entries). The remaining 9 (DeconstructionTransform,
  IndexRangeTransform, InlineArrayTransform, IntroduceNativeIntTypeOnLocals,
  LocalFunctionDecompiler, SwitchOnStringTransform,
  TransformArrayInitializers, DeconstructInstruction,
  UserDefinedLogicOperator -- all .cpp + their tests + their driver calls)
  are FULLY REPAIRED (the dedicated slice): all nine re-registered, their
  eight test files re-registered (22 restored tests, all green), and their
  driver calls re-added at the C# slots (SwitchOnStringTransform after
  SwitchDetection; TransformArrayInitializers/IndexRangeTransform/
  DeconstructionTransform in the per-statement group after
  NullPropagationStatementTransform; LocalFunctionDecompiler after
  DelegateConstruction; IntroduceNativeIntTypeOnLocals before
  AssignVariableNames). The re-grafted surfaces: five ILTransformSettings
  flags (SwitchStatementOnString, SwitchOnReadOnlySpanChar, LocalFunctions,
  Ranges, InlineArrays, Deconstruction -- all C# defaults true), the
  ILTransformContext LocalFunctionBodyResolver hook + CurrentTypeDefinition
  (the pre-merge shape, the DelegateBodyResolver convention),
  ILFunction::DeclarationScope, Block::GetContainingStatement, the two
  Deconstruction BlockKind values, ILInlining::CanMoveInto (re-exported in
  the header for IndexRangeTransform), the UserDefinedLogicOperator
  method-based ctor declaration, and the StringToInt 3-arg-ctor call-site
  updates. TWO REAL MERGED-TREE BUGS THE RESTORED TESTS EXPOSED, both
  fixed: (1) the merged ILInlining called the FLAG-PAIR MayReorder overload
  where the C# (and the pre-merge port) use the SEMANTIC
  SemanticHelper.MayReorder(inst, inst) -- the flag approximation rejects
  a store-to-one-local moving past a load-of-another, which broke
  IndexRangeTransform's fold (FindLoadInNext returned Stop); all of
  ILInlining's MayReorder calls now use the semantic overload; (2) the
  merged LookupTypeDefinition::HasAttribute reads the SetKnownAttributes
  list, not the SetAttributes IAttribute list (the pre-merge stub derived
  it) -- the local-function tests now set both. The IntroduceNativeInt /
  StoreInstructions-List adaptation: the per-variable use lists the
  pre-merge ILVariable carried are still gone (the D11/D62/D68 walk
  convention covers them); SplitVariables' GetAddressLoadForRefLocalUse
  walks the tree (the CachedDelegateInitialization precedent).
  INTRODUCE-NATIVE-INT AND USER-DEFINED-LOGIC-OPERATOR carry no dedicated
  test suites (their coverage is the driver-level families). GATES: the
  connid render is BYTE-IDENTICAL (the corpus has none of the nine shapes
  -- no re-pin needed); the full env-excluded suite 12908 ran with the same
  30 mono-profile failures; the audit command over
  `git ls-files 'Decompiler/**/*.cpp'` reports ZERO orphans. NOTE: the
  deliberately-dropped RunTransforms_Test.cpp / GetILTransforms_Test.cpp
  are NOT part of this (they test pre-merge surfaces the merge removed;
  leave their registrations dropped).
- IntroduceDynamicTypeOnLocals -- deferred on
  `DynamicInstruction.GetArgumentInfoOfChild`.
- ~~Attribute enum-argument decode (DebuggableAttribute's `DebuggingModes`)~~
  DONE (`49cfdfcc9`): the blocker was the reference-set SHAPE, not the
  resolver's search arms -- DecompilerTypeSystem loaded only the main
  module's direct AssemblyRefs. Ported the C# InitializeCoreAsync queue
  closure (DecompilerTypeSystem.cs lines 326-372): every loaded assembly
  contributes its ExportedTypes' implementations (an AssemblyRef
  implementation queues the forwarder's target -- System.Runtime's
  forwarders pull in System.Private.CoreLib; an AssemblyFile implementation
  queues the sibling module file), plus the implicit-references tail for
  .NETCoreApp/.NETStandard/.NET (System.Runtime.InteropServices +
  System.Runtime.CompilerServices.Unsafe, queued by parsed name). ONE
  documented divergence: the implicit tail arms ONCE (the C# re-arms per
  queue drain and would spin forever on an unresolvable implicit
  reference). The connid Debuggable row decodes to the member form when
  the reference set resolves; the test env-gates on System.Runtime loading
  (dotnet on PATH -- `PATH=/home/jim/.dotnet:$PATH`; the default bash env
  lacks it). Without it the decode-error comment form remains -- the
  standing CLI render.
- `DetermineAddressUse` deferred arms (LdFlda chains, Await, call-argument,
  ref-local shapes) in SplitVariables.
- The two recorded render-layer follow-ups (the enum member's nested
  qualification + the attribute argument-list space) BOTH DONE
  (`6c232b405`), and shared a root cause: NOT ConvertType bugs -- the C#
  facade's own builder sets AlwaysUseShortTypeNames=true and renders the
  short `DebuggingModes.X` pre-transform (verified with a C# probe over the
  repo's own ICSharpCode.Decompiler); the qualification comes from
  IntroduceUsingDeclarations's FullyQualifyAmbiguousTypeNamesVisitor, which
  the facade never ran because its GetAstTransforms still carried pre-merge
  "deferred" comments for the eight transforms master had ported. The space
  came from the render's Mono-default policy; the C# facade passes
  settings.CSharpFormattingOptions (Allman-derived) -- the facade now builds
  that policy (SettingsFormattingOptions in CSharpDecompiler.cpp). All
  eight transforms are wired at their C# slots; the attribute block
  byte-matches the repo C# render; the connid baseline re-pinned
  (abf6a844eba7c0b3).
- ~~The IL driver's DetectExitPoints placement~~ DONE: the driver now runs
  the C#'s TWO DetectExitPoints passes at their C# slots (the line-103
  first call after DetectCatchWhenConditionBlocks; the re-run after
  LoopDetection and BEFORE PatternMatchingTransform, where the port had it
  after). A behavioral RED does not exist by construction: DetectExitPoints
  rewrites unconditional branch-to-exit and nothing between the two call
  sites in the port's driver distinguishes those shapes. The change is
  pure C#-order fidelity, verified unobservable by A/B: the connid render
  byte-identical, three real net48 corpus assemblies (WindowsBase,
  System.Core, System.Configuration -- ~12k rendered lines,
  reference-resolved) byte-identical against the pre-change build, and the
  full 12856-test suite unchanged. The remaining deferred IL transforms
  (ProxyCallReplacer, YieldReturnDecompiler, AsyncAwaitDecompiler,
  DynamicCallSiteTransform, IntroduceRefReadOnlyModifierOnLocals) are NOT
  ported (no files) -- genuine port projects.
- The foreach-on-multi-dim arms: VERIFIED COMPLETE after the merge (the
  merged PatternStatementTransform carries the full
  TransformForeachOnMultiDimArray + MatchForeachOnMultiDimArray +
  MatchLowerBound + the four patterns, line-for-line with
  PatternStatementTransform.cs lines 516-700; 7 tests green).
- PatternStatementTransform's remaining arms + the
  IteratorVariablesDeclaredInsideLoopBody bail (see above).

## Build / test / sweep

```
cd /home/jim/source/ilspy/cpp
export PATH=/home/jim/cpp-tools/cmake/bin:/home/jim/cpp-tools/ninja-bin:$PATH
ninja -C build/linux-ninja ilspy_tests   # + ilspy_cli before harness/connid runs
./build/linux-ninja/tests/ilspy_tests --gtest_filter='PatternStatementTransformTest.*:AstTransformPipeline.*:TransformExpressionTreesTest.*:RunTransformsTest.*:GetILTransformsTest.*:CSharpDecompilerTest.*:SwitchOnStringTransformTest.*:SwitchOnStringHashtableTest.*:SwitchOnStringLengthCharTest.*:TransformDisplayClassUsageTest.*:TransformDisplayClassUsageSroaTest.*:LocalFunctionDecompilerUseSitesTest.*:StatementTransformTest.*:TransformCollectionAndObjectInitializersTest.*:TransformCollectionAndObjectInitializersStalePosTest.*:IndexRangeTransformTest.*:InlineArrayTransformTest.*:NamedArgumentTransformTest.*:DeconstructionTransformTest.*:TupleTransformTest.*:TransformArrayInitializersTest.*:ExpressionTransformsTest.*:LocalFunctionDecompilerTest.*:DelegateConstructionTest.*:DelegateConstruction.*:CombineExitsTransform.*:VariableUsageLists.*:IntroduceNativeIntTypeOnLocals.*:ReachingDefinitions.*:SplitVariables.*:Util_UnionFind.*:TypeSystemExtensionsTest.IsAnonymous*:PropertyAndEventBackingFieldLookupTest.*:IlspyCmdProgramTest.*' --gtest_brief=1
./build/linux-ninja/ILSpyCmd/ilspy_cli /tmp/connid_res.dll --csharp   # vs baseline
```

connid fixture: `tests/TestFixtures/ConnIdResFixtures.hpp`
(`WriteConnIdResDll()` -> temp dll). Baseline text at
`/tmp/connid_csharp_baseline.txt` (re-generate if /tmp was wiped; re-pin the
hash only after a DELIBERATE change, documented in the commit message).

Commit style: subject <= 72 chars, body explains the why, trailer
`Assisted-by: GLM:glm-5.3-flash:pi`, `git commit -F /tmp/msg.txt`, local
commits only, never push.
