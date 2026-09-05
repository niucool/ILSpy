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
# Elements()/Attributes() sequences on XContainer), and the serialization
# slice (XmlWriter -- the flattened XmlWellFormedWriter/
# XmlEncodedRawTextWriterIndent composition -- plus the ElementWriter walk
# and the XNode/XElement/XDocument/XAttribute ToString/WriteTo/Save
# surface), and the XML text-parser slice (XmlTextParser: the XmlReader
# machinery behind XElement.Parse/XDocument.Parse -- LoadOptions.None, the
# LiteralContentHandler form).

target_sources(ilspy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ElementWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlTextParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XmlWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XAttribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XContainer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XElement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/XText.cpp
)
