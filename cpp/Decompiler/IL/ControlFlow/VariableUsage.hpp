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

// Variable- and block-usage analysis (C++-only infrastructure). The C# tracks
// ILVariable.LoadCount/StoreCount/AddressCount and Block.IncomingEdgeCount
// through reader events; this port computes them fresh over the tree before a
// transform run (and the control-flow transforms keep them current while they
// rewrite, mirroring the event updates).

#pragma once

namespace ILSpy::Decompiler::IL {

class ILFunction;

// Recompute every variable's LoadCount/StoreCount/AddressCount from the tree.
// Parameters start with StoreCount == 1 (they arrive with a value), matching
// the C# usesInitialValue convention.
void ComputeVariableUsage(ILFunction& function);

// Recompute every block's IncomingEdgeCount from the Branch nodes in the tree.
void RecomputeIncomingEdgeCounts(ILFunction& function);

} // namespace ILSpy::Decompiler::IL
