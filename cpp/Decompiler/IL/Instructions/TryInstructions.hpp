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

// Exception-handler ILAst nodes, faithful to the generated Instructions.cs and
// the hand-written TryInstruction.cs. TryInstruction is the base (TryBlock child,
// DirectFlags=ControlFlow, result Void). TryCatch adds a Handlers collection;
// TryCatchHandler carries an optional Filter, a Body, and the catch Variable;
// TryFinally/TryFault add a second block. The IL reader builds these from the
// method's ExceptionHandlerClause list.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

// Base: a try block. DirectFlags=ControlFlow; result Void.
class TryInstruction : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> TryBlock;
protected:
    TryInstruction(OpCode op, std::unique_ptr<ILInstruction> tryBlock)
        : ILInstruction(op), TryBlock(std::move(tryBlock)) {
        if (TryBlock) { TryBlock->Parent = this; TryBlock->ChildIndex = 0; }
    }
public:
    InstructionFlags DirectFlags() const override { return InstructionFlags::ControlFlow; }
    StackType ResultType() const override { return StackType::Void; }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(TryBlock);
        TryBlock = std::move(n);
        return old;
    }
};

// try { } catch (T v) { }: TryBlock + a collection of TryCatchHandler.
class TryCatchHandler : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Filter;  // optional (real catch filters)
    std::unique_ptr<ILInstruction> Body;
    ILVariablePtr Variable;  // the caught exception (null for fault/finally-less)
    TryCatchHandler(std::unique_ptr<ILInstruction> filter, std::unique_ptr<ILInstruction> body,
                    ILVariablePtr variable)
        : ILInstruction(OpCode::TryCatchHandler),
          Filter(std::move(filter)), Body(std::move(body)), Variable(std::move(variable)) {
        if (Filter) { Filter->Parent = this; Filter->ChildIndex = 0; }
        if (Body) { Body->Parent = this; Body->ChildIndex = 1; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::ControlFlow | InstructionFlags::MayWriteLocals;
    }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return (Filter ? 1 : 0) + (Body ? 1 : 0); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Filter.get();
        if (i == 1) return Body.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "catch ";
        if (Variable && Variable->Type) out += Variable->Type->ReflectionName();
        else out += "?";
        if (Variable) { out += " "; out += Variable->Name; }
        out += " { ";
        if (Body) Body->WriteTo(out); else out += "(null)";
        out += " }";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0 || i == 1);
        if (i == 0) { auto old = std::move(Filter); Filter = std::move(n); return old; }
        auto old = std::move(Body); Body = std::move(n); return old;
    }
};

class TryCatch : public TryInstruction {
public:
    std::vector<std::unique_ptr<TryCatchHandler>> Handlers;
    explicit TryCatch(std::unique_ptr<ILInstruction> tryBlock) : TryInstruction(OpCode::TryCatch, std::move(tryBlock)) {}
    int ChildCount() const override { return 1 + static_cast<int>(Handlers.size()); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return TryBlock.get();
        int h = i - 1;
        return (h >= 0 && h < static_cast<int>(Handlers.size())) ? Handlers[h].get() : nullptr;
    }
    void AddHandler(std::unique_ptr<TryCatchHandler> h) {
        if (h) { h->Parent = this; h->ChildIndex = static_cast<int>(Handlers.size()) + 1; }
        Handlers.push_back(std::move(h));
    }
    void WriteTo(std::string& out) const override {
        out += "try { ";
        if (TryBlock) TryBlock->WriteTo(out); else out += "(null)";
        out += " } ";
        for (auto& h : Handlers) { if (h) h->WriteTo(out); out += ' '; }
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(TryBlock); TryBlock = std::move(n); return old; }
        int h = i - 1;
        if (h < 0 || h >= static_cast<int>(Handlers.size())) return n;
        auto old = std::move(Handlers[h]);
        Handlers[h].reset(static_cast<TryCatchHandler*>(n.release()));
        return old;
    }
};

class TryFinally : public TryInstruction {
public:
    std::unique_ptr<ILInstruction> FinallyBlock;
    TryFinally(std::unique_ptr<ILInstruction> tryBlock, std::unique_ptr<ILInstruction> finallyBlock)
        : TryInstruction(OpCode::TryFinally, std::move(tryBlock)), FinallyBlock(std::move(finallyBlock)) {
        if (FinallyBlock) { FinallyBlock->Parent = this; FinallyBlock->ChildIndex = 1; }
    }
    int ChildCount() const override { return 1 + (FinallyBlock ? 1 : 0); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return TryBlock.get();
        if (i == 1) return FinallyBlock.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "try { ";
        if (TryBlock) TryBlock->WriteTo(out); else out += "(null)";
        out += " } finally { ";
        if (FinallyBlock) FinallyBlock->WriteTo(out); else out += "(null)";
        out += " }";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(TryBlock); TryBlock = std::move(n); return old; }
        assert(i == 1);
        auto old = std::move(FinallyBlock); FinallyBlock = std::move(n); return old;
    }
};

class TryFault : public TryInstruction {
public:
    std::unique_ptr<ILInstruction> FaultBlock;
    TryFault(std::unique_ptr<ILInstruction> tryBlock, std::unique_ptr<ILInstruction> faultBlock)
        : TryInstruction(OpCode::TryFault, std::move(tryBlock)), FaultBlock(std::move(faultBlock)) {
        if (FaultBlock) { FaultBlock->Parent = this; FaultBlock->ChildIndex = 1; }
    }
    int ChildCount() const override { return 1 + (FaultBlock ? 1 : 0); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return TryBlock.get();
        if (i == 1) return FaultBlock.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        out += "try { ";
        if (TryBlock) TryBlock->WriteTo(out); else out += "(null)";
        out += " } fault { ";
        if (FaultBlock) FaultBlock->WriteTo(out); else out += "(null)";
        out += " }";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(TryBlock); TryBlock = std::move(n); return old; }
        assert(i == 1);
        auto old = std::move(FaultBlock); FaultBlock = std::move(n); return old;
    }
};

} // namespace ILSpy::Decompiler::IL
