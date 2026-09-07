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
// Deferrals (each named at the member that needs it): the full `ConvertTo`
// cast-insertion machinery on TranslatedExpression (the ~350-line C# body -- the
// loud std::logic_error marks the unported arms and its consumers), the heavy
// Visit arms (Call/CallVirt through CallBuilder, the Comp/BinaryNumeric/
// compound-assignment folds, the block-family arms), and the CancellationToken
// (the cooperative-cancel throw is a no-op in the port, the DecompileRun
// convention).

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
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <memory>
#include <string>
#include <unordered_set>

// The forward declarations at GLOBAL scope (the nested-namespace trap: a declaration
// written inside namespace CSharp would create CSharp::IL and shadow the real
// ILSpy::Decompiler::IL). UnboxAny is a ByValueInstruction parameter; BinaryInstruction
// is the HandleThreeValuedLogic parameter type (an incomplete type is fine in a member
// declaration).
namespace ILSpy::Decompiler::IL {
class UnboxAny;
class BinaryInstruction;
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

    // -- The visitor-dispatch surface (the C# ILVisitor base) -------------------------

    // The C# double-dispatch: the OpCode switch calling the per-instruction
    // Visit method. Unported arms fall to `Default`.
    TranslatedExpression Visit(IL::ILInstruction* inst, TranslationContext context);

    // The C# `protected override TranslatedExpression Default(ILInstruction inst,
    // TranslationContext context)`: the "OpCode not supported" error expression.
    TranslatedExpression Default(IL::ILInstruction* inst, TranslationContext context);

    // -- The ported Visit arms (the leaf loads/stores) --------------------------------

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

    // The C# `internal bool HidesVariableWithName(string name)` / static overload:
    // whether any enclosing ILFunction (the ancestor walk INCLUDES the function
    // itself) declares a variable or a local function with that name.
    bool HidesVariableWithName(const std::string& name) const;
    static bool HidesVariableWithName(const IL::ILFunction& currentFunction, const std::string& name);

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

    // The C# `static TranslatedExpression LdcI4(ICompilation compilation, int val)`
    // (a private TranslatedExpression helper): the int32 literal over its constant.
    static TranslatedExpression LdcI4(const TS::ICompilation& compilation, std::int32_t val);

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

} // namespace ILSpy::Decompiler::CSharp
