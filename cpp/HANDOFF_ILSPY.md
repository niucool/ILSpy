# ILSpy C++ Port -- Session Handoff (written after `dfcba6e08`)

Read this + `PORT_PLAN.md` + `cpp/README.md` (and the sibling
`cpp/PORT_LOG_BAML.md` / `cpp/PORT_LOG_DISASM.md` logs) at the start of a
fresh session.
Standing baseline: **connid_csharp sha256 `8358d5c1d6ff7ad3`** (re-baselined
deliberately at `164dd1a9b`; byte-verified stable). Sweep: `118 passed + 1
skipped` (filters below).

## Current position

- **PatternStatementTransform: the shell + four arms landed.** Two commits:
  `93f1f9bd2` (the cascading if-else simplification, the `a && (b && c)` ->
  `(a && b) && c` reassociation, the `!(a == b)` -> `a != b` rewrite, the
  ContextTrackingVisitor shell with the re-visit loop, the
  `Statement::PatternPlaceholder`/`Statement::ToStatement` bridge, the
  GetAstTransforms head wiring) and `dfcba6e08` (TransformFor: the
  while->for reshape + the declaration merge into an existing for's
  initializers; ForStatementUsesVariable / IsVariableUsedAfter /
  DescendIntoStatement and the same-variable / ref-local-used-after /
  continue bails).
- **RED discipline held both slices** (23 gtest cases in
  `tests/Decompiler/CSharp/Transforms/PatternStatementTransform_Test.cpp`).
- **Design notes for the landed code (read before extending):**
  - The void-visitor re-visit loop carries the C# `ContextTrackingVisitor<
    AstNode>` return value in the visitor's `lastResult` slot; every Visit
    override records there the node the C# method returns. Keep that
    contract when adding arms.
  - Patterns are lazily built process-lifetime singletons
    (`CascadingIfElsePatternHolder` / `TransformForPatternsHolder`, the
    `GetForeachPatterns` convention) with pattern children embedded through
    `Expression::ToExpression` / `Statement::ToStatement`.
- **Known deferral inside TransformFor:** the
  `IteratorVariablesDeclaredInsideLoopBody` bail reads
  `declareVariables.GetDeclarationPoint` and is deferred on the
  DeclareVariables port (loud comment at its C# slot in
  `PatternStatementTransform.cpp`). Nothing observes it yet -- the CLI
  `--csharp` path does not consult `GetAstTransforms` (connid stays
  byte-identical through both commits).
- Remaining PatternStatementTransform arms, smallest-first: foreach-on-array
  (`forOnArrayPattern`, ~287), foreach-on-inline-array (~410),
  foreach-on-multi-dim (~516), automatic property (~693, needs the
  IsBackingFieldOfAutomaticProperty regex + IProperty), destructor (~931,
  needs currentTypeDefinition), try-catch-finally reshape (~983), C# 7.3
  pattern-based fixed (~1087, needs GetResolveResult().Type), C# 8.0
  enhanced using (~1119), the Identifier backing-field rewrite (~840).

## Next steps (in order)

1. **DeclareVariables (893 lines)**: `Analyze`/`FindInsertionPoints`/
   `ResolveCollisions`/`InsertVariableDeclarations`/`UpdateAnnotations` +
   `VariableNeedsDeclaration` + `GetDeclarationPoint` -- RED-first per
   slice. When it lands: (a) wire `declareVariables.Analyze(rootNode)` +
   `ClearAnalysisResults` into `PatternStatementTransform::Run` (the loud
   comment there), (b) land the deferred `IteratorVariablesDeclaredInsideLoopBody`
   bail in TransformFor, (c) add its slot to `GetAstTransforms` (the C#
   position: after AddCheckedBlocks, before
   TransformFieldAndConstructorInitializers -- currently a loud comment).
2. The foreach arms (the `ForStatement` patterns at ~287/410/516; they
   also need `VisitForStatement`).
3. Then: the remaining arms above, the facade completion items, the
   deferred GetAstTransforms slots as their transforms land.

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
