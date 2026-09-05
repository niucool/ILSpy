# Decompiler/Xml: the port-authored System.Xml / System.Xml.Linq stand-in the
# BAML decompiler consumes (the C# ICSharpCode.BamlDecompiler uses the BCL
# classes directly -- there is no ICSharpCode source file to mirror). This
# slice holds the name layer (XmlConvert.EncodeLocalName/VerifyNCName/
# VerifyName, XNamespace, XName), the node half of the DOM (XObject,
# XNode with the lazy sibling sequences, XContainer with the Inserter and
# the shared_ptr ownership model, XText/XCData/XComment/
# XProcessingInstruction/XDocumentType, and XDocument as the concrete
# container), and the element half (XAttribute, XElement with the circular
# attribute list and the namespace prefix resolution, and the lazy
# Elements()/Attributes() sequences on XContainer). The ToString
# serialization (the eventual --decompile-baml output gate) and the
# XmlReader Load/Parse paths extend this directory.

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XAttribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XContainer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XElement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XText.cpp
)
