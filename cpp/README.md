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
  25 of ~40 transforms ported. The switch-detection family is now complete in
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
  ConditionDetection + LockTransform + UsingTransform + CachedDelegateInitialization + CachedReadOnlySpanInitialization + StatementTransform{ILInlining} + AssignVariableNames + RemoveRedundantReturn before the
  C# seed, so `fixed (...) { ... }`, `default(T)`, reconstructed `switch`
  statements, switch-on-nullable `case null:` arms, `is T x` patterns,
  `lock (...) { ... }`, and `using (...) { ... }` statements now
  appear in the output. 25 of ~40 transforms ported (the StatementTransform
  orchestration + its first child ILInlining; the remaining 15 per-statement
  children are deferred).
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
  instruction stays in range). The remaining 15 per-statement children
  (ExpressionTransforms, DynamicIsEventAssignmentTransform, TransformAssignment,
  NullCoalescingTransform, NullableLiftingStatementTransform,
  NullPropagationStatementTransform, TransformArrayInitializers,
  TransformCollectionAndObjectInitializers, TransformExpressionTrees,
  IndexRangeTransform, DeconstructionTransform, NamedArgumentTransform,
  RemoveUnconstrainedGenericReferenceTypeCheck, UserDefinedLogicTransform,
  InterpolatedStringTransform) and the `ILInlining` `AllowInliningOfLdloca`
  option (the ldloca-into-`addressof` path the C# second pass enables, which
  needs an `AddressOf` node + `IsGeneratedTemporaryForAddressOf` +
  `ClassifyExpression`) are deferred.
  The remaining field-cached delegate shapes (now unblocked on the IField side)
  still need the block-model adaptation + the per-variable store-list tree
  walk + a corpus probe; the async/iterator state machines
  (YieldReturnDecompiler/AsyncAwaitDecompiler), SplitVariables (needs
  reaching-definitions dataflow),
  DetectExitPoints + the full ConditionDetection (multi-pred join blocks),
  the PatternMatchingTransform recursive sub-patterns (DetectPropertySubPatterns /
  PropertyOrFieldAccess / CompatibleExitInstruction),
  HighLevelLoopTransform (while/for), and the remaining StatementTransform
  per-statement children (ExpressionTransforms, TransformAssignment, ...) are
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
