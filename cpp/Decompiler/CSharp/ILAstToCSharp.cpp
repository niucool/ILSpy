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

// Implementation of the ILAst-to-C#-text seed. The walker translates the ILAst
// shapes the IL reader produces: blocks flatten into statement lists, branches
// become gotos to IL_XXXX labels (no BlockBuilder region recovery yet), stloc
// declares a `var` on first store, and calls/casts/field/array accesses use
// approximate C# syntax (no resolver, so no using directives, no overload or
// parenthesization decisions). Unknown nodes degrade to a marked-up default
// expression rather than dropping text or crashing.

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::IL {
namespace {

// "Namespace.Type::Member" -> "Namespace.Type.Member"; a trailing "::.ctor"
// marks a constructor reference (the "new" form is added by the caller).
std::string FlattenMetadataName(std::string name) {
    for (std::size_t pos; (pos = name.find("::")) != std::string::npos;)
        name.replace(pos, 2, ".");
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "..") == 0)
        name.erase(name.size() - 1);  // fold ".." left by an empty member segment
    return name;
}

std::string EscapeStringLiteral(std::string_view value) {
    std::string out = "\"";
    char buf[8];
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': out += "\\0"; break;
            default:
                if (c < 0x20 || c > 0x7E) {
                    std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
    return out;
}

std::string TypeDisplayName(const TypeSystem::ITypePtr& type) {
    return type ? type->ReflectionName() : std::string("?");
}

// A C# type name for a local declaration: the C# keyword for primitives
// (int, bool, string, ...), the short type name otherwise (a seed
// approximation -- real C# would carry a using directive). `var` for an
// unknown type (e.g. an untyped stack slot).
std::string CSharpTypeName(const TypeSystem::ITypePtr& type) {
    if (!type) return "var";
    if (auto* k = dynamic_cast<const TypeSystem::KnownType*>(type.get())) {
        switch (k->Code()) {
            case TypeSystem::KnownTypeCode::Boolean: return "bool";
            case TypeSystem::KnownTypeCode::Char: return "char";
            case TypeSystem::KnownTypeCode::SByte: return "sbyte";
            case TypeSystem::KnownTypeCode::Byte: return "byte";
            case TypeSystem::KnownTypeCode::Int16: return "short";
            case TypeSystem::KnownTypeCode::UInt16: return "ushort";
            case TypeSystem::KnownTypeCode::Int32: return "int";
            case TypeSystem::KnownTypeCode::UInt32: return "uint";
            case TypeSystem::KnownTypeCode::Int64: return "long";
            case TypeSystem::KnownTypeCode::UInt64: return "ulong";
            case TypeSystem::KnownTypeCode::Single: return "float";
            case TypeSystem::KnownTypeCode::Double: return "double";
            case TypeSystem::KnownTypeCode::Decimal: return "decimal";
            case TypeSystem::KnownTypeCode::String: return "string";
            case TypeSystem::KnownTypeCode::Object: return "object";
            case TypeSystem::KnownTypeCode::IntPtr: return "nint";
            case TypeSystem::KnownTypeCode::UIntPtr: return "nuint";
            default: break;
        }
    }
    std::string rn = type->ReflectionName();
    auto pos = rn.rfind('.');
    return pos != std::string::npos ? rn.substr(pos + 1) : rn;
}

const char* ConvTargetName(StackType target) {
    switch (target) {
        case StackType::I4: return "int";
        case StackType::I8: return "long";
        case StackType::I: return "nint";
        case StackType::F4: return "float";
        case StackType::F8: return "double";
        default: return "int";
    }
}

// Emit an ILFunction body as C#-ish text. See the file header for the level
// of fidelity this seed aims for.
class CEmitter {
public:
    explicit CEmitter(std::string& out) : out_(out) {}

    void EmitMethod(const ILFunction& fn, std::string_view returnType,
                    std::string_view methodName, std::string_view paramDecl) {
        out_ += returnType;
        out_ += ' ';
        out_ += methodName;
        out_ += '(';
        out_ += paramDecl;
        out_ += ")\n{\n";
        if (fn.Body) {
            CollectLabels(fn.Body.get());
            EmitContainer(*fn.Body, 1);
        }
        out_ += "}\n";
    }

private:
    std::string& out_;
    std::set<std::string> declared_;          // locals already introduced with `var`
    std::map<const Block*, std::string> labels_;  // branch-target block -> IL_XXXX

    void Line(int indent, std::string_view text) {
        out_.append(static_cast<std::size_t>(indent) * 4, ' ');
        out_ += text;
        out_ += '\n';
    }

    static std::string LabelFor(std::uint32_t offset) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "IL_%04X", offset);
        return buf;
    }

    // Every branch-target block gets an IL_XXXX label; walk the whole tree so
    // branches nested in if/switch arms are covered too.
    void CollectLabels(const ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Branch) {
            const auto* br = static_cast<const Branch*>(inst);
            if (br->TargetBlock && labels_.find(br->TargetBlock) == labels_.end())
                labels_[br->TargetBlock] = LabelFor(br->TargetOffset);
        }
        for (int i = 0; i < inst->ChildCount(); ++i) CollectLabels(inst->GetChild(i));
    }

    std::string GotoText(const Branch& br) const {
        if (br.TargetBlock) {
            auto it = labels_.find(br.TargetBlock);
            if (it != labels_.end()) return "goto " + it->second + ";";
        }
        return "goto " + LabelFor(br.TargetOffset) + ";";
    }

    void EmitContainer(const BlockContainer& container, int indent) {
        for (const auto& block : container.Blocks) {
            if (block) EmitBlock(*block, indent);
        }
    }

    void EmitBlock(const Block& block, int indent) {
        auto label = labels_.find(&block);
        if (label != labels_.end()) {
            // C# labels start in column 0 by convention.
            out_ += label->second;
            out_ += ":\n";
        }
        for (const auto& inst : block.Instructions) {
            if (inst && inst->Op != OpCode::Nop) EmitStatement(*inst, indent);
        }
        if (block.FinalInstruction) EmitStatement(*block.FinalInstruction, indent);
    }

    // A non-Block arm of if/try: brace it at this indent.
    void EmitBraced(const ILInstruction& inst, int indent) {
        Line(indent, "{");
        if (inst.Op == OpCode::Block) {
            EmitBlock(static_cast<const Block&>(inst), indent + 1);
        } else if (inst.Op == OpCode::BlockContainer) {
            EmitContainer(static_cast<const BlockContainer&>(inst), indent + 1);
        } else {
            EmitStatement(inst, indent + 1);
        }
        Line(indent, "}");
    }

    void EmitStatement(const ILInstruction& inst, int indent) {
        switch (inst.Op) {
            case OpCode::StLoc: {
                const auto& st = static_cast<const StLoc&>(inst);
                std::string name = st.Variable ? st.Variable->Name : "?";
                bool declare = st.Variable && st.Variable->Kind != VariableKind::Parameter &&
                               declared_.insert(name).second;
                // `V = V op expr` -> `V op= expr` (or `V++`/`V--` for +/- 1) when
                // the binary's left is a load of the same variable. A declaration
                // (`var V = ...`) is never a compound assignment.
                std::string assign = declare ? " = " + Expr(*st.Value) : AssignmentText(st, name);
                std::string decl = declare ? CSharpTypeName(st.Variable->Type) + " " + name : name;
                Line(indent, decl + assign + ";");
                return;
            }
            case OpCode::Call: {
                Line(indent, CtorCallStatementText(static_cast<const Call&>(inst)) + ";");
                return;
            }
            case OpCode::StObj: {
                const auto& st = static_cast<const StObj&>(inst);
                Line(indent, StoreTargetText(*st.Target) + " = " + Expr(*st.Value) + ";");
                return;
            }
            case OpCode::Throw: {
                const auto& th = static_cast<const Throw&>(inst);
                Line(indent, "throw " + (th.Argument ? Expr(*th.Argument) : std::string("(rethrow)")) + ";");
                return;
            }
            case OpCode::Rethrow:
                Line(indent, "throw;");
                return;
            case OpCode::Leave: {
                const auto& leave = static_cast<const Leave&>(inst);
                if (leave.Value) Line(indent, "return " + Expr(*leave.Value) + ";");
                else Line(indent, "return;");
                return;
            }
            case OpCode::Branch:
                Line(indent, GotoText(static_cast<const Branch&>(inst)));
                return;
            case OpCode::IfInstruction: {
                const auto& iff = static_cast<const IfInstruction&>(inst);
                std::string cond = iff.Condition ? Expr(*iff.Condition) : "(default)";
                if (!iff.FalseInst && iff.TrueInst && iff.TrueInst->Op == OpCode::Branch) {
                    Line(indent, "if (" + cond + ") " +
                         GotoText(*static_cast<const Branch*>(iff.TrueInst.get())));
                    return;
                }
                Line(indent, "if (" + cond + ")");
                if (iff.TrueInst) EmitBraced(*iff.TrueInst, indent);
                else Line(indent, "{ }");
                if (iff.FalseInst) {
                    Line(indent, "else");
                    EmitBraced(*iff.FalseInst, indent);
                }
                return;
            }
            case OpCode::SwitchInstruction: {
                const auto& sw = static_cast<const SwitchInstruction&>(inst);
                Line(indent, "switch (" + (sw.Value ? Expr(*sw.Value) : std::string("(default)")) + ")");
                Line(indent, "{");
                for (const auto& section : sw.Sections) {
                    if (!section) continue;
                    if (section->Labels.empty()) {
                        Line(indent + 1, "default:");
                    } else {
                        for (std::int64_t label : section->Labels)
                            Line(indent + 1, "case " + std::to_string(label) + ":");
                    }
                    if (section->Body) EmitStatement(*section->Body, indent + 1);
                }
                Line(indent, "}");
                return;
            }
            case OpCode::TryCatch: {
                const auto& tc = static_cast<const TryCatch&>(inst);
                Line(indent, "try");
                if (tc.TryBlock) EmitBraced(*tc.TryBlock, indent); else Line(indent, "{ }");
                for (const auto& handler : tc.Handlers) {
                    if (!handler) continue;
                    std::string head = "catch";
                    if (handler->Variable) {
                        head += " (" + (handler->Variable->Type
                                        ? handler->Variable->Type->ReflectionName()
                                        : std::string("System.Exception")) +
                                " " + handler->Variable->Name + ")";
                        // A plain catch carries the constant filter ldc.i4(1);
                        // don't print it as a `when` clause.
                        bool isAlwaysTrue = false;
                        if (auto* one = dynamic_cast<const LdcI4*>(handler->Filter.get()))
                            isAlwaysTrue = (one->Value == 1);
                        if (handler->Filter && !isAlwaysTrue)
                            head += " when (" + Expr(*handler->Filter) + ")";
                    }
                    Line(indent, head);
                    if (handler->Body) EmitBraced(*handler->Body, indent); else Line(indent, "{ }");
                }
                return;
            }
            case OpCode::TryFinally: {
                const auto& tf = static_cast<const TryFinally&>(inst);
                Line(indent, "try");
                if (tf.TryBlock) EmitBraced(*tf.TryBlock, indent); else Line(indent, "{ }");
                Line(indent, "finally");
                if (tf.FinallyBlock) EmitBraced(*tf.FinallyBlock, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::TryFault: {
                const auto& tf = static_cast<const TryFault&>(inst);
                Line(indent, "try");
                if (tf.TryBlock) EmitBraced(*tf.TryBlock, indent); else Line(indent, "{ }");
                Line(indent, "fault");
                if (tf.FaultBlock) EmitBraced(*tf.FaultBlock, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::Block:
                EmitBraced(inst, indent);
                return;
            case OpCode::BlockContainer:
                EmitContainer(static_cast<const BlockContainer&>(inst), indent);
                return;
            case OpCode::Nop:
                return;
            default:
                Line(indent, "/* unhandled statement op " +
                             std::to_string(static_cast<int>(inst.Op)) + " */;");
                return;
        }
    }

    // "Namespace.Type::.ctor" -> "new Namespace.Type(args)"; other members and
    // plain methods -> "Namespace.Type.Member(args)".
    std::string CallText(const Call& call) {
        std::string name = call.MethodName;
        std::string prefix;
        static const std::string ctorSuffix = "::.ctor";
        static const std::string cctorSuffix = "::.cctor";
        if (name.size() > ctorSuffix.size() &&
            name.compare(name.size() - ctorSuffix.size(), ctorSuffix.size(), ctorSuffix) == 0) {
            name.erase(name.size() - ctorSuffix.size());
            prefix = "new ";
        } else if (name.size() > cctorSuffix.size() &&
                   name.compare(name.size() - cctorSuffix.size(), cctorSuffix.size(), cctorSuffix) == 0) {
            name.erase(name.size() - cctorSuffix.size());
            prefix = "new ";
        }
        std::string text = prefix + FlattenMetadataName(std::move(name)) + "(";
        for (std::size_t i = 0; i < call.Arguments.size(); ++i) {
            if (i) text += ", ";
            text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
        }
        text += ')';
        return text;
    }

    // The short method name (after "::") for an instance call: receiver.Method.
    static std::string ShortMethodName(std::string_view full) {
        auto pos = full.rfind("::");
        return pos != std::string_view::npos ? std::string(full.substr(pos + 2)) : std::string(full);
    }

    // An instance call (call/callvirt, not newobj) renders as
    // `receiver.Method(restArgs)`; the receiver is Arguments[0]. A ref/deref
    // receiver (`&V`, `*(&V)`) is parenthesized so the member access binds.
    // Static calls and newobj go through CallText.
    std::string InstanceCallText(const Call& call) {
        if (call.Arguments.empty() || !call.Arguments[0])
            return CallText(call);
        std::string recv = Expr(*call.Arguments[0]);
        // A ref/deref receiver renders with a leading `&` or `*`, which binds
        // looser than `.` -- parenthesize so the member access wins. An array
        // element (`values[0]`) or a plain load needs no parens.
        bool needsParens = !recv.empty() && (recv[0] == '&' || recv[0] == '*');
        std::string text = (needsParens ? "(" + recv + ")" : recv) +
                           "." + ShortMethodName(call.MethodName) + "(";
        for (std::size_t i = 1; i < call.Arguments.size(); ++i) {
            if (i > 1) text += ", ";
            text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
        }
        text += ')';
        return text;
    }

    // A `.ctor` call used as a statement (void, in a block) whose first arg is
    // `this` is a base/sibling constructor call; render it as `base(args)` (the
    // `this` arg is implicit). A newobj (which pushes the new object) is an
    // expression and goes through CallText as `new Type(args)`. A `.ctor`
    // statement without a `this` first arg (e.g. a synthetic test) falls back
    // to `new Type(args)`.
    std::string CtorCallStatementText(const Call& call) {
        std::string name = call.MethodName;
        static const std::string ctorSuffix = "::.ctor";
        if (name.size() > ctorSuffix.size() &&
            name.compare(name.size() - ctorSuffix.size(), ctorSuffix.size(), ctorSuffix) == 0 &&
            !call.Arguments.empty() && call.Arguments[0] &&
            call.Arguments[0]->Op == OpCode::LdLoc) {
            auto& ld = static_cast<const LdLoc&>(*call.Arguments[0]);
            if (ld.Variable && ld.Variable->Name == "this") {
                std::string text = "base(";
                for (std::size_t i = 1; i < call.Arguments.size(); ++i) {
                    if (i > 1) text += ", ";
                    text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
                }
                text += ')';
                return text;
            }
        }
        return call.IsInstanceCall ? InstanceCallText(call) : CallText(call);
    }

    // `V = V op expr` -> `V op= expr` (or `V++`/`V--` for +/- 1) when the
    // binary's left is a load of the same variable. A plain `V = expr` (no
    // self-load, or an operator C# has no compound form for) returns ` = expr`.
    std::string AssignmentText(const StLoc& st, const std::string& name) {
        (void)name;
        auto* bin = dynamic_cast<const BinaryNumericInstruction*>(st.Value.get());
        if (!bin || !bin->Left || bin->Left->Op != OpCode::LdLoc) return " = " + Expr(*st.Value);
        auto* ld = static_cast<const LdLoc*>(bin->Left.get());
        if (!ld->Variable || !st.Variable || ld->Variable.get() != st.Variable.get())
            return " = " + Expr(*st.Value);
        const char* op = nullptr;
        switch (bin->Operator) {
            case BinaryNumericOperator::Add: op = "+"; break;
            case BinaryNumericOperator::Sub: op = "-"; break;
            case BinaryNumericOperator::Mul: op = "*"; break;
            case BinaryNumericOperator::Div: op = "/"; break;
            case BinaryNumericOperator::Rem: op = "%"; break;
            case BinaryNumericOperator::BitAnd: op = "&"; break;
            case BinaryNumericOperator::BitOr: op = "|"; break;
            case BinaryNumericOperator::BitXor: op = "^"; break;
            case BinaryNumericOperator::ShiftLeft: op = "<<"; break;
            case BinaryNumericOperator::ShiftRight: op = ">>"; break;
            default: return " = " + Expr(*st.Value);
        }
        if ((bin->Operator == BinaryNumericOperator::Add || bin->Operator == BinaryNumericOperator::Sub) &&
            bin->Right && bin->Right->Op == OpCode::LdcI4 &&
            static_cast<const LdcI4*>(bin->Right.get())->Value == 1) {
            return bin->Operator == BinaryNumericOperator::Add ? "++" : "--";
        }
        return " " + std::string(op) + "= " + (bin->Right ? Expr(*bin->Right) : std::string("(default)"));
    }

    // The C# form of a store target: a field reference, an array element, or
    // a dereferenced pointer.
    std::string StoreTargetText(const ILInstruction& target) {
        if (target.Op == OpCode::LdFlda) {
            const auto& f = static_cast<const LdFlda&>(target);
            std::string field = FlattenMetadataName(f.FieldName);
            std::string obj = f.Target ? Expr(*f.Target) : "(default)";
            return obj == "this" ? "this." + SimpleName(field) : obj + "." + SimpleName(field);
        }
        if (target.Op == OpCode::LdsFlda) {
            return FlattenMetadataName(static_cast<const LdsFlda&>(target).FieldName);
        }
        if (target.Op == OpCode::LdElema) return ElementAccess(static_cast<const LdElema&>(target));
        return "*(" + Expr(target) + ")";
    }

    // "Namespace.Type::name" -> "name" (the field/member segment).
    static std::string SimpleName(const std::string& flattened) {
        auto dot = flattened.rfind('.');
        return dot == std::string::npos ? flattened : flattened.substr(dot + 1);
    }

    std::string ElementAccess(const LdElema& elema) {
        std::string text = elema.Array ? Expr(*elema.Array) : "(default)";
        text += '[';
        for (std::size_t i = 0; i < elema.Indices.size(); ++i) {
            if (i) text += ", ";
            text += elema.Indices[i] ? Expr(*elema.Indices[i]) : "(default)";
        }
        text += ']';
        return text;
    }

    std::string Expr(const ILInstruction& inst) {
        switch (inst.Op) {
            case OpCode::LdLoc: {
                const auto& ld = static_cast<const LdLoc&>(inst);
                return ld.Variable ? ld.Variable->Name : "?";
            }
            case OpCode::LdLoca: {
                const auto& ld = static_cast<const LdLoca&>(inst);
                return "&" + (ld.Variable ? ld.Variable->Name : std::string("?"));
            }
            case OpCode::LdcI4:
                return std::to_string(static_cast<const LdcI4&>(inst).Value);
            case OpCode::LdcI8:
                return std::to_string(static_cast<const LdcI8&>(inst).Value);
            case OpCode::LdcF4: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.9g", static_cast<const LdcF4&>(inst).Value);
                return std::string(buf) + "f";
            }
            case OpCode::LdcF8: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", static_cast<const LdcF8&>(inst).Value);
                return buf;
            }
            case OpCode::LdStr:
                return EscapeStringLiteral(static_cast<const LdStr&>(inst).Value);
            case OpCode::LdNull:
                return "null";
            case OpCode::Call:
                return static_cast<const Call&>(inst).IsInstanceCall
                    ? InstanceCallText(static_cast<const Call&>(inst))
                    : CallText(static_cast<const Call&>(inst));
            case OpCode::Comp: {
                const auto& comp = static_cast<const Comp&>(inst);
                const char* op = "==";
                switch (comp.Kind) {
                    case ComparisonKind::Equality: op = "=="; break;
                    case ComparisonKind::Inequality: op = "!="; break;
                    case ComparisonKind::LessThan: op = "<"; break;
                    case ComparisonKind::LessThanOrEqual: op = "<="; break;
                    case ComparisonKind::GreaterThan: op = ">"; break;
                    case ComparisonKind::GreaterThanOrEqual: op = ">="; break;
                }
                return "(" + (comp.Left ? Expr(*comp.Left) : "(default)") + " " + op + " " +
                       (comp.Right ? Expr(*comp.Right) : "(default)") + ")";
            }
            case OpCode::BinaryNumericInstruction: {
                const auto& bin = static_cast<const BinaryNumericInstruction&>(inst);
                const char* op = "+";
                switch (bin.Operator) {
                    case BinaryNumericOperator::Add: op = "+"; break;
                    case BinaryNumericOperator::Sub: op = "-"; break;
                    case BinaryNumericOperator::Mul: op = "*"; break;
                    case BinaryNumericOperator::Div: op = "/"; break;
                    case BinaryNumericOperator::Rem: op = "%"; break;
                    case BinaryNumericOperator::BitAnd: op = "&"; break;
                    case BinaryNumericOperator::BitOr: op = "|"; break;
                    case BinaryNumericOperator::BitXor: op = "^"; break;
                    case BinaryNumericOperator::ShiftLeft: op = "<<"; break;
                    case BinaryNumericOperator::ShiftRight: op = ">>"; break;
                    default: op = "?"; break;
                }
                std::string text = "(" + (bin.Left ? Expr(*bin.Left) : "(default)") + " " + op + " " +
                                   (bin.Right ? Expr(*bin.Right) : "(default)") + ")";
                if (bin.CheckForOverflow) text = "checked" + text;
                return text;
            }
            case OpCode::Conv: {
                const auto& conv = static_cast<const Conv&>(inst);
                // conv.i4(ldlen) is the IL for `array.Length` (ldlen returns
                // unsigned int32; the cast to signed i4 is implicit in C#).
                if (conv.TargetStackType == StackType::I4 && conv.Argument &&
                    conv.Argument->Op == OpCode::LdLen) {
                    return Expr(*conv.Argument);
                }
                return "(" + std::string(ConvTargetName(conv.TargetStackType)) + ")(" +
                       (conv.Argument ? Expr(*conv.Argument) : "(default)") + ")";
            }
            case OpCode::CastClass: {
                const auto& cast = static_cast<const CastClass&>(inst);
                return "(" + TypeDisplayName(cast.Type) + ")(" +
                       (cast.Argument ? Expr(*cast.Argument) : "(default)") + ")";
            }
            case OpCode::UnboxAny: {
                const auto& unbox = static_cast<const UnboxAny&>(inst);
                return "(" + TypeDisplayName(unbox.Type) + ")(" +
                       (unbox.Argument ? Expr(*unbox.Argument) : "(default)") + ")";
            }
            case OpCode::Box:
                // Boxing is implicit in C#.
                return static_cast<const Box&>(inst).Argument
                           ? Expr(*static_cast<const Box&>(inst).Argument)
                           : "(default)";
            case OpCode::IsInst: {
                const auto& isinst = static_cast<const IsInst&>(inst);
                return "(" + (isinst.Argument ? Expr(*isinst.Argument) : "(default)") +
                       " as " + TypeDisplayName(isinst.Type) + ")";
            }
            case OpCode::LdLen: {
                const auto& ld = static_cast<const LdLen&>(inst);
                return (ld.Argument ? Expr(*ld.Argument) : "(default)") + std::string(".Length");
            }
            case OpCode::LdObj: {
                const auto& ld = static_cast<const LdObj&>(inst);
                return ld.Target ? LoadTargetText(*ld.Target)
                                 : std::string("(default)");
            }
            case OpCode::LdFlda: {
                const auto& f = static_cast<const LdFlda&>(inst);
                std::string field = FlattenMetadataName(f.FieldName);
                std::string obj = f.Target ? Expr(*f.Target) : "(default)";
                return obj == "this" ? "this." + SimpleName(field) : obj + "." + SimpleName(field);
            }
            case OpCode::LdsFlda:
                return FlattenMetadataName(static_cast<const LdsFlda&>(inst).FieldName);
            case OpCode::LdElema:
                return ElementAccess(static_cast<const LdElema&>(inst));
            case OpCode::NewArr: {
                const auto& arr = static_cast<const NewArr&>(inst);
                std::string text = "new " + TypeDisplayName(arr.Type) + "[";
                for (std::size_t i = 0; i < arr.Indices.size(); ++i) {
                    if (i) text += ", ";
                    text += arr.Indices[i] ? Expr(*arr.Indices[i]) : "(default)";
                }
                text += ']';
                return text;
            }
            case OpCode::LdFtn:
                return FlattenMetadataName(static_cast<const LdFtn&>(inst).MethodName);
            case OpCode::LdVirtFtn:
                return FlattenMetadataName(static_cast<const LdVirtFtn&>(inst).MethodName);
            case OpCode::SizeOf:
                return "sizeof(" + static_cast<const SizeOf&>(inst).TypeName + ")";
            case OpCode::LdTypeToken:
                return "typeof(" + FlattenMetadataName(static_cast<const LdTypeToken&>(inst).TokenName) + ")";
            default:
                return "(default)/*op=" + std::to_string(static_cast<int>(inst.Op)) + "*/";
        }
    }

    // The expression form of a load target (LdObj child): field, array
    // element, or dereference.
    std::string LoadTargetText(const ILInstruction& target) {
        if (target.Op == OpCode::LdFlda || target.Op == OpCode::LdsFlda ||
            target.Op == OpCode::LdElema) {
            return Expr(target);
        }
        return "*(" + Expr(target) + ")";
    }
};

} // namespace

std::string ILAstToCSharp(const ILFunction& fn,
                          std::string_view returnType,
                          std::string_view methodName,
                          std::string_view paramDecl) {
    std::string out;
    CEmitter emitter(out);
    emitter.EmitMethod(fn, returnType, methodName, paramDecl);
    return out;
}

} // namespace ILSpy::Decompiler::IL
