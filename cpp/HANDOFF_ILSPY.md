# ILSpy C++ Port -- Session Handoff (written after `46a269370`)

Read this + `PORT_PLAN.md` + `cpp/README.md` (and the sibling
`cpp/PORT_LOG_BAML.md` / `cpp/PORT_LOG_DISASM.md` logs) at the start of a
fresh session.
Standing baseline: **connid_csharp sha256 `7c269b8e61993d80`** (re-pinned
DELIBERATELY at the event-member-surface slice: the --csharp render now
carries `event RoutedEventHandler Click;` and drops the add_Click/
remove_Click accessor bodies -- the Button.Click row the corpus held all
along. Prior re-pin: `7ee1614849b6c8c3` at the whole-module adoption;
pre-facade `8358d5c1d6ff7ad3` at `164dd1a9b`). Sweep:
`222 passed + 8 skipped` (the mscorlib/net48 env-gates skip when
unprovisioned; IlspyCmdProgramTest.* joined the filter with the -o
slice). Sweep discipline: passed + skipped MUST equal ran, and the exit
code is the gate -- a `tail -3` of brief output hid a failing
env-gated test for two slices.

## Current position

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

1. The SkipInit forms in DeclareVariables -- UNBLOCKED by the
   reference-set wiring: the recorded blocker was a live
   context.TypeSystem for FindType(KnownTypeCode.Unsafe) (the
   MinimalCorlib net answers it when the reference set misses Unsafe).
   The remaining work: wire TransformContext::TypeSystem (the slot
   exists, null today; the callers that hold a module can pass
   &module.Compilation()), then port the C# DeclareVariables lines
   717-770 (the SkipInit call statement in both the out-var and the
   plain-declaration forms).
2. The deferred GetAstTransforms slots as their transforms land
   (ReplaceMethodCallsWithOperators, IntroduceUnsafeModifier,
   AddCheckedBlocks, TransformFieldAndConstructorInitializers,
   IntroduceUsingDeclarations, ...).
3. GetOptions (the DecompilerSettings -> TypeSystemOptions mapping in
   DecompilerTypeSystem.cs) -- deliberately deferred: the settings
   defaults could shift the render; port with its own baseline
   evaluation.
4. The remaining loud sub-deferrals: UseImplicitlyTypedOutAnnotation,
   DeclareVariables' InsertDeconstructionVariableDeclarations,
   IsRefReadOnly.

## LANDED this session (the reference-set wiring)

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

- ConvertCoalesce / ConvertComparison (TransformExpressionTrees) -- blocked
  on the CSharpConversions/resolver ports.
- ProxyCallReplacer -- deferred on the resolved-IMethod call surfaces +
  `EarlyILTransforms` + `MatchLeave`.
- IntroduceDynamicTypeOnLocals -- deferred on
  `DynamicInstruction.GetArgumentInfoOfChild`.
- Attribute enum-argument decode (DebuggableAttribute's `DebuggingModes`):
  needs the referenced-core-library resolver (the connid-only compilation
  cannot resolve the nested TypeRef); the C#-faithful decode-error comment
  form IS ported (`07f7b95f0`).
- `DetermineAddressUse` deferred arms (LdFlda chains, Await, call-argument,
  ref-local shapes) in SplitVariables.
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
