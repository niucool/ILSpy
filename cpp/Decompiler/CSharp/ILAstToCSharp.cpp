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
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/CSharp/RequiredImportsRecorder.hpp"
#include <algorithm>
#include <functional>
#include <cmath>
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/BlockKind.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/InvalidInstructions.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/Await.hpp"
#include "Decompiler/IL/Instructions/DynamicInstructions.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/RefAnyType.hpp"
#include "Decompiler/IL/Instructions/TypedReferenceInstructions.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/Unbox.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::IL {
namespace {

// "Namespace.Type::Member" -> "Namespace.Type.Member"; a trailing "::.ctor"
// marks a constructor reference (the "new" form is added by the caller).
// Remove the ``N` type-arity suffix immediately preceding a type-argument
// list (`List`1<...>` -> `List<...>`), recursing into nested type arguments.
// The arity is a CLR encoding detail; C# writes the type as `Name<args>`.
// Compiler-generated names that merely *start* with `<` (e.g. `<>c`,
// `<Foo>d__0`) do not match because they are not preceded by `` `N ``.
static void StripGenericArity(std::string& s, std::size_t start) {
    std::size_t pos = start;
    while ((pos = s.find('<', pos)) != std::string::npos) {
        // Walk back over the digits of the arity count, then the backtick.
        std::size_t digitEnd = pos;
        std::size_t p = pos;
        while (p > 0 && s[p - 1] >= '0' && s[p - 1] <= '9') --p;
        if (p < digitEnd && p > 0 && s[p - 1] == '`') {
            // s[p-1 .. pos-1] is ``Ndigits` -- erase it.
            s.erase(p - 1, pos - (p - 1));
            // Continue scanning from where the erased backtick was (the '<' moved up).
            pos = p - 1;
        } else {
            ++pos;
        }
    }
}

// Convert the metadata reflection form's generic-argument geometry into the
// C# display form: `Type`N[[A],[B]]` -> `Type<A, B>` (nested instantiations
// and array-suffix arguments included). Each `[[` opens an argument list,
// `]]` closes it, and `],[` separates two arguments; a single `[`/`]` pair
// inside an argument is an array suffix (`A[]`, `A[,]`). Malformed input
// keeps its original text.
static void FlattenReflectionArgs(std::string& s) {
    std::size_t pos;
    while ((pos = s.find("[[")) != std::string::npos) {
        std::string out = s.substr(0, pos) + "<";
        std::size_t i = pos + 2;
        std::size_t depth = 1;
        while (i < s.size()) {
            if (i + 1 < s.size() && s[i] == '[' && s[i + 1] == '[') {
                depth++;
                out += '<';
                i += 2;
            } else if (i + 1 < s.size() && s[i] == ']' && s[i + 1] == ']') {
                depth--;
                out += '>';
                i += 2;
                if (depth == 0)
                    break;
            } else if (i + 2 < s.size() && s.compare(i, 3, "],[") == 0) {
                out += ", ";
                i += 3;
            } else if (s[i] == '[') {
                // An array suffix: copy through its closing ']'.
                std::size_t j = s.find(']', i);
                if (j == std::string::npos) {
                    out += s.substr(i);
                    i = s.size();
                } else {
                    out += s.substr(i, j - i + 1);
                    i = j + 1;
                }
            } else {
                out += s[i];
                ++i;
            }
        }
        if (depth != 0)
            return;  // malformed: keep the original string
        s = out + s.substr(i);
    }
}

std::string FlattenMetadataName(std::string name) {
    for (std::size_t pos; (pos = name.find("::")) != std::string::npos;)
        name.replace(pos, 2, ".");
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "..") == 0)
        name.erase(name.size() - 1);  // fold ".." left by an empty member segment
    FlattenReflectionArgs(name);
    StripGenericArity(name, 0);
    return name;
}

// The short method name -- the substring after the last "::" in a Call's
// resolved MethodName ("Namespace.Type::Method" -> "Method").
std::string_view ShortMethodName(std::string_view fullName) {
    auto pos = fullName.rfind("::");
    return (pos == std::string_view::npos) ? fullName : fullName.substr(pos + 2);
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
    return CSharpTypeName(type);
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

// Forward declarations: whether a branch renders no `goto` (a redundant
// fall-through or a loop pre-header entry). CollectLabels consults these so it
// only creates a label for a branch target that will actually be referenced by
// an emitted `goto`. Defined later in the file.
static bool IsFallThroughGoto(const Branch* br);
static bool IsLoopEntryFallThrough(const Branch* br);
static const Block* TextuallyNextEmittedBlock(const ILInstruction* node);
// The first block the emitter visits inside `block`: descends into a
// "pure construct" block (no instructions, a TryFinally/TryCatch/Using/Lock
// final whose body is a BlockContainer) to the construct body's first block.
// Defined later in the file.
static const Block* FirstEmittedBlockOf(const Block* block);

// Emit an ILFunction body as C#-ish text. See the file header for the level
// of fidelity this seed aims for.
class CEmitter {
public:
    explicit CEmitter(std::string& out) : out_(out) {}

    // The C# name lookup's unqualified resolution (the common case): a
    // static member of the current function's declaring type is nameable
    // unqualified inside the type. The comparison is by the reflection
    // names (the nested-type '+' separator normalized to '.'); a null
    // Method (the delegate-body reads and the hand-built fixtures) leaves
    // the context empty and the qualified render stands.
    void SetCurrentTypeName(const ILFunction& fn) {
        if (fn.Method == nullptr || fn.Method->DeclaringType() == nullptr)
            return;
        std::string name = fn.Method->DeclaringType()->ReflectionName();
        for (char& c : name) {
            if (c == '+') c = '.';
        }
        currentTypeName_ = std::move(name);
    }

    // A flattened `Type.Member` (or deeper `NS.Type.Member`) renders as the
    // bare member when the type prefix names the current function's
    // declaring type.
    // The C# member-of-type access: a static member's declaring type
    // renders its SHORT name (`Button.ClickEvent`, not the full dotted
    // chain) -- the scope resolves the type; the emitter has no resolver,
    // so the declaring type's last segment is the spelling (the corpus
    // and connid oracles' form).
    std::string ShortQualifiedMember(const std::string& name) const {
        auto lastDot = name.rfind('.');
        if (lastDot == std::string::npos || lastDot == 0)
            return name;
        std::string typePart = name.substr(0, lastDot);
        auto typeDot = typePart.rfind('.');
        return (typeDot == std::string::npos)
                   ? name
                   : typePart.substr(typeDot + 1) + "." +
                         name.substr(lastDot + 1);
    }

    std::string SimplifyQualifiedMember(const std::string& name) const {
        if (currentTypeName_.empty())
            return name;
        if (name.size() <= currentTypeName_.size() + 1 ||
            name.compare(0, currentTypeName_.size(), currentTypeName_) != 0 ||
            name[currentTypeName_.size()] != '.')
            return name;
        std::string member = name.substr(currentTypeName_.size() + 1);
        // The remainder is the member name (a further '.' means the
        // prefix matched a longer nesting chain, not the member's type).
        if (member.find('.') != std::string::npos)
            return name;
        return member;
    }

    void EmitMethod(const ILFunction& fn, std::string_view returnType,
                    std::string_view methodName, std::string_view paramDecl,
                    bool isConstructor = false,
                    std::string_view methodConstraints = std::string_view()) {
        fn_ = &fn;
        SetCurrentTypeName(fn);
        returnTypeName_ = std::string(returnType);
        methodName_ = std::string(methodName);
        // A constructor header carries no return type: the methodName holds
        // the TYPE name (the C# `TypeName(...)` header; the flat renderer
        // carries no modifiers).
        if (isConstructor) {
            out_ += methodName;
        } else {
            out_ += returnType;
            out_ += ' ';
            out_ += methodName;
        }
        out_ += '(';
        out_ += paramDecl;
        out_ += ')';
        out_ += methodConstraints;
        out_ += "\n{\n";
        if (fn.Body) {
            CollectLoopHeaders(fn.Body.get());
            CollectLabels(fn.Body.get());
            AnalyzeReturnPropagation(fn.Body.get());
            AnalyzeNullCoalescingChains(fn.Body.get());
            AnalyzeSingleUseLocals();
            AnalyzeNullPropagation();
            AnalyzeLabelRegions();
            HoistForInitializers(fn.Body.get());
            EmitContainer(*fn.Body, 1);
        }
        out_ += "}\n";
    }

private:
    std::string& out_;
    const ILFunction* fn_ = nullptr;
    std::string currentTypeName_;
    std::string returnTypeName_;  // the C# name of the function's return type
    std::string methodName_;  // the method's display name (for diagnostics)
    std::set<std::string> declared_;          // locals already introduced with `var`
    // The alias statement folded into a preceding ArrayInitializer render
    // (skipped when the statement loop reaches it).
    const StLoc* skippedAlias_ = nullptr;
    // The null-coalescing chain fold (the dumped shape): `a = expr; b = a;
    // if (a) goto L (else Nop);` in one block, then `b = alt; branch L` in
    // the next, folds to `b = (expr ?? alt);` -- composed with the port's
    // existing NullCoalescingInstruction rendering rather than a bespoke
    // text form. The instances are owned here (the ILAst tree does not own
    // them; they live only for the render).
    std::map<const StLoc*, NullCoalescingInstruction*> coalesceFolds_;
    // The elision extension: a folded target with exactly ONE remaining
    // load site inlines the coalesce expression into that use (the C#
    // ILInlining's single-use rule) and drops the declaration. The map
    // holds the single LdLoc with the folded node.
    std::map<const LdLoc*, NullCoalescingInstruction*> coalesceElisions_;
    // The GENERAL single-use elision (the same rule without the coalesce):
    // a local with exactly one load and one store, never address-taken,
    // whose store's expression is pure and whose use sits after the
    // declaration in the same container's straight-line flow, inlines the
    // expression into the use and drops the declaration -- the port keeps
    // `var x = expr; use(x);` where the oracle renders `use(expr);`.
    std::map<const LdLoc*, ILInstruction*> singleUseElisions_;
    // The null-propagation fold (the [NP3]-probed shape): b0 =
    // [stloc dup = expr] + if-final(comp(dup == 0/null), Leave(ldnull),
    // no false-arm); b1 = the fall-through use block [leave call
    // M(ldloc dup, ...)]. The [stloc, if] skip; b1 renders
    // `return expr?.Member(args);` through the map.
    std::map<const Call*, ILInstruction*> nullPropagation_;
    // The label-region fold (the goto-to-later-sibling restructure): a
    // block's if-final branching to bK+2, with the one-block fall-through
    // (bK+1) and the one-block target (bT=bK+2) both exiting to the same
    // continuation (the fall-through after bT), merges into an if/else
    // over the two regions -- the goto disappears.
    std::map<const IfInstruction*,
             std::pair<Block*, Block*>> labelRegionFolds_;
    std::set<const Block*> labelRegionSuppressed_;
    std::set<const ILInstruction*> singleUseSkipped_;
    std::set<const ILInstruction*> coalesceSkipped_;
    std::set<const Block*> coalesceSuppressed_;
    std::vector<std::unique_ptr<NullCoalescingInstruction>> coalesceKeepAlive_;
    // The foreach substitution: the for-shape whose body's only uses of
    // the counter are the array element accesses renders as
    // `foreach (T e in arr)` with the accesses substituted by `e`.
    std::set<const LdElema*> foreachSubst_;
    std::string foreachElementName_;
    // The for-initializer hoists (the C# PatternStatementTransform's
    // TransformFor): the pre-pass moves a loop-preceding `v = init;` store
    // into the for's initializer and records the rendered init text (and
    // the variable name, for the declare-state) keyed by the loop
    // container.
    struct ForHoist {
        std::string Text;
        std::string VariableName;
    };
    std::map<const BlockContainer*, ForHoist> hoistedForInits_;
    std::map<const Block*, std::string> labels_;  // branch-target block -> IL_XXXX
    std::set<const Block*> loopHeaders_;  // first block of each Loop container
    // Blocks whose `IL_XXXX:` label was emitted BEFORE a construct keyword
    // (a loop/try/catch/finally/lock/using whose entry block is a `goto` target
    // from outside the construct). EmitBlock suppresses the label for these so
    // it is not emitted a second time inside the construct body.
    std::set<const Block*> emittedHeaderLabels_;
    // For each loop container, the block it exits to (the post-loop block a
    // `break;` targets). Used by GotoText to render an inner-loop `br` to the
    // loop's exit as `break;`.
    std::map<const BlockContainer*, const Block*> loopExits_;

    // Section bodies of a switch instruction: each body is a Branch thunk to a
    // target body block whose content is the real code, laid out after the
    // switch-host block in the same outer container. When the thunks are all
    // eligible (computed by AnalyzeSwitchInline), the seed renders the bodies
    // inline under their case labels and replaces each body's trailing
    // `br exit` with `break;` -- the C# form. `inlinedBodyBlocks_` are the
    // target blocks the outer container loop must skip; `breakBranches_` the
    // individual Branch nodes (thunk or body-exit) that render as `break;`,
    // for the CollectLabels pass.
    struct SwitchInlinePlan {
        bool eligible = false;
        std::vector<const Block*> targets;
        const Block* exit = nullptr;
        // The DEFAULT section whose thunk targets the exit renders as the
        // fall-through: the after-switch code IS the default path, so the
        // section emits neither a `default:` label nor a body.
        bool defaultFallsToExit = false;
        std::size_t defaultSectionIdx = static_cast<std::size_t>(-1);
        // Sections whose body is a direct Leave (a `return value;` or `throw`),
        // not a Branch thunk to a body block. They inline their Leave directly
        // under the case label (no body block, no thunk goto). Maps the section
        // index to its Leave body for emission.
        std::map<std::size_t, const Leave*> directLeaveSections;
        // Sections whose body is a direct Throw (`case N: throw new ...;`),
        // not a Branch thunk. They inline the throw directly under the case
        // label (no body block, no thunk goto, no exit contribution).
        std::map<std::size_t, const ILInstruction*> directThrowSections;
    };
    std::unordered_map<const SwitchInstruction*, SwitchInlinePlan> switchInlinePlans_;
    std::set<const Block*> inlinedBodyBlocks_;
    std::set<const Branch*> breakBranches_;
    // The goto-to-return propagation: a branch whose target block is only
    // a function-body leave (the reader's shared-exit block), with the
    // branch as the block's single predecessor and no fall-through into
    // it, renders the leave's return at the branch site and the target
    // block is suppressed (the C#'s reader models the same shape as a
    // value-carrying leave at the branch, so the oracle renders `return
    // x;` where the port emitted `goto IL_xxxx;`).
    std::map<const Branch*, const Block*> returnPropagation_;
    std::set<const Block*> suppressedReturnBlocks_;



    // Recursion-depth guard. The seed is a recursive AST walker with no
    // intrinsic bound: EmitStatement / EmitBlock / EmitContainer / EmitBraced /
    // Expr recurse into children with no depth limit. A malformed ILAst -- a
    // cycle (a transform bug producing a Parent-pointer loop) or a
    // pathologically deep tree -- would make these recurse without bound and
    // emit a runaway (a repeated token such as `lock (...)` ad infinitum),
    // producing garbage output or an OOM. Bound the depth so a runaway becomes
    // a single `/* max rendering depth */` marker (statement) or `/* ... */`
    // placeholder (expression) instead. The underlying tree bug is still
    // caught by CheckInvariant (run in debug after every transform); this
    // guard only bounds the production text so a single bad method cannot
    // corrupt a whole-module dump.
    static constexpr int kMaxRenderDepth = 300;
    int depth_ = 0;
    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& d_) : d(d_) { ++d; }
        ~DepthGuard() { --d; }
    };

    // Whether the recursion-depth guard has fired for this method. Set once
    // at the first guard trigger so a single debug warning is emitted per
    // method (not per guarded entry). In a debug build the warning goes to
    // stderr so a transform-induced cycle (the real cause of the runaway the
    // guard bounds) surfaces visibly -- the guard only masks the symptom in
    // the production text; the warning makes the underlying bug diagnosable.
    bool depthGuardFired_ = false;
    bool DepthAtLimit() {
        if (depth_ < kMaxRenderDepth) return false;
#ifndef NDEBUG
        if (!depthGuardFired_) {
            depthGuardFired_ = true;
            std::fprintf(stderr,
                "ILAstToCSharp: max rendering depth %d hit -- possible ILAst "
                "cycle or pathologically deep tree in method '%s'. Output for "
                "this method is truncated at a /* max rendering depth */ marker.\n",
                kMaxRenderDepth,
                methodName_.empty() ? "<unknown>" : methodName_.c_str());
        }
#endif
        return true;
    }

    void Line(int indent, std::string_view text) {
        out_.append(static_cast<std::size_t>(indent), '\t');
        out_ += text;
        out_ += '\n';
    }

    // The whole-word replacement (the identifier boundaries): the
    // foreach collapse renames the hoisted element local without touching
    // longer identifiers that contain it.
    static void ReplaceWholeWord(std::string& text, const std::string& from,
                                 const std::string& to) {
        std::size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            bool leftOk = pos == 0 ||
                !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) ||
                  text[pos - 1] == '_');
            std::size_t end = pos + from.size();
            bool rightOk = end >= text.size() ||
                !(std::isalnum(static_cast<unsigned char>(text[end])) ||
                  text[end] == '_');
            if (leftOk && rightOk) {
                text.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos += 1;
            }
        }
    }

    // The one-tab dedent: the collapsed foreach body replaces the
    // using+while nesting, so each rendered line moves out one level.
    static std::string DedentByOne(std::string text) {
        std::string out;
        out.reserve(text.size());
        std::size_t i = 0;
        while (i < text.size()) {
            std::size_t eol = text.find('\n', i);
            if (eol == std::string::npos) eol = text.size();
            std::size_t lineStart = i;
            if (lineStart < eol && text[lineStart] == '\t')
                lineStart++;
            out += text.substr(lineStart, eol - lineStart);
            if (eol < text.size()) out += '\n';
            i = eol + 1;
        }
        return out;
    }

    // The enumerator-foreach collapse (the C# StatementBuilder's
    // TransformToForeach): `using (coll.GetEnumerator()) { while
    // ((ref enum).MoveNext()) { ... enum.Current ... } }` renders
    // `foreach (T e in coll) { ... }` with the element named by the
    // C# GenerateForeachVariableName (the collection's name singularized).
    // The element type comes from the hoisted `T v = ...Current;`
    // declaration, else the collection type's first type argument, else
    // var. Returns false on any shape mismatch (the caller keeps the
    // using form).
    bool TryCollapseEnumeratorForeach(const UsingInstruction& us,
                                      const Call& getEnumerator,
                                      int indent, std::string& result) {
        const std::string enumName =
            us.Variable ? us.Variable->Name : std::string();
        if (enumName.empty() || us.Body == nullptr)
            return false;
        // The collection: the GetEnumerator call's receiver.
        if (getEnumerator.Arguments.empty())
            return false;
        ILInstruction* receiver = getEnumerator.Arguments[0].get();
        std::string collectionText = Expr(*receiver);
        if (collectionText.empty())
            return false;
        // Render the using body with the normal machinery, then collapse
        // the rendered while shape (the port's own deterministic output).
        std::string saved;
        saved.swap(out_);
        EmitBraced(*us.Body, indent);
        std::string bodyText;
        bodyText.swap(out_);
        out_.swap(saved);
        // The while header: `while ((ref ENUM).MoveNext())`.
        const std::string header = "while ((ref " + enumName + ").MoveNext())";
        std::size_t headerPos = bodyText.find(header);
        if (headerPos == std::string::npos)
            return false;
        // The loop body: the braces after the header line.
        std::size_t open = bodyText.find('{', headerPos + header.size());
        if (open == std::string::npos)
            return false;
        int depth = 0;
        std::size_t close = std::string::npos;
        for (std::size_t i = open; i < bodyText.size(); i++) {
            if (bodyText[i] == '{') depth++;
            else if (bodyText[i] == '}') {
                depth--;
                if (depth == 0) { close = i; break; }
            }
        }
        if (close == std::string::npos)
            return false;
        std::string loopBody = bodyText.substr(open + 1, close - open - 1);
        std::string trailing = bodyText.substr(close + 1);
        // The element name: the C# GenerateForeachVariableName over the
        // collection expression, with the conflict suffix.
        std::string elemName =
            IL::AssignVariableNames::SuggestForeachElementName(receiver);
        if (elemName.empty())
            elemName = "item";
        {
            std::string base = elemName;
            for (int n = 2; declared_.count(elemName); ++n)
                elemName = base + std::to_string(n);
        }
        declared_.insert(elemName);
        // The element type: the hoisted `T v = ref ENUM.Current;` first
        // statement, else the collection type's first argument.
        std::string elemType = "var";
        const std::string currentRead = "ref " + enumName + ".Current";
        {
            // The hoisted declaration: `TYPE NAME = ref ENUM.Current;`.
            std::string pattern = " = " + currentRead + ";";
            std::size_t declPos = loopBody.find(pattern);
            if (declPos != std::string::npos) {
                std::size_t lineStart =
                    loopBody.rfind('\n', declPos) + 1;
                std::size_t eq = loopBody.rfind(" = ", declPos);
                if (eq != std::string::npos && eq >= lineStart) {
                    std::string decl = loopBody.substr(lineStart, eq - lineStart);
                    std::size_t sp = decl.rfind(' ');
                    if (sp != std::string::npos) {
                        elemType = decl.substr(0, sp);
                        // The declaration line's leading tabs stay with the
                        // erased statement, not the type text.
                        std::size_t firstNonWs = elemType.find_first_not_of("\t ");
                        if (firstNonWs != std::string::npos)
                            elemType = elemType.substr(firstNonWs);
                        std::string hoisted = decl.substr(sp + 1);
                        if (!hoisted.empty()) {
                            loopBody.erase(
                                lineStart,
                                loopBody.find('\n', declPos) - lineStart + 1);
                            ReplaceWholeWord(loopBody, hoisted, elemName);
                        }
                    }
                }
            }
        }
        if (elemType == "var") {
            // The collection's element type: a generic collection's first
            // type argument (List<string> -> string).
            const TypeSystem::IType* collectionType = nullptr;
            if (auto* obj = dynamic_cast<const LdObj*>(receiver))
                collectionType = obj->Type.get();
            else if (auto* ldloc =
                         dynamic_cast<const LdLoc*>(receiver))
                collectionType = ldloc->Variable
                                     ? ldloc->Variable->Type.get()
                                     : nullptr;
            if (auto* p = dynamic_cast<const TypeSystem::ParameterizedType*>(
                    collectionType)) {
                if (!p->TypeArguments().empty())
                    elemType = CSharpTypeName(p->TypeArguments()[0]);
            }
        }
        // The inline element reads become the element name.
        ReplaceWholeWord(loopBody, currentRead, elemName);
        // Any remaining enumerator uses (beyond Current) abort -- the
        // foreach hides the enumerator, so a MoveNext/Dispose reference
        // cannot render.
        if (loopBody.find(enumName) != std::string::npos)
            return false;
        // The composition mirrors EmitBraced's geometry: the foreach at
        // the using's level, the loop body dedented one level out of the
        // while, the trailing statements (after the while) and the
        // using's own closing brace folded the same way.
        if (!trailing.empty() && trailing.back() == '}')
            trailing.pop_back();
        while (!trailing.empty() &&
               (trailing.back() == '\t' || trailing.back() == '\n' ||
                trailing.back() == ' '))
            trailing.pop_back();
        std::size_t firstNonWs = trailing.find_first_not_of("\t\n ");
        trailing = firstNonWs == std::string::npos
                       ? std::string()
                       : trailing.substr(firstNonWs);
        if (!trailing.empty())
            trailing += '\n';
        result.clear();
        for (int i = 0; i < indent; i++) result += '\t';
        result += "foreach (" + elemType + " " + elemName + " in " +
                  collectionText + ")";
        result += "\n";
        for (int i = 0; i < indent; i++) result += '\t';
        result += "{" + DedentByOne(loopBody) + "}\n";
        if (!trailing.empty()) {
            for (int i = 0; i < indent; i++) result += '\t';
            result += DedentByOne(trailing);
        }
        return true;
    }

    static std::string LabelFor(std::uint32_t offset) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "IL_%04X", offset);
        return buf;
    }

    // The goto-to-return propagation analysis: find the blocks whose whole
    // content is a leave to the function body, count their predecessors,
    // and when exactly one branch reaches the block with no fall-through
    // into it, record the branch -> leave mapping and suppress the block.
    void AnalyzeReturnPropagation(ILInstruction* root) {
        if (root == nullptr || fn_ == nullptr || fn_->Body == nullptr)
            return;
        // Count every branch targeting each block (the recursive walk,
        // including switch-section bodies).
        std::map<const Block*, int> preds;
        std::function<void(const ILInstruction*)> countBranches =
            [&](const ILInstruction* i) {
            if (i == nullptr) return;
            if (i->Op == OpCode::Branch) {
                auto* br = static_cast<const Branch*>(i);
                if (br->TargetBlock != nullptr)
                    preds[br->TargetBlock]++;
            }
            for (int c = 0; c < i->ChildCount(); ++c)
                countBranches(i->GetChild(c));
        };
        countBranches(root);
        std::function<void(BlockContainer*)> scan = [&](BlockContainer* c) {
            if (c == nullptr) return;
            for (std::size_t k = 0; k < c->Blocks.size(); ++k) {
                Block* b = c->Blocks[k].get();
                if (b == nullptr || b->FinalInstruction == nullptr)
                    continue;
                if (preds[b] != 1 || k == 0)
                    continue;
                // No fall-through: the preceding block must end in an
                // unconditional control transfer.
                const Block* prev = c->Blocks[k - 1].get();
                if (prev == nullptr || prev->FinalInstruction == nullptr)
                    continue;
                OpCode f = prev->FinalInstruction->Op;
                if (f != OpCode::Branch && f != OpCode::Leave &&
                    f != OpCode::Throw && f != OpCode::SwitchInstruction)
                    continue;
                // Find the single branch reaching the block.
                const Branch* src = nullptr;
                std::function<void(const ILInstruction*)> find =
                    [&](const ILInstruction* i) {
                    if (i == nullptr || src != nullptr) return;
                    if (i->Op == OpCode::Branch) {
                        auto* br = static_cast<const Branch*>(i);
                        if (br->TargetBlock == b) { src = br; return; }
                    }
                    for (int c2 = 0; c2 < i->ChildCount(); ++c2)
                        find(i->GetChild(c2));
                };
                find(root);
                if (src == nullptr) continue;
                returnPropagation_[src] = b;
                suppressedReturnBlocks_.insert(b);
            }
            for (int c2 = 0; c2 < c->ChildCount(); ++c2)
                scan(dynamic_cast<BlockContainer*>(c->GetChild(c2)));
        };
        scan(dynamic_cast<BlockContainer*>(root));
    }

    // The null-coalescing chain fold (the dump-first design): the chain
    // spans two blocks -- [.., a = expr, b = a, if (a) goto L else Nop]
    // then [b = alt, branch L] -- and folds to a NullCoalescingInstruction
    // composed with the existing ?? rendering. The safety gates: the
    // temporary a is loaded only by the copy and the guard, the target b is
    // stored only by the copy and the alternative, both paths join at L,
    // and the alternative block has NO other branch predecessor (the
    // measured constraint -- a suppression of a shared block loses other
    // paths' statements).
    void AnalyzeNullCoalescingChains(ILInstruction* root) {
        if (root == nullptr || fn_ == nullptr || fn_->Body == nullptr)
            return;
        std::function<void(BlockContainer*)> scan = [&](BlockContainer* c) {
            if (c == nullptr) return;
            for (std::size_t k = 0; k + 1 < c->Blocks.size(); ++k) {
                Block* b0 = c->Blocks[k].get();
                Block* b1 = c->Blocks[k + 1].get();
                if (b0 == nullptr || b1 == nullptr)
                    continue;
                if (b0->Instructions.size() < 2)
                    continue;
                // The dumped shape: [.., a = expr, b = a] as the last two
                // statements, the guard if as the block's FINAL
                // (If(LdLoc a, Branch->L, Nop)).
                auto* storeA = dynamic_cast<StLoc*>(
                    b0->Instructions[b0->Instructions.size() - 2].get());
                auto* copyB = dynamic_cast<StLoc*>(
                    b0->Instructions[b0->Instructions.size() - 1].get());
                auto* iff = dynamic_cast<IfInstruction*>(
                    b0->FinalInstruction.get());
                if (storeA == nullptr || copyB == nullptr || iff == nullptr)
                    continue;
                // The guard: an if over the temporary, the true-arm a branch
                // to L, the false-arm a Nop (the reader's empty-arm form).
                if (iff->Condition == nullptr ||
                    iff->Condition->Op != OpCode::LdLoc ||
                    iff->TrueInst == nullptr ||
                    iff->TrueInst->Op != OpCode::Branch)
                    continue;
                if (iff->FalseInst != nullptr &&
                    iff->FalseInst->Op != OpCode::Nop)
                    continue;
                ILVariable* temp =
                    static_cast<LdLoc*>(iff->Condition.get())->Variable.get();
                auto* guardBr = static_cast<Branch*>(iff->TrueInst.get());
                if (temp == nullptr || guardBr->TargetBlock == nullptr)
                    continue;
                if (copyB->Value == nullptr ||
                    copyB->Value->Op != OpCode::LdLoc ||
                    static_cast<LdLoc*>(copyB->Value.get())->Variable.get() !=
                        temp)
                    continue;
                ILVariable* target = copyB->Variable.get();
                if (target == nullptr || target == temp)
                    continue;
                if (storeA->Variable.get() != temp ||
                    storeA->Value == nullptr)
                    continue;
                // The alternative block: the single `b = alt;` store whose
                // final branches to L (both paths join there).
                if (b1->Instructions.size() != 1)
                    continue;
                auto* alt = dynamic_cast<StLoc*>(b1->Instructions[0].get());
                if (alt == nullptr || alt->Variable.get() != target ||
                    alt->Value == nullptr)
                    continue;
                if (b1->FinalInstruction == nullptr ||
                    b1->FinalInstruction->Op != OpCode::Branch ||
                    static_cast<Branch*>(b1->FinalInstruction.get())
                            ->TargetBlock != guardBr->TargetBlock)
                    continue;
                // The temporary's loads: exactly the copy and the guard.
                if (temp->LoadCount != 2)
                    continue;
                // The target's stores: exactly the copy and the
                // alternative.
                if (target->StoreCount != 2)
                    continue;
                // The alternative block must have NO other branch
                // predecessor (the measured constraint).
                int b1Preds = 0;
                std::function<void(const ILInstruction*)> countB1 =
                    [&](const ILInstruction* i) {
                    if (i == nullptr) return;
                    if (i->Op == OpCode::Branch) {
                        auto* b = static_cast<const Branch*>(i);
                        if (b->TargetBlock == b1) b1Preds++;
                    }
                    for (int ci = 0; ci < i->ChildCount(); ++ci)
                        countB1(i->GetChild(ci));
                };
                countB1(root);
                if (b1Preds != 0)
                    continue;
                // The composed fold: the NullCoalescing takes the
                // ownership of the two expressions (released from the
                // skipped statements -- they are never rendered, so the
                // null values are inert; the keepalive vector owns the
                // node for the render's lifetime).
                coalesceKeepAlive_.push_back(
                    std::make_unique<NullCoalescingInstruction>(
                        NullCoalescingKind::Ref,
                        std::move(storeA->Value),
                        std::move(alt->Value)));
                coalesceFolds_[copyB] = coalesceKeepAlive_.back().get();
                coalesceSkipped_.insert(storeA);
                coalesceSkipped_.insert(iff);
                coalesceSuppressed_.insert(b1);
                // The single-use elision: a target with exactly one load
                // site inlines the coalesce into that use and drops the
                // declaration (the C# oracle's form -- `new ResourceName(
                // ReadString(...) ?? string.Empty)`). The safety gates:
                // the load is in the same container, in a LATER block than
                // the declaration (the straight-line flow -- a loop back
                // edge would re-evaluate a side-effecting expression), and
                // the container is not a loop.
                if (target->LoadCount == 1) {
                    const LdLoc* useSite = nullptr;
                    std::function<void(const ILInstruction*)> findLoad =
                        [&](const ILInstruction* i) {
                        if (i == nullptr || useSite != nullptr) return;
                        if (i->Op == OpCode::LdLoc) {
                            auto* l = static_cast<const LdLoc*>(i);
                            if (l->Variable.get() == target)
                                useSite = l;
                        }
                        for (int ci = 0; ci < i->ChildCount(); ++ci)
                            findLoad(i->GetChild(ci));
                    };
                    findLoad(root);
                    if (useSite != nullptr) {
                        // The use site sits inside a nested expression
                        // (the ctor's arguments); walk the ancestors to its
                        // enclosing block, then the ancestor-chain
                        // dominance (the single-use elision's rule: the
                        // chain reaches the declaration's container at an
                        // index >= the declaration's; the loops are
                        // covered by the coalesce's own purity -- the
                        // folded expression is the original store's).
                        const Block* useBlock = nullptr;
                        for (const ILInstruction* p = useSite; p != nullptr;
                             p = p->Parent) {
                            if (p->Op == OpCode::Block) {
                                useBlock = static_cast<const Block*>(p);
                                break;
                            }
                        }
                        bool dominated = false;
                        if (useBlock != nullptr) {
                            const Block* ub = useBlock;
                            auto* uc =
                                dynamic_cast<BlockContainer*>(useBlock->Parent);
                            while (uc != nullptr) {
                                if (uc == c) {
                                    for (std::size_t ui = 0;
                                         ui < c->Blocks.size(); ++ui) {
                                        if (c->Blocks[ui].get() == ub) {
                                            if (ui > k)
                                                dominated = true;
                                            break;
                                        }
                                    }
                                    break;
                                }
                                const Block* pb = nullptr;
                                for (const ILInstruction* p = uc->Parent;
                                     p != nullptr; p = p->Parent) {
                                    if (p->Op == OpCode::Block) {
                                        pb = static_cast<const Block*>(p);
                                        break;
                                    }
                                }
                                if (pb == nullptr)
                                    break;
                                ub = pb;
                                uc = dynamic_cast<BlockContainer*>(pb->Parent);
                            }
                        }
                        if (dominated) {
                            coalesceElisions_[useSite] =
                                coalesceKeepAlive_.back().get();
                            coalesceSkipped_.insert(copyB);
                        }
                    }
                }
            }
            // The nested containers sit inside the blocks' instructions
            // (a container's direct children are Blocks), so the descent
            // walks every instruction subtree for the containers.
            std::function<void(const ILInstruction*)> descend =
                [&](const ILInstruction* i) {
                if (i == nullptr) return;
                if (auto* nested =
                        dynamic_cast<const BlockContainer*>(i))
                    scan(const_cast<BlockContainer*>(nested));
                for (int ci = 0; ci < i->ChildCount(); ++ci)
                    descend(i->GetChild(ci));
            };
            for (const auto& bi : c->Blocks) {
                if (!bi) continue;
                for (const auto& si : bi->Instructions) descend(si.get());
                if (bi->FinalInstruction)
                    descend(bi->FinalInstruction.get());
            }
        };
        scan(dynamic_cast<BlockContainer*>(root));
    }

    // The general single-use elision (the C# ILInlining's single-use rule):
    // one load, one store, no address-taken, the store's expression pure,
    // and the use after the declaration in a non-loop container's
    // straight-line flow. The byref uses (ldloca) are excluded by the
    // load-count gate; the coalesce-folded targets are already handled
    // (their elisions recorded by the fold).
    void AnalyzeSingleUseLocals() {
        if (fn_ == nullptr || fn_->Body == nullptr)
            return;
        // The variable collection walks the tree, not fn_->Variables: the
        // reader's dup-slot temporaries (the S_/dup_ names) are created
        // outside the function's registered variable list, so the list
        // iteration missed them entirely (measured: the [SU] debug showed
        // zero S_/dup_ entries while the renders carried them).
        std::vector<ILVariable*> variables;
        {
            std::set<ILVariable*> seen;
            std::function<void(const ILInstruction*)> collect =
                [&](const ILInstruction* i) {
                if (i == nullptr) return;
                ILVariable* var = nullptr;
                if (i->Op == OpCode::StLoc)
                    var = static_cast<const StLoc*>(i)->Variable.get();
                else if (i->Op == OpCode::LdLoc)
                    var = static_cast<const LdLoc*>(i)->Variable.get();
                else if (i->Op == OpCode::LdLoca)
                    var = static_cast<const LdLoca*>(i)->Variable.get();
                if (var != nullptr && seen.insert(var).second)
                    variables.push_back(var);
                for (int ci = 0; ci < i->ChildCount(); ++ci)
                    collect(i->GetChild(ci));
            };
            collect(fn_->Body.get());
        }
        for (const auto& v : variables) {
            if (v == nullptr || v->Kind != VariableKind::Local)
                continue;
            if (v->LoadCount != 1 || v->StoreCount != 1 ||
                v->AddressCount != 0)
                continue;
            // The coalesce fold already owns the single-use targets it
            // elided.
            bool coalesced = false;
            for (const auto& [stLoc, nc] : coalesceFolds_)
                if (stLoc->Variable.get() == v) {
                    coalesced = true;
                    break;
                }
            if (coalesced)
                continue;
            // The single store (the StLoc) and the single load (the walk).
            StLoc* store = nullptr;
            std::function<void(ILInstruction*)> findStore =
                [&](ILInstruction* i) {
                if (i == nullptr || store != nullptr) return;
                if (i->Op == OpCode::StLoc) {
                    auto* st = static_cast<StLoc*>(i);
                    if (st->Variable.get() == v)
                        store = st;
                }
                for (int ci = 0; ci < i->ChildCount(); ++ci)
                    findStore(i->GetChild(ci));
            };
            findStore(fn_->Body.get());
            if (store == nullptr || store->Value == nullptr)
                continue;
            // The pure gate: a side-effecting expression re-evaluated at
            // the use site (or moved across a branch) changes behavior.
            if (!IsPure(store->Value->Flags()))
                continue;
            const LdLoc* useSite = nullptr;
            std::function<void(const ILInstruction*)> findLoad =
                [&](const ILInstruction* i) {
                if (i == nullptr || useSite != nullptr) return;
                if (i->Op == OpCode::LdLoc) {
                    auto* l = static_cast<const LdLoc*>(i);
                    if (l->Variable.get() == v)
                        useSite = l;
                }
                for (int ci = 0; ci < i->ChildCount(); ++ci)
                    findLoad(i->GetChild(ci));
            };
            findLoad(fn_->Body.get());
            if (useSite == nullptr)
                continue;
            // The enclosing blocks (the ancestor walk -- the use sits
            // inside nested expressions).
            const Block* storeBlock = nullptr;
            for (const ILInstruction* p = store; p != nullptr; p = p->Parent)
                if (p->Op == OpCode::Block) {
                    storeBlock = static_cast<const Block*>(p);
                    break;
                }
            const Block* useBlock = nullptr;
            for (const ILInstruction* p = useSite; p != nullptr; p = p->Parent)
                if (p->Op == OpCode::Block) {
                    useBlock = static_cast<const Block*>(p);
                    break;
                }
            if (storeBlock == nullptr || useBlock == nullptr)
                continue;
            auto* container =
                dynamic_cast<BlockContainer*>(storeBlock->Parent);
            if (container == nullptr)
                continue;
            std::size_t storeIndex = 0;
            bool foundStore = false;
            for (std::size_t ui = 0; ui < container->Blocks.size(); ++ui) {
                if (container->Blocks[ui].get() == storeBlock) {
                    storeIndex = ui;
                    foundStore = true;
                    break;
                }
            }
            if (!foundStore)
                continue;
            // The dominance approximation (the C#'s flow-insensitive
            // rule): walk up from the use's block through the nested
            // containers; the use is dominated by the declaration when
            // the chain reaches the declaration's container at a block
            // index >= the declaration's. The loop containers are no
            // longer excluded -- the pure gate covers the re-evaluation
            // (a pure expression re-evaluated per iteration is
            // semantically inert, and the C# inlines the same shapes).
            bool dominated = false;
            {
                const Block* ub = useBlock;
                auto* uc = dynamic_cast<BlockContainer*>(useBlock->Parent);
                while (uc != nullptr) {
                    if (uc == container) {
                        for (std::size_t ui = 0; ui < container->Blocks.size();
                             ++ui) {
                            if (container->Blocks[ui].get() == ub) {
                                if (ui >= storeIndex)
                                    dominated = true;
                                break;
                            }
                        }
                        break;
                    }
                    // Up one level: the container's parent block (the
                    // container is an instruction inside it).
                    const Block* pb = nullptr;
                    for (const ILInstruction* p = uc->Parent; p != nullptr;
                         p = p->Parent) {
                        if (p->Op == OpCode::Block) {
                            pb = static_cast<const Block*>(p);
                            break;
                        }
                    }
                    if (pb == nullptr)
                        break;
                    ub = pb;
                    uc = dynamic_cast<BlockContainer*>(pb->Parent);
                }
            }
            if (!dominated)
                continue;
            singleUseElisions_[useSite] = store->Value.get();
            singleUseSkipped_.insert(store);
        }
    }

    // The null-propagation fold: the two-block shape with the INLINE
    // leave-null true arm (the `if (dup == 0) return null;` guard) and the
    // fall-through use. The dup's uses counted from the tree (the reader
    // does not track the dup slots' counts).
    void AnalyzeNullPropagation() {
        if (fn_ == nullptr || fn_->Body == nullptr)
            return;
        std::function<void(BlockContainer*)> scan = [&](BlockContainer* c) {
            if (c == nullptr) return;
            for (std::size_t k = 0; k + 1 < c->Blocks.size(); ++k) {
                Block* b0 = c->Blocks[k].get();
                Block* b1 = c->Blocks[k + 1].get();
                if (b0 == nullptr || b1 == nullptr)
                    continue;
                if (b0->Instructions.size() != 1)
                    continue;
                auto* storeDup = dynamic_cast<StLoc*>(
                    b0->Instructions[0].get());
                if (storeDup == nullptr || storeDup->Value == nullptr)
                    continue;
                ILVariable* dup = storeDup->Variable.get();
                if (dup == nullptr)
                    continue;
                if (b0->FinalInstruction == nullptr ||
                    b0->FinalInstruction->Op != OpCode::IfInstruction)
                    continue;
                auto* iff = static_cast<IfInstruction*>(
                    b0->FinalInstruction.get());
                if (iff->Condition == nullptr ||
                    iff->Condition->Op != OpCode::Comp ||
                    iff->FalseInst != nullptr)
                    continue;
                auto* comp = static_cast<Comp*>(iff->Condition.get());
                if (comp->Left == nullptr || comp->Right == nullptr ||
                    comp->Left->Op != OpCode::LdLoc ||
                    static_cast<LdLoc*>(comp->Left.get())->Variable.get() !=
                        dup)
                    continue;
                // The true arm: the inline null return (`return null`).
                if (iff->TrueInst == nullptr ||
                    iff->TrueInst->Op != OpCode::Leave)
                    continue;
                auto* nullLeave = static_cast<Leave*>(iff->TrueInst.get());
                if (nullLeave->Value == nullptr ||
                    nullLeave->Value->Op != OpCode::LdNull)
                    continue;
                // The use: b1's final is a CALL whose first argument is
                // the dup's own load.
                if (!b1->Instructions.empty() ||
                    b1->FinalInstruction == nullptr ||
                    b1->FinalInstruction->Op != OpCode::Leave)
                    continue;
                auto* useLeave = static_cast<Leave*>(
                    b1->FinalInstruction.get());
                if (useLeave->Value == nullptr ||
                    useLeave->Value->Op != OpCode::Call)
                    continue;
                auto* useCall = static_cast<Call*>(useLeave->Value.get());
                if (useCall->Arguments.empty() ||
                    useCall->Arguments[0]->Op != OpCode::LdLoc ||
                    static_cast<LdLoc*>(useCall->Arguments[0].get())
                            ->Variable.get() != dup)
                    continue;
                // The dup's uses from the tree: the guard's comp + the
                // use's receiver = 2 loads, 1 store.
                int dupLoads = 0, dupStores = 0;
                std::function<void(const ILInstruction*)> countUses =
                    [&](const ILInstruction* i) {
                    if (i == nullptr) return;
                    if (i->Op == OpCode::LdLoc) {
                        auto* l = static_cast<const LdLoc*>(i);
                        if (l->Variable.get() == dup) dupLoads++;
                    }
                    if (i->Op == OpCode::StLoc) {
                        auto* st = static_cast<const StLoc*>(i);
                        if (st->Variable.get() == dup) dupStores++;
                    }
                    for (int ci = 0; ci < i->ChildCount(); ++ci)
                        countUses(i->GetChild(ci));
                };
                countUses(fn_->Body.get());
                if (dupLoads != 2 || dupStores != 1)
                    continue;
                nullPropagation_[useCall] = storeDup->Value.get();
                coalesceSkipped_.insert(storeDup);
                coalesceSkipped_.insert(iff);
            }
            std::function<void(const ILInstruction*)> descend =
                [&](const ILInstruction* i) {
                if (i == nullptr) return;
                if (auto* nested =
                        dynamic_cast<const BlockContainer*>(i))
                    scan(const_cast<BlockContainer*>(nested));
                for (int ci = 0; ci < i->ChildCount(); ++ci)
                    descend(i->GetChild(ci));
            };
            for (const auto& bi : c->Blocks) {
                if (!bi) continue;
                for (const auto& si : bi->Instructions) descend(si.get());
                if (bi->FinalInstruction)
                    descend(bi->FinalInstruction.get());
            }
        };
        scan(dynamic_cast<BlockContainer*>(fn_->Body.get()));
    }

    // The merged-region if/else render: `if (cond) { r2's statements }
    // else { r1's statements }` (the branch target is the true arm).
    void EmitFoldedIf(const IfInstruction& iff, Block* r1, Block* r2,
                      int indent) {
        // The oracle's orientation: the fall-through region is the true
        // arm under the INVERTED condition (the branch guard's `br` is
        // the negative path), the branch target the else. A Comp
        // condition negates through its kind; a value condition wraps
        // parenthesized (the `==` binds tighter than the bitwise `&`).
        std::string condText =
            dynamic_cast<const Comp*>(iff.Condition.get()) != nullptr
                ? NegateCondText(*iff.Condition)
                : "(" + Expr(*iff.Condition) + ") == 0";
        Line(indent, "if (" + condText + ")");
        Line(indent, "{");
        if (r1 != nullptr)
            for (const auto& st : r1->Instructions)
                EmitStatement(*st, indent + 1);
        Line(indent, "}");
        Line(indent, "else");
        Line(indent, "{");
        if (r2 != nullptr)
            for (const auto& st : r2->Instructions)
                EmitStatement(*st, indent + 1);
        Line(indent, "}");
    }

    // The label-region fold: the if-final's branch to a later sibling
    // whose region is one block, with the one-block fall-through region,
    // both exiting to the shared continuation.
    void AnalyzeLabelRegions() {
        if (fn_ == nullptr || fn_->Body == nullptr)
            return;
        std::function<void(BlockContainer*)> scan = [&](BlockContainer* c) {
            if (c == nullptr) return;
            for (std::size_t k = 0; k + 2 < c->Blocks.size(); ++k) {
                Block* bK = c->Blocks[k].get();
                Block* bR1 = c->Blocks[k + 1].get();
                Block* bT = c->Blocks[k + 2].get();
                if (bK == nullptr || bR1 == nullptr || bT == nullptr)
                    continue;
                if (bK->FinalInstruction == nullptr ||
                    bK->FinalInstruction->Op != OpCode::IfInstruction)
                    continue;
                auto* iff = static_cast<IfInstruction*>(
                    bK->FinalInstruction.get());
                if (iff->Condition == nullptr ||
                    iff->TrueInst == nullptr ||
                    iff->TrueInst->Op != OpCode::Branch ||
                    iff->FalseInst != nullptr)
                    continue;
                auto* guardBr = static_cast<Branch*>(iff->TrueInst.get());
                if (guardBr->TargetBlock != bT)
                    continue;
                // The target's only predecessor: the guard's branch.
                int preds = 0;
                std::function<void(const ILInstruction*)> count =
                    [&](const ILInstruction* i) {
                    if (i == nullptr) return;
                    if (i->Op == OpCode::Branch) {
                        auto* b = static_cast<const Branch*>(i);
                        if (b->TargetBlock == bT) preds++;
                    }
                    for (int ci = 0; ci < i->ChildCount(); ++ci)
                        count(i->GetChild(ci));
                };
                count(fn_->Body.get());
                if (preds != 1) continue;
                // The two regions' exits: the identical continuation
                // (both branch/leave the same target block, the one after
                // bT in order -- the fall-through equivalence).
                auto exitOf = [](Block* b) -> Block* {
                    if (b == nullptr || b->FinalInstruction == nullptr)
                        return (Block*)nullptr;
                    if (b->FinalInstruction->Op == OpCode::Branch)
                        return static_cast<Branch*>(
                                   b->FinalInstruction.get())
                            ->TargetBlock;
                    return (Block*)nullptr;
                };
                if (k + 3 >= c->Blocks.size()) continue;
                Block* after = c->Blocks[k + 3].get();
                if (exitOf(bR1) != after || exitOf(bT) != after)
                    continue;
                // Both regions must be single-block straight-line bodies
                // (statements + the exit branch).
                if (!bR1->Instructions.empty() && !bT->Instructions.empty()) {
                }
                labelRegionFolds_[iff] = std::make_pair(bR1, bT);
                labelRegionSuppressed_.insert(bR1);
                labelRegionSuppressed_.insert(bT);
            }
            std::function<void(const ILInstruction*)> descend =
                [&](const ILInstruction* i) {
                if (i == nullptr) return;
                if (auto* nested =
                        dynamic_cast<const BlockContainer*>(i))
                    scan(const_cast<BlockContainer*>(nested));
                for (int ci = 0; ci < i->ChildCount(); ++ci)
                    descend(i->GetChild(ci));
            };
            for (const auto& bi : c->Blocks) {
                if (!bi) continue;
                for (const auto& si : bi->Instructions) descend(si.get());
                if (bi->FinalInstruction)
                    descend(bi->FinalInstruction.get());
            }
        };
        scan(dynamic_cast<BlockContainer*>(fn_->Body.get()));
    }

    // Every branch-target block gets an IL_XXXX label; walk the whole tree so
    // branches nested in if/switch arms are covered too.
    void CollectLoopHeaders(const ILInstruction* inst) {
        if (!inst) return;
        if (auto* c = dynamic_cast<const BlockContainer*>(inst)) {
            if ((c->Kind == ContainerKind::Loop || c->Kind == ContainerKind::While ||
                 c->Kind == ContainerKind::For || c->Kind == ContainerKind::DoWhile) && !c->Blocks.empty()) {
                if (const Block* exit = LoopExitBlock(c)) loopExits_[c] = exit;
                if (c->Kind == ContainerKind::Loop || c->Kind == ContainerKind::While ||
                    c->Kind == ContainerKind::For)
                    loopHeaders_.insert(c->Blocks.front().get());
                // For a While container, the body entry (the target of the
                // condition's true-arm Branch) is also unlabeled -- it's the
                // implicit body start, not a goto target.
                if (c->Kind == ContainerKind::While || c->Kind == ContainerKind::For) {
                    const Block* entry = c->Blocks.front().get();
                    if (entry && entry->FinalInstruction &&
                        entry->FinalInstruction->Op == OpCode::IfInstruction) {
                        const auto& iff = static_cast<const IfInstruction&>(*entry->FinalInstruction);
                        if (iff.TrueInst && iff.TrueInst->Op == OpCode::Branch)
                            loopHeaders_.insert(static_cast<const Branch*>(iff.TrueInst.get())->TargetBlock);
                    }
                }
                // For a For container the last block is the increment block: a
                // branch to it is a `continue` (C# `continue` runs the
                // for-update), not a goto target.
                if (c->Kind == ContainerKind::For && c->Blocks.size() >= 2)
                    loopHeaders_.insert(c->Blocks.back().get());
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) CollectLoopHeaders(inst->GetChild(i));
    }

    // Whether `br` renders as a `continue` (its target is a loop header AND
    // the branch is inside that loop's container). Mirrors the `GotoText`
    // continue check so `CollectLabels` only labels a loop header when a
    // branch to it renders as a real `goto` (from outside the loop), not a
    // `continue` (from inside -- no label, the loop head carries none). A
    // branch to a loop header from outside that is not a fall-through needs a
    // label before the loop so the `goto` is not a dangling reference.
    bool IsContinueBranch(const Branch* br) const {
        if (!br || !br->TargetBlock) return false;
        if (loopHeaders_.find(br->TargetBlock) == loopHeaders_.end()) return false;
        auto* loopContainer = dynamic_cast<BlockContainer*>(br->TargetBlock->Parent);
        if (!loopContainer) return false;
        for (const ILInstruction* p = br; p; p = p->Parent)
            if (p == loopContainer) return true;
        return false;
    }

    // The C# PatternStatementTransform's TransformFor hoist, as a render
    // pre-pass: a For container whose preceding statement (the host
    // block's previous instruction, or the previous sibling block's last
    // instruction -- the loop pre-header's trailing store) is `v = init;`
    // with v used by the loop's condition or increment, moves that store
    // into the for's initializer. The declaration rides the hoist (the
    // variable's first store): `for (T v = init; ...)`.
    void HoistForInitializers(ILInstruction* inst) {
        if (inst == nullptr) return;
        // Phase 1: collect the candidate pairs (the walk must not mutate
        // while descending -- the erase below shifts the sibling slots).
        struct Hoist {
            BlockContainer* container;
            Block* hostBlock;
            int idx;
            StLoc* st;
        };
        std::vector<Hoist> candidates;
        std::function<void(ILInstruction*)> collect =
            [&](ILInstruction* node) {
                if (node == nullptr) return;
                if (auto* container = dynamic_cast<BlockContainer*>(node)) {
                    if (container->Kind == ContainerKind::For &&
                        !container->Blocks.empty() &&
                        hoistedForInits_.find(container) ==
                            hoistedForInits_.end()) {
                        auto* parentBlock =
                            dynamic_cast<Block*>(container->Parent);
                        ILInstruction* prevStmt = nullptr;
                        Block* hostBlock = parentBlock;
                        int idx = container->ChildIndex;
                        if (parentBlock != nullptr && idx > 0 &&
                            static_cast<std::size_t>(idx) <=
                                parentBlock->Instructions.size()) {
                            --idx;
                            prevStmt =
                                parentBlock->Instructions[idx].get();
                        } else if (parentBlock != nullptr) {
                            auto* outer = dynamic_cast<BlockContainer*>(
                                parentBlock->Parent);
                            if (outer != nullptr) {
                                for (std::size_t i = 0;
                                     i < outer->Blocks.size(); ++i) {
                                    if (outer->Blocks[i].get() !=
                                            parentBlock ||
                                        i == 0)
                                        continue;
                                    Block* prevBlock =
                                        outer->Blocks[i - 1].get();
                                    if (prevBlock != nullptr &&
                                        !prevBlock->Instructions.empty()) {
                                        prevStmt = prevBlock->Instructions
                                                       .back()
                                                       .get();
                                        hostBlock = prevBlock;
                                        idx = static_cast<int>(
                                            prevBlock->Instructions.size() -
                                            1);
                                    }
                                    break;
                                }
                            }
                        }
                        if (prevStmt != nullptr &&
                            prevStmt->Op == OpCode::StLoc)
                            candidates.push_back(Hoist{container, hostBlock,
                                                       idx,
                                                       static_cast<StLoc*>(
                                                           prevStmt)});
                    }
                }
                for (int c = 0; c < node->ChildCount(); ++c)
                    collect(node->GetChild(c));
            };
        collect(inst);
        // Phase 2: validate and apply (the loop-use check, the init text,
        // the removal from the host block).
        for (const Hoist& h : candidates) {
            ILVariable* v = h.st->Variable.get();
            if (v == nullptr || v->Kind == VariableKind::Parameter) continue;
            bool loopUsesV = false;
            std::function<void(const ILInstruction*)> usesVar =
                [&](const ILInstruction* i) {
                    if (i == nullptr) return;
                    if (i->Op == OpCode::LdLoc &&
                        static_cast<const LdLoc*>(i)->Variable.get() == v)
                        loopUsesV = true;
                    for (int c = 0; c < i->ChildCount(); ++c)
                        usesVar(i->GetChild(c));
                };
            const Block* header = h.container->Blocks.front().get();
            if (header && header->FinalInstruction)
                usesVar(header->FinalInstruction.get());
            for (const auto& b : h.container->Blocks) {
                if (!b || b.get() == header) continue;
                for (const auto& i2 : b->Instructions)
                    if (i2) usesVar(i2.get());
            }
            if (!loopUsesV) continue;
            hoistedForInits_[h.container] = ForHoist{
                CSharpTypeName(v->Type) + " " + v->Name + " = " +
                    (h.st->Value ? Expr(*h.st->Value)
                                 : std::string("(default)")),
                v->Name};
            h.hostBlock->Instructions.erase(
                h.hostBlock->Instructions.begin() + h.idx);
        }
    }

    void CollectLabels(const ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::SwitchInstruction)
            AnalyzeSwitchInline(*static_cast<const SwitchInstruction*>(inst));
        if (inst->Op == OpCode::Branch && breakBranches_.count(static_cast<const Branch*>(inst)))
            return;  // a `break`-replaced branch emits no goto, so no label
        if (inst->Op == OpCode::Branch) {
            const auto* br = static_cast<const Branch*>(inst);
            // Only a branch that will render a `goto` gets a label; dropped
            // branches reference the target but emit nothing, so the label must
            // not be created (it would be orphaned). A branch to a loop header
            // from outside the loop (not a `continue`) DOES need a label so the
            // `goto` is not a dangling reference -- the loop emits the header's
            // label before its keyword (EmitContainer).
            if (br->TargetBlock && !IsFallThroughGoto(br) && !IsLoopEntryFallThrough(br) &&
                !IsLoopBreak(*br) && !IsContinueBranch(br) &&
                labels_.find(br->TargetBlock) == labels_.end())
                labels_[br->TargetBlock] = LabelFor(br->TargetOffset);
        }
        for (int i = 0; i < inst->ChildCount(); ++i) CollectLabels(inst->GetChild(i));
    }

    // True if `br` is a redundant `goto nextBlock`: the branch's enclosing
    // block's container has the target as the block after the enclosing block,
    // AND the branch is the enclosing block's FinalInstruction (a plain block
    // final, not a switch section / if arm -- those render their gotos
    // conventionally). The enclosing block falls through, so the goto is a
    // no-op.
    static bool IsFallThroughGoto(const Branch* br) {
        if (!br || !br->TargetBlock) return false;
        // Walk up to the enclosing block (the first Block ancestor).
        Block* encBlock = nullptr;
        for (const ILInstruction* p = br; p; p = p->Parent) {
            encBlock = const_cast<Block*>(dynamic_cast<const Block*>(p));
            if (encBlock) break;
        }
        if (!encBlock) return false;
        // The branch must be a block-final fall-through: either it IS the
        // enclosing block's FinalInstruction, or it is the TRUE ARM of the
        // block-final `if (cond) br target` (the enclosing block's final is
        // that if). A no-else if's true-arm br is a redundant no-op when the
        // block's textual next is the target (both paths reach it: true via
        // the branch, false via fall-through). The same holds for an if WITH
        // an else: dropping the true-arm br makes the true path fall to the
        // block's textual next (== the target), and the false path (the else
        // arm) is untouched -- so the `!ifFinal->FalseInst` restriction is
        // unnecessary; the textual-next == target check below is the guard.
        // A branch inside an if-arm whose final is not the enclosing block's
        // final, or inside a switch section, is not a block-final fall-through.
        const ILInstruction* final = encBlock->FinalInstruction.get();
        bool isFinal = (final == br);
        if (!isFinal) {
            if (auto* ifFinal = dynamic_cast<const IfInstruction*>(final))
                isFinal = (ifFinal->TrueInst.get() == br);
        }
        if (!isFinal) return false;
        // The textually next block, walking up construct boundaries (out of
        // a using/try body to the following block).
        const Block* nxt = TextuallyNextEmittedBlock(encBlock);
        if (FirstEmittedBlockOf(nxt) == br->TargetBlock) return true;
        // Relay skip: `nxt` may be a transparent relay (no instructions + a
        // forward Branch final) that emits nothing, with the real target one
        // or more relays further on in the same container. Walk forward from
        // encBlock's container position, stopping AT the target (a `br` to a
        // relay that is the immediate next still drops -- fall-through reaches
        // the relay), skipping relays that are not the target, stopping at the
        // first block with content. Only applies when encBlock is a direct
        // child of a container (the common block-final and if-arm cases).
        if (auto* c = dynamic_cast<const BlockContainer*>(encBlock->Parent)) {
            for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                if (c->Blocks[i].get() != encBlock) continue;
                for (std::size_t j = i + 1; j < c->Blocks.size(); ++j) {
                    if (c->Blocks[j].get() == br->TargetBlock) return true;
                    if (IsTransparentRelay(c, j)) continue;
                    break;
                }
                break;
            }
        }
        return false;
    }

    // The block the emitter visits next in textual emission order after
    // `block`, walking up construct boundaries: inside a container it is the
    // next block; at a container's end, the container node's own position
    // inside its enclosing BLOCK determines the fall-through -- only when the
    // construct is the enclosing block's last emitted statement (no remaining
    // instructions, no (relayed) final) does the fall-through continue to the
    // enclosing block's own next block. Returns nullptr when the fall-through
    // does not reach a block (e.g. into more statements, a switch arm, or off
    // the function's end).
    static const Block* TextuallyNextEmittedBlock(const ILInstruction* node) {
        const ILInstruction* cur = node;
        while (cur) {
            if (const Block* b = dynamic_cast<const Block*>(cur)) {
                const auto* c = dynamic_cast<const BlockContainer*>(b->Parent);
                if (!c) {
                    // The block's parent is a construct node (an if-arm, a
                    // try/using body) -- the block is the end of that
                    // construct's body, so fall through to the construct node's
                    // own position (the walk-up below continues from there).
                    cur = b->Parent;
                    continue;
                }
                bool advanced = false;
                for (std::size_t i = 0; i < c->Blocks.size(); ++i) {
                    if (c->Blocks[i].get() == b) {
                        if (i + 1 < c->Blocks.size()) return c->Blocks[i + 1].get();
                        // End of this container: continue from whatever owns
                        // the container as a statement (a construct node or an
                        // enclosing Block).
                        cur = c;
                        advanced = true;
                        break;
                    }
                }
                if (!advanced) return nullptr;
                continue;
            }
            // cur is a container or a construct node: walk up through
            // non-Block owners (a UsingInstruction/TryCatch owning its body
            // container) until a Block carries the whole subtree as one of its
            // statements. That block must carry it as the last emitted
            // statement -- the final member of pb->Instructions with no
            // additional final, or with a final that is itself only a
            // fall-through Branch to pb's own textual next (the two drops
            // match), OR the block's FinalInstruction itself (an if-else as
            // the block's terminator is the last emitted thing).
            const ILInstruction* owner = cur->Parent;
            if (!owner) return nullptr;
            if (!dynamic_cast<const Block*>(owner)) { cur = owner; continue; }
            const Block* pb = static_cast<const Block*>(owner);
            const ILInstruction* final = pb->FinalInstruction.get();
            bool lastInInstrs = !pb->Instructions.empty() &&
                                 pb->Instructions.back().get() == cur;
            bool isTheFinal = (final == cur);
            if (!lastInInstrs && !isTheFinal) return nullptr;
            if (isTheFinal) {
                // cur is pb's terminator (e.g. an if-else as the block's
                // final) -- after it, the emitter is done with pb's content;
                // fall through to pb's own next block.
                cur = pb;
                continue;
            }
            // cur is pb's last statement; the terminator (final) must also
            // fall through to pb's textual next.
            if (final) {
                auto* fb = dynamic_cast<const Branch*>(final);
                if (!fb || fb->TargetBlock != TextuallyNextEmittedBlock(pb))
                    return nullptr;
            }
            cur = pb;
        }
        return nullptr;
    }

    // The first block the emitter visits inside `block`: if `block` is a
    // "construct-leading" block -- its first instruction (or, with no
    // instructions, its final) is a TryFinally/TryCatch/Using/Lock whose body is
    // a BlockContainer -- descend into the construct body's first block
    // (recursively). Otherwise the block itself is emitted first (its
    // instructions, or its non-construct final). Used by IsFallThroughGoto so a
    // `goto X` whose target X is the entry of the immediately-following
    // block's construct body (a `goto X; <block>{ try { X: ... } ... }` shape,
    // where the try is a leading statement of the next block) is recognized
    // as a redundant fall-into-construct.
    static const BlockContainer* ConstructEntryBody(const ILInstruction* inst) {
        if (!inst) return nullptr;
        if (auto* tf = dynamic_cast<const TryFinally*>(inst))
            return dynamic_cast<const BlockContainer*>(tf->TryBlock.get());
        if (auto* tc = dynamic_cast<const TryCatch*>(inst))
            return dynamic_cast<const BlockContainer*>(tc->TryBlock.get());
        if (auto* u = dynamic_cast<const UsingInstruction*>(inst))
            return dynamic_cast<const BlockContainer*>(u->Body.get());
        if (auto* l = dynamic_cast<const LockInstruction*>(inst))
            return dynamic_cast<const BlockContainer*>(l->Body.get());
        return nullptr;
    }
    // Whether `container`'s block at index `j` is a transparent relay: no
    // non-Nop instructions and a Branch final to a LATER block in the same
    // container (a forward relay that emits nothing -- its own goto drops via
    // the same fall-through logic). Backward-Branch finals (continue/goto) and
    // blocks with content or a non-Branch final are NOT transparent.
    static bool IsTransparentRelay(const BlockContainer* c, std::size_t j) {
        const Block* bj = c->Blocks[j].get();
        if (!bj) return true;
        for (const auto& inst : bj->Instructions)
            if (inst && inst->Op != OpCode::Nop) return false;
        const auto* f = bj->FinalInstruction.get();
        if (!f || f->Op != OpCode::Branch) return false;
        auto* bbr = static_cast<const Branch*>(f);
        for (std::size_t k = j + 1; k < c->Blocks.size(); ++k)
            if (c->Blocks[k].get() == bbr->TargetBlock) return true;
        return false;
    }
    static const Block* NextEmittedSibling(const BlockContainer* c, std::size_t i) {
        for (std::size_t j = i + 1; j < c->Blocks.size(); ++j) {
            if (IsTransparentRelay(c, j)) continue;
            return c->Blocks[j].get();
        }
        return nullptr;
    }

    static const Block* FirstEmittedBlockOf(const Block* block) {
        while (block) {
            const ILInstruction* entry = nullptr;
            if (!block->Instructions.empty())
                entry = block->Instructions.front().get();
            else
                entry = block->FinalInstruction.get();
            if (!entry) return block;
            const BlockContainer* body = ConstructEntryBody(entry);
            if (!body || body->Blocks.empty()) return block;
            block = body->Blocks.front().get();
        }
        return block;
    }

    // The block a `break;` exits to for `loop`: the block the loop falls to
    // after completion -- the block after the loop container's holder, with a
    // descent into a construct-leading next block (a `goto X; <next>{ try { X: }
    // }` exits to X). Returns null if the loop has no clean post-loop block.
    static const Block* LoopExitBlock(const BlockContainer* loop) {
        const Block* holder = nullptr;
        for (const ILInstruction* p = loop; p; p = p->Parent) {
            holder = dynamic_cast<const Block*>(p);
            if (holder) break;
        }
        if (!holder) return nullptr;
        const Block* next = TextuallyNextEmittedBlock(holder);
        if (!next) return nullptr;
        return FirstEmittedBlockOf(next);
    }

    // Whether this block-final branch is the implicit pre-header entry into a
    // loop (while/do-while/loop) that immediately follows. A loop condition
    // block becomes the loop head (e.g. `while (cond)`) and carries no IL label,
    // so a `goto` to it would be a dangling reference. When the loop container
    // is the leading instruction of the very next block in the enclosing
    // container, the branch falls through into the loop and must be dropped.
    static bool IsLoopEntryFallThrough(const Branch* br) {
        if (!br || !br->TargetBlock) return false;
        const Block* target = br->TargetBlock;
        // The branch must be a block's FinalInstruction (a plain block final).
        const Block* encBlock = nullptr;
        for (const ILInstruction* p = br; p; p = p->Parent) {
            encBlock = dynamic_cast<const Block*>(p);
            if (encBlock) break;
        }
        if (!encBlock || encBlock->FinalInstruction.get() != br) return false;
        if (encBlock == target) return false;
        // The target must be the FIRST block of a Loop/While/DoWhile container.
        const auto* loopC = dynamic_cast<const BlockContainer*>(target->Parent);
        if (!loopC || loopC->Blocks.empty() || loopC->Blocks.front().get() != target) return false;
        if (loopC->Kind != ContainerKind::Loop && loopC->Kind != ContainerKind::While &&
            loopC->Kind != ContainerKind::DoWhile && loopC->Kind != ContainerKind::For)
            return false;
        // A branch from INSIDE the loop is a back-edge (continue), not an entry.
        for (const ILInstruction* p = encBlock; p; p = p->Parent)
            if (p == loopC) return false;
        // The loop container must be the leading instruction of the next block in
        // the enclosing container, so the loop is emitted immediately after.
        const auto* encContainer = dynamic_cast<const BlockContainer*>(encBlock->Parent);
        if (!encContainer) return false;
        for (std::size_t i = 0; i + 1 < encContainer->Blocks.size(); ++i) {
            if (encContainer->Blocks[i].get() == encBlock) {
                const Block* next = encContainer->Blocks[i + 1].get();
                if (!next || next->Instructions.empty()) return false;
                const auto* lc = dynamic_cast<const BlockContainer*>(next->Instructions[0].get());
                return lc == loopC;
            }
        }
        return false;
    }

    // Whether `br` is a `break;` -- a branch from inside a loop to that loop's
    // exit (the post-loop block). Walks up the branch's ancestors to the
    // INNERMOST loop whose recorded exit == the target; a plain C# `break`
    // exits the innermost loop, so a target that is an OUTER loop's exit (not
    // the innermost) is NOT a break (stays a goto -- would need a labeled break).
    bool IsLoopBreak(const Branch& br) const {
        if (!br.TargetBlock) return false;
        for (const ILInstruction* p = &br; p; p = p->Parent) {
            auto* lc = dynamic_cast<const BlockContainer*>(p);
            if (!lc) continue;
            auto it = loopExits_.find(lc);
            if (it != loopExits_.end() && it->second == br.TargetBlock) return true;
        }
        return false;
    }

    std::string GotoText(const Branch& br) const {
        // A branch marked as a switch-case `break` (the body's br-exit, found
        // by the switch-inline analysis, possibly nested under compound
        // conditions) renders as `break;` wherever it appears in the inlined
        // case body -- consistent with CollectLabels, which skips label
        // creation for these. Put this before the loop-header/continue check:
        // `break;` exits the switch, not the loop, even if the exit block is a
        // loop header.
        if (breakBranches_.count(&br)) return "break;";
        // The propagated return: the goto's target block is only the shared
        // exit leave, so the return renders here and the block is
        // suppressed (the returnPropagation_ note).
        {
            auto prop = returnPropagation_.find(&br);
            if (prop != returnPropagation_.end() &&
                prop->second != nullptr &&
                prop->second->Instructions.empty() &&
                prop->second->FinalInstruction->Op == OpCode::Leave) {
                auto* lv = static_cast<const Leave*>(
                    prop->second->FinalInstruction.get());
                if (lv->Value == nullptr)
                    return "return;";
                return "return " +
                       const_cast<CEmitter*>(this)->Expr(
                           *lv->Value) + ";";
            }
        }
        if (br.TargetBlock) {
            // A branch to a loop header is a `continue` (the back-edge) only
            // when the branch is INSIDE the loop (the header's container is an
            // ancestor of the branch). A branch from outside the loop (the
            // pre-header's entry branch) is the implicit loop start, not a
            // continue -- render it as a goto (or drop it if the loop follows).
            if (loopHeaders_.find(br.TargetBlock) != loopHeaders_.end()) {
                auto* loopContainer = dynamic_cast<BlockContainer*>(br.TargetBlock->Parent);
                bool insideLoop = false;
                for (const ILInstruction* p = &br; p; p = p->Parent)
                    if (p == loopContainer) { insideLoop = true; break; }
                if (insideLoop) return "continue;";
            }
            // A branch from INSIDE a loop to that loop's exit (the post-loop
            // block) is a `break;`.
            if (IsLoopBreak(br)) return "break;";
            // Drop a redundant `goto nextBlock` when the branch's enclosing
            // block's container's next block IS the target -- the enclosing
            // block falls through to it. (A rendering-only no-op; the ILAst
            // goto survives but is not emitted.) Likewise drop the pre-header
            // entry branch to a following loop's condition block: the loop head
            // carries no label, so the goto would be a dangling reference.
            if (IsFallThroughGoto(&br) || IsLoopEntryFallThrough(&br)) return "";
            auto it = labels_.find(br.TargetBlock);
            if (it != labels_.end()) return "goto " + it->second + ";";
        }
        return "goto " + LabelFor(br.TargetOffset) + ";";
    }

    const SwitchInlinePlan& AnalyzeSwitchInline(const SwitchInstruction& sw) {
        auto it = switchInlinePlans_.find(&sw);
        if (it != switchInlinePlans_.end()) return it->second;
        SwitchInlinePlan& plan = switchInlinePlans_[&sw];  // starts ineligible
        auto bail = [&](const char* /*why*/) -> SwitchInlinePlan& { return plan; };
        // Whether an if-arm (the true arm of a no-else trailing if) "falls
        // through" -- it does work but never transfers control (no Branch,
        // Leave, Throw, or Switch terminator), so both the arm's path and the
        // if's false path fall positionally to the same place. Such a body
        // contributes no exit branch; the positional integrity gate checks
        // the fall goes to the next section's body or the exit.
        std::function<bool(const ILInstruction*)> armFallsThrough =
            [&](const ILInstruction* inst) -> bool {
            if (!inst) return true;
            if (auto* b = dynamic_cast<const Block*>(inst)) {
                if (!b->FinalInstruction) return true;
                return armFallsThrough(b->FinalInstruction.get());
            }
            if (auto* iif = dynamic_cast<const IfInstruction*>(inst)) {
                if (iif->FalseInst)
                    return armFallsThrough(iif->TrueInst.get()) &&
                           armFallsThrough(iif->FalseInst.get());
                return armFallsThrough(iif->TrueInst.get());
            }
            switch (inst->Op) {
            case OpCode::Branch:
            case OpCode::Leave:
            case OpCode::Throw:
            case OpCode::SwitchInstruction:
                return false;
            default:
                return true;
            }
        };
        // Collect every Branch terminator reachable in an if-arm's structure
        // (recursing through Blocks and nested no-else/if-else ifs). Used to
        // find a body's nested exit branch -- a `br exit` under compound
        // conditions (`if (c) { if (c2) br exit }` or `if (c) { work; br exit }`),
        // which the single-level check misses. Leave/Throw self-terminators and
        // bare statements (fall-through) contribute no branch.
        std::function<void(const ILInstruction*, std::vector<const Branch*>&)> collectExitBranches =
            [&](const ILInstruction* inst, std::vector<const Branch*>& out) {
            if (!inst) return;
            if (auto* b = dynamic_cast<const Branch*>(inst)) { out.push_back(b); return; }
            if (auto* blk = dynamic_cast<const Block*>(inst)) {
                for (const auto& i : blk->Instructions) collectExitBranches(i.get(), out);
                collectExitBranches(blk->FinalInstruction.get(), out);
                return;
            }
            if (auto* iif = dynamic_cast<const IfInstruction*>(inst)) {
                collectExitBranches(iif->TrueInst.get(), out);
                if (iif->FalseInst && iif->FalseInst->Op != OpCode::Nop)
                    collectExitBranches(iif->FalseInst.get(), out);
                return;
            }
        };
        // Locate the host block and its outer container. The host block is
        // the block directly carrying the switch (sw.Parent). Usually it is
        // directly in a container, so `outer` is its parent. But the switch
        // can sit in an if-arm (the host block's parent is the IfInstruction,
        // not a container) -- `if (cond) { switch ... }` -- in which case the
        // case bodies live in the ANCESTOR container (the one holding the if).
        // Walk up to the nearest ancestor block-in-a-container: that block is
        // the host position (the if's block) and its parent is `outer`, so the
        // `no-outer` shape still inlines and the after-host target/exit checks
        // use the if's block position.
        const Block* hostBlock = dynamic_cast<const Block*>(sw.Parent);
        const BlockContainer* outer = nullptr;
        const Block* hostPosBlock = hostBlock;
        if (hostBlock) {
            if (auto* c = dynamic_cast<const BlockContainer*>(hostBlock->Parent)) {
                outer = c;
            } else {
                for (const ILInstruction* p = hostBlock->Parent; p; p = p->Parent) {
                    if (auto* b = dynamic_cast<const Block*>(p)) {
                        if (auto* c = dynamic_cast<const BlockContainer*>(b->Parent)) {
                            hostPosBlock = b;
                            outer = c;
                            break;
                        }
                    }
                }
            }
        }
        if (!outer) return bail("no-outer");
        std::size_t hostIdx = outer->Blocks.size();
        for (std::size_t i = 0; i < outer->Blocks.size(); ++i)
            if (outer->Blocks[i].get() == hostPosBlock) { hostIdx = i; break; }
        if (hostIdx >= outer->Blocks.size()) return bail("no-host");
        // Every section body must be a single Branch thunk to a body block in
        // the same outer container, laid out AFTER the switch host, in the
        // section order, with no duplicate targets.
        std::vector<const Block*> targets;
        std::vector<std::size_t> targetIdx;
        std::size_t secIdx = 0;
        for (const auto& section : sw.Sections) {
            if (!section || !section->Body) return bail("null-section");
            // A section whose body is a direct Leave (a `return value;` or
            // `throw` -- common in switch-on-enum getters) inlines its Leave
            // directly: no body block, no thunk goto, no exit contribution.
            if (auto* lv = dynamic_cast<const Leave*>(section->Body.get())) {
                plan.directLeaveSections[secIdx] = lv;
                ++secIdx;
                continue;
            }
            // A section whose body is a direct Throw (`case N: throw new ...;`)
            // inlines its throw directly: no body block, no thunk goto, no exit.
            if (section->Body->Op == OpCode::Throw) {
                plan.directThrowSections[secIdx] = section->Body.get();
                ++secIdx;
                continue;
            }
            auto* br = dynamic_cast<const Branch*>(section->Body.get());
            if (!br || !br->TargetBlock) return bail("not-thunk");
            std::size_t j = outer->Blocks.size();
            for (j = 0; j < outer->Blocks.size(); ++j)
                if (outer->Blocks[j].get() == br->TargetBlock) break;
            if (j >= outer->Blocks.size() || j <= hostIdx) return bail("target-outside");
            if (std::find(targets.begin(), targets.end(), br->TargetBlock) != targets.end())
                return bail("dup");
            targets.push_back(br->TargetBlock);
            targetIdx.push_back(j);
            ++secIdx;
        }
        if (targets.empty() && plan.directLeaveSections.empty() &&
            plan.directThrowSections.empty()) return bail("empty");
        std::set<const Block*> tgtSet(targets.begin(), targets.end());
        std::vector<const Branch*> bodyExit;
        // The exit is the unique shared convergence of the body-final Branches:
        // every target's trailing Branch (or no-else trailing `if (cond) br T`)
        // must target the SAME outer block after the host (a body that
        // returns/throws or only falls positionally contributes nothing).
        const Block* exit = nullptr;
        std::size_t kConv = 0;
        for (const Block* t : targets) {
            ++kConv;
            const ILInstruction* fin = t->FinalInstruction.get();
            if (!fin) continue;  // fall-through-only body: contributes nothing
            if (fin->Op == OpCode::Leave) continue;
            const Branch* fbr = dynamic_cast<const Branch*>(fin);
            bool conditionalExit = false;
            if (!fbr) {
                // A terminating body final (a case body that always throws)
                // contributes no exit; the emission keeps the throw in place.
                if (t->FinalInstruction->Op == OpCode::Throw) continue;
                // A conditional-exit body final: `if (cond) br T` with no else
                // -- the cond-false path falls positionally (checked below).
                // Also accepts `if (cond) { return; }` / `if (cond) throw ...`
                // (a Block true arm whose final is Leave/Throw, or a bare
                // Leave/Throw): the true arm exits, contributing no exit
                // branch; the false path falls positionally (integrity-gated).
                auto* iif = dynamic_cast<const IfInstruction*>(fin);
                if (iif && (!iif->FalseInst || iif->FalseInst->Op == OpCode::Nop) && iif->TrueInst) {
                    fbr = dynamic_cast<const Branch*>(iif->TrueInst.get());
                    if (fbr) {
                        conditionalExit = true;  // D182: `if (cond) br exit`
                    } else {
                        // True arm exits (returns/throws)? A nested no-else
                        // if whose true arm exits (a `if (cond2) return;`) also
                        // exits -- the only non-fall path exits, every other
                        // path falls positionally (recurses for `if (c1) if (c2) return;`).
                        std::function<bool(const ILInstruction*)> trueArmExits =
                            [&](const ILInstruction* arm) -> bool {
                            if (!arm) return false;
                            if (arm->Op == OpCode::Leave || arm->Op == OpCode::Throw)
                                return true;
                            if (auto* b = dynamic_cast<const Block*>(arm))
                                if (b->FinalInstruction)
                                    return b->FinalInstruction->Op == OpCode::Leave ||
                                           b->FinalInstruction->Op == OpCode::Throw;
                            if (auto* iif = dynamic_cast<const IfInstruction*>(arm))
                                if (!iif->FalseInst && iif->TrueInst)
                                    return trueArmExits(iif->TrueInst.get());
                            return false;
                        };
                        if (trueArmExits(iif->TrueInst.get()))
                            continue;  // contributes no exit; false falls positionally
                        if (armFallsThrough(iif->TrueInst.get()))
                            continue;  // true arm does work then falls through; no exit
                        // The true arm carries a nested exit branch (under
                        // compound conditions, or work-then-br): collect every
                        // Branch; they must all target the SAME block (the body's
                        // exit contribution) or the body is multi-exit.
                        std::vector<const Branch*> nested;
                        collectExitBranches(iif->TrueInst.get(), nested);
                        if (nested.empty()) return bail("final-kind");
                        const Block* nt = nested[0]->TargetBlock;
                        for (const Branch* nb : nested)
                            if (nb->TargetBlock != nt) return bail("final-kind");
                        fbr = nested[0];
                        conditionalExit = true;
                        for (std::size_t i = 1; i < nested.size(); ++i)
                            bodyExit.push_back(nested[i]);
                    }
                } else {
                    return bail("final-kind");
                }
            }
            if (!exit) {
                exit = fbr->TargetBlock;
                bool inOuter = false;
                for (const auto& b : outer->Blocks)
                    if (b.get() == exit) { inOuter = true; break; }
                if (!inOuter) return bail("exit-outside");
            } else if (fbr->TargetBlock != exit) {
                return bail("brk-not-exit");
            }
            bodyExit.push_back(fbr);
        }
        // exit is null when every body self-terminates (throw/return) or
        // falls positionally -- the switch has no shared `break` exit. Use
        // the block after the last target as the implicit exit (the post-switch
        // continuation); no body branches to it, so no `break;` lines render.
        if (!exit) {
            if (targets.empty()) {
                // All sections are direct Leave/Throw bodies (every case
                // returns/throws); no body branches to a shared exit, so no
                // `break;` lines render. Leave exit null.
            } else if (targetIdx.back() + 1 >= outer->Blocks.size()) {
                // No block after the last target (the switch is at the
                // container's end): no shared exit. Leave exit null -- the
                // positional-integrity gate below rejects any body that would
                // need to fall to a next section/exit it cannot reach, so only
                // self-terminating bodies (return/throw) or bodies whose fall
                // is intra-body qualify. No `break;` lines render.
            } else {
                exit = outer->Blocks[targetIdx.back() + 1].get();
            }
        }
        // The exit itself must not be a body target -- EXCEPT the DEFAULT
        // section whose thunk goes to the exit: the after-switch code is the
        // default path, the section renders as the fall-through, and it
        // carries no body block.
        if (exit && tgtSet.count(exit)) {
            bool defaultToExit = false;
            for (std::size_t di = 0; di < sw.Sections.size(); ++di) {
                const auto& section = sw.Sections[di];
                if (!section || !section->Body) continue;
                if (!section->Labels.IsEmpty() || section->HasNullLabel)
                    continue;
                auto* dbr = dynamic_cast<const Branch*>(section->Body.get());
                if (dbr && dbr->TargetBlock == exit) {
                    defaultToExit = true;
                    plan.defaultSectionIdx = di;
                    break;
                }
            }
            if (!defaultToExit) return bail("exit-is-target");
            plan.defaultFallsToExit = true;
            auto tpos = std::find(targets.begin(), targets.end(), exit);
            std::size_t tindex = static_cast<std::size_t>(tpos - targets.begin());
            targets.erase(tpos);
            if (tindex < targetIdx.size()) targetIdx.erase(targetIdx.begin() + tindex);
            tgtSet.erase(exit);
        }
        // Positional integrity: a body whose trailing lets control continue
        // positionally (null final, or a no-else `if (cond) br exit`) must
        // flow into the next outer block equal to the NEXT section's body
        // (for mid sections) or the exit (for the last section) -- otherwise
        // the section-order inlining would mis-route the fall-through.
        for (std::size_t k = 0; k < targets.size(); ++k) {
            const ILInstruction* fin = targets[k]->FinalInstruction.get();
            bool fallsThrough = !fin;
            if (auto* iif = fin ? dynamic_cast<const IfInstruction*>(fin) : nullptr)
                fallsThrough = !iif->FalseInst || iif->FalseInst->Op == OpCode::Nop;
            if (!fallsThrough) continue;
            const Block* want = (k + 1 < targets.size()) ? targets[k + 1] : exit;
            if (!want) return bail("fall-no-target");
            if (targetIdx[k] + 1 >= outer->Blocks.size() ||
                outer->Blocks[targetIdx[k] + 1].get() != want)
                return bail("fall-order");
        }
        // Foreign-reference check: nothing outside the switch's own section
        // thunk Branches and the body's trailing `br exit` may target a
        // body block.
        std::unordered_map<const Block*, const Branch*> okBranchFor;
        for (const auto& section : sw.Sections)
            okBranchFor[static_cast<const Branch*>(section->Body.get())->TargetBlock] =
                static_cast<const Branch*>(section->Body.get());
        bool foreign = false;
        std::function<void(const ILInstruction*)> check = [&](const ILInstruction* inst) {
            if (!inst || foreign) return;
            if (auto* b = dynamic_cast<const Branch*>(inst)) {
                if (b->TargetBlock && tgtSet.count(b->TargetBlock) &&
                    b != okBranchFor[b->TargetBlock]) { foreign = true; return; }
            }
            for (int i = 0; i < inst->ChildCount(); ++i) check(inst->GetChild(i));
        };
        check(fn_->Body.get());
        if (foreign) return bail("foreign-branch");
        // Succeed: inline the bodies.
        plan.eligible = true;
        plan.targets = targets;
        plan.exit = exit;
        inlinedBodyBlocks_.insert(tgtSet.begin(), tgtSet.end());
        for (const auto& section : sw.Sections)
            breakBranches_.insert(static_cast<const Branch*>(section->Body.get()));
        for (const Branch* fb : bodyExit) breakBranches_.insert(fb);
        // Any `br exit` inside a body block's instruction list becomes
        // `break` too.
        for (const Block* t : targets)
            for (const auto& inst : t->Instructions)
                if (auto* b = dynamic_cast<const Branch*>(inst.get()))
                    if (b->TargetBlock == exit) breakBranches_.insert(b);
        return plan;
    }

    // Whether any Branch targeting `header` renders a real `goto` AND is from
    // INSIDE `bodyContainer` (the construct body whose entry is `header`). A
    // goto from inside the construct to its own entry re-enters the construct;
    // moving the entry's label before the construct keyword would make such an
    // inside-branch jump OUT of the construct (running a finally/leave) and back
    // in -- wrong. So the label moves before the keyword only when every
    // goto-rendering branch to `header` is from OUTSIDE the construct. Branches
    // that render as `continue`/`break`/a dropped fall-through do not reference
    // the label (CollectLabels skipped them), so they do not block the move.
    bool EntryTargetedFromInside(const Block* header, const BlockContainer* body) const {
        if (!header || !body) return false;
        std::function<bool(const ILInstruction*)> walk = [&](const ILInstruction* inst) -> bool {
            if (!inst) return false;
            if (auto* br = dynamic_cast<const Branch*>(inst))
                if (br->TargetBlock == header &&
                    !IsContinueBranch(br) && !IsLoopBreak(*br) &&
                    !IsFallThroughGoto(br) && !IsLoopEntryFallThrough(br)) {
                    for (const ILInstruction* p = br; p; p = p->Parent)
                        if (p == body) return true;
                }
            for (int i = 0; i < inst->ChildCount(); ++i)
                if (walk(inst->GetChild(i))) return true;
            return false;
        };
        return walk(fn_->Body.get());
    }

    // Emit the construct-entry block's `IL_xxxx:` label BEFORE the construct
    // keyword (a loop/try/catch/finally/lock/using) when the entry is a `goto`
    // target from OUTSIDE the construct. C# forbids `goto` into a `try`/
    // `catch`/`finally`/`lock`/`using` body (and into a loop body), so the entry
    // block's label must sit before the keyword (outside the construct), making
    // the goto target the construct statement (valid) rather than a point
    // inside it. Records the block so EmitBlock suppresses the inside emission
    // (no double label). C# labels start in column 0. `bodyContainer` is the
    // construct body whose first block is `header` (the loop/try/catch/finally/
    // lock/using body); the inside-branch check uses it.
    void EmitHeaderLabel(const Block* header, const BlockContainer* bodyContainer) {
        if (!header) return;
        auto label = labels_.find(header);
        if (label == labels_.end()) return;
        if (EntryTargetedFromInside(header, bodyContainer)) return;  // keep inside
        out_ += label->second;
        out_ += ":\n";
        emittedHeaderLabels_.insert(header);
    }
    // Convenience: the body is an ILInstruction (a BlockContainer in practice);
    // cast and emit the entry label. No-op if the body is empty or not a
    // BlockContainer.
    void EmitBodyHeaderLabel(const ILInstruction* bodyInst) {
        auto* body = dynamic_cast<const BlockContainer*>(bodyInst);
        if (body && !body->Blocks.empty())
            EmitHeaderLabel(body->Blocks.front().get(), body);
    }

    void EmitContainer(const BlockContainer& container, int indent) {
        if (DepthAtLimit()) {
            Line(indent, "/* max rendering depth: possible ILAst cycle */");
            return;
        }
        DepthGuard g{depth_};
        if (container.Kind == ContainerKind::While && !container.Blocks.empty()) {
            // A while container: the entry point's FinalInstruction is the
            // while condition `if (cond) br body else leave(loop)`. Render as
            // `while (cond) { body }` -- the condition from the if, the body
            // from the blocks after the entry.
            const Block* header = container.Blocks.front().get();
            EmitHeaderLabel(header, &container);
            std::string cond = "(default)";
            if (header && header->FinalInstruction &&
                header->FinalInstruction->Op == OpCode::IfInstruction) {
                const auto& iff = static_cast<const IfInstruction&>(*header->FinalInstruction);
                if (iff.Condition) cond = CondExpr(*iff.Condition);
            }
            Line(indent, "while (" + cond + ")");
            Line(indent, "{");
            // The entry block's non-final Instructions (its preamble between
            // the guard and the condition: a csc while with body-in-entry or a
            // do-while's refreshed local) execute at the top of every
            // iteration -- render them at the body's open, not silently.
            if (header && !header->Instructions.empty()) {
                for (const auto& inst : header->Instructions)
                    if (inst) EmitStatement(*inst, indent + 1);
            }
            // The body is every block after the entry point.
            for (std::size_t i = 1; i < container.Blocks.size(); ++i) {
                const auto& block = container.Blocks[i];
                if (!block) continue;
                // Drop a trailing back-edge branch to the entry (implicit iter).
                bool dropFinal = false;
                if (block->FinalInstruction &&
                    block->FinalInstruction->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                    if (br->TargetBlock == header) dropFinal = true;
                }
                EmitBlock(*block, indent + 1, dropFinal);
            }
            Line(indent, "}");
            return;
        }
        if (container.Kind == ContainerKind::For && container.Blocks.size() >= 2) {
            // A for container (HighLevelLoopTransform's MatchForLoop): the entry
            // block's FinalInstruction is the condition if `if (cond) br body
            // else leave loop`; the increment block (simple statements + an
            // implicit back-edge to the header) was moved to the container's end
            // by the transform, but later transforms may leave a trailing empty
            // block behind it -- so locate it by shape (final Branch to the
            // header, all-simple statements) instead of assuming position.
            const Block* header = container.Blocks.front().get();
            EmitHeaderLabel(header, &container);
            const Block* increment = nullptr;
            std::size_t incIdx = 0;
            for (std::size_t i = container.Blocks.size(); i-- > 1;) {
                const Block* b = container.Blocks[i].get();
                if (!b || b->Instructions.empty() || !b->FinalInstruction ||
                    b->FinalInstruction->Op != OpCode::Branch)
                    continue;
                auto* br = static_cast<const Branch*>(b->FinalInstruction.get());
                if (br->TargetBlock != header) continue;
                bool simple = true;
                for (const auto& inst : b->Instructions) {
                    if (!inst) continue;
                    if (inst->Op != OpCode::StLoc && inst->Op != OpCode::StObj &&
                        inst->Op != OpCode::Call) { simple = false; break; }
                }
                if (simple) { increment = b; incIdx = i; break; }
            }
            std::string cond = "(default)";
            if (header && header->FinalInstruction &&
                header->FinalInstruction->Op == OpCode::IfInstruction) {
                const auto& iff = static_cast<const IfInstruction&>(*header->FinalInstruction);
                if (iff.Condition) cond = CondExpr(*iff.Condition);
            }
            // The increment clause renders the increment block's statements
            // comma-joined (`V = V + 1` -> `V++`; `V = V op x` -> `V op= x`).
            std::string incrText;
            if (increment) {
                for (const auto& inst : increment->Instructions) {
                    if (!inst) continue;
                    std::string part;
                    if (inst->Op == OpCode::StLoc) {
                        const auto& st = static_cast<const StLoc&>(*inst);
                        std::string name = st.Variable ? st.Variable->Name : "?";
                        part = name + AssignmentText(st, name);
                    } else if (inst->Op == OpCode::StObj) {
                        const auto& st = static_cast<const StObj&>(*inst);
                        part = StoreTargetText(*st.Target) + " = " +
                               (st.Value ? Expr(*st.Value) : std::string("(default)"));
                    } else if (inst->Op == OpCode::Call) {
                        part = CallText(static_cast<const Call&>(*inst));
                    } else {
                        // Not one of the simple statement kinds MatchIncrementBlock
                        // admits; render it as an expression statement.
                        part = Expr(*inst);
                    }
                    if (!incrText.empty()) incrText += ", ";
                    incrText += part;
                }
            }
            // The C# PatternStatementTransform's TransformFor hoist: the
            // statement immediately before the for (`v = init;`) moves into
            // the for's initializer when the loop's condition or increment
            // uses v. The declaration rides the hoist (the variable's
            // first store): `for (T v = init; ...)`.
            std::string initText;
            {
                auto it = hoistedForInits_.find(&container);
                if (it != hoistedForInits_.end()) {
                    initText = it->second.Text;
                    // The hoisted initializer declares the variable: the
                    // later stores render without the type.
                    declared_.insert(it->second.VariableName);
                }
            }
            auto emitForBody = [&]() {
                // The for-loop header's preamble (a while-initialized value
                // the csc lowers to an entry statement, or a refreshed
                // do-while local) executes at the top of every iteration
                // -- render it at the body open.
                if (header && !header->Instructions.empty()) {
                    for (const auto& inst : header->Instructions)
                        if (inst) EmitStatement(*inst, indent + 1);
                }
                // Index of the last emitted (non-empty) body block: only
                // that block's trailing jump to the increment block is the
                // iteration itself (a dangling `continue` before `}`), so
                // it drops silently; a jump to the increment from any other
                // position is a real `continue` (the for-update still runs
                // -- the C# `continue`).
                std::size_t lastBodyIdx = container.Blocks.size() - 1;
                if (lastBodyIdx == incIdx) --lastBodyIdx;
                for (std::size_t i = 1; i < container.Blocks.size(); ++i) {
                    if (i == incIdx) continue;  // the increment block rendered in the header
                    const auto& block = container.Blocks[i];
                    if (!block) continue;
                    // Drop an empty trailing block whose only edge is back
                    // to the increment block (a dead segment of the
                    // pre-for layout).
                    if (block->Instructions.empty() && block->FinalInstruction &&
                        block->FinalInstruction->Op == OpCode::Branch &&
                        static_cast<const Branch*>(block->FinalInstruction.get())->TargetBlock == increment)
                        continue;
                    // Drop a trailing back-edge branch to the entry
                    // (implicit iter), and (only on the last body block) a
                    // trailing branch to the increment block (the
                    // iteration -- dropping it anywhere else would erase a
                    // real `continue`).
                    bool dropFinal = false;
                    if (block->FinalInstruction &&
                        block->FinalInstruction->Op == OpCode::Branch) {
                        auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                        if (br->TargetBlock == header) dropFinal = true;
                        if (i == lastBodyIdx && br->TargetBlock == increment && increment)
                            dropFinal = true;
                    }
                    EmitBlock(*block, indent + 1, dropFinal);
                }
            };
            // The C# PatternStatementTransform's TransformForeachOnArray:
            // a for over `i < arr.Length` with `i++` whose body's only
            // uses of i are `arr[i]` element accesses renders as
            // `foreach (T e in arr)`. The condition: comp(lt, ldloc i,
            // ldlen(ldloc arr)) (the ldlen possibly conv-widened); the
            // increment: the single `i = i + 1` store; every body use of
            // i sits in an ldelema(arr, ldloc i).
            {
                const Comp* condComp =
                    header && header->FinalInstruction &&
                            header->FinalInstruction->Op ==
                                OpCode::IfInstruction
                        ? dynamic_cast<const Comp*>(static_cast<
                              const IfInstruction&>(
                              *header->FinalInstruction)
                                  .Condition.get())
                        : nullptr;
                const LdLoc* counter = nullptr;
                const LdLoc* arrLoc = nullptr;
                if (condComp != nullptr &&
                    condComp->Kind == ComparisonKind::LessThan &&
                    condComp->Left &&
                    condComp->Left->Op == OpCode::LdLoc) {
                    counter = static_cast<const LdLoc*>(
                        condComp->Left.get());
                    const ILInstruction* len = condComp->Right.get();
                    if (len != nullptr && len->Op == OpCode::Conv) {
                        auto* cv = static_cast<const Conv*>(len);
                        if (cv->Argument)
                            len = cv->Argument.get();
                    }
                    if (len != nullptr && len->Op == OpCode::LdLen) {
                        const ILInstruction* arr =
                            len->GetChild(0);
                        if (arr != nullptr && arr->Op == OpCode::LdLoc)
                            arrLoc = static_cast<const LdLoc*>(arr);
                    }
                }
                bool singleIncrement =
                    increment != nullptr &&
                    increment->Instructions.size() == 1 &&
                    increment->Instructions[0] &&
                    increment->Instructions[0]->Op == OpCode::StLoc;
                if (counter != nullptr && arrLoc != nullptr &&
                    singleIncrement && counter->Variable &&
                    arrLoc->Variable) {
                    ILVariable* iv = counter->Variable.get();
                    ILVariable* av = arrLoc->Variable.get();
                    // Every body use of iv is an ldelema(av, ldloc iv).
                    std::vector<const LdElema*> accesses;
                    bool allElement = true;
                    std::function<void(const ILInstruction*)> walk =
                        [&](const ILInstruction* node) {
                            if (node == nullptr || !allElement) return;
                            if (node->Op == OpCode::LdLoc &&
                                static_cast<const LdLoc*>(node)
                                        ->Variable.get() == iv) {
                                // A load of the counter outside an
                                // ldelema index disqualifies the shape.
                                const ILInstruction* p = node->Parent;
                                bool inIndex = false;
                                while (p != nullptr &&
                                       p->Op == OpCode::LdElema) {
                                    auto* le = static_cast<const LdElema*>(p);
                                    for (const auto& idx : le->Indices)
                                        if (idx.get() == node) inIndex = true;
                                    if (le->Array &&
                                        le->Array->Op == OpCode::LdLoc &&
                                        static_cast<const LdLoc*>(
                                            le->Array.get())
                                                ->Variable.get() == av &&
                                        le->Indices.size() == 1 &&
                                        le->Indices[0].get() == node) {
                                        if (inIndex) accesses.push_back(le);
                                        else allElement = false;
                                        return;
                                    }
                                    p = p->Parent;
                                }
                                allElement = false;
                                return;
                            }
                            for (int c = 0; c < node->ChildCount(); ++c)
                                walk(node->GetChild(c));
                        };
                    for (std::size_t i = 1;
                         i < container.Blocks.size(); ++i) {
                        const auto& b = container.Blocks[i];
                        if (!b || b.get() == header || b.get() == increment)
                            continue;
                        for (const auto& stmt : b->Instructions)
                            walk(stmt.get());
                        if (b->FinalInstruction)
                            walk(b->FinalInstruction.get());
                    }
                    if (allElement && !accesses.empty()) {
                        // The element type from the ARRAY's element type
                        // (the C# TransformForeachOnArray reads the array
                        // type; the ldelema may carry the stelem.ref
                        // object form).
                        std::string elemType = "var";
                        const TypeSystem::IType* elementType = nullptr;
                        if (av->Type) {
                            if (auto* at = dynamic_cast<
                                    const TypeSystem::ArrayType*>(
                                    av->Type.get())) {
                                elemType = CSharpTypeName(at->Element());
                                elementType = at->Element().get();
                            } else {
                                elemType = CSharpTypeName(av->Type);
                                elementType = av->Type.get();
                            }
                        }
                        // The element name is the type-based variable-name
                        // suggestion (the C# foreach-on-array keeps the
                        // loop variable's AssignName'd name), never the
                        // rendered type text: a keyword (`byte`) is not a
                        // legal identifier, and a class type renders its
                        // lowercased form (ImageDebugDirectory ->
                        // imageDebugDirectory), not the type name verbatim.
                        std::string base =
                            IL::AssignVariableNames::SuggestNameForType(
                                elementType);
                        if (base.empty())
                            base = "val";
                        std::string elemName = base;
                        for (int n = 2; declared_.count(elemName); ++n)
                            elemName = base + std::to_string(n);
                        declared_.insert(elemName);
                        foreachSubst_.insert(accesses.begin(),
                                             accesses.end());
                        foreachElementName_ = elemName;
                        Line(indent, "foreach (" + elemType + " " +
                                        elemName + " in " +
                                        av->Name + ")");
                        Line(indent, "{");
                        emitForBody();
                        Line(indent, "}");
                        return;
                    }
                }
            }
            Line(indent, "for (" + initText + "; " + cond + "; " + incrText + ")");
            Line(indent, "{");
            emitForBody();
            Line(indent, "}");
            return;
        }
        if (container.Kind == ContainerKind::DoWhile && !container.Blocks.empty()) {
            // A do-while loop: render as `do { ... } while (cond);` where the
            // condition is the last block's if (negated, since the true-arm is
            // the continue / the fall-through is the break).
            const Block* header = container.Blocks.front().get();
            const Block* last = container.Blocks.back().get();
            std::string cond = "true";
            if (last && last->FinalInstruction &&
                last->FinalInstruction->Op == OpCode::IfInstruction) {
                const auto& iff = static_cast<const IfInstruction&>(*last->FinalInstruction);
                if (iff.Condition) cond = NegateCondText(*iff.Condition);
            }
            Line(indent, "do");
            Line(indent, "{");
            // A do-while header block (the first block of the body after the
            // guard) may also carry statements between the fall into the body
            // and the first body block -- they execute at the top of every
            // iteration.
            if (header && !header->Instructions.empty()) {
                for (const auto& inst : header->Instructions)
                    if (inst) EmitStatement(*inst, indent + 1);
            }
            for (std::size_t i = 0; i < container.Blocks.size(); ++i) {
                const auto& block = container.Blocks[i];
                if (!block) continue;
                bool dropFinal = (block.get() == last);  // drop the do-while condition if
                // Also drop a trailing back-edge to the header (implicit iter).
                if (!dropFinal && block->FinalInstruction &&
                    block->FinalInstruction->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                    if (br->TargetBlock == header) dropFinal = true;
                }
                EmitBlock(*block, indent + 1, dropFinal);
            }
            Line(indent, "} while (" + cond + ");");
            return;
        }
        if (container.Kind == ContainerKind::Loop && !container.Blocks.empty()) {
            // A loop container renders as `while (true) { ... }`; the back-edge
            // branch to the header (the first block) is implicit -- the loop
            // iterates. Only the LAST block's trailing back-edge is dropped (a
            // mid-loop back-edge is a `continue`, rendered as a goto for now).
            const Block* header = container.Blocks.front().get();
            EmitHeaderLabel(header, &container);
            Line(indent, "while (true)");
            Line(indent, "{");
            for (std::size_t i = 0; i < container.Blocks.size(); ++i) {
                const auto& block = container.Blocks[i];
                if (!block) continue;
                bool isLast = (i + 1 == container.Blocks.size());
                bool dropFinal = false;
                if (isLast && block->FinalInstruction &&
                    block->FinalInstruction->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                    if (br->TargetBlock == header) dropFinal = true;
                }
                EmitBlock(*block, indent + 1, dropFinal);
            }
            Line(indent, "}");
            return;
        }
        for (std::size_t i = 0; i < container.Blocks.size(); ++i) {
            const auto& block = container.Blocks[i];
            if (!block) continue;
            // Drop a trailing `goto nextBlock` when nextBlock is the next EMITTED
            // block -- the block falls through, so the goto is redundant. The
            // scan is target-aware: it stops AT the target (a `br` to a relay
            // that is the immediate next block still drops -- fall-through
            // reaches the relay), skips transparent relay blocks that are NOT
            // the target (they emit nothing), and stops at the first block with
            // content. Switch-body blocks inlined into case sections are also
            // skipped. (CFS can't merge a multi-pred nextBlock, but the goto is
            // still redundant for rendering.)
            bool dropFinal = false;
            if (block->FinalInstruction && block->FinalInstruction->Op == OpCode::Branch) {
                auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                for (std::size_t j = i + 1; j < container.Blocks.size(); ++j) {
                    if (br->TargetBlock == container.Blocks[j].get()) { dropFinal = true; break; }
                    if (inlinedBodyBlocks_.count(container.Blocks[j].get())) continue;
                    if (IsTransparentRelay(&container, j)) continue;
                    break;
                }
            }
            // The reader's fall-through duplicate: after an always-exiting
            // construct (a using/try whose body leaves the function), a
            // following block that only carries a Leave to the function body
            // is unreachable -- the construct's own leave already exited.
            // Skip it (the C#'s dead-code handling drops the same shape;
            // the oracle renders one return).
            bool unreachableExit = false;
            if (block->FinalInstruction &&
                block->FinalInstruction->Op == OpCode::Leave) {
                auto* lv = static_cast<const Leave*>(
                    block->FinalInstruction.get());
                if (fn_ && lv->TargetContainer == fn_->Body.get()) {
                    for (std::size_t j = 0; j < i; ++j) {
                        const auto& prev = container.Blocks[j];
                        if (!prev || prev->Instructions.empty()) continue;
                        bool anyExit = false;
                        for (const auto& stmt : prev->Instructions) {
                            if (stmt && ConstructAlwaysExits(stmt.get())) {
                                anyExit = true;
                                break;
                            }
                        }
                        if (anyExit) { unreachableExit = true; break; }
                    }
                }
            }
            if (unreachableExit) continue;
            EmitBlock(*block, indent, dropFinal);
        }
    }

    // Whether the variable has any StLoc store in the tree (the dangling-
    // alias check for the initializer fold).
    static bool VariableHasStoreIn(const ILInstruction* inst,
                                   const ILVariable* v) {
        if (inst == nullptr) return false;
        if (inst->Op == OpCode::StLoc &&
            static_cast<const StLoc*>(inst)->Variable.get() == v)
            return true;
        for (int c = 0; c < inst->ChildCount(); ++c)
            if (VariableHasStoreIn(inst->GetChild(c), v)) return true;
        return false;
    }

    // Whether a construct statement (using/try/lock) always exits the
    // function: its body contains a Leave to the function body. The C#
    // ILAst's dead-code handling drops the fall-through leave the reader
    // leaves after such a construct (the try's leave already exited), so
    // the port must not render the unreachable duplicate.
    bool ConstructAlwaysExits(const ILInstruction* inst) {
        // The construct bodies are Blocks or BlockContainers depending
        // on the reader's modeling (the using body is a Block, the try
        // bodies are containers), so the walk starts from whichever body
        // the construct carries.
        const ILInstruction* body = nullptr;
        if (auto* u = dynamic_cast<const UsingInstruction*>(inst))
            body = u->Body.get();
        else if (auto* tf = dynamic_cast<const TryFinally*>(inst))
            body = tf->TryBlock.get();
        else if (auto* tc = dynamic_cast<const TryCatch*>(inst))
            body = tc->TryBlock.get();
        else if (auto* l = dynamic_cast<const LockInstruction*>(inst))
            body = l->Body.get();
        if (body == nullptr || fn_ == nullptr)
            return false;
        std::function<bool(const ILInstruction*)> exits =
            [&](const ILInstruction* i) -> bool {
            if (i == nullptr) return false;
            if (auto* lv = dynamic_cast<const Leave*>(i)) {
                if (lv->TargetContainer == fn_->Body.get())
                    return true;
            }
            for (int c = 0; c < i->ChildCount(); ++c)
                if (exits(i->GetChild(c))) return true;
            return false;
        };
        return exits(body);
    }

    void EmitBlock(const Block& block, int indent, bool dropFinal = false) {
        if (DepthAtLimit()) {
            Line(indent, "/* max rendering depth: possible ILAst cycle */");
            return;
        }
        DepthGuard g{depth_};
        // A switch body block inlined into its case is not emitted again as a
        // standalone labeled block (see AnalyzeSwitchInline).
        if (inlinedBodyBlocks_.count(&block)) return;
        // The propagated-return target block: its leave rendered at the
        // goto site (the returnPropagation_ note).
        if (suppressedReturnBlocks_.count(&block)) return;
        // The coalesce fold's alternative block: its store folded into the
        // declaration (the AnalyzeNullCoalescingChains note).
        if (coalesceSuppressed_.count(&block)) return;
        // The label-region fold's merged blocks (the fall-through and
        // the branch target -- their statements render inside the
        // folded if/else).
        if (labelRegionSuppressed_.count(&block)) return;
        auto label = labels_.find(&block);
        if (label != labels_.end() && !emittedHeaderLabels_.count(&block)) {
            // C# labels start in column 0 by convention. A block whose label
            // was already emitted before a construct keyword (EmitHeaderLabel)
            // is not re-labeled here -- the goto targets the construct statement.
            out_ += label->second;
            out_ += ":\n";
        }
        bool exited = false;
        for (const auto& inst : block.Instructions) {
            if (inst.get() == skippedAlias_) {
                // Folded into the preceding ArrayInitializer render.
                skippedAlias_ = nullptr;
                continue;
            }
            // The coalesce fold's skipped statements (the temporary's
            // store and the guard if).
            if (inst && coalesceSkipped_.count(inst.get()) != 0)
                continue;
            // The general single-use elision's dropped declaration.
            if (inst && singleUseSkipped_.count(inst.get()) != 0)
                continue;
            // The coalesce fold's declaration: `b = a;` renders as
            // `b = (expr ?? alt);` through the composed
            // NullCoalescingInstruction.
            if (inst && inst->Op == OpCode::StLoc) {
                auto fold = coalesceFolds_.find(
                    static_cast<StLoc*>(inst.get()));
                if (fold != coalesceFolds_.end()) {
                    Line(indent,
                         "var " + fold->first->Variable->Name + " = " +
                             Expr(*fold->second) + ";");
                    continue;
                }
            }
            if (inst && inst->Op != OpCode::Nop) {
                // An always-exiting construct makes the rest of the block
                // unreachable; a following Leave to the function body is
                // the reader's fall-through duplicate of the construct's
                // own exit (the double-return shape) -- drop it.
                if (exited && inst->Op == OpCode::Leave)
                    continue;
                EmitStatement(*inst, indent);
                if (ConstructAlwaysExits(inst.get()))
                    exited = true;
            }
        }
        // The same unreachable-exit rule for the block's final: a leave
        // final after an always-exiting construct statement in this block
        // is the reader's fall-through duplicate. The coalesce fold's
        // guard-if is also a skipped final (its whole chain folded into the
        // declaration).
        if (block.FinalInstruction && !dropFinal &&
            !(exited && block.FinalInstruction->Op == OpCode::Leave) &&
            coalesceSkipped_.find(block.FinalInstruction.get()) ==
                coalesceSkipped_.end()) {
            // The label-region fold: the if-final renders the merged
            // regions (the branch target's block as the true arm, the
            // fall-through block as the else arm).
            auto fold = labelRegionFolds_.find(
                static_cast<const IfInstruction*>(
                    block.FinalInstruction.get()));
            if (fold != labelRegionFolds_.end() &&
                fold->first != nullptr) {
                Block* r1 = fold->second.first;
                Block* r2 = fold->second.second;
                EmitFoldedIf(*fold->first, r1, r2, indent);
            } else {
                EmitStatement(*block.FinalInstruction, indent);
            }
        }
    }

    // Render a Block(InterpolatedString) as a C# `$"..."` interpolation.
    // Walks Instructions[1..] (skipping Instructions[0], the
    // stloc v(newobj DefaultInterpolatedStringHandler(..)) handler init):
    // AppendLiteral renders its LdStr arg as literal text (`{`/`}` escaped),
    // AppendFormatted renders its value as `{expr}` (with optional
    // `,alignment` and/or `:format`). Faithful to the real back end's
    // TranslateInterpolatedString. The ToStringAndClear final is the implicit
    // conversion to string and is not emitted.
    // The C# TranslateArrayInitializer (ExpressionBuilder.cs lines
    // 3685-3760): an ArrayInitializer block evaluates to
    // `new T[dims] { elements }`. The first instruction is
    // stloc v(newarr T(dims)); the remaining instructions are the element
    // stores stobj(ldelema(T, ldloc v, indices), value); the final is
    // ldloc v (the implicit conversion, not rendered). The element values
    // render in index order; a multi-dimensional initializer nests the
    // initializer braces per dimension (the C# container stack).
    std::string ArrayInitializerText(const Block& block) {
        const StLoc* stloc = block.Instructions.empty()
            ? nullptr
            : dynamic_cast<const StLoc*>(block.Instructions[0].get());
        const NewArr* newArr =
            stloc != nullptr && stloc->Value != nullptr
                ? dynamic_cast<const NewArr*>(stloc->Value.get())
                : nullptr;
        if (newArr == nullptr || newArr->Type == nullptr)
            return "(default)";
        std::string text = "new " + CSharpTypeName(newArr->Type) + "[";
        for (std::size_t i = 0; i < newArr->Indices.size(); ++i) {
            if (i) text += ", ";
            text += newArr->Indices[i] ? Expr(*newArr->Indices[i]) : "(default)";
        }
        text += "]";
        // The (index tuple -> rendered value) pairs in order.
        std::vector<std::pair<std::vector<int>, std::string>> entries;
        for (std::size_t i = 1; i < block.Instructions.size(); ++i) {
            const StObj* stObj =
                dynamic_cast<const StObj*>(block.Instructions[i].get());
            if (stObj == nullptr || stObj->Target == nullptr) continue;
            const LdElema* ldElema =
                dynamic_cast<const LdElema*>(stObj->Target.get());
            if (ldElema == nullptr) continue;
            std::vector<int> indices;
            bool constant = true;
            for (const auto& idx : ldElema->Indices) {
                if (!idx || idx->Op != OpCode::LdcI4) { constant = false; break; }
                indices.push_back(static_cast<const LdcI4*>(idx.get())->Value);
            }
            if (!constant) continue;
            entries.emplace_back(std::move(indices),
                                 stObj->Value ? Expr(*stObj->Value)
                                              : std::string("(default)"));
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        // The nested braces per dimension: single-dim renders the flat
        // `{ v0, v1 }`; a deeper tuple opens a brace per new row prefix.
        text += " {";
        std::vector<std::vector<int>> openRows;
        bool first = true;
        for (const auto& entry : entries) {
            std::size_t depth = 0;
            while (depth < openRows.size() &&
                   depth + 1 < entry.first.size() &&
                   openRows[depth] ==
                       std::vector<int>(entry.first.begin(),
                                        entry.first.begin() + depth + 1)) {
                ++depth;
            }
            while (openRows.size() > depth) {
                openRows.pop_back();
                text += " }";
                first = true;
            }
            while (openRows.size() + 1 < entry.first.size()) {
                std::vector<int> prefix(entry.first.begin(),
                                        entry.first.begin() +
                                            static_cast<std::ptrdiff_t>(
                                                openRows.size() + 1));
                if (openRows.size() == 0 ||
                    openRows.back() != prefix) {
                    if (!first) text += ",";
                    text += " {";
                    openRows.push_back(prefix);
                    first = true;
                }
            }
            if (!first) text += ",";
            text += " " + entry.second;
            first = false;
        }
        while (!openRows.empty()) {
            openRows.pop_back();
            text += " }";
        }
        text += " }";
        return text;
    }

    std::string InterpolatedStringText(const Block& block) {
        std::string out = "$\"";
        for (std::size_t i = 1; i < block.Instructions.size(); ++i) {
            auto* inst = block.Instructions[i].get();
            if (!inst || inst->Op != OpCode::Call) continue;
            const auto& call = static_cast<const Call&>(*inst);
            auto name = ShortMethodName(call.MethodName);
            if (name == "AppendLiteral" && call.Arguments.size() == 2 &&
                call.Arguments[1]->Op == OpCode::LdStr) {
                const auto& lit = static_cast<const LdStr&>(*call.Arguments[1]).Value;
                for (char c : lit) {
                    if (c == '{') out += "{{";
                    else if (c == '}') out += "}}";
                    else out += c;
                }
            } else if (name == "AppendFormatted" && call.Arguments.size() >= 2 &&
                       call.Arguments.size() <= 4) {
                std::string value = Expr(*call.Arguments[1]);
                std::string alignment;
                std::string format;
                if (call.Arguments.size() >= 3 && call.Arguments[2]->Op == OpCode::LdcI4)
                    alignment = std::to_string(static_cast<const LdcI4&>(*call.Arguments[2]).Value);
                else if (call.Arguments.size() >= 3 && call.Arguments[2]->Op == OpCode::LdStr)
                    format = static_cast<const LdStr&>(*call.Arguments[2]).Value;
                if (call.Arguments.size() == 4 && call.Arguments[3]->Op == OpCode::LdStr)
                    format = static_cast<const LdStr&>(*call.Arguments[3]).Value;
                out += '{';
                out += value;
                if (!alignment.empty()) { out += ','; out += alignment; }
                if (!format.empty()) { out += ':'; out += format; }
                out += '}';
            }
        }
        out += '"';
        return out;
    }

    // A non-Block arm of if/try: brace it at this indent.
    void EmitBraced(const ILInstruction& inst, int indent) {
        if (DepthAtLimit()) {
            Line(indent, "/* max rendering depth: possible ILAst cycle */");
            Line(indent, "{}");
            return;
        }
        DepthGuard g{depth_};
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

    // The ExpressionBuilder's ILOffsetHex shape (the " near IL_xxxx"
    // suffix text): the zero-padded hex offset. File-local mirror (the
    // seed does not include the ExpressionBuilder's internals).
    static std::string ILOffsetText(std::int32_t offset) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%x",
            static_cast<std::uint32_t>(offset));
        std::string result(buffer);
        while (result.size() < 4) result.insert(result.begin(), '0');
        return result;
    }

    void EmitStatement(const ILInstruction& inst, int indent) {
        if (DepthAtLimit()) {
            Line(indent, "/* max rendering depth: possible ILAst cycle */");
            return;
        }
        DepthGuard g{depth_};
        switch (inst.Op) {
            case OpCode::StLoc: {
                const auto& st = static_cast<const StLoc&>(inst);
                std::string name = st.Variable ? st.Variable->Name : "?";
                bool declare = st.Variable && st.Variable->Kind != VariableKind::Parameter &&
                               declared_.insert(name).second;
                // `V = V op expr` -> `V op= expr` (or `V++`/`V--` for +/- 1) when
                // the binary's left is a load of the same variable. A declaration
                // (`var V = ...`) is never a compound assignment.
                std::string valueText = BoolLiteralText(st.Variable.get(), st.Value.get());
                if (valueText.empty()) {
                    valueText = Expr(*st.Value);
                    // Strip the redundant outer parens the seed wraps around `as`
                    // and `isinst` expressions in an assignment.
                    if (st.Value && st.Value->Op == OpCode::IsInst)
                        valueText = StripOuterParens(std::move(valueText));
                }
                std::string assign = declare ? " = " + valueText : AssignmentText(st, name);
                std::string decl = declare ? CSharpTypeName(st.Variable->Type) + " " + name : name;
                Line(indent, decl + assign + ";");
                return;
            }
            case OpCode::Call: {
                std::string callText =
                    CtorCallStatementText(static_cast<const Call&>(inst));
                if (!callText.empty()) {
                    Line(indent, callText + ";");
                }
                return;
            }
            case OpCode::StObj: {
                const auto& st = static_cast<const StObj&>(inst);
                std::string valueText = Expr(*st.Value);
                // The C# stores a boolean through the boolean literal
                // (the CSharpPrimitiveCast over the target type): an int
                // constant stored to a bool-typed field renders
                // true/false, not 1/0.
                if (st.Type != nullptr &&
                    st.Type->ReflectionName() == "System.Boolean") {
                    if (auto* constant =
                            dynamic_cast<const LdcI4*>(st.Value.get())) {
                        if (constant->Value == 0)
                            valueText = "false";
                        else if (constant->Value == 1)
                            valueText = "true";
                    }
                }
                Line(indent, StoreTargetText(*st.Target) + " = " + valueText + ";");
                return;
            }
            case OpCode::NumericCompoundAssign: {
                // A numeric compound assignment as a statement: `target op= value;`
                // (or `target++;`/`target--;` for the post-increment/decrement).
                // The Expr case renders the compound-assign expression form.
                Line(indent, Expr(inst) + ";");
                return;
            }
            case OpCode::UserDefinedCompoundAssign: {
                // A user-defined compound assignment as a statement: `target op= value;`
                // (or `target++;`/`++target;` for op_Increment/op_Decrement). The Expr
                // case renders the compound-assign / increment expression form.
                Line(indent, Expr(inst) + ";");
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
                // An `endfinally` is a Leave whose TargetContainer is the
                // instruction's immediately-enclosing container (the finally
                // body), not the function body and not a loop -- the finally
                // ends and control returns to the try's continuation. It
                // renders as nothing (a bare `break;` would be invalid C# with
                // no enclosing loop/switch).
                if (leave.TargetContainer) {
                    // An `endfinally`/end-of-try-body leave: the TargetContainer
                    // is the finally/filter/try body (a construct body, not
                    // the function body and not a loop) and the leave carries no
                    // value -- the construct ends and control continues. It
                    // renders as nothing (a bare `break;` would be invalid C#
                    // with no enclosing loop/switch).
                    auto* owner = leave.TargetContainer->Parent;
                    bool isConstructBody = owner &&
                        (owner->Op == OpCode::TryFinally ||
                         owner->Op == OpCode::TryCatch ||
                         owner->Op == OpCode::TryFault ||
                         owner->Op == OpCode::UsingInstruction ||
                         owner->Op == OpCode::LockInstruction ||
                         owner->Op == OpCode::PinnedRegion);
                    bool isLoop = leave.TargetContainer->Kind == ContainerKind::Loop ||
                        leave.TargetContainer->Kind == ContainerKind::While ||
                        leave.TargetContainer->Kind == ContainerKind::For ||
                        leave.TargetContainer->Kind == ContainerKind::DoWhile;
                    if (isConstructBody && !isLoop && !leave.Value &&
                        (!fn_ || leave.TargetContainer != fn_->Body.get()))
                        return;
                }
                if (!leave.TargetContainer) return;
                // A leave of the function body is `return`; a leave of a loop
                // container is `break`; other leaves (switch/try) are `break`
                // for now (the real back end disambiguates).
                if (fn_ && leave.TargetContainer == fn_->Body.get()) {
                    if (leave.Value) {
                        // `return ldc.i4 0/1` in a Boolean-returning function is
                        // `return false;`/`return true;` (the IL idiom for bool
                        // constants in the return position).
                        std::string val = BoolLiteralFromReturn(leave.Value.get());
                        if (val.empty()) val = Expr(*leave.Value);
                        Line(indent, "return " + val + ";");
                    }
                    else Line(indent, "return;");
                } else {
                    Line(indent, "break;");
                }
                return;
            }
            case OpCode::Branch: {
                // The propagated shared exit: the goto's target block (the
                // assign-then-return stores plus the leave) renders here
                // and the target block is suppressed. The leave-only case
                // goes through GotoText (a single return line); the stores
                // need the statement renderer.
                {
                    const auto& br = static_cast<const Branch&>(inst);
                    auto prop = returnPropagation_.find(&br);
                    if (prop != returnPropagation_.end() &&
                        prop->second != nullptr) {
                        for (const auto& si : prop->second->Instructions)
                            if (si) EmitStatement(*si, indent);
                        EmitStatement(*prop->second->FinalInstruction,
                                      indent);
                        return;
                    }
                }
                // A dropped branch (redundant fall-through / loop-entry) yields an
                // empty GotoText -- emit nothing, not a blank line.
                std::string gt = GotoText(static_cast<const Branch&>(inst));
                if (!gt.empty()) Line(indent, gt);
                return;
            }
            case OpCode::IfInstruction: {
                const auto& iff = static_cast<const IfInstruction&>(inst);
                std::string cond = iff.Condition ? CondExpr(*iff.Condition) : "(default)";
                if (!iff.FalseInst && iff.TrueInst && iff.TrueInst->Op == OpCode::Branch) {
                    std::string gotoText = GotoText(*static_cast<const Branch*>(iff.TrueInst.get()));
                    if (!gotoText.empty())
                        Line(indent, "if (" + cond + ") " + gotoText);
                    // An empty goto (redundant goto-to-next): drop the whole if
                    // -- the block falls through, the if is a no-op. (The
                    // condition may have side effects but the IL executed it
                    // unconditionally before the branch; our model has it as
                    // the if's condition, so dropping is sound only when the
                    // condition is pure. We approximate by dropping -- the
                    // common case is a pure comparison.)
                    return;
                }
                // An empty true arm (a Block with no instructions/final, or null)
                // with a non-empty else: swap to `if (!cond) { else }` so the
                // output has no empty `{ }`.
                auto isEmptyArm = [](const std::unique_ptr<ILInstruction>& arm) {
                    if (!arm) return true;
                    // A Nop arm carries no content -- treat as empty (an
                    // `if (c) { } else nop`-shape must not print `else { }`).
                    if (arm->Op == OpCode::Nop) return true;
                    // A fall-through Branch arm (a redundant goto dropped per
                    // D188/D189) renders nothing -- treat as empty so
                    // `if (cond) { } else { work }` swaps to `if (!cond) { work }`.
                    if (arm->Op == OpCode::Branch)
                        return IsFallThroughGoto(static_cast<const Branch*>(arm.get()));
                    if (auto* b = dynamic_cast<const Block*>(arm.get())) {
                        if (!b->Instructions.empty()) return false;  // has work
                        if (!b->FinalInstruction) return true;  // null final
                        // A Block whose only content is a fall-through Branch
                        // final (the goto dropped) renders empty.
                        return b->FinalInstruction->Op == OpCode::Branch &&
                               IsFallThroughGoto(static_cast<const Branch*>(
                                   b->FinalInstruction.get()));
                    }
                    return false;
                };
                std::string trueArm = cond;
                std::unique_ptr<ILInstruction>* elseArm = nullptr;
                bool negated = false;
                if (isEmptyArm(iff.TrueInst) && !isEmptyArm(iff.FalseInst)) {
                    // swap: render if (!cond) { false } -- no else.
                    negated = true;
                    cond = StripOuterParens(NegateCondText(*iff.Condition));
                    elseArm = const_cast<std::unique_ptr<ILInstruction>*>(&iff.TrueInst);
                    // Render the false arm as the (only) body.
                    Line(indent, "if (" + cond + ")");
                    EmitBraced(*iff.FalseInst, indent);
                    return;
                }
                Line(indent, "if (" + cond + ")");
                if (iff.TrueInst) EmitBraced(*iff.TrueInst, indent);
                else Line(indent, "{ }");
                if (iff.FalseInst) {
                    if (isEmptyArm(iff.FalseInst)) return;  // skip empty else
                    Line(indent, "else");
                    EmitBraced(*iff.FalseInst, indent);
                }
                return;
            }
            case OpCode::SwitchInstruction: {
                const auto& sw = static_cast<const SwitchInstruction&>(inst);
                const SwitchInlinePlan* plan = nullptr;
                {
                    auto pit = switchInlinePlans_.find(&sw);
                    if (pit != switchInlinePlans_.end() && pit->second.eligible)
                        plan = &pit->second;
                }
                Line(indent, "switch (" + (sw.Value ? Expr(*sw.Value) : std::string("(default)")) + ")");
                Line(indent, "{");
                for (std::size_t k = 0; k < sw.Sections.size(); ++k) {
                    const auto& section = sw.Sections[k];
                    if (!section) continue;
                    // The default whose thunk targets the exit renders as the
                    // fall-through: no label, no body (the after-switch code
                    // is the default path).
                    if (plan && plan->defaultFallsToExit &&
                        k == plan->defaultSectionIdx)
                        continue;
                    if (section->HasNullLabel) {
                        Line(indent + 1, "case null:");
                    }
                    if (section->Labels.IsEmpty() && !section->HasNullLabel) {
                        Line(indent + 1, "default:");
                    } else {
                        for (const auto& iv : section->Labels.Intervals()) {
                            if (iv.Start == iv.InclusiveEnd())
                                Line(indent + 1, "case " + std::to_string(iv.Start) + ":");
                            else
                                Line(indent + 1, "case " + std::to_string(iv.Start) +
                                      ".." + std::to_string(iv.InclusiveEnd()) + ":");
                        }
                    }
                    if (!plan) {
                        if (section->Body) EmitStatement(*section->Body, indent + 1);
                        continue;
                    }
                    // A direct-Leave section (a `return value;` body, no
                    // thunk): emit the Leave directly under the label.
                    auto dit = plan->directLeaveSections.find(k);
                    if (dit != plan->directLeaveSections.end()) {
                        EmitStatement(*dit->second, indent + 1);
                        continue;
                    }
                    // A direct-Throw section (a `throw new ...;` body, no
                    // thunk): emit the throw directly under the label.
                    auto tit = plan->directThrowSections.find(k);
                    if (tit != plan->directThrowSections.end()) {
                        EmitStatement(*tit->second, indent + 1);
                        continue;
                    }
                    // Emit the inlined case body. Its trailing `br exit` (and
                    // any mid-body `br exit`, both gated at analysis) renders
                    // as `break`; a Leave stays as `return`/`throw`.
                    // The thunk index into plan->targets: section k minus the
                    // direct-Leave/Throw sections before it.
                    std::size_t thunkIdx = k;
                    for (const auto& dp : plan->directLeaveSections)
                        if (dp.first < k) --thunkIdx;
                    for (const auto& dp : plan->directThrowSections)
                        if (dp.first < k) --thunkIdx;
                    if (plan->defaultFallsToExit &&
                        k > plan->defaultSectionIdx)
                        --thunkIdx;
                    const Block* body = plan->targets[thunkIdx];
                    for (const auto& inst : body->Instructions) {
                        if (!inst || inst->Op == OpCode::Nop) continue;
                        if (auto* b = dynamic_cast<const Branch*>(inst.get())) {
                            if (b->TargetBlock == plan->exit) {
                                Line(indent + 1, "break;");
                                continue;
                            }
                        }
                        EmitStatement(*inst, indent + 1);
                    }
                    if (const ILInstruction* fin = body->FinalInstruction.get()) {
                        if (fin->Op == OpCode::Leave || fin->Op == OpCode::Throw) {
                            EmitStatement(*fin, indent + 1);
                        } else if (dynamic_cast<const Branch*>(fin) &&
                                 static_cast<const Branch*>(fin)->TargetBlock == plan->exit) {
                            Line(indent + 1, "break;");
                        } else if (auto* iif = dynamic_cast<const IfInstruction*>(fin);
                                   iif && (!iif->FalseInst || iif->FalseInst->Op == OpCode::Nop) && iif->TrueInst &&
                                   dynamic_cast<const Branch*>(iif->TrueInst.get()) &&
                                   static_cast<const Branch*>(iif->TrueInst.get())->TargetBlock == plan->exit) {
                            // Trailing conditional exit `if (cond) br exit`
                            // (analysis-gated): renders as `if (cond) break;`
                            // -- the false path falls positionally to the next
                            // section's body or the exit (fall-order-gated).
                            std::string c = iif->Condition ? CondExpr(*iif->Condition)
                                                           : std::string("(default)");
                            Line(indent + 1, "if (" + c + ") break;");
                        } else {
                            EmitStatement(*fin, indent + 1);
                        }
                    }
                }
                Line(indent, "}");
                return;
            }
            case OpCode::TryCatch: {
                const auto& tc = static_cast<const TryCatch&>(inst);
                EmitBodyHeaderLabel(tc.TryBlock.get());
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
                EmitBodyHeaderLabel(tf.TryBlock.get());
                Line(indent, "try");
                if (tf.TryBlock) EmitBraced(*tf.TryBlock, indent); else Line(indent, "{ }");
                Line(indent, "finally");
                if (tf.FinallyBlock) EmitBraced(*tf.FinallyBlock, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::LockInstruction: {
                const auto& lk = static_cast<const LockInstruction&>(inst);
                EmitBodyHeaderLabel(lk.Body.get());
                Line(indent, "lock (" + (lk.OnExpression ? Expr(*lk.OnExpression) : std::string("?")) + ")");
                if (lk.Body) EmitBraced(*lk.Body, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::UsingInstruction: {
                const auto& us = static_cast<const UsingInstruction&>(inst);
                // The enumerator-foreach shape: a using whose resource is a
                // GetEnumerator call collapses to the foreach (the
                // TryCollapseEnumeratorForeach note).
                if (us.ResourceExpression &&
                    (us.ResourceExpression->Op == OpCode::Call ||
                     us.ResourceExpression->Op == OpCode::CallVirt)) {
                    auto* call = static_cast<const Call*>(
                        us.ResourceExpression.get());
                    if (call->MethodName.size() >= 15 &&
                        call->MethodName.rfind("::GetEnumerator") ==
                            call->MethodName.size() - 15) {
                        EmitBodyHeaderLabel(us.Body.get());
                        std::string collapsed;
                        if (TryCollapseEnumeratorForeach(
                                us, *call, indent, collapsed)) {
                            out_ += collapsed;
                            return;
                        }
                    }
                }
                // The C# `using` statement: `using (resource) { body }` (the
                // expression form). The UsingInstruction also carries the local
                // the resource is stored into, but the seed elides it (the real
                // back end declares the using-local via the using, not DeclareVariables).
                EmitBodyHeaderLabel(us.Body.get());
                Line(indent, "using (" +
                     (us.ResourceExpression ? Expr(*us.ResourceExpression) : std::string("null")) + ")");
                if (us.Body) EmitBraced(*us.Body, indent); else Line(indent, "{ }");
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
            case OpCode::PinnedRegion: {
                const auto& pr = static_cast<const PinnedRegion&>(inst);
                std::string varType = pr.Variable && pr.Variable->Type
                    ? CSharpTypeName(pr.Variable->Type) : std::string("var");
                std::string varName = pr.Variable ? pr.Variable->Name : std::string("pinned");
                Line(indent, "fixed (" + varType + " " + varName + " = " +
                             (pr.Init ? Expr(*pr.Init) : std::string("null")) + ")");
                if (pr.Body) EmitBraced(*pr.Body, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::Block: {
                const auto& blk = static_cast<const Block&>(inst);
                if (blk.Kind == BlockKind::InterpolatedString) {
                    Line(indent, InterpolatedStringText(blk) + ";");
                    return;
                }
                if (blk.Kind == BlockKind::ArrayInitializer) {
                    // An ArrayInitializer block in the statement position:
                    // the store's inline did not fold it into the user's
                    // local (the dup-slot aliasing), so render the explicit
                    // assignment through the initializer's final variable --
                    // `v = new T[n] { ... };` -- instead of the braced
                    // statement block (the element stores and the final
                    // load are the initializer's own shape). When the NEXT
                    // statement is the alias store `T u = v;` over the
                    // initializer's variable (the dup-slot chain the C#
                    // inlines away), fold it: `T u = new T[n] { ... };` and
                    // skip the alias.
                    const LdLoc* final =
                        dynamic_cast<const LdLoc*>(blk.FinalInstruction.get());
                    std::string var = final != nullptr && final->Variable
                        ? final->Variable->Name
                        : std::string();
                    if (!var.empty() && !blk.Instructions.empty()) {
                        // The outer store's variable (the block's parent
                        // StLoc): the alias chain's link.
                        ILVariable* target = final->Variable.get();
                        std::string decl;
                        auto* parentBlock = dynamic_cast<Block*>(inst.Parent);
                        int nextIdx = inst.ChildIndex + 1;
                        if (parentBlock != nullptr &&
                            static_cast<std::size_t>(nextIdx) <
                                parentBlock->Instructions.size()) {
                            auto* nextSt = dynamic_cast<StLoc*>(
                                parentBlock->Instructions[nextIdx].get());
                            // The alias fold: the next statement
                            // `T u = v;` where v's store was replaced by
                            // this initializer block (the dead-store
                            // keep-expression arm consumed the store, so v
                            // has no store left in the tree -- a dangling
                            // load). Fold the alias into the initializer:
                            // `T u = new ... { ... };`.
                            if (nextSt != nullptr && nextSt->Variable &&
                                nextSt->Value &&
                                nextSt->Value->Op == OpCode::LdLoc) {
                                ILVariable* loaded =
                                    static_cast<LdLoc*>(
                                        nextSt->Value.get())
                                        ->Variable.get();
                                if (loaded != nullptr &&
                                    !VariableHasStoreIn(
                                        fn_->Body.get(), loaded)) {
                                    target = nextSt->Variable.get();
                                    skippedAlias_ = nextSt;
                                }
                            }
                        }
                        if (target != final->Variable.get() &&
                            declared_.insert(target->Name).second)
                            decl = CSharpTypeName(target->Type) + " ";
                        Line(indent, decl + target->Name + " = " +
                                        ArrayInitializerText(blk) + ";");
                        return;
                    }
                }
                EmitBraced(inst, indent);
                return;
            }
            case OpCode::BlockContainer:
                EmitContainer(static_cast<const BlockContainer&>(inst), indent);
                return;
            case OpCode::Nop:
                return;
            case OpCode::YieldReturn: {
                // `yield return <expr>;` (the real back end's
                // VisitYieldReturn).
                const auto& yr = static_cast<const YieldReturn&>(inst);
                Line(indent, "yield return " +
                                (yr.Value ? Expr(*yr.Value)
                                          : std::string("(default)")) +
                                ";");
                return;
            }
            case OpCode::Await: {
                // An await as a statement: `await <expr>;` (the real back
                // end's VisitAwait inside an ExpressionStatement).
                const auto& aw = static_cast<const Await&>(inst);
                Line(indent, "await " +
                                (aw.Value ? Expr(*aw.Value)
                                          : std::string("(default)")) +
                                ";");
                return;
            }
            case OpCode::DynamicGetMemberInstruction:
            case OpCode::DynamicInvokeMemberInstruction:
            case OpCode::DynamicInvokeInstruction:
            case OpCode::DynamicGetIndexInstruction:
            case OpCode::DynamicInvokeConstructorInstruction:
            case OpCode::DynamicBinaryOperatorInstruction:
            case OpCode::DynamicUnaryOperatorInstruction:
            case OpCode::DynamicConvertInstruction:
            case OpCode::DynamicIsEventInstruction: {
                // A value-form dynamic node as a statement renders as the
                // expression statement (the C# ExpressionStatement form).
                Line(indent, Expr(inst) + ";");
                return;
            }
            case OpCode::DynamicSetMemberInstruction: {
                // `target.Name = value;` (the real back end's
                // VisitDynamicSetMember inside an AssignmentExpression).
                const auto& sm =
                    static_cast<const DynamicSetMemberInstruction&>(inst);
                std::string target =
                    sm.Target ? Expr(*sm.Target) : std::string("(default)");
                std::string value =
                    sm.Value ? Expr(*sm.Value) : std::string("(default)");
                Line(indent, target + "." + sm.Name + " = " + value + ";");
                return;
            }
            case OpCode::DynamicSetIndexInstruction: {
                // `target[args] = value;`.
                const auto& si =
                    static_cast<const DynamicSetIndexInstruction&>(inst);
                std::string target =
                    si.Arguments.empty()
                        ? std::string("(default)")
                        : Expr(*si.Arguments[0]);
                std::string indices = DynamicArgumentList(si, 1);
                std::string value =
                    si.Arguments.size() >= 1 && si.Arguments.back()
                        ? Expr(*si.Arguments.back())
                        : std::string("(default)");
                Line(indent, target + "[" + indices + "] = " + value + ";");
                return;
            }
            case OpCode::InvalidBranch: {
                // The C# ExpressionBuilder.VisitInvalidBranch (lines
                // 5121-5133) renders the node as an ErrorExpression whose
                // comment text is "Error" plus the optional " near
                // IL_xxxx" suffix for a non-zero start offset and the
                // optional ": message" tail; the statement prints the
                // comment followed by the empty statement's semicolon.
                auto* invalidBranch = static_cast<const InvalidBranch*>(&inst);
                std::string message = "Error";
                if (invalidBranch->StartILOffset != 0)
                    message += " near IL_" + ILOffsetText(invalidBranch->StartILOffset);
                if (invalidBranch->Message && !invalidBranch->Message->empty())
                    message += ": " + *invalidBranch->Message;
                Line(indent, "/*" + message + "*/;");
                return;
            }
            default:
                Line(indent, "/* unhandled statement op " +
                             std::to_string(static_cast<int>(inst.Op)) + " */;");
                return;
        }
    }

    // Map a C# operator method name ("op_Equality") to its symbolic form.
    // Returns nullptr for non-operators.
    static const char* OperatorSymbol(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        std::string_view op = (pos != std::string_view::npos) ? methodName.substr(pos + 2) : methodName;
        if (op == "op_Equality") return "==";
        if (op == "op_Inequality") return "!=";
        if (op == "op_LessThan") return "<";
        if (op == "op_LessThanOrEqual") return "<=";
        if (op == "op_GreaterThan") return ">";
        if (op == "op_GreaterThanOrEqual") return ">=";
        if (op == "op_Addition") return "+";
        if (op == "op_Subtraction") return "-";
        if (op == "op_Multiply") return "*";
        if (op == "op_Division") return "/";
        if (op == "op_Modulus") return "%";
        if (op == "op_BitwiseAnd") return "&";
        if (op == "op_BitwiseOr") return "|";
        if (op == "op_ExclusiveOr") return "^";
        if (op == "op_LeftShift") return "<<";
        if (op == "op_RightShift") return ">>";
        return nullptr;
    }

    // The symbol for a unary operator method, or nullptr. Returns a 2-char
    // prefix (e.g. "-", "!", "~") or "++"/"--" for inc/dec.
    static const char* UnaryOperatorSymbol(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        std::string_view op = (pos != std::string_view::npos) ? methodName.substr(pos + 2) : methodName;
        if (op == "op_UnaryNegation") return "-";
        if (op == "op_UnaryPlus") return "+";
        if (op == "op_OnesComplement") return "~";
        if (op == "op_LogicalNot") return "!";
        if (op == "op_Increment") return "++";
        if (op == "op_Decrement") return "--";
        return nullptr;
    }

    // The declaring type's short name for a "Namespace.Type::member" string,
    // for op_Explicit/op_Implicit conversion rendering ((TargetType)value).
    static std::string DeclaringTypeName(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        if (pos == std::string_view::npos) return std::string{};
        std::string type(methodName.substr(0, pos));
        // A generic declaring type carries the ECMA arity marker and the
        // reflection-argument geometry (e.g.
        // `Span`1[[T]]::op_Implicit`); flatten both so the cast renders the
        // C# form (`(Span<T>)(x)`).
        FlattenReflectionArgs(type);
        StripGenericArity(type, 0);
        auto dot = type.rfind('.');
        return std::string(dot != std::string::npos ? type.substr(dot + 1) : type);
    }

    // True if methodName is op_Explicit or op_Implicit (a conversion operator).
    static bool IsConversionOperator(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        std::string_view op = (pos != std::string_view::npos) ? methodName.substr(pos + 2) : methodName;
        return op == "op_Explicit" || op == "op_Implicit";
    }

    // "Namespace.Type::.ctor" -> "new Namespace.Type(args)"; other members and
    // plain methods -> "Namespace.Type.Member(args)". A static operator call
    // (op_Equality etc.) renders as `(arg0 op arg1)`; a unary operator as
    // `op arg0`; a conversion operator (op_Explicit/op_Implicit) as
    // `(TargetType)arg0`.
    // The delegate-construction shape: a newobj whose constructor takes
    // the (target, method-group) pair -- only delegate ctors take a native
    // function pointer as their second argument, so the shape identifies
    // the construction. The C# HandleDelegateConstruction folds the pair
    // into the method group; the new-expression keeps the delegate type
    // (`new RoutedEventHandler(M)`), the target argument dropping.
    static bool IsDelegateConstruction(const Call& call) {
        return call.IsNewObj && call.Arguments.size() == 2 &&
               call.Arguments[1] != nullptr &&
               (call.Arguments[1]->Op == OpCode::LdFtn ||
                call.Arguments[1]->Op == OpCode::LdVirtFtn);
    }

    // The event add/remove accessors (the compiler-generated add_X /
    // remove_X methods -- only events produce them): the C#
    // ReplaceMethodCallsWithOperators event arm renders the compound
    // assignment (`recv.X += handler`), the handler's delegate
    // construction folding to the bare method group in the event-handler
    // position. Empty when the call is not an event accessor.
    // The C# cast expression as a member-access receiver wraps itself
    // (`((Button)target).AddHandler(...)`): the cast binds tighter than
    // the dot, so the receiver parenthesizes when it is itself a cast.
    bool IsCastInstruction(const ILInstruction* node) {
        return node != nullptr &&
               (node->Op == OpCode::CastClass || node->Op == OpCode::Unbox ||
                node->Op == OpCode::UnboxAny);
    }

    std::string EventAddRemoveText(const Call& call) {
        std::string_view name(call.MethodName);
        auto sep = name.rfind("::");
        if (sep == std::string_view::npos)
            return std::string();
        std::string_view member = name.substr(sep + 2);
        const bool isAdd = member.size() > 4 &&
                           member.compare(0, 4, "add_") == 0;
        const bool isRemove = member.size() > 7 &&
                              member.compare(0, 7, "remove_") == 0;
        if (!isAdd && !isRemove)
            return std::string();
        if (!call.IsInstanceCall || call.Arguments.size() != 2 ||
            call.Arguments[0] == nullptr || call.Arguments[1] == nullptr)
            return std::string();
        std::string eventName(member.substr(isAdd ? 4 : 7));
        std::string receiver = Expr(*call.Arguments[0]);
        // The cast receiver wraps like the call path (the cast binds
        // tighter than the dot and the +=).
        if (IsCastInstruction(call.Arguments[0].get()))
            receiver = "(" + receiver + ")";
        // The handler: a delegate construction folds to its method group
        // (the event's handler type makes the conversion implicit).
        std::string handler;
        if (auto* ctor = dynamic_cast<const Call*>(call.Arguments[1].get());
            ctor != nullptr && IsDelegateConstruction(*ctor)) {
            handler = Expr(*ctor->Arguments[1]);
        } else {
            handler = Expr(*call.Arguments[1]);
        }
        return receiver + "." + eventName + (isAdd ? " += " : " -= ") +
               handler;
    }

    // The C# CastExpression's operand parenthesization: a simple load
    // or literal (the identifier-shaped forms) renders unparenthesized --
    // `(Button)target`, not `(Button)(target)`; anything with looser-
    // binding structure keeps the parens.
    bool IsSimpleCastOperand(const ILInstruction* node) {
        if (node == nullptr)
            return true;
        switch (node->Op) {
            case OpCode::LdLoc:
            case OpCode::LdLoca:
            case OpCode::LdStr:
            case OpCode::LdStrUtf8:
            case OpCode::LdcI4:
            case OpCode::LdcI8:
            case OpCode::LdcF4:
            case OpCode::LdcF8:
            case OpCode::LdcDecimal:
            case OpCode::LdNull:
                return true;
            default:
                return false;
        }
    }

    std::string CastText(const std::string& typeName,
                         const std::unique_ptr<ILInstruction>& argument) {
        if (!argument)
            return "(" + typeName + ")(default)";
        std::string operand = Expr(*argument);
        if (!IsSimpleCastOperand(argument.get()))
            operand = "(" + operand + ")";
        return "(" + typeName + ")" + operand;
    }

    std::string CallText(const Call& call) {
        {
            std::string eventText = EventAddRemoveText(call);
            if (!eventText.empty())
                return eventText;
        }
        if (!call.IsInstanceCall) {
            // A static conversion operator: op_Explicit/op_Implicit(value) ->
            // (TargetType)value. The target type is the declaring type.
            if (call.Arguments.size() == 1 && IsConversionOperator(call.MethodName)) {
                std::string targetType = DeclaringTypeName(call.MethodName);
                return "(" + targetType + ")(" +
                       (call.Arguments[0] ? Expr(*call.Arguments[0]) : std::string("(default)")) + ")";
            }
            // A static property accessor: Type.get_X() -> Type.X ;
            // Type.set_X(value) -> not common (C# set_ is instance); skip.
            if (call.Arguments.size() <= 1) {
                std::string prop = AccessorPropertyName(call.MethodName);
                if (!prop.empty()) {
                    auto pos = call.MethodName.rfind("::");
                    std::string_view type = (pos != std::string_view::npos)
                        ? std::string_view(call.MethodName).substr(0, pos) : std::string_view{};
                    // A generic declaring type carries the ECMA arity
                    // marker and the reflection-argument geometry (e.g.
                    // `System.Collections.Generic.EqualityComparer`1[[System.
                    // Object]]::get_Default`); flatten the geometry, then take
                    // the short name before the argument list (the last '.'
                    // inside the args would leak `Object>`), so the accessor
                    // renders `EqualityComparer<System.Object>.Default`.
                    std::string fullType(type);
                    FlattenReflectionArgs(fullType);
                    ::ILSpy::Decompiler::CSharp::RequiredImports::RecordTypeName(fullType);
                    std::size_t lt = fullType.find('<');
                    std::size_t dot = fullType.rfind('.',
                        (lt == std::string::npos) ? std::string::npos : lt);
                    std::string shortType(dot == std::string::npos
                        ? fullType : fullType.substr(dot + 1));
                    StripGenericArity(shortType, 0);
                    return shortType + "." + prop;
                }
            }
            // A static unary operator: op_UnaryNegation(a) -> (-a), etc.
            if (call.Arguments.size() == 1) {
                if (const char* sym = UnaryOperatorSymbol(call.MethodName)) {
                    std::string arg = call.Arguments[0] ? Expr(*call.Arguments[0]) : std::string("(default)");
                    return std::string(sym) + arg;
                }
            }
            // A static binary operator: Namespace.Type::op_X(a, b) -> (a op b)
            if (call.Arguments.size() == 2) {
                if (const char* sym = OperatorSymbol(call.MethodName)) {
                    return "(" + (call.Arguments[0] ? Expr(*call.Arguments[0]) : std::string("(default)")) +
                           " " + sym + " " +
                           (call.Arguments[1] ? Expr(*call.Arguments[1]) : std::string("(default)")) + ")";
                }
            }
        }
        std::string name = call.MethodName;
        // The C# ReplaceMethodCallsWithOperators reductions that the flat
        // emitter carries at the call site:
        //  - `System.Type.GetTypeFromHandle(ldtoken X)` is the IL for
        //    `typeof(X)` -- the wrapper renders as the typeof expression
        //    itself.
        //  - `System.String.Concat(a, b, ...)` is the IL for the string
        //    concatenation `a + b + ...` (the StringConcat setting,
        //    default true; every argument renders into the + chain).
        {
            std::string_view mn = call.MethodName;
            if (mn == "System.Type::GetTypeFromHandle" &&
                call.Arguments.size() == 1 && call.Arguments[0] &&
                call.Arguments[0]->Op == OpCode::LdTypeToken)
                return Expr(*call.Arguments[0]);
            if (mn == "System.String::Concat" && call.Arguments.size() >= 2) {
                std::string text;
                for (std::size_t i = 0; i < call.Arguments.size(); ++i) {
                    if (i) text += " + ";
                    text += call.Arguments[i]
                        ? Expr(*call.Arguments[i])
                        : std::string("(default)");
                }
                return text;
            }
        }
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
        // A newobj on a generic type: render the resolved declaring type via
        // CSharpTypeName so it comes out as `List<string>` (the C# form), not the
        // metadata name `List`1<System.String>`. Non-generic ctors leave
        // DeclaringType unset (or resolve to a SimpleType), so those keep the
        // full illustrative name from the metadata MethodName unchanged.
        const TypeSystem::ParameterizedType* genDecl =
            (call.IsNewObj && call.DeclaringType)
                ? dynamic_cast<const TypeSystem::ParameterizedType*>(call.DeclaringType.get())
                : nullptr;
        ::ILSpy::Decompiler::CSharp::RequiredImports::RecordMethodTarget(call.MethodName);
        std::string typeName = genDecl ? CSharpTypeName(call.DeclaringType)
                                       : FlattenMetadataName(std::move(name));
        if (!genDecl) {
            if (prefix.empty()) {
                // The static call: the own-type member renders
                // unqualified.
                typeName = SimplifyQualifiedMember(typeName);
                // Otherwise the target type renders its short name --
                // the C# name lookup through the using directives (the
                // oracle's `Guid.NewGuid()` and `Contract.Requires`
                // over the full metadata names; the resolver-based
                // collision qualification rides with the
                // name-qualification family).
                auto dot = typeName.rfind('.');
                if (dot != std::string::npos && dot > 0) {
                    auto prev = typeName.rfind('.', dot - 1);
                    if (prev != std::string::npos)
                        typeName = typeName.substr(prev + 1);
                }
            } else {
                // A new-expression's type renders its short name (the C#
                // name lookup through the using directives; the oracle's
                // `new RoutedEventHandler(...)` over the full
                // `new System.Windows.RoutedEventHandler(...)`). The
                // resolver-based collision qualification rides with the
                // name-qualification family.
                auto dot = typeName.rfind('.');
                if (dot != std::string::npos)
                    typeName = typeName.substr(dot + 1);
            }
        }
        // The instantiated static call renders its generic argument list
        // (`Contract.Requires<ArgumentNullException>(...)`). A VAR/MVAR
        // argument (a `!`/`!!` reflection-name marker) suppresses the
        // list: the type-parameter naming is emitter-context work.
        if (!call.TypeArgumentNames.empty()) {
            bool allConcrete = true;
            for (const auto& tn : call.TypeArgumentNames)
                if (tn.find('!') != std::string::npos) allConcrete = false;
            if (allConcrete) {
                typeName += "<";
                for (std::size_t i = 0; i < call.TypeArgumentNames.size(); ++i) {
                    if (i > 0) typeName += ", ";
                    // The short form (the C# name lookup through the using
                    // directives; the last-segment convention).
                    const std::string& tn = call.TypeArgumentNames[i];
                    ::ILSpy::Decompiler::CSharp::RequiredImports::RecordTypeName(tn);
                    auto dot = tn.rfind('.');
                    typeName += (dot != std::string::npos)
                        ? tn.substr(dot + 1) : tn;
                }
                typeName += ">";
            }
        }
        std::string text = prefix + typeName + "(";
        // The delegate construction: the (target, method-group) pair folds
        // to the method group alone (`new RoutedEventHandler(M)`).
        std::size_t firstArgument = 0;
        if (prefix == "new " && IsDelegateConstruction(call))
            firstArgument = 1;
        for (std::size_t i = firstArgument; i < call.Arguments.size(); ++i) {
            if (i > firstArgument) text += ", ";
            text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
        }
        text += ')';
        return text;
    }

    // The C# operator precedence for the numeric binaries (the C#
    // grammar: multiplicative > additive > shift > & > ^ > |).
    static int BinaryPrecedence(BinaryNumericOperator op) {
        switch (op) {
            case BinaryNumericOperator::Mul:
            case BinaryNumericOperator::Div:
            case BinaryNumericOperator::Rem:
                return 1;
            case BinaryNumericOperator::Add:
            case BinaryNumericOperator::Sub:
                return 2;
            case BinaryNumericOperator::ShiftLeft:
            case BinaryNumericOperator::ShiftRight:
                return 3;
            case BinaryNumericOperator::BitAnd:
                return 4;
            case BinaryNumericOperator::BitXor:
                return 5;
            case BinaryNumericOperator::BitOr:
                return 6;
            default:
                return 7;
        }
    }

    // The C# FractionApprox (TypeSystemAstBuilder.cs lines 1725-1784): the
    // continued-fraction approximation of `value` with denominators bounded
    // by `maxDenominator`. Returns (0, 0) for the out-of-range / degenerate
    // shapes. Follows the C# two-candidate (first/second) delta comparison.
    static std::pair<long, long> FractionApprox(double value,
                                                int maxDenominator) {
        if (std::fabs(value) > 0x7FFFFFFF) return {0, 0};
        double startValue = value;
        if (value < 0) value = -value;
        long ai;
        long m[2][2] = {{1, 0}, {0, 1}};
        double v = value;
        while (m[1][0] * (ai = static_cast<long>(v)) + m[1][1] <=
               maxDenominator) {
            long t = m[0][0] * ai + m[0][1];
            m[0][1] = m[0][0];
            m[0][0] = t;
            t = m[1][0] * ai + m[1][1];
            m[1][1] = m[1][0];
            m[1][0] = t;
            if (v - ai == 0) break;
            v = 1 / (v - ai);
            if (std::fabs(v) >=
                static_cast<double>(std::numeric_limits<long>::max()))
                break;
        }
        if (m[1][0] == 0) return {0, 0};
        long firstN = m[0][0];
        long firstD = m[1][0];
        ai = (maxDenominator - m[1][1]) / m[1][0];
        long secondN = m[0][0] * ai + m[0][1];
        long secondD = m[1][0] * ai + m[1][1];
        double firstDelta =
            std::fabs(value - firstN / static_cast<double>(firstD));
        double secondDelta =
            std::fabs(value - secondN / static_cast<double>(secondD));
        if (firstDelta < secondDelta)
            return {startValue < 0 ? -firstN : firstN, firstD};
        return {startValue < 0 ? -secondN : secondN, secondD};
    }

    // The C# IsValidFraction (lines 1458-1466): a positive denominator, a
    // nonzero numerator, and either a trivial part (1) or |num| < den with
    // the denominator built from the 2/3/5 prime family.
    static bool IsValidFraction(long num, long den) {
        if (!(den > 0 && num != 0)) return false;
        if (den == 1 || std::labs(num) == 1) return true;
        return std::labs(num) < den && (den % 2 == 0 || den % 3 == 0 ||
                                        den % 5 == 0);
    }

    // The C# ConvertFloatingPointLiteral's special-constants arm: the PI/E
    // forms (TypeSystemAstBuilder.cs lines 1553-1688). A double literal
    // whose value / Math.PI (or E) approximates a valid fraction renders
    // as `Math.PI` [` * n`] [` / d`] when `field * n / d == value`
    // exactly, or `n / Math.PI` / `n / (d * Math.PI)` when the division
    // form reconstructs the value. The useFraction gate: the %.17g form
    // must carry more than five significant characters (the C#'s "r"
    // round-trip string minus the sign and the leading digit).
    static std::string SpecialDoubleConstantText(double value) {
        static const double kFields[2] = {3.141592653589793,
                                           2.718281828459045};
        static const char* kNames[2] = {"PI", "E"};
        constexpr int kMaxDenominator = 1000;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", value);
        std::string str = buf;
        if (str.size() - (str[0] == '-' ? 2 : 1) <= 5) return std::string();
        for (int i = 0; i < 2; ++i) {
            auto [num, den] =
                FractionApprox(value / kFields[i], kMaxDenominator);
            if (!IsValidFraction(num, den)) continue;
            // The multiply form: field * n / d == value.
            double approx = kFields[i] * static_cast<double>(num) /
                            static_cast<double>(den);
            if (approx == value) {
                std::string expr = std::string("Math.") + kNames[i];
                if (num == -1) expr = "-" + expr;
                else if (num != 1)
                    expr += " * " + std::to_string(num);
                if (den != 1)
                    expr += " / " + std::to_string(den);
                return expr;
            }
            // The division form: n / (d * field) == value.
            double divApprox = static_cast<double>(num) /
                               (static_cast<double>(den) * kFields[i]);
            if (divApprox == value) {
                std::string field = std::string("Math.") + kNames[i];
                if (den == 1)
                    return std::to_string(num) + " / " + field;
                return std::to_string(num) + " / (" +
                       std::to_string(den) + " * " + field + ")";
            }
        }
        return std::string();
    }

    // The short method name (after "::") for an instance call: receiver.Method.
    static std::string ShortMethodName(std::string_view full) {
        auto pos = full.rfind("::");
        return pos != std::string_view::npos ? std::string(full.substr(pos + 2)) : std::string(full);
    }

    // The property name for a get_/set_ accessor method, or "" if not an
    // accessor. `get_Major` -> `Major`, `set_Value` -> `Value`.
    static std::string AccessorPropertyName(std::string_view full) {
        auto pos = full.rfind("::");
        std::string_view member = (pos != std::string_view::npos) ? full.substr(pos + 2) : full;
        if (member.size() > 4 && member.substr(0, 4) == "get_")
            return std::string(member.substr(4));
        if (member.size() > 4 && member.substr(0, 4) == "set_")
            return std::string(member.substr(4));
        return std::string{};
    }

    // An instance call (call/callvirt, not newobj) renders as
    // `receiver.Method(restArgs)`; the receiver is Arguments[0]. A ref/deref
    // receiver (`&V`, `*(&V)`) is parenthesized so the member access binds.
    // Static calls and newobj go through CallText. A property accessor
    // (get_X with 0 extra args / set_X with 1 extra arg) renders as
    // `receiver.X` / `receiver.X = value`.
    std::string InstanceCallText(const Call& call) {
        if (call.Arguments.empty() || !call.Arguments[0])
            return CallText(call);
        // Property accessor: get_X(receiver) -> receiver.X ;
        // set_X(receiver, value) -> receiver.X = value.
        std::string prop = AccessorPropertyName(call.MethodName);
        if (!prop.empty()) {
            std::string recv = Expr(*call.Arguments[0]);
            bool needsParens = !recv.empty() && (recv[0] == '&' || recv[0] == '*');
            if (IsCastInstruction(call.Arguments[0].get()))
                needsParens = true;
            std::string target = (needsParens ? "(" + recv + ")" : recv) + "." + prop;
            if (call.MethodName.size() >= 4) {
                auto pos = call.MethodName.rfind("::");
                std::string_view member = (pos != std::string_view::npos)
                    ? std::string_view(call.MethodName).substr(pos + 2) : std::string_view(call.MethodName);
                if (member.substr(0, 4) == "set_" && call.Arguments.size() >= 2) {
                    return target + " = " + (call.Arguments[1] ? Expr(*call.Arguments[1]) : std::string("(default)"));
                }
            }
            return target;
        }
        std::string recv = Expr(*call.Arguments[0]);
        // A ref/deref receiver renders with a leading `ref `/`&`/`*`, which
        // binds looser than `.` -- parenthesize so the member access wins.
        bool needsParens = recv.size() >= 4 && recv.compare(0, 4, "ref ") == 0;
        if (!needsParens && !recv.empty()) needsParens = (recv[0] == '&' || recv[0] == '*');
        if (IsCastInstruction(call.Arguments[0].get()))
            needsParens = true;
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
                // The C# constructor initializer renders a base call only
                // with arguments; the no-argument form is the implicit
                // default and renders nothing -- but only when the call
                // is the first statement of the constructor body (the
                // IntroduceConstructorInitializers shape). A base call
                // that appears later stays a statement and renders the
                // raw method reference (`base..ctor();`).
                if (call.Arguments.size() == 1) {
                    const auto* blk =
                        dynamic_cast<const Block*>(call.Parent);
                    bool firstInBlock =
                        blk != nullptr && !blk->Instructions.empty() &&
                        blk->Instructions.front().get() == &call;
                    bool entryBlock =
                        fn_ != nullptr && fn_->Body != nullptr &&
                        !fn_->Body->Blocks.empty() &&
                        fn_->Body->Blocks.front().get() == blk;
                    if (firstInBlock && entryBlock)
                        return std::string();
                    return std::string("base..ctor()");
                }
                std::string text = "base(";
                for (std::size_t i = 1; i < call.Arguments.size(); ++i) {
                    if (i > 1) text += ", ";
                    text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
                }
                text += ')';
                return text;
            }
        }
        // The event add/remove accessors rewrite ahead of the
        // instance/static split (the compound-assignment form).
        {
            std::string eventText = EventAddRemoveText(call);
            if (!eventText.empty())
                return eventText;
        }
        return call.IsInstanceCall ? InstanceCallText(call) : CallText(call);
    }

    // `V = V op expr` -> `V op= expr` (or `V++`/`V--` for +/- 1) when the
    // binary's left is a load of the same variable. A plain `V = expr` (no
    // self-load, or an operator C# has no compound form for) returns ` = expr`.
    // `ldc.i4 0`/`ldc.i4 1` in the return position of a Boolean-returning
    // function renders as `false`/`true` (the IL idiom for bool return values).
    std::string BoolLiteralFromReturn(const ILInstruction* value) const {
        if (returnTypeName_ != "bool" || !value || value->Op != OpCode::LdcI4) return {};
        auto v = static_cast<const LdcI4*>(value)->Value;
        if (v == 0) return "false";
        if (v == 1) return "true";
        return {};
    }

    // `ldc.i4 0`/`ldc.i4 1` stored into a Boolean variable renders as
    // `false`/`true` (the IL idiom for Boolean constants). Returns "" when the
    // value is not a Boolean literal for a Boolean variable.
    static std::string BoolLiteralText(const ILVariable* var, const ILInstruction* value) {
        if (!var || !var->Type || !value || value->Op != OpCode::LdcI4) return {};
        auto* k = dynamic_cast<const TypeSystem::KnownType*>(var->Type.get());
        if (!k || k->Code() != TypeSystem::KnownTypeCode::Boolean) return {};
        auto v = static_cast<const LdcI4*>(value)->Value;
        if (v == 0) return "false";
        if (v == 1) return "true";
        return {};
    }

    std::string AssignmentText(const StLoc& st, const std::string& name) {
        (void)name;
        auto* bin = dynamic_cast<const BinaryNumericInstruction*>(st.Value.get());
        if (!bin || !bin->Left || bin->Left->Op != OpCode::LdLoc) {
            auto bl = BoolLiteralText(st.Variable.get(), st.Value.get());
            return " = " + (bl.empty() ? Expr(*st.Value) : bl);
        }
        auto* ld = static_cast<const LdLoc*>(bin->Left.get());
        if (!ld->Variable || !st.Variable || ld->Variable.get() != st.Variable.get()) {
            auto bl = BoolLiteralText(st.Variable.get(), st.Value.get());
            return " = " + (bl.empty() ? Expr(*st.Value) : bl);
        }
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
    // If `target` is a load of a byref variable (a managed ref to its own
    // type), the C# is just the variable name -- the indirection is implicit.
    // Handles `*(this)` in value-type methods and `*(byrefParam)`.
    static std::string ByRefVarName(const ILInstruction& target) {
        auto* ld = dynamic_cast<const LdLoc*>(&target);
        if (!ld || !ld->Variable) return std::string{};
        // The implicit `this` parameter: ldobj(ldarg this) in a value-type
        // method loads `this` as a value -- render as `this` (the indirection
        // is implicit). The reader leaves this's type null, so check by name/kind.
        if (ld->Variable->Kind == VariableKind::Parameter && ld->Variable->Name == "this")
            return "this";
        if (!ld->Variable->Type) return std::string{};
        if (dynamic_cast<const TypeSystem::ByReferenceType*>(ld->Variable->Type.get()))
            return ld->Variable->Name;
        return std::string{};
    }

    std::string StoreTargetText(const ILInstruction& target) {
        if (target.Op == OpCode::LdFlda) {
            const auto& f = static_cast<const LdFlda&>(target);
            std::string field = FlattenMetadataName(f.FieldName);
            std::string obj = f.Target ? Expr(*f.Target) : "(default)";
            return obj == "this" ? SimpleName(field) : obj + "." + SimpleName(field);
        }
        if (target.Op == OpCode::LdsFlda) {
            std::string flattened = FlattenMetadataName(
                static_cast<const LdsFlda&>(target).FieldName);
            std::string simplified = SimplifyQualifiedMember(flattened);
            return simplified == flattened ? ShortQualifiedMember(flattened)
                                           : simplified;
        }
        if (target.Op == OpCode::LdElema) return ElementAccess(static_cast<const LdElema&>(target));
        auto byref = ByRefVarName(target);
        if (!byref.empty()) return byref;
        // A pointer-typed local: `*ptr` (the deref is explicit but needs no
        // parens around a bare name).
        if (auto* ld = dynamic_cast<const LdLoc*>(&target))
            if (ld->Variable && ld->Variable->Type &&
                dynamic_cast<const TypeSystem::PointerType*>(ld->Variable->Type.get()))
                return "*" + ld->Variable->Name;
        return "*(" + Expr(target) + ")";
    }

    // "Namespace.Type::name" -> "name" (the field/member segment).
    static std::string SimpleName(const std::string& flattened) {
        auto dot = flattened.rfind('.');
        return dot == std::string::npos ? flattened : flattened.substr(dot + 1);
    }

    std::string ElementAccess(const LdElema& elema) {
        // A foreach-substituted element access renders as the iteration
        // variable.
        if (foreachSubst_.count(&elema) != 0)
            return foreachElementName_;
        std::string text = elema.Array ? Expr(*elema.Array) : "(default)";
        text += '[';
        for (std::size_t i = 0; i < elema.Indices.size(); ++i) {
            if (i) text += ", ";
            text += elema.Indices[i] ? Expr(*elema.Indices[i]) : "(default)";
        }
        text += ']';
        return text;
    }

    // Negate a condition's text for the seed: a Comp `(a op b)` becomes
    // `(a op.Negate b)` (re-render with the negated kind), else wrap as `!(cond)`.
    std::string NegateCondText(const ILInstruction& cond) {
        if (auto* comp = dynamic_cast<const Comp*>(&cond)) {
            std::string left = comp->Left ? Expr(*comp->Left) : "(default)";
            std::string right = comp->Right ? Expr(*comp->Right) : "(default)";
            const char* op = "==";
            switch (NegateComparison(comp->Kind)) {
                case ComparisonKind::Equality: op = "=="; break;
                case ComparisonKind::Inequality: op = "!="; break;
                case ComparisonKind::LessThan: op = "<"; break;
                case ComparisonKind::LessThanOrEqual: op = "<="; break;
                case ComparisonKind::GreaterThan: op = ">"; break;
                case ComparisonKind::GreaterThanOrEqual: op = ">="; break;
            }
            return "(" + left + " " + op + " " + right + ")";
        }
        return "!(" + Expr(cond) + ")";
    }

    // Strip one layer of outer parens from a string if they enclose the whole
    // string with balanced nesting (e.g. `(a + b)` -> `a + b`, but
    // `(a)(b)` stays). Used for assignment values and conditions.
    static std::string StripOuterParens(std::string e) {
        if (e.size() < 2 || e.front() != '(' || e.back() != ')') return e;
        int depth = 0;
        for (std::size_t i = 0; i < e.size(); ++i) {
            if (e[i] == '(') ++depth;
            else if (e[i] == ')') { --depth; if (depth == 0 && i + 1 < e.size()) return e; }
        }
        return e.substr(1, e.size() - 2);
    }

    // The condition expression for an `if`/`while`: strip redundant outer
    // parens so `if ((cond))` becomes `if (cond)`.
    std::string CondExpr(const ILInstruction& inst) {
        return StripOuterParens(ConvertConditionText(inst, false));
    }

    // The C# ExpressionBuilder.TranslateCondition ->
    // TranslatedExpression.ConvertToBoolean: a condition consumer (the
    // if/loop condition, the ternary condition) needs a Boolean
    // expression. A Boolean-valued condition (a comparison, a short-
    // circuit) renders bare; an integer-typed one renders `x != 0` (the
    // negated form `x == 0`); an int32 constant folds to the bool
    // literal. An unclassifiable (unknown-typed) condition renders bare
    // -- the C# TypeKind.Unknown arm. The enum zero-member and the
    // pointer-null arms are deferred (no fixture exercises them).
    std::string ConvertConditionText(const ILInstruction& cond, bool negate) {
        switch (cond.Op) {
            case OpCode::Comp:
            case OpCode::IfInstruction:
                // Boolean-valued by construction.
                return Expr(cond);
            case OpCode::LdcI4: {
                bool val = static_cast<const LdcI4&>(cond).Value != 0;
                val ^= negate;
                return val ? "true" : "false";
            }
            case OpCode::BinaryNumericInstruction:
                return Expr(cond) + (negate ? " == 0" : " != 0");
            case OpCode::LdLoc: {
                const auto* ld = static_cast<const LdLoc*>(&cond);
                const auto* k =
                    ld->Variable && ld->Variable->Type
                        ? dynamic_cast<const TypeSystem::KnownType*>(
                              ld->Variable->Type.get())
                        : nullptr;
                if (k != nullptr) {
                    switch (k->Code()) {
                        case TypeSystem::KnownTypeCode::Boolean:
                            return Expr(cond);
                        case TypeSystem::KnownTypeCode::SByte:
                        case TypeSystem::KnownTypeCode::Byte:
                        case TypeSystem::KnownTypeCode::Int16:
                        case TypeSystem::KnownTypeCode::UInt16:
                        case TypeSystem::KnownTypeCode::Int32:
                        case TypeSystem::KnownTypeCode::UInt32:
                        case TypeSystem::KnownTypeCode::Int64:
                        case TypeSystem::KnownTypeCode::UInt64:
                        case TypeSystem::KnownTypeCode::Char:
                            return Expr(cond) + (negate ? " == 0" : " != 0");
                        default:
                            return Expr(cond);
                    }
                }
                return Expr(cond);
            }
            case OpCode::Call: {
                const auto* call = static_cast<const Call*>(&cond);
                const auto* k =
                    call->ReturnIType
                        ? dynamic_cast<const TypeSystem::KnownType*>(
                              call->ReturnIType.get())
                        : nullptr;
                if (k != nullptr &&
                    k->Code() == TypeSystem::KnownTypeCode::Boolean)
                    return Expr(cond);
                if (k != nullptr) {
                    switch (k->Code()) {
                        case TypeSystem::KnownTypeCode::SByte:
                        case TypeSystem::KnownTypeCode::Byte:
                        case TypeSystem::KnownTypeCode::Int16:
                        case TypeSystem::KnownTypeCode::UInt16:
                        case TypeSystem::KnownTypeCode::Int32:
                        case TypeSystem::KnownTypeCode::UInt32:
                        case TypeSystem::KnownTypeCode::Int64:
                        case TypeSystem::KnownTypeCode::UInt64:
                            return Expr(cond) + (negate ? " == 0" : " != 0");
                        default:
                            return Expr(cond);
                    }
                }
                return Expr(cond);
            }
            default:
                return Expr(cond);
        }
    }

    std::string Expr(const ILInstruction& inst) {
        if (DepthAtLimit()) {
            return "/* max rendering depth: possible ILAst cycle */";
        }
        DepthGuard g{depth_};
        switch (inst.Op) {
            case OpCode::Block: {
                // A Block(InterpolatedString) evaluates to the string the
                // ToStringAndClear final yields; render it as the `$"..."`
                // interpolation (the ToStringAndClear is the implicit conversion,
                // matching the real back end's TranslateInterpolatedString).
                // A plain ControlFlow block cannot evaluate to a value, so it
                // only reaches Expr via the InterpolatedString kind.
                const auto& blk = static_cast<const Block&>(inst);
                if (blk.Kind == BlockKind::InterpolatedString)
                    return InterpolatedStringText(blk);
                if (blk.Kind == BlockKind::ArrayInitializer)
                    return ArrayInitializerText(blk);
                return "(default)/*op=" + std::to_string(static_cast<int>(inst.Op)) + "*/";
            }
            case OpCode::LdLoc: {
                const auto& ld = static_cast<const LdLoc&>(inst);
                // The elided coalesce target: the single use site renders
                // the folded expression itself.
                auto el = coalesceElisions_.find(&ld);
                if (el != coalesceElisions_.end())
                    return Expr(*el->second);
                auto su = singleUseElisions_.find(&ld);
                if (su != singleUseElisions_.end())
                    return Expr(*su->second);
                return ld.Variable ? ld.Variable->Name : "?";
            }
            case OpCode::StLoc: {
                // An inline assignment used as an expression value:
                // `outer = (inner = value)` -- render as the chained assignment
                // `outer = inner = value`. Chained assignment is right-
                // associative, so no parentheses are needed around the inner.
                const auto& st = static_cast<const StLoc&>(inst);
                std::string name = st.Variable ? st.Variable->Name : std::string("?");
                std::string val = st.Value ? Expr(*st.Value) : std::string("(default)");
                return name + " = " + val;
            }
            case OpCode::LdLoca: {
                const auto& ld = static_cast<const LdLoca&>(inst);
                // `ldloca V` is the IL idiom for `ref V` (a byref argument or
                // an address-of). Render as `ref V` (the C# form), not `&V`
                // (the IL form).
                return "ref " + (ld.Variable ? ld.Variable->Name : std::string("?"));
            }
            case OpCode::LdcI4: {
                // The C# IsSpecialConstant: the recognizable boundary
                // constants render as their named forms.
                std::int32_t v = static_cast<const LdcI4&>(inst).Value;
                if (v == 2147483647) return "int.MaxValue";
                if (v == -2147483648) return "int.MinValue";
                return std::to_string(v);
            }
            case OpCode::LdcI8: {
                // The long boundary constants (the same IsSpecialConstant
                // arm over the 64-bit values).
                std::int64_t v = static_cast<const LdcI8&>(inst).Value;
                if (v == 9223372036854775807LL) return "long.MaxValue";
                if (v == -9223372036854775807LL - 1) return "long.MinValue";
                return std::to_string(v);
            }
            case OpCode::LdcDecimal:
                // The C# decimal literal form (`1m`, `0m`, `-1m`, `1.5m`),
                // faithful to the real back end's VisitLdcDecimal
                // (ConvertConstantValue -> a PrimitiveExpression with the `m`
                // suffix). The trailing zeros from the scale are preserved
                // (System.Decimal.ToString semantics).
                return static_cast<const LdcDecimal&>(inst).Value.ToString() + "m";
            case OpCode::LdcF4: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.9g", static_cast<const LdcF4&>(inst).Value);
                return std::string(buf) + "f";
            }
            case OpCode::LdcF8: {
                double value = static_cast<const LdcF8&>(inst).Value;
                // The C# ConvertFloatingPointLiteral's special-constants
                // arm: a literal that reconstructs exactly from Math.PI or
                // Math.E (times a small fraction) renders as the field
                // reference. The raw-fraction arm before it (a plain
                // `num / den` literal) is not ported -- no fixture
                // exercises a fraction literal; the float (MathF) arm is
                // likewise deferred (the flat emitter carries no
                // compilation to probe MathF's presence, and no fixture
                // exercises it).
                {
                    std::string special = SpecialDoubleConstantText(value);
                    if (!special.empty()) return special;
                }
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", value);
                return buf;
            }
            case OpCode::LdStr:
                return EscapeStringLiteral(static_cast<const LdStr&>(inst).Value);
            case OpCode::LdNull:
                return "null";
            case OpCode::Call: {
                const auto& callRef = static_cast<const Call&>(inst);
                // The null-propagation fold: the use renders the value
                // expression with the null-conditional.
                auto np = nullPropagation_.find(&callRef);
                if (np != nullPropagation_.end() && np->second != nullptr) {
                    std::string method =
                        callRef.MethodName.rfind("::") == std::string::npos
                            ? callRef.MethodName
                            : callRef.MethodName.substr(
                                  callRef.MethodName.rfind("::") + 2);
                    std::string args;
                    for (std::size_t ai = 1; ai < callRef.Arguments.size();
                         ++ai) {
                        if (!args.empty()) args += ", ";
                        args += Expr(*callRef.Arguments[ai]);
                    }
                    return Expr(*np->second) + "?." + method + "(" + args +
                           ")";
                }
                return callRef.IsInstanceCall
                    ? InstanceCallText(callRef)
                    : CallText(callRef);
            }
            case OpCode::Comp: {
                const auto& comp = static_cast<const Comp&>(inst);
                // `comp(eq, ldloc boolVar, ldc.i4 0)` is `!boolVar` (a Boolean
                // negation); `comp(ne, ldloc boolVar, 0)` is just `boolVar`.
                // Also `comp(eq, call BoolMethod(..), 0)` is `!BoolMethod(..)`.
                // Also `comp(eq, LogicAnd/LogicOr, 0)` (a value IfInstruction
                // built by ConditionDetection.IntroduceShortCircuit then
                // wrapped by TryInvertIfExit's NegateCondition/Comp.LogicNot)
                // is `!(a && b)` / `!(a || b)` -- the logic.not of a short-
                // circuit condition. The LogicAnd/LogicOr shape is matched here
                // (not via a shared helper) to keep the seed renderer
                // self-contained; the bare LogicAnd/LogicOr Expr case (D204)
                // renders the inner `(a && b)` / `(a || b)`.
                if (comp.Right && comp.Right->Op == OpCode::LdcI4 &&
                    static_cast<const LdcI4*>(comp.Right.get())->Value == 0 &&
                    comp.Left) {
                    bool isBoolLeft = false;
                    std::string leftExpr;
                    if (comp.Left->Op == OpCode::LdLoc) {
                        auto* ld = static_cast<const LdLoc*>(comp.Left.get());
                        if (ld->Variable && ld->Variable->Type &&
                            dynamic_cast<const TypeSystem::KnownType*>(ld->Variable->Type.get()) &&
                            static_cast<const TypeSystem::KnownType*>(ld->Variable->Type.get())->Code() ==
                                TypeSystem::KnownTypeCode::Boolean) {
                            isBoolLeft = true;
                            leftExpr = ld->Variable->Name;
                        }
                    } else if (comp.Left->Op == OpCode::Call) {
                        auto* call = static_cast<const Call*>(comp.Left.get());
                        if (call->ReturnIType) {
                            auto* k = dynamic_cast<const TypeSystem::KnownType*>(call->ReturnIType.get());
                            if (k && k->Code() == TypeSystem::KnownTypeCode::Boolean) {
                                isBoolLeft = true;
                                leftExpr = Expr(*comp.Left);
                            }
                        }
                    } else if (comp.Left->Op == OpCode::IfInstruction) {
                        // LogicAnd(a, b) = `if (a) b else ldc.i4 0`;
                        // LogicOr(a, b)  = `if (a) ldc.i4 1 else b`.
                        // Both evaluate to Boolean, so `comp(eq, X, 0)` is `!X`.
                        auto* iff = static_cast<const IfInstruction*>(comp.Left.get());
                        auto isLdcI4 = [](const ILInstruction* a, int v) -> bool {
                            if (!a || a->Op != OpCode::LdcI4) return false;
                            return static_cast<const LdcI4*>(a)->Value == v;
                        };
                        if (iff->Condition &&
                            ((isLdcI4(iff->FalseInst.get(), 0) && iff->TrueInst) ||
                             (isLdcI4(iff->TrueInst.get(), 1) && iff->FalseInst))) {
                            isBoolLeft = true;
                            leftExpr = Expr(*comp.Left);
                        }
                    }
                    if (isBoolLeft)
                        return comp.Kind == ComparisonKind::Equality ? "!" + leftExpr : leftExpr;
                    // The null-literal typing: a reference-typed left makes
                    // the zero constant the null reference (the C# oracle
                    // renders `x == null`, not `x == 0`). The value types
                    // keep the numeric literal.
                    bool isRefLeft = false;
                    const TypeSystem::IType* leftType = nullptr;
                    if (comp.Left->Op == OpCode::LdLoc) {
                        auto* ld = static_cast<const LdLoc*>(
                            comp.Left.get());
                        if (ld->Variable) leftType = ld->Variable->Type.get();
                    } else if (comp.Left->Op == OpCode::Call) {
                        leftType = static_cast<const Call*>(
                            comp.Left.get())->ReturnIType.get();
                    }
                    if (leftType != nullptr) {
                        auto* k = dynamic_cast<const TypeSystem::KnownType*>(
                            leftType);
                        if (k == nullptr) {
                            isRefLeft = true;
                        } else {
                            switch (k->Code()) {
                                case TypeSystem::KnownTypeCode::Boolean:
                                case TypeSystem::KnownTypeCode::Char:
                                case TypeSystem::KnownTypeCode::SByte:
                                case TypeSystem::KnownTypeCode::Byte:
                                case TypeSystem::KnownTypeCode::Int16:
                                case TypeSystem::KnownTypeCode::UInt16:
                                case TypeSystem::KnownTypeCode::Int32:
                                case TypeSystem::KnownTypeCode::UInt32:
                                case TypeSystem::KnownTypeCode::Int64:
                                case TypeSystem::KnownTypeCode::UInt64:
                                case TypeSystem::KnownTypeCode::Single:
                                case TypeSystem::KnownTypeCode::Double:
                                    break;
                                default:
                                    isRefLeft = true;
                                    break;
                            }
                        }
                    }
                    if (isRefLeft) {
                        return (comp.Left ? Expr(*comp.Left)
                                           : std::string("(default)")) +
                               " " +
                               (comp.Kind == ComparisonKind::Equality
                                    ? "=="
                                    : "!=") +
                               " null";
                    }
                }
                const char* op = "==";
                switch (comp.Kind) {
                    case ComparisonKind::Equality: op = "=="; break;
                    case ComparisonKind::Inequality: op = "!="; break;
                    case ComparisonKind::LessThan: op = "<"; break;
                    case ComparisonKind::LessThanOrEqual: op = "<="; break;
                    case ComparisonKind::GreaterThan: op = ">"; break;
                    case ComparisonKind::GreaterThanOrEqual: op = ">="; break;
                }
                // The comparison renders without the outer parentheses:
                // the C# ast keeps the precedence and no context needs
                // them (the call argument list, the if condition, the
                // store, and the return all take the bare operator
                // expression). A nested comparison (a comparison of a
                // comparison result) would need precedence parens; that
                // shape only arises from bool-typed stack stacking,
                // which the bool-negation arms above rewrite.
                return (comp.Left ? Expr(*comp.Left) : "(default)") + " " + op + " " +
                       (comp.Right ? Expr(*comp.Right) : "(default)");
            }
            case OpCode::BinaryNumericInstruction: {
                const auto& bin = static_cast<const BinaryNumericInstruction&>(inst);
                // `0 - x` (and `0.0 - x`) is the IL for unary negation `-x`.
                if (bin.Operator == BinaryNumericOperator::Sub && bin.Left &&
                    (bin.Left->Op == OpCode::LdcI4 && static_cast<const LdcI4*>(bin.Left.get())->Value == 0)) {
                    return "-" + Expr(*bin.Right);
                }
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
                std::string text = (bin.Left ? Expr(*bin.Left) : "(default)") + " " + op + " " +
                                   (bin.Right ? Expr(*bin.Right) : "(default)");
                if (bin.CheckForOverflow) text = "checked(" + text + ")";
                // The C# ast parenthesizes by precedence: the flat emitter
                // keeps the outer parens only where the tree shape demands
                // them -- under a parent binary when this is the RIGHT
                // operand or the parent binds tighter (e.g. `(a + b) * c`
                // keeps, `a * b + c` drops), and in the ternary branch
                // slots (the ILSpy conditional style). The parent-binary
                // LEFT with an equal-or-looser parent (`(a * b) * c` ->
                // `a * b * c`), the ternary condition slot, and the
                // non-binary contexts (the store value, the call argument,
                // the return tail) render bare.
                bool parenthesize = false;
                const ILInstruction* p = inst.Parent;
                if (p != nullptr &&
                    p->Op == OpCode::BinaryNumericInstruction) {
                    const auto* pb =
                        static_cast<const BinaryNumericInstruction*>(p);
                    if (!(pb->Left.get() == &inst &&
                          BinaryPrecedence(pb->Operator) >=
                              BinaryPrecedence(bin.Operator)))
                        parenthesize = true;
                } else if (p != nullptr && p->Op == OpCode::IfInstruction &&
                           inst.ChildIndex != 0) {
                    parenthesize = true;
                }
                if (parenthesize) text = "(" + text + ")";
                return text;
            }
            case OpCode::Conv: {
                const auto& conv = static_cast<const Conv&>(inst);
                // conv.i4(ldlen) is the IL for `array.Length` (the raw ldlen
                // pushes a native int; the cast to i4 is implicit in C#). The
                // VisitConv `conv.iN(ldlen)` fold (ExpressionTransforms) folds
                // this to a single LdLen(I4, ..) in the pipeline, so this special
                // case is a fallback for the pre-fold shape (e.g. a test that
                // calls the seed directly).
                if (conv.ResultType() == StackType::I4 && conv.Argument &&
                    conv.Argument->Op == OpCode::LdLen) {
                    return Expr(*conv.Argument);
                }
                return "(" + std::string(ConvTargetName(conv.ResultType())) + ")(" +
                       (conv.Argument ? Expr(*conv.Argument) : "(default)") + ")";
            }
            case OpCode::CastClass: {
                const auto& cast = static_cast<const CastClass&>(inst);
                return CastText(TypeDisplayName(cast.Type), cast.Argument);
            }
            case OpCode::Unbox: {
                const auto& unbox = static_cast<const Unbox&>(inst);
                return CastText(TypeDisplayName(unbox.Type), unbox.Argument);
            }
            case OpCode::UnboxAny: {
                const auto& unbox = static_cast<const UnboxAny&>(inst);
                return CastText(TypeDisplayName(unbox.Type), unbox.Argument);
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
            case OpCode::RefAnyType: {
                // The `refanytype` IL opcode (the C# `__reftype` undocumented
                // keyword) returns the System.Type embedded in a TypedReference;
                // the C# back end renders it as `__reftype(arg).TypeHandle` (the
                // UndocumentedExpression + `.TypeHandle` member access). This is
                // the faithful render without the resolver (the resolver would
                // resolve `.TypeHandle` to the property; the keyword form is valid
                // C# and matches the C# ILSpy back end's ExpressionBuilder output).
                const auto& ref = static_cast<const RefAnyType&>(inst);
                return "__reftype(" + (ref.Argument ? Expr(*ref.Argument) : "(default)") +
                       ").TypeHandle";
            }
            case OpCode::MakeRefAny: {
                // `mkrefany <T>` -- the C# `__makeref(arg)` undocumented keyword.
                const auto& mr = static_cast<const MakeRefAny&>(inst);
                return "__makeref(" + (mr.Argument ? Expr(*mr.Argument) : "(default)") + ")";
            }
            case OpCode::RefAnyValue: {
                // `refanyval <T>` -- the C# `__refvalue(arg, T)` undocumented
                // keyword (a managed pointer to the value in the typed reference).
                const auto& rv = static_cast<const RefAnyValue&>(inst);
                return "__refvalue(" + (rv.Argument ? Expr(*rv.Argument) : "(default)") +
                       ", " + TypeDisplayName(rv.Type) + ")";
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
                return obj == "this" ? SimpleName(field) : obj + "." + SimpleName(field);
            }
            case OpCode::LdsFlda: {
                std::string flattened = FlattenMetadataName(
                    static_cast<const LdsFlda&>(inst).FieldName);
                std::string simplified = SimplifyQualifiedMember(flattened);
                return simplified == flattened
                           ? ShortQualifiedMember(flattened)
                           : simplified;
            }
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
                return SimplifyQualifiedMember(FlattenMetadataName(
                    static_cast<const LdFtn&>(inst).MethodName));
            case OpCode::LdVirtFtn:
                return FlattenMetadataName(static_cast<const LdVirtFtn&>(inst).MethodName);
            case OpCode::LdVirtDelegate: {
                // A virtual delegate construction renders as
                // `new DelegateType(target.Method)` -- the real back end's
                // VisitLdVirtDelegate (CallBuilder.Build -> HandleDelegateConstruction)
                // folds the target and the virtual method into a `target.Method`
                // method group inside the `new DelegateType(...)`. The delegate
                // type renders as its reflection name (matching the existing
                // newobj rendering `new System.Action(...)`); the method is the
                // short name (after `::`) so `target.Bar` is the C# method-group
                // form.
                const auto& d = static_cast<const LdVirtDelegate&>(inst);
                std::string typeName = d.Type ? d.Type->ReflectionName() : std::string("var");
                std::string target = d.Argument ? Expr(*d.Argument) : std::string("(default)");
                return "new " + typeName + "(" + target + "." +
                       ShortMethodName(d.MethodName) + ")";
            }
            case OpCode::SizeOf:
                return "sizeof(" + static_cast<const SizeOf&>(inst).TypeName + ")";
            case OpCode::LdTypeToken: {
                // The C# name lookup renders the typeof's type through the
                // using directives (`typeof(Circle)` over
                // `typeof(Demo.Circle)`); the flat emitter takes the short
                // name (the last segment -- the same convention as the
                // static-call target type).
                std::string typeName =
                    FlattenMetadataName(static_cast<const LdTypeToken&>(inst).TokenName);
                auto dot = typeName.rfind('.');
                if (dot != std::string::npos &&
                    typeName.find('<') == std::string::npos)
                    typeName = typeName.substr(dot + 1);
                return "typeof(" + typeName + ")";
            }
            case OpCode::Arglist:
                // The real back end's VisitArglist renders the ArgListAccess
                // UndocumentedExpression as the `__arglist` keyword.
                return "__arglist";
            case OpCode::DefaultValue: {
                const auto& dv = static_cast<const DefaultValue&>(inst);
                return "default(" + CSharpTypeName(dv.Type) + ")";
            }
            case OpCode::MatchInstruction: {
                // A MatchInstruction renders as the C# `is` pattern
                // `testedOperand is <pattern>`, matching the real back end's
                // VisitMatchInstruction (a BinaryOperatorExpression with the Is
                // operator). The pattern is: `var v` (IsVar), `T v` (CheckType
                // + designator), `{} v` (CheckNotNull, no type), or `T` (a pure
                // type test with no designator). Sub-patterns (recursive
                // patterns) and deconstruct patterns are deferred.
                const auto& m = static_cast<const MatchInstruction&>(inst);
                std::string lhs = m.TestedOperand ? Expr(*m.TestedOperand) : std::string("(default)");
                std::string varName = m.Variable ? m.Variable->Name : std::string("_");
                std::string pattern;
                if (m.IsVar()) {
                    // `expr is var x`
                    pattern = "var " + varName;
                } else if (m.CheckType) {
                    std::string typeName = m.Variable && m.Variable->Type
                        ? CSharpTypeName(m.Variable->Type) : std::string("var");
                    if (m.HasDesignator()) {
                        // `expr is T x` (a CheckNotNull + CheckType is still
                        // `is T x`; the non-null is implied by the type test for
                        // reference types).
                        pattern = typeName + " " + varName;
                    } else {
                        // `expr is T` (pure type test, no designator).
                        pattern = typeName;
                    }
                } else if (m.CheckNotNull) {
                    // `expr is {} x` (non-null pattern, no type).
                    pattern = "{} " + varName;
                } else {
                    // No flags and no sub-patterns but not IsVar (e.g. an empty
                    // recursive pattern): render the designator as `var x`.
                    pattern = "var " + varName;
                }
                return lhs + " is " + pattern;
            }
            case OpCode::IfInstruction: {
                // The expression form of an IfInstruction. LogicAnd/LogicOr --
                // `if (a) b else ldc.i4 0` / `if (a) ldc.i4 1 else b` (the C#
                // IfInstruction.LogicAnd/LogicOr forms) -- render as the
                // short-circuit operators `a && b` / `a || b`, not the ternary.
                // The statement form (`if (cond) { ... } else { ... }`) is emitted
                // by the Statement path and does not reach Expr.
                const auto& iff = static_cast<const IfInstruction&>(inst);
                auto isLdcI4 = [](const ILInstruction* a, int v) -> bool {
                    if (!a || a->Op != OpCode::LdcI4) return false;
                    return static_cast<const LdcI4*>(a)->Value == v;
                };
                if (isLdcI4(iff.FalseInst.get(), 0) && iff.TrueInst && iff.Condition) {
                    // LogicAnd(a, b) = if (a) b else 0  ->  a && b
                    return "(" + Expr(*iff.Condition) + " && " + Expr(*iff.TrueInst) + ")";
                }
                if (isLdcI4(iff.TrueInst.get(), 1) && iff.FalseInst && iff.Condition) {
                    // LogicOr(a, b) = if (a) 1 else b  ->  a || b
                    return "(" + Expr(*iff.Condition) + " || " + Expr(*iff.FalseInst) + ")";
                }
                // Otherwise the conditional operator `cond ? true : false`.
                auto ArmExpr = [&](const std::unique_ptr<ILInstruction>& arm) -> std::string {
                    if (!arm) return "(default)";
                    if (arm->Op == OpCode::Block) {
                        auto* blk = static_cast<const Block*>(arm.get());
                        if (!blk->FinalInstruction && blk->Instructions.size() == 1)
                            return Expr(*blk->Instructions[0]);
                    }
                    return Expr(*arm);
                };
                std::string cond = iff.Condition
                    ? ConvertConditionText(*iff.Condition, false)
                    : "(default)";
                // The ILSpy conditional style: the condition and the
                // branches each parenthesized, the whole conditional
                // parenthesized (`((cond) ? (a) : (b))`).
                return "((" + cond + ") ? " + ArmExpr(iff.TrueInst) +
                       " : " + ArmExpr(iff.FalseInst) + ")";
            }
            case OpCode::NullCoalescingInstruction: {
                // The C# `??` (null-coalescing) operator: `value ?? fallback`.
                // Faithful to the real back end's VisitNullCoalescingInstruction
                // (a BinaryOperatorExpression with the NullCoalescing operator).
                // The Kind (Ref / Nullable / NullableWithValueFallback) is a
                // semantic flavour the back end uses to pick the conversion; the
                // surface syntax is the same `??` for all three.
                const auto& nc = static_cast<const NullCoalescingInstruction&>(inst);
                std::string value = nc.ValueInst ? Expr(*nc.ValueInst) : std::string("(default)");
                std::string fallback = nc.FallbackInst ? Expr(*nc.FallbackInst) : std::string("(default)");
                return "(" + value + " ?? " + fallback + ")";
            }
            case OpCode::ThreeValuedBoolAnd: {
                // Three-valued logic `&` on bool?: `left & right`. Faithful to
                // the real back end's VisitThreeValuedBoolAnd (a
                // BinaryOperatorExpression with the BitwiseAnd operator; the
                // operands are converted to bool?). Unlike logic.and() this does
                // not short-circuit (the three-valued truth tables require both
                // sides to detect a null result).
                const auto& tv = static_cast<const ThreeValuedBoolAnd&>(inst);
                std::string left = tv.Left ? Expr(*tv.Left) : std::string("(default)");
                std::string right = tv.Right ? Expr(*tv.Right) : std::string("(default)");
                return "(" + left + " & " + right + ")";
            }
            case OpCode::ThreeValuedBoolOr: {
                // Three-valued logic `|` on bool?: `left | right`. Faithful to
                // the real back end's VisitThreeValuedBoolOr (a
                // BinaryOperatorExpression with the BitwiseOr operator).
                const auto& tv = static_cast<const ThreeValuedBoolOr&>(inst);
                std::string left = tv.Left ? Expr(*tv.Left) : std::string("(default)");
                std::string right = tv.Right ? Expr(*tv.Right) : std::string("(default)");
                return "(" + left + " | " + right + ")";
            }
            case OpCode::UserDefinedLogicOperator: {
                // A user-defined short-circuiting logic operator: `left && right`
                // for op_BitwiseAnd, `left || right` for op_BitwiseOr. Faithful
                // to the real back end's VisitUserDefinedLogicOperator (a
                // BinaryOperatorExpression with the ConditionalAnd /
                // ConditionalOr operator derived from the method name). The
                // operator is the short method name (after "::"), matching
                // GetBinaryOperatorTypeFromMetadataName.
                const auto& ul = static_cast<const UserDefinedLogicOperator&>(inst);
                std::string left = ul.Left ? Expr(*ul.Left) : std::string("(default)");
                std::string right = ul.Right ? Expr(*ul.Right) : std::string("(default)");
                auto pos = ul.MethodName.rfind("::");
                std::string shortName = (pos != std::string::npos)
                    ? ul.MethodName.substr(pos + 2) : ul.MethodName;
                const char* op = (shortName == "op_BitwiseOr") ? "||" : "&&";
                return "(" + left + " " + op + " " + right + ")";
            }
            case OpCode::NullableRewrap: {
                // The C# null-conditional rewrap is implicit in the `?.`
                // surface syntax: `x?.M()` is `nullable.rewrap(M(nullable.unwrap(x)))`,
                // and the rewrap (which evaluates to null when an inner unwrap
                // took the null branch) does not appear as a separate operator.
                // The real back end renders it as a UnaryOperatorExpression
                // (NullConditionalRewrap) that the output visitor elides. The
                // seed renders the access-chain argument directly (the Box
                // precedent -- boxing is implicit in C#; so is the rewrap).
                const auto& nr = static_cast<const NullableRewrap&>(inst);
                return nr.Argument ? Expr(*nr.Argument) : std::string("(default)");
            }
            case OpCode::NullableUnwrap: {
                // The C# null-conditional `?.` is the NullableUnwrap. The real
                // back end renders it as a UnaryOperatorExpression (NullConditional)
                // whose postfix `?` sits on the receiver of the surrounding
                // member access (`x?.M`); the seed has no member-access context
                // here, so it renders the unwrapped value with a trailing `?`
                // to keep the null-conditional visible (a placeholder until the
                // real back end lands).
                const auto& nu = static_cast<const NullableUnwrap&>(inst);
                return (nu.Argument ? Expr(*nu.Argument) : std::string("(default)")) + "?";
            }
            case OpCode::NumericCompoundAssign: {
                // A numeric compound assignment `target op= value` (or the
                // post-increment/decrement `target++`/`target--` for Add/Sub with
                // a ldc.i4 1 RHS in EvaluatesToOldValue mode). Faithful to the
                // real back end's VisitNumericCompoundAssign (an
                // AssignmentExpression with the operator derived from Operator).
                // The Address Target is an LdLoca; the back end loads the value
                // at that address (LdObj(Target, Type)), so the seed renders the
                // target variable's bare name (stripping the address `&`).
                const auto& ca = static_cast<const NumericCompoundAssign&>(inst);
                std::string target;
                if (ca.TargetKind == CompoundTargetKind::Address &&
                    ca.Target && ca.Target->Op == OpCode::LdLoca) {
                    const auto& lda = static_cast<const LdLoca&>(*ca.Target);
                    target = lda.Variable ? lda.Variable->Name : std::string("?");
                } else {
                    target = ca.Target ? Expr(*ca.Target) : std::string("(default)");
                }
                std::string value = ca.Value ? Expr(*ca.Value) : std::string("(default)");
                // The post-increment/decrement: Add/Sub with a ldc.i4 1 RHS in
                // the EvaluatesToOldValue mode renders as `target++`/`target--`.
                if (ca.EvalMode == CompoundEvalMode::EvaluatesToOldValue &&
                    (ca.Operator == BinaryNumericOperator::Add ||
                     ca.Operator == BinaryNumericOperator::Sub) &&
                    ca.Value && ca.Value->Op == OpCode::LdcI4) {
                    const auto& c = static_cast<const LdcI4&>(*ca.Value);
                    if (c.Value == 1) {
                        return target + (ca.Operator == BinaryNumericOperator::Add ? "++" : "--");
                    }
                }
                const char* op = "?=";
                switch (ca.Operator) {
                    case BinaryNumericOperator::Add: op = "+="; break;
                    case BinaryNumericOperator::Sub: op = "-="; break;
                    case BinaryNumericOperator::Mul: op = "*="; break;
                    case BinaryNumericOperator::Div: op = "/="; break;
                    case BinaryNumericOperator::Rem: op = "%="; break;
                    case BinaryNumericOperator::BitAnd: op = "&="; break;
                    case BinaryNumericOperator::BitOr: op = "|="; break;
                    case BinaryNumericOperator::BitXor: op = "^="; break;
                    case BinaryNumericOperator::ShiftLeft: op = "<<="; break;
                    case BinaryNumericOperator::ShiftRight: op = ">>="; break;
                }
                return target + " " + op + " " + value;
            }
            case OpCode::UserDefinedCompoundAssign: {
                // A user-defined compound assignment built from a user-defined
                // operator call. Faithful to the real back end's
                // VisitUserDefinedCompoundAssign: a 1-arg op_Increment/
                // op_Decrement renders as the unary `target++`/`++target`/
                // `target--`/`--target` (EvaluatesToOldValue = postfix,
                // EvaluatesToNewValue = prefix); a 2-arg operator renders as
                // `target op= value` (op_Addition -> `+=`, ...); `string.Concat`
                // renders as `target += value`. The operator is derived from the
                // method name (the part after "::"), matching the C#
                // GetUnaryOperatorTypeFromMetadataName /
                // GetAssignmentOperatorTypeFromMetadataName.
                const auto& ca = static_cast<const UserDefinedCompoundAssign&>(inst);
                std::string target;
                if (ca.TargetKind == CompoundTargetKind::Address &&
                    ca.Target && ca.Target->Op == OpCode::LdLoca) {
                    const auto& lda = static_cast<const LdLoca&>(*ca.Target);
                    target = lda.Variable ? lda.Variable->Name : std::string("?");
                } else {
                    target = ca.Target ? Expr(*ca.Target) : std::string("(default)");
                }
                // The short method name (after "::") selects the operator.
                auto pos = ca.MethodName.rfind("::");
                std::string shortName = (pos != std::string::npos)
                    ? ca.MethodName.substr(pos + 2) : ca.MethodName;
                // The 1-arg increment/decrement operators render as the unary
                // forms (postfix for EvaluatesToOldValue, prefix for NewValue).
                if (shortName == "op_Increment" || shortName == "op_CheckedIncrement") {
                    return (ca.EvalMode == CompoundEvalMode::EvaluatesToOldValue)
                        ? target + "++" : "++" + target;
                }
                if (shortName == "op_Decrement" || shortName == "op_CheckedDecrement") {
                    return (ca.EvalMode == CompoundEvalMode::EvaluatesToOldValue)
                        ? target + "--" : "--" + target;
                }
                // The 2-arg operators (and string.Concat) render as
                // `target op= value`.
                std::string value = ca.Value ? Expr(*ca.Value) : std::string("(default)");
                const char* op = "?=";
                if (shortName == "Concat" || shortName == "op_Addition") op = "+=";
                else if (shortName == "op_Subtraction") op = "-=";
                else if (shortName == "op_Multiply") op = "*=";
                else if (shortName == "op_Division") op = "/=";
                else if (shortName == "op_Modulus") op = "%=";
                else if (shortName == "op_BitwiseAnd") op = "&=";
                else if (shortName == "op_BitwiseOr") op = "|=";
                else if (shortName == "op_ExclusiveOr") op = "^=";
                else if (shortName == "op_LeftShift") op = "<<=";
                else if (shortName == "op_RightShift") op = ">>=";
                else if (shortName == "op_UnsignedRightShift") op = ">>>=";
                return target + " " + op + " " + value;
            }
            case OpCode::Await: {
                // An await as a value: `await <expr>` (the real back end's
                // VisitAwait).
                const auto& aw = static_cast<const Await&>(inst);
                return "await " + (aw.Value ? Expr(*aw.Value)
                                           : std::string("(default)"));
            }
            case OpCode::DynamicGetMemberInstruction: {
                // `target.Name` (the real back end's VisitDynamicGetMember).
                const auto& gm =
                    static_cast<const DynamicGetMemberInstruction&>(inst);
                return (gm.Target ? Expr(*gm.Target)
                                  : std::string("(default)")) +
                       "." + gm.Name;
            }
            case OpCode::DynamicInvokeMemberInstruction: {
                // `target.Name<TArgs>(args)`.
                const auto& im =
                    static_cast<const DynamicInvokeMemberInstruction&>(inst);
                std::string result =
                    im.Arguments.empty()
                        ? std::string("(default)")
                        : Expr(*im.Arguments[0]);
                result += "." + im.Name;
                if (!im.TypeArguments.empty()) {
                    result += "<";
                    for (std::size_t i = 0; i < im.TypeArguments.size(); ++i) {
                        if (i != 0) result += ", ";
                        result += im.TypeArguments[i]
                                      ? CSharpTypeName(im.TypeArguments[i])
                                      : std::string("var");
                    }
                    result += ">";
                }
                result += "(" + DynamicArgumentList(im, 1) + ")";
                return result;
            }
            case OpCode::DynamicInvokeInstruction: {
                // `target(args)`.
                const auto& iv =
                    static_cast<const DynamicInvokeInstruction&>(inst);
                return (iv.Arguments.empty()
                            ? std::string("(default)")
                            : Expr(*iv.Arguments[0])) +
                       "(" + DynamicArgumentList(iv, 1) + ")";
            }
            case OpCode::DynamicGetIndexInstruction: {
                // `target[indices]`.
                const auto& gi =
                    static_cast<const DynamicGetIndexInstruction&>(inst);
                return (gi.Arguments.empty()
                            ? std::string("(default)")
                            : Expr(*gi.Arguments[0])) +
                       "[" + DynamicArgumentList(gi, 1) + "]";
            }
            case OpCode::DynamicInvokeConstructorInstruction: {
                // `new T(args)`.
                const auto& ic =
                    static_cast<const DynamicInvokeConstructorInstruction&>(
                        inst);
                return "new " + CSharpTypeName(ic.ConstructedType) + "(" +
                       DynamicArgumentList(ic, 0) + ")";
            }
            case OpCode::DynamicBinaryOperatorInstruction: {
                // `left <op> right` (the real back end's
                // VisitDynamicBinaryOperator over the ExpressionType map).
                const auto& bo =
                    static_cast<const DynamicBinaryOperatorInstruction&>(
                        inst);
                std::string left =
                    bo.Left ? Expr(*bo.Left) : std::string("(default)");
                std::string right =
                    bo.Right ? Expr(*bo.Right) : std::string("(default)");
                return left + " " + DynamicOperatorSymbol(bo.Operation) +
                       " " + right;
            }
            case OpCode::DynamicUnaryOperatorInstruction: {
                // `<op>operand` (the prefix operators) or
                // `operand<op>` (the post forms).
                const auto& uo =
                    static_cast<const DynamicUnaryOperatorInstruction&>(
                        inst);
                std::string operand =
                    uo.Operand ? Expr(*uo.Operand)
                               : std::string("(default)");
                switch (uo.Operation) {
                    case ::ILSpy::Decompiler::IL::ExpressionType::
                        PostIncrementAssign:
                        return operand + "++";
                    case ::ILSpy::Decompiler::IL::ExpressionType::
                        PostDecrementAssign:
                        return operand + "--";
                    default:
                        return DynamicOperatorSymbol(uo.Operation) + operand;
                }
            }
            case OpCode::DynamicConvertInstruction: {
                // An explicit conversion renders the cast; an implicit one
                // leaves the operand alone (the real back end's
                // VisitDynamicConversion wraps the CastExpression only when
                // C# requires it).
                const auto& cv =
                    static_cast<const DynamicConvertInstruction&>(inst);
                std::string operand =
                    cv.Argument ? Expr(*cv.Argument)
                                : std::string("(default)");
                if (cv.IsExplicit())
                    return "(" + CSharpTypeName(cv.Type) + ")" + operand;
                return operand;
            }
            case OpCode::DynamicIsEventInstruction: {
                // The is-event check rides the compound-assignment
                // pattern; the bare node renders its argument (the event
                // access).
                const auto& ie =
                    static_cast<const DynamicIsEventInstruction&>(inst);
                return ie.Argument ? Expr(*ie.Argument)
                                   : std::string("(default)");
            }
            default:
                return "(default)/*op=" + std::to_string(static_cast<int>(inst.Op)) + "*/";
        }
    }

    // The comma-separated argument/element list of a dynamic
    // variable-argument node, from `start` on.
    std::string DynamicArgumentList(
        const ::ILSpy::Decompiler::IL::DynamicArgumentsInstruction& dyn,
        std::size_t start) {
        std::string result;
        for (std::size_t i = start; i < dyn.Arguments.size(); ++i) {
            if (i != start) result += ", ";
            result += dyn.Arguments[i]
                          ? Expr(*dyn.Arguments[i])
                          : std::string("(default)");
        }
        return result;
    }

    // The C# operator symbol of a dynamic operator (the ExpressionBuilder
    // operator map's common subset; the exotic kinds fall back to the
    // ExpressionType name).
    static std::string DynamicOperatorSymbol(
        ::ILSpy::Decompiler::IL::ExpressionType op) {
        using ET = ::ILSpy::Decompiler::IL::ExpressionType;
        switch (op) {
            case ET::Add:
            case ET::AddAssign:
            case ET::AddAssignChecked:
                return "+";
            case ET::AddChecked:
                return "+";
            case ET::Subtract:
            case ET::SubtractAssign:
            case ET::SubtractAssignChecked:
                return "-";
            case ET::Multiply:
            case ET::MultiplyAssign:
            case ET::MultiplyAssignChecked:
                return "*";
            case ET::Divide:
            case ET::DivideAssign:
                return "/";
            case ET::Modulo:
            case ET::ModuloAssign:
                return "%";
            case ET::Equal:
                return "==";
            case ET::NotEqual:
                return "!=";
            case ET::GreaterThan:
                return ">";
            case ET::GreaterThanOrEqual:
                return ">=";
            case ET::LessThan:
                return "<";
            case ET::LessThanOrEqual:
                return "<=";
            case ET::And:
            case ET::AndAssign:
                return "&";
            case ET::Or:
            case ET::OrAssign:
                return "|";
            case ET::ExclusiveOr:
            case ET::ExclusiveOrAssign:
                return "^";
            case ET::LeftShift:
            case ET::LeftShiftAssign:
                return "<<";
            case ET::RightShift:
            case ET::RightShiftAssign:
                return ">>";
            case ET::Negate:
            case ET::NegateChecked:
            case ET::Decrement:
            case ET::PreDecrementAssign:
                return "-";
            case ET::UnaryPlus:
                return "+";
            case ET::Not:
                return "!";
            case ET::OnesComplement:
                return "~";
            case ET::Increment:
            case ET::PreIncrementAssign:
                return "++";
            default:
                return ::ILSpy::Decompiler::IL::ExpressionTypeName(op);
        }
    }

    // The expression form of a load target (LdObj child): field, array
    // element, or dereference.
    std::string LoadTargetText(const ILInstruction& target) {
        if (target.Op == OpCode::LdFlda || target.Op == OpCode::LdsFlda ||
            target.Op == OpCode::LdElema) {
            return Expr(target);
        }
        auto byref = ByRefVarName(target);
        if (!byref.empty()) return byref;
        if (auto* ld = dynamic_cast<const LdLoc*>(&target))
            if (ld->Variable && ld->Variable->Type &&
                dynamic_cast<const TypeSystem::PointerType*>(ld->Variable->Type.get()))
                return "*" + ld->Variable->Name;
        return "*(" + Expr(target) + ")";
    }
};

} // namespace

// A C# type name for a declaration: the C# keyword for primitives (int, bool,
// string, ...), the short type name otherwise (a seed approximation -- real
// C# would carry a using directive), recursing into array/byref. `var` for an
// unknown type (e.g. an untyped stack slot).
std::string CSharpTypeName(const TypeSystem::ITypePtr& type) {
    if (!type) return "var";
    if (auto* a = dynamic_cast<const TypeSystem::ArrayType*>(type.get()))
        return CSharpTypeName(a->Element()) + (a->IsSzArray() ? "[]" : "[,]");
    if (auto* byref = dynamic_cast<const TypeSystem::ByReferenceType*>(type.get()))
        return CSharpTypeName(byref->Element());
    if (auto* ptr = dynamic_cast<const TypeSystem::PointerType*>(type.get()))
        return CSharpTypeName(ptr->Element()) + "*";
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
            // The IntPtr/UIntPtr known types render the FULL NAMES (the
            // C# keyword table has no entries for them); the nint/nuint
            // keywords belong to the ELEMENT_TYPE_I/U forms (the
            // NInt/NUInt kinds, the by-kind switch below).
            default: break;
        }
    }
    if (auto* p = dynamic_cast<const TypeSystem::ParameterizedType*>(type.get())) {
        // A generic type: C# `SimpleName<T1, T2>`. The base (generic definition)
        // reflection name carries the ``N arity suffix; strip it. Each type
        // argument renders recursively (so System.String -> string).
        std::string base;
        if (p->GenericType()) {
            std::string rn = p->GenericType()->ReflectionName();
            ::ILSpy::Decompiler::CSharp::RequiredImports::RecordTypeName(rn);
            auto dot = rn.rfind('.');
            base = dot != std::string::npos ? rn.substr(dot + 1) : rn;
        } else {
            base = "?";
        }
        auto bt = base.find('`');
        if (bt != std::string::npos) base = base.substr(0, bt);  // arity suffix
        base += "<";
        for (std::size_t i = 0; i < p->TypeArguments().size(); ++i) {
            if (i) base += ", ";
            base += CSharpTypeName(p->TypeArguments()[i]);
        }
        base += ">";
        return base;
    }
    std::string rn = type->ReflectionName();
    // The builtin type keywords (the C# UseKeywordsForBuiltinTypes over
    // any type whose reflection name is a known primitive -- the entity
    // model resolves System.UInt32 to a SimpleType, not a KnownType, so
    // the dynamic_cast arm above misses it). The native integers stay
    // OFF this map: only the ELEMENT_TYPE_I/U signature forms (the
    // KnownType arm above) render the nint/nuint keywords; a plain
    // System.IntPtr type reference renders the full name.
    static const std::map<std::string, const char*> kBuiltinKeywords = {
        {"System.Boolean", "bool"},   {"System.Char", "char"},
        {"System.SByte", "sbyte"},   {"System.Byte", "byte"},
        {"System.Int16", "short"},   {"System.UInt16", "ushort"},
        {"System.Int32", "int"},     {"System.UInt32", "uint"},
        {"System.Int64", "long"},    {"System.UInt64", "ulong"},
        {"System.Single", "float"},  {"System.Double", "double"},
        {"System.Decimal", "decimal"}, {"System.String", "string"},
        {"System.Object", "object"},
    };
    auto builtin = kBuiltinKeywords.find(rn);
    if (builtin != kBuiltinKeywords.end())
        return builtin->second;
    // A nested type the render did not resolve (the file-signature
    // decode's reference carries the reflection `+` chain): the nested
    // name is only nameable through its enclosing type, so it renders
    // the full dotted form (the C# simple type's dotted full name).
    if (rn.find('+') != std::string::npos) {
        ::ILSpy::Decompiler::CSharp::RequiredImports::RecordTypeName(rn);
        std::string dotted = rn;
        for (char& c : dotted)
            if (c == '+') c = '.';
        return dotted;
    }
    ::ILSpy::Decompiler::CSharp::RequiredImports::RecordTypeName(rn);
    auto pos = rn.rfind('.');
    return pos != std::string::npos ? rn.substr(pos + 1) : rn;
}

std::string ILAstToCSharp(const ILFunction& fn,
                          std::string_view returnType,
                          std::string_view methodName,
                          std::string_view paramDecl,
                          bool isConstructor,
                          std::string_view methodConstraints) {
    std::string out;
    CEmitter emitter(out);
    emitter.EmitMethod(fn, returnType, methodName, paramDecl, isConstructor,
                       methodConstraints);
    return out;
}

} // namespace ILSpy::Decompiler::IL
