// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/ILInlining.cs (subset). Inlines
// single-use StLoc values into their single load site and removes dead pure
// stores. The third transform in GetILTransforms(), after SplitVariables (which
// the port defers until the reaching-definitions dataflow lands; this subset
// works on the per-variable usage counts ComputeVariableUsage provides).
//
// The C# ILInlining implements three interfaces: IILTransform (whole-function),
// IBlockTransform (per-block), and IStatementTransform (per-statement, the first
// child of the GetILTransforms() StatementTransform). This port implements the
// IILTransform entry (the early whole-function pass) and the IStatementTransform
// entry (the second inlining pass the StatementTransform runs interleaved with
// the other per-statement transforms); the IBlockTransform entry is not wired
// (the port models BlockILTransform post-order transforms as IILTransform).
// The IStatementTransform Run loops InlineOneIfPossible at the given position
// until no change (the C# per-statement overload); the AllowInliningOfLdloca
// option (the ldloca-into-addressof path the C# second pass enables, which needs
// an AddressOf node + IsGeneratedTemporaryForAddressOf + ClassifyExpression;
// ClassifyExpression/IsReadonlyReference have since landed) is deferred to a
// later iteration.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
// The type-system method interface the free `MethodRequiresCopyForReadonlyLValue`
// takes (the IL/Instructions headers' full-include convention; a nested
// forward-declaration block would shadow the real `ILSpy::Decompiler::TypeSystem`
// namespace).
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class ILFunction;
class ILInstruction;
class ILVariable;

// The C# `[Flags] enum InliningOptions` (ILInlining.cs line 30): the options the
// inlining search consults. The port carries the members the ported callers set:
// None is the default; IntroduceNamedArguments lets FindLoadInNext promote a call
// argument to a named argument when the load cannot be reached by re-ordering;
// AllowInliningOfLdloca / Aggressive / FindDeconstruction /
// AllowChangingOrderOfEvaluationForExceptions stay deferred with the ldloca-
// into-addressof path, the aggressive heuristics, and the deconstruction finder.
enum class InliningOptions : unsigned {
    None = 0,
    Aggressive = 1,
    IntroduceNamedArguments = 2,
    FindDeconstruction = 4,
    AllowChangingOrderOfEvaluationForExceptions = 8,
    AllowInliningOfLdloca = 0x10,
};

inline InliningOptions operator|(InliningOptions a, InliningOptions b) {
    return static_cast<InliningOptions>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline InliningOptions operator&(InliningOptions a, InliningOptions b) {
    return static_cast<InliningOptions>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
inline bool HasInliningOption(InliningOptions options, InliningOptions flag) {
    return (static_cast<unsigned>(options) & static_cast<unsigned>(flag)) != 0;
}

// Inline the StLoc at `pos` into the next instruction's load of its variable,
// or remove it as a dead store. A free function mirroring the C#
// `ILInlining.InlineOneIfPossible(block, pos, InliningOptions.None, ctx)` static
// call. Returns true if the stloc was consumed. Exposed so other per-statement
// transforms (e.g. NullCoalescingTransform) can call it after a fold that opens
// up an inlining opportunity, matching the C#.
bool InlineOneIfPossible(Block* block, int pos, ILTransformContext& ctx);

// The C# `public static bool CanMoveInto(...)` (ILInlining.cs line 933):
// whether an expression can move to the target load's position (the ancestor
// slots must accept the inlining and the move must not reorder past any
// earlier sibling). Consumed by IndexRangeTransform's slicing re-merge.
bool CanMoveInto(ILInstruction* expressionBeingMoved, ILInstruction* stmt,
                 ILInstruction* targetLoad);

// The options-aware overload (the C# InlineOneIfPossible(block, pos, options,
// ctx)). NamedArgumentTransform.Run calls it with IntroduceNamedArguments set so
// a load the search cannot reach by re-ordering is promoted to a named argument.
bool InlineOneIfPossible(Block* block, int pos, InliningOptions options,
                         ILTransformContext& ctx);

// The C# `public static bool InlineIfPossible(Block block, int pos,
// ILTransformContext context)` (ILInlining.cs lines 159-165): aggressively
// inlines the stloc instruction at pos into the next instruction. The
// Aggressive option in the C# skips the NonAggressiveInlineInto restrictions
// applied to non-stack-slot variables; this port's inlining implements no such
// restriction (a documented divergence), so the wrapper delegates with the
// flag set, keeping the call shape the statement-level transforms
// (TransformCollectionAndObjectInitializers.Run's tail) use.
bool InlineIfPossible(Block* block, int pos, ILTransformContext& ctx);

// The C# `public static int InlineInto(Block block, int pos, InliningOptions
// options, ILTransformContext context)` (ILInlining.cs lines 138-157): inlines
// the instructions BEFORE pos into block.Instructions[pos], walking backwards
// from pos-1 while each InlineOneIfPossible succeeds, stopping at the first
// failure. Returns the number of instructions inlined (the count
// CopyPropagation.DoPropagate subtracts from its caller's loop index). The C#
// counts the block final inside Instructions, so its `pos >= Count` guard maps
// to `pos > Instructions.size()` here (pos == size is the final's index, a
// legitimate target the C# admits).
int InlineInto(Block* block, int pos, InliningOptions options,
               ILTransformContext& ctx);

// Result of ILInlining::FindLoadInNext -- the search for the single load of a
// variable inside an instruction subtree, into which an expression can be
// inlined. Faithful to the C# ILInlining.FindResultType / FindResult (subset: no
// Deconstruction, which needs the deconstruction finder this port defers).
//
//   Found         -- a load of the variable was found; inlining is possible (the
//                    caller decides whether the load's slot permits it).
//   Stop          -- the load was not found and re-ordering is not possible; abort.
//   Continue      -- the load was not found but the expression can be re-ordered
//                    past the tested subtree; keep searching.
//   NamedArgument -- a load was found in a call but re-ordering with respect to
//                    the other call arguments is not possible; the call can be
//                    converted to a named-argument call (only with
//                    IntroduceNamedArguments).
enum class FindResultType { Found, Stop, Continue, NamedArgument };
struct FindResult {
    FindResultType type;
    ILInstruction* loadInst;  // the ldloc/ldloca found (valid when type == Found / NamedArgument)
    // The call argument that must be promoted to a named argument (valid when
    // type == NamedArgument). Mirrors the C# FindResult.CallArgument.
    ILInstruction* callArgument = nullptr;
    static FindResult FoundResult(ILInstruction* loadInst) {
        return {FindResultType::Found, loadInst, nullptr};
    }
    static FindResult NamedArgumentResult(ILInstruction* loadInst, ILInstruction* callArg) {
        return {FindResultType::NamedArgument, loadInst, callArg};
    }
    static FindResult StopResult() { return {FindResultType::Stop, nullptr, nullptr}; }
    static FindResult ContinueResult() { return {FindResultType::Continue, nullptr, nullptr}; }
};

// Find the single load of `v` (an LdLoc or an LdLoca) inside `expr` that can be
// replaced by `expressionBeingMoved`, mirroring ILInlining.FindLoadInNext.
// Returns Found for both an LdLoc(v) and an LdLoca(v) match (faithful to the C#,
// which returns Found for both -- the CALLER gates whether an ldloca can
// actually be inlined; this port defers the ldloca-into-addressof path, so the
// inlining caller InlineOneIfPossible skips an LdLoca found). Exposed so other
// per-statement transforms (NullCoalescingTransform's value-types throw-
// expression fold and the hoisted-constructor-argument null guard) can locate
// the use they redirect, matching the C# static call.
FindResult FindLoadInNext(ILInstruction* expr, ILVariable* v,
                          ILInstruction* expressionBeingMoved,
                          InliningOptions options = InliningOptions::None);

// True when `inst` sits in the constructor initializer -- before the chained
// `: base(...)`/`: this(...)` call -- so a preceding hoisted argument null-guard
// is necessarily compiler-hoisted and can be folded into the call argument.
// Faithful to the C# ILInlining.IsInConstructorInitializer: returns false when
// the function is not an instance constructor with a chained call, or when the
// instruction (or its enclosing top-level statement) ends after the chained call
// starts. The top-level statement is the last ancestor (including `inst` itself)
// whose parent is a Block (the C# inst.Ancestors.LastOrDefault(.. Parent is Block),
// where Ancestors includes the node itself).
bool IsInConstructorInitializer(const ILFunction* function, const ILInstruction* inst);

// The top-level statement containing `inst` -- the last ancestor (including
// inst) whose parent is a Block, or null when no such ancestor exists. Exposed so
// the hoisted-constructor-argument null-guard fold can reuse the ancestor walk.
ILInstruction* TopLevelStatement(const ILInstruction* inst);

// The C# `internal static bool MethodRequiresCopyForReadonlyLValue(IMethod
// method, IType constrainedTo = null)` (IL/Transforms/ILInlining.cs line 438):
// whether calling `method` on a readonly lvalue requires an implicit copy --
// always for a null method (the caller's `toStringMethod != null` guard pairs
// with this C#-side default), never for a reference-type declaring type
// (reference types are never implicitly copied) or a readonly-struct method,
// otherwise yes. `constrainedTo` (the call's constrained prefix) wins over the
// method's declaring type. The port's IMethod interface carries no
// ThisIsRefReadOnly override for metadata methods yet, so the readonly-struct
// arm reads false -- a readonly-struct method is treated as requiring the copy
// (the conservative direction; the ReadonlyRefLike metadata shape is deferred
// with the interface's remaining ref-read-only surface).
bool MethodRequiresCopyForReadonlyLValue(const TypeSystem::IMethod* method,
                                         const TypeSystem::IType* constrainedTo = nullptr);

// The C# `internal static bool IsReadOnlySpanCharCtor(IMethod method)`
// (IL/Transforms/ILInlining.cs line 541): whether the method is the
// `ReadOnlySpan<char>..ctor(ref readonly char)` constructor -- a one-parameter
// constructor whose declaring type is the closed `ReadOnlySpan<char>` generic
// instantiation and whose parameter type is `ref readonly char` (a
// ByReferenceType over Char). The CallBuilder's span-based string-concat
// detection walks it over the `newobj ReadOnlySpan<char>(&c)` operand shapes.
bool IsReadOnlySpanCharCtor(const TypeSystem::IMethod* method);

// The C# `internal enum ExpressionClassification` (ILInlining.cs line 987): how a
// translated C# expression may be used as an lvalue -- an rvalue, a mutable lvalue,
// or a readonly lvalue.
enum class ExpressionClassification {
    RValue,
    MutableLValue,
    ReadonlyLValue,
};

// The C# `internal static ExpressionClassification ClassifyExpression(ILInstruction
// inst)` (ILInlining.cs line 557): classifies the expression `inst` will turn into.
// A local that is ref-readonly / a foreach / a using local is a readonly lvalue;
// every other local is a mutable lvalue; an ldobj/stobj is a mutable lvalue unless
// its address is a readonly reference; a call returning a multi-dimensional array
// element is a mutable lvalue; everything else is an rvalue. Exposed so the
// ExpressionBuilder's AddressOf arm can decide whether a cast is needed to force a
// copy.
ExpressionClassification ClassifyExpression(ILInstruction* inst);

// The C# `internal static bool IsReadonlyReference(ILInstruction addr)` (ILInlining.cs
// line 613): whether the address `addr` denotes a location the C# compiler considers
// readonly. The port's LdFlda/LdsFlda carry the resolved FieldAttributes.InitOnly bit
// (FieldIsReadOnly); the field's ref-readonly return type checks and the
// MatchLdFld default arm (a field's ref-readonly return type) stay deferred with the
// port's IField surface.
bool IsReadonlyReference(ILInstruction* addr);

class ILInlining : public IILTransform, public IStatementTransform {
public:
    // IILTransform: the whole-function inlining pass (runs early in the
    // pipeline, before InlineReturnTransform).
    void Run(ILFunction& function, ILTransformContext& context) override;
    // IStatementTransform: the per-statement inlining pass (the first child of
    // the StatementTransform). Loops InlineOneIfPossible at `pos` until no
    // change, mirroring the C# ILInlining.Run(Block, pos, ctx) overload.
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
