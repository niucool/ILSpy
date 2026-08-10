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
  operator-call (`op_Increment`/`op_Decrement`) case (needs
  `UserDefinedCompoundAssign` + `Call.IsLifted`) and the
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
  matching the real back end's `VisitUserDefinedCompoundAssign`. No pipeline
  transform constructs these nodes yet, so `--csharp` output is unchanged;
  the foundation is exercised by the unit tests + an 8000-method mscorlib
  sweep that constructs the nodes from real operator calls (System.Decimal's
  op_Equality/op_Addition/etc.).
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
