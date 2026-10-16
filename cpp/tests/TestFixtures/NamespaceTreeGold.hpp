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

// The namespace-definition gold fixtures (the TinyNetModule.hpp precedent):
// the SRM namespace tree pinned against the REAL .NET 10 System.Reflection
// Metadata (the C:/temp-probe/NsDefProbe reflection probe driving
// MetadataReader.GetNamespaceDefinitionRoot / GetNamespaceDefinition /
// GetString over the identical fixtures).
//
//   * kNsDefSynthHex / kNsDefMergeHex: a 2048-byte assembly manifest built
//     with SRM's MetadataBuilder/ManagedPEBuilder (the iteration-12 recipe)
//     carrying every namespace shape no real file pins together -- a type
//     in the empty namespace, three types sharing one "Q.R" heap location
//     (suffix-shared inside "P.Q.R"), the "P.Q.R" chain forcing the virtual
//     synthesis of "P" and "P.Q", a nested type (the IsNested skip), a
//     forwarder in the same namespace as a real type and one in the empty
//     namespace (the shared-offset mixed accumulation), and the three
//     ExportedType implementation kinds (the nested-forwarder skip among
//     them). The MERGE variant re-points one TypeDef row's namespace column
//     at the NUL byte of an existing string -- a second heap offset whose
//     string reads "" -- pinning the duplicate-full-name merge (the row
//     moves from "Q.R" into the root's type list, appended after the root's
//     own types, and its handle maps to the root's node).
//   * kGold<Name>: the probe's byte-exact dump per fixture (the TREE walk
//     plus the handle table in insertion order). mscorlib/System/CoreLib
//     carry the node lines only (their per-token lists are pure row order;
//     the counts pin the row-to-namespace assignment); synth/merge/facade/
//     tiny carry the full token lines. kGoldThrows pins the unknown-handle
//     exception, the nil-is-root quirk, and the virtual-handle full name.
//
// Regenerate with: dotnet run (in C:/temp-probe/NsDefProbe) > gold_raw.txt,
// then python3 gen_fixture.py.

#pragma once

#include <cstring>
#include <string>

namespace ILSpy::Tests::NsDefGold {

// The synthetic namespace-shape manifest (2048 bytes).
inline constexpr char kNsDefSynthHex[] =
    "4D5A90000300000004000000FFFF0000B8000000000000004000000000000000"
    "0000000000000000000000000000000000000000000000000000000080000000"
    "0E1FBA0E00B409CD21B8014CCD21546869732070726F6772616D2063616E6E6F"
    "742062652072756E20696E20444F53206D6F64652E0D0D0A2400000000000000"
    "504500004C01020073159C6A0000000000000000E00002200B01300000040000"
    "0002000000000000922300000020000000400000000040000020000000020000"
    "0400000000000000040000000000000000600000000200000000000003004085"
    "0000100000100000000010000010000000000000100000000000000000000000"
    "402300004F000000000000000000000000000000000000000000000000000000"
    "004000000C000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000020000008000000"
    "0000000000000000082000004800000000000000000000002E74657874000000"
    "9803000000200000000400000002000000000000000000000000000020000060"
    "2E72656C6F6300000C0000000040000000020000000600000000000000000000"
    "0000000040000042000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "7423000000000000480000000200050050200000700200000100000000000000"
    "0000000000000000C02200008000000000000000000000000000000000000000"
    "0000000000000000000000000000000042534A4201000100000000000C000000"
    "76342E302E33303331390000000005006C00000024010000237E000090010000"
    "C800000023537472696E6773000000005802000004000000235553005C020000"
    "1000000023475549440000006C0200000400000023426C6F6200000000000000"
    "0200000105000000C900000000FA013300160000010000000800000001000000"
    "0100000001000000050000000000810001000000000001000000B40000000000"
    "0000000001000000490008000000010001000100000090000800000001000100"
    "010000002D000800000001000100010000009700060000000100010001000000"
    "9D009E0000000100010001000000A400AB000000010001000200000020000000"
    "1C00010001000480000001000000000000000000000000007600000002000000"
    "00000000000000000000BA000000000000000000360000000100000000000000"
    "62001C0005000100000000000000500001000400020000000000000016000C00"
    "0600010000000000000059009E00050001000000000000006A00000005000000"
    "004677643200502E512E52004677644E6573746564004E657374656446776400"
    "4E6573746564496E73696465005451725468726565006F746865726D6F642E6E"
    "65746D6F64756C65005451724F6E650046776432547970650046776433547970"
    "65004677645479706500467764526F6F7454797065004E7344656653796E7468"
    "004E7344656653796E74682E646C6C0054517254776F00544465657000544F74"
    "686572004E6573746572004E65737465724E730054526F6F7400536F6D654173"
    "73656D626C79000000000000DDCCBBAA221144335566778899AABBCC00000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "6823000000000000000000008223000000200000000000000000000000000000"
    "000000000000000074230000000000000000000000005F436F72446C6C4D6169"
    "6E006D73636F7265652E646C6C0000000000FF25002040000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "002000000C000000943300000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "";;

// The duplicate-full-name merge variant of the same manifest.
inline constexpr char kNsDefMergeHex[] =
    "4D5A90000300000004000000FFFF0000B8000000000000004000000000000000"
    "0000000000000000000000000000000000000000000000000000000080000000"
    "0E1FBA0E00B409CD21B8014CCD21546869732070726F6772616D2063616E6E6F"
    "742062652072756E20696E20444F53206D6F64652E0D0D0A2400000000000000"
    "504500004C01020073159C6A0000000000000000E00002200B01300000040000"
    "0002000000000000922300000020000000400000000040000020000000020000"
    "0400000000000000040000000000000000600000000200000000000003004085"
    "0000100000100000000010000010000000000000100000000000000000000000"
    "402300004F000000000000000000000000000000000000000000000000000000"
    "004000000C000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000020000008000000"
    "0000000000000000082000004800000000000000000000002E74657874000000"
    "9803000000200000000400000002000000000000000000000000000020000060"
    "2E72656C6F6300000C0000000040000000020000000600000000000000000000"
    "0000000040000042000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "7423000000000000480000000200050050200000700200000100000000000000"
    "0000000000000000C02200008000000000000000000000000000000000000000"
    "0000000000000000000000000000000042534A4201000100000000000C000000"
    "76342E302E33303331390000000005006C00000024010000237E000090010000"
    "C800000023537472696E6773000000005802000004000000235553005C020000"
    "1000000023475549440000006C0200000400000023426C6F6200000000000000"
    "0200000105000000C900000000FA013300160000010000000800000001000000"
    "0100000001000000050000000000810001000000000001000000B40000000000"
    "000000000100000049000800000001000100010000009000A300000001000100"
    "010000002D000800000001000100010000009700060000000100010001000000"
    "9D009E0000000100010001000000A400AB000000010001000200000020000000"
    "1C00010001000480000001000000000000000000000000007600000002000000"
    "00000000000000000000BA000000000000000000360000000100000000000000"
    "62001C0005000100000000000000500001000400020000000000000016000C00"
    "0600010000000000000059009E00050001000000000000006A00000005000000"
    "004677643200502E512E52004677644E6573746564004E657374656446776400"
    "4E6573746564496E73696465005451725468726565006F746865726D6F642E6E"
    "65746D6F64756C65005451724F6E650046776432547970650046776433547970"
    "65004677645479706500467764526F6F7454797065004E7344656653796E7468"
    "004E7344656653796E74682E646C6C0054517254776F00544465657000544F74"
    "686572004E6573746572004E65737465724E730054526F6F7400536F6D654173"
    "73656D626C79000000000000DDCCBBAA221144335566778899AABBCC00000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "6823000000000000000000008223000000200000000000000000000000000000"
    "000000000000000074230000000000000000000000005F436F72446C6C4D6169"
    "6E006D73636F7265652E646C6C0000000000FF25002040000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "002000000C000000943300000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "";;

// The probe gold for the synth fixture.
inline constexpr const char* kGoldSynth =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=6 types=1 exported=1
T 0x02000001
E 0x27000005
  N handle=2147483649 name='Q' full='Q' parent=nil children=1 types=0 exported=0
    N handle=8 name='R' full='Q.R' parent=2147483649 children=0 types=3 exported=0
    T 0x02000002
    T 0x02000003
    T 0x02000004
  N handle=2147483651 name='P' full='P' parent=nil children=1 types=0 exported=0
    N handle=2147483650 name='Q' full='P.Q' parent=2147483651 children=1 types=0 exported=0
      N handle=6 name='R' full='P.Q.R' parent=2147483650 children=0 types=1 exported=0
      T 0x02000005
  N handle=158 name='Other' full='Other' parent=nil children=0 types=1 exported=1
  T 0x02000006
  E 0x27000004
  N handle=171 name='NesterNs' full='NesterNs' parent=nil children=0 types=1 exported=0
  T 0x02000007
  N handle=28 name='Fwd' full='Fwd' parent=nil children=0 types=0 exported=1
  E 0x27000001
  N handle=1 name='Fwd2' full='Fwd2' parent=nil children=0 types=0 exported=1
  E 0x27000002
HANDLES
H handle=0 name='' full='' parent=nil children=6 types=1 exported=1
H handle=8 name='R' full='Q.R' parent=2147483649 children=0 types=3 exported=0
H handle=6 name='R' full='P.Q.R' parent=2147483650 children=0 types=1 exported=0
H handle=158 name='Other' full='Other' parent=nil children=0 types=1 exported=1
H handle=171 name='NesterNs' full='NesterNs' parent=nil children=0 types=1 exported=0
H handle=28 name='Fwd' full='Fwd' parent=nil children=0 types=0 exported=1
H handle=1 name='Fwd2' full='Fwd2' parent=nil children=0 types=0 exported=1
H handle=2147483649 name='Q' full='Q' parent=nil children=1 types=0 exported=0
H handle=2147483650 name='Q' full='P.Q' parent=2147483651 children=1 types=0 exported=0
H handle=2147483651 name='P' full='P' parent=nil children=1 types=0 exported=0
)gold";;

// The probe gold for the merge fixture.
inline constexpr const char* kGoldMerge =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=6 types=2 exported=1
T 0x02000001
T 0x02000003
E 0x27000005
  N handle=2147483649 name='Q' full='Q' parent=nil children=1 types=0 exported=0
    N handle=8 name='R' full='Q.R' parent=2147483649 children=0 types=2 exported=0
    T 0x02000002
    T 0x02000004
  N handle=2147483651 name='P' full='P' parent=nil children=1 types=0 exported=0
    N handle=2147483650 name='Q' full='P.Q' parent=2147483651 children=1 types=0 exported=0
      N handle=6 name='R' full='P.Q.R' parent=2147483650 children=0 types=1 exported=0
      T 0x02000005
  N handle=158 name='Other' full='Other' parent=nil children=0 types=1 exported=1
  T 0x02000006
  E 0x27000004
  N handle=171 name='NesterNs' full='NesterNs' parent=nil children=0 types=1 exported=0
  T 0x02000007
  N handle=28 name='Fwd' full='Fwd' parent=nil children=0 types=0 exported=1
  E 0x27000001
  N handle=1 name='Fwd2' full='Fwd2' parent=nil children=0 types=0 exported=1
  E 0x27000002
HANDLES
H handle=0 name='' full='' parent=nil children=6 types=2 exported=1
H handle=8 name='R' full='Q.R' parent=2147483649 children=0 types=2 exported=0
H handle=163 name='' full='' parent=nil children=6 types=2 exported=1
H handle=6 name='R' full='P.Q.R' parent=2147483650 children=0 types=1 exported=0
H handle=158 name='Other' full='Other' parent=nil children=0 types=1 exported=1
H handle=171 name='NesterNs' full='NesterNs' parent=nil children=0 types=1 exported=0
H handle=28 name='Fwd' full='Fwd' parent=nil children=0 types=0 exported=1
H handle=1 name='Fwd2' full='Fwd2' parent=nil children=0 types=0 exported=1
H handle=2147483649 name='Q' full='Q' parent=nil children=1 types=0 exported=0
H handle=2147483650 name='Q' full='P.Q' parent=2147483651 children=1 types=0 exported=0
H handle=2147483651 name='P' full='P' parent=nil children=1 types=0 exported=0
)gold";;

// The probe gold for the mscorlib fixture.
inline constexpr const char* kGoldMscorlib =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=3 types=8 exported=0
  N handle=2147483649 name='Microsoft' full='Microsoft' parent=nil children=3 types=0 exported=0
    N handle=9908 name='Win32' full='Microsoft.Win32' parent=2147483649 children=1 types=19 exported=0
      N handle=325320 name='SafeHandles' full='Microsoft.Win32.SafeHandles' parent=9908 children=0 types=19 exported=0
    N handle=2147483650 name='Runtime' full='Microsoft.Runtime' parent=2147483649 children=1 types=0 exported=0
      N handle=198259 name='Hosting' full='Microsoft.Runtime.Hosting' parent=2147483650 children=0 types=3 exported=0
    N handle=258136 name='Reflection' full='Microsoft.Reflection' parent=2147483649 children=0 types=1 exported=0
  N handle=2147483652 name='Windows' full='Windows' parent=nil children=1 types=0 exported=0
    N handle=2147483651 name='Foundation' full='Windows.Foundation' parent=2147483652 children=1 types=0 exported=0
      N handle=317634 name='Diagnostics' full='Windows.Foundation.Diagnostics' parent=2147483651 children=0 types=8 exported=0
  N handle=235862 name='System' full='System' parent=nil children=14 types=312 exported=0
    N handle=2147483653 name='Configuration' full='System.Configuration' parent=235862 children=1 types=0 exported=0
      N handle=322500 name='Assemblies' full='System.Configuration.Assemblies' parent=2147483653 children=0 types=3 exported=0
    N handle=50382 name='IO' full='System.IO' parent=235862 children=1 types=58 exported=0
      N handle=114371 name='IsolatedStorage' full='System.IO.IsolatedStorage' parent=50382 children=0 types=13 exported=0
    N handle=432186 name='Security' full='System.Security' parent=235862 children=7 types=60 exported=0
      N handle=231296 name='AccessControl' full='System.Security.AccessControl' parent=432186 children=0 types=69 exported=0
      N handle=420118 name='Cryptography' full='System.Security.Cryptography' parent=432186 children=1 types=107 exported=0
        N handle=333221 name='X509Certificates' full='System.Security.Cryptography.X509Certificates' parent=420118 children=0 types=53 exported=0
      N handle=348167 name='Permissions' full='System.Security.Permissions' parent=432186 children=0 types=64 exported=0
      N handle=346543 name='Claims' full='System.Security.Claims' parent=432186 children=0 types=6 exported=0
      N handle=224277 name='Principal' full='System.Security.Principal' parent=432186 children=0 types=29 exported=0
      N handle=415761 name='Policy' full='System.Security.Policy' parent=432186 children=0 types=58 exported=0
      N handle=228203 name='Util' full='System.Security.Util' parent=432186 children=0 types=16 exported=0
    N handle=2147483654 name='Numerics' full='System.Numerics' parent=235862 children=1 types=0 exported=0
      N handle=191928 name='Hashing' full='System.Numerics.Hashing' parent=2147483654 children=0 types=1 exported=0
    N handle=321127 name='Resources' full='System.Resources' parent=235862 children=0 types=22 exported=0
    N handle=256228 name='Globalization' full='System.Globalization' parent=235862 children=0 types=65 exported=0
    N handle=317615 name='Diagnostics' full='System.Diagnostics' parent=235862 children=4 types=26 exported=0
      N handle=156960 name='SymbolStore' full='System.Diagnostics.SymbolStore' parent=317615 children=0 types=15 exported=0
      N handle=360194 name='Contracts' full='System.Diagnostics.Contracts' parent=317615 children=1 types=15 exported=0
        N handle=221337 name='Internal' full='System.Diagnostics.Contracts.Internal' parent=360194 children=0 types=1 exported=0
      N handle=343264 name='CodeAnalysis' full='System.Diagnostics.CodeAnalysis' parent=317615 children=0 types=1 exported=0
      N handle=190506 name='Tracing' full='System.Diagnostics.Tracing' parent=317615 children=1 types=110 exported=0
        N handle=221301 name='Internal' full='System.Diagnostics.Tracing.Internal' parent=190506 children=0 types=1 exported=0
    N handle=349253 name='Collections' full='System.Collections' parent=235862 children=3 types=32 exported=0
      N handle=387632 name='Concurrent' full='System.Collections.Concurrent' parent=349253 children=0 types=11 exported=0
      N handle=226530 name='ObjectModel' full='System.Collections.ObjectModel' parent=349253 children=0 types=5 exported=0
      N handle=69384 name='Generic' full='System.Collections.Generic' parent=349253 children=0 types=42 exported=0
    N handle=190563 name='Threading' full='System.Threading' parent=235862 children=2 types=104 exported=0
      N handle=344579 name='Tasks' full='System.Threading.Tasks' parent=190563 children=0 types=62 exported=0
      N handle=156356 name='NetCore' full='System.Threading.NetCore' parent=190563 children=0 types=2 exported=0
    N handle=355310 name='StubHelpers' full='System.StubHelpers' parent=235862 children=0 types=33 exported=0
    N handle=258118 name='Reflection' full='System.Reflection' parent=235862 children=1 types=123 exported=0
      N handle=378671 name='Emit' full='System.Reflection.Emit' parent=258118 children=0 types=68 exported=0
    N handle=2147483655 name='Deployment' full='System.Deployment' parent=235862 children=1 types=0 exported=0
      N handle=221375 name='Internal' full='System.Deployment.Internal' parent=2147483655 children=1 types=2 exported=0
        N handle=252908 name='Isolation' full='System.Deployment.Internal.Isolation' parent=221375 children=1 types=75 exported=0
          N handle=397644 name='Manifest' full='System.Deployment.Internal.Isolation.Manifest' parent=252908 children=0 types=91 exported=0
    N handle=144046 name='Runtime' full='System.Runtime' parent=235862 children=9 types=7 exported=0
      N handle=320844 name='DesignerServices' full='System.Runtime.DesignerServices' parent=144046 children=0 types=1 exported=0
      N handle=193088 name='Versioning' full='System.Runtime.Versioning' parent=144046 children=0 types=13 exported=0
      N handle=266618 name='ConstrainedExecution' full='System.Runtime.ConstrainedExecution' parent=144046 children=0 types=5 exported=0
      N handle=256249 name='Serialization' full='System.Runtime.Serialization' parent=144046 children=1 types=47 exported=0
        N handle=356870 name='Formatters' full='System.Runtime.Serialization.Formatters' parent=256249 children=1 types=11 exported=0
          N handle=425538 name='Binary' full='System.Runtime.Serialization.Formatters.Binary' parent=356870 children=0 types=64 exported=0
      N handle=320735 name='ExceptionServices' full='System.Runtime.ExceptionServices' parent=144046 children=0 types=3 exported=0
      N handle=198016 name='Remoting' full='System.Runtime.Remoting' parent=144046 children=8 types=37 exported=0
        N handle=66552 name='Metadata' full='System.Runtime.Remoting.Metadata' parent=198016 children=1 types=12 exported=0
          N handle=3249 name='W3cXsd2001' full='System.Runtime.Remoting.Metadata.W3cXsd2001' parent=66552 children=0 types=33 exported=0
        N handle=325048 name='Proxies' full='System.Runtime.Remoting.Proxies' parent=198016 children=0 types=8 exported=0
        N handle=320539 name='Services' full='System.Runtime.Remoting.Services' parent=198016 children=0 types=3 exported=0
        N handle=365257 name='Contexts' full='System.Runtime.Remoting.Contexts' parent=198016 children=0 types=21 exported=0
        N handle=143987 name='Lifetime' full='System.Runtime.Remoting.Lifetime' parent=198016 children=0 types=9 exported=0
        N handle=345499 name='Channels' full='System.Runtime.Remoting.Channels' parent=198016 children=0 types=47 exported=0
        N handle=191786 name='Messaging' full='System.Runtime.Remoting.Messaging' parent=198016 children=0 types=60 exported=0
        N handle=255969 name='Activation' full='System.Runtime.Remoting.Activation' parent=198016 children=0 types=15 exported=0
      N handle=320812 name='CompilerServices' full='System.Runtime.CompilerServices' parent=144046 children=0 types=95 exported=0
      N handle=320781 name='InteropServices' full='System.Runtime.InteropServices' parent=144046 children=4 types=194 exported=0
        N handle=240809 name='TCEAdapterGen' full='System.Runtime.InteropServices.TCEAdapterGen' parent=320781 children=0 types=5 exported=0
        N handle=144074 name='WindowsRuntime' full='System.Runtime.InteropServices.WindowsRuntime' parent=320781 children=0 types=92 exported=0
        N handle=270178 name='Expando' full='System.Runtime.InteropServices.Expando' parent=320781 children=0 types=1 exported=0
        N handle=331813 name='ComTypes' full='System.Runtime.InteropServices.ComTypes' parent=320781 children=0 types=50 exported=0
      N handle=198236 name='Hosting' full='System.Runtime.Hosting' parent=144046 children=0 types=3 exported=0
    N handle=402010 name='Text' full='System.Text' parent=235862 children=0 types=47 exported=0
HANDLES
H handle=0 name='' full='' parent=nil children=3 types=8 exported=0
H handle=9908 name='Win32' full='Microsoft.Win32' parent=2147483649 children=1 types=19 exported=0
H handle=325320 name='SafeHandles' full='Microsoft.Win32.SafeHandles' parent=9908 children=0 types=19 exported=0
H handle=198259 name='Hosting' full='Microsoft.Runtime.Hosting' parent=2147483650 children=0 types=3 exported=0
H handle=258136 name='Reflection' full='Microsoft.Reflection' parent=2147483649 children=0 types=1 exported=0
H handle=317634 name='Diagnostics' full='Windows.Foundation.Diagnostics' parent=2147483651 children=0 types=8 exported=0
H handle=235862 name='System' full='System' parent=nil children=14 types=312 exported=0
H handle=322500 name='Assemblies' full='System.Configuration.Assemblies' parent=2147483653 children=0 types=3 exported=0
H handle=50382 name='IO' full='System.IO' parent=235862 children=1 types=58 exported=0
H handle=114371 name='IsolatedStorage' full='System.IO.IsolatedStorage' parent=50382 children=0 types=13 exported=0
H handle=432186 name='Security' full='System.Security' parent=235862 children=7 types=60 exported=0
H handle=231296 name='AccessControl' full='System.Security.AccessControl' parent=432186 children=0 types=69 exported=0
H handle=420118 name='Cryptography' full='System.Security.Cryptography' parent=432186 children=1 types=107 exported=0
H handle=333221 name='X509Certificates' full='System.Security.Cryptography.X509Certificates' parent=420118 children=0 types=53 exported=0
H handle=348167 name='Permissions' full='System.Security.Permissions' parent=432186 children=0 types=64 exported=0
H handle=346543 name='Claims' full='System.Security.Claims' parent=432186 children=0 types=6 exported=0
H handle=224277 name='Principal' full='System.Security.Principal' parent=432186 children=0 types=29 exported=0
H handle=415761 name='Policy' full='System.Security.Policy' parent=432186 children=0 types=58 exported=0
H handle=228203 name='Util' full='System.Security.Util' parent=432186 children=0 types=16 exported=0
H handle=191928 name='Hashing' full='System.Numerics.Hashing' parent=2147483654 children=0 types=1 exported=0
H handle=321127 name='Resources' full='System.Resources' parent=235862 children=0 types=22 exported=0
H handle=256228 name='Globalization' full='System.Globalization' parent=235862 children=0 types=65 exported=0
H handle=317615 name='Diagnostics' full='System.Diagnostics' parent=235862 children=4 types=26 exported=0
H handle=156960 name='SymbolStore' full='System.Diagnostics.SymbolStore' parent=317615 children=0 types=15 exported=0
H handle=360194 name='Contracts' full='System.Diagnostics.Contracts' parent=317615 children=1 types=15 exported=0
H handle=221337 name='Internal' full='System.Diagnostics.Contracts.Internal' parent=360194 children=0 types=1 exported=0
H handle=343264 name='CodeAnalysis' full='System.Diagnostics.CodeAnalysis' parent=317615 children=0 types=1 exported=0
H handle=190506 name='Tracing' full='System.Diagnostics.Tracing' parent=317615 children=1 types=110 exported=0
H handle=221301 name='Internal' full='System.Diagnostics.Tracing.Internal' parent=190506 children=0 types=1 exported=0
H handle=349253 name='Collections' full='System.Collections' parent=235862 children=3 types=32 exported=0
H handle=387632 name='Concurrent' full='System.Collections.Concurrent' parent=349253 children=0 types=11 exported=0
H handle=226530 name='ObjectModel' full='System.Collections.ObjectModel' parent=349253 children=0 types=5 exported=0
H handle=69384 name='Generic' full='System.Collections.Generic' parent=349253 children=0 types=42 exported=0
H handle=190563 name='Threading' full='System.Threading' parent=235862 children=2 types=104 exported=0
H handle=344579 name='Tasks' full='System.Threading.Tasks' parent=190563 children=0 types=62 exported=0
H handle=156356 name='NetCore' full='System.Threading.NetCore' parent=190563 children=0 types=2 exported=0
H handle=355310 name='StubHelpers' full='System.StubHelpers' parent=235862 children=0 types=33 exported=0
H handle=258118 name='Reflection' full='System.Reflection' parent=235862 children=1 types=123 exported=0
H handle=378671 name='Emit' full='System.Reflection.Emit' parent=258118 children=0 types=68 exported=0
H handle=221375 name='Internal' full='System.Deployment.Internal' parent=2147483655 children=1 types=2 exported=0
H handle=252908 name='Isolation' full='System.Deployment.Internal.Isolation' parent=221375 children=1 types=75 exported=0
H handle=397644 name='Manifest' full='System.Deployment.Internal.Isolation.Manifest' parent=252908 children=0 types=91 exported=0
H handle=144046 name='Runtime' full='System.Runtime' parent=235862 children=9 types=7 exported=0
H handle=320844 name='DesignerServices' full='System.Runtime.DesignerServices' parent=144046 children=0 types=1 exported=0
H handle=193088 name='Versioning' full='System.Runtime.Versioning' parent=144046 children=0 types=13 exported=0
H handle=266618 name='ConstrainedExecution' full='System.Runtime.ConstrainedExecution' parent=144046 children=0 types=5 exported=0
H handle=256249 name='Serialization' full='System.Runtime.Serialization' parent=144046 children=1 types=47 exported=0
H handle=356870 name='Formatters' full='System.Runtime.Serialization.Formatters' parent=256249 children=1 types=11 exported=0
H handle=425538 name='Binary' full='System.Runtime.Serialization.Formatters.Binary' parent=356870 children=0 types=64 exported=0
H handle=320735 name='ExceptionServices' full='System.Runtime.ExceptionServices' parent=144046 children=0 types=3 exported=0
H handle=198016 name='Remoting' full='System.Runtime.Remoting' parent=144046 children=8 types=37 exported=0
H handle=66552 name='Metadata' full='System.Runtime.Remoting.Metadata' parent=198016 children=1 types=12 exported=0
H handle=3249 name='W3cXsd2001' full='System.Runtime.Remoting.Metadata.W3cXsd2001' parent=66552 children=0 types=33 exported=0
H handle=325048 name='Proxies' full='System.Runtime.Remoting.Proxies' parent=198016 children=0 types=8 exported=0
H handle=320539 name='Services' full='System.Runtime.Remoting.Services' parent=198016 children=0 types=3 exported=0
H handle=365257 name='Contexts' full='System.Runtime.Remoting.Contexts' parent=198016 children=0 types=21 exported=0
H handle=143987 name='Lifetime' full='System.Runtime.Remoting.Lifetime' parent=198016 children=0 types=9 exported=0
H handle=345499 name='Channels' full='System.Runtime.Remoting.Channels' parent=198016 children=0 types=47 exported=0
H handle=191786 name='Messaging' full='System.Runtime.Remoting.Messaging' parent=198016 children=0 types=60 exported=0
H handle=255969 name='Activation' full='System.Runtime.Remoting.Activation' parent=198016 children=0 types=15 exported=0
H handle=320812 name='CompilerServices' full='System.Runtime.CompilerServices' parent=144046 children=0 types=95 exported=0
H handle=320781 name='InteropServices' full='System.Runtime.InteropServices' parent=144046 children=4 types=194 exported=0
H handle=240809 name='TCEAdapterGen' full='System.Runtime.InteropServices.TCEAdapterGen' parent=320781 children=0 types=5 exported=0
H handle=144074 name='WindowsRuntime' full='System.Runtime.InteropServices.WindowsRuntime' parent=320781 children=0 types=92 exported=0
H handle=270178 name='Expando' full='System.Runtime.InteropServices.Expando' parent=320781 children=0 types=1 exported=0
H handle=331813 name='ComTypes' full='System.Runtime.InteropServices.ComTypes' parent=320781 children=0 types=50 exported=0
H handle=198236 name='Hosting' full='System.Runtime.Hosting' parent=144046 children=0 types=3 exported=0
H handle=402010 name='Text' full='System.Text' parent=235862 children=0 types=47 exported=0
H handle=2147483649 name='Microsoft' full='Microsoft' parent=nil children=3 types=0 exported=0
H handle=2147483650 name='Runtime' full='Microsoft.Runtime' parent=2147483649 children=1 types=0 exported=0
H handle=2147483651 name='Foundation' full='Windows.Foundation' parent=2147483652 children=1 types=0 exported=0
H handle=2147483652 name='Windows' full='Windows' parent=nil children=1 types=0 exported=0
H handle=2147483653 name='Configuration' full='System.Configuration' parent=235862 children=1 types=0 exported=0
H handle=2147483654 name='Numerics' full='System.Numerics' parent=235862 children=1 types=0 exported=0
H handle=2147483655 name='Deployment' full='System.Deployment' parent=235862 children=1 types=0 exported=0
)gold";;

// The probe gold for the System fixture.
inline constexpr const char* kGoldSystem =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=2 types=8 exported=0
  N handle=2147483649 name='Microsoft' full='Microsoft' parent=nil children=3 types=0 exported=0
    N handle=71257 name='VisualBasic' full='Microsoft.VisualBasic' parent=2147483649 children=0 types=5 exported=0
    N handle=278123 name='CSharp' full='Microsoft.CSharp' parent=2147483649 children=0 types=5 exported=0
    N handle=8464 name='Win32' full='Microsoft.Win32' parent=2147483649 children=1 types=26 exported=0
      N handle=318271 name='SafeHandles' full='Microsoft.Win32.SafeHandles' parent=8464 children=0 types=11 exported=0
  N handle=238111 name='System' full='System' parent=nil children=17 types=49 exported=0
    N handle=204012 name='Drawing' full='System.Drawing' parent=238111 children=0 types=1 exported=0
    N handle=69441 name='Web' full='System.Web' parent=238111 children=0 types=3 exported=0
    N handle=342167 name='Timers' full='System.Timers' parent=238111 children=0 types=4 exported=0
    N handle=256409 name='Configuration' full='System.Configuration' parent=238111 children=1 types=78 exported=0
      N handle=225260 name='Internal' full='System.Configuration.Internal' parent=256409 children=0 types=1 exported=0
    N handle=363020 name='Net' full='System.Net' parent=238111 children=8 types=364 exported=0
      N handle=349910 name='WebSockets' full='System.Net.WebSockets' parent=363020 children=0 types=19 exported=0
      N handle=146979 name='Mime' full='System.Net.Mime' parent=363020 children=0 types=23 exported=0
      N handle=229457 name='Mail' full='System.Net.Mail' parent=363020 children=0 types=71 exported=0
      N handle=254071 name='NetworkInformation' full='System.Net.NetworkInformation' parent=363020 children=0 types=110 exported=0
      N handle=124119 name='Cache' full='System.Net.Cache' parent=363020 children=0 types=25 exported=0
      N handle=256494 name='Configuration' full='System.Net.Configuration' parent=363020 children=0 types=46 exported=0
      N handle=425940 name='Security' full='System.Net.Security' parent=363020 children=0 types=20 exported=0
      N handle=349879 name='Sockets' full='System.Net.Sockets' parent=363020 children=0 types=61 exported=0
    N handle=2147483650 name='Windows' full='System.Windows' parent=238111 children=2 types=0 exported=0
      N handle=398417 name='Input' full='System.Windows.Input' parent=2147483650 children=0 types=2 exported=0
      N handle=278527 name='Markup' full='System.Windows.Markup' parent=2147483650 children=0 types=1 exported=0
    N handle=66233 name='Media' full='System.Media' parent=238111 children=0 types=3 exported=0
    N handle=2147483651 name='Collections' full='System.Collections' parent=238111 children=4 types=0 exported=0
      N handle=102082 name='Specialized' full='System.Collections.Specialized' parent=2147483651 children=0 types=19 exported=0
      N handle=227957 name='ObjectModel' full='System.Collections.ObjectModel' parent=2147483651 children=0 types=2 exported=0
      N handle=70876 name='Generic' full='System.Collections.Generic' parent=2147483651 children=0 types=20 exported=0
      N handle=380312 name='Concurrent' full='System.Collections.Concurrent' parent=2147483651 children=0 types=4 exported=0
    N handle=193577 name='Threading' full='System.Threading' parent=238111 children=0 types=5 exported=1
    N handle=2147483652 name='Runtime' full='System.Runtime' parent=238111 children=2 types=0 exported=0
      N handle=198200 name='Versioning' full='System.Runtime.Versioning' parent=2147483652 children=0 types=1 exported=0
      N handle=314055 name='InteropServices' full='System.Runtime.InteropServices' parent=2147483652 children=2 types=3 exported=0
        N handle=320045 name='ComTypes' full='System.Runtime.InteropServices.ComTypes' parent=314055 children=0 types=11 exported=0
        N handle=148899 name='WindowsRuntime' full='System.Runtime.InteropServices.WindowsRuntime' parent=314055 children=0 types=16 exported=0
    N handle=259481 name='Reflection' full='System.Reflection' parent=238111 children=0 types=1 exported=0
    N handle=50009 name='IO' full='System.IO' parent=238111 children=2 types=15 exported=0
      N handle=354424 name='Ports' full='System.IO.Ports' parent=50009 children=0 types=15 exported=0
      N handle=247394 name='Compression' full='System.IO.Compression' parent=50009 children=0 types=33 exported=0
    N handle=425924 name='Security' full='System.Security' parent=238111 children=5 types=1 exported=0
      N handle=333856 name='Claims' full='System.Security.Claims' parent=425924 children=0 types=1 exported=0
      N handle=251882 name='Authentication' full='System.Security.Authentication' parent=425924 children=1 types=6 exported=0
        N handle=263879 name='ExtendedProtection' full='System.Security.Authentication.ExtendedProtection' parent=251882 children=1 types=9 exported=0
          N handle=256430 name='Configuration' full='System.Security.Authentication.ExtendedProtection.Configuration' parent=263879 children=0 types=4 exported=0
      N handle=416062 name='Cryptography' full='System.Security.Cryptography' parent=425924 children=1 types=20 exported=0
        N handle=321639 name='X509Certificates' full='System.Security.Cryptography.X509Certificates' parent=416062 children=0 types=35 exported=0
      N handle=335319 name='Permissions' full='System.Security.Permissions' parent=425924 children=0 types=8 exported=0
      N handle=233023 name='AccessControl' full='System.Security.AccessControl' parent=425924 children=0 types=4 exported=0
    N handle=312052 name='Diagnostics' full='System.Diagnostics' parent=238111 children=1 types=123 exported=0
      N handle=331001 name='CodeAnalysis' full='System.Diagnostics.CodeAnalysis' parent=312052 children=0 types=1 exported=0
    N handle=227988 name='ComponentModel' full='System.ComponentModel' parent=238111 children=1 types=187 exported=0
      N handle=243382 name='Design' full='System.ComponentModel.Design' parent=227988 children=1 types=60 exported=0
        N handle=257874 name='Serialization' full='System.ComponentModel.Design.Serialization' parent=243382 children=0 types=19 exported=0
    N handle=238971 name='CodeDom' full='System.CodeDom' parent=238111 children=1 types=86 exported=0
      N handle=293369 name='Compiler' full='System.CodeDom.Compiler' parent=238971 children=0 types=26 exported=0
    N handle=2147483653 name='Text' full='System.Text' parent=238111 children=1 types=0 exported=0
      N handle=335220 name='RegularExpressions' full='System.Text.RegularExpressions' parent=2147483653 children=0 types=40 exported=0
HANDLES
H handle=0 name='' full='' parent=nil children=2 types=8 exported=0
H handle=71257 name='VisualBasic' full='Microsoft.VisualBasic' parent=2147483649 children=0 types=5 exported=0
H handle=278123 name='CSharp' full='Microsoft.CSharp' parent=2147483649 children=0 types=5 exported=0
H handle=8464 name='Win32' full='Microsoft.Win32' parent=2147483649 children=1 types=26 exported=0
H handle=318271 name='SafeHandles' full='Microsoft.Win32.SafeHandles' parent=8464 children=0 types=11 exported=0
H handle=238111 name='System' full='System' parent=nil children=17 types=49 exported=0
H handle=204012 name='Drawing' full='System.Drawing' parent=238111 children=0 types=1 exported=0
H handle=69441 name='Web' full='System.Web' parent=238111 children=0 types=3 exported=0
H handle=342167 name='Timers' full='System.Timers' parent=238111 children=0 types=4 exported=0
H handle=256409 name='Configuration' full='System.Configuration' parent=238111 children=1 types=78 exported=0
H handle=225260 name='Internal' full='System.Configuration.Internal' parent=256409 children=0 types=1 exported=0
H handle=363020 name='Net' full='System.Net' parent=238111 children=8 types=364 exported=0
H handle=349910 name='WebSockets' full='System.Net.WebSockets' parent=363020 children=0 types=19 exported=0
H handle=146979 name='Mime' full='System.Net.Mime' parent=363020 children=0 types=23 exported=0
H handle=229457 name='Mail' full='System.Net.Mail' parent=363020 children=0 types=71 exported=0
H handle=254071 name='NetworkInformation' full='System.Net.NetworkInformation' parent=363020 children=0 types=110 exported=0
H handle=124119 name='Cache' full='System.Net.Cache' parent=363020 children=0 types=25 exported=0
H handle=256494 name='Configuration' full='System.Net.Configuration' parent=363020 children=0 types=46 exported=0
H handle=425940 name='Security' full='System.Net.Security' parent=363020 children=0 types=20 exported=0
H handle=349879 name='Sockets' full='System.Net.Sockets' parent=363020 children=0 types=61 exported=0
H handle=398417 name='Input' full='System.Windows.Input' parent=2147483650 children=0 types=2 exported=0
H handle=278527 name='Markup' full='System.Windows.Markup' parent=2147483650 children=0 types=1 exported=0
H handle=66233 name='Media' full='System.Media' parent=238111 children=0 types=3 exported=0
H handle=102082 name='Specialized' full='System.Collections.Specialized' parent=2147483651 children=0 types=19 exported=0
H handle=227957 name='ObjectModel' full='System.Collections.ObjectModel' parent=2147483651 children=0 types=2 exported=0
H handle=70876 name='Generic' full='System.Collections.Generic' parent=2147483651 children=0 types=20 exported=0
H handle=380312 name='Concurrent' full='System.Collections.Concurrent' parent=2147483651 children=0 types=4 exported=0
H handle=193577 name='Threading' full='System.Threading' parent=238111 children=0 types=5 exported=1
H handle=198200 name='Versioning' full='System.Runtime.Versioning' parent=2147483652 children=0 types=1 exported=0
H handle=314055 name='InteropServices' full='System.Runtime.InteropServices' parent=2147483652 children=2 types=3 exported=0
H handle=320045 name='ComTypes' full='System.Runtime.InteropServices.ComTypes' parent=314055 children=0 types=11 exported=0
H handle=148899 name='WindowsRuntime' full='System.Runtime.InteropServices.WindowsRuntime' parent=314055 children=0 types=16 exported=0
H handle=259481 name='Reflection' full='System.Reflection' parent=238111 children=0 types=1 exported=0
H handle=50009 name='IO' full='System.IO' parent=238111 children=2 types=15 exported=0
H handle=354424 name='Ports' full='System.IO.Ports' parent=50009 children=0 types=15 exported=0
H handle=247394 name='Compression' full='System.IO.Compression' parent=50009 children=0 types=33 exported=0
H handle=425924 name='Security' full='System.Security' parent=238111 children=5 types=1 exported=0
H handle=333856 name='Claims' full='System.Security.Claims' parent=425924 children=0 types=1 exported=0
H handle=251882 name='Authentication' full='System.Security.Authentication' parent=425924 children=1 types=6 exported=0
H handle=263879 name='ExtendedProtection' full='System.Security.Authentication.ExtendedProtection' parent=251882 children=1 types=9 exported=0
H handle=256430 name='Configuration' full='System.Security.Authentication.ExtendedProtection.Configuration' parent=263879 children=0 types=4 exported=0
H handle=416062 name='Cryptography' full='System.Security.Cryptography' parent=425924 children=1 types=20 exported=0
H handle=321639 name='X509Certificates' full='System.Security.Cryptography.X509Certificates' parent=416062 children=0 types=35 exported=0
H handle=335319 name='Permissions' full='System.Security.Permissions' parent=425924 children=0 types=8 exported=0
H handle=233023 name='AccessControl' full='System.Security.AccessControl' parent=425924 children=0 types=4 exported=0
H handle=312052 name='Diagnostics' full='System.Diagnostics' parent=238111 children=1 types=123 exported=0
H handle=331001 name='CodeAnalysis' full='System.Diagnostics.CodeAnalysis' parent=312052 children=0 types=1 exported=0
H handle=227988 name='ComponentModel' full='System.ComponentModel' parent=238111 children=1 types=187 exported=0
H handle=243382 name='Design' full='System.ComponentModel.Design' parent=227988 children=1 types=60 exported=0
H handle=257874 name='Serialization' full='System.ComponentModel.Design.Serialization' parent=243382 children=0 types=19 exported=0
H handle=238971 name='CodeDom' full='System.CodeDom' parent=238111 children=1 types=86 exported=0
H handle=293369 name='Compiler' full='System.CodeDom.Compiler' parent=238971 children=0 types=26 exported=0
H handle=335220 name='RegularExpressions' full='System.Text.RegularExpressions' parent=2147483653 children=0 types=40 exported=0
H handle=2147483649 name='Microsoft' full='Microsoft' parent=nil children=3 types=0 exported=0
H handle=2147483650 name='Windows' full='System.Windows' parent=238111 children=2 types=0 exported=0
H handle=2147483651 name='Collections' full='System.Collections' parent=238111 children=4 types=0 exported=0
H handle=2147483652 name='Runtime' full='System.Runtime' parent=238111 children=2 types=0 exported=0
H handle=2147483653 name='Text' full='System.Text' parent=238111 children=1 types=0 exported=0
)gold";;

// The probe gold for the CoreLib fixture.
inline constexpr const char* kGoldCorelib =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=3 types=8 exported=0
  N handle=2147483649 name='Microsoft' full='Microsoft' parent=nil children=1 types=0 exported=0
    N handle=36938 name='Win32' full='Microsoft.Win32' parent=2147483649 children=1 types=1 exported=0
      N handle=458852 name='SafeHandles' full='Microsoft.Win32.SafeHandles' parent=36938 children=0 types=11 exported=0
  N handle=329484 name='System' full='System' parent=nil children=18 types=332 exported=0
    N handle=2147483650 name='Private' full='System.Private' parent=329484 children=1 types=0 exported=0
      N handle=89800 name='CoreLib' full='System.Private.CoreLib' parent=2147483650 children=0 types=1 exported=0
    N handle=601146 name='Security' full='System.Security' parent=329484 children=3 types=19 exported=0
      N handle=311883 name='Principal' full='System.Security.Principal' parent=601146 children=0 types=4 exported=0
      N handle=478443 name='Permissions' full='System.Security.Permissions' parent=601146 children=0 types=6 exported=0
      N handle=581242 name='Cryptography' full='System.Security.Cryptography' parent=601146 children=0 types=1 exported=0
    N handle=456240 name='Resources' full='System.Resources' parent=329484 children=0 types=17 exported=0
    N handle=451926 name='Numerics' full='System.Numerics' parent=329484 children=0 types=45 exported=0
    N handle=507772 name='Net' full='System.Net' parent=329484 children=0 types=1 exported=0
    N handle=363428 name='Globalization' full='System.Globalization' parent=329484 children=0 types=68 exported=0
    N handle=2147483651 name='Configuration' full='System.Configuration' parent=329484 children=1 types=0 exported=0
      N handle=457388 name='Assemblies' full='System.Configuration.Assemblies' parent=2147483651 children=0 types=2 exported=0
    N handle=319066 name='ComponentModel' full='System.ComponentModel' parent=329484 children=0 types=4 exported=0
    N handle=2147483652 name='CodeDom' full='System.CodeDom' parent=329484 children=1 types=0 exported=0
      N handle=428938 name='Compiler' full='System.CodeDom.Compiler' parent=2147483652 children=0 types=2 exported=0
    N handle=484167 name='Buffers' full='System.Buffers' parent=329484 children=2 types=64 exported=0
      N handle=563819 name='Text' full='System.Buffers.Text' parent=484167 children=0 types=7 exported=0
      N handle=590766 name='Binary' full='System.Buffers.Binary' parent=484167 children=0 types=1 exported=0
    N handle=265183 name='Threading' full='System.Threading' parent=329484 children=1 types=104 exported=0
      N handle=474906 name='Tasks' full='System.Threading.Tasks' parent=265183 children=1 types=46 exported=0
        N handle=455955 name='Sources' full='System.Threading.Tasks.Sources' parent=474906 children=0 types=7 exported=0
    N handle=563807 name='Text' full='System.Text' parent=329484 children=1 types=45 exported=0
      N handle=156705 name='Unicode' full='System.Text.Unicode' parent=563807 children=0 types=5 exported=0
    N handle=485294 name='StubHelpers' full='System.StubHelpers' parent=329484 children=0 types=21 exported=0
    N handle=189277 name='Runtime' full='System.Runtime' parent=329484 children=9 types=18 exported=0
      N handle=268572 name='Versioning' full='System.Runtime.Versioning' parent=189277 children=0 types=18 exported=0
      N handle=363449 name='Serialization' full='System.Runtime.Serialization' parent=189277 children=0 types=19 exported=0
      N handle=278154 name='Remoting' full='System.Runtime.Remoting' parent=189277 children=0 types=1 exported=0
      N handle=379225 name='ConstrainedExecution' full='System.Runtime.ConstrainedExecution' parent=189277 children=0 types=5 exported=0
      N handle=418121 name='Loader' full='System.Runtime.Loader' parent=189277 children=0 types=5 exported=0
      N handle=452008 name='Intrinsics' full='System.Runtime.Intrinsics' parent=189277 children=3 types=16 exported=0
        N handle=330512 name='Wasm' full='System.Runtime.Intrinsics.Wasm' parent=452008 children=0 types=1 exported=0
        N handle=330153 name='Arm' full='System.Runtime.Intrinsics.Arm' parent=452008 children=0 types=12 exported=0
        N handle=58495 name='X86' full='System.Runtime.Intrinsics.X86' parent=452008 children=0 types=31 exported=0
      N handle=455575 name='InteropServices' full='System.Runtime.InteropServices' parent=189277 children=6 types=115 exported=0
        N handle=70185 name='ObjectiveC' full='System.Runtime.InteropServices.ObjectiveC' parent=455575 children=0 types=2 exported=0
        N handle=520078 name='Swift' full='System.Runtime.InteropServices.Swift' parent=455575 children=0 types=4 exported=0
        N handle=267844 name='Marshalling' full='System.Runtime.InteropServices.Marshalling' parent=455575 children=0 types=15 exported=0
        N handle=89618 name='Java' full='System.Runtime.InteropServices.Java' parent=455575 children=0 types=4 exported=0
        N handle=462923 name='ComTypes' full='System.Runtime.InteropServices.ComTypes' parent=455575 children=0 types=46 exported=0
        N handle=484862 name='CustomMarshalers' full='System.Runtime.InteropServices.CustomMarshalers' parent=455575 children=0 types=6 exported=0
      N handle=455509 name='ExceptionServices' full='System.Runtime.ExceptionServices' parent=189277 children=0 types=5 exported=0
      N handle=455606 name='CompilerServices' full='System.Runtime.CompilerServices' parent=189277 children=0 types=170 exported=0
    N handle=366867 name='Reflection' full='System.Reflection' parent=329484 children=2 types=153 exported=0
      N handle=89037 name='Metadata' full='System.Reflection.Metadata' parent=366867 children=0 types=9 exported=0
      N handle=522022 name='Emit' full='System.Reflection.Emit' parent=366867 children=0 types=64 exported=0
    N handle=80179 name='IO' full='System.IO' parent=329484 children=2 types=53 exported=0
      N handle=457355 name='Strategies' full='System.IO.Strategies' parent=80179 children=0 types=7 exported=0
      N handle=360750 name='Enumeration' full='System.IO.Enumeration' parent=80179 children=0 types=5 exported=0
    N handle=452292 name='Diagnostics' full='System.Diagnostics' parent=329484 children=4 types=25 exported=0
      N handle=265131 name='Tracing' full='System.Diagnostics.Tracing' parent=452292 children=0 types=95 exported=0
      N handle=207791 name='SymbolStore' full='System.Diagnostics.SymbolStore' parent=452292 children=0 types=1 exported=0
      N handle=490220 name='Contracts' full='System.Diagnostics.Contracts' parent=452292 children=0 types=15 exported=0
      N handle=473485 name='CodeAnalysis' full='System.Diagnostics.CodeAnalysis' parent=452292 children=0 types=27 exported=0
    N handle=478992 name='Collections' full='System.Collections' parent=329484 children=3 types=20 exported=0
      N handle=319035 name='ObjectModel' full='System.Collections.ObjectModel' parent=478992 children=0 types=6 exported=0
      N handle=542148 name='Concurrent' full='System.Collections.Concurrent' parent=478992 children=0 types=8 exported=0
      N handle=91182 name='Generic' full='System.Collections.Generic' parent=478992 children=0 types=61 exported=0
  N handle=311788 name='Internal' full='Internal' parent=nil children=2 types=4 exported=0
    N handle=36923 name='Win32' full='Internal.Win32' parent=311788 children=1 types=2 exported=0
      N handle=458825 name='SafeHandles' full='Internal.Win32.SafeHandles' parent=36923 children=0 types=1 exported=0
    N handle=2147483653 name='Runtime' full='Internal.Runtime' parent=311788 children=2 types=0 exported=0
      N handle=455542 name='InteropServices' full='Internal.Runtime.InteropServices' parent=2147483653 children=0 types=10 exported=0
      N handle=485544 name='CompilerHelpers' full='Internal.Runtime.CompilerHelpers' parent=2147483653 children=0 types=1 exported=0
HANDLES
H handle=0 name='' full='' parent=nil children=3 types=8 exported=0
H handle=36938 name='Win32' full='Microsoft.Win32' parent=2147483649 children=1 types=1 exported=0
H handle=458852 name='SafeHandles' full='Microsoft.Win32.SafeHandles' parent=36938 children=0 types=11 exported=0
H handle=329484 name='System' full='System' parent=nil children=18 types=332 exported=0
H handle=89800 name='CoreLib' full='System.Private.CoreLib' parent=2147483650 children=0 types=1 exported=0
H handle=601146 name='Security' full='System.Security' parent=329484 children=3 types=19 exported=0
H handle=311883 name='Principal' full='System.Security.Principal' parent=601146 children=0 types=4 exported=0
H handle=478443 name='Permissions' full='System.Security.Permissions' parent=601146 children=0 types=6 exported=0
H handle=581242 name='Cryptography' full='System.Security.Cryptography' parent=601146 children=0 types=1 exported=0
H handle=456240 name='Resources' full='System.Resources' parent=329484 children=0 types=17 exported=0
H handle=451926 name='Numerics' full='System.Numerics' parent=329484 children=0 types=45 exported=0
H handle=507772 name='Net' full='System.Net' parent=329484 children=0 types=1 exported=0
H handle=363428 name='Globalization' full='System.Globalization' parent=329484 children=0 types=68 exported=0
H handle=457388 name='Assemblies' full='System.Configuration.Assemblies' parent=2147483651 children=0 types=2 exported=0
H handle=319066 name='ComponentModel' full='System.ComponentModel' parent=329484 children=0 types=4 exported=0
H handle=428938 name='Compiler' full='System.CodeDom.Compiler' parent=2147483652 children=0 types=2 exported=0
H handle=484167 name='Buffers' full='System.Buffers' parent=329484 children=2 types=64 exported=0
H handle=563819 name='Text' full='System.Buffers.Text' parent=484167 children=0 types=7 exported=0
H handle=590766 name='Binary' full='System.Buffers.Binary' parent=484167 children=0 types=1 exported=0
H handle=265183 name='Threading' full='System.Threading' parent=329484 children=1 types=104 exported=0
H handle=474906 name='Tasks' full='System.Threading.Tasks' parent=265183 children=1 types=46 exported=0
H handle=455955 name='Sources' full='System.Threading.Tasks.Sources' parent=474906 children=0 types=7 exported=0
H handle=563807 name='Text' full='System.Text' parent=329484 children=1 types=45 exported=0
H handle=156705 name='Unicode' full='System.Text.Unicode' parent=563807 children=0 types=5 exported=0
H handle=485294 name='StubHelpers' full='System.StubHelpers' parent=329484 children=0 types=21 exported=0
H handle=189277 name='Runtime' full='System.Runtime' parent=329484 children=9 types=18 exported=0
H handle=268572 name='Versioning' full='System.Runtime.Versioning' parent=189277 children=0 types=18 exported=0
H handle=363449 name='Serialization' full='System.Runtime.Serialization' parent=189277 children=0 types=19 exported=0
H handle=278154 name='Remoting' full='System.Runtime.Remoting' parent=189277 children=0 types=1 exported=0
H handle=379225 name='ConstrainedExecution' full='System.Runtime.ConstrainedExecution' parent=189277 children=0 types=5 exported=0
H handle=418121 name='Loader' full='System.Runtime.Loader' parent=189277 children=0 types=5 exported=0
H handle=452008 name='Intrinsics' full='System.Runtime.Intrinsics' parent=189277 children=3 types=16 exported=0
H handle=330512 name='Wasm' full='System.Runtime.Intrinsics.Wasm' parent=452008 children=0 types=1 exported=0
H handle=330153 name='Arm' full='System.Runtime.Intrinsics.Arm' parent=452008 children=0 types=12 exported=0
H handle=58495 name='X86' full='System.Runtime.Intrinsics.X86' parent=452008 children=0 types=31 exported=0
H handle=455575 name='InteropServices' full='System.Runtime.InteropServices' parent=189277 children=6 types=115 exported=0
H handle=70185 name='ObjectiveC' full='System.Runtime.InteropServices.ObjectiveC' parent=455575 children=0 types=2 exported=0
H handle=520078 name='Swift' full='System.Runtime.InteropServices.Swift' parent=455575 children=0 types=4 exported=0
H handle=267844 name='Marshalling' full='System.Runtime.InteropServices.Marshalling' parent=455575 children=0 types=15 exported=0
H handle=89618 name='Java' full='System.Runtime.InteropServices.Java' parent=455575 children=0 types=4 exported=0
H handle=462923 name='ComTypes' full='System.Runtime.InteropServices.ComTypes' parent=455575 children=0 types=46 exported=0
H handle=484862 name='CustomMarshalers' full='System.Runtime.InteropServices.CustomMarshalers' parent=455575 children=0 types=6 exported=0
H handle=455509 name='ExceptionServices' full='System.Runtime.ExceptionServices' parent=189277 children=0 types=5 exported=0
H handle=455606 name='CompilerServices' full='System.Runtime.CompilerServices' parent=189277 children=0 types=170 exported=0
H handle=366867 name='Reflection' full='System.Reflection' parent=329484 children=2 types=153 exported=0
H handle=89037 name='Metadata' full='System.Reflection.Metadata' parent=366867 children=0 types=9 exported=0
H handle=522022 name='Emit' full='System.Reflection.Emit' parent=366867 children=0 types=64 exported=0
H handle=80179 name='IO' full='System.IO' parent=329484 children=2 types=53 exported=0
H handle=457355 name='Strategies' full='System.IO.Strategies' parent=80179 children=0 types=7 exported=0
H handle=360750 name='Enumeration' full='System.IO.Enumeration' parent=80179 children=0 types=5 exported=0
H handle=452292 name='Diagnostics' full='System.Diagnostics' parent=329484 children=4 types=25 exported=0
H handle=265131 name='Tracing' full='System.Diagnostics.Tracing' parent=452292 children=0 types=95 exported=0
H handle=207791 name='SymbolStore' full='System.Diagnostics.SymbolStore' parent=452292 children=0 types=1 exported=0
H handle=490220 name='Contracts' full='System.Diagnostics.Contracts' parent=452292 children=0 types=15 exported=0
H handle=473485 name='CodeAnalysis' full='System.Diagnostics.CodeAnalysis' parent=452292 children=0 types=27 exported=0
H handle=478992 name='Collections' full='System.Collections' parent=329484 children=3 types=20 exported=0
H handle=319035 name='ObjectModel' full='System.Collections.ObjectModel' parent=478992 children=0 types=6 exported=0
H handle=542148 name='Concurrent' full='System.Collections.Concurrent' parent=478992 children=0 types=8 exported=0
H handle=91182 name='Generic' full='System.Collections.Generic' parent=478992 children=0 types=61 exported=0
H handle=311788 name='Internal' full='Internal' parent=nil children=2 types=4 exported=0
H handle=36923 name='Win32' full='Internal.Win32' parent=311788 children=1 types=2 exported=0
H handle=458825 name='SafeHandles' full='Internal.Win32.SafeHandles' parent=36923 children=0 types=1 exported=0
H handle=455542 name='InteropServices' full='Internal.Runtime.InteropServices' parent=2147483653 children=0 types=10 exported=0
H handle=485544 name='CompilerHelpers' full='Internal.Runtime.CompilerHelpers' parent=2147483653 children=0 types=1 exported=0
H handle=2147483649 name='Microsoft' full='Microsoft' parent=nil children=1 types=0 exported=0
H handle=2147483650 name='Private' full='System.Private' parent=329484 children=1 types=0 exported=0
H handle=2147483651 name='Configuration' full='System.Configuration' parent=329484 children=1 types=0 exported=0
H handle=2147483652 name='CodeDom' full='System.CodeDom' parent=329484 children=1 types=0 exported=0
H handle=2147483653 name='Runtime' full='Internal.Runtime' parent=311788 children=2 types=0 exported=0
)gold";;

// The probe gold for the facade fixture.
inline constexpr const char* kGoldFacade =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=1 types=1 exported=0
T 0x02000001
  N handle=3072 name='System' full='System' parent=nil children=10 types=0 exported=161
  E 0x27000001
  E 0x27000002
  E 0x27000003
  E 0x27000004
  E 0x27000005
  E 0x27000006
  E 0x27000007
  E 0x27000008
  E 0x27000009
  E 0x2700000a
  E 0x2700000b
  E 0x2700000c
  E 0x2700000d
  E 0x2700000e
  E 0x2700000f
  E 0x27000010
  E 0x27000011
  E 0x27000012
  E 0x27000013
  E 0x27000014
  E 0x27000015
  E 0x27000016
  E 0x27000017
  E 0x27000018
  E 0x27000019
  E 0x2700001a
  E 0x2700001b
  E 0x2700001c
  E 0x2700001d
  E 0x2700001e
  E 0x2700001f
  E 0x27000020
  E 0x27000021
  E 0x27000022
  E 0x27000023
  E 0x2700003e
  E 0x27000042
  E 0x27000043
  E 0x27000044
  E 0x27000045
  E 0x27000046
  E 0x27000047
  E 0x2700004b
  E 0x2700004c
  E 0x2700004d
  E 0x2700004e
  E 0x2700004f
  E 0x27000050
  E 0x27000051
  E 0x27000052
  E 0x27000053
  E 0x27000054
  E 0x27000055
  E 0x27000056
  E 0x27000057
  E 0x27000058
  E 0x27000059
  E 0x2700005a
  E 0x2700005b
  E 0x2700005c
  E 0x2700005d
  E 0x2700005e
  E 0x2700005f
  E 0x27000060
  E 0x27000061
  E 0x27000062
  E 0x27000063
  E 0x27000064
  E 0x27000065
  E 0x27000066
  E 0x27000067
  E 0x27000068
  E 0x2700006c
  E 0x2700006d
  E 0x2700006e
  E 0x2700006f
  E 0x27000070
  E 0x27000071
  E 0x27000072
  E 0x27000073
  E 0x27000074
  E 0x27000075
  E 0x27000076
  E 0x27000077
  E 0x27000078
  E 0x27000079
  E 0x2700007a
  E 0x2700007b
  E 0x2700007c
  E 0x2700007d
  E 0x2700007e
  E 0x2700007f
  E 0x27000085
  E 0x27000086
  E 0x27000087
  E 0x27000088
  E 0x27000089
  E 0x2700008a
  E 0x2700008b
  E 0x2700008c
  E 0x2700008d
  E 0x2700008e
  E 0x2700008f
  E 0x27000090
  E 0x27000091
  E 0x27000092
  E 0x27000093
  E 0x27000094
  E 0x27000095
  E 0x27000096
  E 0x27000097
  E 0x27000098
  E 0x27000099
  E 0x2700009a
  E 0x2700009b
  E 0x2700009c
  E 0x2700009d
  E 0x2700009e
  E 0x270000e1
  E 0x270000e2
  E 0x270000e3
  E 0x270000e4
  E 0x270000eb
  E 0x270000ec
  E 0x270000ed
  E 0x270000ee
  E 0x270000ef
  E 0x270000f4
  E 0x270000f5
  E 0x270000f6
  E 0x270000f7
  E 0x270000fa
  E 0x270000fb
  E 0x270000fc
  E 0x270000fd
  E 0x270000fe
  E 0x270000ff
  E 0x27000100
  E 0x27000101
  E 0x27000102
  E 0x27000103
  E 0x27000104
  E 0x27000105
  E 0x27000106
  E 0x27000107
  E 0x27000108
  E 0x27000109
  E 0x2700010a
  E 0x2700010b
  E 0x2700010c
  E 0x2700010d
  E 0x2700010e
  E 0x2700010f
  E 0x27000110
  E 0x27000111
  E 0x27000112
  E 0x27000113
  E 0x27000114
  E 0x27000115
  E 0x27000116
  E 0x27000117
    N handle=4519 name='Collections' full='System.Collections' parent=3072 children=2 types=0 exported=11
    E 0x27000024
    E 0x27000032
    E 0x27000033
    E 0x27000034
    E 0x27000035
    E 0x27000036
    E 0x27000037
    E 0x27000038
    E 0x27000039
    E 0x2700003a
    E 0x2700003b
      N handle=769 name='Generic' full='System.Collections.Generic' parent=4519 children=0 types=0 exported=13
      E 0x27000025
      E 0x27000026
      E 0x27000027
      E 0x27000028
      E 0x27000029
      E 0x2700002a
      E 0x2700002b
      E 0x2700002c
      E 0x2700002d
      E 0x2700002e
      E 0x2700002f
      E 0x27000030
      E 0x27000031
      N handle=3000 name='ObjectModel' full='System.Collections.ObjectModel' parent=4519 children=0 types=0 exported=2
      E 0x2700003c
      E 0x2700003d
    N handle=3031 name='ComponentModel' full='System.ComponentModel' parent=3072 children=0 types=0 exported=3
    E 0x2700003f
    E 0x27000040
    E 0x27000041
    N handle=4307 name='Diagnostics' full='System.Diagnostics' parent=3072 children=0 types=0 exported=2
    E 0x27000048
    E 0x27000049
    N handle=3109 name='Globalization' full='System.Globalization' parent=3072 children=0 types=0 exported=3
    E 0x27000069
    E 0x2700006a
    E 0x2700006b
    N handle=750 name='IO' full='System.IO' parent=3072 children=0 types=0 exported=5
    E 0x27000080
    E 0x27000081
    E 0x27000082
    E 0x27000083
    E 0x27000084
    N handle=3137 name='Reflection' full='System.Reflection' parent=3072 children=0 types=0 exported=21
    E 0x2700009f
    E 0x270000a0
    E 0x270000a1
    E 0x270000a2
    E 0x270000a3
    E 0x270000a4
    E 0x270000a5
    E 0x270000a6
    E 0x270000a7
    E 0x270000a8
    E 0x270000a9
    E 0x270000aa
    E 0x270000ab
    E 0x270000ac
    E 0x270000ad
    E 0x270000ae
    E 0x270000af
    E 0x270000b0
    E 0x270000b1
    E 0x270000b2
    E 0x270000b3
    N handle=1217 name='Runtime' full='System.Runtime' parent=3072 children=4 types=0 exported=3
    E 0x270000d7
    E 0x270000d8
    E 0x270000d9
      N handle=4390 name='CompilerServices' full='System.Runtime.CompilerServices' parent=1217 children=0 types=0 exported=31
      E 0x270000b4
      E 0x270000b5
      E 0x270000b6
      E 0x270000b7
      E 0x270000b8
      E 0x270000b9
      E 0x270000ba
      E 0x270000bb
      E 0x270000bd
      E 0x270000be
      E 0x270000bf
      E 0x270000c0
      E 0x270000c1
      E 0x270000c2
      E 0x270000c3
      E 0x270000c4
      E 0x270000c5
      E 0x270000c6
      E 0x270000c7
      E 0x270000c8
      E 0x270000c9
      E 0x270000ca
      E 0x270000cb
      E 0x270000cc
      E 0x270000cd
      E 0x270000ce
      E 0x270000d1
      E 0x270000d2
      E 0x270000d3
      E 0x270000d4
      E 0x270000d5
      N handle=4326 name='ExceptionServices' full='System.Runtime.ExceptionServices' parent=1217 children=0 types=0 exported=1
      E 0x270000d6
      N handle=4359 name='InteropServices' full='System.Runtime.InteropServices' parent=1217 children=0 types=0 exported=6
      E 0x270000da
      E 0x270000db
      E 0x270000dc
      E 0x270000dd
      E 0x270000de
      E 0x270000df
      N handle=2900 name='Versioning' full='System.Runtime.Versioning' parent=1217 children=0 types=0 exported=1
      E 0x270000e0
    N handle=4778 name='Security' full='System.Security' parent=3072 children=0 types=0 exported=6
    E 0x270000e5
    E 0x270000e6
    E 0x270000e7
    E 0x270000e8
    E 0x270000e9
    E 0x270000ea
    N handle=4696 name='Text' full='System.Text' parent=3072 children=0 types=0 exported=1
    E 0x270000f0
    N handle=2883 name='Threading' full='System.Threading' parent=3072 children=0 types=0 exported=3
    E 0x270000f1
    E 0x270000f2
    E 0x270000f3
HANDLES
H handle=0 name='' full='' parent=nil children=1 types=1 exported=0
H handle=3072 name='System' full='System' parent=nil children=10 types=0 exported=161
H handle=4519 name='Collections' full='System.Collections' parent=3072 children=2 types=0 exported=11
H handle=769 name='Generic' full='System.Collections.Generic' parent=4519 children=0 types=0 exported=13
H handle=3000 name='ObjectModel' full='System.Collections.ObjectModel' parent=4519 children=0 types=0 exported=2
H handle=3031 name='ComponentModel' full='System.ComponentModel' parent=3072 children=0 types=0 exported=3
)gold"
R"gold(H handle=4307 name='Diagnostics' full='System.Diagnostics' parent=3072 children=0 types=0 exported=2
H handle=3109 name='Globalization' full='System.Globalization' parent=3072 children=0 types=0 exported=3
H handle=750 name='IO' full='System.IO' parent=3072 children=0 types=0 exported=5
H handle=3137 name='Reflection' full='System.Reflection' parent=3072 children=0 types=0 exported=21
H handle=4390 name='CompilerServices' full='System.Runtime.CompilerServices' parent=1217 children=0 types=0 exported=31
H handle=4326 name='ExceptionServices' full='System.Runtime.ExceptionServices' parent=1217 children=0 types=0 exported=1
H handle=1217 name='Runtime' full='System.Runtime' parent=3072 children=4 types=0 exported=3
H handle=4359 name='InteropServices' full='System.Runtime.InteropServices' parent=1217 children=0 types=0 exported=6
H handle=2900 name='Versioning' full='System.Runtime.Versioning' parent=1217 children=0 types=0 exported=1
H handle=4778 name='Security' full='System.Security' parent=3072 children=0 types=0 exported=6
H handle=4696 name='Text' full='System.Text' parent=3072 children=0 types=0 exported=1
H handle=2883 name='Threading' full='System.Threading' parent=3072 children=0 types=0 exported=3
)gold";;

// The probe gold for the tiny fixture.
inline constexpr const char* kGoldTiny =
R"gold(TREE
N handle=0 name='' full='' parent=nil children=0 types=2 exported=0
T 0x02000001
T 0x02000002
HANDLES
H handle=0 name='' full='' parent=nil children=0 types=2 exported=0
)gold";;

// The probe gold for the throws fixture.
inline constexpr const char* kGoldThrows =
R"gold(THROW System.BadImageFormatException: Invalid handle.
NIL name='' children=6 types=1
ROOT2 name='' children=6 types=1
GETSTR 'Q'
)gold";;

// Decodes one of the hex arrays into bytes.
inline void HexToBytes(const char* hex, std::string& out) {
    out.clear();
    out.reserve(std::strlen(hex) / 2);
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        int hi = p[0] <= '9' ? p[0] - '0' : (p[0] | 32) - 'a' + 10;
        int lo = p[1] <= '9' ? p[1] - '0' : (p[1] | 32) - 'a' + 10;
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
}

} // namespace ILSpy::Tests::NsDefGold
