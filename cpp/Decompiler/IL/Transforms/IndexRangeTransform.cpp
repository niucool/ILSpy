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

// Port of ICSharpCode.Decompiler/IL/Transforms/IndexRangeTransform.cs -- the
// C# 8 System.Index / System.Range feature recovery. The transform matches
//   stloc len(get_Length(ldloc container))          (optional)
//   stloc range(rangeVarInit)                       (optional, `var r = ...`)
//   stloc offset(GetOffset / sub(len, i))           (the start offset)
//   complex_expr(get_Item(container, offset) | Slice(container, off, len))
// and rewrites the access into a SyntheticRangeIndexAccessor call
// (`container[System.Index]` / `container[Range]`) for the C# renderer.
//
// Ported: HandleLdElema (the `array[^i]` / `array[GetOffset]` ldelema case,
// the expression-transform hook), the Run driver with TransformIndexing /
// TransformSlicing, MatchGetOffset, MatchSliceLength,
// MatchContainerLengthStore / MatchContainerLength / MatchContainerVar,
// IsSlicingMethod, CheckContainerLengthVariableUseCount,
// MatchIndexImplicitConv, MatchIndexFromRange, MakeIndex / MakeRange, the
// IndexMethods member scan, and CSharpWillGenerateIndexer. The C# local
// functions (closing over the match state) port to an explicit state struct
// with member functions.
//
// Deferred with its surface: ExtendSlicing (the second-pass extension that
// merges a previously-built partial `newobj Range(...)` pattern into a full
// range expression; it re-runs over already-rewritten statements and needs
// the Ancestors walk over the container-length load sites). The IL-range
// accumulation (the C# AddILRange calls over the folded instructions) is a
// sequence-point detail the port leaves to the surviving instruction's own
// range.

#include "Decompiler/IL/Transforms/IndexRangeTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/SyntheticRangeIndexAccessor.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>
#include <functional>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::IL {

namespace {

// The port's stand-in for the C# `IInstructionWithVariableOperand` load probe:
// whether the node is an ldloc/ldloca of the variable.
bool IsLoadOfVariableRef(ILInstruction* inst, ILVariable* variable) {
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst))
        return ldloc->Variable.get() == variable;
    if (auto* ldloca = dynamic_cast<LdLoca*>(inst))
        return ldloca->Variable.get() == variable;
    return false;
}

// The C# `inst.Descendants` walk (self-inclusive).
void CollectLoadSitesOf(ILInstruction* inst, ILVariable* variable,
                        std::vector<ILInstruction*>& sites) {
    if (!inst) return;
    if (IsLoadOfVariableRef(inst, variable)) sites.push_back(inst);
    for (int i = 0; i < inst->ChildCount(); ++i)
        CollectLoadSitesOf(inst->GetChild(i), variable, sites);
}

bool VariableTypeIsKnownType(const ILVariable* variable, TypeSystem::KnownTypeCode code) {
    return variable != nullptr && variable->Type != nullptr &&
           TypeSystem::IsKnownType(*variable->Type, code);
}

bool ParameterTypeIsKnownType(const TypeSystem::IParameter* parameter,
                              TypeSystem::KnownTypeCode code) {
    return parameter != nullptr &&
           TypeSystem::IsKnownType(parameter->Type(), code);
}

// The C# `IType IndexType => IndexCtor?.DeclaringType` — an owning handle via
// the empty-deleter aliasing convention (the member/type live in the
// compilation; the LambdaConversion non-owning-shared_ptr precedent).
TypeSystem::ITypePtr ShareTypePtr(const TypeSystem::IType* type) {
    if (type == nullptr) return nullptr;
    return TypeSystem::ITypePtr(const_cast<TypeSystem::IType*>(type),
                                [](TypeSystem::IType*) {});
}

std::shared_ptr<TypeSystem::IMethod> ShareMethodPtr(const TypeSystem::IMethod* method) {
    if (method == nullptr) return nullptr;
    return std::shared_ptr<TypeSystem::IMethod>(
        const_cast<TypeSystem::IMethod*>(method), [](TypeSystem::IMethod*) {});
}

} // namespace

// The C# `enum IndexKind` (IndexRangeTransform.cs).
enum class IndexKind {
    FromStart,
    RefSystemIndex,
    FromEnd,
    TheStart,
    TheEnd,
};

// The C# `class IndexMethods` (IndexRangeTransform.cs lines 90-152): the
// System.Index / System.Range member scan through the context's compilation
// (`FindType(KnownTypeCode)`); a framework without the types yields an
// invalid scan and the transform bails.
struct IndexMethods {
    const TypeSystem::IMethod* IndexCtor = nullptr;
    const TypeSystem::IMethod* IndexImplicitConv = nullptr;
    const TypeSystem::IMethod* RangeCtor = nullptr;
    const TypeSystem::IMethod* RangeStartAt = nullptr;
    const TypeSystem::IMethod* RangeEndAt = nullptr;
    const TypeSystem::IMethod* RangeGetAll = nullptr;
    bool IsValid() const {
        return IndexCtor != nullptr && IndexImplicitConv != nullptr &&
               RangeCtor != nullptr;
    }

    explicit IndexMethods(const TypeSystem::ICompilation* compilation) {
        if (compilation == nullptr) return;
        const TypeSystem::IType& indexType =
            compilation->FindType(TypeSystem::KnownTypeCode::Index);
        for (const TypeSystem::IMethod* ctor :
             indexType.GetConstructors([](const TypeSystem::IMethod* m) {
                 return m != nullptr && m->Parameters().size() == 2;
             })) {
            if (ctor->Parameters().size() == 2 &&
                ParameterTypeIsKnownType(ctor->Parameters()[0],
                                         TypeSystem::KnownTypeCode::Int32) &&
                ParameterTypeIsKnownType(ctor->Parameters()[1],
                                         TypeSystem::KnownTypeCode::Boolean)) {
                IndexCtor = ctor;
            }
        }
        for (const TypeSystem::IMethod* op :
             indexType.GetMethods([](const TypeSystem::IMethod* m) {
                 return m != nullptr && m->IsOperator() && m->Name() == "op_Implicit";
             })) {
            if (op->Parameters().size() == 1 &&
                ParameterTypeIsKnownType(op->Parameters()[0],
                                         TypeSystem::KnownTypeCode::Int32)) {
                IndexImplicitConv = op;
            }
        }
        const TypeSystem::IType& rangeType =
            compilation->FindType(TypeSystem::KnownTypeCode::Range);
        for (const TypeSystem::IMethod* ctor :
             rangeType.GetConstructors([](const TypeSystem::IMethod* m) {
                 return m != nullptr && m->Parameters().size() == 2;
             })) {
            if (ctor->Parameters().size() == 2 &&
                ParameterTypeIsKnownType(ctor->Parameters()[0],
                                         TypeSystem::KnownTypeCode::Index) &&
                ParameterTypeIsKnownType(ctor->Parameters()[1],
                                         TypeSystem::KnownTypeCode::Index)) {
                RangeCtor = ctor;
            }
        }
        for (const TypeSystem::IMethod* m :
             rangeType.GetMethods([](const TypeSystem::IMethod* m) {
                 return m != nullptr && m->Parameters().size() == 1;
             })) {
            if (m->Parameters().size() == 1 &&
                ParameterTypeIsKnownType(m->Parameters()[0],
                                         TypeSystem::KnownTypeCode::Index)) {
                if (m->Name() == "StartAt")
                    RangeStartAt = m;
                else if (m->Name() == "EndAt")
                    RangeEndAt = m;
            }
        }
        for (const TypeSystem::IProperty* p :
             rangeType.GetProperties([](const TypeSystem::IProperty* p) {
                 return p != nullptr && p->IsStatic() && p->Name() == "All";
             })) {
            if (p != nullptr && p->IsStatic() && p->Name() == "All")
                RangeGetAll = p->Getter();
        }
    }

    // The C# `public static bool IsRangeCtor(IMethod method)`.
    static bool IsRangeCtor(const TypeSystem::IMethod* method) {
        if (method == nullptr) return false;
        if (method->SymbolKind() != TypeSystem::SymbolKind::Constructor) return false;
        if (method->Parameters().size() != 2) return false;
        if (method->DeclaringType() == nullptr ||
            !TypeSystem::IsKnownType(*method->DeclaringType(),
                                     TypeSystem::KnownTypeCode::Range))
            return false;
        for (const TypeSystem::IParameter* p : method->Parameters()) {
            if (!ParameterTypeIsKnownType(p, TypeSystem::KnownTypeCode::Index))
                return false;
        }
        return true;
    }
};

namespace {

// The C# `MatchContainerLength(ILInstruction init, ILVariable lengthVar, ref
// ILVariable containerVar)`: with a non-null lengthVar matches `ldloc
// lengthVar`; otherwise the `call get_Length/get_Count(ldloc container)`
// accessor shape.
bool MatchContainerLength(ILInstruction* init, ILVariable* lengthVar,
                          ILVariable*& containerVar);
bool MatchContainerVar(ILInstruction* inst, ILVariable*& containerVar);
bool MatchContainerLength(ILInstruction* init, ILVariable* lengthVar,
                          ILVariable*& containerVar) {
    if (lengthVar != nullptr) {
        return IsLoadOfVariableRef(init, lengthVar);
    }
    auto* call = dynamic_cast<Call*>(init);
    if (call == nullptr || call->IsNewObj || call->Method == nullptr) return false;
    (void)0;
    if (call->ResultType() != StackType::I4) return false;
    if (!(call->Method->IsAccessor() &&
          call->Method->AccessorKind() ==
              TypeSystem::MethodSemanticsAttributes::Getter))
        return false;
    const auto* lengthProp =
        dynamic_cast<const TypeSystem::IProperty*>(call->Method->AccessorOwner());
    if (lengthProp == nullptr) return false;
    if (lengthProp->Name() == "Length") {
        // OK, Length is preferred
    } else if (lengthProp->Name() == "Count") {
        // Also works, but only if the type doesn't have "Length"
        bool hasLength = false;
        for (const TypeSystem::IProperty* p :
             lengthProp->DeclaringTypeDefinition()->GetProperties(
                 [](const TypeSystem::IProperty* p) {
                     return p != nullptr && p->Name() == "Length";
                 })) {
            (void)p;
            hasLength = true;
        }
        if (hasLength) return false;
    } else {
        return false;
    }
    if (TypeSystem::IsKnownType(lengthProp->ReturnType(),
                                TypeSystem::KnownTypeCode::Int32) != true)
        return false;
    if (lengthProp->IsVirtual() && call->Op != OpCode::CallVirt) return false;
    if (call->Arguments.size() != 1) return false;
    return MatchContainerVar(call->Arguments[0].get(), containerVar);
}

// The C# `MatchContainerVar(ILInstruction inst, ref ILVariable containerVar)`.
bool MatchContainerVar(ILInstruction* inst, ILVariable*& containerVar) {
    if (containerVar != nullptr) return IsLoadOfVariableRef(inst, containerVar);
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        containerVar = ldloc->Variable.get();
        return containerVar != nullptr;
    }
    if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
        containerVar = ldloca->Variable.get();
        return containerVar != nullptr;
    }
    return false;
}

// The C# `static bool MatchContainerLengthStore(ILInstruction inst, out
// ILVariable lengthVar, ref ILVariable containerVar)`.
bool MatchContainerLengthStore(ILInstruction* inst, ILVariable*& lengthVar,
                               ILVariable*& containerVar) {
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr || stloc->Variable == nullptr) return false;
    lengthVar = stloc->Variable.get();
    ILInstruction* init = stloc->Value.get();
    if (!(lengthVar->IsSingleDefinition() && lengthVar->StackType() == StackType::I4))
        return false;
    if (lengthVar->LoadCount == 0 || lengthVar->LoadCount > 2) return false;
    return MatchContainerLength(init, nullptr, containerVar);
}

// The C# `static bool MatchIndexImplicitConv(ILInstruction inst, out
// ILInstruction offsetInst)`: `call op_Implicit(indexLoad)` on System.Index.
bool MatchIndexImplicitConv(ILInstruction* inst, ILInstruction*& offsetInst) {
    offsetInst = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj || call->Method == nullptr) return false;
    if (!(call->Method->IsOperator() && call->Method->Name() == "op_Implicit"))
        return false;
    const TypeSystem::IMethod* op = call->Method.get();
    if (!(op->Parameters().size() == 1 &&
          ParameterTypeIsKnownType(op->Parameters()[0],
                                   TypeSystem::KnownTypeCode::Int32)))
        return false;
    if (op->DeclaringType() == nullptr ||
        !TypeSystem::IsKnownType(*op->DeclaringType(), TypeSystem::KnownTypeCode::Index))
        return false;
    if (call->Arguments.size() != 1) return false;
    offsetInst = call->Arguments[0].get();
    return true;
}

// The C# `static bool MatchIndexFromRange(IndexKind indexKind, ILInstruction
// indexLoad, ILVariable rangeVar, string accessorName)`: matches
// `addressof System.Index(call get_Start/get_End(ldloca rangeVar))`.
bool MatchIndexFromRange(IndexKind indexKind, ILInstruction* indexLoad,
                         ILVariable* rangeVar, const char* accessorName) {
    if (indexKind != IndexKind::RefSystemIndex) return false;
    auto* addressOf = dynamic_cast<AddressOf*>(indexLoad);
    if (addressOf == nullptr) return false;
    if (addressOf->Type == nullptr ||
        !TypeSystem::IsKnownType(*addressOf->Type,
                                 TypeSystem::KnownTypeCode::Index))
        return false;
    auto* call = dynamic_cast<Call*>(addressOf->Value.get());
    if (call == nullptr || call->Method == nullptr) return false;
    if (call->Method->Name() != accessorName) return false;
    if (call->Method->DeclaringType() == nullptr ||
        !TypeSystem::IsKnownType(*call->Method->DeclaringType(),
                                 TypeSystem::KnownTypeCode::Range))
        return false;
    if (call->Arguments.size() != 1) return false;
    return IsLoadOfVariableRef(call->Arguments[0].get(), rangeVar);
}

// The match record for an index load: the C# MatchGetOffset's indexLoad is
// a borrowed reference into the stloc's init expression; the port records
// the owning parent + child slot so the node can be detached (TakeChild) when
// the new call is built. An owned LdcI4(0) (the C# `new LdcI4(0)` for the
// TheEnd case) is carried directly.
struct IndexLoadSite {
    ILInstruction* load = nullptr;       // borrowed; valid when owned == nullptr
    ILInstruction* owner = nullptr;      // the parent owning `load`
    int childIndex = -1;                 // load's slot in owner
    std::unique_ptr<ILInstruction> owned;  // the detached/owned load (LdcI4(0))
};

std::unique_ptr<ILInstruction> TakeIndexLoad(IndexLoadSite& site) {
    if (site.owned != nullptr) return std::move(site.owned);
    if (site.owner != nullptr && site.load != nullptr)
        return site.owner->TakeChild(site.childIndex);
    return nullptr;
}

// The C# `MatchGetOffset(ILInstruction inst, out ILInstruction indexLoad,
// ILVariable containerLengthVar, ref ILVariable containerVar)`: matches
// `call System.Index.GetOffset(indexLoad, ldloc containerLengthVar)` or
// `binary.sub.i4(ldloc containerLengthVar, indexLoad)`; anything else is an
// int expression from the start of the container.
IndexKind MatchGetOffset(ILInstruction* inst, IndexLoadSite& indexLoad,
                         ILVariable* containerLengthVar, ILVariable*& containerVar) {
    if (MatchContainerLength(inst, containerLengthVar, containerVar)) {
        // The C# `indexLoad = new LdcI4(0); return IndexKind.TheEnd;`.
        indexLoad.owned = std::make_unique<LdcI4>(0);
        return IndexKind::TheEnd;
    }
    if (auto* call = dynamic_cast<Call*>(inst);
        call != nullptr && !call->IsNewObj && call->Method != nullptr) {
        // call System.Index.GetOffset(indexLoad, ldloc containerLengthVar)
        if (call->Method->Name() == "GetOffset" &&
            call->Method->DeclaringType() != nullptr &&
            TypeSystem::IsKnownType(*call->Method->DeclaringType(),
                                    TypeSystem::KnownTypeCode::Index) &&
            call->Arguments.size() == 2 &&
            MatchContainerLength(call->Arguments[1].get(), containerLengthVar,
                                 containerVar)) {
            indexLoad.load = call->Arguments[0].get();
            indexLoad.owner = call;
            indexLoad.childIndex = 0;
            return IndexKind::RefSystemIndex;
        }
        return IndexKind::FromStart;
    }
    if (auto* bni = dynamic_cast<BinaryNumericInstruction*>(inst);
        bni != nullptr && bni->Operator == BinaryNumericOperator::Sub) {
        if (bni->CheckForOverflow || bni->ResultType() != StackType::I4 ||
            bni->IsLifted)
            return IndexKind::FromStart;
        // binary.sub.i4(ldloc containerLengthVar, indexLoad)
        if (!MatchContainerLength(bni->Left.get(), containerLengthVar, containerVar))
            return IndexKind::FromStart;
        indexLoad.load = bni->Right.get();
        indexLoad.owner = bni;
        indexLoad.childIndex = 1;
        return IndexKind::FromEnd;
    }
    return IndexKind::FromStart;
}

// The C# `static bool MatchSliceLength(ILInstruction inst, out IndexKind
// endIndexKind, out ILInstruction endIndexLoad, ILVariable
// containerLengthVar, ref ILVariable containerVar, ILVariable startOffsetVar)`.
bool MatchSliceLength(ILInstruction* inst, IndexKind& endIndexKind,
                      IndexLoadSite& endIndexLoad, ILVariable* containerLengthVar,
                      ILVariable*& containerVar, ILVariable* startOffsetVar) {
    if (auto* bni = dynamic_cast<BinaryNumericInstruction*>(inst);
        bni != nullptr && bni->Operator == BinaryNumericOperator::Sub) {
        if (bni->CheckForOverflow || bni->ResultType() != StackType::I4 ||
            bni->IsLifted)
            return false;
        if (startOffsetVar == nullptr) {
            // When slicing without explicit start point: `a[..endIndex]`
            auto* right = dynamic_cast<LdcI4*>(bni->Right.get());
            if (right == nullptr || right->Value != 0) return false;
        } else {
            if (!IsLoadOfVariableRef(bni->Right.get(), startOffsetVar)) return false;
        }
        endIndexKind = MatchGetOffset(bni->Left.get(), endIndexLoad,
                                      containerLengthVar, containerVar);
        return true;
    }
    if (startOffsetVar == nullptr) {
        // When slicing without explicit start point: `a[..endIndex]`, the
        // compiler doesn't always emit the "- 0".
        endIndexKind = MatchGetOffset(inst, endIndexLoad, containerLengthVar,
                                      containerVar);
        return true;
    }
    return false;
}

} // namespace

// The C# `private bool CSharpWillGenerateIndexer(IType declaringType, bool
// slicing)`: whether the C# compiler will call `container[int]` (or the Slice
// method) so the rewrite produces the source shape.
bool CSharpWillGenerateIndexer(const TypeSystem::IType* declaringType, bool slicing) {
    if (declaringType == nullptr) return false;
    bool foundInt32Overload = false;
    bool foundIndexOverload = false;
    bool foundRangeOverload = false;
    bool foundCountProperty = false;
    for (const TypeSystem::IProperty* prop : declaringType->GetProperties(
             [](const TypeSystem::IProperty* p) {
                 return p != nullptr &&
                        (p->IsIndexer() || p->Name() == "Length" ||
                         p->Name() == "Count");
             })) {
        if (prop->IsIndexer() && prop->Parameters().size() == 1) {
            const TypeSystem::IParameter* p = prop->Parameters()[0];
            if (ParameterTypeIsKnownType(p, TypeSystem::KnownTypeCode::Int32)) {
                foundInt32Overload = true;
            } else if (ParameterTypeIsKnownType(p, TypeSystem::KnownTypeCode::Index)) {
                foundIndexOverload = true;
            } else if (ParameterTypeIsKnownType(p, TypeSystem::KnownTypeCode::Range)) {
                foundRangeOverload = true;
            }
        } else if (prop->Name() == "Length" || prop->Name() == "Count") {
            foundCountProperty = true;
        }
    }
    if (slicing) {
        return foundCountProperty && !foundRangeOverload;
    }
    return foundInt32Overload && foundCountProperty && !foundIndexOverload;
}

// The C# `static bool IsSlicingMethod(IMethod method)`.
bool IndexRangeTransform::IsSlicingMethod(const TypeSystem::IMethod* method) {
    if (method == nullptr) return false;
    if (method->IsExtensionMethod()) return false;
    if (method->Parameters().size() != 2) return false;
    for (const TypeSystem::IParameter* p : method->Parameters()) {
        if (!ParameterTypeIsKnownType(p, TypeSystem::KnownTypeCode::Int32))
            return false;
    }
    if (method->Name() == "Slice") return true;
    if (method->Name() == "Substring" && method->DeclaringType() != nullptr &&
        TypeSystem::IsKnownType(*method->DeclaringType(),
                                TypeSystem::KnownTypeCode::String))
        return true;
    return false;
}

// The C# Run driver's local-function state (the captured variables of
// TransformIndexing / TransformSlicing). The C# local functions read/write
// `pos`, `containerLengthVar`, `containerVar`, `rangeVar`, `rangeVarInit`,
// `startOffsetVar`, `startIndexLoad` and `startIndexKind` from the enclosing
// Run scope; the port carries them in this struct.
struct IndexRangeState {
    Block& block;
    int startPos;
    ILTransformContext& context;
    ILVariable* containerLengthVar = nullptr;
    ILVariable* containerVar = nullptr;
    ILVariable* rangeVar = nullptr;
    ILInstruction* rangeVarInit = nullptr;
    ILVariable* startOffsetVar = nullptr;
    ILInstruction* startOffsetVarInit = nullptr;
    IndexLoadSite startIndex;
    IndexKind startIndexKind = IndexKind::FromStart;
    int pos = 0;

    // The C# `ILInstruction MakeIndex(IndexKind, ILInstruction, IndexMethods)`
    // local function.
    std::unique_ptr<ILInstruction> MakeIndex(IndexKind indexKind,
                                             std::unique_ptr<ILInstruction> indexLoad,
                                             const IndexMethods& specialMethods) {
        if (indexKind == IndexKind::RefSystemIndex) {
            //  stloc containerLengthVar(call get_Length/get_Count(ldloc container))
            //  stloc startOffsetVar(call GetOffset(startIndexLoad, ldloc length))
            //  complex_expr(call get_Item(ldloc container, ldloc startOffsetVar))
            // --> complex_expr(call get_Item(ldloc container, ldobj startIndexLoad))
            return std::make_unique<LdObj>(
                std::move(indexLoad),
                ShareTypePtr(specialMethods.IndexCtor != nullptr
                                 ? specialMethods.IndexCtor->DeclaringType().get()
                                 : nullptr));
        }
        if (indexKind == IndexKind::FromEnd || indexKind == IndexKind::TheEnd) {
            //  complex_expr(call get_Item(ldloc container, ldloc startOffsetVar))
            // --> complex_expr(call get_Item(ldloc container,
            //     newobj System.Index(startIndexLoad, fromEnd: true)))
            auto newCall = std::make_unique<Call>(
                ShareMethodPtr(specialMethods.IndexCtor), true);
            newCall->AddArg(std::move(indexLoad));
            newCall->AddArg(std::make_unique<LdcI4>(1));
            return newCall;
        }
        assert((indexKind == IndexKind::FromStart || indexKind == IndexKind::TheStart));
        auto newCall = std::make_unique<Call>(
            ShareMethodPtr(specialMethods.IndexImplicitConv));
        newCall->AddArg(std::move(indexLoad));
        return newCall;
    }

    // The C# `ILInstruction MakeRange(IndexKind, ILInstruction, IndexKind,
    // ILInstruction, IndexMethods)` local function.
    std::unique_ptr<ILInstruction> MakeRange(IndexKind endIndexKind,
                                             IndexLoadSite& endIndex,
                                             const IndexMethods& specialMethods) {
        if (rangeVar != nullptr) {
            return std::unique_ptr<ILInstruction>(rangeVarInit->Clone());
        }
        if (startIndexKind == IndexKind::TheStart && endIndexKind == IndexKind::TheEnd &&
            specialMethods.RangeGetAll != nullptr) {
            return std::make_unique<Call>(ShareMethodPtr(specialMethods.RangeGetAll));
        }
        if (startIndexKind == IndexKind::TheStart && specialMethods.RangeEndAt != nullptr) {
            auto rangeCtorCall = std::make_unique<Call>(
                ShareMethodPtr(specialMethods.RangeEndAt));
            rangeCtorCall->AddArg(
                MakeIndex(endIndexKind, TakeIndexLoad(endIndex), specialMethods));
            return rangeCtorCall;
        }
        if (endIndexKind == IndexKind::TheEnd && specialMethods.RangeStartAt != nullptr) {
            auto rangeCtorCall = std::make_unique<Call>(
                ShareMethodPtr(specialMethods.RangeStartAt));
            rangeCtorCall->AddArg(MakeIndex(
                startIndexKind, TakeIndexLoad(startIndex), specialMethods));
            return rangeCtorCall;
        }
        auto rangeCtorCall =
            std::make_unique<Call>(ShareMethodPtr(specialMethods.RangeCtor), true);
        rangeCtorCall->AddArg(
            MakeIndex(startIndexKind, TakeIndexLoad(startIndex), specialMethods));
        rangeCtorCall->AddArg(
            MakeIndex(endIndexKind, TakeIndexLoad(endIndex), specialMethods));
        return rangeCtorCall;
    }

    // The C# `private bool CheckContainerLengthVariableUseCount(ILVariable
    // containerLengthVar, IndexKind startIndexKind, IndexKind endIndexKind)`.
    bool CheckContainerLengthVariableUseCount(ILVariable* lengthVar,
                                              IndexKind startKind,
                                              IndexKind endKind) const {
        int expectedUses = 0;
        if (startKind != IndexKind::FromStart && startKind != IndexKind::TheStart)
            expectedUses += 1;
        if (endKind != IndexKind::FromStart && endKind != IndexKind::TheStart)
            expectedUses += 1;
        if (lengthVar != nullptr) {
            return lengthVar->LoadCount == expectedUses;
        }
        return expectedUses <= 1;  // can have one inline use
    }
};

// The C# `void IStatementTransform.Run(Block block, int pos,
// StatementTransformContext context)` (IndexRangeTransform.cs lines 102-152).
void IndexRangeTransform::Run(Block& block, int pos, StatementTransformContext& context) {
    if (!context.Base.Settings.Ranges) return;
    // The sentinel pos = -1 (an if-final-only block) has no statements to
    // match; guard it together with the upper bound.
    if (pos < 0 || pos >= static_cast<int>(block.Instructions.size())) return;
    IndexRangeState state{block, pos, context.Base};
    state.pos = pos;
    // The container length access may be a separate instruction, or it may be
    // inline with the variable's use.
    if (MatchContainerLengthStore(
            block.Instructions[static_cast<std::size_t>(state.pos)].get(),
            state.containerLengthVar, state.containerVar)) {
        //  stloc containerLengthVar(call get_Length/get_Count(ldloc container))
        state.pos++;
    } else {
        // Reset if MatchContainerLengthStore only had a partial match.
        // MatchGetOffset() will then set `containerVar`.
        state.containerLengthVar = nullptr;
        state.containerVar = nullptr;
    }
    if (state.pos < static_cast<int>(block.Instructions.size())) {
        if (auto* rangeStore = dynamic_cast<StLoc*>(
                block.Instructions[static_cast<std::size_t>(state.pos)].get());
            rangeStore != nullptr && rangeStore->Variable != nullptr &&
            VariableTypeIsKnownType(rangeStore->Variable.get(),
                                    TypeSystem::KnownTypeCode::Range)) {
            // stloc rangeVar(rangeVarInit)
            state.rangeVar = rangeStore->Variable.get();
            state.rangeVarInit = rangeStore->Value.get();
            state.pos++;
        }
    }
    if (state.pos >= static_cast<int>(block.Instructions.size())) return;
    // stloc startOffsetVar(call GetOffset(startIndexLoad, ldloc length))
    auto* startOffsetStore = dynamic_cast<StLoc*>(
        block.Instructions[static_cast<std::size_t>(state.pos)].get());
    if (!(startOffsetStore != nullptr && startOffsetStore->Variable != nullptr &&
          startOffsetStore->Variable->IsSingleDefinition() &&
          startOffsetStore->Variable->StackType() == StackType::I4)) {
        // Not our primary indexing/slicing pattern. However, we might be
        // dealing with a partially-transformed pattern that needs to be
        // extended (the C# ExtendSlicing pass).
        ExtendSlicing(state);
        return;
    }
    state.startOffsetVar = startOffsetStore->Variable.get();
    state.startOffsetVarInit = startOffsetStore->Value.get();
    state.startIndexKind = MatchGetOffset(
        state.startOffsetVarInit, state.startIndex, state.containerLengthVar,
        state.containerVar);
    state.pos++;
    if (state.startOffsetVar->LoadCount == 1) {
        TransformIndexing(state);
    } else if (state.startOffsetVar->LoadCount == 2) {
        // might be slicing: startOffset is used once for the slice length
        // calculation, and once for the Slice() method call
        TransformSlicing(state);
    }
}

// The C# `void TransformIndexing()` local function (IndexRangeTransform.cs
// lines 154-206): the `complex_expr(call get_Item(ldloc container,
// ldloc startOffsetVar))` rewrite.
void IndexRangeTransform::TransformIndexing(IndexRangeState& state) {
    if (state.rangeVar != nullptr) return;
    std::vector<ILInstruction*> loadSites;
    CollectLoadSitesOf(
        state.block.Instructions[static_cast<std::size_t>(state.pos)].get(),
        state.startOffsetVar, loadSites);
    if (loadSites.size() != 1) return;
    auto* call = dynamic_cast<Call*>(loadSites[0]->Parent);
    if (call == nullptr || call->Method == nullptr) return;
    if (call->Method->AccessorKind() ==
            TypeSystem::MethodSemanticsAttributes::Getter &&
        call->Arguments.size() == 2) {
        if (call->Method->AccessorOwner() == nullptr) return;
        const auto* owner = dynamic_cast<const TypeSystem::IProperty*>(
            call->Method->AccessorOwner());
        if (owner == nullptr || !owner->IsIndexer()) return;
        if (call->Method->Parameters().size() != 1) return;
    } else if (call->Method->AccessorKind() ==
                   TypeSystem::MethodSemanticsAttributes::Setter &&
               call->Arguments.size() == 3) {
        if (call->Method->AccessorOwner() == nullptr) return;
        const auto* owner = dynamic_cast<const TypeSystem::IProperty*>(
            call->Method->AccessorOwner());
        if (owner == nullptr || !owner->IsIndexer()) return;
        if (call->Method->Parameters().size() != 2) return;
    } else if (IsSlicingMethod(call->Method.get())) {
        TransformSlicing(state, true);
        return;
    } else {
        return;
    }
    if (state.startIndexKind == IndexKind::FromStart) {
        return;
    }
    if (!state.CheckContainerLengthVariableUseCount(
            state.containerLengthVar, state.startIndexKind, IndexKind::FromStart)) {
        return;
    }
    if (!call->IsDescendantOf(
            state.block.Instructions[static_cast<std::size_t>(state.pos)].get())) {
        return;
    }
    // startOffsetVar might be used deep inside a complex statement, ensure we
    // can inline up to that point:
    for (int i = state.startPos; i < state.pos; i++) {
        if (FindLoadInNext(
                state.block.Instructions[static_cast<std::size_t>(state.pos)].get(),
                state.startOffsetVar,
                state.block.Instructions[static_cast<std::size_t>(i)].get())
                .type != FindResultType::Found) {
            return;
        }
    }
    if (call->Method->Parameters().size() < 1 ||
        !ParameterTypeIsKnownType(call->Method->Parameters()[0],
                                  TypeSystem::KnownTypeCode::Int32)) {
        return;
    }
    if (!MatchContainerVar(call->Arguments[0].get(), state.containerVar)) {
        return;
    }
    if (!IsLoadOfVariableRef(call->Arguments[1].get(), state.startOffsetVar)) {
        return;
    }
    IndexMethods specialMethods(state.context.TypeSystem);
    if (!specialMethods.IsValid()) {
        return;
    }
    if (!CSharpWillGenerateIndexer(call->Method->DeclaringType().get(), false)) {
        return;
    }

    state.context.StepOnce("indexed with System.Index");
    auto newMethod =
        std::make_shared<TypeSystem::Implementation::SyntheticRangeIndexAccessor>(
            std::const_pointer_cast<const TypeSystem::IMethod>(call->Method),
            ShareTypePtr(specialMethods.IndexCtor != nullptr
                             ? specialMethods.IndexCtor->DeclaringType().get()
                             : nullptr),
            false);
    auto newCall = std::make_unique<Call>(newMethod, call->IsNewObj);
    newCall->ConstrainedTo = call->ConstrainedTo;
    newCall->ILStackWasEmpty = call->ILStackWasEmpty;
    newCall->AddArg(std::move(call->Arguments[0]));
    newCall->AddArg(state.MakeIndex(state.startIndexKind,
                                    TakeIndexLoad(state.startIndex),
                                    specialMethods));
    for (std::size_t i = 2; i < call->Arguments.size(); ++i)
        newCall->AddArg(std::move(call->Arguments[i]));
    call->ReplaceWith(std::move(newCall));
    state.block.Instructions.erase(
        state.block.Instructions.begin() + state.startPos,
        state.block.Instructions.begin() + state.pos);
    state.block.RenumberChildren();
}

// The C# `void TransformSlicing(bool sliceLengthWasMisdetectedAsStartOffset)`
// local function (IndexRangeTransform.cs lines 208-285): the
// `call Slice(ldloc container, ldloc startOffset, ldloc sliceLength)` rewrite.
void IndexRangeTransform::TransformSlicing(IndexRangeState& state,
                                           bool sliceLengthWasMisdetectedAsStartOffset) {
    ILVariable* sliceLengthVar = nullptr;
    ILInstruction* sliceLengthVarInit = nullptr;
    if (sliceLengthWasMisdetectedAsStartOffset) {
        // Special case: when slicing without a start point, the slice length
        // calculation is mis-detected as the start offset, and since it only
        // has a single use, we end in TransformIndexing(), which then calls
        // TransformSlicing on this code path.
        sliceLengthVar = state.startOffsetVar;
        sliceLengthVarInit = state.startOffsetVarInit;
        state.startOffsetVar = nullptr;
        state.startIndex = IndexLoadSite();
        state.startIndexKind = IndexKind::TheStart;
    } else {
        if (state.pos >= static_cast<int>(state.block.Instructions.size())) return;
        auto* store = dynamic_cast<StLoc*>(
            state.block.Instructions[static_cast<std::size_t>(state.pos)].get());
        if (store == nullptr || store->Variable == nullptr) return;
        sliceLengthVar = store->Variable.get();
        sliceLengthVarInit = store->Value.get();
        state.pos++;
    }
    if (!(sliceLengthVar->IsSingleDefinition() && sliceLengthVar->LoadCount == 1))
        return;
    IndexLoadSite endIndex;
    IndexKind endIndexKind = IndexKind::FromStart;
    if (!MatchSliceLength(sliceLengthVarInit, endIndexKind, endIndex,
                          state.containerLengthVar, state.containerVar,
                          state.startOffsetVar))
        return;
    if (!state.CheckContainerLengthVariableUseCount(
            state.containerLengthVar, state.startIndexKind, endIndexKind)) {
        return;
    }
    if (state.rangeVar != nullptr) {
        return;  // this should only ever happen in the second step (ExtendSlicing)
    }
    std::vector<ILInstruction*> loadSites;
    CollectLoadSitesOf(
        state.block.Instructions[static_cast<std::size_t>(state.pos)].get(),
        sliceLengthVar, loadSites);
    if (loadSites.size() != 1) return;
    auto* call = dynamic_cast<Call*>(loadSites[0]->Parent);
    if (call == nullptr || call->Method == nullptr) return;
    if (!call->IsDescendantOf(
            state.block.Instructions[static_cast<std::size_t>(state.pos)].get()))
        return;
    if (!IsSlicingMethod(call->Method.get())) return;
    if (call->Arguments.size() != 3) return;
    if (!MatchContainerVar(call->Arguments[0].get(), state.containerVar)) return;
    if (state.startOffsetVar == nullptr) {
        auto* arg = dynamic_cast<LdcI4*>(call->Arguments[1].get());
        if (arg == nullptr || arg->Value != 0) return;
    } else {
        if (!IsLoadOfVariableRef(call->Arguments[1].get(), state.startOffsetVar))
            return;
        if (!CanMoveInto(
                state.startOffsetVarInit,
                state.block.Instructions[static_cast<std::size_t>(state.pos)].get(),
                call->Arguments[1].get()))
            return;
    }
    if (!IsLoadOfVariableRef(call->Arguments[2].get(), sliceLengthVar)) return;
    if (!CanMoveInto(sliceLengthVarInit,
                     state.block.Instructions[static_cast<std::size_t>(state.pos)].get(),
                     call->Arguments[2].get()))
        return;
    if (!CSharpWillGenerateIndexer(call->Method->DeclaringType().get(), true)) return;
    IndexMethods specialMethods(state.context.TypeSystem);
    if (!specialMethods.IsValid()) return;

    state.context.StepOnce("sliced");
    auto newMethod =
        std::make_shared<TypeSystem::Implementation::SyntheticRangeIndexAccessor>(
            std::const_pointer_cast<const TypeSystem::IMethod>(call->Method),
            ShareTypePtr(specialMethods.RangeCtor != nullptr
                             ? specialMethods.RangeCtor->DeclaringType().get()
                             : nullptr),
            true);
    auto newCall = std::make_unique<Call>(newMethod, call->IsNewObj);
    newCall->ConstrainedTo = call->ConstrainedTo;
    newCall->ILStackWasEmpty = call->ILStackWasEmpty;
    newCall->AddArg(std::move(call->Arguments[0]));
    newCall->AddArg(state.MakeRange(endIndexKind, endIndex, specialMethods));
    for (std::size_t i = 3; i < call->Arguments.size(); ++i)
        newCall->AddArg(std::move(call->Arguments[i]));
    call->ReplaceWith(std::move(newCall));
    state.block.Instructions.erase(
        state.block.Instructions.begin() + state.startPos,
        state.block.Instructions.begin() + state.pos);
    state.block.RenumberChildren();
}

// The C# `static bool HandleLdElema(LdElema ldelema, ILTransformContext
// context)` (IndexRangeTransform.cs lines 37-75): the `array[System.Index]`
// cases called by the expression transforms.
bool IndexRangeTransform::HandleLdElema(LdElema& ldelema, ILTransformContext& context) {
    if (!context.Settings.Ranges) return false;
    if (ldelema.Array == nullptr) return false;
    auto* arrayLoad = dynamic_cast<LdLoc*>(ldelema.Array.get());
    if (arrayLoad == nullptr) return false;
    ILVariable* array = arrayLoad->Variable.get();
    if (ldelema.Indices.size() != 1)
        return false;  // the index/range feature doesn't support multi-dimensional arrays
    ILInstruction* index = ldelema.Indices[0].get();
    if (auto* call = dynamic_cast<Call*>(index);
        call != nullptr && !call->IsNewObj && call->Method != nullptr &&
        call->Method->Name() == "GetOffset" &&
        call->Method->DeclaringType() != nullptr &&
        TypeSystem::IsKnownType(*call->Method->DeclaringType(),
                                TypeSystem::KnownTypeCode::Index)) {
        // ldelema T(ldloc array, call GetOffset(..., ldlen.i4(ldloc array)))
        // -> withsystemindex.ldelema T(ldloc array, ...)
        if (call->Arguments.size() != 2) return false;
        auto* ldlen = dynamic_cast<LdLen*>(call->Arguments[1].get());
        if (ldlen == nullptr || ldlen->resultType != StackType::I4) return false;
        if (!IsLoadOfVariableRef(ldlen->Argument.get(), array)) return false;
        context.StepOnce("ldelema with System.Index");
        ldelema.WithSystemIndex = true;
        // The method call had a `ref System.Index` argument for the this
        // pointer, but we want a `System.Index` by-value. The C#
        // `ldelema.Indices[0] = new LdObj(call.Arguments[0],
        // call.Method.DeclaringType)` re-parents the taken argument.
        ldelema.Indices[0] =
            std::make_unique<LdObj>(std::move(call->Arguments[0]),
                                    ShareTypePtr(call->Method->DeclaringType().get()));
        ldelema.Indices[0]->Parent = &ldelema;
        ldelema.Indices[0]->ChildIndex = 1;
        return true;
    }
    if (auto* bni = dynamic_cast<BinaryNumericInstruction*>(index);
        bni != nullptr && bni->Operator == BinaryNumericOperator::Sub &&
        !bni->IsLifted && !bni->CheckForOverflow) {
        // ldelema T(ldloc array, binary.sub.i4(ldlen.i4(ldloc array), ...))
        // -> withsystemindex.ldelema T(ldloc array, newobj System.Index(...,
        // fromEnd: true))
        auto* ldlen = dynamic_cast<LdLen*>(bni->Left.get());
        if (ldlen == nullptr || ldlen->resultType != StackType::I4) return false;
        if (!IsLoadOfVariableRef(ldlen->Argument.get(), array)) return false;
        IndexMethods indexMethods(context.TypeSystem);
        if (!indexMethods.IsValid())
            return false;  // don't use System.Index if not supported by the target framework
        context.StepOnce("ldelema indexed from end");
        ldelema.WithSystemIndex = true;
        // The C# `MakeIndex(IndexKind.FromEnd, bni.Right, indexMethods)`: a
        // `newobj System.Index(indexLoad, fromEnd: true)`.
        auto indexCtorCall = std::make_unique<Call>(
            ShareMethodPtr(indexMethods.IndexCtor), true);
        indexCtorCall->AddArg(bni->TakeChild(1));
        indexCtorCall->AddArg(std::make_unique<LdcI4>(1));
        ldelema.Indices[0] = std::move(indexCtorCall);
        ldelema.Indices[0]->Parent = &ldelema;
        ldelema.Indices[0]->ChildIndex = 1;
        return true;
    }
    return false;
}


// The C# `void ExtendSlicing()` local function (IndexRangeTransform.cs lines
// 397-458): the second-pass extension. A previous Run may have executed
// TransformSlicing on a partial pattern (slicing-from-end mis-detected as
// slicing-from-start); the merged range construction
// `newobj Range(GetOffset(...), GetOffset(...))` inside a
// SyntheticRangeIndexAccessor call is re-merged into a direct range
// construction when the container length is available. The C# reads
// `containerLengthVar.LoadInstructions[0]` (the whole-function load list);
// the port's ILVariable does not track load lists, so the walk starts at the
// block's first instruction (the container-length store lives in this
// block, so its loads do too).
void IndexRangeTransform::ExtendSlicing(IndexRangeState& state) {
    if (state.containerLengthVar == nullptr) {
        return;  // need a container length to extend with
    }
    assert(state.containerLengthVar->IsSingleDefinition());
    std::vector<ILInstruction*> loadSites;
    if (!state.block.Instructions.empty()) {
        CollectLoadSitesOf(state.block.Instructions[0].get(),
                           state.containerLengthVar, loadSites);
    }
    if (loadSites.empty()) return;
    // Walk the first load's ancestors up to (and including) the block,
    // looking for the NewObj Range-ctor call (the C# `inst.Ancestors` walk
    // with the `inst == block` stop).
    Call* rangeCtorCall = nullptr;
    for (ILInstruction* inst = loadSites[0]; inst != nullptr;
         inst = inst->Parent) {
        if (inst == &state.block) break;
        if (auto* call = dynamic_cast<Call*>(inst);
            call != nullptr && call->IsNewObj &&
            IndexMethods::IsRangeCtor(call->Method.get())) {
            rangeCtorCall = call;
            break;
        }
    }
    if (rangeCtorCall == nullptr) return;
    // Now match the pattern that TransformSlicing() generated in the
    // IndexKind.FromStart case: the NewObj's parent must be the
    // SyntheticRangeIndexAccessor slicing call.
    auto* slicingCall = dynamic_cast<Call*>(rangeCtorCall->Parent);
    if (slicingCall == nullptr || slicingCall->IsNewObj ||
        slicingCall->Method == nullptr) {
        return;
    }
    if (dynamic_cast<const TypeSystem::Implementation::SyntheticRangeIndexAccessor*>(
            slicingCall->Method.get()) == nullptr) {
        return;
    }
    if (slicingCall->Arguments.empty()) return;
    if (!MatchContainerVar(slicingCall->Arguments[0].get(),
                           state.containerVar)) {
        return;
    }
    if (!slicingCall->IsDescendantOf(
            state.block.Instructions[static_cast<std::size_t>(state.pos)].get())) {
        return;
    }
    assert(rangeCtorCall->Arguments.size() == 2);
    if (rangeCtorCall->Arguments.size() != 2) return;
    ILInstruction* startOffsetInst = nullptr;
    ILInstruction* endOffsetInst = nullptr;
    if (!MatchIndexImplicitConv(rangeCtorCall->Arguments[0].get(),
                                startOffsetInst)) {
        return;
    }
    if (!MatchIndexImplicitConv(rangeCtorCall->Arguments[1].get(),
                                endOffsetInst)) {
        return;
    }
    IndexLoadSite startSite;
    IndexLoadSite endIndex;
    IndexKind startIndexKind = MatchGetOffset(
        startOffsetInst, startSite, state.containerLengthVar,
        state.containerVar);
    IndexKind endIndexKind = MatchGetOffset(
        endOffsetInst, endIndex, state.containerLengthVar,
        state.containerVar);
    if (!state.CheckContainerLengthVariableUseCount(
            state.containerLengthVar, startIndexKind, endIndexKind)) {
        return;
    }
    // holds because we've used containerLengthVar at least once
    assert(startIndexKind != IndexKind::FromStart ||
           endIndexKind != IndexKind::FromStart);
    // The port's MakeRange consumes the state's start fields (the C# passes
    // them as parameters); install the matched start for the duration.
    // The port's MakeRange consumes the state's start fields (the C# passes
    // them as parameters); install the matched start for the duration (the
    // state object is the per-Run holder and dies right after, so no restore
    // is needed).
    state.startIndex = std::move(startSite);
    state.startIndexKind = startIndexKind;
    if (state.rangeVar != nullptr) {
        if (state.rangeVarInit == nullptr) return;
        if (!CanMoveInto(state.rangeVarInit,
                         state.block.Instructions[static_cast<std::size_t>(state.pos)].get(),
                         state.startIndex.load)) {
            return;
        }
        if (!MatchIndexFromRange(startIndexKind, state.startIndex.load,
                                 state.rangeVar, "get_Start")) {
            return;
        }
        if (!MatchIndexFromRange(endIndexKind, endIndex.load,
                                 state.rangeVar, "get_End")) {
            return;
        }
    }
    IndexMethods specialMethods(state.context.TypeSystem);
    if (!specialMethods.IsValid()) return;
    state.context.StepOnce("Merge containerLengthVar into slicing");
    std::unique_ptr<ILInstruction> merged =
        state.MakeRange(endIndexKind, endIndex, specialMethods);
    rangeCtorCall->ReplaceWith(std::move(merged));
    // The C# adds the removed instructions' IL spans to the slicing call
    // (`slicingCall.AddILRange(...)`); the port's IL ranges are the
    // [StartILOffset, EndILOffset) spans -- fold the removed range's start.
    for (int i = state.startPos; i < state.pos; i++) {
        ILInstruction* removed =
            state.block.Instructions[static_cast<std::size_t>(i)].get();
        if (removed != nullptr && removed->StartILOffset != 0 &&
            slicingCall->StartILOffset == 0) {
            slicingCall->StartILOffset = removed->StartILOffset;
        }
    }
    state.block.Instructions.erase(
        state.block.Instructions.begin() + state.startPos,
        state.block.Instructions.begin() + state.pos);
    state.block.RenumberChildren();
}

} // namespace ILSpy::Decompiler::IL
