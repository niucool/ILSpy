// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the ExpressionBuilder SKELETON (ICSharpCode.Decompiler/CSharp/
// ExpressionBuilder.cs -- the ILVisitor<TranslationContext, TranslatedExpression>
// that translates ILAst to C# expressions): the ctor with its resolver/ast-builder/
// type-inference field construction, the translation entry family (Translate /
// TranslateCondition / the ConvertType / ConvertConstantValue / ConvertVariable
// helpers), the visitor-dispatch entry with the per-instruction Visit arms the
// slices have landed (the leaf loads/stores of literals, locals and addresses),
// and the self-contained statics the later slices share (the operator-name
// tables, the overflow checks, UnwrapBoxingConversion, ChangeDirectionExpressionTo,
// the ErrorExpression fallback).
//
// The C# `ILVisitor<TranslationContext, TranslatedExpression>` base is a
// double-dispatch: every ILInstruction overrides AcceptVisitor<C,T> to call the
// visitor's Visit<Instr>(this, context). The port's IL tree has no AcceptVisitor
// surface (the ExpressionTransforms convention): `Visit` is an OpCode switch
// dispatching to the per-instruction Visit methods, and `Default` is the C#
// base's not-overridden fallback (the ErrorExpression with the 'OpCode not
// supported' message) -- so an instruction whose C# Visit method exists but has
// not been ported yet degrades to the error expression instead of crashing.
//
// The C# `StatementBuilder` parameter is a mutual reference the ctor only
// STORES (the builder owns the statement-level walk and constructs this
// ExpressionBuilder passing `this`); the port forward-declares it and takes a
// non-owning pointer (the C# GC reference convention).
//
// Deferrals (each named at the member that needs it): the heavy Visit arms that
// have not landed yet (the dynamic/deconstruct arms, and Await, which needs the
// awaiter/GetResultMethod pipeline surfaces), and the CancellationToken (the
// cooperative-cancel throw is a no-op in the port, the DecompileRun convention).

#pragma once

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/CSharp/TranslationContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

// The forward declarations at GLOBAL scope (the nested-namespace trap: a declaration
// written inside namespace CSharp would create CSharp::IL and shadow the real
// ILSpy::Decompiler::IL). UnboxAny is a ByValueInstruction parameter; BinaryInstruction
// is the HandleThreeValuedLogic parameter type (an incomplete type is fine in a member
// declaration).
namespace ILSpy::Decompiler::IL {
class UnboxAny;
class BinaryInstruction;
class Call;
class IsInst;
class LocAlloc;
class LocAllocSpan;
class Comp;
class BinaryNumericInstruction;
class StringToInt;
class SwitchInstruction;
enum class ComparisonKind : std::uint8_t;
}

namespace ILSpy::Decompiler::CSharp {

// The REAL type-system namespace alias: the CSharp/TypeSystem sub-namespace
// (CSharpTypeResolveContext/UsingScope) shadows the plain `TypeSystem::` lookup
// inside this namespace, so every type-system reference goes through the
// fully-qualified alias (the TypeSystemAstBuilder TS:: convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

class StatementBuilder;

// Forward-declared at GLOBAL scope (the nested-namespace trap: a declaration written
// inside namespace CSharp would create CSharp::IL and shadow the real one). This file
// opens the real namespace before namespace ILSpy::Decompiler::CSharp begins.

// The ExpressionBuilder skeleton. The C# `sealed class ExpressionBuilder :
// ILVisitor<TranslationContext, TranslatedExpression>`; the port models the
// visitor as the OpCode-switch `Visit` (the ExpressionTransforms convention) with
// one `VisitXxx` member per ported instruction kind.
class ExpressionBuilder {
public:
    // The C# `new TypeInference(compilation) { Algorithm =
    // TypeInferenceAlgorithm.Improved }` field stand-in: the type-inference
    // instance is a compilation + algorithm pair (the port's TypeInference is
    // free functions threading those parameters -- the header convention); the
    // instance state the C# class carries (`nestingLevel`, `classTypeArguments`)
    // is fresh per call site, so no more state is needed.
    struct TypeInferenceInstance {
        const TS::ICompilation* compilation = nullptr;
        Resolver::TypeInferenceAlgorithm algorithm = Resolver::TypeInferenceAlgorithm::CSharp4;
    };

    // The C# `public ExpressionBuilder(StatementBuilder statementBuilder,
    // IDecompilerTypeSystem typeSystem, ITypeResolveContext decompilationContext,
    // ILFunction currentFunction, DecompilerSettings settings, DecompileRun
    // decompileRun, CancellationToken cancellationToken)`: builds the
    // CSharpResolver over a CSharpTypeResolveContext of the decompilation
    // context's module + the run's using scope + the current type/member, the
    // TypeSystemAstBuilder over it (the four flag assignments), and the
    // Improved-algorithm TypeInference. The C# `Debug.Assert(decompilationContext
    // != null)` is the port's invalid_argument guard (the D424 convention); the
    // IDecompilerTypeSystem surface ports as the ICompilation narrowing (the
    // BamlDecompilerTypeSystem convention -- the port has no IDecompilerTypeSystem
    // interface, so the FindType consumption routes through
    // TypeSystemExtensions over the compilation); the CancellationToken is the
    // documented deferral.
    explicit ExpressionBuilder(const StatementBuilder* statementBuilder,
                               const TS::ICompilation& typeSystem,
                               const TS::ITypeResolveContext& decompilationContext,
                               IL::ILFunction* currentFunction,
                               const DecompilerSettings* settings,
                               const DecompileRun* decompileRun);

    // -- The translation entry family -------------------------------------------------

    // The C# `public AstType ConvertType(IType type)`: the astBuilder's
    // ConvertType with the TypeResolveResult annotation assert.
    Syntax::AstType* ConvertType(TS::IType& type) const;

    // The C# `public ExpressionWithResolveResult ConvertConstantValue(
    // ResolveResult rr, bool allowImplicitConversion = false)`: the astBuilder's
    // ConvertConstantValue with the NullReferenceExpression-cast arm (a null
    // literal over a non-null type), the C#-small-integer cast arm (the literal
    // is in-range so no unchecked annotation is necessary), and the
    // C#-native-integer cast arm; the missing resolve-result annotation on the
    // built expression falls back to `rr` (added before the wrapper is built).
    ExpressionWithResolveResult ConvertConstantValue(
        std::shared_ptr<Sem::ResolveResult> rr, bool allowImplicitConversion = false) const;

    // The C# overload with `displayAsHex`: flips the astBuilder's
    // PrintIntegralValuesAsHex for the call and restores it afterwards (the
    // C# try/finally).
    ExpressionWithResolveResult ConvertConstantValue(
        std::shared_ptr<Sem::ResolveResult> rr, bool allowImplicitConversion, bool displayAsHex) const;

    // The C# `public TranslatedExpression Translate(ILInstruction inst, IType?
    // typeHint = null)`: builds the TranslationContext (the hint or the
    // UnknownType null object), runs the visitor dispatch, and validates the
    // Translate post-condition in debug builds (the C# DEBUG block).
    TranslatedExpression Translate(IL::ILInstruction* inst, const TS::IType* typeHint = nullptr);

    // The C# `public TranslatedExpression TranslateCondition(ILInstruction
    // condition, bool negate = false)`: translate with the Boolean hint, widen
    // through ConvertTo(StackType.I4) when the stack type is wider than 4 bytes
    // (the deferred ConvertTo -- a loud deferral), then ConvertToBoolean.
    TranslatedExpression TranslateCondition(IL::ILInstruction* condition, bool negate = false);

    // The C# `internal TranslatedExpression TranslateTarget(ILInstruction? target,
    // bool nonVirtualInvocation, bool memberStatic, IType memberDeclaringType,
    // IType? constrainedTo = null)` (ExpressionBuilder.cs lines 2734-2844): the
    // call/field TARGET translation -- the base-reference arm over the current
    // type definition's base types, the pointer/ref type-hint machinery for
    // value-type receivers (the `ExpectedTypeForThisPointer == Ref` walk with
    // the issue-#1333 reference-of-the-correct-type conversion), the
    // DirectionExpression and null-conditional unwraps, and the static
    // type-reference arm. `memberDeclaringType` is a non-null reference (the C#
    // parameter has no null check); `constrainedTo` is the optional
    // constrained-prefix type operand.
    TranslatedExpression TranslateTarget(IL::ILInstruction* target, bool nonVirtualInvocation,
                                         bool memberStatic, const TS::IType& memberDeclaringType,
                                         const TS::IType* constrainedTo = nullptr);

    // The C# `private TranslatedExpression EnsureTargetNotNullable(TranslatedExpression
    // expr, ILInstruction inst)` (ExpressionBuilder.cs lines 2846-2873): the whole
    // nullability-annotation body is commented out in the C# source (the TODO for
    // the nullability support that would sprinkle `!` operators), so the member is
    // the identity pass-through. The `inst` parameter is unused there as well.
    TranslatedExpression EnsureTargetNotNullable(TranslatedExpression expr,
                                                 IL::ILInstruction* inst);

    // The C# `bool RequiresQualifier(IMember member, TranslatedExpression target)`
    // (ExpressionBuilder.cs lines 293-301): whether a member reference needs an
    // explicit qualifier (the `AlwaysQualifyMemberReferences` / variable-shadowing
    // gates, the static-member current-or-containing-type check, and the
    // instance-member this/base receiver check). `member` is a non-null reference
    // (the C# parameter has no null check).
    bool RequiresQualifier(const TS::IMember& member, const TranslatedExpression& target) const;

    // The C# `ExpressionWithResolveResult ConvertField(IField field, ILInstruction?
    // targetInstruction = null)` (ExpressionBuilder.cs lines 302-398): the field
    // reference render -- the automatic-event backing-field special case (the field
    // is printed as the field-like event), the target translation, the
    // requires-qualifier decision (made against the backing field's property when
    // PatternStatementTransform will hide the field), the ambiguous-access retry
    // loop (the simple-name lookup, the member lookup, and the declaring-type
    // cast), and the member/identifier access with the by-reference wrap for a
    // ref-typed field.
    ExpressionWithResolveResult ConvertField(const TS::IField& field,
                                             IL::ILInstruction* targetInstruction = nullptr);

    // The C# `bool IsBackingFieldOfAutomaticEvent(IField field,
    // [NotNullWhen(true)] out IEvent? ev)` (ExpressionBuilder.cs lines 400-425):
    // whether the field is the backing field of an automatic (field-like) event
    // whose reference should be printed as the event. Gated on the
    // PropertyAndEventBackingFieldLookup association, the current-accessor
    // self-reference check, the AutoEventDecompiler verdict, and the backing-field
    // identity check. `ev` is null when the field is not such a backing field.
    bool IsBackingFieldOfAutomaticEvent(const TS::IField& field, const TS::IEvent*& ev);

    // -- The visitor-dispatch surface (the C# ILVisitor base) -------------------------

    // The C# double-dispatch: the OpCode switch calling the per-instruction
    // Visit method. Unported arms fall to `Default`.
    TranslatedExpression Visit(IL::ILInstruction* inst, TranslationContext context);

    // The C# `protected override TranslatedExpression Default(ILInstruction inst,
    // TranslationContext context)`: the "OpCode not supported" error expression.
    TranslatedExpression Default(IL::ILInstruction* inst, TranslationContext context);

    // -- The ported Visit arms (the leaf loads/stores and the type-operand family) ------

    TranslatedExpression VisitLdLoc(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdLoca(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdNull(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitDefaultValue(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdStr(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdcI4(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdcI8(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdcF4(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdcF8(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdcDecimal(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitBitNot(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitThrow(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitThreeValuedBoolAnd(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitThreeValuedBoolOr(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitUserDefinedLogicOperator(UserDefinedLogicOperator inst, TranslationContext
    // context)` (ExpressionBuilder.cs lines 1233-1257): the user-defined
    // short-circuiting `&&`/`||` render -- convert both operands to the operator
    // method's parameter types, derive the `&&`/`||` operator from the method name
    // (op_BitwiseAnd/op_BitwiseOr), and emit a BinaryOperatorExpression carrying an
    // InvocationResolveResult.
    TranslatedExpression VisitUserDefinedLogicOperator(IL::ILInstruction* inst,
                                                       TranslationContext context);
    // The type-operand family (the C# lines 434-483 and 712-744): the `isinst`-
    // shaped `is`/`as` expression, `sizeof T`, and `typeof(T).TypeHandle`.
    TranslatedExpression VisitIsInst(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitSizeOf(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitLdTypeToken(IL::ILInstruction* inst, TranslationContext context);
    // The boxing/cast conversion family (the C# lines 3285-3358): the unboxing
    // conversion (`unbox.any`, with the isinst-to-`as` shortcut over nullable
    // value types and reference types), the managed-pointer unboxing (`unbox`),
    // the boxing conversion (`box`), and the explicit cast (`castclass`).
    TranslatedExpression VisitUnbox(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitUnboxAny(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitBox(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitCastClass(IL::ILInstruction* inst, TranslationContext context);
    // The memory-access load/store family (the C# lines 2857-3086): the typed
    // managed/raw load (`ldobj`) and store (`stobj`). VisitLdObj prefers the
    // type hint (except for a pointer hint in the unaligned/ref-address shape),
    // renders the `unaligned.` prefix as `Unsafe.ReadUnaligned<T>` and otherwise
    // dereferences through the LdObj helper. VisitStObj dispatches to the
    // Unsafe.Write/WriteUnaligned helper for a `unaligned.` prefix or a
    // non-ref non-unmanaged target, else dereferences the pointer and renders
    // the assignment (with the `ref (a = ref b)` re-assignment shape).
    TranslatedExpression VisitLdObj(IL::ILInstruction* inst, TranslationContext context);
    TranslatedExpression VisitStObj(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLdLen(LdLen inst,
    // TranslationContext context)` (ExpressionBuilder.cs lines 3088-3116): the array
    // length render -- translate the array with the System.Array type hint (converting
    // a non-array expression to System.Array), pick the `Length`/`LongLength` member
    // name and the Int32/Int64 result type from the load's StackType, look the property
    // up on System.Array, and render `array.Member` with the member resolve result (or a
    // plain Int32/Int64 resolve result when System.Array exposes no such property).
    TranslatedExpression VisitLdLen(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLdElema(LdElema
    // inst, TranslationContext context)` (ExpressionBuilder.cs lines 3203-3229): the
    // array-element-address render -- translate the array (converting a non-array or
    // element-type-mismatched expression to a fresh array of `inst.Type` and the
    // index count), then index through TranslateArrayIndex (or the System.Index
    // conversion when `withsystemindex` is set) and wrap in a `ref` DirectionExpression
    // carrying a ByReferenceResolveResult over the element type.
    TranslatedExpression VisitLdElema(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLdsFlda(LdsFlda
    // inst, TranslationContext context)` (ExpressionBuilder.cs lines 3196-3201): the
    // static field-address render -- resolve the field reference through ConvertField
    // and wrap it in a `ref` DirectionExpression carrying a ByReferenceResolveResult.
    TranslatedExpression VisitLdsFlda(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLdFlda(
    // LdFlda inst, TranslationContext context)` (ExpressionBuilder.cs lines
    // 3118-3194): the `&target.field` render -- the fixed-buffer rewrite
    // (`TupleTransform` + `CSharpDecompiler.IsFixedField`), the tuple-element
    // access, and the base ConvertField render wrapped in a `ref`
    // DirectionExpression (or an `&` UnaryOperatorExpression for a native
    // pointer result).
    TranslatedExpression VisitLdFlda(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitNullableRewrap(NullableRewrap inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 4298-4309): the null-conditional join point --
    // translate the Argument and, when its type is a non-nullable value type, lift
    // it into `Nullable<T>` (NullableType.Create); the render is a
    // NullConditionalRewrap UnaryOperatorExpression carrying a plain ResolveResult of
    // that type.
    TranslatedExpression VisitNullableRewrap(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitNullableUnwrap(NullableUnwrap inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 4311-4321): the `?.` dereference -- translate the
    // Argument; for a RefInput (and non-RefOutput) argument whose render is a ref
    // DirectionExpression, strip the direction (the managed reference is dereferenced
    // by removing the `ref`); the render is a NullConditional UnaryOperatorExpression
    // carrying a plain ResolveResult of the underlying type
    // (NullableType.GetUnderlyingType).
    TranslatedExpression VisitNullableUnwrap(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitNullCoalescingInstruction(NullCoalescingInstruction inst, TranslationContext
    // context)` (ExpressionBuilder.cs lines 3912-3953): the `a ?? b` render -- translate
    // both operands, constant-adjust the fallback to the value's type, resolve the
    // null-coalescing operator, and on an error recover the target type (a throw
    // fallback over NoType uses the value's underlying type, two differing non-null
    // types fall back to `inst.UnderlyingResultType`, else the non-null operand's
    // type) and convert the operands (the nullable wrap for the non-ref kinds); the
    // render is a BinaryOperatorExpression with the NullCoalescing operator.
    TranslatedExpression VisitNullCoalescingInstruction(IL::ILInstruction* inst,
                                                        TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitAddressOf(AddressOf
    // inst, TranslationContext context)` (ExpressionBuilder.cs lines 4231-4266): the
    // `&value` render -- classify the wrapped value (an ILInlining.ClassifyExpression
    // call), translate and convert it to the address's type, and when the value is a
    // mutable lvalue whose address would let a mutating call modify the original
    // (unless the parent is an ldobj) insert a redundant cast so the C# compiler
    // copies; the render is a ref DirectionExpression carrying a
    // ByReferenceResolveResult.
    TranslatedExpression VisitAddressOf(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitRefAnyType(
    // RefAnyType inst, TranslationContext context)` (ExpressionBuilder.cs lines
    // 3386-3394): the `__reftype(typedReference).TypeHandle` render -- the RefType
    // UndocumentedExpression over the translated argument, wrapped in a `TypeHandle`
    // member reference with a TypeResolveResult for System.RuntimeTypeHandle.
    TranslatedExpression VisitRefAnyType(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitMakeRefAny(MakeRefAny inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 3371-3384): the `__makeref(arg)` render -- the
    // translated argument (a DirectionExpression is stripped to its inner
    // expression) as the single argument of a MakeRef UndocumentedExpression,
    // carrying a TypeResolveResult for System.TypedReference.
    TranslatedExpression VisitMakeRefAny(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitRefAnyValue(RefAnyValue inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 3396-3404): the `ref __refvalue(arg, T)` render
    // -- a RefValue UndocumentedExpression over the translated argument and a
    // TypeReferenceExpression for the node's type, wrapped in a ref
    // DirectionExpression with a ByReferenceResolveResult.
    TranslatedExpression VisitRefAnyValue(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitArglist(Arglist
    // inst, TranslationContext context)` (ExpressionBuilder.cs lines 3274-3280):
    // the `__arglist` render -- the ArgListAccess UndocumentedExpression carrying
    // a TypeResolveResult for System.RuntimeArgumentHandle.
    TranslatedExpression VisitArglist(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitIfInstruction(IfInstruction inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 3956-4049): the if-as-expression render -- the
    // short-circuit &&/|| shapes and the `?:` conditional with its type
    // unification and the by-reference result wrap.
    TranslatedExpression VisitIfInstruction(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitSwitchInstruction(SwitchInstruction inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 4176-4229): the switch-expression render -- the
    // switch value through TranslateSwitchValue (expression context), the result
    // type from the type hint or the instruction's stack type, the per-section
    // arm patterns (the null label, the typed case constants, the skipped
    // compiler-generated default), and the `_` default arm.
    TranslatedExpression VisitSwitchInstruction(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression
    // VisitMatchInstruction(MatchInstruction inst, TranslationContext context)`
    // (ExpressionBuilder.cs lines 4989-5005): the `is`-pattern render -- translate
    // the tested operand (unwrapping a boxing cast when the pattern does not need
    // it), translate the pattern through TranslatePattern, and emit a
    // BinaryOperatorExpression with the IsPattern operator carrying a boolean
    // ResolveResult. The pattern translation is the recursive helper below.
    TranslatedExpression VisitMatchInstruction(IL::ILInstruction* inst, TranslationContext context);
    // The C# `ExpressionWithILInstruction TranslatePattern(ILInstruction pattern,
    // IType leftHandType)` (ExpressionBuilder.cs lines 5007-5118): the pattern
    // render -- a MatchInstruction becomes a recursive/declaration/type pattern,
    // a Comp becomes a constant or relational pattern, and a string/decimal
    // op_Equality call becomes the constant pattern's value. The port returns a
    // TranslatedExpression (the C# returns the ExpressionWithILInstruction base,
    // which TranslatedExpression derives from; the port wrappers are flat). The
    // deconstruct-pattern guards throw NotImplementedException in the C# but the
    // port's MatchInstruction node does not carry the deconstruct flags, so the
    // corresponding arms cannot arise.
    TranslatedExpression TranslatePattern(IL::ILInstruction* pattern,
                                          const TS::IType* leftHandType);
    // The C# `protected internal override TranslatedExpression VisitInvalidBranch(
    // InvalidBranch inst, TranslationContext context)` (ExpressionBuilder.cs lines
    // 5121-5133): the ErrorExpression with the "Error" prefix, an optional
    // ' near IL_xxxx' suffix (non-zero StartILOffset), and an optional ': message'.
    TranslatedExpression VisitInvalidBranch(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitInvalidExpression(
    // InvalidExpression inst, TranslationContext context)` (ExpressionBuilder.cs lines
    // 5135-5146): the same error text with the node's Severity as the prefix.
    TranslatedExpression VisitInvalidExpression(IL::ILInstruction* inst, TranslationContext context);
    // The C# `private TranslatedExpression StObjViaHelperCall(StObj inst)`
    // (ExpressionBuilder.cs lines 3087-3125): the `Unsafe.Write` /
    // `Unsafe.WriteUnaligned` intrinsic rewrite for a store that cannot be a
    // plain dereference assignment.
    TranslatedExpression StObjViaHelperCall(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedExpression VisitStLoc(StLoc inst,
    // TranslationContext context)` (ExpressionBuilder.cs lines 809-870): the
    // assignment arm -- the stack-slot type refinement, the by-ref re-assignment
    // `ref (a = ref b)` shape, and the plain Assignment.
    TranslatedExpression VisitStLoc(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitComp(Comp inst,
    // TranslationContext context)` (ExpressionBuilder.cs lines 871-946): the comparison
    // dispatch -- the ThreeValuedLogic lifted-not arm, the Ref arm over the Unsafe
    // AreSame/IsAddressLessThan intrinsics, then TranslateCeq (equality/inequality) or
    // TranslateComp (the relational operators).
    TranslatedExpression VisitComp(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitNewArr(NewArr inst,
    // TranslationContext context)` (ExpressionBuilder.cs lines 502-516): the
    // `new T[...]` array-creation render -- every index through TranslateArrayIndex,
    // the `new int[n][]` ComposedType specifier move, and the ArrayCreateResolveResult
    // over the reconstructed array type (`Empty<ResolveResult>.Array` as the
    // present-but-empty initializer, the C# non-null-empty-list state).
    TranslatedExpression VisitNewArr(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitConv(Conv inst,
    // TranslationContext context)` (ExpressionBuilder.cs lines 2227-2386): the numeric
    // conversion render -- the checked/IntToFloat arm (first normalize the input to
    // the conv's sign, then one direct cast), then the ConversionKind switch:
    // StartGCTracking passthrough, StopGCTracking (the pointer cast for a fixed
    // address, the Unsafe.AsPointer intrinsic otherwise, the integer passthrough for
    // a start-tracking-then-stop), SignExtend/ZeroExtend (normalize the input to the
    // right sign/size and let the caller handle the extension), Nop, Truncate (the
    // small-integer case with its own double truncation, else the caller's), the
    // Invalid unknown->O arm, and the default simple cast with the TypeHint-aware
    // target-type pick.
    TranslatedExpression VisitConv(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLocAlloc(LocAlloc
    // inst, TranslationContext context)` (ExpressionBuilder.cs lines 517-522): the
    // `stackalloc` render -- TranslateLocAlloc's element type plus a pointer resolve
    // result.
    TranslatedExpression VisitLocAlloc(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLocAllocSpan(
    // LocAllocSpan inst, TranslationContext context)` (ExpressionBuilder.cs lines
    // 523-528): the Span<T> stackalloc render -- TranslateLocAllocSpan's element type
    // over the span's own type as the resolve result.
    TranslatedExpression VisitLocAllocSpan(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitLdFtn(LdFtn
    // inst, TranslationContext context)` (ExpressionBuilder.cs lines
    // 4774-4832): the function-pointer render -- the CallBuilder method-group
    // reference, the instance `__ldftn` intrinsic fallback, and the static
    // FunctionPointerType address-of + cast over the method-group conversion.
    TranslatedExpression VisitLdFtn(IL::ILInstruction* inst, TranslationContext context);
    // The C# `VisitLdVirtFtn` (lines 4834-4841): the `__ldvirtftn` intrinsic.
    TranslatedExpression VisitLdVirtFtn(IL::ILInstruction* inst, TranslationContext context);
    // The C# `VisitLdVirtDelegate` (line 497-500): the CallBuilder.Build
    // virtual-delegate delegation.
    TranslatedExpression VisitLdVirtDelegate(IL::ILInstruction* inst, TranslationContext context);
    // The C# `protected internal override TranslatedExpression VisitCall(Call
    // inst, TranslationContext context)` / `VisitCallVirt` siblings
    // (ExpressionBuilder.cs lines 2455-2462): the CallBuilder render over the
    // call's resolved method, wrapped in the byref direction expression when
    // the return type is a by-reference type. The port's one-Call-node model
    // (the reader reuses Call for call/callvirt/newobj with the IsNewObj flag)
    // routes every opcode here; CallBuilder.Build dispatches the newobj shape
    // internally.
    TranslatedExpression VisitCall(IL::ILInstruction* inst, TranslationContext context);
    // The C# `StackAllocExpression TranslateLocAllocSpan(LocAllocSpan inst, IType
    // typeHint, out IType elementType)` (ExpressionBuilder.cs lines 530-539): the
    // span's element type, the count converted to int32, and the StackAllocExpression.
    Syntax::StackAllocExpression* TranslateLocAllocSpan(IL::LocAllocSpan* inst,
                                                        const TS::IType* typeHint,
                                                        TS::ITypePtr& elementType);
    // The C# `StackAllocExpression TranslateLocAlloc(LocAlloc inst, IType typeHint,
    // out IType elementType)` (ExpressionBuilder.cs lines 541-579): the element type
    // from the count's `sizeof` operand, the type hint's pointer element (via
    // GetPointerArithmeticOffset), or the byte fallback, each with the count converted
    // to int32.
    Syntax::StackAllocExpression* TranslateLocAlloc(IL::LocAlloc* inst,
                                                    const TS::IType* typeHint,
                                                    TS::ITypePtr& elementType);
    // The C# `TranslatedExpression EnsureIntegerType(TranslatedExpression expr)`
    // (ExpressionBuilder.cs line 1506): convert non-primitive/non-native-integer
    // types to the arithmetic type of their stack type and sign (pointer arithmetic
    // accepts all primitive integer types, but no enums etc.).
    TranslatedExpression EnsureIntegerType(TranslatedExpression expr);
    // The C# `TranslatedExpression? GetPointerArithmeticOffset(ILInstruction
    // byteOffsetInst, TranslatedExpression byteOffsetExpr, IType
    // pointerElementType, bool checkForOverflow, bool unwrapZeroExtension)`
    // (ExpressionBuilder.cs line 1516): run PointerArithmeticOffset.Detect and
    // translate the detected element-count instruction, keeping the ORIGINAL
    // byte-offset instruction as the annotation.
    std::optional<TranslatedExpression> GetPointerArithmeticOffset(
        IL::ILInstruction* byteOffsetInst, TranslatedExpression byteOffsetExpr,
        const TS::IType* pointerElementType, bool checkForOverflow,
        bool unwrapZeroExtension = false);
    // The C# `TranslatedExpression TranslateArrayIndex(ILInstruction i)` (a private
    // helper, ExpressionBuilder.cs line 3248): translate the index and convert it to
    // its own stack type with allowIntPtr: false.
    TranslatedExpression TranslateArrayIndex(IL::ILInstruction* i);
    // The C# `TranslatedExpression ConvertArrayIndex(TranslatedExpression input,
    // StackType stackType, bool allowIntPtr)` (a private helper, ExpressionBuilder.cs
    // line 3253): the array-index conversion decision tree -- truncate an oversized
    // result to the stack type, pass C# primitive/native integer types through,
    // pass (U)IntPtr through only when allowIntPtr is set, prefer int over the
    // stack type when the input is small, else convert to the stack type's
    // arithmetic type.
    TranslatedExpression ConvertArrayIndex(TranslatedExpression input, IL::StackType stackType,
                                           bool allowIntPtr);
    // The C# `TranslatedExpression IsType(IsInst inst)` helper (ExpressionBuilder.cs
    // line 425): the `expr is T` expression the comp/unbox.any special cases build.
    TranslatedExpression IsType(IL::IsInst& inst);

    // The C# `bool ValueMightBeOversized(ResolveResult rr, StackType stackType)`
    // (ExpressionBuilder.cs lines 2388-2406): whether the resolve result computes a
    // value that might be oversized for the stack type -- only a pointer subtraction
    // under StackType.I is known to fit; everything else might be oversized.
    static bool ValueMightBeOversized(const Sem::ResolveResult& rr, IL::StackType stackType);

    // The C# `TranslatedExpression HandleThreeValuedLogic(BinaryInstruction inst,
    // BinaryOperatorType op, ExpressionType eop)` -- the shared body of the two
    // three-valued-logic arms: convert both operands to bool / Nullable<bool>
    // (the nullable side lifts, the non-nullable side converts through the
    // ConvertTo machinery) and build the LIFTED bitwise operator resolve result.
    // (Declared private in the C#; the port has no visibility levels.)
    TranslatedExpression HandleThreeValuedLogic(IL::BinaryInstruction& inst,
                                               Syntax::BinaryOperatorType op,
                                               TS::ExpressionType eop);

    // -- The value helpers ------------------------------------------------------------

    // The C# `internal ExpressionWithResolveResult ConvertVariable(ILVariable
    // variable)`: the parameter-this / identifier node, the by-ref wrapping (the
    // element resolve result on the identifier + the DirectionExpression over
    // the ByReferenceResolveResult), else the plain ILVariableResolveResult.
    ExpressionWithResolveResult ConvertVariable(const IL::ILVariablePtr& variable);

    // The C# `ExpressionWithResolveResult Assignment(TranslatedExpression left,
    // TranslatedExpression right)` (ExpressionBuilder.cs line 1255): convert the
    // value to the assignment target type (implicit conversions allowed) and
    // build the AssignmentExpression over the Assign OperatorResolveResult.
    ExpressionWithResolveResult Assignment(TranslatedExpression left,
                                           TranslatedExpression right);

    // -- The VisitStLoc stack-slot refinement helpers (the C# local functions) -------

    // The C# `bool CanUseTypeForStackSlot(ILVariable v, IType type)` local
    // function: a stack-slot's declared type may be replaced with the value's
    // type when the variable is single-definition, the type is an "other value
    // type", the variable is a ref slot, or all stores agree on the type.
    bool CanUseTypeForStackSlot(const IL::ILVariable& variable, const TS::IType& type);

    // The C# `bool IsOtherValueType(IType type)` local function: a value type
    // carried on the eval stack as O (the stack slot's widened-object shape).
    static bool IsOtherValueType(const TS::IType& type);

    // The C# `bool AllStoresUseConsistentType(IReadOnlyList<IStoreInstruction>
    // storeInstructions, IType expectedType)` local function: every store to the
    // variable is an StLoc whose value infers to the expected type (compared
    // through the TypeErasure normalization).
    bool AllStoresUseConsistentType(const std::vector<IL::ILInstruction*>& storeInstructions,
                                    const TS::IType& expectedType);

    // The port stand-in for the C# `ILVariable.StoreInstructions` per-variable
    // store list AllStoresUseConsistentType walks: the port keeps no per-variable
    // use lists (the tree owns the instructions), so the lists are gathered by
    // one recursive scan of the current function's live body (every
    // IStoreInstruction-shaped node -- StLoc, MatchInstruction, UsingInstruction,
    // TryCatchHandler, PinnedRegion -- keyed by its Variable) on the first
    // VisitStLoc call of the function. The builder never mutates the tree during
    // translation, so the snapshot stays valid for the whole run; the scan is
    // redone when currentFunction changes.
    const std::vector<IL::ILInstruction*>& StoreInstructionsOf(const IL::ILVariable& variable);
    std::unordered_map<const IL::ILVariable*, std::vector<IL::ILInstruction*>> storeInstructions;
    IL::ILFunction* storeScanFunction = nullptr;

    // The C# `internal ILFunction? ResolveLocalFunction(IMethod method)`
    // (ExpressionBuilder.cs lines 278-292): the local-function lookup -- the
    // method's member definition's ReducedFrom's member definition is matched
    // against every ILFunction ancestor's (this one first) LocalFunctions by
    // their own method's member definition. Null when no ancestor declares the
    // local function (the C# `FirstOrDefault` shape -- the Debug.Assert only
    // covers the call's own assertion that the method IS one).
    IL::ILFunction* ResolveLocalFunction(const TS::IMethod& method) const;

    // The C# `internal bool HidesVariableWithName(string name)` / static overload:
    // whether any enclosing ILFunction (the ancestor walk INCLUDES the function
    // itself) declares a variable or a local function with that name.
    bool HidesVariableWithName(const std::string& name) const;
    static bool HidesVariableWithName(const IL::ILFunction& currentFunction, const std::string& name);

    // The C# `internal bool IsCurrentOrContainingType(ITypeDefinition? type)`
    // (ExpressionBuilder.cs lines 2476-2487): the null-tolerant
    // declaring-chain walk -- `type` is the current type definition or any of
    // its enclosing type definitions.
    bool IsCurrentOrContainingType(const TS::ITypeDefinition* type) const;

    // The C# `internal bool IsBaseTypeOfCurrentType(ITypeDefinition? type)`
    // (ExpressionBuilder.cs lines 2488-2491): whether the decompilation
    // context's current type definition derives from `type` (the
    // GetAllBaseTypeDefinitions extension over the context's slot). The C#
    // NREs when the context carries no current type definition (a null
    // receiver on the extension call); the port answers false there -- the
    // degenerate-stub shape no caller reaches (every decompilation context
    // carries the current type definition).
    bool IsBaseTypeOfCurrentType(const TS::ITypeDefinition* type) const;

    // The C# `internal ExpressionWithResolveResult LogicNot(TranslatedExpression
    // expr)`: the "!" operator, with the implicit-bool-conversion unwrap unless the
    // input type declares a user-defined op_LogicalNot.
    ExpressionWithResolveResult LogicNot(const TranslatedExpression& expr) const;

    // The C# `internal ExpressionWithResolveResult GetDefaultValueExpression(IType
    // type)`: the null literal / (T)null cast / decimal zero / default(T) node
    // shapes per the type's kind.
    ExpressionWithResolveResult GetDefaultValueExpression(TS::IType& type) const;

    // The C# `private bool ShouldDisplayAsHex(long value, IType type)`: the
    // binary-numeric hex-literal gate.
    bool ShouldDisplayAsHex(long long value, const TS::IType& type) const;

    // The C# `private ResolveResult AdjustConstantToType(ResolveResult rr, IType
    // typeHint)`: the lossless constant re-typing (the 0/1 boolean, enum/char/
    // small-integer resolver cast, and pointer-null arms).
    std::shared_ptr<Sem::ResolveResult> AdjustConstantToType(std::shared_ptr<Sem::ResolveResult> rr,
                                                             TS::IType& typeHint) const;

    // -- The comparison family (the VisitComp helpers) --------------------------------

    // The C# `TranslatedExpression AdjustConstantExpressionToType(TranslatedExpression
    // expr, IType typeHint)` (ExpressionBuilder.cs line 3861): the constant re-typing
    // wrapper -- re-render the expression when AdjustConstantToType re-typed the
    // resolve result, else keep the original expression.
    TranslatedExpression AdjustConstantExpressionToType(TranslatedExpression expr,
                                                        TS::IType& typeHint) const;

    // The C# `TranslatedExpression TranslateCeq(Comp inst, out bool negateOutput)`
    // (line 947): the equality/inequality comparison -- the '(e as T) == null'
    // rewrites, the redundant-bool-comparison removal, the pointer-null comparisons,
    // the enum/char literal type unification, the string/delegate-with-null reference
    // special case, and the resolver-driven render with the ConvertTo retries.
    TranslatedExpression TranslateCeq(IL::Comp& inst, bool& negateOutput);

    // The C# `TranslatedExpression TryUniteEqualityOperandType(TranslatedExpression
    // left, TranslatedExpression right)` (line 1098): the enum-flag-check constant
    // adjustment ((enum & EnumType.SomeValue) == 0 renders 0 as an integer) and the
    // plain AdjustConstantExpressionToType(left, right.Type) fallback.
    TranslatedExpression TryUniteEqualityOperandType(TranslatedExpression left,
                                                     TranslatedExpression right) const;

    // The C# `bool IsSpecialCasedReferenceComparisonWithNull(TranslatedExpression lhs,
    // TranslatedExpression rhs)` (line 1111): when comparing a string/delegate with
    // null, the C# compiler generates a reference comparison -- the special case is
    // rendered as a builtin reference comparison rather than a value comparison.
    bool IsSpecialCasedReferenceComparisonWithNull(TranslatedExpression lhs,
                                                   TranslatedExpression rhs) const;

    // The C# `ExpressionWithResolveResult CreateBuiltinBinaryOperator(TranslatedExpression
    // left, BinaryOperatorType type, TranslatedExpression right, bool checkForOverflow
    // = false)` (line 1117): the BinaryOperatorExpression over a fresh
    // OperatorResolveResult with the Linq node type of the operator.
    ExpressionWithResolveResult CreateBuiltinBinaryOperator(TranslatedExpression left,
                                                            Syntax::BinaryOperatorType type,
                                                            TranslatedExpression right,
                                                            bool checkForOverflow = false) const;

    // The C# `TranslatedExpression TranslateComp(Comp inst)` (line 1122): handle the
    // Comp instruction for operators other than equality/inequality -- the pointer-
    // pointer builtin, the arithmetic-argument preparation, the constant adjustment,
    // the sign-corrected conversion through FindArithmeticType, and the object-type
    // Unsafe.As<object, UIntPtr> wrap for StackType.O.
    TranslatedExpression TranslateComp(IL::Comp& inst);

    // -- The arithmetic-type helpers (the FindArithmeticType family) ------------------

    // The C# `IType FindType(StackType stackType, Sign sign)` (a private instance
    // helper): the type for an IL stack type -- nint/nuint when native integers are
    // available for StackType.I (the settings gate), else the Sign-aware lookup
    // through the ReflectionHelper FindType extension.
    TS::ITypePtr FindType(IL::StackType stackType, TS::Sign sign) const;

    // The C# `IType FindArithmeticType(StackType stackType, Sign sign)`: the
    // arithmetic result type -- nint/nuint when native integers are available for
    // StackType.I, else widened to I8 (or the Sign-aware lookup).
    TS::ITypePtr FindArithmeticType(IL::StackType stackType, TS::Sign sign) const;

    // The C# `TranslatedExpression PrepareArithmeticArgument(...)`: the
    // oversize-truncation + IntPtr-conversion entry. The ConvertTo call sites are
    // the deferred machinery -- the overload guards before them are faithful and
    // the loud deferral throws at the first conversion point (no ported consumer
    // reaches it yet).
    TranslatedExpression PrepareArithmeticArgument(TranslatedExpression arg, IL::StackType argStackType,
                                                   TS::Sign sign, bool isLifted) const;

    // -- The binary-numeric family (the VisitBinaryNumericInstruction arm) ------------

    // The C# `protected internal override TranslatedExpression
    // VisitBinaryNumericInstruction(BinaryNumericInstruction inst, TranslationContext
    // context)` (ExpressionBuilder.cs lines 1262-1298): the arithmetic dispatch --
    // every operator through HandleBinaryNumeric, Div first through
    // HandlePointerSubtraction, the shifts through HandleShift; the default arm
    // throws the parameterless ArgumentOutOfRangeException (mapped to
    // std::out_of_range, the ToBinaryOperatorType convention).
    TranslatedExpression VisitBinaryNumericInstruction(IL::ILInstruction* inst,
                                                       TranslationContext context);

    // The C# `TranslatedExpression HandleBinaryNumeric(BinaryNumericInstruction inst,
    // BinaryOperatorType op, TranslationContext context)` (lines 1619-1746): the
    // shared arithmetic render -- the type-hint propagation gate for bitwise ops over
    // mixed input stack types, the managed/plain pointer-arithmetic arms,
    // PrepareArithmeticArgument on both inputs, the `0 - x` unary-minus rewrite,
    // the enum-constant adjustment for bitwise ops, the resolver-driven render with
    // the common-type fallback, the bitwise constant re-render (the hex gate), and
    // the checked/unchecked/constant-overflow annotation tail.
    TranslatedExpression HandleBinaryNumeric(IL::BinaryNumericInstruction& inst,
                                             Syntax::BinaryOperatorType op,
                                             TranslationContext context);

    // The C# `TranslatedExpression HandleShift(BinaryNumericInstruction inst,
    // BinaryOperatorType op, TranslationContext context)` (lines 1849-1912): the
    // shift render -- the small-integer promotion rule, the C# 11 >>> operator
    // selection (the UnsignedRightShift setting + the type-hint sign gate), the
    // sign-preferring cast fallback, and the always-int32 right-hand conversion.
    TranslatedExpression HandleShift(IL::BinaryNumericInstruction& inst,
                                     Syntax::BinaryOperatorType op,
                                     TranslationContext context);

    // The C# `TranslatedExpression? HandlePointerArithmetic(BinaryNumericInstruction
    // inst, TranslatedExpression left, TranslatedExpression right, TranslationContext
    // context)` (lines 1300-1384): the raw-pointer arithmetic -- the type-hint
    // element-type rule over non-primitive/differently-sized element types,
    // GetPointerArithmeticOffset or the byte-pointer fallback, and the
    // ptr +/- int BinaryOperatorExpression over the pointer resolve result.
    std::optional<TranslatedExpression> HandlePointerArithmetic(
        IL::BinaryNumericInstruction& inst, TranslatedExpression left,
        TranslatedExpression right, TranslationContext context);

    // The C# `TranslatedExpression? HandleManagedPointerArithmetic(
    // BinaryNumericInstruction inst, TranslatedExpression left, TranslatedExpression
    // right)` (lines 1386-1484): the managed-pointer (ref) arithmetic -- the
    // ref-ref ByteOffset intrinsic, the ref +/- int Add/Subtract(+ByteOffset)
    // intrinsics over the detected element offset, the int + ref named-argument
    // arms, and the fixed-buffer indexer direction (the FixedBuffers setting plus
    // the LdFlda-of-LdFlda fixed-field shape, rendered as `ref buffer[index]`).
    std::optional<TranslatedExpression> HandleManagedPointerArithmetic(
        IL::BinaryNumericInstruction& inst, TranslatedExpression left,
        TranslatedExpression right);

    // The C# `TranslatedExpression? HandlePointerSubtraction(BinaryNumericInstruction
    // inst)` (lines 1565-1619): the ptr - ptr -> long division render -- the div(sub(a,b), sizeof(T))
    // or div(sub(a,b), constant) pattern over the matching pointer types, with the
    // debug-build divide-by-1 two-pointer arm.
    std::optional<TranslatedExpression> HandlePointerSubtraction(IL::BinaryNumericInstruction& inst);

    // The C# `bool ConstantBinaryOperatorOverflows(BinaryOperatorType op,
    // ResolveResult left, ResolveResult right)` (lines 1842-1847): whether the
    // already-unchecked-resolved constant binary operation overflows in a checked
    // context (the explicit unchecked(...) wrapper gate). The C# GC-reference
    // operands port to the shared handles (the SharedResolveResultAnnotation
    // call-site convention -- ResolveResult is not shared_from_this-able).
    bool ConstantBinaryOperatorOverflows(
        Syntax::BinaryOperatorType op, const std::shared_ptr<Sem::ResolveResult>& left,
        const std::shared_ptr<Sem::ResolveResult>& right) const;

    // The C# `protected internal override TranslatedExpression
    // VisitUserDefinedCompoundAssign(UserDefinedCompoundAssign inst,
    // TranslationContext context)` (ExpressionBuilder.cs lines 1912-1998): the
    // user-defined compound-assignment render -- the string-concat detection
    // (span-based vs the plain `s += value` shape), the Address target kind's
    // LdObj dereference, the checked/unchecked target annotations (the
    // op_Checked... name / the HasCheckedEquivalent twin), the 2-parameter
    // AssignmentExpression render through GetAssignmentOperatorTypeFromMetadata
    // Name, and the 1-parameter UnaryOperatorExpression render through
    // GetUnaryOperatorTypeFromMetadataName (EvaluatesToOldValue = postfix).
    TranslatedExpression VisitUserDefinedCompoundAssign(IL::ILInstruction* inst,
                                                        TranslationContext context);

    // The C# `protected internal override TranslatedExpression
    // VisitNumericCompoundAssign(NumericCompoundAssign inst, TranslationContext
    // context)` (ExpressionBuilder.cs lines 2032-2073): the numeric compound
    // assignment dispatch -- the eight arithmetic/bitwise operators through
    // HandleCompoundAssignment, the two shift operators through HandleCompoundShift
    // (ShiftRight with the sign/small-integer gates that preserve the C# >>> spelling
    // when the setting allows), and the ArgumentOutOfRangeException default arm.
    TranslatedExpression VisitNumericCompoundAssign(IL::ILInstruction* inst,
                                                    TranslationContext context);

    // The C# `TranslatedExpression HandleCompoundAssignment(NumericCompoundAssign
    // inst, AssignmentOperatorType op)` (ExpressionBuilder.cs lines 2075-2165): the
    // arithmetic/bitwise compound assignment -- the Address/LdObj target, the
    // EvaluatesToOldValue postfix ++/-- render, the EvaluatesToNewValue value
    // preparation (the pointer-offset / enum-underlying / underlying-type conversions
    // through the ConvertValue local function), the AssignmentExpression render,
    // and the checked/unchecked overflow annotation tail.
    TranslatedExpression HandleCompoundAssignment(const IL::NumericCompoundAssign& inst,
                                                  Syntax::AssignmentOperatorType op);

    // The C# `TranslatedExpression ConvertValue(TranslatedExpression value, IType
    // targetType)` -- the local function inside HandleCompoundAssignment
    // (ExpressionBuilder.cs lines 2155-2164): the n(u)int collapse (an implicit
    // conversion is kept only for n(u)int) and the nullable wrap of the target.
    TranslatedExpression ConvertValue(TranslatedExpression value, TS::IType& targetType,
                                      bool checkForOverflow);

    // The C# `TranslatedExpression HandleCompoundShift(NumericCompoundAssign inst,
    // AssignmentOperatorType op)` (ExpressionBuilder.cs lines 2167-2188): the shift
    // compound assignment -- the value always converted to int (the C# shift
    // operator's RHS type, nullable-wrapped when the value is nullable), rendered
    // through the resolver's ResolveAssignment.
    TranslatedExpression HandleCompoundShift(const IL::NumericCompoundAssign& inst,
                                             Syntax::AssignmentOperatorType op);

    // -- The self-contained statics ----------------------------------------------------

    // The C# `internal static AssignmentOperatorType?
    // GetAssignmentOperatorTypeFromMetadataName(string name, DecompilerSettings
    // settings)`: the op_Addition..op_RightShift table plus the
    // op_UnsignedRightShift gate on the setting.
    static std::optional<Syntax::AssignmentOperatorType> GetAssignmentOperatorTypeFromMetadataName(
        const std::string& name, const DecompilerSettings& settings);

    // The C# `internal static UnaryOperatorType? GetUnaryOperatorTypeFromMetadataName(
    // string name, bool isPostfix)`: the op_(Checked)Increment/(Checked)Decrement table.
    static std::optional<Syntax::UnaryOperatorType> GetUnaryOperatorTypeFromMetadataName(
        const std::string& name, bool isPostfix);

    // The C# `static bool IsCompatibleWithSign(IType type, Sign sign)`.
    static bool IsCompatibleWithSign(const TS::IType& type, TS::Sign sign);

    // The C# `static bool BinaryOperatorMightCheckForOverflow(BinaryOperatorType op)`.
    static bool BinaryOperatorMightCheckForOverflow(Syntax::BinaryOperatorType op);

    // The C# `static bool AssignmentOperatorMightCheckForOverflow(AssignmentOperatorType op)`.
    static bool AssignmentOperatorMightCheckForOverflow(Syntax::AssignmentOperatorType op);

    // The C# `internal static bool IsUnboxAnyWithIsInst(UnboxAny unboxAny, IType
    // isInstType)`.
    static bool IsUnboxAnyWithIsInst(const IL::UnboxAny& unboxAny, const TS::IType& isInstType);

    // The C# `internal static TranslatedExpression UnwrapBoxingConversion(
    // TranslatedExpression arg)`: strip a boxing cast over System.Object from an
    // `is`/`as` input.
    static TranslatedExpression UnwrapBoxingConversion(TranslatedExpression arg);

    // The C# `internal static TranslatedExpression ChangeDirectionExpressionTo(
    // TranslatedExpression input, ReferenceKind kind, bool isAddressOf)`.
    static TranslatedExpression ChangeDirectionExpressionTo(TranslatedExpression input,
                                                            TS::ReferenceKind kind,
                                                            bool isAddressOf);

    // The C# `static TranslatedExpression ErrorExpression(string message)`: the
    // error node with the multi-line comment trivia.
    static TranslatedExpression ErrorExpression(const std::string& message);

    // The C# `internal TranslatedExpression CallUnsafeIntrinsic(...)`: the
    // System.Unsafe.<name> invocation builder (the Unsafe.As ref-exchange path
    // ConvertTo's ByReference arm needs).
    TranslatedExpression CallUnsafeIntrinsic(const std::string& name,
                                             std::vector<Syntax::Expression*> arguments,
                                             const TS::IType& returnType,
                                             IL::ILInstruction* inst = nullptr,
                                             std::optional<std::vector<TS::ITypePtr>> typeArguments = std::nullopt) const;

    // The C# `TranslatedExpression WrapInRef(Expression expression, IType type)`:
    // the `ref <expr>` node over the element type. (Declared private in the C#;
    // the port's no-visibility-level convention.)
    static TranslatedExpression WrapInRef(Syntax::Expression& expression, const TS::IType& type);

    // The C# `TranslatedExpression WrapInRef(TranslatedExpression expr, IType
    // type)` (ExpressionBuilder.cs lines 2464-2474, the sibling of the static
    // helper above): when `type` is a by-reference type, wraps the translated
    // call in a `ref <expr>` DirectionExpression whose resolve result is a
    // ByReferenceResolveResult over the call's own resolve result; every other
    // type passes the translated expression through unchanged. VisitCall and
    // VisitCallVirt are the call sites.
    static TranslatedExpression WrapInRef(TranslatedExpression expr, const TS::IType& type);

    // The C# `static TranslatedExpression LdcI4(ICompilation compilation, int val)`
    // (a private TranslatedExpression helper): the int32 literal over its constant.
    static TranslatedExpression LdcI4(const TS::ICompilation& compilation, std::int32_t val);

    // The C# `ExpressionWithResolveResult LdObj(ILInstruction address, IType
    // loadType)` (a private helper, ExpressionBuilder.cs lines 2894-2962): the
    // dereference render -- translate the address with the byref/pointer type
    // hint, then either unwrap the managed-reference/`&`-wrapper child, render
    // the `*pointer` dereference, or re-type through ConvertTo (the incompatible
    // pointer arm with the Unsafe.Read<T> intrinsic for a managed load type).
    // Declared private in the C#; the port's no-visibility-level convention.
    ExpressionWithResolveResult LdObj(IL::ILInstruction* address, const TS::IType& loadType);

    // -- The switch-value translation (the switch arms' shared entry) ------------------

    // The C# `internal (TranslatedExpression, IType, StringToInt?)
    // TranslateSwitchValue(SwitchInstruction inst, bool isExpressionContext)`
    // (ExpressionBuilder.cs line 4059): the switch governing value -- the
    // StringToInt arm (a string switch), the governing-type validation over the
    // value's stack type (I8/I4 re-finding, the small-integer range bail), the
    // context-aware ConvertTo, and the C# governing-type compatibility double
    // conversion. The C# tuple ports to the struct below (the StringToInt member
    // name shadows the IL class name after its declaration -- the CaseLabel
    // `Expression()` crux convention; no later use of the type in the struct).
    struct SwitchValueTranslation {
        TranslatedExpression Value;
        const TS::IType* CaseType = nullptr;
        IL::StringToInt* StringToInt = nullptr;
    };
    SwitchValueTranslation TranslateSwitchValue(IL::SwitchInstruction& inst,
                                                bool isExpressionContext);

    // The C# `static IType GetCSharpSwitchGoverningType(IType type)` (inside
    // TranslateSwitchValue): the governing type C# switch allows (the compatible
    // primitive set), else the single op_Implicit conversion's return type when
    // exactly one is switch-compatible, else the type unchanged.
    const TS::IType* GetCSharpSwitchGoverningType(const TS::IType& type) const;

    // -- The field surface (the C# `internal readonly` fields) ------------------------

    const StatementBuilder* statementBuilder = nullptr;
    const TS::ITypeResolveContext* decompilationContext = nullptr;
    IL::ILFunction* currentFunction = nullptr;
    const TS::ICompilation* compilation = nullptr;
    std::shared_ptr<const Resolver::CSharpResolver> resolver;
    // The C# `internal readonly TypeSystemAstBuilder astBuilder` (a GC reference;
    // the port value-holds the builder -- its shared resolver handle keeps the
    // resolver alive, and the builder is stateless beyond its config flags).
    mutable std::optional<Syntax::TypeSystemAstBuilder> astBuilder;
    TypeInferenceInstance typeInference;
    const DecompilerSettings* settings = nullptr;

    // The C# `readonly HashSet<ILVariable> loadedVariablesSet` -- the stack-slot
    // loads VisitStLoc dedups through. Value-identity keyed on the ILVariablePtr
    // (the C# ILVariable reference equality); the VisitStLoc arm that consumes it
    // lands with that slice.
    std::unordered_set<IL::ILVariablePtr> loadedVariablesSet;

    // The decompile run (the C# `DecompileRun decompileRun` is a ctor parameter the
    // builder keeps implicitly through the resolver's using scope; the port also
    // stores the run for the later slices that consult it).
    const DecompileRun* decompileRun = nullptr;
};

// The C# `public static BinaryOperatorType ToBinaryOperatorType(this ComparisonKind
// kind)` (IL/Instructions/Comp.cs line 66): the IL ComparisonKind -> CSharp
// BinaryOperatorType mapping. Ported as a free function beside its only ported
// consumer (the C# extension lives in Comp.cs, whose GetToken already references
// the CSharp::Syntax layer; putting the function here keeps the port's include
// direction -- the IL layer does not see the CSharp::Syntax layer).
Syntax::BinaryOperatorType ToBinaryOperatorType(IL::ComparisonKind kind);

} // namespace ILSpy::Decompiler::CSharp
