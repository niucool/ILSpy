# Decompiler/Xml: the port-authored System.Xml / System.Xml.Linq stand-in the
# BAML decompiler consumes (the C# ICSharpCode.BamlDecompiler uses the BCL
# classes directly -- there is no ICSharpCode source file to mirror). This
# slice holds the name layer: XmlConvert.EncodeLocalName/VerifyNCName with the
# probed XML 1.0 4th-edition NCName tables, XNamespace, and XName. The XElement/
# XAttribute/XDocument DOM half (the remaining gate for the XamlContext /
# handlers / rewrite-pass chain) extends this directory.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XName.cpp
)
