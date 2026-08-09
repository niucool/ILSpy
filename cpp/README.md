# ilspy (C++ port)

C++17 port of the ILSpy decompiler **core and CLI** (the `ICSharpCode.Decompiler`,
`ICSharpCode.ILSpyX`, `ICSharpCode.BamlDecompiler`, and `ICSharpCode.ILSpyCmd`
projects). The GUI, AddIns, Installer, and PowerShell front-ends are out of scope.

The *what and why* of the port lives in [`../PORT_PLAN.md`](../PORT_PLAN.md); the
*how* (file/name/namespace mirroring, C#-to-C++ construct mapping) lives in
[`../MIRROR_LAYOUT_REORGANIZATION.md`](../MIRROR_LAYOUT_REORGANIZATION.md).

## Status

Phase 0, the bulk of Phase 1 (metadata), the start of Phase 2 (type system),
and the start of Phase 3 (the ILAst model + a straight-line IL reader) are
implemented and green here. Everything else follows the phase plan in
`PORT_PLAN.md`:

- **Phase 0** -- build system, `Util/` primitives (UTF-8/16, `Span`, `ImmutableStack`),
  the three targets, and a Google Test driver. DONE.
- **Phase 1** -- `Decompiler/Metadata/` on the vendored `microsoft/winmd` ECMA-335
  baseline: method-body decoding (ECMA-335 II.25.4 tiny/fat + exception handlers),
  signature decoding (Type/Method/Field -> IType), the entity surface (TypeDef/
  Method/Field/Property + base types + custom attributes + TypeKind derivation),
  token resolution, and an IL text disassembler. DONE except: Portable PDB debug
  tables, WebCIL, single-file bundles, the assembly resolver (`.deps.json`).
- **Phase 2** -- `Decompiler/TypeSystem/`: naming primitives (`TopLevelTypeName`,
  `FullTypeName`), `KnownTypeCode` (full 60-entry table), the `IType` hierarchy
  (`KnownType`/`SimpleType`/`ParameterizedType`/`ArrayType`/`ByReferenceType`/
  `PointerType`/`TypeParameter`/`SpecialType`), `DeriveTypeKind`. STARTED (the full
  entity layer, `ICompilation`/`MetadataModule`, interning remain).
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
- **Phase 4 (in progress)** -- the ILAst transform pipeline: `IILTransform` /
  `ILTransformContext` (Transforms/), variable/block usage analysis
  (`ControlFlow/VariableUsage`), the pipeline's first transform
  `ControlFlowSimplification` (branch-chain collapse, dead stack-slot store
  removal, debug return-block inlining, branch-to-leave folding, single-edge
  block merging), `ILInlining` (single-use variable inlining + dead pure
  store removal), `InlineReturnTransform` (duplicate shared return blocks so
  each `stloc V; br ret` gets a 1-pred return block CFS then merges), and the
  FlowAnalysis foundation (`ControlFlowNode`, `Dominance` --
  Cooper-Harvey-Kennedy dominators, `ControlFlowGraph` -- per-container CFG).
  `LoopDetection` wraps back edges in loop containers; `ConditionDetection`
  inlines single-pred fall-through into if/else and inverts `if (cond) goto X
  else { exit }` to `if (!cond) { exit }` (early-exit, condition negation),
  turning sequential if-throw chains (e.g. `System.Version..ctor`) into clean
  `if (arg < 0) { throw }` with no gotos. `InlineReturnTransform` duplicates
  shared return blocks; `StObjToStLoc` turns `*(&V) = value` into `V = value`;
  `AssignVariableNames` renames `V_0` to type-inferred names (`num`, `text`,
  ...); `RemoveRedundantReturn` drops trailing `return;`; `RemoveInfeasiblePath`
  redirects a constant-store-and-test around the infeasible arm (drops the dead
  store and the branch straight to the feasible exit). `DetectPinnedRegions`
  detects IL `fixed` blocks: a pinned local's store + the region it covers are
  wrapped in a `PinnedRegion` (the GC-pin scope), the trailing unpin store is
  stripped on the region's single-predecessor exit, and the pin block falls
  through to the exit. `DetectCatchWhenConditionBlocks` drops the redundant
  isinst type test at the start of a `catch (T e) when (...)` filter (the catch
  is already typed T), branching the entry straight to the when-condition
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
  ConditionDetection + LockTransform + UsingTransform + CachedDelegateInitialization + CachedReadOnlySpanInitialization + StatementTransform{ILInlining, ExpressionTransforms} + AssignVariableNames + RemoveRedundantReturn before the
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
  the delegate type, and the ldftn method name. `Call` gained an `IsNewObj`
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
  and `IsLifted` is dropped -- no nullable-lifting model). The `conv.i4(ldlen)`
  first VisitConv rewrite is still blocked by the LdLen model divergence (this
  port's LdLen already returns I4, so the conv is a no-op I4->I4 and
  `conv.i8(ldlen)` cannot be faithfully rewritten). The fold fires 1 time on
  the 8000-method mscorlib sweep (the legacy csc emits `conv.r.un` rarely; it
  fires more on Roslyn-compiled / modern .NET), so it is a real-corpus ILAst-
  cleaning transform, not faithfulness-only.
  The
  remaining 13 per-statement children (DynamicIsEventAssignmentTransform,
  TransformAssignment, NullableLiftingStatementTransform,
  NullPropagationStatementTransform, TransformArrayInitializers,
  TransformCollectionAndObjectInitializers, TransformExpressionTrees,
  IndexRangeTransform, DeconstructionTransform, NamedArgumentTransform,
  RemoveUnconstrainedGenericReferenceTypeCheck, UserDefinedLogicTransform,
  InterpolatedStringTransform) and the `ILInlining` `AllowInliningOfLdloca`
  option (the ldloca-into-`addressof` path the C# second pass enables, which
  needs an `AddressOf` node + `IsGeneratedTemporaryForAddressOf` +
  `ClassifyExpression`) are deferred, as are the rest of `ExpressionTransforms`
  (the NullableLifting call, the VisitConv `conv.i4(ldlen)` first rewrite (still
  blocked by the LdLen model divergence -- this port's LdLen already returns I4;
  the `conv.rN(conv.r.un(...))` combining rewrite is now ported, unblocked by
  the D85 Conv Kind model), the Call/NewObj/LdObj/StObj/StLoc
  `HandleCompoundAssign`, the remaining VisitIfInstruction pieces
  (NullableLifting, UserDefinedLogic,
  `TransformDynamicAddAssignOrRemoveAssign`), the SwitchExpression/Dynamic/
  TryCatchHandler visit methods). It folds the `VisitBinaryNumericInstruction`
  shift-size rewrite: `a << (b & 31)` / `a >> (b & 31)` -> `a << b` / `a >> b` --
  a shift's right operand masked with the bit-width minus one is redundant in
  C# (the shift already masks the count); the mask is dropped when it is the
  expected width (31 for I4, 63 for I8). The native-int (I) case --
  `sizeof(IntPtr) * 8 - 1` -- is deferred (needs SizeOf to carry an IType with
  GetStackType), as is the BitAnd/Boolean nullable-lift case (needs
  NullableLiftingTransform + InferType). The `NullCoalescingInstruction` ILAst
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
  throw-expression cases (the `a ?? throw ...` arm, the hoisted-constructor-
  argument null guard, the value-types throw-expression) are deferred (need the
  ThrowExpressions setting + a mutable Throw ResultType + ILFunction.Method
  metadata + ILInlining.IsInConstructorInitializer + MatchLogicNot /
  MatchHasValueCall wiring). The reference-type `??` lowering is a Roslyn-era
  codegen pattern; a corpus probe across 8000 mscorlib methods found 1797
  `comp(eq, ldloc X, ldnull)` null-check ifs and 513 `comp(ne, ..)` but zero
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
  struct; the Decimal-operator branch (a Call to op_Equality/... on
  System.Decimal) is deferred (needs `Call.Method.IsOperator`, which this
  port's Call does not carry). The helper is the bridge the nullable-lifting
  lift machinery consults to recognise the C#-style lifted comparison shape;
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
  The remaining `Run(IfInstruction)` paths (the LiftCSharpUserComparison rest
  of LiftNormal [needs the Call-operator case], MatchCompOrDecimal/LiftCSharp*
  [the comparison lift, needs CompOrDecimal.MakeLifted + a faithful
  NewNullable], NullPropagation [a separate 583-line transform]),
  `Run(BinaryNumericInstruction)` (the BitAnd-as-short-circuit analysis), and
  the `RunStatements(Block, int)` block transform are the subsequent
  in-order targets.
  `BitSet` (Util/, a tested foundation ported from BitSet.cs) is the
  fixed-capacity 64-bit-word bitset the `DoLift`/`DoLiftBinary` relevance
  analysis returns -- `bits.All(0, nullableVars.Count)` is the "every nullable
  var contributed to the lift" gate the D100 DoLift path (now wired) and the
  deferred MatchCompOrDecimal/LiftCSharp* comparison-lift path consult -- and
  the foundation the deferred DefiniteAssignment / Dominance /
  ReachingDefinitions analyses are built on. Faithful to the C# API
  (capacity rounding, `Any`/`All`/set-relation predicates/`Set`/`Clear`/
  `NextSetBit`/`SetBits`/`ReplaceWith`/`Clone`/`ToString`), with the C# 6-bit
  `ulong` shift masking replicated explicitly (a >=64-bit shift is UB in
  C++17) and a portable `TrailingZeroCount64` (C++17 has no
  `std::countr_zero`). Not wired into any transform yet; exercised by the unit
  tests.
  The remaining field-cached delegate shapes (now unblocked on the IField side)
  still need the block-model adaptation + the per-variable store-list tree
  walk + a corpus probe; the async/iterator state machines
  (YieldReturnDecompiler/AsyncAwaitDecompiler), SplitVariables (needs
  reaching-definitions dataflow),
  DetectExitPoints + the full ConditionDetection (multi-pred join blocks),
  the PatternMatchingTransform recursive sub-patterns (DetectPropertySubPatterns /
  PropertyOrFieldAccess / CompatibleExitInstruction),
  HighLevelLoopTransform (while/for), and the remaining StatementTransform
  per-statement children (TransformAssignment, ...) are
  the subsequent in-order targets.
- **Phase 5 (seed)** -- `Decompiler/CSharp/ILAstToCSharp`: an ILAst -> C#-text
  walker that closes the IL -> ILAst -> text pipeline end-to-end ahead of the
  real back end. It now produces readable C#: real parameter names (Param
  table) and string literals (#US heap), type-inferred local names (`num`,
  `text`, `array`) declared with C# keywords (`int num`, `double x`),
  `if/else` for fall-through + early-exit if-throw chains + shared-tail merges
  with condition negation, `while (true) { ... }` loops with `continue`/`break`,
  `base(args)` for base-ctor calls, `receiver.Method(args)` for instance
  calls, compound assignments (`V++`, `V += expr`), `value == null` for object
  null checks, `arr.Length` (no redundant `(int)` cast), `-x` for `0 - x`,
  `*(this)`/`*(byref)` rendered as the bare name, and generic `newobj` with
  its type. No trailing `return;` / no duplicate loop labels. Remaining gaps
  vs the real back end: gotos for multi-pred join blocks and loop-internal
  condition/increment jumps (needs DetectExitPoints + HighLevelLoopTransform),
  full type names (no `using` directives), and overload-resolved casts --
  these land with the Phase 5 C# AST + resolver.
- Phases 4-11 (IL transforms, C# AST + resolver + output, disassembler output,
  orchestration, ILSpyX, BamlDecompiler, the full `ilspycmd`, integration) -- per
  `PORT_PLAN.md`.

The CLI does `<assembly> --il [-t Type]` (IL text disassembly),
`<assembly> --ilast[-all] [-t Type]` (decode bodies to an ILAst tree and dump
it), and `<assembly> --csharp [-t Type]` (translate decodable bodies to
C#-ish text via the Phase 5 seed) end-to-end today; real C# output is Phase 5.

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
  ILSpyX/                <- ICSharpCode.ILSpyX core subset (Phase 8)
  BamlDecompiler/        <- ICSharpCode.BamlDecompiler (Phase 9)
  ILSpyCmd/              <- ICSharpCode.ILSpyCmd CLI (Phase 10)
  tests/                 <- gtest, mirroring source dirs
```
