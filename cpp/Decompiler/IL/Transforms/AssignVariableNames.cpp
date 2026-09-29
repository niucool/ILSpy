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

#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/CSharp/OutputVisitor/CSharpKeywordCheck.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cctype>
#include <map>
#include <set>
#include <cstring>
#include <string>

namespace ILSpy::Decompiler::IL {

namespace {

// ECMA known-type -> default variable name, matching the C#
// typeNameToVariableNameDict (AssignVariableNames.cs).
const std::map<std::string, std::string>& TypeNameDict() {
    static const std::map<std::string, std::string> dict = {
        {"System.Boolean", "flag"},
        {"System.Byte", "b"}, {"System.SByte", "b"},
        {"System.Int16", "num"}, {"System.Int32", "num"}, {"System.Int64", "num"},
        {"System.UInt16", "num"}, {"System.UInt32", "num"}, {"System.UInt64", "num"},
        {"System.Single", "num"}, {"System.Double", "num"}, {"System.Decimal", "num"},
        {"System.String", "text"},
        {"System.Object", "obj"},
        {"System.Char", "c"},
    };
    return dict;
}

// Infer a base variable name from a type. Known primitive types use the dict;
// other types fall back to the lowercased short type name (System.Version ->
// "version", stripping a trailing `N generic-arity suffix). Returns "" for an
// unknown type so the caller keeps the original V_N name.
std::string InferName(const TypeSystem::IType* type) {
    if (!type) return "";
    // An array type is named "array" (not the lowercased element type with
    // brackets, which yields names like "byte[]").
    if (dynamic_cast<const TypeSystem::ArrayType*>(type)) return "array";
    // A byref is named after the element type (the C# uses the underlying).
    if (auto* byref = dynamic_cast<const TypeSystem::ByReferenceType*>(type))
        return InferName(byref->Element().get());
    // A pointer is named "ptr" (not the lowercased element type with *, which
    // yields names like "byte*" that clash with the type syntax).
    if (dynamic_cast<const TypeSystem::PointerType*>(type)) return "ptr";
    std::string rn = type->ReflectionName();
    auto it = TypeNameDict().find(rn);
    if (it != TypeNameDict().end()) return it->second;
    // A parameterized type's reflection name carries its type arguments in
    // `[[...]]` (e.g. `System.Collections.Generic.List`1[[System.String]]`);
    // the variable name derives from the base (container) type, so cut the
    // type-argument list before taking the last segment. Without this the
    // last '.' lands inside the type args and leaks a mangled name
    // ("string]]"). Array/pointer/byref types were handled above, so the
    // first '[' can only open a type-argument list.
    auto lt = rn.find('[');
    if (lt != std::string::npos) rn = rn.substr(0, lt);
    // Short name: the segment after the last '.', lowercased first letter.
    auto pos = rn.rfind('.');
    std::string name = (pos != std::string::npos) ? rn.substr(pos + 1) : rn;
    auto bt = name.find('`');
    if (bt != std::string::npos) name = name.substr(0, bt);  // arity suffix
    if (name.empty()) return "";
    // The C# GetNameByType's interface strip: `remove the 'I' for
    // interfaces` -- an I followed by an upper-case letter and a lower-case
    // letter (IShape -> Shape; a name like `Int32` is untouched because its
    // third letter is upper-case... `In32`-style names with a lower third
    // letter do strip, matching the C# predicate).
    if (name.size() >= 3 && name[0] == 'I' &&
        std::isupper(static_cast<unsigned char>(name[1])) &&
        std::islower(static_cast<unsigned char>(name[2])))
        name = name.substr(1);
    name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    return name;
}

// The C# `static string CleanUpVariableName(string name)`
// (AssignVariableNames.cs): strip the generic-arity backtick and the
// m_ / _ field prefixes, reject non-printable, empty, and illegal names
// and C# keywords, then lowercase the first character. Returns "" when
// the name is unusable (the caller falls through to the next proposal).
std::string CleanUpVariableName(std::string name) {
    // remove the backtick (generics)
    std::size_t pos = name.find('`');
    if (pos != std::string::npos)
        name = name.substr(0, pos);
    // remove field prefix:
    if (name.size() > 2 && name.compare(0, 2, "m_") == 0)
        name = name.substr(2);
    else if (name.size() > 1 && name[0] == '_' &&
             (std::isalpha(static_cast<unsigned char>(name[1])) ||
              name[1] == '_'))
        name = name.substr(1);
    if (name.empty() || name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789_") != std::string::npos)
        return "";
    std::string lowerCaseName = name;
    lowerCaseName[0] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(lowerCaseName[0])));
    if (CSharp::OutputVisitor::IsKeyword(lowerCaseName))
        return "";
    return lowerCaseName;
}

// The method name after the `::` separator of a "Namespace.Type::Method"
// reflection-style name ("" when there is none).
std::string MethodNamePart(const std::string& full) {
    std::size_t sep = full.rfind("::");
    return sep == std::string::npos ? std::string()
                                    : full.substr(sep + 2);
}

// The C# `static bool ExcludeMethodFromCandidates(IMethod m)`: the
// operators, ToString, string Concat, and GetPinnableReference never
// name a variable.
bool ExcludeMethodFromCandidates(const std::string& name) {
    return name.rfind("op_", 0) == 0 || name == "ToString" ||
           name == "Concat" || name == "GetPinnableReference";
}

// The C# `static string GetNameFromInstruction(ILInstruction inst)`: the
// name a store's value context suggests -- the field loads (the field
// name, recursing into the target for compiler-generated fields), and the
// get_/Get* property/method calls (the name remainder). Returns "" when
// the instruction suggests nothing.
std::string GetNameFromInstruction(ILInstruction* inst) {
    if (inst == nullptr) return std::string();
    switch (inst->Op) {
        case OpCode::LdObj: {
            // ldfld is LdObj(LdFlda(target, field), type): the field name.
            auto* ldobj = static_cast<LdObj*>(inst);
            return GetNameFromInstruction(ldobj->Target.get());
        }
        case OpCode::LdFlda: {
            auto* ldflda = static_cast<LdFlda*>(inst);
            if (ldflda->IsCompilerGeneratedField)
                return GetNameFromInstruction(ldflda->Target.get());
            return CleanUpVariableName(MethodNamePart(ldflda->FieldName));
        }
        case OpCode::LdsFlda: {
            auto* ldsflda = static_cast<LdsFlda*>(inst);
            return CleanUpVariableName(MethodNamePart(ldsflda->FieldName));
        }
        case OpCode::Call:
        case OpCode::CallVirt: {
            auto* call = static_cast<Call*>(inst);
            std::string name = MethodNamePart(call->MethodName);
            if (ExcludeMethodFromCandidates(name)) break;
            if (name.rfind("get_", 0) == 0 && call->Arguments.empty()) {
                // use name from properties, but not from indexers
                return CleanUpVariableName(name.substr(4));
            }
            if (name.rfind("Get", 0) == 0 && name.size() >= 4 &&
                std::isupper(static_cast<unsigned char>(name[3]))) {
                // use name from Get-methods
                return CleanUpVariableName(name.substr(3));
            }
            break;
        }
        default:
            break;
    }
    return std::string();
}

// The C# `static string GetNameForArgument(ILInstruction parent, int i)`:
// the name a load's use context suggests -- the field store target, and
// the set_/Set-method call the argument feeds. (The C# also reads the
// call's formal PARAMETER names and the Leave -> "result" arm; the port's
// flat reader resolves no IMethod for the calls, so those arms stay
// deferred -- recorded in the handoff.)
std::string GetNameForArgument(ILInstruction* parent, int childIndex) {
    if (parent == nullptr) return std::string();
    switch (parent->Op) {
        case OpCode::StObj: {
            // stfld is StObj(LdFlda(target, field), value, type).
            auto* stobj = static_cast<StObj*>(parent);
            if (stobj->Target == nullptr) break;
            if (stobj->Target->Op == OpCode::LdFlda)
                return CleanUpVariableName(MethodNamePart(
                    static_cast<LdFlda*>(stobj->Target.get())->FieldName));
            if (stobj->Target->Op == OpCode::LdsFlda)
                return CleanUpVariableName(MethodNamePart(
                    static_cast<LdsFlda*>(stobj->Target.get())->FieldName));
            break;
        }
        case OpCode::Call:
        case OpCode::CallVirt: {
            auto* call = static_cast<Call*>(parent);
            if (childIndex < 0 ||
                childIndex >= static_cast<int>(call->Arguments.size()))
                break;
            std::string name = MethodNamePart(call->MethodName);
            if (ExcludeMethodFromCandidates(name)) return std::string();
            // A single-argument setter call: the argument might be the
            // value of a setter.
            if (call->Arguments.size() == 1) {
                if (name.rfind("set_", 0) == 0)
                    return CleanUpVariableName(name.substr(4));
                if (name.rfind("Set", 0) == 0 && name.size() >= 4 &&
                    std::isupper(static_cast<unsigned char>(name[3])))
                    return CleanUpVariableName(name.substr(3));
            }
            break;
        }
        case OpCode::Leave:
            return "result";
        default:
            break;
    }
    return std::string();
}

// The trailing-digit strip (the C# SplitName's base-name half): a proposed
// name like "text2" renames to "text" before the conflict suffix applies.
std::string StripTrailingDigits(const std::string& name) {
    std::size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1])))
        --end;
    return name.substr(0, end);
}

} // namespace

// The C# GetNameByType's naming core, exposed for the flat emitter's
// foreach element naming: the known-type dict (byte -> b, string ->
// text), the array/pointer/byref names, the interface strip, and the
// lowercased short type name. The rendered type text itself is never a
// name (a keyword like `byte` is not a legal identifier).
std::string AssignVariableNames::SuggestNameForType(
    const TypeSystem::IType* type) {
    return InferName(type);
}

// Vocabularies.Default's core rules, the subset the variable names hit):
// the irregulars, -ies -> -y, the -es after the sibilants, and the plain
// -s. A word that does not look plural returns unchanged (the caller's
// IsPlural check: only a changed result counts as a plural).
std::string SingularizeWord(const std::string& word) {
    static const std::map<std::string, std::string> irregulars = {
        {"data", "datum"},       {"men", "man"},
        {"women", "woman"},       {"children", "child"},
        {"people", "person"},    {"teeth", "tooth"},
        {"feet", "foot"},        {"geese", "goose"},
        {"mice", "mouse"},       {"criteria", "criterion"},
        {"phenomena", "phenomenon"},
    };
    auto it = irregulars.find(word);
    if (it != irregulars.end())
        return it->second;
    auto endsWith = [&](const char* suffix) {
        std::size_t n = std::strlen(suffix);
        return word.size() >= n && word.compare(word.size() - n, n, suffix) == 0;
    };
    if (endsWith("ies") && word.size() > 3)
        return word.substr(0, word.size() - 3) + "y";
    if (endsWith("ses") || endsWith("xes") || endsWith("zes") ||
        endsWith("ches") || endsWith("shes"))
        return word.substr(0, word.size() - 2);
    if (endsWith("s") && !endsWith("ss") && !endsWith("us") &&
        !endsWith("is") && word.size() > 1)
        return word.substr(0, word.size() - 1);
    return word;
}

// The C# GenerateForeachVariableName: the element name from the
// collection expression (the singularized suggested name, the List-suffix
// strip, the list -> item and children rules, the digit strip, the item
// fallback). The conflict suffix is the caller's (the emitter's declared
// set).
std::string AssignVariableNames::SuggestForeachElementName(
    ILInstruction* collection) {
    std::string baseName;
    if (collection != nullptr)
        baseName = GetNameFromInstruction(collection);
    if (baseName.empty() && collection != nullptr &&
        collection->Op == OpCode::LdLoc) {
        // The C#'s parameter arm: a collection held in a parameter keeps
        // the parameter's name.
        ILVariable* v = static_cast<LdLoc*>(collection)->Variable.get();
        if (v != nullptr && v->Kind == VariableKind::Parameter)
            baseName = v->Name;
    }
    std::string proposedName = "item";
    if (!baseName.empty()) {
        std::string singular = SingularizeWord(baseName);
        if (singular != baseName) {
            proposedName = singular;
        } else if (baseName.size() > 4 &&
                   baseName.compare(baseName.size() - 4, 4, "List") == 0) {
            proposedName = baseName.substr(0, baseName.size() - 4);
        } else if (baseName == "list") {
            proposedName = "item";
        } else if (baseName.size() >= 8 &&
                   baseName.compare(baseName.size() - 8, 8, "children") == 0) {
            proposedName = baseName.substr(0, baseName.size() - 3);
        } else {
            proposedName = baseName;
        }
    }
    return StripTrailingDigits(proposedName);
}

// The minimal English singularization (the Humanizer


// The C# `internal static bool IsValidName(string varName)` (line 677):
// whitespace/empty fails, the first unit must be a letter or '_', every
// following unit a letter, digit, or '_'. The C# `char.IsLetter` /
// `char.IsLetterOrDigit` port through the Util::Char probe tables (the
// string indexes the UTF-16 units).
bool AssignVariableNames::IsValidName(const std::string& varName) {
    // The C# `string.IsNullOrWhiteSpace` arm is folded into the unit walk: an
    // empty string fails, and an all-whitespace string fails the letter check
    // on the first unit (whitespace is never a letter).
    const std::u16string units = Util::Utf8ToUtf16(varName);
    if (units.empty())
        return false;
    if (!(Util::IsLetter(units[0]) || units[0] == u'_'))
        return false;
    for (std::size_t i = 1; i < units.size(); i++)
    {
        if (!(Util::IsLetterOrDigit(units[i]) || units[i] == u'_'))
            return false;
    }
    return true;
}

namespace {

// The C# loop-counter detection (AssignVariableNames.cs lines 87-98): the
// variables incremented in a For container's increment block (`v = v + k`
// stores) -- named i, j, k, l, m, n.
void CollectLoopCounters(ILInstruction* inst, std::set<ILVariable*>& counters) {
    if (inst == nullptr) return;
    if (auto* container = dynamic_cast<BlockContainer*>(inst)) {
        if (container->Kind == ContainerKind::For) {
            for (const auto& block : container->Blocks) {
                if (!block) continue;
                for (const auto& stmt : block->Instructions) {
                    if (!stmt || stmt->Op != OpCode::StLoc) continue;
                    auto* st = static_cast<StLoc*>(stmt.get());
                    if (!st->Variable || !st->Value ||
                        st->Value->Op != OpCode::BinaryNumericInstruction)
                        continue;
                    auto* bin = static_cast<BinaryNumericInstruction*>(
                        st->Value.get());
                    if (bin->Operator != BinaryNumericOperator::Add &&
                        bin->Operator != BinaryNumericOperator::Sub)
                        continue;
                    if (bin->Left && bin->Left->Op == OpCode::LdLoc &&
                        static_cast<LdLoc*>(bin->Left.get())->Variable.get() ==
                            st->Variable.get())
                        counters.insert(st->Variable.get());
                }
            }
        }
    }
    for (int c = 0; c < inst->ChildCount(); ++c)
        CollectLoopCounters(inst->GetChild(c), counters);
}

// The LdLoc-site collection for the load proposal (see the Run).
void CollectLoadSites(
    ILInstruction* inst,
    std::map<ILVariable*, std::vector<ILInstruction*>>& loadSites) {
    if (inst == nullptr) return;
    if (inst->Op == OpCode::LdLoc) {
        ILVariable* variable =
            static_cast<LdLoc*>(inst)->Variable.get();
        if (variable != nullptr)
            loadSites[variable].push_back(inst);
    }
    for (int c = 0; c < inst->ChildCount(); ++c)
        CollectLoadSites(inst->GetChild(c), loadSites);
}

} // namespace

// The C# `static bool IsLowerCase(string name)` (AssignVariableNames.cs):
// a non-empty name whose first character is already lower-case.
bool IsLowerCaseName(const std::string& name) {
    return !name.empty() &&
           std::tolower(static_cast<unsigned char>(name[0])) ==
               static_cast<unsigned char>(name[0]);
}

void AssignVariableNames::Run(ILFunction& function, ILTransformContext& context) {
    // The loop counters (the i/j/k/n naming below).
    std::set<ILVariable*> loopCounters;
    if (function.Body)
        CollectLoopCounters(function.Body.get(), loopCounters);
    // The load sites per variable (the C# ILVariable.LoadInstructions --
    // the port's reader computes only the counts, so the naming's load
    // proposal walks the tree once): every LdLoc keyed by its variable.
    std::map<ILVariable*, std::vector<ILInstruction*>> loadSites;
    if (function.Body)
        CollectLoadSites(function.Body.get(), loadSites);
    // Names already taken: parameters (kept as-is) and locals renamed so far.
    std::set<std::string> taken;
    for (auto& v : function.Variables) {
        if (v && v->Kind == VariableKind::Parameter)
            taken.insert(v->Name);
    }
    // The C# VariableScope root-scope currentLowerCaseTypeOrMemberNames
    // (AssignVariableNames.cs lines 107-129): the lower-case member names
    // of the declaring type -- `GetMembers()` includes the inherited
    // members, so a base-declared field name filters too -- plus the
    // lower-case type names of the declaring type's namespace and nesting
    // chain. A naming proposal matching one of these is rejected: the
    // local would shadow the member/type in the rendered scope and force
    // a `base.`/`this.` qualifier on the member accesses. The type names
    // also reserve the name outright (the C# AddExistingName on
    // reservedVariableNames); the member names only filter the proposals.
    // The using-scope type-name arm (the C# context.UsingScope) stays
    // deferred: the port's IL pipeline context carries no using scope.
    // The primary-constructor backing-field arm (`<name>P` fields) needs
    // the IsCompilerGenerated surface; the corpora predate C# 12 primary
    // constructors, so the arm stays deferred with it.
    std::set<std::string> currentLowerCaseTypeOrMemberNames;
    {
        const TypeSystem::ITypeDefinition* declaringType =
            function.Method != nullptr
                ? function.Method->DeclaringTypeDefinition()
                : context.CurrentTypeDefinition;
        if (declaringType != nullptr) {
            for (const TypeSystem::IMember* m :
                 declaringType->GetMembers()) {
                if (m != nullptr && IsLowerCaseName(m->Name()))
                    currentLowerCaseTypeOrMemberNames.insert(m->Name());
            }
            if (context.TypeSystem != nullptr) {
                const TypeSystem::INamespace* ns =
                    TypeSystem::GetNamespaceByFullName(
                        *context.TypeSystem, declaringType->Namespace());
                if (ns != nullptr) {
                    for (const TypeSystem::ITypeDefinition* t : ns->Types()) {
                        if (t != nullptr && IsLowerCaseName(t->Name())) {
                            currentLowerCaseTypeOrMemberNames.insert(t->Name());
                            taken.insert(t->Name());
                        }
                    }
                }
            }
            for (const TypeSystem::ITypeDefinition* current = declaringType;
                 current != nullptr;
                 current = current->DeclaringTypeDefinition()) {
                for (const TypeSystem::ITypeDefinition* nested :
                     current->NestedTypes()) {
                    if (nested != nullptr && IsLowerCaseName(nested->Name())) {
                        currentLowerCaseTypeOrMemberNames.insert(nested->Name());
                        taken.insert(nested->Name());
                    }
                }
            }
        }
    }
    for (auto& v : function.Variables) {
        if (!v || v->Kind == VariableKind::Parameter) continue;
        // The C# GenerateNameForVariable's loop-counter arm: an int32 loop
        // counter names from the i, j, k, l, m, n sequence.
        std::string base;
        if (v->Type && loopCounters.count(v.get()) != 0) {
            auto* k = dynamic_cast<const TypeSystem::KnownType*>(v->Type.get());
            if (k != nullptr && k->Code() == TypeSystem::KnownTypeCode::Int32) {
                for (char c = 'i'; c <= 'n'; ++c) {
                    if (!taken.count(std::string(1, c))) {
                        base = std::string(1, c);
                        break;
                    }
                }
            }
        }
        if (base.empty()) {
            // The C# GenerateNameForVariable's store proposal: the names
            // the variable's stores suggest (the field loads and the
            // get_/Get* calls), used only when every store agrees.
            std::set<std::string> storeNames;
            for (ILInstruction* store : v->StoreInstructions) {
                if (store->Op != OpCode::StLoc) continue;
                // The C# adds the null suggestions to the set too: a
                // store that suggests nothing makes the set ambiguous
                // (a lone null singleton proposes nothing), so the empty
                // string participates in the distinct-count. A proposal
                // matching a member/type name is rejected instead (the
                // currentLowerCaseTypeOrMemberNames filter: the local must
                // not shadow the member).
                std::string suggested = GetNameFromInstruction(
                    static_cast<StLoc*>(store)->Value.get());
                if (!suggested.empty() &&
                    currentLowerCaseTypeOrMemberNames.count(suggested) != 0)
                    continue;
                storeNames.insert(std::move(suggested));
            }
            if (storeNames.size() == 1 && !storeNames.begin()->empty())
                base = *storeNames.begin();
        }
        if (base.empty()) {
            // The C# load proposal: the names the variable's loads
            // suggest through their use context (the field store targets
            // and the set_/Set-method arguments), again only when every
            // load agrees.
            std::set<std::string> loadNames;
            auto it = loadSites.find(v.get());
            if (it != loadSites.end()) {
                for (ILInstruction* load : it->second) {
                    // The store proposal's null rule applies here too
                    // (the C# Except keeps the nulls as a distinct set
                    // element): a load whose use context suggests nothing
                    // vetoes the proposal. A suggestion matching a
                    // member/type name is rejected (the same
                    // currentLowerCaseTypeOrMemberNames filter).
                    std::string suggested =
                        GetNameForArgument(load->Parent, load->ChildIndex);
                    if (!suggested.empty() &&
                        currentLowerCaseTypeOrMemberNames.count(suggested) != 0)
                        continue;
                    loadNames.insert(std::move(suggested));
                }
                if (loadNames.size() == 1 && !loadNames.begin()->empty())
                    base = *loadNames.begin();
            }
        }
        if (base.empty())
            base = InferName(v->Type.get());
        if (base.empty()) continue;  // leave V_N when the type is unknown
        base = StripTrailingDigits(base);
        std::string name = base;
        // The C# conflict suffix: the number directly appended (`num2`),
        // starting from 2 for the first conflict.
        for (int i = 2; taken.count(name); ++i)
            name = base + std::to_string(i);
        taken.insert(name);
        v->Name = name;
    }
}

} // namespace ILSpy::Decompiler::IL
