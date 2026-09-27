# ILSpy C++ Port -- Session Handoff (written after the master merge)

Read this + `PORT_PLAN.md` + `cpp/README.md` (and the sibling
`cpp/PORT_LOG_BAML.md` / `cpp/PORT_LOG_DISASM.md` logs) at the start of a
fresh session.
Standing baseline: **connid_csharp sha256 `dfb728fb5d6b0935`** (re-pinned
DELIBERATELY at the tab-formatting migration -- the type members indent
one level (one tab per level) and the seed bodies follow, per the C#
SyntaxTreeToString convention; the blank separator before a type's
closing brace drops; prior: `aa0c70560a254275` at the whole-module
using set -- the header carries the
module-wide required namespaces; prior: `1349c3bf...` the event +=/
delegate foldings, `b15a4355...` the body-name simplification slices: the own-type
static member accesses render unqualified (`f532f2d0...` -- the .cctor's
`StaticReadonlyField = 42;`, the bare method groups) and the
new-expression types render short (`b15a4355...` -- `new
RoutedEventHandler(...)`). Prior pins: `8944c06b3e316e71` (the
whole-module namespace grouping -- the types nest
under `namespace X { }` blocks; the single-type -t header rides the
entry and does not move the whole-module hash). Prior pins:
`025b55b938356763` (the render-fidelity slices: the enum keyword +
builtin keywords (`9c778042...`) and the expression-bodied getter-only
properties (`025b55b9...`); the enum member list did not move it (the
connid module has no enum types). Prior pins: `185df866d5551fd8`
(the accessor bodies; also across the facade-gap batch: the explicit-impl interface
qualifier (`d4149f89...`), the member/type attributes (`65650637...`), and
the accessor visibility/body forms (`185df866...` -- the getter-only
properties with real bodies render their blocks instead of the stub).
The unnameable-base-list filter and the const initializer did not move
it. Prior pins: `b2a7f7cfc3d5a7f5` (the implicit-base-call elision),
`51ae0b55e6017c2e` (the type modifiers + base types),
`af05cf1b7861f69b` (the member modifiers), `abf6a844eba7c0b3` (the
facade-transform wiring): the type-level render now
carries the member declaration modifiers -- the oracle's `public Button
_okButton;` / `private void OKButton_Click(...)` -- the body-less
abstract/interface method declarations that previously vanished, and the
type headers' modifiers + base-type lists (`public class Page1 :
Application, IComponentConnector`; the unconditional `partial` stand-in is
gone -- partial renders only for a registered partial half). Intermediate
pins: `af05cf1b7861f69b` (the member modifiers), prior
`abf6a844eba7c0b3` (re-pinned
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

- Full suite: `13016 ran` with the env-exclusion filter
  (`--gtest_filter=-$(cat /tmp/excl2.txt)`-equivalent, no env vars) --
  32 unique failures: the 30 mono-profile family (see below) + the 2
  newly-counted ResolveType fixture failures (the PD13 retirement; see
  the PD13 section), where the 30 are the mono-profile family
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

0. The remaining AST-pipeline surface (the follow-ons the slice opened):
   the facade renders the de-sugared methods but the ILAstToCSharp
   render of the de-sugared locals still names the state machine
   fields `num_1`-style (AssignVariableNames over the transferred
   locals); the field/property surfaces of the type header (the C#
   DecompileType's field and property declarations) are still the
   metadata-slice deferral. Then continue with:
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

### AsyncAwaitDecompiler (parts 1-4, commits 40e0b9927/195d60f6a/4a6c9b9bf/1f9248eb4)

The full task-shape port of `ICSharpCode.Decompiler/IL/ControlFlow/
AsyncAwaitDecompiler.cs` (the async void / Task / Task<T> state
machines): MatchTaskCreationPattern (the builder/state field pair, the
-1 initial state, the Start call, the get_Task return), the MoveNext
analyses (AnalyzeMoveNext / ValidateCatchBlock / the AnalyzeDisposeAsync
gate), InlineBodyOfMoveNext (the body swap, the branch-to-set-result
conversion into leaves, the variable registration) + CleanUpBodyOfMoveNext,
AnalyzeStateMachine (the per-container StateRangeAnalysis in the
AsyncMoveNext mode; the await-point blocks -- everything between the
awaiter field store and the leave -- collapse to Await(ldloca awaiter) +
a branch to the state block; the entry skips the state dispatcher),
DetectAwaitPattern (the GetAwaiter/IsCompleted/GetResult chain folds
into the Await node over the awaited value, carrying the methods), the
tail cleanups (CleanDoFinallyBodies' null early-out,
TranslateFieldsToLocalAccess shared with the yield decompiler,
FinalizeInlineMoveNext turning undetected leaves into InvalidBranch),
and the GetILTransforms driver slot right after YieldReturnDecompiler.

Engine fixes that surfaced under the locally-compiled fixture
(`/home/jim/ilspy-test-fixtures/async_fixture/AsyncFixture.dll`, the
AsyncShapes AwaitPlain/AwaitTask/AwaitTaskOfT trio; rebuild with the
csc line in the test file's fixture comment):
- The reader resolves the catch-clause exception variable's type (the
  C# reads it through the module at IL-reader time; the port had left
  it null). A plain catch reads its TypeDef/Ref/Spec token through
  GetFullTypeName; System.Exception becomes the KnownType stand-in;
  filter handlers get Object, matching the C# ILReader.
- ReachingDefinitionsVisitor gained the C# VisitTryCatch /
  HandleTryBlock pair: handler bodies walked with an empty input state
  fragmented same-handler variables in SplitVariables (the exception
  temp + the state field), whose store-less loads were then deleted as
  dead code.
- The container topological sort counts the positional fall-through as
  a successor edge (the C# ILAst materializes every terminator as an
  explicit branch, so its branch-only walk is complete; this port's
  reader leaves the fall-through implicit in the FinalInstruction
  slot, and a fall-through-only-reachable block was deleted as
  unreachable).
- ILVariable gained the C# StoreInstructions list (populated by
  ComputeVariableUsage like AddressInstructions) for the C#
  StackSlotValue single-definition stack-slot resolution.
- A call whose Method handle is null (out-of-module, the deferred-
  resolution convention) synthesizes a FakeMethod from the reader's
  call surfaces for the Await's GetAwaiter/GetResult methods (the
  CreateDynamicAwaiterMethod precedent).
- The transferred MoveNext try block re-parents to the function (the
  driver's temporary decompiler dies with the statement; a parent
  chain left pointing into its tree dangled and crashed the loop-
  detection CFG's function-exit test).

Deferred (documented at the Run branch + the slice-state header):
NormalizeAwaitOnCompletedDualBranch + the dynamic-callsite arms
(DynamicCallSiteTransform, CoalesceDynamicAwaiterBlocks, the dynamic
GetAwaiter/GetResult sites), the async-enumerator arms
(MatchAsyncEnumeratorCreationPattern, AnalyzeYieldReturn,
TransformYieldBreak, SimplifyIfDisposeMode, the real AnalyzeDisposeAsync),
the Visual Basic arms, the pre-roslyn stack save/restore, the
cachedFieldToParameterMap capture, and the AsyncDebugInfo map (no
ILFunction surface). 8 gtest cases
(MatchesTask/TaskOfT/VoidCreationOverRealBody,
InlinesMoveNextBodyAndMarksAsync, TaskOfTUnderlyingReturnTypeIsTheElement,
DetectsAwaitOverRealBody, DriverSlotConvertsTheRealBody,
RejectsNonAsyncMethod); the sweep holds the baseline failure set and
the connid hash is unchanged.

### DynamicCallSiteTransform (parts 1-3, commits 615a00b8c/850f6be68/860d6655c)

The full port of `ICSharpCode.Decompiler/IL/Transforms/
DynamicCallSiteTransform.cs` + `Instructions/DynamicInstructions.cs`:
the 11 dynamic nodes (binary/unary/convert/get-member/set-member/
is-event; the DynamicArgumentsInstruction base over
get/set-index/invoke/invoke-member/invoke-constructor), the 85-member
ExpressionType enum, the CSharpArgumentInfo/BinderFlags structs, the
dumps (the C# dotted mnemonics + the flag suffixes), and the transform
(FindDynamicCallSitesInBlock, MatchCallSiteCacheNullCheck, the 11
binder-arm scan, ExtractArgumentInfo, MakeDynamicInstruction). Driver
slot after the second CFS, before SwitchDetection (the C# order), plus
RunOnBasicBlock (the per-block static entry AsyncAwaitDecompiler's
AnalyzeStateMachine calls over the MoveNext blocks).

The port's deferred-resolution toolbox this slice established (the
conventions every later C# list-read consults):
- The resolver resolves `LdsFlda::Field` too (LdFlda was already there).
- MatchGetTypeFromHandle falls back to the reader's
  "System.Type::GetTypeFromHandle" surface when Method is null.
- ReadBinderValue, the dual-form helper: the port's inliner folds
  single-use scalar stores across instructions (the C# only inlines
  into the immediately-following instruction), so binder scalar args
  read the inlined value directly while array args keep the stloc chain.
- HandleSimpleArrayInitializer gained the copy-store arm
  (stloc copy(ldloc array); stobj(ldelema(ldloc copy), value) -- the
  reader's stack-slot materialization of the array reference).

Fixture: `/home/jim/ilspy-test-fixtures/dynamic_fixture/DynamicFixture.dll`
(built over the SHARED-FRAMEWORK impl assemblies, NOT the ref pack --
the ref-pack Microsoft.CSharp lacks the compiler-required Binder
members; the csc line is in the test file's fixture comment).
19 gtest cases across the two suites.

### IntroduceRefReadOnlyModifierOnLocals (commit c043521c5)

The 71-line transform: a by-ref local is marked ref-readonly when a
store's value is IsReadonlyReference-shaped or a readonly. ldelema.
`LdElema::IsReadOnly` + the reader's `readonly.` IL prefix decode
(ReaderState::pendingReadOnlyPrefix, consumed by the next ldelema --
the opcode was skipped with the semantics lost). Driver slot after
SwitchOnNullable, before LoopDetection. 5 hand-built tests (the
0-firing-on-corpus precedent: Roslyn rarely emits readonly. ldelema in
the corpus -- net48 is reference assemblies with no bodies).

### The AST-pipeline slice (commits a33da10d9/2614fed27/28759bd01)

The facade now routes type bodies through the real pipeline end-to-end:
- **The render arms** (`a33da10d9`): ILAstToCSharp renders the
  Await/YieldReturn/dynamic nodes (the statement forms + the expression
  forms; the ExpressionType -> C# operator symbol table; the
  DynamicConvert explicit/implicit split). The IndexRangeTransform
  crash this exposed: the C# reads the block list at a walked position
  (terminators included); the port carries them in the FinalInstruction
  slot, so the positional reads went through the BlockInstructionAt
  helper (the established convention).
- **The type-system wiring** (`2614fed27`): the static/instance
  DecompileTypeToString entries thread the DecompilerTypeSystem they
  already build through DecompileTypeToStringBody into a
  type-system-carrying DecompileMethodToString overload; the bare
  per-method static builds its own (the C# static shape). The facade
  also resolves `ILFunction::Method` (ReadIL leaves it null; the
  matchers read the return type through it) and keeps the shared_ptr
  alive past the render. Two more crashes the wired path surfaced:
  (a) InlineBodyOfMoveNext registered the inlined body's variables
  through NON-OWNING aliasing shared_ptrs -- the state machine function
  (the other owner) dies with the temporary decompiler, and the later
  inlining passes delete the body's loads/stores, so the function's
  variable list held dangling aliases; the registration now takes
  owning copies (the nodes' shared_ptr members) and the result-variable
  load takes the owning handle. (b) MatchEnumeratorCreationPattern lost
  the C# empty-list bail (the port's list excludes the final slot, so
  a non-return final fell through to the multi-instruction path and
  indexed an empty vector).
- **The hidden state machine types** (`28759bd01`): the C# MemberIsHidden
  skips the compiler-generated state machine/enumerator types the
  de-sugar replaces; the type-level body now consults the same
  CodeMappingInfo predicates and returns an empty render when the
  transform gates are on. The closure/local-function/anonymous-type
  arms stay deferred (no transforms for them yet).

`--csharp` over the async/iterator/dynamic fixtures now renders
`await ...`, `yield return ...`, and `d.Foo` / `d.Bar(i)` end-to-end,
without the `<T>d__` state machine classes. 18 gtest cases in
`tests/Decompiler/CSharp/ILAstToCSharpAsyncDynamicArms_Test.cpp` (the
hand-built render arms + the three facade end-to-ends + the
hidden-state-machine whole-module pair). The sweep baseline moved to
13016 ran / same failure set; the connid hash is unchanged.

### The facade member/type modifiers (commits fb66d3a22/7f46ba335 + 878..)

(plus the implicit-base-call elision: a constructor's no-argument base
call renders nothing -- the C# constructor-initializer convention; the
`base();` line was the seed's render of the decoded base .ctor call.)

The type-level render's declaration surfaces, composed from the ported
TypeSystemAstBuilder pieces (GetMemberModifiers /
ModifierFromAccessibility / ConvertField's const-readonly-volatile bits)
over the module entities (GetDefinitionField/Method/Property/Event):
- **The member modifiers** (`fb66d3a22`): the declaration keywords in
  the AllModifiers order; GetMemberModifiers already handles the
  accessibility suppression (explicit interface implementations, static
  constructors, interface members) and the virtual family
  (virtual/override/sealed override/abstract). The body-less method arm
  (the C# DoDecompileMethod no-body path): abstract and interface
  members render as declarations (`public abstract int M();`) instead
  of vanishing (RVA-less methods were skipped outright); a body-less
  non-abstract non-interface member takes extern.
- **The type modifiers + base types** (`7f46ba335`): the accessibility,
  the static/abstract/sealed chain, the kind adjustments (struct/enum
  drop sealed; interface drops abstract; readonly struct gains
  readonly), partial only for a registered partial half (the C#
  partialTypeInfo != null arm -- the unconditional partial stand-in is
  gone), and the base-type list (DirectBaseTypes minus Object, minus
  the struct's ValueType, the enum's Enum replaced by the underlying
  type when not int). The C#'s BaseTypeAccessibleFrom filter stays
  deferred with the MemberLookup resolver surface.

### The render-fidelity batch (commits a71543577/7ebc3b816/dbd408fa4)

The corpus-diff findings, one commit each:
- **The enum keyword + builtin keywords** (`a71543577`): the type-header
  switch's enum arm rendered `struct` (the stand-in) -- the corpus's
  `internal enum CS : uint` came out `internal struct CS : UInt32`; the
  arm now renders `enum`. The builtin keywords missed the
  entity-resolved types (System.UInt32 resolves to a SimpleType, not a
  KnownType) -- the name-based arm in CSharpTypeName maps the primitive
  reflection names to their keyword forms. The unnameable-base-list
  filter's internal check also over-dropped over the corpus: ISealable
  is internal to WindowsBase but PresentationFramework names it through
  [InternalsVisibleTo] -- the check now uses the ported
  IModule::InternalsVisibleTo (the same-assembly arm plus the friend
  list). The PresentationFramework base-list diffs drop 125 -> 79 (the
  remainder are the resolver-based name qualification -- a different gap
  family).
- **The enum member list** (`7ebc3b816`): the enum's const fields
  render as the C# enum member list (the display-mode analysis:
  consecutive-from-zero bare names, first-only for a non-zero start,
  all / all-hex otherwise; value__ never renders). The literal suffix
  spellings match the C# TextWriterTokenWriter (uint "u", ulong "uL").
- **The expression-bodied getter-only property** (`dbd408fa4`): the
  single-return getter folds to `=> <expr>;` (the
  NormalizeBlockStatements.SimplifyPropertyDeclaration shape);
  multi-statement bodies keep the block form.

### The namespace-declaration slices (commits 0096c19e0/328c65e54)

- **The whole-module namespace grouping** (`0096c19e0`): the C#
  DoDecompileTypes NamespaceDeclaration emission -- consecutive
  same-namespace types nest under one `namespace X { }` block, the
  empty namespace renders at the root, a hidden type does not break the
  group (the hidden check extracts into TypeIsHiddenFromRender, shared
  with the type-level body). The earlier flat render diverged from the
  oracle from the first type onward.
- **The single-type namespace header** (`328c65e54`): the -t render
  carries the file-scoped `namespace X;` header (the oracle's single-
  type form); the whole-module paths pass wrapNamespace=false.

The NESTED-TYPE placement remains the known divergence: the oracle
nests nested types inside their declaring type's braces (the
`public sealed class Derived` inside ModifierShapes); the flat render
emits them as top-level classes in the empty-namespace root group.
That rides with the member-iteration restructuring (the C# DoDecompile
type's NestedTypes concat leads the member list).

### The single-type render completion (commits 1acdb59d8/676aac6bd)

- **The nested-type placement** (`1acdb59d8`): the nested types render
  inside their declaring type's braces (the C# DoDecompile's member
  order -- the NestedTypes concat LEADS the member list) through the
  recursive body entry; the hidden state machine types skip there too;
  the whole-module loops skip nested types (they render once, within
  their parent). The partial-type consult became a lookup callback so
  each nested type resolves its own partial info. The single-type
  namespace header walks the declaring chain (the -t render of
  Derived carries `namespace ModifierFixture;`).
- **The required using directives on the -t render** (`676aac6bd`): the
  minimal using set over the new CollectRequiredNamespaces walk (the
  entity walk without the known-type candidate seeding, the implicit
  bases, the never-rendering enum members, the stripped attribute
  families, and the type-reference base sweep -- the C#'s candidate
  pool + resolver filtering approximated by restricting the walk to
  what the render names). The modifier fixture matches the oracle
  exactly (`using System;` on ModifierShapes, none on Color).

### The body-name simplification slices (commits c1f999948/6e0cfcf72)

The corpus's body-level qualification family (~540 lines), two
commits:
- **The own-type static member accesses render unqualified**
  (`c1f999948`): the emitter derives the current function's declaring
  type from ILFunction.Method (a null Method keeps the qualified
  render) and simplifies the flattened member name when its type
  prefix matches -- the static field loads/stores, the method groups
  (LdFtn), and the static calls. The nested-type '+' separator
  normalizes; the inherited-static case (a base's member named from a
  derived type) stays with the name-qualification family.
- **The new-expression types render short** (`6e0cfcf72`): the newobj
  arm takes the flattened name's last segment (`new
  RoutedEventHandler(...)`); the collision qualification stays with
  the resolver family.

The REMAINING name-qualification family (the resolver surface): the
base-list collisions (79 corpus diffs -- the oracle qualifies
`System.Collections.IEnumerable` when the short name is ambiguous),
the nested-type short forms in base lists (`TemplateContent+Frame` ->
`Frame` inside the parent), and the `+=` operator rewrite for the
event add/remove calls (ReplaceMethodCallsWithOperators -- the flat
renderer renders `add_Click(...)` where the oracle renders `.Click +=`).

### The operator-rename and using-header slices (commits 0a58a7c83/79ad77a2f)

- **The event add/remove compound assignments** (`0a58a7c83`): the
  compiler-generated event accessors render `recv.Click += handler`
  (the ReplaceMethodCallsWithOperators event arm); the delegate
  construction (a newobj over the (target, method-group) pair -- only
  delegate ctors take a native function pointer second) folds the
  target away, and the event-handler position folds the whole
  new-expression to the bare method group. The connid's Page1 matches
  the oracle's forms.
- **The whole-module using set** (`79ad77a2f`): the header builds from
  the per-type required-namespace walks -- a namespace emits iff some
  type OUTSIDE it references it (a name inside `namespace N { }`
  resolves without a using for N) plus the attribute namespaces. The
  per-block resolver filtering rides with the resolver surface; the
  approximation over-collects two namespaces on the connid (documented
  in the commit).

THE REMAINING NAME-QUALIFICATION WORK, refined: the -t single-type
renders already match the oracle (no qualification -- the per-type
using scopes do not collide); the whole-module base-list
qualification (the oracle's `System.Collections.IEnumerable` when
System.Collections.Generic is in the file's using set) needs the
per-block scope + the name-collision check over the FILE's using set
(the resolver's LookupSimpleNameOrTypeName over a UsingScope -- the
ported resolver surface exists; the wiring is the work).

### The name-decision family (RESOLVED -- four commits this cycle)

The C# rule (confirmed from the source): the TypeSystemAstBuilder's
short-name decision resolves the name through the USING SCOPE --
`resolver.LookupSimpleNameOrTypeName(name, args, mode)`; a non-error
TypeResolveResult MATCHING the intended type keeps the short name, an
error result (Unknown OR Ambiguous) or a mismatch qualifies.
Probe: `/home/jim/ilspy-test-fixtures/ambiguous_fixture/
probe_ambiguity.cpp` (the build recipe: the standard probe link line).

The port-side mechanism (the probe reproduces it): over the PF
whole-module emitted set (98 namespaces), the `IEnumerable` lookup
returns **AmbiguousTypeResolveResult** -- the collision pair is
`System.Collections.IEnumerable` +
`System.Runtime.InteropServices.ComTypes.IEnumerable` -- BOTH in the
net48 mscorlib itself (the legacy .NET Framework duplicate; 48
ComTypes-hits in the file). Two hard requirements the probe found:
  * The using namespaces MUST resolve through
    `module.Compilation().RootNamespace()` (the MERGED tree) --
    `module.RootNamespace()` (the module's own types only) does NOT
    resolve System.Collections at all.
  * The scope threads through `ctx->WithUsingScope(scope)` (the
    context's immutable-With pattern; a direct member assignment
    writes a copy).

THE SUBTLETY (resolved): the net48 mscorlib's ComTypes.IEnumerable
duplicate is INTERNAL, and the C# using-scope lookup's
TopLevelTypeDefinitionIsAccessible gate only lets it participate when
mscorlib's InternalsVisibleTo friend list covers the consuming
assembly. The friend list includes PresentationFramework -- which is
why the PF corpus qualifies while any small fixture does not. The RED
fixture (`/home/jim/ilspy-test-fixtures/ambiguous_fixture/`) builds
under the name PresentationFramework.dll for exactly that trigger
(mscorlib+System copied beside it so the sibling-directory resolution
finds the duplicate carrier).

THE SLICES (all landed, all corpus-verified against the oracle):
  * `5bb3fdf8d` -- the base-list qualification: the render's using set
    (the type's own on -t, the module-wide on the whole-module path --
    one DecompileRun per the C# flow) threads into a UsingScope
    resolved through the COMPILATION root (the merged tree; the
    module's own root carries only its own types), nested along the
    current type's namespace chain, carrying the current type
    definition into the CSharpResolver. The whole-module using set
    computes ONCE and feeds both the header and the scopes (the
    per-type CollectRequiredNamespaces walk over every type is the
    render's most expensive single pass -- a second copy doubled the
    corpus render to ~8 minutes). BOUND: a lookup that finds NOTHING
    keeps the short name for a TOP-LEVEL name (the port's compilation
    loads a subset of the C#'s modules -- the netcore runtime-pack
    discovery is unported -- so a miss is typically a resolution gap,
    not real information); a NESTED miss is real (nested types are not
    namespace members) and falls to the dotted form.
  * `0b5c7ff80` -- the nested spelling: the declaring type renders
    through the same decision joined by '.' (the sibling nested types
    resolve by their own name -- the enclosing type's members are in
    the lookup scope); the reflection '+' spelling is gone.
  * `5f245055c` -- the generic <...> tail through the same decision +
    the type-declaration header's bare name + the declared parameter
    list (<TItem>, never `1`) + the keyword spellings (uint, ...)
    preceding every name decision.
  * `201d0cca0` -- the type-parameter constraint clauses (where T :
    class/struct/new()/type-constraints) on the declaration line.
CORPUS RESULT: the PresentationFramework base-list diff went 79 -> 0
(the whole base-declaration category -- modifiers, base types,
qualification, nested spelling, arguments, constraints -- is
oracle-exact). The connid pin `aa0c7056...` held through every slice.

THE MEMBER-SIGNATURE SLICES (landed after the base-list family):
  * `bdd7be98a` -- the member-signature qualification: the signature
    arms (the return/param/field/property/event types + the
    explicit-impl interface names) render through RenderBaseTypeName
    with the type-level scopeResolver. The name decision needs the
    DEFINITIONS, so the arms prefer the ENTITY path (the
    method/property/field entities' resolved return types +
    parameters, the no-op-deleter ITypePtr aliasing) with the
    file-signature decode as the fallback (the decode yields
    unresolved simple types). The method arm also gained the
    body-decode-failure fallback: a reference assembly's stale RVAs
    never decode, and the members previously VANISHED (the
    DecompileMethodToString failure had no else arm); they now render
    as declarations with the reference-assembly empty-body comment.
    Corpus: 51 -> 241 qualified rows of the oracle's 321 code rows.
  * `928819a5d` -- the .override forwarder synthesis (the C#
    AddInterfaceImplHelpers): a plain-named method with a MethodImpl
    row binding it to an interface contract renders the synthesized
    explicit-impl forwarder (the member's return type + params, the
    interface through the name decision, the generated comment, the
    forwarding call). The RED fixture is crafted (the MetadataBuilder
    recipe at /home/jim/tmp-build/overrideprobe/ -- the
    /home/jim/ilspy-test-fixtures/override_fixture/OverrideSynth.dll;
    the MethodList ranges must point at the right rows or the types
    leak each other's methods).

THE WORKLIST + PARAMETER SLICES (landed after the member-signature
batch):
  * `88232828d` -- the worklist: a hidden compiler-generated type whose
    declaring type's rendered members still reference it (the
    state-machine attribute's typeof) renders at its nested position.
    The reference discovery walks the member entities' attribute typeof
    arguments (the any-held ITypePtr), MIRRORING the rendered text: a
    state-machine attribute counts only when its de-sugar did NOT
    succeed (the collection consults the method pipeline's outcomes
    once per state-machine-attributed method -- the regression the
    async fixture caught: its de-sugared methods' metadata still
    carries the attribute, but the rendered declaration dropped it).
    The hidden check moved from the render body to its callers (the C#
    DoDecompileType has none of its own) so the nested-types loop can
    admit the worklist types. Corpus: the 17 .override forwarders now
    match the oracle exactly.
  * `fa9b68ee2` -- the ref/out/in/params parameter modifiers (the
    entity-path builder renders them; the file-decode fallback carries
    no reference kinds).
  * `08b0eb6f4` -- the optional parameters' default values (the
    trailing-optional rule + the shared ConstantValueText with the
    float/double forms).
Corpus position: the member-signature comparison reached a SINGLE
remaining row after the next three slices:
  * `6d20af3f3` -- the outer-parameter skip: the port's TypeParameters
    surface is the CHAIN-MERGED list (the declaring type's parameters
    first, then the own -- probe it before assuming the C#'s own-list
    semantics), so the header's own slice starts at the declaring
    type's count; the state machines reference the enclosing generic's
    parameters without re-declaring any, so they render no list.
  * `ceed4b695` -- the delegate shape: `delegate Ret Name(params);`
    over GetDelegateInvokeMethod (the free function in the TypeSystem
    namespace), the sealed strip BEFORE the modifier emission, the
    return type unconditionally (the keyword map gained
    System.Void -> void), the early return skipping the base list +
    the member arms + the brace. The connid pin RE-BASED
    deliberately: `074375839ad0...` (the RoutedEventHandler stub now
    matches the oracle's one-line form byte-for-byte, the known
    whole-module indentation divergence aside).
  * (this commit) -- the indexer `this[params]` form + the
    decode-failed accessors' reference-assembly blocks (the accessor
    helper previously swallowed the failure into the stub form).
The last row CLOSED (`bfb458db8`): the attribute typeof argument over
a type nested in a GENERIC declaring type renders `RBTree<>.<...>d__39`
(the compiler's TypeSpec form -- CS0416 keeps every source form out;
the ca-blob decode resolves to the bare definition in the port, so the
typeof render rebuilds the unbound markers manually -- an empty
type-argument child per declaring parameter, the MemberType
composition bypassing the attribute builder's
AlwaysUseShortTypeNames short path). **The member-signature and
base-declaration corpus categories now have ZERO differing rows.**

THE XML DOCUMENTATION FEATURE -- LANDED (two slices, the category
CLOSED at zero differing doc lines on the corpus, from 16,108):
  * `f56c0ec37` -- the provider + the type docs:
    `Documentation/XmlDocumentationProvider.{hpp,cpp}` (the adjacent
    `<assembly>.xml` scan, the `<member name=...>` content captured
    verbatim -- the C# ReadInnerXml shape; the port loads the whole
    file into a map where the C# streams with an index, the bounded
    divergence) + the T: IDs + the doc-line renderer (the
    InsertXmlDocumentation lift: the first non-empty line's
    indentation, the trailing empty lines dropped, the between-empty
    lines as bare `///`).
  * `bd399a97a` -- the member IDs: the M:/P:/F:/E: forms (the dotted
    declaring chain, the escaped names, the `` ``N `` generic counts,
    the doc type-name parameter encodings -- the byref '@', the arrays,
    the generic-parameter markers, the instantiation's brace
    distribution; the '~ReturnType' conversion suffix), the enum
    member list's per-member F: docs, the constructors' '#ctor', and
    the nint/nuint alias-to-full-name mapping (the doc IDs spell
    System.IntPtr where the port's resolution renders the alias). The
    indentation strip is CONDITIONAL (the C# StartsWith check -- an
    unconditional strip chopped the wrapped continuations mid-word).
THE REMAINING CORPUS GAP (after the docs): 2,160 oracle-only / 2,099
mine-only normalized lines. The event explicit-implementation family
CLOSED (`25826ab44` -- the interface-qualified name + the add/remove
accessor blocks, with the empty-body brace-overlap fix in
AccessorBodyText). The member-decl families that remain:
  * THE NATIVE-INTEGER SPELLING -- CLOSED (`5bd74eadd`): the port's
    type-system Default carried NativeIntegersWithoutAttribute (the
    flag the C# sets only under DecompilerSettings.NumericIntPtr,
    which the language-version gate turns off below C# 11). The
    oracle's empirical boundary: the modern .NET targets render every
    IntPtr as nint, the .NETFramework family the full name -- the TFM
    approximates it in NativeIntegerOptionsFor (the .NETFramework/
    Silverlight/.NETPortable families keep the flag OFF), applied at
    EVERY facade type-system construction (the static entries, the
    method-body entry, AND the instance's state_->typeSystem.emplace
    -- the last is easy to miss; the connid pin catches it). Corpus:
    2160/2099 -> 1650/1589 normalized rows.
  * The [FLAGS] enum display mode -- CLOSED (`eba37b718`): the
    [Flags] check at the analysis's head (the C# returns early; the
    walk's out-of-order fallback must not overwrite it) + the hex
    literal's underlying-type suffix preservation (the suffix is the
    decimal literal's part after the LEADING digits -- `0x10u`).
    Corpus: 1650/1589 -> 1402/1343.
  * THE PARAMETER ATTRIBUTES + THE UNSAFE POINTER SIGNATURES -- CLOSED
    (`e2f6b9d2e`-era): (1) the [In]/[Out]/[MarshalAs] parameter
    attributes render as the compact bracket-adjacent sections before
    the modifiers (ParameterAttributesText, forward-declared in the
    early anon namespace -- the use at the top of the file); (2) the
    `unsafe` modifier on pointer signatures (TypeContainsPointer/
    MemberSignatureHasPointer -- the method's return/params, the
    field, the delegate's Invoke; the IntroduceUnsafeModifier
    transform only runs over the AST pipeline, so the empty-body
    reference members need the declaration-level rule); (3) the
    composite types (pointer/byref/array) recurse on the element
    through the name decision in RenderBaseTypeName -- a sibling
    nested type inside a pointer renders `FSPOINT*` (the C# wraps the
    CONVERTED element); (4) the unresolved nested references render
    the dotted form (`+`->`.`), not the reflection spelling. Corpus:
    1219/1156 -> 791/744.
  * THE OPERATOR DECLARATIONS -- CLOSED (`69f41692a`): the op_*
    special names render as `operator !=(...)` / `implicit operator
    RetType(...)` -- the rewrite on the shared methodName/returnType
    pair after the signature computation (every render arm composes
    `returnType + " " + methodName`; the conversions pass the
    implicit/explicit keyword in the return-type slot). Corpus:
    791/744 -> 717/670.
  * THE NEW MODIFIER -- CLOSED (`bbec91e22`): the C# SetNewModifier
    port (MemberHidesBaseMember -- the non-interface base-type walk,
    the methods by signature, the others by name; the accessibility
    through the ported MemberLookup). THE GATE IS THE CRUX: the walk
    runs only when the method's Virtual flag equals its NewSlot flag
    (the plain methods + the new-slot virtuals); a
    Virtual-without-NewSlot (the first declaration OR the override --
    both the C# IMethod.IsOverride) NEVER takes `new`. Without the
    gate the corpus BLEW UP (+1000 spurious `new override` rows). The
    properties/events gate on their accessor; the fields/types walk
    unconditionally. Corpus: 717/670 -> 636/653.
  * THE EXPLICIT-IMPL INTERFACE INSTANTIATION -- CLOSED (`031c7054d`):
    the explicit-implementation names render the interface WITH its
    type arguments (`IEnumerator<XmlNamespaceMapping>.Current`) -- the
    MethodImpl row resolves the definition form; the constructed form
    comes from the class's own DIRECT-base list (a RECURSIVE walk was
    tried and rejected: it finds the DEFINITION's own unsubstituted
    base, `IEnumerable<T>` from IList`1). The already-parameterized
    resolved declaring types (the inherited shapes, `ICollection<Uri>`)
    pass through. Corpus: 621/652 -> 604/639.
  * THE DESTRUCTORS -- CLOSED (`9d1be5467`): the Finalize overrides
    render `~TypeName()` (no modifiers, no return type; the emitter
    shares the constructor's no-return form).
  * THE NULLABLE SHORTHAND -- CLOSED (`126903e67`): the Nullable<T>
    value types render `T?` (the AstBuilder's nullable specifier; the
    argument recurses through the same name decision). 56 rows.
  * THE EXTENSION + GENERIC METHOD FORMS -- CLOSED (`ce3c281f3`): the
    `this` on the first parameter (the entity's IsExtensionMethod, a
    new defaulted MethodDeclString parameter), the method's own
    type-parameter list after the name (the chain-merged surface
    sliced at the declaring type's chain size), and the method-level
    `where` clauses through the extracted ConstraintClausesText (the
    shared skip rule; A HAZARD: the clause renders AFTER the closing
    paren -- an arm wrote it inside the parens first). Corpus:
    604/639 -> 497/532 across the two commits.
  * THE ACCESSOR-BLOCK FORMS -- CLOSED (`84fe2b30e` + the test fix):
    the automatic forms (`{ get; set; }` / `event X Y;`) render only
    when the accessors carry NO bodies or decompile to the recognized
    pattern; the reference assemblies' stale-RVA accessors render the
    add/remove/get/set blocks with the empty-body comments. THE
    PROPERTY PATTERN: the backing field + the TRIMMED rendered bodies
    (`return <X>k__BackingField;` / `<X>k__BackingField = value;` --
    the body text carries leading indentation!). THE EVENT PATTERN:
    both accessors [CompilerGenerated] + the Delegate.Combine/Remove
    text (the connid's real events -- its pin broke TWICE during the
    slice, both pattern regressions). The property blocks render the
    accessor attributes; the event blocks do not. The stale test
    expectation (the field form) was updated to the block form.
    Corpus: 486/521 -> 274/309 (212 rows, the biggest family yet).
  * THE ENUM DISPLAY-MODE ABORT -- CLOSED (`b09d311ae`): the C#'s
    per-member early abort (neither consecutive nor all powers of two
    -> the All mode) -- the walk only aborted on out-of-order before.
    101 rows.
  * THE ALIASED ENUM MEMBERS -- CLOSED (`6cfc4313d`): the C#
    ConvertEnumValue direct-match arm (an EARLIER same-value member's
    name; the [Flags] single-bit-only rule; the AllHex skip for
    aliases -- the first form concatenated the hex digits with the
    alias). 47 rows. Corpus: 274/309 -> 126/165 across the two.
  * THE [FLAGS] UNION/COMPLEMENT -- CLOSED (`9c411ca86`): the C#
    ConvertEnumValue flags arms (the union `A | B` over the earlier
    single-bit members by weight descending; the complement `~X`; the
    multi-bit-within-mask numeric rule). THE MASK CRUX: the negated
    value masks to the underlying range for byte/short/int only -- a
    64-bit underlying type keeps the full complement (its high bits
    never clear -- masking to 32 bits wrongly completed a 30-term
    bogus complement on the oracle's long-based enum). Corpus:
    126/165 -> 101/141.
  * THE QUALIFIED CONST-FIELD ENUM FORMS + THE SPECIAL CONSTANTS --
    CLOSED (`<next>`): the enum-typed const fields render the qualified
    member/composition/cast forms (EnumConstantFieldExpression --
    declaringEnumMember=null in the C#: no row rule, `XmlToken.Left`);
    the integral boundaries render the specialConstants names
    (`uint.MaxValue`, `int.MaxValue`). Corpus: 101/141 -> 74/114.
## THE MEMBER-DECL RENDER EFFORT: COMPLETE (`fd1310339`)

The corpus-verified member-declaration render families are all closed.
The net48 PresentationFramework corpus (16,108 normalized lines at the
start of this effort) stands at 41 oracle-only / 32 mine-only rows --
99.5% parity, every remaining row in the recorded residual list below.

The final full-gate sequence (sweep38): 13,163 tests ran / 32 baseline
failures (the pre-existing exclusion-list set, unchanged); the facade
suites 45/45; the connid render pin
`074375839ad052f67ee438d6d4dc02a9f7723fc307652daf08` stable through
every family closure.

The residual rows (41 oracle-only / 32 mine-only, the recorded
follow-ups): the `[return: MarshalAs(...)]` interop return-attribute
family (~15 rows -- the COM interop interfaces' return types); the
`ENABLED = ~DOES_NOT_EXIST` complement shapes the C# produces through
its negated-loop accumulation over multi-bit members; the
`Contract.Requires<ArgumentNullException>(...)` body lines (the
reference-contract calls inside decompiled bodies); the `base..ctor()`
/ `_id = Guid.NewGuid();` constructor-initializer body rows; the
`new void SetValue(...)` new-modifier misses on cross-module base
members; the `/*Error: End of method reached without returning.*/`
error-comment variant; the misc near-miss parameter pairs.

## THE PORT COMPLETION REPORT (the milestone)

The C++ ILSpy port is functionally complete for the decompilation
corpus its CI guards. The arc, in phases:

1. THE ENGINE (the earlier handoffs): the metadata reader, the type
   system, the IL reader, the ILAst pipeline (the async/iterator/yield/
   dynamic transforms), and the statement/assignment synthesis --
   closed with the IL-view corpus at 286/286 and the decompiler test
   families green.
2. THE C# RENDER (this arc, ~60 commits): the facade's whole-module
   type/member declaration renderer, driven RED-first against two
   pinned oracles -- the whole-module PresentationFramework net48
   corpus (43,644 normalized oracle lines) and the per-change connid
   render pin. The families closed in order: the modifiers, the base
   lists, the qualification machinery (the UsingScope resolution
   through the compilation root), the `.override` forwarders, the
   worklist, the parameter modifiers/defaults, the XML documentation
   (16,108 doc lines to zero), the native-integer TFM split, the
   [Flags] enum display modes with the alias/union/complement forms,
   the operator/destructor/generic/extension declarations, the
   automatic-vs-accessor-block forms, and the long tail (the nullable
   shorthand, the special constants, the array-initializer backing).
3. THE FINAL GATES (this turn, all green on one build):
   - The facade suites: FacadeMemberModifiersTest 45/45.
   - The connid render pin:
     074375839ad052f67ee438d6d4dc02a9f7723fc307652daf08.
   - The full sweep: 13,163 tests ran / 32 baseline failures (the
     pre-existing exclusion-list set, unchanged through the arc).
   - The corpus: 40 oracle-only / 31 mine-only of 43,644 lines --
     99.9% line parity.

The deferred items (the recorded follow-ups, none blocking):
- THE TRIAGE PAIR (from the baml loader-registry slice, pre-existing
  on bare HEAD): MetadataNamespaceTest.ChildCacheIsStable (a vector
  OOB abort, ILSPY_TEST_MSCORLIB-gated) and
  SpecializeTest.FieldCreateArms (a segv).
- THE RESIDUAL CORPUS ROWS (40/31): the [return: MarshalAs] interop
  family (~11), the multi-bit complement shapes, the
  reference-contract body lines, the constructor-initializer rows,
  the anonymous-type ctor spellings, the cross-module new-modifier
  misses.
- GetOptions (user-deferred) and the recorded stale-queue catches
  (ProxyCallReplacer, TransformFieldAndConstructorInitializers,
  DetectExitPoints -- all landed via the master merge instead).

What the port enables: a self-contained C++ decompiler (libilspy) +
CLI (ilspy_cli) that renders whole real-world assemblies at
oracle-parity, guarded by a 13k-test sweep, two pinned corpora, and a
per-change render pin -- the foundation for the remaining ILSpyX
surfaces (the search/analysis APIs) and the plugin host.

## THE MSCORLIB GATING COMPLETE + THE PIN/MERGE STATE NOTE

The mscorlib-loading test family is fully availability-gated now
(MetadataModule_Test, Specialize_Test, MetadataTypeDefinition_Test,
TypeProvider_Test -- the REQUIRE_MSCORLIB macro after every fixture
construction; GTEST_SKIP only works from a void test body -- it
expands to a void return, so the value-returning fixture helpers
cannot carry it). THE VERIFICATION: the FULL suite (no exclusion
filter) with ILSPY_TEST_MSCORLIB unset completes with no crash signal
(13,453 tests, the 267 assertion failures being the by-design excluded
set); the canonical excluded run: 13,166 ran / 15-20 failed (the
pre-existing baseline; the count varies with the mscorlib family
skipping), no crash.

THE CONNID PIN -- RE-ESTABLISHED ON THE MERGED TREE:
dfb728fb5d6b09351fae1c4705ed0bb6bf1c2cd2e9ea039bff08075c64aa8cbf
(the old 074375839ad0... was the pre-tab-migration space-indented
render; the port-baml merge d0850c67c + the tab migration 3ce63a4ab
changed the shape between turns). THE CONNID CONTENT AUDIT (the
normalized set-diff against a fresh oracle render -- the old and new
oracle renders are byte-identical as sets, so the C# side and the
fixture DLL are stable; 31 mine-only / 9 oracle-only normalized
lines) -- SIX DIVERGENCE CATEGORIES, each a queued slice:

(a) THE UNSIMPLIFIED SWITCH BODY: the Page1.Connect method renders
    the raw `goto IL_0018` labels + `default:` (the control-flow
    simplification did not reduce the switch to the break-structured
    form the oracle renders);
(b) CLOSED (the operand parenthesizes only for the non-atomic forms;
    a cast receiver wraps itself in the call, property, and event
    paths) + (c) CLOSED (the bool-store literal: an int constant to a
    bool field renders true/false) -- `95fd01de7`, the pin at
    84ee064e..., the connid normalized diff 29/9 -> 24/4; the corpus
    unchanged (these shapes are decompiled-body forms, absent from
    the reference assembly's empty bodies);
(d) CLOSED (the automatic-event backing field hides, gated on the
    isAutomaticEvent classification; the custom-block events keep
    their fields) -- the pin moved to d6cbe93ef...;
    NOTE: the corpus stands at the MERGED tree's 60/51 baseline (the
    port-baml merge + the tab migration moved it from the pre-merge
    40/31, stash-verified -- the drift is the merge's own render
    changes, the remaining audit items);
(e) THE USING-SET OVER-COLLECTION: `using System.Collections;` +
    `using System.Threading.Tasks;` render where the oracle's set is
    smaller;
(f) THE ASSEMBLY ATTRIBUTE DECODE: `[assembly: Debuggable(/*Could
    not decode attribute arguments.*/)]` (the DebuggableAttribute
    ctor's DebuggingModes enum argument fails to decode);
plus the empty default constructors render where the oracle hides
the compiler-generated ones -- CLOSED (`1ad5f83e9` + the test
inversion): the public parameterless empty-body ctor elides (the
connid 24/4 -> 16/4; the pin at 9c683c94...; the corpus unchanged).
The remaining queue: (f) the Debuggable decode and (e) the using-set
over-collection -- BOTH ROOT-CAUSED (the audit's final two):

(f) THE DEBUGGABLE DECODE -- CLOSED BY THE FIXTURE SET (no code
    change: the port's reference queue ALREADY follows the type
    forwarders -- DecompilerTypeSystem.cpp's ExportedTypes walk): the
    connid's System.Runtime/System.Collections references resolve when
    the runtime DLLs sit beside the fixture, and the forwarder queue
    then loads System.Private.CoreLib through System.Runtime's
    ExportedType rows. With
    /tmp/{System.Runtime,System.Collections,System.Private.CoreLib}.dll
    present (the .NET 10 runtime files), the attribute renders the
    EXACT oracle form: `[assembly:
    Debuggable(DebuggableAttribute.DebuggingModes
    .IgnoreSymbolStoreSequencePoints)]`. THE CONNID IS 3 MINE-ONLY / 0
    ORACLE-ONLY with the reference set present -- every oracle line
    matches. THE PIN (with the reference fixture set):
    e68b2358f6535ddcb3e8394f6de3d78e318ea723b2d6c72fe2a10d36e5796252
    -- MEASURED WITH THE /tmp REFERENCE DLLS PRESENT (the fixture set
    is part of the pin's contract now, like the net48 corpus's own
    directory; the /tmp sweeper can break it -- restore from
    /home/jim/.dotnet/shared/Microsoft.NETCore.App/10.0.12/). Without
    the files the pin is 54895b5b... (the attribute renders the
    gold-pinned error comment).
    (e) CLOSED (`ac9664c96`): the whole-module attribute sweep used the
    RequiredNamespaceCollector's DEFAULT constructor -- it SEEDS the
    known-type namespaces (System, System.Collections, System.Collections
    .Generic, System.Threading.Tasks, System.Numerics -- every assembly
    attribute contributed them; the per-type minimal sweep never did,
    which is why the per-type probes showed nothing). The sweep now
    uses the unseeded minimal collector, and the render names only the
    namespaces the declarations actually reference.

## THE AUDIT ARC COMPLETE: THE CONNID AT FULL PARITY

`ac9664c96` closes the last audit item. THE CONNID RENDER MATCHES THE
ORACLE WITH ZERO NORMALIZED-LINE DIVERGENCE IN EITHER DIRECTION (the
31/9 at the audit's start -> 0/0). THE PIN (with the reference fixture
set -- /tmp/{System.Runtime,System.Collections,
System.Private.CoreLib}.dll from the .NET 10 runtime):
acb64a2cf7506cbc22d4f0dacbbf3ccfd8b14c56b807461032e6aad5adeb7c4e
(the three commits of the arc: a2ad46ced the switch fall-through,
7771054c3 the short static qualification, 95fd01de7 the cast/bool
forms, 1ad5f83e9 the default-ctor elision, 720bce5ee the event backing
field, ac9664c96 the using set -- each pin move deliberate).

THE FINAL GATES (all green on one build): the facade suite holds its
one pre-existing merged-tree failure (the tab-migration test-string
gap); the sweep 13,166 ran / 15 baseline failures; the net48 corpus
39 oracle-only / 34 mine-only (the merged-tree drift 60/51 -> 60/50
-> 39/34 across the using-set fix and the interop-property closure);
the connid 0/0.

THE MERGED-TREE CORPUS DRIFT -- PARTIALLY CLOSED (`593f10185`): the
interop-property family (the accessor attributes -- [MethodImpl],
[SuppressUnmanagedCodeSecurity], the [return: MarshalAs] return-type
sections over the accessor-block form, 37 rows). THE REMAINING
~39/34, SAMPLED AND PROBED (the constraint-clause drop, the turn's
findings): (a) the corpus's `IsNull<T>(T item) where T : class` and
`IsNotNull<T>(T value) where T : class` (the Standard.Assert/Verify
family) render WITHOUT the where clause -- the REGRESSION WINDOW is
between the renders pf_mine_dl.txt (the where present, the
fd1310339 era) and pf_mine_cb.txt (absent, the 95fd01de7 cast/bool
turn -- which touched ONLY ILAstToCSharp.cpp, so the causality is
NOT the facade's constraint code). THE PROBES: a standalone probe
over `DecompilerTypeSystem(file, resolver)` (the default options,
the empty-TFM resolver) reads the SAME methods' T with
HasReferenceTypeConstraint=1 -- but the RENDER's env-gated WHERE-TP
print inside ConstraintClausesText shows no ref=1+hasTC=1 row for
them (the ref=1 rows carry hasTC=0; the hasTC=1 rows carry ref=0)
-- CAVEAT: the WHERE-TP prints MIX the type-header callers with the
method-level callers (both go through ConstraintClausesText), so the
rows are not attributable without adding the caller to the print.
CLOSED (`e2d4f9b18`): the caller-tagged probe showed the constraints
COMPUTE CORRECTLY at the method level (`constraints=[ where T :
class]`) -- the drop was DOWNSTREAM: the decompiled-body arm emits the
emitter's text, which never carried them (the empty-body/stub arms
did). The regression window's emitter-only commit flipped WHICH arm
renders the stale-RVA bodies (they decode to empty bodies with the
error comment through the DECOMPILED arm), exposing the gap. The
constraints now thread through DecompileMethodToString into the
emitter's method header. Corpus: 39/34 -> 22/17 (the constraint
family, 34 rows); the connid pin unchanged.

## THE RESUME STATE (the closing numbers)

- THE CONNID: 0/0 (full parity, the pin acb64a2c... WITH the /tmp
  reference fixture set -- System.Runtime/System.Collections/
  System.Private.CoreLib from the .NET 10 runtime; without them the
  pin is 54895b5b...).
- THE CORPUS: 0 oracle-only / 0 mine-only -- FULL PARITY (the arc: the merged tree
  60/51 -> the using-set fix 60/50 -> the interop properties 39/34 ->
  the constraint clauses 22/17 -> the conversion operators 19/14 ->
  the complement forms 17/13 -> the explicit indexers 13/10:
  `Interface.this[...]` with no modifiers -- the GetExplicitInterfaceType
  rewrite the plain property arm already had) -> the special constants
  on the enum members 12/9 (`uint.MaxValue` over the AllHex path) ->
  the methods' return-type attributes 9/9 (the [return: MarshalAs]
  sections on the stub + empty-body arms, the accessor slice's
  AccessorReturnAttributesText applied to the methods).
  -> the ComImport co-class ctors 9/7 (the .ctor + RVA 0 + the
  TypeAttributes.Import hide).
  THE REMAINING ~9/7 (sampled): the `Contract.Requires<...>` body
  rows (3), the `base..ctor();`/`_id = Guid.NewGuid();` constructor-
  initializer rows, the `public virtual extern IEnumString
  SupportedLanguages` row, the `/*Error: End of method reached
  without returning.*/;` variant, the cross-module `new` misses (2),
  CLOSED THIS TURN: the anonymous-type hides (the <>f__AnonymousType0
  + its member rows -- the C# IsAnonymousType rule: generated name +
  compiler-generated + properties-only getter check).
  THE EXTERN-PROPERTY ATTEMPT WAS REVERTED (the accessor-body
  computation moved before the modifier emission to feed the extern
  gate -- the restructure dropped the properties' modifiers wholesale
  (the corpus blew to 4421/3959; the pin broke); the mechanism is
  unclear and the slice needs a fresh investigation -- the C# rule is
  `!getterHasBody && !setterHasBody && !IsAbstract && declaring kind
  != Interface -> Modifiers.Extern`, the mirror of the method arm's
  memberRendersExtern).
- THE PORT COMPLETION MILESTONE (this turn): the PresentationFramework
  corpus reached FULL PARITY -- 0 oracle-only / 0 mine-only normalized
  lines. The final four rules (committed together, each verified in
  sequence): (1) the DoDecompileProperty extern arm (the RVA-0
  accessor check -- no reorder needed; the first attempt's
  body-text-gate variant was the breakage); (2) the SetNewModifier
  indexer arm (the indexer-vs-indexer signature hide -- Indexer is a
  distinct SymbolKind, so the hide is signature-based); (3) the
  substituted view of a parameterized base type (the class
  ITypeParameter by index through the base's TypeArguments; verified
  empirically against the C# with the synthetic B<T>/D:B<string>
  fixture at /home/jim/tmp-build/hidetest); (4) the late base-ctor
  statement (the no-arg elision only when the call is the first
  statement of the entry block; the ret-less code-contracts bodies put
  the base call after the field stores, and the C# renders the raw
  `base..ctor();` statement form).
  The full gate holds (13,166 ran / 15 baseline, identical failures)
  and the connid pin is unchanged through every step. REMAINING KNOWN
  GAPS (not corpus rows): the mscorlib-env gold family (the net48
  ILSPY_TEST_MSCORLIB gate, gold values pin the mono layout), the
  facade suite's one pre-existing merged-tree failure (the tab
  migration's XmlDocumentationCommentsRenderOnMembers string), and
  GetOptions (user-deferred).
  CLOSED THIS TURN: the `[DefaultMember("Item")]` extra (the
  compiler-generated attribute drops when the type declares a
  non-explicit indexer, `8179a690f`).
- THE GATES: the sweep 13,166 ran / 15 baseline (env -u
  ILSPY_TEST_MSCORLIB); the facade suite 45 with the ONE pre-existing
  merged-tree failure (XmlDocumentationCommentsRenderOnMembers -- the
  tab migration's expected-string gap, recorded as the merge's own);
  the mscorlib-gated families all REQUIRE_MSCORLIB-gated.
- THE FIXTURES: the net48 corpus at
  /home/jim/ilspy-test-fixtures/net48/ (oracle /tmp/pf_oracle.txt, my
  renders /home/jim/tmp-build/pf_*.txt); the connid /tmp/connid_res.dll
  (oracle connid_oracle_new.txt) + ITS reference set in /tmp (the
  sweeper can delete them -- restore from
  /home/jim/.dotnet/shared/Microsoft.NETCore.App/10.0.12/); the
  exclusion list /tmp/excl2.txt.
- THE BUILD: cd cpp && export TMPDIR=/home/jim/tmp-build && export
  PATH=/home/jim/cpp-tools/cmake/bin:/home/jim/cpp-tools/ninja-bin:
  $PATH && ninja -C build/linux-ninja ilspy_tests ilspy_cli.

    (f's HISTORICAL MAP: the connid's references are System.Runtime +
    System.Collections (10.0.0.0) -- a .NETCoreApp shape. With those
    DLLs placed next to the fixture, the RESOLVER finds them and the
    compilation loads them (probed: the modules list grows to 4) --
    but the DebuggableAttribute's definition is NOT IN System.Runtime
    (the runtime's System.Runtime.dll is a FACADE): it lives in
    System.Private.CoreLib through System.Runtime's TYPE FORWARDERS.
    The resolution needs the forwarder-following lookup (the C#
    resolves the chain through its runtime). Two experiments were run
    and REVERTED: (1) defaulting GetUnderlyingEnumType's unresolved
    arm to Int32 decodes the attribute as `(DebuggingModes)2` but the
    C#'s throw is GOLD-PINNED (the decoder tests break); (2) the net48
    mscorlib copy is irrelevant (the reference is netcore). THE FIX (a
    proper slice): the type-forwarder resolution through the loaded
    facade references (GetExportedTypes/Resolve the forwards the way
    the C#'s DecompilerTypeSystem does), or the netcore runtime-pack
    discovery port. The attribute currently renders `/*Could not
    decode attribute arguments.*/` via the gold-pinned
    EnumUnderlyingTypeResolveException catch.

(e) THE USING OVER-COLLECTION: System.Collections and
    System.Threading.Tasks render where the oracle's set is smaller
    (the oracle derives its usings from the types the RENDERED SYNTAX
    names; the port's MinimalUsingSetOf approximates from the metadata
    signatures and bodies). The two extra namespaces' sources are NOT
    yet identified -- the assembly attributes and the rendered members
    name neither; probe the per-type minimal sets to find the
    contributing type before fixing. THE SWITCH-BODY SIMPLIFICATION -- CLOSED (`a2ad46ced`):
the default section whose thunk targets the exit is the FALL-THROUGH
shape (the after-switch code IS the default path) -- it drops from the
inlining targets, renders no `default:` label or body, and the case
bodies inline with their break/return terminators (the
exit-is-target gate rejected the whole switch before, leaving the raw
goto form). The connid 14/2 -> 3/1 (the pin at 54895b5b...); the
corpus unchanged. THE CONNID IS NOW 3/1: the Debuggable decode (1)
+ the two extra usings -- the last two items of the audit. THE
BUTTON.CLICKEVENT QUALIFICATION -- CLOSED (`7771054c3`): the static
members' declaring types render SHORT (`Button.ClickEvent`) -- the
emitter's ShortQualifiedMember after the same-type simplification;
the connid 16/4 -> 14/2; the pin at af9265b1... (the CastsAndTypeOperators
test's `(string)(V_0)` expectation updated to the bare-operand form).

The pin guards the merged tree's CURRENT state; each fix above moves
it deliberately (re-pin + the diff documented per slice).

## THE UNGUARDED MSCORLIB GOLD FAMILY -- CLOSED (`bc345d63c`): the
MetadataModule_Test mscorlib family ran with no availability check --
on a runner without the default mono mscorlib the module loads empty,
the gold counts fail, and the indexing into the empty
TopLevelTypeDefinitions vector SIGABRTs the whole binary (the earlier
triage note's ITypeDefinition vector abort was THIS family). Every
MscorlibFixture-using test now carries REQUIRE_MSCORLIB (the
Specialize_Test convention). The default-env suite completes with NO
SIGABRT; the four previously-failing gold tests skip; the baseline
sweep drops 32 -> 28.

## THE TRIAGE PAIR -- CLOSED: both tests assumed the MONO mscorlib's
shapes and failed under ILSPY_TEST_MSCORLIB pointing at the net48
corpus mscorlib. ChildCacheIsStable asserted the cached children by
hardcoded index (first[2]) -- the net48 mscorlib has only two root
namespaces, so the index aborted the vector access (the assertion
frame reports the INamespace vector; the earlier triage note's
ITypeDefinition vector was the imprecise recollection); the assertion
now locates the children by name. FieldCreateArms looked up List`1's
implementation fields -- the net48 mscorlib is a REFERENCE assembly
and strips the private fields (its List`1 carries none), so the null
lookup segfaulted in the Specialize call; the test now skips with a
message when the selected mscorlib lacks the shapes. THE WIDER
mscorlib-env FAMILY (the gold-value and digest tests pinned to the
mono mscorlib's layout -- ~35 tests under the net48 gate, e.g.
WholeTypeSpecializeDigests, RootChildrenOrderMatchesGold, the
BlockBuilder/ILFunctionMethod mscorlib sweeps) is the REMAINING
fixture-selection concern: they fail against the reference assembly by
design and need either the mono mscorlib on the CI or the same
skip-gating treatment (the default suite -- no env var -- skips them
cleanly; the baseline sweep stays 32).
  * The EXPLICIT-IMPL PROPERTY NAMES -- CLOSED (`77df0389a`): the
    interface-qualified rewrite mirrors the event arm's (the first
    explicitly implemented member's declaring type through
    RenderBaseTypeName + the name's last segment; the dotted metadata
    name on a NON-explicit property keeps the metadata name). Corpus:
    1402/1343 -> 1221/1158.
All in the normalized set diff over /tmp/pf_oracle.txt vs
/home/jim/tmp-build/pf_mine_nint2.txt (strip + de-duplicate before
categorizing -- the raw diff is dominated by the whole-module
indentation divergence).

THE NEXT QUEUE for this family (the corpus remainder):
  * The four families above (all in the final corpus diff at
    /tmp/pf_final_diff.txt).
  * The nested-generic declaring instantiation bound (the recursion
    renders the declaring type argument-less when the parameterized
    form lacks a generic-type chain).
  * The using-set over-collection: my PF whole-module set carries
    `using System.Threading.Tasks;` where the oracle's does not (find
    the over-collecting reference).

NOTE (the stale-queue catches): TransformFieldAndConstructorInitializers
was ALREADY PORTED via the master merge (885 lines + 17 tests, the
record support / primary constructors / XML doc / decimal-constant arms
deferred loudly in its .hpp) -- the facade queue's entry was stale, as
was the earlier ProxyCallReplacer one. The queue's remaining REAL
items: the name-qualification family (79 corpus base-list diffs -- the
resolver-based short-vs-qualified decisions, the biggest single
remaining diff mass), the body-level qualified member accesses
(`ModifierFixture.ModifierShapes.StaticField` -> `StaticField`, ~540
corpus lines), and the field-access short names in the flat bodies.

ProxyCallReplacer is DONE (the handoff's earlier entry -- the
EarlyILTransforms surface the facade queue referenced landed with it;
the queue note was stale).

### The facade-gap batch (commits 52e915582/a9285994e/25dd73412/545a47fb1)

The four recorded facade gaps, one commit each:
- **The explicit-impl interface qualifier** (`52e915582`): the dotted
  metadata name (`ModifierFixture.IShape.Area`) renders as the C# form
  (`IShape.Area`) -- the name after the last dot, qualified by the first
  ExplicitlyImplementedInterfaceMembers entry's declaring type.
- **The member and type attributes** (`a9285994e`): the entity's
  attributes render as the leading `[...]` sections through the ported
  ConvertAttribute over the output visitor (the CreateAstBuilder
  configuration -- AlwaysUseShortTypeNames, so `GeneratedCode` not
  `System.CodeDom.Compiler.GeneratedCode`). The CleanUpMethodDeclaration
  state machine attribute removals ride with it: AsyncStateMachine /
  IteratorStateMachine drop when the corresponding de-sugar succeeded
  (the async/iterator outcomes thread out of DecompileMethodToString;
  without it the state machine types leaked back through the attribute
  text -- the hidden-state-machines test caught it).
- **The accessor visibility and bodies** (`25dd73412`): the stub form
  (`{ get; protected set; }`) applies when the property has its
  compiler-generated `<Name>k__BackingField` field (only the compiler
  emits the angle-bracket names) or its accessors carry no bodies (an
  interface member); a real accessor body renders as its block (the
  statements extracted from the flat method render). The old stub-only
  form dropped real bodies on the floor.
- **The unnameable base-list filter** (`545a47fb1`): the C# f41b12c01
  fix -- the nested exemption over the declaring chain, then the
  MemberLookup.IsAccessible shape for type definitions (private nested
  only through the exemption; internal same-module; protected through
  the IsDerivedFrom chain walk; the declaring-type recursion). The
  generic type-argument walk stays deferred with the resolver surface.
  Fixture: `/home/jim/ilspy-test-fixtures/baselist_fixture/
  BaseListSynth.dll` -- a crafted assembly (the MetadataBuilder recipe,
  built with the box's dotnet 10) whose Unrelated implements the public
  control interface plus a private nested interface of an unrelated
  type. The corpus scan found zero droppable rows (the net48 reference
  set has none), so the filter is fixture-tested only.

17 gtest cases in FacadeMemberModifiers_Test.cpp across the batch. The
corpus scan over PresentationFramework (the whole-module render diffed
against the oracle) surfaced the NEXT facade gaps: the `enum` keyword
(the port renders `internal struct CS : UInt32` where the oracle renders
`internal enum CS : uint` -- the enum arm of the type-keyword switch is
the stand-in `struct`) and the builtin type keywords in base lists
(`uint`/`ushort` vs `UInt32`/`UInt16` -- the CSharpTypeName full names).
125 base-list diffs over the corpus, all these two shapes.

The const field initializer rides here too (`af35644b9`): the literal
from the field entity's decoded constant (the integer family with the
C# suffixes, the boolean, the quoted string, the char, null; the
floating-point forms stay deferred with the ConvertFloatingPointLiteral
fraction logic). The implicit no-argument base constructor call renders
nothing (`3269e9c63`): the C# constructor-initializer convention -- a
base call renders only with arguments; the spurious `base();` was the
seed's render of the decoded base .ctor call.

Fixture: `/home/jim/ilspy-test-fixtures/modifier_fixture/
ModifierFixture.dll` (the modifier matrix, the string/bool/long consts;
the build recipe is in ModifierFixture.cs). 12 gtest cases in
`tests/Decompiler/CSharp/FacadeMemberModifiers_Test.cpp`. The
accessor-visibility (`private set`) and accessor-body (`{ get { ... } }`)
forms remain the documented stand-in gap (the property renders `get;`
stubs); the member attributes ([GeneratedCode] etc.) remain deferred.

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
  (ProxyCallReplacer DONE -- see above; YieldReturnDecompiler DONE --
  all four parts landed: LongDict + SymbolicExecution + StateRangeAnalysis
  (Part 1); the enumerator-creation matching + the ctor/current/mapping/
  dispose metadata analyses (Part 2); AnalyzeMoveNext +
  PropagateCopiesOfFields + ConvertBody + TranslateFieldsToLocalAccess +
  the driver registration (Part 3); DecompileFinallyBlocks +
  ReconstructTryFinallyBlocks + BlockContainer::TopologicalSort/SortBlocks
  (Part 4). The tests ride a LOCALLY COMPILED fixture -- the net48 corpus
  turned out to be the REFERENCE-assembly set (stripped bodies; the oracle
  renders "Empty body found"), so
  /home/jim/ilspy-test-fixtures/yield_fixture/IteratorFixture.dll is built
  with the box's csc (Numbers/WithParameters/WithFinally iterator shapes;
  rebuild per IteratorFixture.cs in that directory). Port-surface notes:
  the out-form matchers stay file-local (MatchLdLocOut joins MatchLdcI4 --
  the shared MatchLdLoc is the match-against form); the reader leaves
  Call::Method null, so Call::MethodToken carries the raw token and
  CreateILAst resolves both fields AND in-module method tokens (the
  AliasMethod borrow); the terminator-slot conventions are documented at
  each site. Two engine bugs fixed under the fixture: the reader's branch
  resolution now walks the branch's enclosing container chain (the C#
  BlockBuilder.FindBranchTarget shape -- a flat offset->block map bound
  branches into an EH region's interior, and the branch-chain
  simplification then bypassed the region); the reaching-definitions walk
  gained the SwitchInstruction arm (the per-section state restore -- its
  absence fragmented the per-state return temporaries).
  The Mono/VB arms (the discriminator + CleanSkipFinallyBodies/
  CleanDoFinallyBodies/CleanFinallyStateChecks) stay deferred;
  DynamicCallSiteTransform, IntroduceRefReadOnlyModifierOnLocals) are
  NOT ported -- genuine port projects. AsyncAwaitDecompiler's task
  shapes ARE ported (see the LANDED entry); its dynamic / enumerator /
  VB arms stay deferred with their slices.
- The foreach-on-multi-dim arms: VERIFIED COMPLETE after the merge (the
  merged PatternStatementTransform carries the full
  TransformForeachOnMultiDimArray + MatchForeachOnMultiDimArray +
  MatchLowerBound + the four patterns, line-for-line with
  PatternStatementTransform.cs lines 516-700; 7 tests green).
- PatternStatementTransform's remaining arms + the
  IteratorVariablesDeclaredInsideLoopBody bail (see above).

### The PD13 pre-existing family segfault (fixed)

The disasm lane's PD13 record flagged a pre-existing family-run segfault
in ResolveTypeDirectBaseTypesTest, reproduced with their changes
reverted and hidden from every full-suite run (the exclusion list kept
the sweep green, and a second crasher --
MetadataModuleResolutionTest.FindModuleByReferenceTwoPasses -- aborted
even the isolated family run). Root cause: this box's
/usr/lib/mono/4.5 fixture path is absent, so the fixture-dependent
lookups return null and the tests dereferenced them -- a missing or
foreign mscorlib crashed the run instead of failing it. Fixes: the three
unguarded fixture derefs in ResolveType_Test.cpp (DirectBaseTypes
CacheAndIdentity's String/IComparable, ResolveTypeSpecificationArm's
FileSystemEnumerableIterator -- the PD13 crasher) ASSERT before the
deref, and FindModuleByReferenceTwoPasses skips when the fixture files
are absent (the file's own FileExists convention) plus guards the
indexed reference rows. The family-run over an mscorlib override (the
PD13 repro) now completes rc 1 with clean failures; the three stale
exclusion entries are retired from the local sweep list, so
DirectBaseTypesCacheAndIdentity and ResolveTypeSpecificationArm now fail
cleanly like their fixture-dependent siblings (the sweep baseline is the
mono-family failure set + these two; the IlParityGoldenTest capa-manifest
row stays flaky per its own note).

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
## THE HELLO.NET8 COMPARISON ARC (the current work order)

The net8 Debug fixture at /tmp/ilspy-cmp/ (out/hello.dll, the oracle
csharp_oracle.cs, the diff gated with `diff -w csharp_oracle.cs cpp_now.cs`).
THE GATE NEEDS DOTNET ON PATH (`export PATH=/home/jim/.dotnet:$PATH`) --
the resolver's runtime discovery is a PATH scan for the dotnet
executable; without it NO reference resolves (resolvedFiles=0). The
C# tool always has its host module directory; the port's documented
divergence (the GetHostRuntimeDirectory comment).

CLOSED (the arc 68 -> 33 normalized diff lines):
- The interpolation re-synthesis (4bdb4b1b5): the pattern matches BOTH
  the C# stloc/newobj fold (the seeded tests) and the port's raw
  `call .ctor(ldloca v, ldc.i4, ldc.i4)` shape; the consumer search
  walks the next statement, the block's final instruction (the inlined
  `leave call ToStringAndClear(...)` tail), then the fall-through
  sibling block. The 13 transform tests pass; the corpus unaffected
  (the net48 fixtures predate interpolated strings).
- The [Debuggable] decode: NO CODE CHANGE -- the decode works once the
  runtime is discoverable (the queue walks System.Runtime's forwarders
  into System.Private.CoreLib, the enum resolves, the flags render).
  The item was the environment, not a bug.
- Math.PI / Math.E (f9b3fe6d0): SpecialDoubleConstantText in the
  ILAstToCSharp (the FractionApprox continued fraction, the
  IsValidFraction 2/3/5 rule, the multiply and division forms; the
  raw-fraction and MathF arms documented as deferred).
- The numeric-binary parenthesization (835a81f29): precedence-only
  (the RIGHT-operand/tighter-parent and the ternary-branch-slots
  parenthesize; everything else bare; `checked(a + b)` single parens).

THE REMAINING RESIDUE (3 lines; the arc 68 -> 33 -> 28 -> 23 -> 21 -> 20 -> 8 -> 5 -> 3):
- CLOSED: the blank-line member separator (the field arm's trailing
  newline; the methods already self-separate; THE CONNID PIN MOVED to
  fbda64dd -- the blank lines, normalized 0/0 against the fresh oracle).
- CLOSED: the for-init hoist (the TransformFor two-phase pre-pass) and
  the naming family (the loop counters i/j/k, the interface I-strip,
  the num2-style suffix).
- CLOSED: the dangling-alias fold (the initializer statement folds the
  following `T u = v;` whose v lost its store to the dead-store
  keep-expression arm) and the foreach (the flat for-render's
  TransformForeachOnArray port: the `i < arr.Length` + `i++` +
  all-element-access shape renders `foreach (T e in arr)` with the
  element-type naming).
- THE LAST RESIDUE (3 -w lines) -- INVESTIGATED AND RECORDED: the
  oracle KEEPS the `string[] array2 = array;` alias and the foreach
  reads `in array2`; the port renders one statement (`in array`).
  THE FINDING: the port's inline gates ARE the C#'s single-load rule
  (VariableCanBeUsedForInlining matches the C# CanBeUsedForInlining
  verbatim); the divergence is the CHAIN GEOMETRY. The port's reader
  models `dup` with stack-slot temporaries, so the chain carries one
  extra hop: [stloc(dup_160, newarr)] [stores] [stloc(dup_168, ldloc
  dup_160)] [stloc(V_1, ldloc dup_168)] [stloc(V_4, ldloc V_1)]; the
  initializer matches at the dup_160 statement and the statement
  group's per-statement ILInlining then collapses the remaining hops
  one per visit, folding the whole chain into `stloc(V_4, block)` --
  the render-level dangling-alias fold then renders one statement. The
  C#'s ILAst has no dup slots (the reader shares the expression), so
  the initializer sits on V_1 and the C#'s chain stops a hop earlier,
  leaving the array2 alias the oracle shows. WHICH C# gate stops the
  last hop over the ArrayInitializer block value is unidentified (the
  CanMoveInto/MayReorder over block values, or the per-statement
  driver's revisit semantics); the root fix is the reader-level dup
  modeling (the C#'s expression sharing) -- a change with corpus-wide
  blast radius, deliberately out of this arc's scope.
THE CLOSING NUMBERS: the hello.net8 arc 68 -> 33 -> 28 -> 23 -> 21 ->
20 -> 8 -> 5 -> 3 -w lines (the remaining 3 = the kept-alias line +
the foreach's array variable name); the render otherwise byte-identical
to the oracle. Every guard held every slice: the full sweep 13,166 ran
/ 15 baseline (identical failures), the connid pin fbda64dd... (the
deliberate member-separator re-base; normalized 0/0 against the fresh
C# oracle), the net48 PresentationFramework corpus 0/0 throughout.
- CLOSED: the array-initializer family's render pieces (the element-
  type fix -- the C# passes MatchNewArr's element type, not the store
  variable's array type; the `new T[dims] { elements }` render -- the
  C# TranslateArrayInitializer port; the statement-position fallback
  `v = new T[n] { ... };` -- the op-32 was the initializer's final
  ldloc rendered as an unhandled statement).
- CLOSED: String.Concat -> the + chain; the GetTypeFromHandle unwrap;
  the typeof short name.
- THE REMAINING ITEMS: (1) the dup-alias line `string[] array =
  array_2;` -- the block-valued store does not inline into the user's
  local (the C# chain does; the port's dup_160/dup_168 slot chain
  breaks the FindLoadInNext inline -- investigate
  InlineOneIfPossible/CanMoveInto for the ArrayInitializer block);
  (2) the foreach -- the oracle renders `foreach (string text in
  array2)`, the port an indexed `for (; num < array.Length; num++)`
  -- the foreach detection (the C# LoopDetection/HighLevelLoop
  foreach pattern over the array enumerator); (3) the for-init hoist
  `for (int i = 1; ...)` vs `int num_1 = 1; for (; ...)`; (4) the
  local naming (num/i, iShape/shape, array_3/array -- the
  AssignVariableNames heuristics); (5) the blank line after the
  field declaration; (6) the Debuggable... all cosmetic-scale.
- CLOSED: the int->bool ternary condition (the ConvertConditionText --
  the C# ConvertToBoolean port; an int-typed condition renders
  `x != 0`; the ternary style `((cond) ? (a) : (b))`).
- CLOSED: the file-scoped namespace (the single-namespace module
  renders `namespace X;`; both whole-file paths; the connid and the
  70-namespace corpus keep the block form).
- THE ARRAY-INITIALIZER FAMILY (the next big item): the port's dup-slot
  modeling (`stloc(dup_160, newarr ...)` + `stloc(dup_168, ldloc
  (dup_160))` + `stloc(V_1, ldloc(dup_168))`) breaks the
  TransformArrayInitializers pattern (the C#'s HandleSimpleArrayInitializer
  needs the element stores on ONE local). The render shows `new string[]
  [2]` (the render-stage NewArr.Type wraps to the ARRAY type somewhere
  in the pipeline -- the raw reader holds the element type; the wrapping
  transform is NOT yet identified), the element stores as separate
  statements, the `/* unhandled statement op 32 */` (a dangling
  ldc-statement left by the pipeline over the dup flow), and the
  dangling array_2/array_3 aliases. The C#'s reader handles `dup`
  without temporaries (the stack-value aliasing); the port's dup_160/
  dup_168 slots are the divergence to fix.
- The loop-init hoist: `for (int i = 1; ...)` vs `int num_1 = 1; for
  (; ...)`.
- String.Concat -> `+` (the C# combines the string concat calls); the
  typeof().FullName form; the blank line after the field; the local
  naming (num/i, shape, text).

## THE PERFORMANCE ARC -- CLOSED (the debug-build discovery)

THE ENDING (the arc's real conclusion): **the whole performance
problem was measuring a debug build.** The `linux-ninja` preset carries
no `CMAKE_BUILD_TYPE`, so the tree built unoptimized with live asserts.
Measured on dnlib.dll end-to-end (`ilspy_cli --csharp`, bare env):

| build                          | wall    |
|--------------------------------|---------|
| Debug, before the RVA fix      | 36.5s   |
| Debug, after the RVA fix       | 15.7s   |
| **Release (new preset)**       | **2.0s**|
| the C# oracle (ilspycmd)       | ~9.4s   |
| Release, DOTNET_ROOT resolved  | 5.5s    |

The port in Release is ~4.7x FASTER than the oracle. The target
(under 20s, then the oracle's ~10s) is exceeded 5x over.

THE PERMANENT ARTEFACTS: (1) the `linux-ninja-release` preset
(CMakePresets.json, commit f96fe307b) -- `cmake --preset
linux-ninja-release && cmake --build --preset linux-ninja-release`
(needs VCPKG_ROOT on env, same as linux-ninja); (2) the README's
perf-measurement rule (Release only, with the numbers); (3) the RVA
fix itself (d597dc3bb, a real algorithmic win regardless of build
type: the using-set collector's per-method RVA linear scan was
quadratic in the module size).

THE CACHES -- ABANDONED, NOT NEEDED: the token-keyed member-ref/
type-resolution caches (the empty-generic-context shape) cut the
Debug resolved path 87s -> 48s but each alone changes the modifier
fixture's -t using set (`using System.Threading;` appears over the
fixture's Interlocked calls where the C# qualifies the calls and adds
no using). The mechanism, half-traced: cacheless, the collector's
per-body walk never reaches the Interlocked MemberRef (the render
resolves those tokens); with the cache, ONE collector visit of an
UnknownType "Interlocked" appears between render resolutions -- which
call it comes from and why the fresh path hides it remains open (the
[NS]/[CMPXCHG]/[COLL] prints were the trail). With the Release build
at 2.0s bare / 5.5s resolved, the win is not worth the risk; the
cache code lives in the turn history if ever revisited.

THE GUARDS (all held, debug AND release): the connid pin
(fbda64dd...), the corpus 0/0, the sweep 13,166 ran / 15 baseline
identical, the hello parity (the recorded 3-line residue; the
release binary's render is byte-identical to the debug one).

## THE PERFORMANCE ARC (the priority work order)

THE DATASET: dnlib.dll (1.1 MB, 710 types, ~9k bodies, netcoreapp3.1) --
the C# oracle ~9.4s / ~375MB; the port was ~36.5s bare / ~87s with the
runtime references resolved.

CLOSED (1f0ca... the RVA fix): the using-set collector's per-method RVA
linear scan over the whole MethodDef table (the quadratic) replaced
with the metadata layer's GetMethodRVA map probe. The bare whole-
module render: 36.5s -> 15.7s (under the 20s target), zero render
delta. THE GUARDS: all four held (the pin, the corpus, the sweep, the
hello parity).

THE MEASURED BREAKDOWN (the resolved path, post-fix): the using-set
walk ~66s (the body walk ~19s over 9094 bodies + the member-signature
sweeps ~16s + the rest), the type renders ~19s, the ctor ~0.3s.

THE ATTEMPTED-AND-REVERTED CACHES (the next session's entry point):
the token-keyed member-ref and type-resolution caches over the
EMPTY-GENERIC-CONTEXT shape cut the resolved path 87 -> 48s with a
zero dnlib render delta -- but they add `using System.Threading;` to
the modifier fixture's -t render (the fixture's Interlocked calls)
where the C# qualifies the calls and adds no using. WHY: unidentified
-- the cache makes some resolution observable that the fresh per-call
path hides (the pointer-identity dedup in the collector's
visitedTypes_ set, or a lazily-computed member surviving across the
cached instance, are the candidates). Bisect: EACH cache alone
reproduces it; the RVA fix alone does not. The cache code is in the
turn's history (the memberRefCache_/fieldRefCache_/typeTokenCache_
members + the cacheable gates in ResolveMethodReference/
ResolveFieldReference/ResolveType).

THE REMAINING HOT SPOTS (in measured order): (1) the member-signature
first-touch in the using-set sweeps (~16s resolved -- the per-method
GetAttributes/ReturnType/Parameters lazy entity construction over
the signature decode); (2) the body walk (~11s -- the per-body IL
cursor + the EH parse + the local-sig decode); (3) the per-type
renders (~19s -- the per-method ReadIL + the transform pipeline + the
flat render). The C# comparison: the oracle's whole run is ~9.4s, so
the port's per-unit work is ~2x -- the caches are the lever, gated on
the fixture regression.

