// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// GENERATED FILE -- the ResolveMethod_Test gold fixture, generated from the
// real ICSharpCode.Decompiler 11.0 RmProbe dump (C:/temp-probe/RmProbe,
// rm_gold_raw.txt) by gen_gold.py. DO NOT EDIT BY HAND: regenerate instead.
// Every digest / count / curated line mirrors the real engine over the same
// fixture files (mscorlib 4.8 + System.dll 4.8, the single-module and paired
// compilation shapes).

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace ILSpy::Tests {

// The mscorlib single-module compilation digests (the probe's mscSingle).
inline constexpr std::uint64_t kMscMethodDigest = 0x82E11537837F7337ULL;
inline constexpr int kMscMethodRows = 1717;
inline constexpr std::uint64_t kMscFieldDigest = 0xB33A62D164067930ULL;
inline constexpr int kMscFieldRows = 888;
inline constexpr std::uint64_t kMscSpecDigest = 0x7EE7612345491730ULL;
inline constexpr int kMscSpecRows = 656;
inline constexpr std::uint64_t kMscSameDigest = 0xAD4D98EF8F524047ULL;
inline constexpr int kMscSameDeclMethods = 1185;
inline constexpr std::uint64_t kMscCrossPlainDigest = 0x004BBC6F5F1E76E5ULL;
inline constexpr int kMscImplMethods = 1318;
inline constexpr int kMscMemberRefDeclMethods = 133;
inline constexpr int kMscAccessorDeclMethods = 51;
inline constexpr std::uint64_t kMscMethoddefDigest = 0x6CDE8CE2DC6DD65DULL;
inline constexpr int kMscMethoddefRows = 15;
// All 824 accessor-shaped method memberrefs of mscorlib have RESOLVABLE
// declaring types (every parent is a TypeSpec into mscorlib itself), so
// the accessor-search arm resolves every one of them to a REAL accessor
// method (both engines agree; the digest is byte-exact).
inline constexpr int kMscAccRealCount = 824;
inline constexpr std::uint64_t kMscAccRealDigest = 0x81C6DD381736A0BFULL;
inline constexpr int kMscAccFakeCount = 0;

// The accessor-named cross-module MethodImpl declarations, partitioned the
// way the probe partitions them: the FAKE subset (UNRESOLVABLE declaring
// types -- both engines build the fake member) vs the REAL subset (the
// resolvable-parent rows the accessor-search arm of ResolveMethodReference
// resolves -- the MetadataProperty/MetadataEvent slice landed them; both
// digests byte-exact against the real engine).
inline constexpr int kMscAccDeclFakeCount = 0;
inline constexpr std::uint64_t kMscAccDeclFakeDigest = 0xCBF29CE484222325ULL;
inline constexpr int kMscAccDeclRealCount = 51;
inline constexpr std::uint64_t kMscAccDeclRealDigest = 0xF98236131FFFF86BULL;
inline constexpr int kSysAccDeclFakeCount = 16;
inline constexpr std::uint64_t kSysAccDeclFakeDigest = 0x28F55211D80EF420ULL;
inline constexpr int kSysAccDeclRealCount = 166;
inline constexpr std::uint64_t kSysAccDeclRealDigest = 0x33C78D02658BAD96ULL;

// The System.dll paired compilation digests (System main + mscorlib ref).
inline constexpr std::uint64_t kSysMethodDigest = 0x85E10B86B73175E3ULL;
inline constexpr int kSysMethodRows = 2275;
inline constexpr std::uint64_t kSysFieldDigest = 0x412C6E58377E373FULL;
inline constexpr int kSysFieldRows = 280;
inline constexpr std::uint64_t kSysSpecDigest = 0x07E0205DD425359BULL;
inline constexpr int kSysSpecRows = 174;
inline constexpr std::uint64_t kSysSameDigest = 0xC9C2E9FEE23F84FDULL;
inline constexpr int kSysSameDeclMethods = 133;
inline constexpr std::uint64_t kSysCrossPlainDigest = 0x3FBF42EBE409F0AEULL;
inline constexpr int kSysImplMethods = 627;
inline constexpr int kSysMemberRefDeclMethods = 494;
inline constexpr int kSysAccessorDeclMethods = 182;
inline constexpr int kSysAccRealCount = 630;
inline constexpr std::uint64_t kSysAccRealDigest = 0x7AD6BB74EF847768ULL;
inline constexpr int kSysAccFakeCount = 74;
inline constexpr std::uint64_t kSysAccFakeDigest = 0xF5FCB155DC7D015FULL;

inline const std::pair<const char*, const char*> kMscFirstMethodLines[] = {
    { "0A000003", "SpecializedMethod|Method|IndexOf|False|0|System.Collections.Generic.IList`1[[`0]]|System.Int32|1|item:`0|0|<null>|[`0 -> `0]" },
    { "0A000004", "SpecializedMethod|Method|Insert|False|0|System.Collections.Generic.IList`1[[`0]]|System.Void|2|index:System.Int32|item:`0|0|<null>|[`0 -> `0]" },
    { "0A000005", "SpecializedMethod|Method|RemoveAt|False|0|System.Collections.Generic.IList`1[[`0]]|System.Void|1|index:System.Int32|0|<null>|[`0 -> `0]" },
    { "0A000008", "SpecializedMethod|Method|Add|False|0|System.Collections.Generic.ICollection`1[[`0]]|System.Void|1|item:`0|0|<null>|[`0 -> `0]" },
    { "0A000009", "SpecializedMethod|Method|Clear|False|0|System.Collections.Generic.ICollection`1[[`0]]|System.Void|0|0|<null>|[`0 -> `0]" },
    { "0A00000A", "SpecializedMethod|Method|Contains|False|0|System.Collections.Generic.ICollection`1[[`0]]|System.Boolean|1|item:`0|0|<null>|[`0 -> `0]" },
    { "0A00000B", "SpecializedMethod|Method|CopyTo|False|0|System.Collections.Generic.ICollection`1[[`0]]|System.Void|2|array:`0[]|arrayIndex:System.Int32|0|<null>|[`0 -> `0]" },
    { "0A00000C", "SpecializedMethod|Method|Remove|False|0|System.Collections.Generic.ICollection`1[[`0]]|System.Boolean|1|item:`0|0|<null>|[`0 -> `0]" },
    { "0A00000D", "SpecializedMethod|Method|GetEnumerator|False|0|System.Collections.Generic.IEnumerable`1[[`0]]|System.Collections.Generic.IEnumerator`1[[`0]]|0|0|<null>|[`0 -> `0]" },
    { "0A00000E", "SpecializedMethod|Method|GetEnumerator|False|0|System.Collections.Generic.IEnumerable`1[[System.Char]]|System.Collections.Generic.IEnumerator`1[[System.Char]]|0|0|<null>|[`0 -> System.Char]" },
    { "0A00000F", "SpecializedMethod|Method|Report|False|0|System.IProgress`1[[`0]]|System.Void|1|value:`0|0|<null>|[`0 -> `0]" },
    { "0A000010", "SpecializedMethod|Method|Compare|False|0|System.Collections.Generic.IComparer`1[[System.TimeZoneInfo]]|System.Int32|2|x:System.TimeZoneInfo|y:System.TimeZoneInfo|0|<null>|[`0 -> System.TimeZoneInfo]" },
    { "0A000012", "SpecializedMethod|Method|GetEnumerator|False|0|System.Collections.Generic.IEnumerable`1[[System.Security.Claims.Claim]]|System.Collections.Generic.IEnumerator`1[[System.Security.Claims.Claim]]|0|0|<null>|[`0 -> System.Security.Claims.Claim]" },
    { "0A000015", "SpecializedMethod|Method|GetEnumerator|False|0|System.Collections.Generic.IEnumerable`1[[System.Tuple`2[[System.Int32],[System.Int32]]]]|System.Collections.Generic.IEnumerator`1[[System.Tuple`2[[System.Int32],[System.Int32]]]]|0|0|<null>|[`0 -> System.Tuple`2[[System.Int32],[System.Int32]]]" },
    { "0A000017", "SpecializedMethod|Method|TryAdd|False|0|System.Collections.Concurrent.IProducerConsumerCollection`1[[`0]]|System.Boolean|1|item:`0|0|<null>|[`0 -> `0]" },
    { "0A000018", "SpecializedMethod|Method|TryTake|False|0|System.Collections.Concurrent.IProducerConsumerCollection`1[[`0]]|System.Boolean|1|item:`0&|0|<null>|[`0 -> `0]" },
    { "0A00001A", "SpecializedMethod|Method|CopyTo|False|0|System.Collections.Generic.ICollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]|System.Void|2|array:System.Collections.Generic.KeyValuePair`2[[`0],[`1]][]|arrayIndex:System.Int32|0|<null>|[`0 -> System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]" },
    { "0A00001B", "SpecializedMethod|Method|Add|False|0|System.Collections.Generic.IDictionary`2[[`0],[`1]]|System.Void|2|key:`0|value:`1|0|<null>|[`0 -> `0, `1 -> `1]" },
    { "0A00001C", "SpecializedMethod|Method|Remove|False|0|System.Collections.Generic.IDictionary`2[[`0],[`1]]|System.Boolean|1|key:`0|0|<null>|[`0 -> `0, `1 -> `1]" },
    { "0A00001F", "SpecializedMethod|Method|Add|False|0|System.Collections.Generic.ICollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]|System.Void|1|item:System.Collections.Generic.KeyValuePair`2[[`0],[`1]]|0|<null>|[`0 -> System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]" },
    { "0A000020", "SpecializedMethod|Method|Contains|False|0|System.Collections.Generic.ICollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]|System.Boolean|1|item:System.Collections.Generic.KeyValuePair`2[[`0],[`1]]|0|<null>|[`0 -> System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]" },
    { "0A000022", "SpecializedMethod|Method|Remove|False|0|System.Collections.Generic.ICollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]|System.Boolean|1|item:System.Collections.Generic.KeyValuePair`2[[`0],[`1]]|0|<null>|[`0 -> System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]" },
    { "0A000025", "SpecializedMethod|Method|GetEnumerator|False|0|System.Collections.Generic.IEnumerable`1[[System.Tuple`2[[System.Int64],[System.Int64]]]]|System.Collections.Generic.IEnumerator`1[[System.Tuple`2[[System.Int64],[System.Int64]]]]|0|0|<null>|[`0 -> System.Tuple`2[[System.Int64],[System.Int64]]]" },
    { "0A00002A", "SpecializedMethod|Method|Clear|False|0|System.Collections.Generic.ICollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]|System.Void|0|0|<null>|[`0 -> System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]" },
};
inline constexpr int kMscFirstMethodLinesCount = 24;

inline const std::pair<const char*, const char*> kMscFirstFieldLines[] = {
    { "0A00003F", "SpecializedField|<message>i__Field|<>f__AnonymousType0`1[[`0]]|`0|False" },
    { "0A000043", "SpecializedField|Value|EmptyArray`1[[`0]]|`0[]|True" },
    { "0A000060", "SpecializedField|Value|EmptyArray`1[[``0]]|``0[]|True" },
    { "0A00006E", "SpecializedField|comparison|System.Array+FunctorComparer`1[[`0]]|System.Comparison`1[[`0]]|False" },
    { "0A000071", "SpecializedField|Empty|System.SZArrayHelper+SZGenericArrayEnumerator`1[[``0]]|System.SZArrayHelper+SZGenericArrayEnumerator`1[[``0]]|True" },
    { "0A000072", "SpecializedField|_array|System.SZArrayHelper+SZGenericArrayEnumerator`1[[`0]]|`0[]|False" },
    { "0A000073", "SpecializedField|_index|System.SZArrayHelper+SZGenericArrayEnumerator`1[[`0]]|System.Int32|False" },
    { "0A000074", "SpecializedField|_endIndex|System.SZArrayHelper+SZGenericArrayEnumerator`1[[`0]]|System.Int32|False" },
    { "0A000077", "SpecializedField|Empty|System.SZArrayHelper+SZGenericArrayEnumerator`1[[`0]]|System.SZArrayHelper+SZGenericArrayEnumerator`1[[`0]]|True" },
    { "0A000078", "SpecializedField|_array|System.ArraySegment`1[[`0]]|`0[]|False" },
    { "0A000079", "SpecializedField|_offset|System.ArraySegment`1[[`0]]|System.Int32|False" },
    { "0A00007A", "SpecializedField|_count|System.ArraySegment`1[[`0]]|System.Int32|False" },
    { "0A00007E", "SpecializedField|_array|System.ArraySegment`1+ArraySegmentEnumerator[[`0]]|`0[]|False" },
    { "0A00007F", "SpecializedField|_start|System.ArraySegment`1+ArraySegmentEnumerator[[`0]]|System.Int32|False" },
    { "0A000080", "SpecializedField|_end|System.ArraySegment`1+ArraySegmentEnumerator[[`0]]|System.Int32|False" },
    { "0A000081", "SpecializedField|_current|System.ArraySegment`1+ArraySegmentEnumerator[[`0]]|System.Int32|False" },
    { "0A00008C", "SpecializedField|m_Item1|System.Tuple`1[[`0]]|`0|False" },
    { "0A000090", "SpecializedField|m_Item1|System.Tuple`2[[`0],[`1]]|`0|False" },
    { "0A000091", "SpecializedField|m_Item2|System.Tuple`2[[`0],[`1]]|`1|False" },
    { "0A000094", "SpecializedField|m_Item1|System.Tuple`3[[`0],[`1],[`2]]|`0|False" },
    { "0A000095", "SpecializedField|m_Item2|System.Tuple`3[[`0],[`1],[`2]]|`1|False" },
    { "0A000096", "SpecializedField|m_Item3|System.Tuple`3[[`0],[`1],[`2]]|`2|False" },
    { "0A00009A", "SpecializedField|m_Item1|System.Tuple`4[[`0],[`1],[`2],[`3]]|`0|False" },
    { "0A00009B", "SpecializedField|m_Item2|System.Tuple`4[[`0],[`1],[`2],[`3]]|`1|False" },
};
inline constexpr int kMscFirstFieldLinesCount = 24;

inline const std::pair<const char*, const char*> kMscFirstSpecLines[] = {
    { "2B000001", "SpecializedMethod|Method|SizeOf|True|1|System.Runtime.InteropServices.Marshal|System.Int32|1|structure:Microsoft.Win32.Win32Native+SECURITY_ATTRIBUTES|0|<null>|[``0 -> Microsoft.Win32.Win32Native+SECURITY_ATTRIBUTES]" },
    { "2B000002", "SpecializedMethod|Method|BinarySearch|True|1|System.Array|System.Int32|5|array:``0[]|index:System.Int32|length:System.Int32|value:``0|comparer:System.Collections.Generic.IComparer`1[[``0]]|0|<null>|[``0 -> ``0]" },
    { "2B000003", "SpecializedMethod|Method|FindIndex|True|1|System.Array|System.Int32|2|array:``0[]|match:System.Predicate`1[[``0]]|0|<null>|[``0 -> ``0]" },
    { "2B000004", "SpecializedMethod|Method|FindIndex|True|1|System.Array|System.Int32|4|array:``0[]|startIndex:System.Int32|count:System.Int32|match:System.Predicate`1[[``0]]|0|<null>|[``0 -> ``0]" },
    { "2B000005", "SpecializedMethod|Method|FindLastIndex|True|1|System.Array|System.Int32|4|array:``0[]|startIndex:System.Int32|count:System.Int32|match:System.Predicate`1[[``0]]|0|<null>|[``0 -> ``0]" },
    { "2B000006", "SpecializedMethod|Method|IndexOf|True|1|System.Array|System.Int32|4|array:``0[]|value:``0|startIndex:System.Int32|count:System.Int32|0|<null>|[``0 -> ``0]" },
    { "2B000007", "SpecializedMethod|Method|LastIndexOf|True|1|System.Array|System.Int32|4|array:``0[]|value:``0|startIndex:System.Int32|count:System.Int32|0|<null>|[``0 -> ``0]" },
    { "2B000008", "SpecializedMethod|Method|Sort|True|1|System.Array|System.Void|4|array:``0[]|index:System.Int32|length:System.Int32|comparer:System.Collections.Generic.IComparer`1[[``0]]|0|<null>|[``0 -> ``0]" },
    { "2B000009", "SpecializedMethod|Method|Sort|True|2|System.Array|System.Void|5|keys:``0[]|items:``1[]|index:System.Int32|length:System.Int32|comparer:System.Collections.Generic.IComparer`1[[``0]]|0|<null>|[``0 -> ``0, ``1 -> ``1]" },
    { "2B00000A", "SpecializedMethod|Method|Sort|True|1|System.Array|System.Void|2|array:``0[]|comparer:System.Collections.Generic.IComparer`1[[``0]]|0|<null>|[``0 -> ``0]" },
    { "2B00000B", "SpecializedMethod|Method|UnsafeCast|True|1|System.Runtime.CompilerServices.JitHelpers|``0[]|1|o:System.Object|0|<null>|[``0 -> ``0[]]" },
    { "2B00000C", "SpecializedMethod|Method|IndexOf|True|1|System.Array|System.Int32|2|array:``0[]|value:``0|0|<null>|[``0 -> ``0]" },
};
inline constexpr int kMscFirstSpecLinesCount = 12;

inline const std::pair<const char*, const char*> kSysFirstMethodLines[] = {
    { "0A000001", "MetadataMethod|Method|GetObjectData|False|0|System.Runtime.Serialization.ISerializable|System.Void|2|info:System.Runtime.Serialization.SerializationInfo|context:System.Runtime.Serialization.StreamingContext|0|<null>|[]" },
    { "0A000002", "MetadataMethod|Destructor|Finalize|False|0|System.Object|System.Void|0|0|<null>|[]" },
    { "0A000005", "FakeMethod|Method|EnsureNetConfigLoaded|False|0|System.Configuration.Internal.IConfigurationManagerHelper|System.Void|0|0|<null>|[]" },
    { "0A000006", "MetadataMethod|Method|Compare|False|0|System.Collections.IComparer|System.Int32|2|x:System.Object|y:System.Object|0|<null>|[]" },
    { "0A000008", "MetadataMethod|Method|MoveNext|False|0|System.Collections.IEnumerator|System.Boolean|0|0|<null>|[]" },
    { "0A000009", "MetadataMethod|Method|Reset|False|0|System.Collections.IEnumerator|System.Void|0|0|<null>|[]" },
    { "0A00000A", "MetadataMethod|Method|Dispose|False|0|System.IDisposable|System.Void|0|0|<null>|[]" },
    { "0A00000B", "MetadataMethod|Method|GetEnumerator|False|0|System.Collections.IEnumerable|System.Collections.IEnumerator|0|0|<null>|[]" },
    { "0A00000C", "MetadataMethod|Method|GetMethod|False|0|System.Reflection.IReflect|System.Reflection.MethodInfo|5|name:System.String|bindingAttr:System.Reflection.BindingFlags|binder:System.Reflection.Binder|types:System.Type[]|modifiers:System.Reflection.ParameterModifier[]|0|<null>|[]" },
    { "0A00000D", "MetadataMethod|Method|GetMethod|False|0|System.Reflection.IReflect|System.Reflection.MethodInfo|2|name:System.String|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A00000E", "MetadataMethod|Method|GetMethods|False|0|System.Reflection.IReflect|System.Reflection.MethodInfo[]|1|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A00000F", "MetadataMethod|Method|GetField|False|0|System.Reflection.IReflect|System.Reflection.FieldInfo|2|name:System.String|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A000010", "MetadataMethod|Method|GetFields|False|0|System.Reflection.IReflect|System.Reflection.FieldInfo[]|1|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A000011", "MetadataMethod|Method|GetProperty|False|0|System.Reflection.IReflect|System.Reflection.PropertyInfo|2|name:System.String|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A000012", "MetadataMethod|Method|GetProperty|False|0|System.Reflection.IReflect|System.Reflection.PropertyInfo|6|name:System.String|bindingAttr:System.Reflection.BindingFlags|binder:System.Reflection.Binder|returnType:System.Type|types:System.Type[]|modifiers:System.Reflection.ParameterModifier[]|0|<null>|[]" },
    { "0A000013", "MetadataMethod|Method|GetProperties|False|0|System.Reflection.IReflect|System.Reflection.PropertyInfo[]|1|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A000014", "MetadataMethod|Method|GetMember|False|0|System.Reflection.IReflect|System.Reflection.MemberInfo[]|2|name:System.String|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A000015", "MetadataMethod|Method|GetMembers|False|0|System.Reflection.IReflect|System.Reflection.MemberInfo[]|1|bindingAttr:System.Reflection.BindingFlags|0|<null>|[]" },
    { "0A000016", "MetadataMethod|Method|InvokeMember|False|0|System.Reflection.IReflect|System.Object|8|name:System.String|invokeAttr:System.Reflection.BindingFlags|binder:System.Reflection.Binder|target:System.Object|args:System.Object[]|modifiers:System.Reflection.ParameterModifier[]|culture:System.Globalization.CultureInfo|namedParameters:System.String[]|0|<null>|[]" },
    { "0A000019", "MetadataMethod|Method|MoveNext|False|0|System.Runtime.CompilerServices.IAsyncStateMachine|System.Void|0|0|<null>|[]" },
    { "0A00001A", "MetadataMethod|Method|SetStateMachine|False|0|System.Runtime.CompilerServices.IAsyncStateMachine|System.Void|1|stateMachine:System.Runtime.CompilerServices.IAsyncStateMachine|0|<null>|[]" },
    { "0A00001B", "MetadataMethod|Method|CopyTo|False|0|System.Collections.ICollection|System.Void|2|array:System.Array|index:System.Int32|0|<null>|[]" },
    { "0A000020", "MetadataMethod|Method|OnDeserialization|False|0|System.Runtime.Serialization.IDeserializationCallback|System.Void|1|sender:System.Object|0|<null>|[]" },
    { "0A000025", "MetadataMethod|Method|Add|False|0|System.Collections.IList|System.Int32|1|value:System.Object|0|<null>|[]" },
};
inline constexpr int kSysFirstMethodLinesCount = 24;

inline const std::pair<const char*, const char*> kSysFirstFieldLines[] = {
    { "0A0000B5", "MetadataField|Empty|System.String|System.String|True" },
    { "0A0000E8", "MetadataField|Zero|System.IntPtr|nint|True" },
    { "0A00010C", "MetadataField|SystemDefaultCharSize|System.Runtime.InteropServices.Marshal|System.Int32|True" },
    { "0A000122", "MetadataField|Empty|System.EventArgs|System.EventArgs|True" },
    { "0A000129", "MetadataField|LocalMachine|Microsoft.Win32.Registry|Microsoft.Win32.RegistryKey|True" },
    { "0A000159", "MetadataField|Null|System.IO.Stream|System.IO.Stream|True" },
    { "0A00015C", "MetadataField|MinValue|System.DateTime|System.DateTime|True" },
    { "0A000160", "MetadataField|Zero|System.TimeSpan|System.TimeSpan|True" },
    { "0A000167", "MetadataField|MinValue|System.TimeSpan|System.TimeSpan|True" },
    { "0A000177", "MetadataField|handle|System.Runtime.InteropServices.SafeHandle|nint|False" },
    { "0A0001D9", "MetadataField|Default|System.Collections.Comparer|System.Collections.Comparer|True" },
    { "0A000365", "MetadataField|MaxValue|System.DateTime|System.DateTime|True" },
    { "0A000394", "MetadataField|Log|System.Diagnostics.Tracing.FrameworkEventSource|System.Diagnostics.Tracing.FrameworkEventSource|True" },
    { "0A00039E", "MetadataField|MaxValue|System.TimeSpan|System.TimeSpan|True" },
    { "0A0003F9", "MetadataField|Value|System.DBNull|System.DBNull|True" },
    { "0A0003FB", "MetadataField|Value|System.Reflection.Missing|System.Reflection.Missing|True" },
    { "0A00052B", "MetadataField|handle|System.Runtime.InteropServices.CriticalHandle|nint|False" },
    { "0A00053B", "MetadataField|InternalHigh|System.Threading.NativeOverlapped|nint|False" },
    { "0A00053C", "MetadataField|InternalLow|System.Threading.NativeOverlapped|nint|False" },
    { "0A00053D", "MetadataField|EventHandle|System.Threading.NativeOverlapped|nint|False" },
    { "0A000598", "MetadataField|InfiniteTimeSpan|System.Threading.Timeout|System.TimeSpan|True" },
    { "0A0006FA", "SpecializedField|_field1|System.Net.Sockets.Socket+StateTaskCompletionSource`2[[System.Net.EndPoint],[System.Net.Sockets.SocketReceiveFromResult]]|System.Net.EndPoint|False" },
    { "0A0006FD", "SpecializedField|_field1|System.Net.Sockets.Socket+StateTaskCompletionSource`2[[System.Net.Sockets.SocketFlags],[System.Net.Sockets.SocketReceiveMessageFromResult]]|System.Net.Sockets.SocketFlags|False" },
    { "0A0006FE", "SpecializedField|_field2|System.Net.Sockets.Socket+StateTaskCompletionSource`3[[System.Net.Sockets.SocketFlags],[System.Net.EndPoint],[System.Net.Sockets.SocketReceiveMessageFromResult]]|System.Net.EndPoint|False" },
};
inline constexpr int kSysFirstFieldLinesCount = 24;

inline const std::pair<const char*, const char*> kSysFirstSpecLines[] = {
    { "2B000001", "SpecializedMethod|Method|CompareExchange|True|1|System.Threading.Interlocked|System.SR|3|location1:System.SR&|value:System.SR|comparand:System.SR|0|<null>|[``0 -> System.SR]" },
    { "2B000002", "SpecializedMethod|Method|FromAsync|False|1|System.Threading.Tasks.TaskFactory`1[[System.Net.IPAddress[]]]|System.Threading.Tasks.Task`1[[System.Net.IPAddress[]]]|4|beginMethod:System.Func`4[[System.String],[System.AsyncCallback],[System.Object],[System.IAsyncResult]]|endMethod:System.Func`2[[System.IAsyncResult],[System.Net.IPAddress[]]]|arg1:System.String|state:System.Object|0|<null>|[`0 -> System.Net.IPAddress[], ``0 -> System.String]" },
    { "2B000003", "SpecializedMethod|Method|FromAsync|False|1|System.Threading.Tasks.TaskFactory`1[[System.Net.IPHostEntry]]|System.Threading.Tasks.Task`1[[System.Net.IPHostEntry]]|4|beginMethod:System.Func`4[[System.Net.IPAddress],[System.AsyncCallback],[System.Object],[System.IAsyncResult]]|endMethod:System.Func`2[[System.IAsyncResult],[System.Net.IPHostEntry]]|arg1:System.Net.IPAddress|state:System.Object|0|<null>|[`0 -> System.Net.IPHostEntry, ``0 -> System.Net.IPAddress]" },
    { "2B000004", "SpecializedMethod|Method|FromAsync|False|1|System.Threading.Tasks.TaskFactory`1[[System.Net.IPHostEntry]]|System.Threading.Tasks.Task`1[[System.Net.IPHostEntry]]|4|beginMethod:System.Func`4[[System.String],[System.AsyncCallback],[System.Object],[System.IAsyncResult]]|endMethod:System.Func`2[[System.IAsyncResult],[System.Net.IPHostEntry]]|arg1:System.String|state:System.Object|0|<null>|[`0 -> System.Net.IPHostEntry, ``0 -> System.String]" },
    { "2B000005", "SpecializedMethod|Method|CompareExchange|True|1|System.Threading.Interlocked|System.Net.HttpListener+DigestContext[]|3|location1:System.Net.HttpListener+DigestContext[]&|value:System.Net.HttpListener+DigestContext[]|comparand:System.Net.HttpListener+DigestContext[]|0|<null>|[``0 -> System.Net.HttpListener+DigestContext[]]" },
    { "2B000006", "SpecializedMethod|Method|Read|True|1|System.Threading.Volatile|System.Collections.Generic.List`1[[System.Security.Authentication.ExtendedProtection.TokenBinding]]|1|location:System.Collections.Generic.List`1[[System.Security.Authentication.ExtendedProtection.TokenBinding]]&|0|<null>|[``0 -> System.Collections.Generic.List`1[[System.Security.Authentication.ExtendedProtection.TokenBinding]]]" },
    { "2B000007", "SpecializedMethod|Method|TryInitialize|True|1|System.Net.ServicePointManager|System.Boolean|2|loadConfiguration:System.Func`2[[System.Boolean],[System.Boolean]]|fallbackDefault:System.Boolean|0|<null>|[``0 -> System.Boolean]" },
    { "2B000008", "SpecializedMethod|Method|TryInitialize|True|1|System.Net.ServicePointManager|System.Security.Authentication.SslProtocols|2|loadConfiguration:System.Func`2[[System.Security.Authentication.SslProtocols],[System.Security.Authentication.SslProtocols]]|fallbackDefault:System.Security.Authentication.SslProtocols|0|<null>|[``0 -> System.Security.Authentication.SslProtocols]" },
    { "2B000009", "SpecializedMethod|Method|TryParse|True|1|System.Enum|System.Boolean|2|value:System.String|result:System.Net.SecurityProtocolType&|0|<null>|[``0 -> System.Net.SecurityProtocolType]" },
    { "2B00000A", "SpecializedMethod|Method|CompareExchange|True|1|System.Threading.Interlocked|System.ComponentModel.AsyncOperation|3|location1:System.ComponentModel.AsyncOperation&|value:System.ComponentModel.AsyncOperation|comparand:System.ComponentModel.AsyncOperation|0|<null>|[``0 -> System.ComponentModel.AsyncOperation]" },
    { "2B00000B", "SpecializedMethod|Method|CompareExchange|True|1|System.Threading.Interlocked|System.Net.OpenReadCompletedEventHandler|3|location1:System.Net.OpenReadCompletedEventHandler&|value:System.Net.OpenReadCompletedEventHandler|comparand:System.Net.OpenReadCompletedEventHandler|0|<null>|[``0 -> System.Net.OpenReadCompletedEventHandler]" },
    { "2B00000C", "SpecializedMethod|Method|CompareExchange|True|1|System.Threading.Interlocked|System.Net.OpenWriteCompletedEventHandler|3|location1:System.Net.OpenWriteCompletedEventHandler&|value:System.Net.OpenWriteCompletedEventHandler|comparand:System.Net.OpenWriteCompletedEventHandler|0|<null>|[``0 -> System.Net.OpenWriteCompletedEventHandler]" },
};
inline constexpr int kSysFirstSpecLinesCount = 12;

inline const std::pair<const char*, const char*> kMscMethoddefLines[] = {
    { "06000001", "MetadataMethod|Accessor|get_message|False|0|<>f__AnonymousType0`1|`0|0|2|Property:message|[]" },
    { "06000553", "VarArgInstanceMethod|Method|Concat|True|0|System.String|System.String|4|arg0:System.Object|arg1:System.Object|arg2:System.Object|arg3:System.Object|0|<null>|[]" },
    { "060009C5", "MetadataMethod|Constructor|.ctor|False|0|System.ArrayTypeMismatchException|System.Void|1|message:System.String|0|<null>|[]" },
    { "06000B7F", "VarArgInstanceMethod|Method|WriteLine|True|0|System.Console|System.Void|5|format:System.String|arg0:System.Object|arg1:System.Object|arg2:System.Object|arg3:System.Object|0|<null>|[]" },
    { "06000B84", "VarArgInstanceMethod|Method|Write|True|0|System.Console|System.Void|5|format:System.String|arg0:System.Object|arg1:System.Object|arg2:System.Object|arg3:System.Object|0|<null>|[]" },
    { "06001389", "MetadataMethod|Accessor|get_StandardName|False|0|System.TimeZone|System.String|0|2|Property:StandardName|[]" },
    { "06001D4D", "MetadataMethod|Method|EnsureTriplesListCreated|False|0|System.Security.PermissionListSet|System.Void|0|0|<null>|[]" },
    { "06002711", "MetadataMethod|Method|Reset|False|0|System.Security.Permissions.ReflectionPermission|System.Void|0|0|<null>|[]" },
    { "060030D5", "MetadataMethod|Method|Compute|True|0|System.Globalization.CalendricalCalculationsHelper|System.Double|1|time:System.Double|0|<null>|[]" },
    { "06003A99", "MetadataMethod|Constructor|.ctor|False|0|System.Collections.Generic.EnumEqualityComparer`1|System.Void|2|information:System.Runtime.Serialization.SerializationInfo|context:System.Runtime.Serialization.StreamingContext|0|<null>|[]" },
    { "0600445D", "MetadataMethod|Accessor|get_Title|False|0|System.Reflection.AssemblyTitleAttribute|System.String|0|2|Property:Title|[]" },
    { "06004E21", "MetadataMethod|Accessor|get_IsGenericType|False|0|System.Reflection.Emit.TypeBuilderInstantiation|System.Boolean|0|2|Property:IsGenericType|[]" },
    { "060057E5", "MetadataMethod|Constructor|.ctor|False|0|System.Runtime.Remoting.Metadata.W3cXsd2001.SoapName|System.Void|0|0|<null>|[]" },
    { "060061A9", "MetadataMethod|Constructor|.cctor|True|0|System.Runtime.InteropServices.Marshal|System.Void|0|0|<null>|[]" },
    { "06006B6D", "MetadataMethod|Method|EndInvoke|False|0|System.Security.AccessControl.NativeObjectSecurity+ExceptionFromErrorCode|System.Exception|1|result:System.IAsyncResult|0|<null>|[]" },
};
inline constexpr int kMscMethoddefLinesCount = 15;

inline const std::pair<const char*, const char*> kSysAccFakeLines[] = {
    { "0A000003", "FakeMethod|Method|get_LineNumber|False|0|System.Configuration.Internal.IConfigErrorInfo|System.Int32|0|2|Property:LineNumber|[]" },
    { "0A000004", "FakeMethod|Method|get_Filename|False|0|System.Configuration.Internal.IConfigErrorInfo|System.String|0|2|Property:Filename|[]" },
    { "0A000228", "FakeMethod|Method|get_Item|False|0|System.Configuration.ConfigurationElement|System.Object|1|:System.Configuration.ConfigurationProperty|2|Indexer:Item|[]" },
    { "0A00022E", "FakeMethod|Method|set_AddElementName|False|0|System.Configuration.ConfigurationElementCollection|System.Void|1|:System.String|1|Property:AddElementName|[]" },
    { "0A00022F", "FakeMethod|Method|set_ClearElementName|False|0|System.Configuration.ConfigurationElementCollection|System.Void|1|:System.String|1|Property:ClearElementName|[]" },
    { "0A000230", "FakeMethod|Method|set_RemoveElementName|False|0|System.Configuration.ConfigurationElementCollection|System.Void|1|:System.String|1|Property:RemoveElementName|[]" },
};
inline constexpr int kSysAccFakeLinesCount = 6;

// The accessor-shaped memberrefs with UNRESOLVABLE declaring types resolve
// through the fake-method path in BOTH engines (the probe renders them
// identically); the RESOLVABLE ones throw the port's deferral -- the
// sampled real-engine lines document the not-yet-reproducible partition.
inline const std::pair<const char*, const char*> kSysAccRealLines[] = {
    { "0A000007", "MetadataMethod|Accessor|get_Current|False|0|System.Collections.IEnumerator|System.Object|0|2|Property:Current|[]" },
    { "0A000017", "MetadataMethod|Accessor|get_UnderlyingSystemType|False|0|System.Reflection.IReflect|System.Type|0|2|Property:UnderlyingSystemType|[]" },
    { "0A000018", "SpecializedMethod|Accessor|get_Current|False|0|System.Collections.Generic.IEnumerator`1[[System.Object]]|System.Object|0|2|Property:Current|[`0 -> System.Object]" },
};
inline constexpr int kSysAccRealLinesCount = 3;

// The ResolveEntity spot drives ("<form> <token> -> <render>" or the nil form).
inline const char* const kMscEntityLines[] = {
    "nil -> True",
    "Tdef 02000073 -> MetadataTypeDefinition|TypeDefinition|String|<null>",
    "Mdef 06000553 -> MetadataMethod|Method|Concat|System.String",
    "Fdef 04000001 -> MetadataField|Field|<message>i__Field|<>f__AnonymousType0`1",
};
inline constexpr int kMscEntityLineCount = 4;

// The MethodImpl sample drives (the real engine's renders; the " ~ "
// separates the header line from the per-member lines).
inline const char* const kMscImplSamples[] = {
    "060002BC System.Collections.ICollection.get_Count isExplicit=True members=1 ~     MetadataMethod|Accessor|get_Count|System.Collections.ICollection",
    "060002C1 System.Collections.IList.get_Item isExplicit=True members=1 ~     MetadataMethod|Accessor|get_Item|System.Collections.IList",
    "060002C2 System.Collections.IList.set_Item isExplicit=True members=1 ~     MetadataMethod|Accessor|set_Item|System.Collections.IList",
    "060002C3 System.Collections.IList.Add isExplicit=True members=1 ~     MetadataMethod|Method|Add|System.Collections.IList",
    "060002C4 System.Collections.IList.Contains isExplicit=True members=1 ~     MetadataMethod|Method|Contains|System.Collections.IList",
    "060002C5 System.Collections.IList.Clear isExplicit=True members=1 ~     MetadataMethod|Method|Clear|System.Collections.IList",
    "060002C6 System.Collections.IList.IndexOf isExplicit=True members=1 ~     MetadataMethod|Method|IndexOf|System.Collections.IList",
    "060002C7 System.Collections.IList.Insert isExplicit=True members=1 ~     MetadataMethod|Method|Insert|System.Collections.IList",
    "060002C8 System.Collections.IList.Remove isExplicit=True members=1 ~     MetadataMethod|Method|Remove|System.Collections.IList",
    "060002C9 System.Collections.IList.RemoveAt isExplicit=True members=1 ~     MetadataMethod|Method|RemoveAt|System.Collections.IList",
    "06000327 System.Collections.Generic.IList<T>.IndexOf isExplicit=True members=1 ~     SpecializedMethod|Method|IndexOf|System.Collections.Generic.IList`1[[`0]]",
    "06000328 System.Collections.Generic.IList<T>.Insert isExplicit=True members=1 ~     SpecializedMethod|Method|Insert|System.Collections.Generic.IList`1[[`0]]",
    "06000329 System.Collections.Generic.IList<T>.RemoveAt isExplicit=True members=1 ~     SpecializedMethod|Method|RemoveAt|System.Collections.Generic.IList`1[[`0]]",
    "0600032C System.Collections.Generic.ICollection<T>.Add isExplicit=True members=1 ~     SpecializedMethod|Method|Add|System.Collections.Generic.ICollection`1[[`0]]",
    "0600032D System.Collections.Generic.ICollection<T>.Clear isExplicit=True members=1 ~     SpecializedMethod|Method|Clear|System.Collections.Generic.ICollection`1[[`0]]",
    "0600032E System.Collections.Generic.ICollection<T>.Contains isExplicit=True members=1 ~     SpecializedMethod|Method|Contains|System.Collections.Generic.ICollection`1[[`0]]",
    "0600032F System.Collections.Generic.ICollection<T>.CopyTo isExplicit=True members=1 ~     SpecializedMethod|Method|CopyTo|System.Collections.Generic.ICollection`1[[`0]]",
    "06000330 System.Collections.Generic.ICollection<T>.Remove isExplicit=True members=1 ~     SpecializedMethod|Method|Remove|System.Collections.Generic.ICollection`1[[`0]]",
    "06000331 System.Collections.Generic.IEnumerable<T>.GetEnumerator isExplicit=True members=1 ~     SpecializedMethod|Method|GetEnumerator|System.Collections.Generic.IEnumerable`1[[`0]]",
    "06000573 System.Collections.Generic.IEnumerable<System.Char>.GetEnumerator isExplicit=True members=1 ~     SpecializedMethod|Method|GetEnumerator|System.Collections.Generic.IEnumerable`1[[System.Char]]",
};
inline constexpr int kMscImplSampleCount = 20;
inline const char* const kSysImplSamples[] = {
    "0600012C System.CodeDom.Compiler.ICodeCompiler.CompileAssemblyFromDom isExplicit=True members=1 ~     MetadataMethod|Method|CompileAssemblyFromDom|System.CodeDom.Compiler.ICodeCompiler",
    "0600012D System.CodeDom.Compiler.ICodeCompiler.CompileAssemblyFromFile isExplicit=True members=1 ~     MetadataMethod|Method|CompileAssemblyFromFile|System.CodeDom.Compiler.ICodeCompiler",
    "0600012E System.CodeDom.Compiler.ICodeCompiler.CompileAssemblyFromSource isExplicit=True members=1 ~     MetadataMethod|Method|CompileAssemblyFromSource|System.CodeDom.Compiler.ICodeCompiler",
    "0600012F System.CodeDom.Compiler.ICodeCompiler.CompileAssemblyFromSourceBatch isExplicit=True members=1 ~     MetadataMethod|Method|CompileAssemblyFromSourceBatch|System.CodeDom.Compiler.ICodeCompiler",
    "06000130 System.CodeDom.Compiler.ICodeCompiler.CompileAssemblyFromFileBatch isExplicit=True members=1 ~     MetadataMethod|Method|CompileAssemblyFromFileBatch|System.CodeDom.Compiler.ICodeCompiler",
    "06000131 System.CodeDom.Compiler.ICodeCompiler.CompileAssemblyFromDomBatch isExplicit=True members=1 ~     MetadataMethod|Method|CompileAssemblyFromDomBatch|System.CodeDom.Compiler.ICodeCompiler",
    "0600013A System.CodeDom.Compiler.ICodeGenerator.GenerateCodeFromType isExplicit=True members=1 ~     MetadataMethod|Method|GenerateCodeFromType|System.CodeDom.Compiler.ICodeGenerator",
    "0600013B System.CodeDom.Compiler.ICodeGenerator.GenerateCodeFromExpression isExplicit=True members=1 ~     MetadataMethod|Method|GenerateCodeFromExpression|System.CodeDom.Compiler.ICodeGenerator",
    "0600013C System.CodeDom.Compiler.ICodeGenerator.GenerateCodeFromCompileUnit isExplicit=True members=1 ~     MetadataMethod|Method|GenerateCodeFromCompileUnit|System.CodeDom.Compiler.ICodeGenerator",
    "0600013D System.CodeDom.Compiler.ICodeGenerator.GenerateCodeFromNamespace isExplicit=True members=1 ~     MetadataMethod|Method|GenerateCodeFromNamespace|System.CodeDom.Compiler.ICodeGenerator",
    "0600032A System.Runtime.Serialization.ISerializable.GetObjectData isExplicit=True members=1 ~     MetadataMethod|Method|GetObjectData|System.Runtime.Serialization.ISerializable",
    "060003C4 System.Runtime.Serialization.ISerializable.GetObjectData isExplicit=True members=1 ~     MetadataMethod|Method|GetObjectData|System.Runtime.Serialization.ISerializable",
    "06000433 Finalize isExplicit=False members=1 ~     MetadataMethod|Destructor|Finalize|System.Object",
    "06000648 System.Configuration.Internal.IConfigurationManagerHelper.EnsureNetConfigLoaded isExplicit=True members=1 ~     FakeMethod|Method|EnsureNetConfigLoaded|System.Configuration.Internal.IConfigurationManagerHelper",
    "06000737 System.Collections.IComparer.Compare isExplicit=True members=1 ~     MetadataMethod|Method|Compare|System.Collections.IComparer",
    "06000776 System.Runtime.Serialization.ISerializable.GetObjectData isExplicit=True members=1 ~     MetadataMethod|Method|GetObjectData|System.Runtime.Serialization.ISerializable",
    "060007CF System.Runtime.Serialization.ISerializable.GetObjectData isExplicit=True members=1 ~     MetadataMethod|Method|GetObjectData|System.Runtime.Serialization.ISerializable",
    "06000801 System.Runtime.Serialization.ISerializable.GetObjectData isExplicit=True members=1 ~     MetadataMethod|Method|GetObjectData|System.Runtime.Serialization.ISerializable",
    "06000885 Finalize isExplicit=False members=1 ~     MetadataMethod|Destructor|Finalize|System.Object",
    "060008C2 System.IDisposable.Dispose isExplicit=True members=1 ~     MetadataMethod|Method|Dispose|System.IDisposable",
};
inline constexpr int kSysImplSampleCount = 20;
// The accessor-named MethodImpl declarations the port defers on ("<token>
// <name>" pairs).
inline const char* const kMscAccDeclSamples[] = {
    "06000325 System.Collections.Generic.IList<T>.get_Item",
    "06000326 System.Collections.Generic.IList<T>.set_Item",
    "0600032A System.Collections.Generic.IReadOnlyList<T>.get_Item",
    "0600032B System.Collections.Generic.ICollection<T>.get_IsReadOnly",
};
inline constexpr int kMscAccDeclSampleCount = 4;
inline const char* const kSysAccDeclSamples[] = {
    "06000529 System.Configuration.Internal.IConfigErrorInfo.get_LineNumber",
    "0600052A System.Configuration.Internal.IConfigErrorInfo.get_Filename",
    "0600052D System.Configuration.Internal.IConfigErrorInfo.get_LineNumber",
    "0600052E System.Configuration.Internal.IConfigErrorInfo.get_Filename",
};
inline constexpr int kSysAccDeclSampleCount = 4;

} // namespace ILSpy::Tests
