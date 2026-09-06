// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files ("the Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The curated gold block of the MetadataProperty / MetadataEvent slice: 84
// exact lines dumped from the REAL ICSharpCode.Decompiler 11.0 through the
// C:/temp-probe/PeProbe probe (its `curated` mode) over the same local
// fixtures the port tests drive -- the C1-C12 sections:
//   * C1/C2: the [DefaultMember] indexers (String.Chars / StringBuilder.Chars)
//   * C3: List`1's full property set (the plain, the explicit-interface
//     dotted names with their resolved EII members, the Item indexer pair)
//   * C4: System.dll's first unresolvable-parent dotted-name properties (the
//     fake-property symbolKind + EII render through the guessed accessor)
//   * C5: the attribute lists (String.Chars's [IndexerName] + the List`1 EII
//     row) and the HasAttribute / GetAttribute matrix over String.Chars
//   * C6: the first IsOverride single-accessor property (the base-copy
//     accessibility arm)
//   * C7/C8/C9: the ToString renders, the Equals module-file identity, the
//     Identity Specialize same-instance contract
//   * C10/C10sys: the first event of mscorlib / System.dll with its
//     attributes, matrix, ToString, and Specialize
//   * C11: the DefaultMemberName drives + ArrayList's full property set
//   * C12: the GetFilteredAccessors composition over List`1 and Exception
// Regenerate with C:/temp-probe/PeProbe/gen_fixture.py after any probe change.

#pragma once

#include <array>

namespace ILSpy::Tests {

inline const std::array<const char*, 84> PeGoldLines = {
    R"pe(C1 String.DefaultMemberName="Chars")pe",
    R"pe(P C1 dt=<System.String> n="Chars" kind=Indexer acc=Public get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=True rro=False eii=False ret=<System.Char> ps=[index|None|<System.Int32>;] gt=060004C8 st2=- ao=self/- eim=n=0)pe",
    R"pe(C2 StringBuilder.DefaultMemberName="Chars")pe",
    R"pe(P C2 dt=<System.Text.StringBuilder> n="Chars" kind=Indexer acc=Public get=True set=True st=False ab=False sd=False vi=False ov=False ob=False ix=True rro=False eii=False ret=<System.Char> ps=[index|None|<System.Int32>;] gt=060066E9 st2=060066EA ao=self/self eim=n=0)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="Capacity" kind=Property acc=Public get=True set=True st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=False ret=<System.Int32> ps=[] gt=06003AEF st2=06003AF0 ao=self/self eim=n=0)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="Count" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=False ret=<System.Int32> ps=[] gt=06003AF1 st2=- ao=self/- eim=n=0)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="System.Collections.IList.IsFixedSize" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Boolean> ps=[] gt=06003AF2 st2=- ao=self/- eim=n=1:<System.Collections.IList.IsFixedSize>)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="System.Collections.Generic.ICollection<T>.IsReadOnly" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Boolean> ps=[] gt=06003AF3 st2=- ao=self/- eim=n=1:<System.Collections.Generic.ICollection`1.IsReadOnly>)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="System.Collections.IList.IsReadOnly" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Boolean> ps=[] gt=06003AF4 st2=- ao=self/- eim=n=1:<System.Collections.IList.IsReadOnly>)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="System.Collections.ICollection.IsSynchronized" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Boolean> ps=[] gt=06003AF5 st2=- ao=self/- eim=n=1:<System.Collections.ICollection.IsSynchronized>)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="System.Collections.ICollection.SyncRoot" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Object> ps=[] gt=06003AF6 st2=- ao=self/- eim=n=1:<System.Collections.ICollection.SyncRoot>)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="Item" kind=Indexer acc=Public get=True set=True st=False ab=False sd=False vi=False ov=False ob=False ix=True rro=False eii=False ret=<`0> ps=[index|None|<System.Int32>;] gt=06003AF7 st2=06003AF8 ao=self/self eim=n=0)pe",
    R"pe(P C3 dt=<System.Collections.Generic.List`1> n="System.Collections.IList.Item" kind=Indexer acc=Private get=True set=True st=False ab=False sd=False vi=False ov=False ob=False ix=True rro=False eii=True ret=<System.Object> ps=[index|None|<System.Int32>;] gt=06003AFA st2=06003AFB ao=self/self eim=n=1:<System.Collections.IList.Item>)pe",
    R"pe(P C4 dt=<System.Configuration.ConfigXmlAttribute> n="System.Configuration.Internal.IConfigErrorInfo.LineNumber" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Int32> ps=[] gt=06000529 st2=- ao=self/- eim=n=1:<System.Configuration.Internal.IConfigErrorInfo.LineNumber>)pe",
    R"pe(P C4 dt=<System.Configuration.ConfigXmlAttribute> n="System.Configuration.Internal.IConfigErrorInfo.Filename" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.String> ps=[] gt=0600052A st2=- ao=self/- eim=n=1:<System.Configuration.Internal.IConfigErrorInfo.Filename>)pe",
    R"pe(P C4 dt=<System.Configuration.ConfigXmlCDataSection> n="System.Configuration.Internal.IConfigErrorInfo.LineNumber" kind=Property acc=Private get=True set=False st=False ab=False sd=False vi=False ov=False ob=False ix=False rro=False eii=True ret=<System.Int32> ps=[] gt=0600052D st2=- ao=self/- eim=n=1:<System.Configuration.Internal.IConfigErrorInfo.LineNumber>)pe",
    R"pe(A C5:msc:Chars n=2)pe",
    R"pe(A0:type=<System.Runtime.CompilerServices.IndexerNameAttribute>:ctor=<System.Runtime.CompilerServices.IndexerNameAttribute..ctor>:err=False:F=1:N=0)pe",
    R"pe(A0:F0:type=<System.String>:val=str:"Chars")pe",
    R"pe(A1:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0)pe",
    R"pe(A C5:msc:ListEii n=1)pe",
    R"pe(A0:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0)pe",
    R"pe(H C5:msc:Chars None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')")pe",
    R"pe(H C5:msc:Chars SpecialName=False)pe",
    R"pe(H C5:msc:Chars Obsolete=False)pe",
    R"pe(H C5:msc:Chars CompilerGenerated=False)pe",
    R"pe(H C5:msc:Chars IndexerName=True)pe",
    R"pe(H C5:msc:Chars Nullable=False)pe",
    R"pe(H C5:msc:Chars NullableContext=False)pe",
    R"pe(H C5:msc:Chars Serializable=False)pe",
    R"pe(H C5:msc:Chars Extension=False)pe",
    R"pe(G C5:msc:Chars None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')")pe",
    R"pe(G C5:msc:Chars SpecialName null)pe",
    R"pe(G C5:msc:Chars Obsolete null)pe",
    R"pe(G C5:msc:Chars CompilerGenerated null)pe",
    R"pe(G C5:msc:Chars IndexerName found)pe",
    R"pe(GA:type=<System.Runtime.CompilerServices.IndexerNameAttribute>:ctor=<System.Runtime.CompilerServices.IndexerNameAttribute..ctor>:err=False:F=1:N=0)pe",
    R"pe(GA:F0:type=<System.String>:val=str:"Chars")pe",
    R"pe(G C5:msc:Chars Nullable null)pe",
    R"pe(G C5:msc:Chars NullableContext null)pe",
    R"pe(G C5:msc:Chars Serializable null)pe",
    R"pe(G C5:msc:Chars Extension null)pe",
    R"pe(P C6 dt=<Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid> n="IsInvalid" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=False ov=True ob=True ix=False rro=False eii=False ret=<System.Boolean> ps=[] gt=06000173 st2=- ao=self/- eim=n=0)pe",
    R"pe(C7 17000085 System.String.Chars)pe",
    R"pe(C7 17000086 System.String.Length)pe",
    R"pe(C7 170008F4 System.Collections.Generic.List`1.Capacity)pe",
    R"pe(C8 self=True cross=False crossType=False)pe",
    R"pe(C9 spIdentity=True spType=MetadataProperty)pe",
    R"pe(E C10 dt=<System.Exception> n="SerializeObjectState" acc=Protected add=True rem=True inv=False st=False ab=False sd=False vi=False ov=False ob=False eii=False ret=<System.EventHandler`1[[System.Runtime.Serialization.SafeSerializationEventArgs]]> at=060005C8 rt=060005C9 it=- ao=self/self eim=n=0)pe",
    R"pe(A C10:msc:firstEvent n=0)pe",
    R"pe(H C10:msc:firstEvent None=False)pe",
    R"pe(H C10:msc:firstEvent SpecialName=False)pe",
    R"pe(H C10:msc:firstEvent Obsolete=False)pe",
    R"pe(H C10:msc:firstEvent CompilerGenerated=False)pe",
    R"pe(H C10:msc:firstEvent IndexerName=False)pe",
    R"pe(H C10:msc:firstEvent Nullable=False)pe",
    R"pe(H C10:msc:firstEvent NullableContext=False)pe",
    R"pe(H C10:msc:firstEvent Serializable=False)pe",
    R"pe(H C10:msc:firstEvent Extension=False)pe",
    R"pe(G C10:msc:firstEvent None null)pe",
    R"pe(G C10:msc:firstEvent SpecialName null)pe",
    R"pe(G C10:msc:firstEvent Obsolete null)pe",
    R"pe(G C10:msc:firstEvent CompilerGenerated null)pe",
    R"pe(G C10:msc:firstEvent IndexerName null)pe",
    R"pe(G C10:msc:firstEvent Nullable null)pe",
    R"pe(G C10:msc:firstEvent NullableContext null)pe",
    R"pe(G C10:msc:firstEvent Serializable null)pe",
    R"pe(G C10:msc:firstEvent Extension null)pe",
    R"pe(C10 14000001 System.Exception.SerializeObjectState)pe",
    R"pe(C10 spIdentity=True spType=MetadataEvent)pe",
    R"pe(E C10sys dt=<Microsoft.Win32.SystemEvents> n="DisplaySettingsChanging" acc=Public add=True rem=True inv=False st=True ab=False sd=False vi=False ov=False ob=False eii=False ret=<System.EventHandler> at=060001CC rt=060001CD it=- ao=self/self eim=n=0)pe",
    R"pe(C11 msc:String="Chars")pe",
    R"pe(C11 msc:StringBuilder="Chars")pe",
    R"pe(C11 msc:Object="<null>")pe",
    R"pe(C11 msc:ArrayList="Item")pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="Capacity" kind=Property acc=Public get=True set=True st=False ab=False sd=False vi=True ov=False ob=True ix=False rro=False eii=False ret=<System.Int32> ps=[] gt=060037D3 st2=060037D4 ao=self/self eim=n=0)pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="Count" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=True ov=False ob=True ix=False rro=False eii=False ret=<System.Int32> ps=[] gt=060037D5 st2=- ao=self/- eim=n=0)pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="IsFixedSize" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=True ov=False ob=True ix=False rro=False eii=False ret=<System.Boolean> ps=[] gt=060037D6 st2=- ao=self/- eim=n=0)pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="IsReadOnly" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=True ov=False ob=True ix=False rro=False eii=False ret=<System.Boolean> ps=[] gt=060037D7 st2=- ao=self/- eim=n=0)pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="IsSynchronized" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=True ov=False ob=True ix=False rro=False eii=False ret=<System.Boolean> ps=[] gt=060037D8 st2=- ao=self/- eim=n=0)pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="SyncRoot" kind=Property acc=Public get=True set=False st=False ab=False sd=False vi=True ov=False ob=True ix=False rro=False eii=False ret=<System.Object> ps=[] gt=060037D9 st2=- ao=self/- eim=n=0)pe",
    R"pe(P C11 dt=<System.Collections.ArrayList> n="Item" kind=Indexer acc=Public get=True set=True st=False ab=False sd=False vi=True ov=False ob=True ix=True rro=False eii=False ret=<System.Object> ps=[index|None|<System.Int32>;] gt=060037DA st2=060037DB ao=self/self eim=n=0)pe",
    R"pe(C12 list accessors=[get_Capacity:Getter;set_Capacity:Setter;get_Count:Getter;System.Collections.IList.get_IsFixedSize:Getter;System.Collections.Generic.ICollection<T>.get_IsReadOnly:Getter;System.Collections.IList.get_IsReadOnly:Getter;System.Collections.ICollection.get_IsSynchronized:Getter;System.Collections.ICollection.get_SyncRoot:Getter;get_Item:Getter;set_Item:Setter;System.Collections.IList.get_Item:Getter;System.Collections.IList.set_Item:Setter])pe",
    R"pe(C12 System.Exception accessors=[get_Message:Getter;get_Data:Getter;get_InnerException:Getter;get_TargetSite:Getter;get_StackTrace:Getter;get_HelpLink:Getter;set_HelpLink:Setter;get_Source:Getter;set_Source:Setter;get_IPForWatsonBuckets:Getter;get_WatsonBuckets:Getter;get_RemoteStackTrace:Getter;get_HResult:Getter;set_HResult:Setter;get_IsTransient:Getter;add_SerializeObjectState:Adder;remove_SerializeObjectState:Remover])pe",
};

} // namespace ILSpy::Tests
