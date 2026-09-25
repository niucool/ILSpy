# ILSpy C++ Port -- Session Handoff (written after `1515baaba`)

Read this + `PORT_PLAN.md` + `cpp/README.md` (and the sibling
`cpp/PORT_LOG_BAML.md` / `cpp/PORT_LOG_DISASM.md` logs) at the start of a
fresh session.
Standing baseline: **connid_csharp sha256 `8358d5c1d6ff7ad3`** (re-baselined
deliberately at `164dd1a9b`; byte-verified stable). Sweep: `118 passed + 1
skipped` (filters below).

## Current position (mid-slice)

- **PatternStatementTransform is the critical path; survey done, no code
  written yet.** Nothing uncommitted. Next concrete step: RED test for the
  first arms (below), then the transform class.
- C# reference: `ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs`
  (1138 lines). Smallest-first arms:
  1. `SimplifyCascadingIfElseStatements` (line ~1005): `cascadingIfElsePattern`
     = `IfElseStatement{Condition=AnyNode, TrueStatement=AnyNode,
     FalseStatement=BlockStatement{Statements={NamedNode("nestedIfStatement",
     IfElseStatement{Condition=AnyNode, TrueStatement=AnyNode,
     FalseStatement=OptionalNode(AnyNode)})}}}` -- rewrite: `node.FalseStatement
     = elseIf.Detach()`.
  2. `VisitBinaryOperatorExpression` (~1044): `a && (b && c)` becomes
     `(a && b) && c` (reassociate; ConditionalAnd/ConditionalOr only).
  3. `VisitUnaryOperatorExpression` (~1084): `!(a == b)` becomes `a != b`.
  4. `TransformFor` (~186, medium): while->for; needs `ForStatementUsesVariable`,
     `IsVariableUsedAfter`, `IteratorVariablesDeclaredInsideLoopBody`,
     `DescendIntoStatement`, the continue-in-while bail.
  Bigger arms (foreach-on-array/inline-array/multi-dim, automatic property,
  destructor, try-catch-finally reshaping, fixed-statement): later slices.
- **Port infra verified ready:** `Syntax/PatternMatching/` is ported and
  tested (`Pattern`, `Match`, `MatchNode(pattern, candidate)`, `AnyNode`,
  `NamedNode`, `OptionalNode`, `Repeat`, `Backreference`, `Choice`; tests at
  `tests/Decompiler/CSharp/Syntax/PatternMatching/PatternNodes_Test.cpp`).
  `GetILVariable(IdentifierExpression)` etc. live in
  `Decompiler/CSharp/Annotations.hpp` (the `ILVariableResolveResult`
  annotation channel). `AstNode : INode` (DoMatch/DoMatchCollection).
- Port transform convention: `IAstTransform` + an internal
  `DepthFirstAstVisitor` (the `NormalizeBlockStatements` precedent -- void
  visitor, in-place rewrites). The C# `ContextTrackingVisitor<AstNode>`'s
  re-visit loop (VisitChildren do-while until stable) needs porting when
  PatternStatementTransform lands.
- The driver from `1515baaba` is the landing harness:
  `CSharpDecompiler::GetAstTransforms()` + `RunAstTransforms(rootNode,
  decompileRun, decompilationContext=nullptr)`. PatternStatementTransform's
  ported slots go into `GetAstTransforms` at the C# head position
  (CSharpDecompiler.cs line ~239, currently a loud comment).

## Hazard-ledger highlights (keep)

- **Include hygiene:** `DecompileRun.hpp` drags
  `CSharp/TypeSystem/UsingScope.hpp` whose fully-qualified
  `namespace ILSpy::Decompiler::CSharp::TypeSystem` SHADOWS unqualified
  `TypeSystem::` inside the CSharp namespace. Rule: a CSharp-namespace header
  must NOT include DecompileRun.hpp; fwd-declare
  `ILSpy::Decompiler::DecompileRun` (sibling) and let the .cpp include it.
- **Unqualified sibling-namespace names inside `namespace CSharp { ... }` do
  not see TypeSystem/other siblings** -- fully qualify
  (`::ILSpy::Decompiler::TypeSystem::X`) in code living inside the CSharp
  namespace.
- **Fwd-decl placement:** sibling-namespace fwd-decls must sit OUTSIDE
  `namespace ILSpy::Decompiler::CSharp { ... }` (fully-qualified statements),
  never nested inside it (the nested form creates a shadowing
  `CSharp::TypeSystem`).
- **Partial-write trap on REAL files:** a python script that writes after an
  assert-abort can leave a TRUNCATED file (this happened to this very
  handoff: the failed ascii-strict write zeroed the file and the empty blob
  got committed). Rule: after ANY scripted-edit abort, `git status` + `git
  diff` BEFORE staging; prefer in-place `edit` over scripted rewrite for
  files that already exist.
- Stale-binary trap: rebuild `ilspy_cli` separately before any connid run
  (test-target builds leave it stale).
- Stash `stash@{0}` is the port-baml WIP from another session -- DO NOT touch.
- Merge commits `07e8123f2`/`a08615150` (port-baml, port-disassembler
  textmatch) are from another session -- untouched.
- **MethodBodyReader DBG litter:** already queued to the baml/disassembler
  session for cleanup -- do NOT duplicate.

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
- PatternStatementTransform's big arms (see above).

## Build / test / sweep

```
cd /home/jim/source/ilspy/cpp
export PATH=/home/jim/cpp-tools/cmake/bin:/home/jim/cpp-tools/ninja-bin:$PATH
ninja -C build/linux-ninja ilspy_tests   # + ilspy_cli before harness/connid runs
./build/linux-ninja/tests/ilspy_tests --gtest_filter='TransformExpressionTreesTest.*:RunTransformsTest.*:GetILTransformsTest.*:CSharpDecompilerTest.*:SwitchOnStringTransformTest.*:SwitchOnStringHashtableTest.*:SwitchOnStringLengthCharTest.*:TransformDisplayClassUsageTest.*:TransformDisplayClassUsageSroaTest.*:LocalFunctionDecompilerUseSitesTest.*:StatementTransformTest.*:TransformCollectionAndObjectInitializersTest.*:TransformCollectionAndObjectInitializersStalePosTest.*:IndexRangeTransformTest.*:InlineArrayTransformTest.*:NamedArgumentTransformTest.*:DeconstructionTransformTest.*:TupleTransformTest.*:TransformArrayInitializersTest.*:ExpressionTransformsTest.*:LocalFunctionDecompilerTest.*:DelegateConstructionTest.*:DelegateConstruction.*:CombineExitsTransform.*:VariableUsageLists.*:IntroduceNativeIntTypeOnLocals.*:ReachingDefinitions.*:SplitVariables.*:Util_UnionFind.*:AstTransformPipeline.*' --gtest_brief=1
./build/linux-ninja/ILSpyCmd/ilspy_cli /tmp/connid_res.dll --csharp   # vs baseline
```

connid fixture: `tests/TestFixtures/ConnIdResFixtures.hpp`
(`WriteConnIdResDll()` -> temp dll). Baseline text at
`/tmp/connid_csharp_baseline.txt` (re-generate if /tmp was wiped; re-pin the
hash only after a DELIBERATE change, documented in the commit message).

## Next steps (in order)

1. PatternStatementTransform: RED test (cascading if-else + the two logic
   arms) -> implement (shell + visitor + arms) -> sweep -> commit.
2. TransformFor arm (the while->for reshape + the pattern use).
3. DeclareVariables (893 lines): `Analyze`/`FindInsertionPoints`/
   `ResolveCollisions`/`InsertVariableDeclarations`/`UpdateAnnotations` --
   RED-first per slice; the C# `PatternStatementTransform.Run` calls
   `declareVariables.Analyze(rootNode)` so wire that when it lands.
4. Then: facade completion items, the remaining GetAstTransforms slots as
   their transforms land, the deferred arms above.

Commit style: subject <= 72 chars, body explains the why, trailer
`Assisted-by: GLM:glm-5.3-flash:pi`, `git commit -F /tmp/msg.txt`, local
commits only, never push.
