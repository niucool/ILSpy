// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the BusyManager port (cpp/Decompiler/Util/BusyManager.{hpp,cpp} --
// the thread-static reentrance guard the recursive type-system resolutions
// use; the first ported consumer is MetadataModule.ResolveForwardedType).
//
// Every expectation below is gold-pinned against the REAL ICSharpCode.Decompiler
// 11.0's public BusyManager class driven over the identical key shapes (the
// C:/temp-probe/ResolutionProbe gold probe, section A).

#include "Decompiler/Util/BusyManager.hpp"

#include <gtest/gtest.h>

using ILSpy::Decompiler::Util::BusyLock;
using ILSpy::Decompiler::Util::BusyManager;

namespace {

struct Key1 {};
struct Key2 {};

} // namespace

// A1/A2/A3: the first Enter succeeds, a re-Enter of the SAME key while held
// fails, an Enter of a DIFFERENT key succeeds.
TEST(BusyManagerTest, EnterReentranceAndIndependence)
{
    Key1 o1;
    Key2 o2;
    {
        BusyLock l1 = BusyManager::Enter(&o1);
        EXPECT_TRUE(l1.Success());                          // A1
        BusyLock l1b = BusyManager::Enter(&o1);
        EXPECT_FALSE(l1b.Success());                        // A2
        BusyLock l2 = BusyManager::Enter(&o2);
        EXPECT_TRUE(l2.Success());                          // A3
    }
}

// A4/A5: the null key is a legal entry that compares equal to itself (the
// second null Enter fails while the first is held).
TEST(BusyManagerTest, NullKeyIsLegalAndReentrant)
{
    {
        BusyLock n1 = BusyManager::Enter(nullptr);
        EXPECT_TRUE(n1.Success());                         // A4
        BusyLock n2 = BusyManager::Enter(nullptr);
        EXPECT_FALSE(n2.Success());                        // A5
    }
    // after the pops the null key is enterable again
    BusyLock n3 = BusyManager::Enter(nullptr);
    EXPECT_TRUE(n3.Success());
}

// A6/A7/A8: the scope exit pops the key, so the key is enterable again; a
// key still held by an OUTER lock stays unenterable.
TEST(BusyManagerTest, DisposePopsAndOuterHeldKeyStaysBusy)
{
    Key1 o1;
    {
        BusyLock l1 = BusyManager::Enter(&o1);
        {
            BusyLock inner = BusyManager::Enter(&o1);
            EXPECT_FALSE(inner.Success());
        }
        // A6: o1 is STILL held by the outer lock -- a fresh Enter fails.
        BusyLock again = BusyManager::Enter(&o1);
        EXPECT_FALSE(again.Success());
    }
    {
        // A7: after the outer lock's scope exit o1 is enterable again.
        BusyLock l3 = BusyManager::Enter(&o1);
        EXPECT_TRUE(l3.Success());
    }
    {
        // A8: and enterable once more after that lock's exit.
        BusyLock l4 = BusyManager::Enter(&o1);
        EXPECT_TRUE(l4.Success());
    }
}

// The LIFO-pop quirk: the lock pops the list's LAST entry, so nested
// scopes always unwind in reverse order and the stack stays balanced
// across interleaved keys.
TEST(BusyManagerTest, NestedScopesUnwindLifo)
{
    Key1 a;
    Key2 b;
    {
        BusyLock la = BusyManager::Enter(&a);
        EXPECT_TRUE(la.Success());
        {
            BusyLock lb = BusyManager::Enter(&b);
            EXPECT_TRUE(lb.Success());
            BusyLock lb2 = BusyManager::Enter(&b);
            EXPECT_FALSE(lb2.Success());
        }
        // b popped; a is still held.
        BusyLock lb3 = BusyManager::Enter(&b);
        EXPECT_TRUE(lb3.Success());
        BusyLock la2 = BusyManager::Enter(&a);
        EXPECT_FALSE(la2.Success());
    }
    BusyLock la3 = BusyManager::Enter(&a);
    EXPECT_TRUE(la3.Success());
}

// The Failed lock's destruction is a no-op (the null-list Dispose).
TEST(BusyManagerTest, FailedLockDestroyIsNoOp)
{
    Key1 o1;
    BusyLock l1 = BusyManager::Enter(&o1);
    BusyLock failed = BusyManager::Enter(&o1);
    EXPECT_FALSE(failed.Success());
    // 'failed' destructs here without touching the list; o1's lock still
    // holds its entry.
    BusyLock again = BusyManager::Enter(&o1);
    EXPECT_FALSE(again.Success());
}
