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

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

using namespace ILSpy::Decompiler::Metadata;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {
// The reader's scratch state: a stack of pending expression trees (the decode
// of stack-machine IL into trees) and the committed stack-slot variables for
// values that survived a flush. For straight-line code there is one block, so
// the slot variables are rarely needed, but the model is preserved.
struct ReaderState {
    std::vector<std::unique_ptr<ILInstruction>> expressionStack;
    // parameter variables (Index = param index) and local variables.
    std::vector<ILVariablePtr> parameters;
    std::vector<ILVariablePtr> locals;
    StackType returnStackType = StackType::Void;

    bool Push(std::unique_ptr<ILInstruction> inst) {
        if (!inst) return false;
        expressionStack.push_back(std::move(inst));
        return true;
    }
    std::unique_ptr<ILInstruction> Pop() {
        if (!expressionStack.empty()) {
            auto inst = std::move(expressionStack.back());
            expressionStack.pop_back();
            return inst;
        }
        // Stack underflow on straight-line code -> bail out (caller returns null).
        return nullptr;
    }
};

// Read a little-endian integer of N bytes at pos (bounds-checked).
bool ReadU8(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint8_t& out) {
    if (pos >= size) return false; out = b[pos]; return true;
}
bool ReadI8(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int8_t& out) {
    if (pos + 1 > size) return false; out = static_cast<std::int8_t>(b[pos]); return true;
}
bool ReadU16(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint16_t& out) {
    if (pos + 2 > size) return false;
    out = static_cast<std::uint16_t>(b[pos]) | (static_cast<std::uint16_t>(b[pos + 1]) << 8);
    return true;
}
bool ReadI32(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::int32_t>(static_cast<std::uint32_t>(b[pos]) |
        (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
        (static_cast<std::uint32_t>(b[pos + 2]) << 16) |
        (static_cast<std::uint32_t>(b[pos + 3]) << 24));
    return true;
}
bool ReadU32(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::uint32_t>(b[pos]) | (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
         (static_cast<std::uint32_t>(b[pos + 2]) << 16) | (static_cast<std::uint32_t>(b[pos + 3]) << 24);
    return true;
}

// Build the parameter variables from the decoded method signature. The implicit
// `this` is parameter 0 for instance methods (the C# ILReader does the same).
void InitParameters(ReaderState& s, const MethodSignature& sig) {
    int first = sig.IsInstance ? 1 : 0;
    int n = static_cast<int>(sig.ParameterTypes.size()) + first;
    s.parameters.resize(n);
    int idx = 0;
    if (sig.IsInstance) {
        auto v = std::make_shared<ILVariable>();
        v->Name = "this";
        v->Kind = VariableKind::Parameter;
        v->Type = nullptr;  // owning type not resolved here; the type system fills it
        v->Index = 0;
        s.parameters[0] = v;
        idx = 1;
    }
    for (const auto& t : sig.ParameterTypes) {
        auto v = std::make_shared<ILVariable>();
        v->Name = "arg_" + std::to_string(idx);
        v->Kind = VariableKind::Parameter;
        v->Type = t;
        v->Index = idx;
        s.parameters[idx] = v;
        ++idx;
    }
}

// Map a decoded method signature's return type to the eval-stack lattice.
StackType ReturnStackTypeOf(const ITypePtr& t) { return StackTypeOf(t); }
} // namespace

std::unique_ptr<ILFunction> ReadStraightLineIL(const MetadataFile& file,
                                               std::uint32_t methodToken,
                                               std::uint32_t rva) {
    if (rva == 0) return nullptr;
    auto body = file.GetMethodBody(rva);
    if (!body.IsValid()) return nullptr;

    // Exception-handler clauses mean control flow we don't model here.
    if (!body.Handlers().empty()) return nullptr;

    auto sigOpt = file.GetMethodSignature(methodToken);
    if (!sigOpt) return nullptr;
    const auto& sig = *sigOpt;

    ReaderState s;
    InitParameters(s, sig);
    s.returnStackType = ReturnStackTypeOf(sig.ReturnType);

    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto* containerPtr = container.get();
    auto block = std::make_unique<Block>();

    const auto* b = body.IL().data();
    std::size_t size = body.IL().size();
    std::size_t pos = 0;

    while (pos < size) {
        std::size_t start = pos;
        ILOpCode op = DecodeOpCode(b, size, pos);
        std::size_t opLen = pos - start;

        switch (op) {
            // ---- constants ----
            case ILOpCode::Ldnull:
                if (!s.Push(std::make_unique<LdNull>())) return nullptr;
                break;
            case ILOpCode::Ldc_i4_0: if (!s.Push(std::make_unique<LdcI4>(0))) return nullptr; break;
            case ILOpCode::Ldc_i4_1: if (!s.Push(std::make_unique<LdcI4>(1))) return nullptr; break;
            case ILOpCode::Ldc_i4_2: if (!s.Push(std::make_unique<LdcI4>(2))) return nullptr; break;
            case ILOpCode::Ldc_i4_3: if (!s.Push(std::make_unique<LdcI4>(3))) return nullptr; break;
            case ILOpCode::Ldc_i4_4: if (!s.Push(std::make_unique<LdcI4>(4))) return nullptr; break;
            case ILOpCode::Ldc_i4_5: if (!s.Push(std::make_unique<LdcI4>(5))) return nullptr; break;
            case ILOpCode::Ldc_i4_6: if (!s.Push(std::make_unique<LdcI4>(6))) return nullptr; break;
            case ILOpCode::Ldc_i4_7: if (!s.Push(std::make_unique<LdcI4>(7))) return nullptr; break;
            case ILOpCode::Ldc_i4_8: if (!s.Push(std::make_unique<LdcI4>(8))) return nullptr; break;
            case ILOpCode::Ldc_i4_m1: if (!s.Push(std::make_unique<LdcI4>(-1))) return nullptr; break;
            case ILOpCode::Ldc_i4_s: {
                std::int8_t v = 0; if (!ReadI8(b, size, pos, v)) return nullptr; pos += 1;
                if (!s.Push(std::make_unique<LdcI4>(static_cast<std::int32_t>(v)))) return nullptr;
                break;
            }
            case ILOpCode::Ldc_i4: {
                std::int32_t v = 0; if (!ReadI32(b, size, pos, v)) return nullptr; pos += 4;
                if (!s.Push(std::make_unique<LdcI4>(v))) return nullptr;
                break;
            }

            // ---- arguments ----
            case ILOpCode::Ldarg_0: case ILOpCode::Ldarg_1:
            case ILOpCode::Ldarg_2: case ILOpCode::Ldarg_3: {
                int idx = static_cast<int>(op) - static_cast<int>(ILOpCode::Ldarg_0);
                if (idx >= static_cast<int>(s.parameters.size())) return nullptr;
                if (!s.Push(std::make_unique<LdLoc>(s.parameters[idx]))) return nullptr;
                break;
            }
            case ILOpCode::Ldarg_s: {
                std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return nullptr; pos += 1;
                if (idx >= s.parameters.size()) return nullptr;
                if (!s.Push(std::make_unique<LdLoc>(s.parameters[idx]))) return nullptr;
                break;
            }

            // ---- locals (the locals signature is not decoded yet; treat as
            // unknown-typed synthetic locals so the tree still builds) ----
            case ILOpCode::Ldloc_0: case ILOpCode::Ldloc_1:
            case ILOpCode::Ldloc_2: case ILOpCode::Ldloc_3: {
                int idx = static_cast<int>(op) - static_cast<int>(ILOpCode::Ldloc_0);
                if (idx >= static_cast<int>(s.locals.size())) {
                    s.locals.resize(idx + 1);
                }
                if (!s.locals[idx]) {
                    auto v = std::make_shared<ILVariable>();
                    v->Name = "V_" + std::to_string(idx);
                    v->Kind = VariableKind::Local;
                    v->Index = idx;
                    s.locals[idx] = v;
                }
                if (!s.Push(std::make_unique<LdLoc>(s.locals[idx]))) return nullptr;
                break;
            }
            case ILOpCode::Stloc_0: case ILOpCode::Stloc_1:
            case ILOpCode::Stloc_2: case ILOpCode::Stloc_3: {
                int idx = static_cast<int>(op) - static_cast<int>(ILOpCode::Stloc_0);
                if (idx >= static_cast<int>(s.locals.size())) s.locals.resize(idx + 1);
                if (!s.locals[idx]) {
                    auto v = std::make_shared<ILVariable>();
                    v->Name = "V_" + std::to_string(idx);
                    v->Kind = VariableKind::Local;
                    v->Index = idx;
                    s.locals[idx] = v;
                }
                auto value = s.Pop();
                if (!value) return nullptr;
                block->Add(std::make_unique<StLoc>(s.locals[idx], std::move(value)));
                break;
            }

            // ---- pop/dup ----
            case ILOpCode::Pop: {
                auto v = s.Pop();
                if (!v) return nullptr;
                // Discard: emit as a statement (the C# uses a pop/leave pattern;
                // for the minimal reader we just drop it from the tree).
                break;
            }

            // ---- call/callvirt/newobj ----
            case ILOpCode::Call: case ILOpCode::Callvirt: case ILOpCode::Newobj: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto callSig = file.GetMethodSignature(tok);
                if (!callSig) return nullptr;
                // Pop arguments in reverse; an instance call's `this` is the
                // lowest argument, so pop (paramCount + IsInstance) values.
                int argCount = static_cast<int>(callSig->ParameterTypes.size()) + (callSig->IsInstance ? 1 : 0);
                if (op == ILOpCode::Newobj) argCount = static_cast<int>(callSig->ParameterTypes.size());
                auto call = std::make_unique<Call>(file.ResolveTokenToString(tok));
                call->ReturnType = StackTypeOf(callSig->ReturnType);
                std::vector<std::unique_ptr<ILInstruction>> args;
                args.reserve(argCount);
                for (int i = 0; i < argCount; ++i) {
                    auto a = s.Pop();
                    if (!a) return nullptr;
                    args.push_back(std::move(a));
                }
                // Reverse so the first-popped (last arg) comes last.
                for (auto it = args.rbegin(); it != args.rend(); ++it) call->AddArg(std::move(*it));
                bool returnsVoid = (callSig->ReturnType &&
                                    callSig->ReturnType->ReflectionName() == "System.Void");
                if (returnsVoid) {
                    block->Add(std::move(call));
                } else {
                    if (!s.Push(std::move(call))) return nullptr;
                }
                break;
            }

            // ---- ldstr (token resolved to a placeholder; #US heap decoding later) ----
            case ILOpCode::Ldstr: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto ld = std::make_unique<LdStr>(file.ResolveTokenToString(tok));
                if (!s.Push(std::move(ld))) return nullptr;
                break;
            }

            // ---- ret ----
            case ILOpCode::Ret: {
                std::unique_ptr<ILInstruction> retVal;
                if (s.returnStackType != StackType::Void) {
                    retVal = s.Pop();
                    if (!retVal) return nullptr;
                }
                block->SetFinal(std::make_unique<Leave>(containerPtr, std::move(retVal)));
                // A trailing ret must be the last instruction; if anything follows,
                // it is unreachable/dead -- stop here (straight-line contract).
                pos = size;
                break;
            }

            // ---- throw ----
            case ILOpCode::Throw: {
                auto exc = s.Pop();
                if (!exc) return nullptr;
                block->SetFinal(std::make_unique<Throw>(std::move(exc)));
                pos = size;
                break;
            }

            case ILOpCode::Nop:
            case ILOpCode::Break:
                // No-op prefixes; ignore.
                break;

            // ---- 64-bit and float constants ----
            case ILOpCode::Ldc_i8: {
                if (pos + 8 > size) return nullptr;
                std::int64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= static_cast<std::int64_t>(b[pos + i]) << (8 * i);
                pos += 8;
                if (!s.Push(std::make_unique<LdcI8>(v))) return nullptr;
                break;
            }
            case ILOpCode::Ldc_r4: {
                if (pos + 4 > size) return nullptr;
                std::uint32_t u = static_cast<std::uint32_t>(b[pos])
                    | (static_cast<std::uint32_t>(b[pos + 1]) << 8)
                    | (static_cast<std::uint32_t>(b[pos + 2]) << 16)
                    | (static_cast<std::uint32_t>(b[pos + 3]) << 24);
                pos += 4;
                float fv; std::memcpy(&fv, &u, sizeof(fv));
                if (!s.Push(std::make_unique<LdcF4>(fv))) return nullptr;
                break;
            }
            case ILOpCode::Ldc_r8: {
                if (pos + 8 > size) return nullptr;
                std::uint64_t u = 0;
                for (int i = 0; i < 8; ++i)
                    u |= static_cast<std::uint64_t>(b[pos + i]) << (8 * i);
                pos += 8;
                double dv; std::memcpy(&dv, &u, sizeof(dv));
                if (!s.Push(std::make_unique<LdcF8>(dv))) return nullptr;
                break;
            }

            // ---- more variable ops ----
            case ILOpCode::Ldloc_s: {
                std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return nullptr; pos += 1;
                if (idx >= s.locals.size()) s.locals.resize(idx + 1);
                if (!s.locals[idx]) {
                    auto v = std::make_shared<ILVariable>();
                    v->Name = "V_" + std::to_string(idx);
                    v->Kind = VariableKind::Local; v->Index = idx;
                    s.locals[idx] = v;
                }
                if (!s.Push(std::make_unique<LdLoc>(s.locals[idx]))) return nullptr;
                break;
            }
            case ILOpCode::Stloc_s: {
                std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return nullptr; pos += 1;
                if (idx >= s.locals.size()) s.locals.resize(idx + 1);
                if (!s.locals[idx]) {
                    auto v = std::make_shared<ILVariable>();
                    v->Name = "V_" + std::to_string(idx);
                    v->Kind = VariableKind::Local; v->Index = idx;
                    s.locals[idx] = v;
                }
                auto value = s.Pop();
                if (!value) return nullptr;
                block->Add(std::make_unique<StLoc>(s.locals[idx], std::move(value)));
                break;
            }
            case ILOpCode::Ldloca_s: {
                std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return nullptr; pos += 1;
                if (idx >= s.locals.size()) s.locals.resize(idx + 1);
                if (!s.locals[idx]) {
                    auto v = std::make_shared<ILVariable>();
                    v->Name = "V_" + std::to_string(idx);
                    v->Kind = VariableKind::Local; v->Index = idx;
                    s.locals[idx] = v;
                }
                if (!s.Push(std::make_unique<LdLoca>(s.locals[idx]))) return nullptr;
                break;
            }
            case ILOpCode::Ldarg: {
                std::uint16_t idx = 0; if (!ReadU16(b, size, pos, idx)) return nullptr; pos += 2;
                if (idx >= s.parameters.size()) return nullptr;
                if (!s.Push(std::make_unique<LdLoc>(s.parameters[idx]))) return nullptr;
                break;
            }
            case ILOpCode::Ldarga: {
                std::uint16_t idx = 0; if (!ReadU16(b, size, pos, idx)) return nullptr; pos += 2;
                if (idx >= s.parameters.size()) return nullptr;
                if (!s.Push(std::make_unique<LdLoca>(s.parameters[idx]))) return nullptr;
                break;
            }
            case ILOpCode::Starg: case ILOpCode::Starg_s: {
                std::uint16_t idx = 0;
                if (op == ILOpCode::Starg_s) {
                    std::uint8_t i8 = 0; if (!ReadU8(b, size, pos, i8)) return nullptr; pos += 1; idx = i8;
                } else {
                    if (!ReadU16(b, size, pos, idx)) return nullptr; pos += 2;
                }
                if (idx >= s.parameters.size()) return nullptr;
                auto value = s.Pop();
                if (!value) return nullptr;
                block->Add(std::make_unique<StLoc>(s.parameters[idx], std::move(value)));
                break;
            }

            // ---- binary arithmetic / bitwise / shift ----
            // Helper: pop two, build a BinaryNumericInstruction, push.
#define IL_BIN(opc, oper) \
    case ILOpCode::opc: { \
        auto r = s.Pop(); auto l = s.Pop(); \
        if (!l || !r) return nullptr; \
        if (!s.Push(std::make_unique<BinaryNumericInstruction>(std::move(l), std::move(r), \
            BinaryNumericOperator::oper, StackType::I4))) return nullptr; \
        break; \
    }
                IL_BIN(Add, Add)
                IL_BIN(Add_ovf, Add)
                IL_BIN(Add_ovf_un, Add)
                IL_BIN(Sub, Sub)
                IL_BIN(Sub_ovf, Sub)
                IL_BIN(Sub_ovf_un, Sub)
                IL_BIN(Mul, Mul)
                IL_BIN(Mul_ovf, Mul)
                IL_BIN(Mul_ovf_un, Mul)
                IL_BIN(Div, Div)
                IL_BIN(Div_un, Div)
                IL_BIN(Rem, Rem)
                IL_BIN(Rem_un, Rem)
                IL_BIN(And, BitAnd)
                IL_BIN(Or, BitOr)
                IL_BIN(Xor, BitXor)
                IL_BIN(Shl, ShiftLeft)
                IL_BIN(Shr, ShiftRight)
                IL_BIN(Shr_un, ShiftRight)
#undef IL_BIN

            // ---- comparisons (ceq/cgt/clt + .un) ----
#define IL_CMP(opc, kind, uns) \
    case ILOpCode::opc: { \
        auto r = s.Pop(); auto l = s.Pop(); \
        if (!l || !r) return nullptr; \
        if (!s.Push(std::make_unique<Comp>(std::move(l), std::move(r), \
            ComparisonKind::kind, uns))) return nullptr; \
        break; \
    }
                IL_CMP(Ceq, Equality, false)
                IL_CMP(Cgt, GreaterThan, false)
                IL_CMP(Cgt_un, GreaterThan, true)
                IL_CMP(Clt, LessThan, false)
                IL_CMP(Clt_un, LessThan, true)
#undef IL_CMP

            // ---- conversions ----
#define IL_CONV(opc, target) \
    case ILOpCode::opc: { \
        auto v = s.Pop(); \
        if (!v) return nullptr; \
        if (!s.Push(std::make_unique<Conv>(std::move(v), StackType::target, false))) return nullptr; \
        break; \
    }
                IL_CONV(Conv_i1, I4)
                IL_CONV(Conv_i2, I4)
                IL_CONV(Conv_i4, I4)
                IL_CONV(Conv_u1, I4)
                IL_CONV(Conv_u2, I4)
                IL_CONV(Conv_u4, I4)
                IL_CONV(Conv_i8, I8)
                IL_CONV(Conv_u8, I8)
                IL_CONV(Conv_r4, F4)
                IL_CONV(Conv_r8, F8)
                IL_CONV(Conv_i, I)
                IL_CONV(Conv_u, I)
                IL_CONV(Conv_r_un, F8)
#undef IL_CONV

            // ---- dup: push a copy of the top ----
            case ILOpCode::Dup: {
                if (s.expressionStack.empty()) return nullptr;
                // Shallow copy via WriteTo/clone is not available; duplicate by
                // taking the top and pushing it twice would double-own. Instead,
                // store the top into a fresh anonymous local and load it twice.
                auto top = s.Pop();
                if (!top) return nullptr;
                auto v = std::make_shared<ILVariable>();
                v->Name = "dup_" + std::to_string(start);
                v->Kind = VariableKind::StackSlot;
                block->Add(std::make_unique<StLoc>(v, std::move(top)));
                if (!s.Push(std::make_unique<LdLoc>(v))) return nullptr;
                if (!s.Push(std::make_unique<LdLoc>(v))) return nullptr;
                break;
            }

            // ---- castclass / isinst / box / unbox / unbox.any ----
            case ILOpCode::Castclass: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto v = s.Pop();
                if (!v) return nullptr;
                if (!s.Push(std::make_unique<CastClass>(type, std::move(v)))) return nullptr;
                break;
            }
            case ILOpCode::Isinst: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto v = s.Pop();
                if (!v) return nullptr;
                if (!s.Push(std::make_unique<IsInst>(type, std::move(v)))) return nullptr;
                break;
            }
            case ILOpCode::Box: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto v = s.Pop();
                if (!v) return nullptr;
                if (!s.Push(std::make_unique<Box>(type, std::move(v)))) return nullptr;
                break;
            }
            case ILOpCode::Unbox: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto v = s.Pop();
                if (!v) return nullptr;
                if (!s.Push(std::make_unique<UnboxAny>(type, std::move(v)))) return nullptr;
                break;
            }
            case ILOpCode::Unbox_any: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto v = s.Pop();
                if (!v) return nullptr;
                if (!s.Push(std::make_unique<UnboxAny>(type, std::move(v)))) return nullptr;
                break;
            }

            // ---- arrays: newarr / ldlen / ldelema / ldelem.* / stelem.* ----
            case ILOpCode::Newarr: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto count = s.Pop();
                if (!count) return nullptr;
                std::vector<std::unique_ptr<ILInstruction>> idx;
                idx.push_back(std::move(count));
                if (!s.Push(std::make_unique<NewArr>(type, std::move(idx)))) return nullptr;
                break;
            }
            case ILOpCode::Ldlen: {
                auto arr = s.Pop();
                if (!arr) return nullptr;
                if (!s.Push(std::make_unique<LdLen>(std::move(arr)))) return nullptr;
                break;
            }
            case ILOpCode::Ldelema: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto idx = s.Pop();
                auto arr = s.Pop();
                if (!arr || !idx) return nullptr;
                std::vector<std::unique_ptr<ILInstruction>> indices;
                indices.push_back(std::move(idx));
                if (!s.Push(std::make_unique<LdElema>(type, std::move(arr), std::move(indices)))) return nullptr;
                break;
            }
#define IL_LDELEM(opc, kt) \
    case ILOpCode::opc: { \
        auto type = std::make_shared<KnownType>(KnownTypeCode::kt); \
        auto idx = s.Pop(); auto arr = s.Pop(); \
        if (!arr || !idx) return nullptr; \
        std::vector<std::unique_ptr<ILInstruction>> indices; \
        indices.push_back(std::move(idx)); \
        auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices)); \
        if (!s.Push(std::make_unique<LdObj>(std::move(addr), type))) return nullptr; \
        break; \
    }
                IL_LDELEM(Ldelem_i1, SByte) IL_LDELEM(Ldelem_u1, Byte)
                IL_LDELEM(Ldelem_i2, Int16) IL_LDELEM(Ldelem_u2, UInt16)
                IL_LDELEM(Ldelem_i4, Int32) IL_LDELEM(Ldelem_u4, UInt32)
                IL_LDELEM(Ldelem_i8, Int64)
                IL_LDELEM(Ldelem_r4, Single) IL_LDELEM(Ldelem_r8, Double)
                IL_LDELEM(Ldelem_i, IntPtr) IL_LDELEM(Ldelem_ref, Object)
#undef IL_LDELEM
            case ILOpCode::Ldelem: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto idx = s.Pop(); auto arr = s.Pop();
                if (!arr || !idx) return nullptr;
                std::vector<std::unique_ptr<ILInstruction>> indices;
                indices.push_back(std::move(idx));
                auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices));
                if (!s.Push(std::make_unique<LdObj>(std::move(addr), type))) return nullptr;
                break;
            }
#define IL_STELEM(opc, kt) \
    case ILOpCode::opc: { \
        auto type = std::make_shared<KnownType>(KnownTypeCode::kt); \
        auto val = s.Pop(); auto idx = s.Pop(); auto arr = s.Pop(); \
        if (!arr || !idx || !val) return nullptr; \
        std::vector<std::unique_ptr<ILInstruction>> indices; \
        indices.push_back(std::move(idx)); \
        auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices)); \
        block->Add(std::make_unique<StObj>(std::move(addr), std::move(val), type)); \
        break; \
    }
                IL_STELEM(Stelem_i, IntPtr) IL_STELEM(Stelem_i1, SByte)
                IL_STELEM(Stelem_i2, Int16) IL_STELEM(Stelem_i4, Int32)
                IL_STELEM(Stelem_i8, Int64) IL_STELEM(Stelem_r4, Single)
                IL_STELEM(Stelem_r8, Double) IL_STELEM(Stelem_ref, Object)
#undef IL_STELEM
            case ILOpCode::Stelem: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto type = file.ResolveTypeToken(tok);
                auto val = s.Pop(); auto idx = s.Pop(); auto arr = s.Pop();
                if (!arr || !idx || !val) return nullptr;
                std::vector<std::unique_ptr<ILInstruction>> indices;
                indices.push_back(std::move(idx));
                auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices));
                block->Add(std::make_unique<StObj>(std::move(addr), std::move(val), type));
                break;
            }
            // The C# represents these as LdObj/StObj over LdFlda/LdsFlda; this port
            // follows that composition so later transforms see the same shape.
            case ILOpCode::Ldfld:
            case ILOpCode::Ldflda:
            case ILOpCode::Stfld:
            case ILOpCode::Ldsfld:
            case ILOpCode::Ldsflda:
            case ILOpCode::Stsfld: {
                std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return nullptr; pos += 4;
                auto fieldType = file.GetFieldSignature(tok);
                std::string fieldName = file.ResolveTokenToString(tok);
                if (op == ILOpCode::Ldfld || op == ILOpCode::Ldflda || op == ILOpCode::Stfld) {
                    // Instance: pop the target object, build LdFlda.
                    auto target = s.Pop();
                    if (!target) return nullptr;
                    auto addr = std::make_unique<LdFlda>(std::move(target), fieldName);
                    addr->DelayExceptions = (op != ILOpCode::Ldflda); // ldfld/stfld defer NRE to LdObj/StObj
                    if (op == ILOpCode::Ldflda) {
                        if (!s.Push(std::move(addr))) return nullptr;
                    } else if (op == ILOpCode::Ldfld) {
                        if (!s.Push(std::make_unique<LdObj>(std::move(addr), fieldType))) return nullptr;
                    } else { // Stfld
                        auto value = s.Pop();
                        if (!value) return nullptr;
                        block->Add(std::make_unique<StObj>(std::move(addr), std::move(value), fieldType));
                    }
                } else {
                    // Static: LdsFlda has no target.
                    auto addr = std::make_unique<LdsFlda>(fieldName);
                    if (op == ILOpCode::Ldsflda) {
                        if (!s.Push(std::move(addr))) return nullptr;
                    } else if (op == ILOpCode::Ldsfld) {
                        if (!s.Push(std::make_unique<LdObj>(std::move(addr), fieldType))) return nullptr;
                    } else { // Stsfld
                        auto value = s.Pop();
                        if (!value) return nullptr;
                        block->Add(std::make_unique<StObj>(std::move(addr), std::move(value), fieldType));
                    }
                }
                break;
            }

            default: {
                // Unsupported (branches, switch, conv.*, binary ops, ldfld, ...) or
                // an opcode needing operand bytes we did not consume. Bail out so the
                // caller degrades to "not straight-line / unsupported".
                return nullptr;
            }
        }
    }

    // If the walk finished without a final control-flow instruction, the method
    // fell through -- not valid IL, or our walk stopped early. Bail.
    if (!block->FinalInstruction) return nullptr;

    // Any pending expressions on the stack at method end are a non-void method
    // that didn't return them, or extra pushes -- not straight-line-clean. Bail.
    if (!s.expressionStack.empty()) return nullptr;

    container->AddBlock(std::move(block));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    // Register the parameters/locals we materialised.
    for (auto& v : s.parameters) if (v) fn->Variables.push_back(v);
    for (auto& v : s.locals) if (v) fn->Variables.push_back(v);

    fn->CheckInvariant(ILPhase::InILReader);
    return fn;
}

} // namespace ILSpy::Decompiler::IL
