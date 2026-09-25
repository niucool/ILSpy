# ILSpy C++ Port -- Session Handoff (written after `6f27d7110`)

Read this + `PORT_PLAN.md` + `cpp/README.md` (and the sibling
`cpp/PORT_LOG_BAML.md` / `cpp/PORT_LOG_DISASM.md` logs) at the start of a
fresh session.
Standing baseline: **connid_csharp sha256 `7ee1614849b6c8c3`** (re-pinned
DELIBERATELY at the whole-module-adoption slice: the --csharp output moved
from per-method comment blocks to the C# ilspycmd shape -- the leading
[assembly]/[module] sections plus `public partial class` type bodies with
fields/properties/constructors/method members. The pre-facade hash was
`8358d5c1d6ff7ad3`, re-baselined deliberately at `164dd1a9b`). Sweep:
`181 passed + 1 skipped` (filters below).

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

1. **The facade completion items** (PORT_PLAN.md's remaining facade
   checklist) and the deferred GetAstTransforms slots as their transforms
   land; the PatternStatementTransform work is done.
   LANDED this session: the AST-path DecompileModuleAndAssemblyAttributes
   (`c24930d38`), the property/constructor member surfaces
   (`067366fd9`), and the whole-module CLI adoption
   (DecompileWholeModuleToString; the --csharp path routes through the
   facade, -t through the type entry -- the connid re-pin above). The
   remaining facade gaps: the event member surface (deferred loudly in
   DecompileTypeToString -- needs an event-carrying fixture), the -o
   file-writer, and the instance surface (the per-instance type-system
   wiring and the partial-types registry lifetime).

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
./build/linux-ninja/tests/ilspy_tests --gtest_filter='PatternStatementTransformTest.*:AstTransformPipeline.*:TransformExpressionTreesTest.*:RunTransformsTest.*:GetILTransformsTest.*:CSharpDecompilerTest.*:SwitchOnStringTransformTest.*:SwitchOnStringHashtableTest.*:SwitchOnStringLengthCharTest.*:TransformDisplayClassUsageTest.*:TransformDisplayClassUsageSroaTest.*:LocalFunctionDecompilerUseSitesTest.*:StatementTransformTest.*:TransformCollectionAndObjectInitializersTest.*:TransformCollectionAndObjectInitializersStalePosTest.*:IndexRangeTransformTest.*:InlineArrayTransformTest.*:NamedArgumentTransformTest.*:DeconstructionTransformTest.*:TupleTransformTest.*:TransformArrayInitializersTest.*:ExpressionTransformsTest.*:LocalFunctionDecompilerTest.*:DelegateConstructionTest.*:DelegateConstruction.*:CombineExitsTransform.*:VariableUsageLists.*:IntroduceNativeIntTypeOnLocals.*:ReachingDefinitions.*:SplitVariables.*:Util_UnionFind.*' --gtest_brief=1
./build/linux-ninja/ILSpyCmd/ilspy_cli /tmp/connid_res.dll --csharp   # vs baseline
```

connid fixture: `tests/TestFixtures/ConnIdResFixtures.hpp`
(`WriteConnIdResDll()` -> temp dll). Baseline text at
`/tmp/connid_csharp_baseline.txt` (re-generate if /tmp was wiped; re-pin the
hash only after a DELIBERATE change, documented in the commit message).

Commit style: subject <= 72 chars, body explains the why, trailer
`Assisted-by: GLM:glm-5.3-flash:pi`, `git commit -F /tmp/msg.txt`, local
commits only, never push.
