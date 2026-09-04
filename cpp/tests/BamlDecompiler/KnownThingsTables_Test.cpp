// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the KnownThings data tables (the Phase-9 KnownThings.g.cs
// Init* rows): the BAML wire-format id -> framework-type/property/string/
// resource mappings WPF's WpfSharedBamlSchemaContext fixes.
//
// Every expectation is gold-pinned two ways: the rows were parsed from the
// repo's C# source AND verified against the runtime resolution of the
// SHIPPED ICSharpCode.BamlDecompiler.dll (ilspycmd 11.0.0.9375) via the
// KnownTablesProbe reflection probe -- a real KnownThings over a real
// BamlDecompilerTypeSystem with the .NET Framework 4.8 WPF assemblies, whose
// dictionaries resolved every one of the 759 type rows and 267 member rows
// to real type definitions matching the table triples exactly. Any drift in
// the port header (a dropped row, an edited namespace, a closed 137 hole)
// fails exactly here.

#include "BamlDecompiler/Baml/KnownMembers.hpp"
#include "BamlDecompiler/Baml/KnownThingsTables.hpp"
#include "BamlDecompiler/Baml/KnownTypes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {
namespace Baml = ILSpy::BamlDecompiler::Baml;

TEST(KnownThingsTablesTest, AssemblySlots)
{
    ASSERT_EQ(std::size(Baml::KnownAssemblies), 6u);
    EXPECT_STREQ(Baml::KnownAssemblies[0], "mscorlib");
    EXPECT_STREQ(Baml::KnownAssemblies[1], "System");
    EXPECT_STREQ(Baml::KnownAssemblies[2], "WindowsBase");
    EXPECT_STREQ(Baml::KnownAssemblies[3], "PresentationCore");
    EXPECT_STREQ(Baml::KnownAssemblies[4], "PresentationFramework");
    EXPECT_STREQ(Baml::KnownAssemblies[5], "System.Xml");
}

// The 759 type rows: ascending contiguous ids 1..759 (Unknown = 0 has no
// row), assembly slots always in range, and every row matching its gold
// triple.
TEST(KnownThingsTablesTest, TypesRowGeometry)
{
    const auto& table = Baml::KnownTypesTable;
    ASSERT_EQ(std::size(table), 759u);
    for (std::size_t i = 0; i < std::size(table); i++) {
        EXPECT_EQ(static_cast<std::int16_t>(table[i].Id), static_cast<std::int16_t>(i + 1)) << i;
        EXPECT_LT(table[i].Row.AssemblyIndex, 6u) << i;
    }
}

TEST(KnownThingsTablesTest, TypesRowsMatchGold)
{
    struct Expected { std::int16_t id; std::uint8_t assemblyIndex; const char* ns; const char* name; };
    static constexpr Expected expected[] = {
        { 1, 4, "System.Windows.Controls", "AccessText" }, // AccessText
        { 2, 4, "System.Windows.Controls", "AdornedElementPlaceholder" }, // AdornedElementPlaceholder
        { 3, 4, "System.Windows.Documents", "Adorner" }, // Adorner
        { 4, 4, "System.Windows.Documents", "AdornerDecorator" }, // AdornerDecorator
        { 5, 4, "System.Windows.Documents", "AdornerLayer" }, // AdornerLayer
        { 6, 3, "System.Windows.Media.Media3D", "AffineTransform3D" }, // AffineTransform3D
        { 7, 3, "System.Windows.Media.Media3D", "AmbientLight" }, // AmbientLight
        { 8, 4, "System.Windows.Documents", "AnchoredBlock" }, // AnchoredBlock
        { 9, 3, "System.Windows.Media.Animation", "Animatable" }, // Animatable
        { 10, 3, "System.Windows.Media.Animation", "AnimationClock" }, // AnimationClock
        { 11, 3, "System.Windows.Media.Animation", "AnimationTimeline" }, // AnimationTimeline
        { 12, 4, "System.Windows", "Application" }, // Application
        { 13, 3, "System.Windows.Media", "ArcSegment" }, // ArcSegment
        { 14, 4, "System.Windows.Markup", "ArrayExtension" }, // ArrayExtension
        { 15, 3, "System.Windows.Media.Media3D", "AxisAngleRotation3D" }, // AxisAngleRotation3D
        { 16, 3, "System.Windows.Media.Converters", "BaseIListConverter" }, // BaseIListConverter
        { 17, 4, "System.Windows.Media.Animation", "BeginStoryboard" }, // BeginStoryboard
        { 18, 3, "System.Windows.Media.Effects", "BevelBitmapEffect" }, // BevelBitmapEffect
        { 19, 3, "System.Windows.Media", "BezierSegment" }, // BezierSegment
        { 20, 4, "System.Windows.Data", "Binding" }, // Binding
        { 21, 4, "System.Windows.Data", "BindingBase" }, // BindingBase
        { 22, 4, "System.Windows.Data", "BindingExpression" }, // BindingExpression
        { 23, 4, "System.Windows.Data", "BindingExpressionBase" }, // BindingExpressionBase
        { 24, 4, "System.Windows.Data", "BindingListCollectionView" }, // BindingListCollectionView
        { 25, 3, "System.Windows.Media.Imaging", "BitmapDecoder" }, // BitmapDecoder
        { 26, 3, "System.Windows.Media.Effects", "BitmapEffect" }, // BitmapEffect
        { 27, 3, "System.Windows.Media.Effects", "BitmapEffectCollection" }, // BitmapEffectCollection
        { 28, 3, "System.Windows.Media.Effects", "BitmapEffectGroup" }, // BitmapEffectGroup
        { 29, 3, "System.Windows.Media.Effects", "BitmapEffectInput" }, // BitmapEffectInput
        { 30, 3, "System.Windows.Media.Imaging", "BitmapEncoder" }, // BitmapEncoder
        { 31, 3, "System.Windows.Media.Imaging", "BitmapFrame" }, // BitmapFrame
        { 32, 3, "System.Windows.Media.Imaging", "BitmapImage" }, // BitmapImage
        { 33, 3, "System.Windows.Media.Imaging", "BitmapMetadata" }, // BitmapMetadata
        { 34, 3, "System.Windows.Media.Imaging", "BitmapPalette" }, // BitmapPalette
        { 35, 3, "System.Windows.Media.Imaging", "BitmapSource" }, // BitmapSource
        { 36, 4, "System.Windows.Documents", "Block" }, // Block
        { 37, 4, "System.Windows.Documents", "BlockUIContainer" }, // BlockUIContainer
        { 38, 3, "System.Windows.Media.Effects", "BlurBitmapEffect" }, // BlurBitmapEffect
        { 39, 3, "System.Windows.Media.Imaging", "BmpBitmapDecoder" }, // BmpBitmapDecoder
        { 40, 3, "System.Windows.Media.Imaging", "BmpBitmapEncoder" }, // BmpBitmapEncoder
        { 41, 4, "System.Windows.Documents", "Bold" }, // Bold
        { 42, 3, "System.Windows.Media.Converters", "BoolIListConverter" }, // BoolIListConverter
        { 43, 0, "System", "Boolean" }, // Boolean
        { 44, 3, "System.Windows.Media.Animation", "BooleanAnimationBase" }, // BooleanAnimationBase
        { 45, 3, "System.Windows.Media.Animation", "BooleanAnimationUsingKeyFrames" }, // BooleanAnimationUsingKeyFrames
        { 46, 1, "System.ComponentModel", "BooleanConverter" }, // BooleanConverter
        { 47, 3, "System.Windows.Media.Animation", "BooleanKeyFrame" }, // BooleanKeyFrame
        { 48, 3, "System.Windows.Media.Animation", "BooleanKeyFrameCollection" }, // BooleanKeyFrameCollection
        { 49, 4, "System.Windows.Controls", "BooleanToVisibilityConverter" }, // BooleanToVisibilityConverter
        { 50, 4, "System.Windows.Controls", "Border" }, // Border
        { 51, 4, "System.Windows.Controls", "BorderGapMaskConverter" }, // BorderGapMaskConverter
        { 52, 3, "System.Windows.Media", "Brush" }, // Brush
        { 53, 3, "System.Windows.Media", "BrushConverter" }, // BrushConverter
        { 54, 4, "System.Windows.Controls.Primitives", "BulletDecorator" }, // BulletDecorator
        { 55, 4, "System.Windows.Controls", "Button" }, // Button
        { 56, 4, "System.Windows.Controls.Primitives", "ButtonBase" }, // ButtonBase
        { 57, 0, "System", "Byte" }, // Byte
        { 58, 3, "System.Windows.Media.Animation", "ByteAnimation" }, // ByteAnimation
        { 59, 3, "System.Windows.Media.Animation", "ByteAnimationBase" }, // ByteAnimationBase
        { 60, 3, "System.Windows.Media.Animation", "ByteAnimationUsingKeyFrames" }, // ByteAnimationUsingKeyFrames
        { 61, 1, "System.ComponentModel", "ByteConverter" }, // ByteConverter
        { 62, 3, "System.Windows.Media.Animation", "ByteKeyFrame" }, // ByteKeyFrame
        { 63, 3, "System.Windows.Media.Animation", "ByteKeyFrameCollection" }, // ByteKeyFrameCollection
        { 64, 3, "System.Windows.Media.Imaging", "CachedBitmap" }, // CachedBitmap
        { 65, 3, "System.Windows.Media.Media3D", "Camera" }, // Camera
        { 66, 4, "System.Windows.Controls", "Canvas" }, // Canvas
        { 67, 0, "System", "Char" }, // Char
        { 68, 3, "System.Windows.Media.Animation", "CharAnimationBase" }, // CharAnimationBase
        { 69, 3, "System.Windows.Media.Animation", "CharAnimationUsingKeyFrames" }, // CharAnimationUsingKeyFrames
        { 70, 1, "System.ComponentModel", "CharConverter" }, // CharConverter
        { 71, 3, "System.Windows.Media.Converters", "CharIListConverter" }, // CharIListConverter
        { 72, 3, "System.Windows.Media.Animation", "CharKeyFrame" }, // CharKeyFrame
        { 73, 3, "System.Windows.Media.Animation", "CharKeyFrameCollection" }, // CharKeyFrameCollection
        { 74, 4, "System.Windows.Controls", "CheckBox" }, // CheckBox
        { 75, 3, "System.Windows.Media.Animation", "Clock" }, // Clock
        { 76, 3, "System.Windows.Media.Animation", "ClockController" }, // ClockController
        { 77, 3, "System.Windows.Media.Animation", "ClockGroup" }, // ClockGroup
        { 78, 4, "System.Windows.Data", "CollectionContainer" }, // CollectionContainer
        { 79, 4, "System.Windows.Data", "CollectionView" }, // CollectionView
        { 80, 4, "System.Windows.Data", "CollectionViewSource" }, // CollectionViewSource
        { 81, 3, "System.Windows.Media", "Color" }, // Color
        { 82, 3, "System.Windows.Media.Animation", "ColorAnimation" }, // ColorAnimation
        { 83, 3, "System.Windows.Media.Animation", "ColorAnimationBase" }, // ColorAnimationBase
        { 84, 3, "System.Windows.Media.Animation", "ColorAnimationUsingKeyFrames" }, // ColorAnimationUsingKeyFrames
        { 85, 3, "System.Windows.Media.Imaging", "ColorConvertedBitmap" }, // ColorConvertedBitmap
        { 86, 4, "System.Windows", "ColorConvertedBitmapExtension" }, // ColorConvertedBitmapExtension
        { 87, 3, "System.Windows.Media", "ColorConverter" }, // ColorConverter
        { 88, 3, "System.Windows.Media.Animation", "ColorKeyFrame" }, // ColorKeyFrame
        { 89, 3, "System.Windows.Media.Animation", "ColorKeyFrameCollection" }, // ColorKeyFrameCollection
        { 90, 4, "System.Windows.Controls", "ColumnDefinition" }, // ColumnDefinition
        { 91, 3, "System.Windows.Media", "CombinedGeometry" }, // CombinedGeometry
        { 92, 4, "System.Windows.Controls", "ComboBox" }, // ComboBox
        { 93, 4, "System.Windows.Controls", "ComboBoxItem" }, // ComboBoxItem
        { 94, 4, "System.Windows.Input", "CommandConverter" }, // CommandConverter
        { 95, 4, "System.Windows", "ComponentResourceKey" }, // ComponentResourceKey
        { 96, 4, "System.Windows.Markup", "ComponentResourceKeyConverter" }, // ComponentResourceKeyConverter
        { 97, 3, "System.Windows.Media", "CompositionTarget" }, // CompositionTarget
        { 98, 4, "System.Windows", "Condition" }, // Condition
        { 99, 3, "System.Windows.Media", "ContainerVisual" }, // ContainerVisual
        { 100, 4, "System.Windows.Controls", "ContentControl" }, // ContentControl
        { 101, 3, "System.Windows", "ContentElement" }, // ContentElement
        { 102, 4, "System.Windows.Controls", "ContentPresenter" }, // ContentPresenter
        { 103, 2, "System.Windows.Markup", "ContentPropertyAttribute" }, // ContentPropertyAttribute
        { 104, 2, "System.Windows.Markup", "ContentWrapperAttribute" }, // ContentWrapperAttribute
        { 105, 4, "System.Windows.Controls", "ContextMenu" }, // ContextMenu
        { 106, 4, "System.Windows.Controls", "ContextMenuService" }, // ContextMenuService
        { 107, 4, "System.Windows.Controls", "Control" }, // Control
        { 108, 4, "System.Windows.Controls", "ControlTemplate" }, // ControlTemplate
        { 109, 4, "System.Windows.Media.Animation", "ControllableStoryboardAction" }, // ControllableStoryboardAction
        { 110, 4, "System.Windows", "CornerRadius" }, // CornerRadius
        { 111, 4, "System.Windows", "CornerRadiusConverter" }, // CornerRadiusConverter
        { 112, 3, "System.Windows.Media.Imaging", "CroppedBitmap" }, // CroppedBitmap
        { 113, 0, "System.Globalization", "CultureInfo" }, // CultureInfo
        { 114, 1, "System.ComponentModel", "CultureInfoConverter" }, // CultureInfoConverter
        { 115, 3, "System.Windows", "CultureInfoIetfLanguageTagConverter" }, // CultureInfoIetfLanguageTagConverter
        { 116, 3, "System.Windows.Input", "Cursor" }, // Cursor
        { 117, 3, "System.Windows.Input", "CursorConverter" }, // CursorConverter
        { 118, 3, "System.Windows.Media", "DashStyle" }, // DashStyle
        { 119, 4, "System.Windows.Data", "DataChangedEventManager" }, // DataChangedEventManager
        { 120, 4, "System.Windows", "DataTemplate" }, // DataTemplate
        { 121, 4, "System.Windows", "DataTemplateKey" }, // DataTemplateKey
        { 122, 4, "System.Windows", "DataTrigger" }, // DataTrigger
        { 123, 0, "System", "DateTime" }, // DateTime
        { 124, 1, "System.ComponentModel", "DateTimeConverter" }, // DateTimeConverter
        { 125, 2, "System.Windows.Markup", "DateTimeConverter2" }, // DateTimeConverter2
        { 126, 0, "System", "Decimal" }, // Decimal
        { 127, 3, "System.Windows.Media.Animation", "DecimalAnimation" }, // DecimalAnimation
        { 128, 3, "System.Windows.Media.Animation", "DecimalAnimationBase" }, // DecimalAnimationBase
        { 129, 3, "System.Windows.Media.Animation", "DecimalAnimationUsingKeyFrames" }, // DecimalAnimationUsingKeyFrames
        { 130, 1, "System.ComponentModel", "DecimalConverter" }, // DecimalConverter
        { 131, 3, "System.Windows.Media.Animation", "DecimalKeyFrame" }, // DecimalKeyFrame
        { 132, 3, "System.Windows.Media.Animation", "DecimalKeyFrameCollection" }, // DecimalKeyFrameCollection
        { 133, 4, "System.Windows.Controls", "Decorator" }, // Decorator
        { 134, 4, "System.Windows.Controls", "DefinitionBase" }, // DefinitionBase
        { 135, 2, "System.Windows", "DependencyObject" }, // DependencyObject
        { 136, 2, "System.Windows", "DependencyProperty" }, // DependencyProperty
        { 137, 4, "System.Windows.Markup", "DependencyPropertyConverter" }, // DependencyPropertyConverter
        { 138, 4, "System.Windows", "DialogResultConverter" }, // DialogResultConverter
        { 139, 3, "System.Windows.Media.Media3D", "DiffuseMaterial" }, // DiffuseMaterial
        { 140, 3, "System.Windows.Media.Media3D", "DirectionalLight" }, // DirectionalLight
        { 141, 3, "System.Windows.Media.Animation", "DiscreteBooleanKeyFrame" }, // DiscreteBooleanKeyFrame
        { 142, 3, "System.Windows.Media.Animation", "DiscreteByteKeyFrame" }, // DiscreteByteKeyFrame
        { 143, 3, "System.Windows.Media.Animation", "DiscreteCharKeyFrame" }, // DiscreteCharKeyFrame
        { 144, 3, "System.Windows.Media.Animation", "DiscreteColorKeyFrame" }, // DiscreteColorKeyFrame
        { 145, 3, "System.Windows.Media.Animation", "DiscreteDecimalKeyFrame" }, // DiscreteDecimalKeyFrame
        { 146, 3, "System.Windows.Media.Animation", "DiscreteDoubleKeyFrame" }, // DiscreteDoubleKeyFrame
        { 147, 3, "System.Windows.Media.Animation", "DiscreteInt16KeyFrame" }, // DiscreteInt16KeyFrame
        { 148, 3, "System.Windows.Media.Animation", "DiscreteInt32KeyFrame" }, // DiscreteInt32KeyFrame
        { 149, 3, "System.Windows.Media.Animation", "DiscreteInt64KeyFrame" }, // DiscreteInt64KeyFrame
        { 150, 3, "System.Windows.Media.Animation", "DiscreteMatrixKeyFrame" }, // DiscreteMatrixKeyFrame
        { 151, 3, "System.Windows.Media.Animation", "DiscreteObjectKeyFrame" }, // DiscreteObjectKeyFrame
        { 152, 3, "System.Windows.Media.Animation", "DiscretePoint3DKeyFrame" }, // DiscretePoint3DKeyFrame
        { 153, 3, "System.Windows.Media.Animation", "DiscretePointKeyFrame" }, // DiscretePointKeyFrame
        { 154, 3, "System.Windows.Media.Animation", "DiscreteQuaternionKeyFrame" }, // DiscreteQuaternionKeyFrame
        { 155, 3, "System.Windows.Media.Animation", "DiscreteRectKeyFrame" }, // DiscreteRectKeyFrame
        { 156, 3, "System.Windows.Media.Animation", "DiscreteRotation3DKeyFrame" }, // DiscreteRotation3DKeyFrame
        { 157, 3, "System.Windows.Media.Animation", "DiscreteSingleKeyFrame" }, // DiscreteSingleKeyFrame
        { 158, 3, "System.Windows.Media.Animation", "DiscreteSizeKeyFrame" }, // DiscreteSizeKeyFrame
        { 159, 3, "System.Windows.Media.Animation", "DiscreteStringKeyFrame" }, // DiscreteStringKeyFrame
        { 160, 4, "System.Windows.Media.Animation", "DiscreteThicknessKeyFrame" }, // DiscreteThicknessKeyFrame
        { 161, 3, "System.Windows.Media.Animation", "DiscreteVector3DKeyFrame" }, // DiscreteVector3DKeyFrame
        { 162, 3, "System.Windows.Media.Animation", "DiscreteVectorKeyFrame" }, // DiscreteVectorKeyFrame
        { 163, 4, "System.Windows.Controls", "DockPanel" }, // DockPanel
        { 164, 4, "System.Windows.Controls.Primitives", "DocumentPageView" }, // DocumentPageView
        { 165, 4, "System.Windows.Documents", "DocumentReference" }, // DocumentReference
        { 166, 4, "System.Windows.Controls", "DocumentViewer" }, // DocumentViewer
        { 167, 4, "System.Windows.Controls.Primitives", "DocumentViewerBase" }, // DocumentViewerBase
        { 168, 0, "System", "Double" }, // Double
        { 169, 3, "System.Windows.Media.Animation", "DoubleAnimation" }, // DoubleAnimation
        { 170, 3, "System.Windows.Media.Animation", "DoubleAnimationBase" }, // DoubleAnimationBase
        { 171, 3, "System.Windows.Media.Animation", "DoubleAnimationUsingKeyFrames" }, // DoubleAnimationUsingKeyFrames
        { 172, 3, "System.Windows.Media.Animation", "DoubleAnimationUsingPath" }, // DoubleAnimationUsingPath
        { 173, 3, "System.Windows.Media", "DoubleCollection" }, // DoubleCollection
        { 174, 3, "System.Windows.Media", "DoubleCollectionConverter" }, // DoubleCollectionConverter
        { 175, 1, "System.ComponentModel", "DoubleConverter" }, // DoubleConverter
        { 176, 3, "System.Windows.Media.Converters", "DoubleIListConverter" }, // DoubleIListConverter
        { 177, 3, "System.Windows.Media.Animation", "DoubleKeyFrame" }, // DoubleKeyFrame
        { 178, 3, "System.Windows.Media.Animation", "DoubleKeyFrameCollection" }, // DoubleKeyFrameCollection
        { 179, 3, "System.Windows.Media", "Drawing" }, // Drawing
        { 180, 3, "System.Windows.Media", "DrawingBrush" }, // DrawingBrush
        { 181, 3, "System.Windows.Media", "DrawingCollection" }, // DrawingCollection
        { 182, 3, "System.Windows.Media", "DrawingContext" }, // DrawingContext
        { 183, 3, "System.Windows.Media", "DrawingGroup" }, // DrawingGroup
        { 184, 3, "System.Windows.Media", "DrawingImage" }, // DrawingImage
        { 185, 3, "System.Windows.Media", "DrawingVisual" }, // DrawingVisual
        { 186, 3, "System.Windows.Media.Effects", "DropShadowBitmapEffect" }, // DropShadowBitmapEffect
        { 187, 3, "System.Windows", "Duration" }, // Duration
        { 188, 3, "System.Windows", "DurationConverter" }, // DurationConverter
        { 189, 4, "System.Windows", "DynamicResourceExtension" }, // DynamicResourceExtension
        { 190, 4, "System.Windows", "DynamicResourceExtensionConverter" }, // DynamicResourceExtensionConverter
        { 191, 4, "System.Windows.Shapes", "Ellipse" }, // Ellipse
        { 192, 3, "System.Windows.Media", "EllipseGeometry" }, // EllipseGeometry
        { 193, 3, "System.Windows.Media.Effects", "EmbossBitmapEffect" }, // EmbossBitmapEffect
        { 194, 3, "System.Windows.Media.Media3D", "EmissiveMaterial" }, // EmissiveMaterial
        { 195, 1, "System.ComponentModel", "EnumConverter" }, // EnumConverter
        { 196, 3, "System.Windows", "EventManager" }, // EventManager
        { 197, 4, "System.Windows", "EventSetter" }, // EventSetter
        { 198, 4, "System.Windows", "EventTrigger" }, // EventTrigger
        { 199, 4, "System.Windows.Controls", "Expander" }, // Expander
        { 200, 2, "System.Windows", "Expression" }, // Expression
        { 201, 2, "System.Windows", "ExpressionConverter" }, // ExpressionConverter
        { 202, 4, "System.Windows.Documents", "Figure" }, // Figure
        { 203, 4, "System.Windows", "FigureLength" }, // FigureLength
        { 204, 4, "System.Windows", "FigureLengthConverter" }, // FigureLengthConverter
        { 205, 4, "System.Windows.Documents", "FixedDocument" }, // FixedDocument
        { 206, 4, "System.Windows.Documents", "FixedDocumentSequence" }, // FixedDocumentSequence
        { 207, 4, "System.Windows.Documents", "FixedPage" }, // FixedPage
        { 208, 4, "System.Windows.Documents", "Floater" }, // Floater
        { 209, 4, "System.Windows.Documents", "FlowDocument" }, // FlowDocument
        { 210, 4, "System.Windows.Controls", "FlowDocumentPageViewer" }, // FlowDocumentPageViewer
        { 211, 4, "System.Windows.Controls", "FlowDocumentReader" }, // FlowDocumentReader
        { 212, 4, "System.Windows.Controls", "FlowDocumentScrollViewer" }, // FlowDocumentScrollViewer
        { 213, 3, "System.Windows.Input", "FocusManager" }, // FocusManager
        { 214, 3, "System.Windows.Media", "FontFamily" }, // FontFamily
        { 215, 3, "System.Windows.Media", "FontFamilyConverter" }, // FontFamilyConverter
        { 216, 4, "System.Windows", "FontSizeConverter" }, // FontSizeConverter
        { 217, 3, "System.Windows", "FontStretch" }, // FontStretch
        { 218, 3, "System.Windows", "FontStretchConverter" }, // FontStretchConverter
        { 219, 3, "System.Windows", "FontStyle" }, // FontStyle
        { 220, 3, "System.Windows", "FontStyleConverter" }, // FontStyleConverter
        { 221, 3, "System.Windows", "FontWeight" }, // FontWeight
        { 222, 3, "System.Windows", "FontWeightConverter" }, // FontWeightConverter
        { 223, 3, "System.Windows.Media.Imaging", "FormatConvertedBitmap" }, // FormatConvertedBitmap
        { 224, 4, "System.Windows.Controls", "Frame" }, // Frame
        { 225, 4, "System.Windows", "FrameworkContentElement" }, // FrameworkContentElement
        { 226, 4, "System.Windows", "FrameworkElement" }, // FrameworkElement
        { 227, 4, "System.Windows", "FrameworkElementFactory" }, // FrameworkElementFactory
        { 228, 4, "System.Windows", "FrameworkPropertyMetadata" }, // FrameworkPropertyMetadata
        { 229, 4, "System.Windows", "FrameworkPropertyMetadataOptions" }, // FrameworkPropertyMetadataOptions
        { 230, 4, "System.Windows.Documents", "FrameworkRichTextComposition" }, // FrameworkRichTextComposition
        { 231, 4, "System.Windows", "FrameworkTemplate" }, // FrameworkTemplate
        { 232, 4, "System.Windows.Documents", "FrameworkTextComposition" }, // FrameworkTextComposition
        { 233, 2, "System.Windows", "Freezable" }, // Freezable
        { 234, 3, "System.Windows.Media", "GeneralTransform" }, // GeneralTransform
        { 235, 3, "System.Windows.Media", "GeneralTransformCollection" }, // GeneralTransformCollection
        { 236, 3, "System.Windows.Media", "GeneralTransformGroup" }, // GeneralTransformGroup
        { 237, 3, "System.Windows.Media", "Geometry" }, // Geometry
        { 238, 3, "System.Windows.Media.Media3D", "Geometry3D" }, // Geometry3D
        { 239, 3, "System.Windows.Media", "GeometryCollection" }, // GeometryCollection
        { 240, 3, "System.Windows.Media", "GeometryConverter" }, // GeometryConverter
        { 241, 3, "System.Windows.Media", "GeometryDrawing" }, // GeometryDrawing
        { 242, 3, "System.Windows.Media", "GeometryGroup" }, // GeometryGroup
        { 243, 3, "System.Windows.Media.Media3D", "GeometryModel3D" }, // GeometryModel3D
        { 244, 3, "System.Windows.Ink", "GestureRecognizer" }, // GestureRecognizer
        { 245, 3, "System.Windows.Media.Imaging", "GifBitmapDecoder" }, // GifBitmapDecoder
        { 246, 3, "System.Windows.Media.Imaging", "GifBitmapEncoder" }, // GifBitmapEncoder
        { 247, 3, "System.Windows.Media", "GlyphRun" }, // GlyphRun
        { 248, 3, "System.Windows.Media", "GlyphRunDrawing" }, // GlyphRunDrawing
        { 249, 3, "System.Windows.Media", "GlyphTypeface" }, // GlyphTypeface
        { 250, 4, "System.Windows.Documents", "Glyphs" }, // Glyphs
        { 251, 3, "System.Windows.Media", "GradientBrush" }, // GradientBrush
        { 252, 3, "System.Windows.Media", "GradientStop" }, // GradientStop
        { 253, 3, "System.Windows.Media", "GradientStopCollection" }, // GradientStopCollection
        { 254, 4, "System.Windows.Controls", "Grid" }, // Grid
        { 255, 4, "System.Windows", "GridLength" }, // GridLength
        { 256, 4, "System.Windows", "GridLengthConverter" }, // GridLengthConverter
        { 257, 4, "System.Windows.Controls", "GridSplitter" }, // GridSplitter
        { 258, 4, "System.Windows.Controls", "GridView" }, // GridView
        { 259, 4, "System.Windows.Controls", "GridViewColumn" }, // GridViewColumn
        { 260, 4, "System.Windows.Controls", "GridViewColumnHeader" }, // GridViewColumnHeader
        { 261, 4, "System.Windows.Controls", "GridViewHeaderRowPresenter" }, // GridViewHeaderRowPresenter
        { 262, 4, "System.Windows.Controls", "GridViewRowPresenter" }, // GridViewRowPresenter
        { 263, 4, "System.Windows.Controls.Primitives", "GridViewRowPresenterBase" }, // GridViewRowPresenterBase
        { 264, 4, "System.Windows.Controls", "GroupBox" }, // GroupBox
        { 265, 4, "System.Windows.Controls", "GroupItem" }, // GroupItem
        { 266, 0, "System", "Guid" }, // Guid
        { 267, 1, "System.ComponentModel", "GuidConverter" }, // GuidConverter
        { 268, 3, "System.Windows.Media", "GuidelineSet" }, // GuidelineSet
        { 269, 4, "System.Windows.Controls", "HeaderedContentControl" }, // HeaderedContentControl
        { 270, 4, "System.Windows.Controls", "HeaderedItemsControl" }, // HeaderedItemsControl
        { 271, 4, "System.Windows", "HierarchicalDataTemplate" }, // HierarchicalDataTemplate
        { 272, 3, "System.Windows.Media", "HostVisual" }, // HostVisual
        { 273, 4, "System.Windows.Documents", "Hyperlink" }, // Hyperlink
        { 274, 3, "System.Windows.Markup", "IAddChild" }, // IAddChild
        { 275, 3, "System.Windows.Markup", "IAddChildInternal" }, // IAddChildInternal
        { 276, 3, "System.Windows.Input", "ICommand" }, // ICommand
        { 277, 2, "System.Windows.Markup", "IComponentConnector" }, // IComponentConnector
        { 278, 2, "System.Windows.Markup", "INameScope" }, // INameScope
        { 279, 4, "System.Windows.Markup", "IStyleConnector" }, // IStyleConnector
        { 280, 3, "System.Windows.Media.Imaging", "IconBitmapDecoder" }, // IconBitmapDecoder
        { 281, 4, "System.Windows.Controls", "Image" }, // Image
        { 282, 3, "System.Windows.Media", "ImageBrush" }, // ImageBrush
        { 283, 3, "System.Windows.Media", "ImageDrawing" }, // ImageDrawing
        { 284, 3, "System.Windows.Media", "ImageMetadata" }, // ImageMetadata
        { 285, 3, "System.Windows.Media", "ImageSource" }, // ImageSource
        { 286, 3, "System.Windows.Media", "ImageSourceConverter" }, // ImageSourceConverter
        { 287, 3, "System.Windows.Media.Imaging", "InPlaceBitmapMetadataWriter" }, // InPlaceBitmapMetadataWriter
        { 288, 4, "System.Windows.Controls", "InkCanvas" }, // InkCanvas
        { 289, 4, "System.Windows.Controls", "InkPresenter" }, // InkPresenter
        { 290, 4, "System.Windows.Documents", "Inline" }, // Inline
        { 291, 4, "System.Windows.Documents", "InlineCollection" }, // InlineCollection
        { 292, 4, "System.Windows.Documents", "InlineUIContainer" }, // InlineUIContainer
        { 293, 3, "System.Windows.Input", "InputBinding" }, // InputBinding
        { 294, 3, "System.Windows.Input", "InputDevice" }, // InputDevice
        { 295, 3, "System.Windows.Input", "InputLanguageManager" }, // InputLanguageManager
        { 296, 3, "System.Windows.Input", "InputManager" }, // InputManager
        { 297, 3, "System.Windows.Input", "InputMethod" }, // InputMethod
        { 298, 3, "System.Windows.Input", "InputScope" }, // InputScope
        { 299, 3, "System.Windows.Input", "InputScopeConverter" }, // InputScopeConverter
        { 300, 3, "System.Windows.Input", "InputScopeName" }, // InputScopeName
        { 301, 3, "System.Windows.Input", "InputScopeNameConverter" }, // InputScopeNameConverter
        { 302, 0, "System", "Int16" }, // Int16
        { 303, 3, "System.Windows.Media.Animation", "Int16Animation" }, // Int16Animation
        { 304, 3, "System.Windows.Media.Animation", "Int16AnimationBase" }, // Int16AnimationBase
        { 305, 3, "System.Windows.Media.Animation", "Int16AnimationUsingKeyFrames" }, // Int16AnimationUsingKeyFrames
        { 306, 1, "System.ComponentModel", "Int16Converter" }, // Int16Converter
        { 307, 3, "System.Windows.Media.Animation", "Int16KeyFrame" }, // Int16KeyFrame
        { 308, 3, "System.Windows.Media.Animation", "Int16KeyFrameCollection" }, // Int16KeyFrameCollection
        { 309, 0, "System", "Int32" }, // Int32
        { 310, 3, "System.Windows.Media.Animation", "Int32Animation" }, // Int32Animation
        { 311, 3, "System.Windows.Media.Animation", "Int32AnimationBase" }, // Int32AnimationBase
        { 312, 3, "System.Windows.Media.Animation", "Int32AnimationUsingKeyFrames" }, // Int32AnimationUsingKeyFrames
        { 313, 3, "System.Windows.Media", "Int32Collection" }, // Int32Collection
        { 314, 3, "System.Windows.Media", "Int32CollectionConverter" }, // Int32CollectionConverter
        { 315, 1, "System.ComponentModel", "Int32Converter" }, // Int32Converter
        { 316, 3, "System.Windows.Media.Animation", "Int32KeyFrame" }, // Int32KeyFrame
        { 317, 3, "System.Windows.Media.Animation", "Int32KeyFrameCollection" }, // Int32KeyFrameCollection
        { 318, 2, "System.Windows", "Int32Rect" }, // Int32Rect
        { 319, 2, "System.Windows", "Int32RectConverter" }, // Int32RectConverter
        { 320, 0, "System", "Int64" }, // Int64
        { 321, 3, "System.Windows.Media.Animation", "Int64Animation" }, // Int64Animation
        { 322, 3, "System.Windows.Media.Animation", "Int64AnimationBase" }, // Int64AnimationBase
        { 323, 3, "System.Windows.Media.Animation", "Int64AnimationUsingKeyFrames" }, // Int64AnimationUsingKeyFrames
        { 324, 1, "System.ComponentModel", "Int64Converter" }, // Int64Converter
        { 325, 3, "System.Windows.Media.Animation", "Int64KeyFrame" }, // Int64KeyFrame
        { 326, 3, "System.Windows.Media.Animation", "Int64KeyFrameCollection" }, // Int64KeyFrameCollection
        { 327, 4, "System.Windows.Documents", "Italic" }, // Italic
        { 328, 4, "System.Windows.Controls", "ItemCollection" }, // ItemCollection
        { 329, 4, "System.Windows.Controls", "ItemsControl" }, // ItemsControl
        { 330, 4, "System.Windows.Controls", "ItemsPanelTemplate" }, // ItemsPanelTemplate
        { 331, 4, "System.Windows.Controls", "ItemsPresenter" }, // ItemsPresenter
        { 332, 4, "System.Windows.Navigation", "JournalEntry" }, // JournalEntry
        { 333, 4, "System.Windows.Navigation", "JournalEntryListConverter" }, // JournalEntryListConverter
        { 334, 4, "System.Windows.Navigation", "JournalEntryUnifiedViewConverter" }, // JournalEntryUnifiedViewConverter
        { 335, 3, "System.Windows.Media.Imaging", "JpegBitmapDecoder" }, // JpegBitmapDecoder
        { 336, 3, "System.Windows.Media.Imaging", "JpegBitmapEncoder" }, // JpegBitmapEncoder
        { 337, 3, "System.Windows.Input", "KeyBinding" }, // KeyBinding
        { 338, 2, "System.Windows.Input", "KeyConverter" }, // KeyConverter
        { 339, 3, "System.Windows.Input", "KeyGesture" }, // KeyGesture
        { 340, 3, "System.Windows.Input", "KeyGestureConverter" }, // KeyGestureConverter
        { 341, 3, "System.Windows.Media.Animation", "KeySpline" }, // KeySpline
        { 342, 3, "System.Windows", "KeySplineConverter" }, // KeySplineConverter
        { 343, 3, "System.Windows.Media.Animation", "KeyTime" }, // KeyTime
        { 344, 3, "System.Windows", "KeyTimeConverter" }, // KeyTimeConverter
        { 345, 3, "System.Windows.Input", "KeyboardDevice" }, // KeyboardDevice
        { 346, 4, "System.Windows.Controls", "Label" }, // Label
        { 347, 3, "System.Windows.Media.Imaging", "LateBoundBitmapDecoder" }, // LateBoundBitmapDecoder
        { 348, 4, "System.Windows", "LengthConverter" }, // LengthConverter
        { 349, 3, "System.Windows.Media.Media3D", "Light" }, // Light
        { 350, 4, "System.Windows.Shapes", "Line" }, // Line
        { 351, 4, "System.Windows.Documents", "LineBreak" }, // LineBreak
        { 352, 3, "System.Windows.Media", "LineGeometry" }, // LineGeometry
        { 353, 3, "System.Windows.Media", "LineSegment" }, // LineSegment
        { 354, 3, "System.Windows.Media.Animation", "LinearByteKeyFrame" }, // LinearByteKeyFrame
        { 355, 3, "System.Windows.Media.Animation", "LinearColorKeyFrame" }, // LinearColorKeyFrame
        { 356, 3, "System.Windows.Media.Animation", "LinearDecimalKeyFrame" }, // LinearDecimalKeyFrame
        { 357, 3, "System.Windows.Media.Animation", "LinearDoubleKeyFrame" }, // LinearDoubleKeyFrame
        { 358, 3, "System.Windows.Media", "LinearGradientBrush" }, // LinearGradientBrush
        { 359, 3, "System.Windows.Media.Animation", "LinearInt16KeyFrame" }, // LinearInt16KeyFrame
        { 360, 3, "System.Windows.Media.Animation", "LinearInt32KeyFrame" }, // LinearInt32KeyFrame
        { 361, 3, "System.Windows.Media.Animation", "LinearInt64KeyFrame" }, // LinearInt64KeyFrame
        { 362, 3, "System.Windows.Media.Animation", "LinearPoint3DKeyFrame" }, // LinearPoint3DKeyFrame
        { 363, 3, "System.Windows.Media.Animation", "LinearPointKeyFrame" }, // LinearPointKeyFrame
        { 364, 3, "System.Windows.Media.Animation", "LinearQuaternionKeyFrame" }, // LinearQuaternionKeyFrame
        { 365, 3, "System.Windows.Media.Animation", "LinearRectKeyFrame" }, // LinearRectKeyFrame
        { 366, 3, "System.Windows.Media.Animation", "LinearRotation3DKeyFrame" }, // LinearRotation3DKeyFrame
        { 367, 3, "System.Windows.Media.Animation", "LinearSingleKeyFrame" }, // LinearSingleKeyFrame
        { 368, 3, "System.Windows.Media.Animation", "LinearSizeKeyFrame" }, // LinearSizeKeyFrame
        { 369, 4, "System.Windows.Media.Animation", "LinearThicknessKeyFrame" }, // LinearThicknessKeyFrame
        { 370, 3, "System.Windows.Media.Animation", "LinearVector3DKeyFrame" }, // LinearVector3DKeyFrame
        { 371, 3, "System.Windows.Media.Animation", "LinearVectorKeyFrame" }, // LinearVectorKeyFrame
        { 372, 4, "System.Windows.Documents", "List" }, // List
        { 373, 4, "System.Windows.Controls", "ListBox" }, // ListBox
        { 374, 4, "System.Windows.Controls", "ListBoxItem" }, // ListBoxItem
        { 375, 4, "System.Windows.Data", "ListCollectionView" }, // ListCollectionView
        { 376, 4, "System.Windows.Documents", "ListItem" }, // ListItem
        { 377, 4, "System.Windows.Controls", "ListView" }, // ListView
        { 378, 4, "System.Windows.Controls", "ListViewItem" }, // ListViewItem
        { 379, 4, "System.Windows", "Localization" }, // Localization
        { 380, 4, "System.Windows", "LostFocusEventManager" }, // LostFocusEventManager
        { 381, 2, "System.Windows.Markup", "MarkupExtension" }, // MarkupExtension
        { 382, 3, "System.Windows.Media.Media3D", "Material" }, // Material
        { 383, 3, "System.Windows.Media.Media3D", "MaterialCollection" }, // MaterialCollection
        { 384, 3, "System.Windows.Media.Media3D", "MaterialGroup" }, // MaterialGroup
        { 385, 2, "System.Windows.Media", "Matrix" }, // Matrix
        { 386, 3, "System.Windows.Media.Media3D", "Matrix3D" }, // Matrix3D
        { 387, 3, "System.Windows.Media.Media3D", "Matrix3DConverter" }, // Matrix3DConverter
        { 388, 3, "System.Windows.Media.Animation", "MatrixAnimationBase" }, // MatrixAnimationBase
        { 389, 3, "System.Windows.Media.Animation", "MatrixAnimationUsingKeyFrames" }, // MatrixAnimationUsingKeyFrames
        { 390, 3, "System.Windows.Media.Animation", "MatrixAnimationUsingPath" }, // MatrixAnimationUsingPath
        { 391, 3, "System.Windows.Media.Media3D", "MatrixCamera" }, // MatrixCamera
        { 392, 2, "System.Windows.Media", "MatrixConverter" }, // MatrixConverter
        { 393, 3, "System.Windows.Media.Animation", "MatrixKeyFrame" }, // MatrixKeyFrame
        { 394, 3, "System.Windows.Media.Animation", "MatrixKeyFrameCollection" }, // MatrixKeyFrameCollection
        { 395, 3, "System.Windows.Media", "MatrixTransform" }, // MatrixTransform
        { 396, 3, "System.Windows.Media.Media3D", "MatrixTransform3D" }, // MatrixTransform3D
        { 397, 3, "System.Windows.Media", "MediaClock" }, // MediaClock
        { 398, 4, "System.Windows.Controls", "MediaElement" }, // MediaElement
        { 399, 3, "System.Windows.Media", "MediaPlayer" }, // MediaPlayer
        { 400, 3, "System.Windows.Media", "MediaTimeline" }, // MediaTimeline
        { 401, 4, "System.Windows.Controls", "Menu" }, // Menu
        { 402, 4, "System.Windows.Controls.Primitives", "MenuBase" }, // MenuBase
        { 403, 4, "System.Windows.Controls", "MenuItem" }, // MenuItem
        { 404, 4, "System.Windows.Controls", "MenuScrollingVisibilityConverter" }, // MenuScrollingVisibilityConverter
        { 405, 3, "System.Windows.Media.Media3D", "MeshGeometry3D" }, // MeshGeometry3D
        { 406, 3, "System.Windows.Media.Media3D", "Model3D" }, // Model3D
        { 407, 3, "System.Windows.Media.Media3D", "Model3DCollection" }, // Model3DCollection
        { 408, 3, "System.Windows.Media.Media3D", "Model3DGroup" }, // Model3DGroup
        { 409, 3, "System.Windows.Media.Media3D", "ModelVisual3D" }, // ModelVisual3D
        { 410, 2, "System.Windows.Input", "ModifierKeysConverter" }, // ModifierKeysConverter
        { 411, 3, "System.Windows.Input", "MouseActionConverter" }, // MouseActionConverter
        { 412, 3, "System.Windows.Input", "MouseBinding" }, // MouseBinding
        { 413, 3, "System.Windows.Input", "MouseDevice" }, // MouseDevice
        { 414, 3, "System.Windows.Input", "MouseGesture" }, // MouseGesture
        { 415, 3, "System.Windows.Input", "MouseGestureConverter" }, // MouseGestureConverter
        { 416, 4, "System.Windows.Data", "MultiBinding" }, // MultiBinding
        { 417, 4, "System.Windows.Data", "MultiBindingExpression" }, // MultiBindingExpression
        { 418, 4, "System.Windows", "MultiDataTrigger" }, // MultiDataTrigger
        { 419, 4, "System.Windows", "MultiTrigger" }, // MultiTrigger
        { 420, 4, "System.Windows", "NameScope" }, // NameScope
        { 421, 4, "System.Windows.Navigation", "NavigationWindow" }, // NavigationWindow
        { 422, 4, "System.Windows.Markup", "NullExtension" }, // NullExtension
        { 423, 4, "System.Windows", "NullableBoolConverter" }, // NullableBoolConverter
        { 424, 1, "System.ComponentModel", "NullableConverter" }, // NullableConverter
        { 425, 3, "System.Windows.Media", "NumberSubstitution" }, // NumberSubstitution
        { 426, 0, "System", "Object" }, // Object
        { 427, 3, "System.Windows.Media.Animation", "ObjectAnimationBase" }, // ObjectAnimationBase
        { 428, 3, "System.Windows.Media.Animation", "ObjectAnimationUsingKeyFrames" }, // ObjectAnimationUsingKeyFrames
        { 429, 4, "System.Windows.Data", "ObjectDataProvider" }, // ObjectDataProvider
        { 430, 3, "System.Windows.Media.Animation", "ObjectKeyFrame" }, // ObjectKeyFrame
        { 431, 3, "System.Windows.Media.Animation", "ObjectKeyFrameCollection" }, // ObjectKeyFrameCollection
        { 432, 3, "System.Windows.Media.Media3D", "OrthographicCamera" }, // OrthographicCamera
        { 433, 3, "System.Windows.Media.Effects", "OuterGlowBitmapEffect" }, // OuterGlowBitmapEffect
        { 434, 4, "System.Windows.Controls", "Page" }, // Page
        { 435, 4, "System.Windows.Documents", "PageContent" }, // PageContent
        { 436, 4, "System.Windows.Navigation", "PageFunctionBase" }, // PageFunctionBase
        { 437, 4, "System.Windows.Controls", "Panel" }, // Panel
        { 438, 4, "System.Windows.Documents", "Paragraph" }, // Paragraph
        { 439, 3, "System.Windows.Media.Animation", "ParallelTimeline" }, // ParallelTimeline
        { 440, 4, "System.Windows.Markup", "ParserContext" }, // ParserContext
        { 441, 4, "System.Windows.Controls", "PasswordBox" }, // PasswordBox
        { 442, 4, "System.Windows.Shapes", "Path" }, // Path
        { 443, 3, "System.Windows.Media", "PathFigure" }, // PathFigure
        { 444, 3, "System.Windows.Media", "PathFigureCollection" }, // PathFigureCollection
        { 445, 3, "System.Windows.Media", "PathFigureCollectionConverter" }, // PathFigureCollectionConverter
        { 446, 3, "System.Windows.Media", "PathGeometry" }, // PathGeometry
        { 447, 3, "System.Windows.Media", "PathSegment" }, // PathSegment
        { 448, 3, "System.Windows.Media", "PathSegmentCollection" }, // PathSegmentCollection
        { 449, 4, "System.Windows.Media.Animation", "PauseStoryboard" }, // PauseStoryboard
        { 450, 3, "System.Windows.Media", "Pen" }, // Pen
        { 451, 3, "System.Windows.Media.Media3D", "PerspectiveCamera" }, // PerspectiveCamera
        { 452, 3, "System.Windows.Media", "PixelFormat" }, // PixelFormat
        { 453, 3, "System.Windows.Media", "PixelFormatConverter" }, // PixelFormatConverter
        { 454, 3, "System.Windows.Media.Imaging", "PngBitmapDecoder" }, // PngBitmapDecoder
        { 455, 3, "System.Windows.Media.Imaging", "PngBitmapEncoder" }, // PngBitmapEncoder
        { 456, 2, "System.Windows", "Point" }, // Point
        { 457, 3, "System.Windows.Media.Media3D", "Point3D" }, // Point3D
        { 458, 3, "System.Windows.Media.Animation", "Point3DAnimation" }, // Point3DAnimation
        { 459, 3, "System.Windows.Media.Animation", "Point3DAnimationBase" }, // Point3DAnimationBase
        { 460, 3, "System.Windows.Media.Animation", "Point3DAnimationUsingKeyFrames" }, // Point3DAnimationUsingKeyFrames
        { 461, 3, "System.Windows.Media.Media3D", "Point3DCollection" }, // Point3DCollection
        { 462, 3, "System.Windows.Media.Media3D", "Point3DCollectionConverter" }, // Point3DCollectionConverter
        { 463, 3, "System.Windows.Media.Media3D", "Point3DConverter" }, // Point3DConverter
        { 464, 3, "System.Windows.Media.Animation", "Point3DKeyFrame" }, // Point3DKeyFrame
        { 465, 3, "System.Windows.Media.Animation", "Point3DKeyFrameCollection" }, // Point3DKeyFrameCollection
        { 466, 3, "System.Windows.Media.Media3D", "Point4D" }, // Point4D
        { 467, 3, "System.Windows.Media.Media3D", "Point4DConverter" }, // Point4DConverter
        { 468, 3, "System.Windows.Media.Animation", "PointAnimation" }, // PointAnimation
        { 469, 3, "System.Windows.Media.Animation", "PointAnimationBase" }, // PointAnimationBase
        { 470, 3, "System.Windows.Media.Animation", "PointAnimationUsingKeyFrames" }, // PointAnimationUsingKeyFrames
        { 471, 3, "System.Windows.Media.Animation", "PointAnimationUsingPath" }, // PointAnimationUsingPath
        { 472, 3, "System.Windows.Media", "PointCollection" }, // PointCollection
        { 473, 3, "System.Windows.Media", "PointCollectionConverter" }, // PointCollectionConverter
        { 474, 2, "System.Windows", "PointConverter" }, // PointConverter
        { 475, 3, "System.Windows.Media.Converters", "PointIListConverter" }, // PointIListConverter
        { 476, 3, "System.Windows.Media.Animation", "PointKeyFrame" }, // PointKeyFrame
        { 477, 3, "System.Windows.Media.Animation", "PointKeyFrameCollection" }, // PointKeyFrameCollection
        { 478, 3, "System.Windows.Media.Media3D", "PointLight" }, // PointLight
        { 479, 3, "System.Windows.Media.Media3D", "PointLightBase" }, // PointLightBase
        { 480, 3, "System.Windows.Media", "PolyBezierSegment" }, // PolyBezierSegment
        { 481, 3, "System.Windows.Media", "PolyLineSegment" }, // PolyLineSegment
        { 482, 3, "System.Windows.Media", "PolyQuadraticBezierSegment" }, // PolyQuadraticBezierSegment
        { 483, 4, "System.Windows.Shapes", "Polygon" }, // Polygon
        { 484, 4, "System.Windows.Shapes", "Polyline" }, // Polyline
        { 485, 4, "System.Windows.Controls.Primitives", "Popup" }, // Popup
        { 486, 3, "System.Windows", "PresentationSource" }, // PresentationSource
        { 487, 4, "System.Windows.Data", "PriorityBinding" }, // PriorityBinding
        { 488, 4, "System.Windows.Data", "PriorityBindingExpression" }, // PriorityBindingExpression
        { 489, 4, "System.Windows.Controls", "ProgressBar" }, // ProgressBar
        { 490, 3, "System.Windows.Media.Media3D", "ProjectionCamera" }, // ProjectionCamera
        { 491, 4, "System.Windows", "PropertyPath" }, // PropertyPath
        { 492, 4, "System.Windows", "PropertyPathConverter" }, // PropertyPathConverter
        { 493, 3, "System.Windows.Media", "QuadraticBezierSegment" }, // QuadraticBezierSegment
        { 494, 3, "System.Windows.Media.Media3D", "Quaternion" }, // Quaternion
        { 495, 3, "System.Windows.Media.Animation", "QuaternionAnimation" }, // QuaternionAnimation
        { 496, 3, "System.Windows.Media.Animation", "QuaternionAnimationBase" }, // QuaternionAnimationBase
        { 497, 3, "System.Windows.Media.Animation", "QuaternionAnimationUsingKeyFrames" }, // QuaternionAnimationUsingKeyFrames
        { 498, 3, "System.Windows.Media.Media3D", "QuaternionConverter" }, // QuaternionConverter
        { 499, 3, "System.Windows.Media.Animation", "QuaternionKeyFrame" }, // QuaternionKeyFrame
        { 500, 3, "System.Windows.Media.Animation", "QuaternionKeyFrameCollection" }, // QuaternionKeyFrameCollection
        { 501, 3, "System.Windows.Media.Media3D", "QuaternionRotation3D" }, // QuaternionRotation3D
        { 502, 3, "System.Windows.Media", "RadialGradientBrush" }, // RadialGradientBrush
        { 503, 4, "System.Windows.Controls", "RadioButton" }, // RadioButton
        { 504, 4, "System.Windows.Controls.Primitives", "RangeBase" }, // RangeBase
        { 505, 2, "System.Windows", "Rect" }, // Rect
        { 506, 3, "System.Windows.Media.Media3D", "Rect3D" }, // Rect3D
        { 507, 3, "System.Windows.Media.Media3D", "Rect3DConverter" }, // Rect3DConverter
        { 508, 3, "System.Windows.Media.Animation", "RectAnimation" }, // RectAnimation
        { 509, 3, "System.Windows.Media.Animation", "RectAnimationBase" }, // RectAnimationBase
        { 510, 3, "System.Windows.Media.Animation", "RectAnimationUsingKeyFrames" }, // RectAnimationUsingKeyFrames
        { 511, 2, "System.Windows", "RectConverter" }, // RectConverter
        { 512, 3, "System.Windows.Media.Animation", "RectKeyFrame" }, // RectKeyFrame
        { 513, 3, "System.Windows.Media.Animation", "RectKeyFrameCollection" }, // RectKeyFrameCollection
        { 514, 4, "System.Windows.Shapes", "Rectangle" }, // Rectangle
        { 515, 3, "System.Windows.Media", "RectangleGeometry" }, // RectangleGeometry
        { 516, 4, "System.Windows.Data", "RelativeSource" }, // RelativeSource
        { 517, 4, "System.Windows.Media.Animation", "RemoveStoryboard" }, // RemoveStoryboard
        { 518, 3, "System.Windows.Media", "RenderOptions" }, // RenderOptions
        { 519, 3, "System.Windows.Media.Imaging", "RenderTargetBitmap" }, // RenderTargetBitmap
        { 520, 3, "System.Windows.Media.Animation", "RepeatBehavior" }, // RepeatBehavior
        { 521, 3, "System.Windows.Media.Animation", "RepeatBehaviorConverter" }, // RepeatBehaviorConverter
        { 522, 4, "System.Windows.Controls.Primitives", "RepeatButton" }, // RepeatButton
        { 523, 4, "System.Windows.Controls.Primitives", "ResizeGrip" }, // ResizeGrip
        { 524, 4, "System.Windows", "ResourceDictionary" }, // ResourceDictionary
        { 525, 4, "System.Windows", "ResourceKey" }, // ResourceKey
        { 526, 4, "System.Windows.Media.Animation", "ResumeStoryboard" }, // ResumeStoryboard
        { 527, 4, "System.Windows.Controls", "RichTextBox" }, // RichTextBox
        { 528, 3, "System.Windows.Media", "RotateTransform" }, // RotateTransform
        { 529, 3, "System.Windows.Media.Media3D", "RotateTransform3D" }, // RotateTransform3D
        { 530, 3, "System.Windows.Media.Media3D", "Rotation3D" }, // Rotation3D
        { 531, 3, "System.Windows.Media.Animation", "Rotation3DAnimation" }, // Rotation3DAnimation
        { 532, 3, "System.Windows.Media.Animation", "Rotation3DAnimationBase" }, // Rotation3DAnimationBase
        { 533, 3, "System.Windows.Media.Animation", "Rotation3DAnimationUsingKeyFrames" }, // Rotation3DAnimationUsingKeyFrames
        { 534, 3, "System.Windows.Media.Animation", "Rotation3DKeyFrame" }, // Rotation3DKeyFrame
        { 535, 3, "System.Windows.Media.Animation", "Rotation3DKeyFrameCollection" }, // Rotation3DKeyFrameCollection
        { 536, 3, "System.Windows.Input", "RoutedCommand" }, // RoutedCommand
        { 537, 3, "System.Windows", "RoutedEvent" }, // RoutedEvent
        { 538, 4, "System.Windows.Markup", "RoutedEventConverter" }, // RoutedEventConverter
        { 539, 3, "System.Windows.Input", "RoutedUICommand" }, // RoutedUICommand
        { 540, 3, "System.Windows", "RoutingStrategy" }, // RoutingStrategy
        { 541, 4, "System.Windows.Controls", "RowDefinition" }, // RowDefinition
        { 542, 4, "System.Windows.Documents", "Run" }, // Run
        { 543, 2, "System.Windows.Markup", "RuntimeNamePropertyAttribute" }, // RuntimeNamePropertyAttribute
        { 544, 0, "System", "SByte" }, // SByte
        { 545, 1, "System.ComponentModel", "SByteConverter" }, // SByteConverter
        { 546, 3, "System.Windows.Media", "ScaleTransform" }, // ScaleTransform
        { 547, 3, "System.Windows.Media.Media3D", "ScaleTransform3D" }, // ScaleTransform3D
        { 548, 4, "System.Windows.Controls.Primitives", "ScrollBar" }, // ScrollBar
        { 549, 4, "System.Windows.Controls", "ScrollContentPresenter" }, // ScrollContentPresenter
        { 550, 4, "System.Windows.Controls", "ScrollViewer" }, // ScrollViewer
        { 551, 4, "System.Windows.Documents", "Section" }, // Section
        { 552, 4, "System.Windows.Media.Animation", "SeekStoryboard" }, // SeekStoryboard
        { 553, 4, "System.Windows.Controls.Primitives", "Selector" }, // Selector
        { 554, 4, "System.Windows.Controls", "Separator" }, // Separator
        { 555, 4, "System.Windows.Media.Animation", "SetStoryboardSpeedRatio" }, // SetStoryboardSpeedRatio
        { 556, 4, "System.Windows", "Setter" }, // Setter
        { 557, 4, "System.Windows", "SetterBase" }, // SetterBase
        { 558, 4, "System.Windows.Shapes", "Shape" }, // Shape
        { 559, 0, "System", "Single" }, // Single
        { 560, 3, "System.Windows.Media.Animation", "SingleAnimation" }, // SingleAnimation
        { 561, 3, "System.Windows.Media.Animation", "SingleAnimationBase" }, // SingleAnimationBase
        { 562, 3, "System.Windows.Media.Animation", "SingleAnimationUsingKeyFrames" }, // SingleAnimationUsingKeyFrames
        { 563, 1, "System.ComponentModel", "SingleConverter" }, // SingleConverter
        { 564, 3, "System.Windows.Media.Animation", "SingleKeyFrame" }, // SingleKeyFrame
        { 565, 3, "System.Windows.Media.Animation", "SingleKeyFrameCollection" }, // SingleKeyFrameCollection
        { 566, 2, "System.Windows", "Size" }, // Size
        { 567, 3, "System.Windows.Media.Media3D", "Size3D" }, // Size3D
        { 568, 3, "System.Windows.Media.Media3D", "Size3DConverter" }, // Size3DConverter
        { 569, 3, "System.Windows.Media.Animation", "SizeAnimation" }, // SizeAnimation
        { 570, 3, "System.Windows.Media.Animation", "SizeAnimationBase" }, // SizeAnimationBase
        { 571, 3, "System.Windows.Media.Animation", "SizeAnimationUsingKeyFrames" }, // SizeAnimationUsingKeyFrames
        { 572, 2, "System.Windows", "SizeConverter" }, // SizeConverter
        { 573, 3, "System.Windows.Media.Animation", "SizeKeyFrame" }, // SizeKeyFrame
        { 574, 3, "System.Windows.Media.Animation", "SizeKeyFrameCollection" }, // SizeKeyFrameCollection
        { 575, 3, "System.Windows.Media", "SkewTransform" }, // SkewTransform
        { 576, 4, "System.Windows.Media.Animation", "SkipStoryboardToFill" }, // SkipStoryboardToFill
        { 577, 4, "System.Windows.Controls", "Slider" }, // Slider
        { 578, 3, "System.Windows.Media", "SolidColorBrush" }, // SolidColorBrush
        { 579, 4, "System.Windows.Controls", "SoundPlayerAction" }, // SoundPlayerAction
        { 580, 4, "System.Windows.Documents", "Span" }, // Span
        { 581, 3, "System.Windows.Media.Media3D", "SpecularMaterial" }, // SpecularMaterial
        { 582, 4, "System.Windows.Controls", "SpellCheck" }, // SpellCheck
        { 583, 3, "System.Windows.Media.Animation", "SplineByteKeyFrame" }, // SplineByteKeyFrame
        { 584, 3, "System.Windows.Media.Animation", "SplineColorKeyFrame" }, // SplineColorKeyFrame
        { 585, 3, "System.Windows.Media.Animation", "SplineDecimalKeyFrame" }, // SplineDecimalKeyFrame
        { 586, 3, "System.Windows.Media.Animation", "SplineDoubleKeyFrame" }, // SplineDoubleKeyFrame
        { 587, 3, "System.Windows.Media.Animation", "SplineInt16KeyFrame" }, // SplineInt16KeyFrame
        { 588, 3, "System.Windows.Media.Animation", "SplineInt32KeyFrame" }, // SplineInt32KeyFrame
        { 589, 3, "System.Windows.Media.Animation", "SplineInt64KeyFrame" }, // SplineInt64KeyFrame
        { 590, 3, "System.Windows.Media.Animation", "SplinePoint3DKeyFrame" }, // SplinePoint3DKeyFrame
        { 591, 3, "System.Windows.Media.Animation", "SplinePointKeyFrame" }, // SplinePointKeyFrame
        { 592, 3, "System.Windows.Media.Animation", "SplineQuaternionKeyFrame" }, // SplineQuaternionKeyFrame
        { 593, 3, "System.Windows.Media.Animation", "SplineRectKeyFrame" }, // SplineRectKeyFrame
        { 594, 3, "System.Windows.Media.Animation", "SplineRotation3DKeyFrame" }, // SplineRotation3DKeyFrame
        { 595, 3, "System.Windows.Media.Animation", "SplineSingleKeyFrame" }, // SplineSingleKeyFrame
        { 596, 3, "System.Windows.Media.Animation", "SplineSizeKeyFrame" }, // SplineSizeKeyFrame
        { 597, 4, "System.Windows.Media.Animation", "SplineThicknessKeyFrame" }, // SplineThicknessKeyFrame
        { 598, 3, "System.Windows.Media.Animation", "SplineVector3DKeyFrame" }, // SplineVector3DKeyFrame
        { 599, 3, "System.Windows.Media.Animation", "SplineVectorKeyFrame" }, // SplineVectorKeyFrame
        { 600, 3, "System.Windows.Media.Media3D", "SpotLight" }, // SpotLight
        { 601, 4, "System.Windows.Controls", "StackPanel" }, // StackPanel
        { 602, 4, "System.Windows.Markup", "StaticExtension" }, // StaticExtension
        { 603, 4, "System.Windows", "StaticResourceExtension" }, // StaticResourceExtension
        { 604, 4, "System.Windows.Controls.Primitives", "StatusBar" }, // StatusBar
        { 605, 4, "System.Windows.Controls.Primitives", "StatusBarItem" }, // StatusBarItem
        { 606, 4, "System.Windows.Controls", "StickyNoteControl" }, // StickyNoteControl
        { 607, 4, "System.Windows.Media.Animation", "StopStoryboard" }, // StopStoryboard
        { 608, 4, "System.Windows.Media.Animation", "Storyboard" }, // Storyboard
        { 609, 3, "System.Windows.Media", "StreamGeometry" }, // StreamGeometry
        { 610, 3, "System.Windows.Media", "StreamGeometryContext" }, // StreamGeometryContext
        { 611, 4, "System.Windows.Resources", "StreamResourceInfo" }, // StreamResourceInfo
        { 612, 0, "System", "String" }, // String
        { 613, 3, "System.Windows.Media.Animation", "StringAnimationBase" }, // StringAnimationBase
        { 614, 3, "System.Windows.Media.Animation", "StringAnimationUsingKeyFrames" }, // StringAnimationUsingKeyFrames
        { 615, 1, "System.ComponentModel", "StringConverter" }, // StringConverter
        { 616, 3, "System.Windows.Media.Animation", "StringKeyFrame" }, // StringKeyFrame
        { 617, 3, "System.Windows.Media.Animation", "StringKeyFrameCollection" }, // StringKeyFrameCollection
        { 618, 3, "System.Windows.Ink", "StrokeCollection" }, // StrokeCollection
        { 619, 3, "System.Windows", "StrokeCollectionConverter" }, // StrokeCollectionConverter
        { 620, 4, "System.Windows", "Style" }, // Style
        { 621, 3, "System.Windows.Input", "Stylus" }, // Stylus
        { 622, 3, "System.Windows.Input", "StylusDevice" }, // StylusDevice
        { 623, 4, "System.Windows.Controls", "TabControl" }, // TabControl
        { 624, 4, "System.Windows.Controls", "TabItem" }, // TabItem
        { 625, 4, "System.Windows.Controls.Primitives", "TabPanel" }, // TabPanel
        { 626, 4, "System.Windows.Documents", "Table" }, // Table
        { 627, 4, "System.Windows.Documents", "TableCell" }, // TableCell
        { 628, 4, "System.Windows.Documents", "TableColumn" }, // TableColumn
        { 629, 4, "System.Windows.Documents", "TableRow" }, // TableRow
        { 630, 4, "System.Windows.Documents", "TableRowGroup" }, // TableRowGroup
        { 631, 3, "System.Windows.Input", "TabletDevice" }, // TabletDevice
        { 632, 4, "System.Windows", "TemplateBindingExpression" }, // TemplateBindingExpression
        { 633, 4, "System.Windows", "TemplateBindingExpressionConverter" }, // TemplateBindingExpressionConverter
        { 634, 4, "System.Windows", "TemplateBindingExtension" }, // TemplateBindingExtension
        { 635, 4, "System.Windows", "TemplateBindingExtensionConverter" }, // TemplateBindingExtensionConverter
        { 636, 4, "System.Windows", "TemplateKey" }, // TemplateKey
        { 637, 4, "System.Windows.Markup", "TemplateKeyConverter" }, // TemplateKeyConverter
        { 638, 4, "System.Windows.Controls", "TextBlock" }, // TextBlock
        { 639, 4, "System.Windows.Controls", "TextBox" }, // TextBox
        { 640, 4, "System.Windows.Controls.Primitives", "TextBoxBase" }, // TextBoxBase
        { 641, 3, "System.Windows.Input", "TextComposition" }, // TextComposition
        { 642, 3, "System.Windows.Input", "TextCompositionManager" }, // TextCompositionManager
        { 643, 3, "System.Windows", "TextDecoration" }, // TextDecoration
        { 644, 3, "System.Windows", "TextDecorationCollection" }, // TextDecorationCollection
        { 645, 3, "System.Windows", "TextDecorationCollectionConverter" }, // TextDecorationCollectionConverter
        { 646, 3, "System.Windows.Media", "TextEffect" }, // TextEffect
        { 647, 3, "System.Windows.Media", "TextEffectCollection" }, // TextEffectCollection
        { 648, 4, "System.Windows.Documents", "TextElement" }, // TextElement
        { 649, 4, "System.Windows.Controls", "TextSearch" }, // TextSearch
        { 650, 4, "System.Windows", "ThemeDictionaryExtension" }, // ThemeDictionaryExtension
        { 651, 4, "System.Windows", "Thickness" }, // Thickness
        { 652, 4, "System.Windows.Media.Animation", "ThicknessAnimation" }, // ThicknessAnimation
        { 653, 4, "System.Windows.Media.Animation", "ThicknessAnimationBase" }, // ThicknessAnimationBase
        { 654, 4, "System.Windows.Media.Animation", "ThicknessAnimationUsingKeyFrames" }, // ThicknessAnimationUsingKeyFrames
        { 655, 4, "System.Windows", "ThicknessConverter" }, // ThicknessConverter
        { 656, 4, "System.Windows.Media.Animation", "ThicknessKeyFrame" }, // ThicknessKeyFrame
        { 657, 4, "System.Windows.Media.Animation", "ThicknessKeyFrameCollection" }, // ThicknessKeyFrameCollection
        { 658, 4, "System.Windows.Controls.Primitives", "Thumb" }, // Thumb
        { 659, 4, "System.Windows.Controls.Primitives", "TickBar" }, // TickBar
        { 660, 3, "System.Windows.Media.Imaging", "TiffBitmapDecoder" }, // TiffBitmapDecoder
        { 661, 3, "System.Windows.Media.Imaging", "TiffBitmapEncoder" }, // TiffBitmapEncoder
        { 662, 3, "System.Windows.Media", "TileBrush" }, // TileBrush
        { 663, 0, "System", "TimeSpan" }, // TimeSpan
        { 664, 1, "System.ComponentModel", "TimeSpanConverter" }, // TimeSpanConverter
        { 665, 3, "System.Windows.Media.Animation", "Timeline" }, // Timeline
        { 666, 3, "System.Windows.Media.Animation", "TimelineCollection" }, // TimelineCollection
        { 667, 3, "System.Windows.Media.Animation", "TimelineGroup" }, // TimelineGroup
        { 668, 4, "System.Windows.Controls.Primitives", "ToggleButton" }, // ToggleButton
        { 669, 4, "System.Windows.Controls", "ToolBar" }, // ToolBar
        { 670, 4, "System.Windows.Controls.Primitives", "ToolBarOverflowPanel" }, // ToolBarOverflowPanel
        { 671, 4, "System.Windows.Controls.Primitives", "ToolBarPanel" }, // ToolBarPanel
        { 672, 4, "System.Windows.Controls", "ToolBarTray" }, // ToolBarTray
        { 673, 4, "System.Windows.Controls", "ToolTip" }, // ToolTip
        { 674, 4, "System.Windows.Controls", "ToolTipService" }, // ToolTipService
        { 675, 4, "System.Windows.Controls.Primitives", "Track" }, // Track
        { 676, 3, "System.Windows.Media", "Transform" }, // Transform
        { 677, 3, "System.Windows.Media.Media3D", "Transform3D" }, // Transform3D
        { 678, 3, "System.Windows.Media.Media3D", "Transform3DCollection" }, // Transform3DCollection
        { 679, 3, "System.Windows.Media.Media3D", "Transform3DGroup" }, // Transform3DGroup
        { 680, 3, "System.Windows.Media", "TransformCollection" }, // TransformCollection
        { 681, 3, "System.Windows.Media", "TransformConverter" }, // TransformConverter
        { 682, 3, "System.Windows.Media", "TransformGroup" }, // TransformGroup
        { 683, 3, "System.Windows.Media.Imaging", "TransformedBitmap" }, // TransformedBitmap
        { 684, 3, "System.Windows.Media", "TranslateTransform" }, // TranslateTransform
        { 685, 3, "System.Windows.Media.Media3D", "TranslateTransform3D" }, // TranslateTransform3D
        { 686, 4, "System.Windows.Controls", "TreeView" }, // TreeView
        { 687, 4, "System.Windows.Controls", "TreeViewItem" }, // TreeViewItem
        { 688, 4, "System.Windows", "Trigger" }, // Trigger
        { 689, 4, "System.Windows", "TriggerAction" }, // TriggerAction
        { 690, 4, "System.Windows", "TriggerBase" }, // TriggerBase
        { 691, 4, "System.Windows.Markup", "TypeExtension" }, // TypeExtension
        { 692, 2, "System.Windows.Markup", "TypeTypeConverter" }, // TypeTypeConverter
        { 693, 4, "System.Windows.Documents", "Typography" }, // Typography
        { 694, 3, "System.Windows", "UIElement" }, // UIElement
        { 695, 0, "System", "UInt16" }, // UInt16
        { 696, 1, "System.ComponentModel", "UInt16Converter" }, // UInt16Converter
        { 697, 0, "System", "UInt32" }, // UInt32
        { 698, 1, "System.ComponentModel", "UInt32Converter" }, // UInt32Converter
        { 699, 0, "System", "UInt64" }, // UInt64
        { 700, 1, "System.ComponentModel", "UInt64Converter" }, // UInt64Converter
        { 701, 3, "System.Windows.Media.Converters", "UShortIListConverter" }, // UShortIListConverter
        { 702, 4, "System.Windows.Documents", "Underline" }, // Underline
        { 703, 4, "System.Windows.Controls.Primitives", "UniformGrid" }, // UniformGrid
        { 704, 1, "System", "Uri" }, // Uri
        { 705, 1, "System", "UriTypeConverter" }, // UriTypeConverter
        { 706, 4, "System.Windows.Controls", "UserControl" }, // UserControl
        { 707, 4, "System.Windows.Controls", "Validation" }, // Validation
        { 708, 2, "System.Windows", "Vector" }, // Vector
        { 709, 3, "System.Windows.Media.Media3D", "Vector3D" }, // Vector3D
        { 710, 3, "System.Windows.Media.Animation", "Vector3DAnimation" }, // Vector3DAnimation
        { 711, 3, "System.Windows.Media.Animation", "Vector3DAnimationBase" }, // Vector3DAnimationBase
        { 712, 3, "System.Windows.Media.Animation", "Vector3DAnimationUsingKeyFrames" }, // Vector3DAnimationUsingKeyFrames
        { 713, 3, "System.Windows.Media.Media3D", "Vector3DCollection" }, // Vector3DCollection
        { 714, 3, "System.Windows.Media.Media3D", "Vector3DCollectionConverter" }, // Vector3DCollectionConverter
        { 715, 3, "System.Windows.Media.Media3D", "Vector3DConverter" }, // Vector3DConverter
        { 716, 3, "System.Windows.Media.Animation", "Vector3DKeyFrame" }, // Vector3DKeyFrame
        { 717, 3, "System.Windows.Media.Animation", "Vector3DKeyFrameCollection" }, // Vector3DKeyFrameCollection
        { 718, 3, "System.Windows.Media.Animation", "VectorAnimation" }, // VectorAnimation
        { 719, 3, "System.Windows.Media.Animation", "VectorAnimationBase" }, // VectorAnimationBase
        { 720, 3, "System.Windows.Media.Animation", "VectorAnimationUsingKeyFrames" }, // VectorAnimationUsingKeyFrames
        { 721, 3, "System.Windows.Media", "VectorCollection" }, // VectorCollection
        { 722, 3, "System.Windows.Media", "VectorCollectionConverter" }, // VectorCollectionConverter
        { 723, 2, "System.Windows", "VectorConverter" }, // VectorConverter
        { 724, 3, "System.Windows.Media.Animation", "VectorKeyFrame" }, // VectorKeyFrame
        { 725, 3, "System.Windows.Media.Animation", "VectorKeyFrameCollection" }, // VectorKeyFrameCollection
        { 726, 3, "System.Windows.Media", "VideoDrawing" }, // VideoDrawing
        { 727, 4, "System.Windows.Controls", "ViewBase" }, // ViewBase
        { 728, 4, "System.Windows.Controls", "Viewbox" }, // Viewbox
        { 729, 4, "System.Windows.Controls", "Viewport3D" }, // Viewport3D
        { 730, 3, "System.Windows.Media.Media3D", "Viewport3DVisual" }, // Viewport3DVisual
        { 731, 4, "System.Windows.Controls", "VirtualizingPanel" }, // VirtualizingPanel
        { 732, 4, "System.Windows.Controls", "VirtualizingStackPanel" }, // VirtualizingStackPanel
        { 733, 3, "System.Windows.Media", "Visual" }, // Visual
        { 734, 3, "System.Windows.Media.Media3D", "Visual3D" }, // Visual3D
        { 735, 3, "System.Windows.Media", "VisualBrush" }, // VisualBrush
        { 736, 3, "System.Windows.Media", "VisualTarget" }, // VisualTarget
        { 737, 2, "System.Windows", "WeakEventManager" }, // WeakEventManager
        { 738, 2, "System.Windows.Markup", "WhitespaceSignificantCollectionAttribute" }, // WhitespaceSignificantCollectionAttribute
        { 739, 4, "System.Windows", "Window" }, // Window
        { 740, 3, "System.Windows.Media.Imaging", "WmpBitmapDecoder" }, // WmpBitmapDecoder
        { 741, 3, "System.Windows.Media.Imaging", "WmpBitmapEncoder" }, // WmpBitmapEncoder
        { 742, 4, "System.Windows.Controls", "WrapPanel" }, // WrapPanel
        { 743, 3, "System.Windows.Media.Imaging", "WriteableBitmap" }, // WriteableBitmap
        { 744, 4, "System.Windows.Markup", "XamlBrushSerializer" }, // XamlBrushSerializer
        { 745, 4, "System.Windows.Markup", "XamlInt32CollectionSerializer" }, // XamlInt32CollectionSerializer
        { 746, 4, "System.Windows.Markup", "XamlPathDataSerializer" }, // XamlPathDataSerializer
        { 747, 4, "System.Windows.Markup", "XamlPoint3DCollectionSerializer" }, // XamlPoint3DCollectionSerializer
        { 748, 4, "System.Windows.Markup", "XamlPointCollectionSerializer" }, // XamlPointCollectionSerializer
        { 749, 4, "System.Windows.Markup", "XamlReader" }, // XamlReader
        { 750, 4, "System.Windows.Markup", "XamlStyleSerializer" }, // XamlStyleSerializer
        { 751, 4, "System.Windows.Markup", "XamlTemplateSerializer" }, // XamlTemplateSerializer
        { 752, 4, "System.Windows.Markup", "XamlVector3DCollectionSerializer" }, // XamlVector3DCollectionSerializer
        { 753, 4, "System.Windows.Markup", "XamlWriter" }, // XamlWriter
        { 754, 4, "System.Windows.Data", "XmlDataProvider" }, // XmlDataProvider
        { 755, 2, "System.Windows.Markup", "XmlLangPropertyAttribute" }, // XmlLangPropertyAttribute
        { 756, 3, "System.Windows.Markup", "XmlLanguage" }, // XmlLanguage
        { 757, 3, "System.Windows.Markup", "XmlLanguageConverter" }, // XmlLanguageConverter
        { 758, 4, "System.Windows.Data", "XmlNamespaceMapping" }, // XmlNamespaceMapping
        { 759, 4, "System.Windows.Documents", "ZoomPercentageConverter" }, // ZoomPercentageConverter
    };
    ASSERT_EQ(std::size(Baml::KnownTypesTable), std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); i++) {
        const auto& row = Baml::KnownTypesTable[i];
        EXPECT_EQ(static_cast<std::int16_t>(row.Id), expected[i].id) << i;
        EXPECT_EQ(row.Row.AssemblyIndex, expected[i].assemblyIndex) << i;
        EXPECT_STREQ(row.Row.Namespace, expected[i].ns) << i;
        EXPECT_STREQ(row.Row.Name, expected[i].name) << i;
    }
}

// The 267 member rows: ascending ids over 1..268 with the 137 hole, every
// parent present in the types table, and every row matching its gold row.
TEST(KnownThingsTablesTest, MembersRowGeometry)
{
    const auto& table = Baml::KnownMembersTable;
    ASSERT_EQ(std::size(table), 267u);
    std::int16_t previous = 0;
    for (std::size_t i = 0; i < std::size(table); i++) {
        const auto id = static_cast<std::int16_t>(table[i].Id);
        EXPECT_GT(id, previous) << i; // ascending
        previous = id;
        EXPECT_NE(table[i].Row.Parent, Baml::KnownTypes::Unknown) << i;
    }
    EXPECT_EQ(previous, 268);
}

TEST(KnownThingsTablesTest, MemberParentsAllResolveInTypesTable)
{
    std::vector<Baml::KnownTypes> known;
    for (const auto& entry : Baml::KnownTypesTable)
        known.push_back(entry.Id);
    for (const auto& entry : Baml::KnownMembersTable) {
        const auto parent = entry.Row.Parent;
        bool found = false;
        for (auto id : known)
            found = found || id == parent;
        EXPECT_TRUE(found) << "member " << static_cast<std::int16_t>(entry.Id);
    }
}

TEST(KnownThingsTablesTest, MembersRowsMatchGold)
{
    struct Expected { std::int16_t id; std::int16_t parent; const char* name; std::uint8_t assemblyIndex; const char* ns; const char* typeName; };
    static constexpr Expected expected[] = {
        { 1, 1, "Text", 0, "System", "String" }, // AccessText_Text
        { 2, 17, "Storyboard", 4, "System.Windows.Media.Animation", "Storyboard" }, // BeginStoryboard_Storyboard
        { 3, 28, "Children", 3, "System.Windows.Media.Effects", "BitmapEffectCollection" }, // BitmapEffectGroup_Children
        { 4, 50, "Background", 3, "System.Windows.Media", "Brush" }, // Border_Background
        { 5, 50, "BorderBrush", 3, "System.Windows.Media", "Brush" }, // Border_BorderBrush
        { 6, 50, "BorderThickness", 4, "System.Windows", "Thickness" }, // Border_BorderThickness
        { 7, 56, "Command", 3, "System.Windows.Input", "ICommand" }, // ButtonBase_Command
        { 8, 56, "CommandParameter", 0, "System", "Object" }, // ButtonBase_CommandParameter
        { 9, 56, "CommandTarget", 3, "System.Windows", "IInputElement" }, // ButtonBase_CommandTarget
        { 10, 56, "IsPressed", 0, "System", "Boolean" }, // ButtonBase_IsPressed
        { 11, 90, "MaxWidth", 0, "System", "Double" }, // ColumnDefinition_MaxWidth
        { 12, 90, "MinWidth", 0, "System", "Double" }, // ColumnDefinition_MinWidth
        { 13, 90, "Width", 4, "System.Windows", "GridLength" }, // ColumnDefinition_Width
        { 14, 100, "Content", 0, "System", "Object" }, // ContentControl_Content
        { 15, 100, "ContentTemplate", 4, "System.Windows", "DataTemplate" }, // ContentControl_ContentTemplate
        { 16, 100, "ContentTemplateSelector", 4, "System.Windows.Controls", "DataTemplateSelector" }, // ContentControl_ContentTemplateSelector
        { 17, 100, "HasContent", 0, "System", "Boolean" }, // ContentControl_HasContent
        { 18, 101, "Focusable", 0, "System", "Boolean" }, // ContentElement_Focusable
        { 19, 102, "Content", 0, "System", "Object" }, // ContentPresenter_Content
        { 20, 102, "ContentSource", 0, "System", "String" }, // ContentPresenter_ContentSource
        { 21, 102, "ContentTemplate", 4, "System.Windows", "DataTemplate" }, // ContentPresenter_ContentTemplate
        { 22, 102, "ContentTemplateSelector", 4, "System.Windows.Controls", "DataTemplateSelector" }, // ContentPresenter_ContentTemplateSelector
        { 23, 102, "RecognizesAccessKey", 0, "System", "Boolean" }, // ContentPresenter_RecognizesAccessKey
        { 24, 107, "Background", 3, "System.Windows.Media", "Brush" }, // Control_Background
        { 25, 107, "BorderBrush", 3, "System.Windows.Media", "Brush" }, // Control_BorderBrush
        { 26, 107, "BorderThickness", 4, "System.Windows", "Thickness" }, // Control_BorderThickness
        { 27, 107, "FontFamily", 3, "System.Windows.Media", "FontFamily" }, // Control_FontFamily
        { 28, 107, "FontSize", 0, "System", "Double" }, // Control_FontSize
        { 29, 107, "FontStretch", 3, "System.Windows", "FontStretch" }, // Control_FontStretch
        { 30, 107, "FontStyle", 3, "System.Windows", "FontStyle" }, // Control_FontStyle
        { 31, 107, "FontWeight", 3, "System.Windows", "FontWeight" }, // Control_FontWeight
        { 32, 107, "Foreground", 3, "System.Windows.Media", "Brush" }, // Control_Foreground
        { 33, 107, "HorizontalContentAlignment", 4, "System.Windows", "HorizontalAlignment" }, // Control_HorizontalContentAlignment
        { 34, 107, "IsTabStop", 0, "System", "Boolean" }, // Control_IsTabStop
        { 35, 107, "Padding", 4, "System.Windows", "Thickness" }, // Control_Padding
        { 36, 107, "TabIndex", 0, "System", "Int32" }, // Control_TabIndex
        { 37, 107, "Template", 4, "System.Windows.Controls", "ControlTemplate" }, // Control_Template
        { 38, 107, "VerticalContentAlignment", 4, "System.Windows", "VerticalAlignment" }, // Control_VerticalContentAlignment
        { 39, 163, "Dock", 4, "System.Windows.Controls", "Dock" }, // DockPanel_Dock
        { 40, 163, "LastChildFill", 0, "System", "Boolean" }, // DockPanel_LastChildFill
        { 41, 167, "Document", 3, "System.Windows.Documents", "IDocumentPaginatorSource" }, // DocumentViewerBase_Document
        { 42, 183, "Children", 3, "System.Windows.Media", "DrawingCollection" }, // DrawingGroup_Children
        { 43, 211, "Document", 4, "System.Windows.Documents", "FlowDocument" }, // FlowDocumentReader_Document
        { 44, 212, "Document", 4, "System.Windows.Documents", "FlowDocument" }, // FlowDocumentScrollViewer_Document
        { 45, 225, "Style", 4, "System.Windows", "Style" }, // FrameworkContentElement_Style
        { 46, 226, "FlowDirection", 3, "System.Windows", "FlowDirection" }, // FrameworkElement_FlowDirection
        { 47, 226, "Height", 0, "System", "Double" }, // FrameworkElement_Height
        { 48, 226, "HorizontalAlignment", 4, "System.Windows", "HorizontalAlignment" }, // FrameworkElement_HorizontalAlignment
        { 49, 226, "Margin", 4, "System.Windows", "Thickness" }, // FrameworkElement_Margin
        { 50, 226, "MaxHeight", 0, "System", "Double" }, // FrameworkElement_MaxHeight
        { 51, 226, "MaxWidth", 0, "System", "Double" }, // FrameworkElement_MaxWidth
        { 52, 226, "MinHeight", 0, "System", "Double" }, // FrameworkElement_MinHeight
        { 53, 226, "MinWidth", 0, "System", "Double" }, // FrameworkElement_MinWidth
        { 54, 226, "Name", 0, "System", "String" }, // FrameworkElement_Name
        { 55, 226, "Style", 4, "System.Windows", "Style" }, // FrameworkElement_Style
        { 56, 226, "VerticalAlignment", 4, "System.Windows", "VerticalAlignment" }, // FrameworkElement_VerticalAlignment
        { 57, 226, "Width", 0, "System", "Double" }, // FrameworkElement_Width
        { 58, 236, "Children", 3, "System.Windows.Media", "GeneralTransformCollection" }, // GeneralTransformGroup_Children
        { 59, 242, "Children", 3, "System.Windows.Media", "GeometryCollection" }, // GeometryGroup_Children
        { 60, 251, "GradientStops", 3, "System.Windows.Media", "GradientStopCollection" }, // GradientBrush_GradientStops
        { 61, 254, "Column", 0, "System", "Int32" }, // Grid_Column
        { 62, 254, "ColumnSpan", 0, "System", "Int32" }, // Grid_ColumnSpan
        { 63, 254, "Row", 0, "System", "Int32" }, // Grid_Row
        { 64, 254, "RowSpan", 0, "System", "Int32" }, // Grid_RowSpan
        { 65, 259, "Header", 0, "System", "Object" }, // GridViewColumn_Header
        { 66, 269, "HasHeader", 0, "System", "Boolean" }, // HeaderedContentControl_HasHeader
        { 67, 269, "Header", 0, "System", "Object" }, // HeaderedContentControl_Header
        { 68, 269, "HeaderTemplate", 4, "System.Windows", "DataTemplate" }, // HeaderedContentControl_HeaderTemplate
        { 69, 269, "HeaderTemplateSelector", 4, "System.Windows.Controls", "DataTemplateSelector" }, // HeaderedContentControl_HeaderTemplateSelector
        { 70, 270, "HasHeader", 0, "System", "Boolean" }, // HeaderedItemsControl_HasHeader
        { 71, 270, "Header", 0, "System", "Object" }, // HeaderedItemsControl_Header
        { 72, 270, "HeaderTemplate", 4, "System.Windows", "DataTemplate" }, // HeaderedItemsControl_HeaderTemplate
        { 73, 270, "HeaderTemplateSelector", 4, "System.Windows.Controls", "DataTemplateSelector" }, // HeaderedItemsControl_HeaderTemplateSelector
        { 74, 273, "NavigateUri", 1, "System", "Uri" }, // Hyperlink_NavigateUri
        { 75, 281, "Source", 3, "System.Windows.Media", "ImageSource" }, // Image_Source
        { 76, 281, "Stretch", 3, "System.Windows.Media", "Stretch" }, // Image_Stretch
        { 77, 329, "ItemContainerStyle", 4, "System.Windows", "Style" }, // ItemsControl_ItemContainerStyle
        { 78, 329, "ItemContainerStyleSelector", 4, "System.Windows.Controls", "StyleSelector" }, // ItemsControl_ItemContainerStyleSelector
        { 79, 329, "ItemTemplate", 4, "System.Windows", "DataTemplate" }, // ItemsControl_ItemTemplate
        { 80, 329, "ItemTemplateSelector", 4, "System.Windows.Controls", "DataTemplateSelector" }, // ItemsControl_ItemTemplateSelector
        { 81, 329, "ItemsPanel", 4, "System.Windows.Controls", "ItemsPanelTemplate" }, // ItemsControl_ItemsPanel
        { 82, 329, "ItemsSource", 0, "System.Collections", "IEnumerable" }, // ItemsControl_ItemsSource
        { 83, 384, "Children", 3, "System.Windows.Media.Media3D", "MaterialCollection" }, // MaterialGroup_Children
        { 84, 408, "Children", 3, "System.Windows.Media.Media3D", "Model3DCollection" }, // Model3DGroup_Children
        { 85, 434, "Content", 0, "System", "Object" }, // Page_Content
        { 86, 437, "Background", 3, "System.Windows.Media", "Brush" }, // Panel_Background
        { 87, 442, "Data", 3, "System.Windows.Media", "Geometry" }, // Path_Data
        { 88, 443, "Segments", 3, "System.Windows.Media", "PathSegmentCollection" }, // PathFigure_Segments
        { 89, 446, "Figures", 3, "System.Windows.Media", "PathFigureCollection" }, // PathGeometry_Figures
        { 90, 485, "Child", 3, "System.Windows", "UIElement" }, // Popup_Child
        { 91, 485, "IsOpen", 0, "System", "Boolean" }, // Popup_IsOpen
        { 92, 485, "Placement", 4, "System.Windows.Controls.Primitives", "PlacementMode" }, // Popup_Placement
        { 93, 485, "PopupAnimation", 4, "System.Windows.Controls.Primitives", "PopupAnimation" }, // Popup_PopupAnimation
        { 94, 541, "Height", 4, "System.Windows", "GridLength" }, // RowDefinition_Height
        { 95, 541, "MaxHeight", 0, "System", "Double" }, // RowDefinition_MaxHeight
        { 96, 541, "MinHeight", 0, "System", "Double" }, // RowDefinition_MinHeight
        { 97, 550, "CanContentScroll", 0, "System", "Boolean" }, // ScrollViewer_CanContentScroll
        { 98, 550, "HorizontalScrollBarVisibility", 4, "System.Windows.Controls", "ScrollBarVisibility" }, // ScrollViewer_HorizontalScrollBarVisibility
        { 99, 550, "VerticalScrollBarVisibility", 4, "System.Windows.Controls", "ScrollBarVisibility" }, // ScrollViewer_VerticalScrollBarVisibility
        { 100, 558, "Fill", 3, "System.Windows.Media", "Brush" }, // Shape_Fill
        { 101, 558, "Stroke", 3, "System.Windows.Media", "Brush" }, // Shape_Stroke
        { 102, 558, "StrokeThickness", 0, "System", "Double" }, // Shape_StrokeThickness
        { 103, 638, "Background", 3, "System.Windows.Media", "Brush" }, // TextBlock_Background
        { 104, 638, "FontFamily", 3, "System.Windows.Media", "FontFamily" }, // TextBlock_FontFamily
        { 105, 638, "FontSize", 0, "System", "Double" }, // TextBlock_FontSize
        { 106, 638, "FontStretch", 3, "System.Windows", "FontStretch" }, // TextBlock_FontStretch
        { 107, 638, "FontStyle", 3, "System.Windows", "FontStyle" }, // TextBlock_FontStyle
        { 108, 638, "FontWeight", 3, "System.Windows", "FontWeight" }, // TextBlock_FontWeight
        { 109, 638, "Foreground", 3, "System.Windows.Media", "Brush" }, // TextBlock_Foreground
        { 110, 638, "Text", 0, "System", "String" }, // TextBlock_Text
        { 111, 638, "TextDecorations", 3, "System.Windows", "TextDecorationCollection" }, // TextBlock_TextDecorations
        { 112, 638, "TextTrimming", 3, "System.Windows", "TextTrimming" }, // TextBlock_TextTrimming
        { 113, 638, "TextWrapping", 3, "System.Windows", "TextWrapping" }, // TextBlock_TextWrapping
        { 114, 639, "Text", 0, "System", "String" }, // TextBox_Text
        { 115, 648, "Background", 3, "System.Windows.Media", "Brush" }, // TextElement_Background
        { 116, 648, "FontFamily", 3, "System.Windows.Media", "FontFamily" }, // TextElement_FontFamily
        { 117, 648, "FontSize", 0, "System", "Double" }, // TextElement_FontSize
        { 118, 648, "FontStretch", 3, "System.Windows", "FontStretch" }, // TextElement_FontStretch
        { 119, 648, "FontStyle", 3, "System.Windows", "FontStyle" }, // TextElement_FontStyle
        { 120, 648, "FontWeight", 3, "System.Windows", "FontWeight" }, // TextElement_FontWeight
        { 121, 648, "Foreground", 3, "System.Windows.Media", "Brush" }, // TextElement_Foreground
        { 122, 667, "Children", 3, "System.Windows.Media.Animation", "TimelineCollection" }, // TimelineGroup_Children
        { 123, 675, "IsDirectionReversed", 0, "System", "Boolean" }, // Track_IsDirectionReversed
        { 124, 675, "Maximum", 0, "System", "Double" }, // Track_Maximum
        { 125, 675, "Minimum", 0, "System", "Double" }, // Track_Minimum
        { 126, 675, "Orientation", 4, "System.Windows.Controls", "Orientation" }, // Track_Orientation
        { 127, 675, "Value", 0, "System", "Double" }, // Track_Value
        { 128, 675, "ViewportSize", 0, "System", "Double" }, // Track_ViewportSize
        { 129, 679, "Children", 3, "System.Windows.Media.Media3D", "Transform3DCollection" }, // Transform3DGroup_Children
        { 130, 682, "Children", 3, "System.Windows.Media", "TransformCollection" }, // TransformGroup_Children
        { 131, 694, "ClipToBounds", 0, "System", "Boolean" }, // UIElement_ClipToBounds
        { 132, 694, "Focusable", 0, "System", "Boolean" }, // UIElement_Focusable
        { 133, 694, "IsEnabled", 0, "System", "Boolean" }, // UIElement_IsEnabled
        { 134, 694, "RenderTransform", 3, "System.Windows.Media", "Transform" }, // UIElement_RenderTransform
        { 135, 694, "Visibility", 3, "System.Windows", "Visibility" }, // UIElement_Visibility
        { 136, 729, "Children", 3, "System.Windows.Media.Media3D", "Visual3DCollection" }, // Viewport3D_Children
        { 138, 2, "Child", 3, "System.Windows", "UIElement" }, // AdornedElementPlaceholder_Child
        { 139, 4, "Child", 3, "System.Windows", "UIElement" }, // AdornerDecorator_Child
        { 140, 8, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // AnchoredBlock_Blocks
        { 141, 14, "Items", 0, "System.Collections", "IList" }, // ArrayExtension_Items
        { 142, 37, "Child", 3, "System.Windows", "UIElement" }, // BlockUIContainer_Child
        { 143, 41, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // Bold_Inlines
        { 144, 45, "KeyFrames", 3, "System.Windows.Media.Animation", "BooleanKeyFrameCollection" }, // BooleanAnimationUsingKeyFrames_KeyFrames
        { 145, 50, "Child", 3, "System.Windows", "UIElement" }, // Border_Child
        { 146, 54, "Child", 3, "System.Windows", "UIElement" }, // BulletDecorator_Child
        { 147, 55, "Content", 0, "System", "Object" }, // Button_Content
        { 148, 56, "Content", 0, "System", "Object" }, // ButtonBase_Content
        { 149, 60, "KeyFrames", 3, "System.Windows.Media.Animation", "ByteKeyFrameCollection" }, // ByteAnimationUsingKeyFrames_KeyFrames
        { 150, 66, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // Canvas_Children
        { 151, 69, "KeyFrames", 3, "System.Windows.Media.Animation", "CharKeyFrameCollection" }, // CharAnimationUsingKeyFrames_KeyFrames
        { 152, 74, "Content", 0, "System", "Object" }, // CheckBox_Content
        { 153, 84, "KeyFrames", 3, "System.Windows.Media.Animation", "ColorKeyFrameCollection" }, // ColorAnimationUsingKeyFrames_KeyFrames
        { 154, 92, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // ComboBox_Items
        { 155, 93, "Content", 0, "System", "Object" }, // ComboBoxItem_Content
        { 156, 105, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // ContextMenu_Items
        { 157, 108, "VisualTree", 4, "System.Windows", "FrameworkElementFactory" }, // ControlTemplate_VisualTree
        { 158, 120, "VisualTree", 4, "System.Windows", "FrameworkElementFactory" }, // DataTemplate_VisualTree
        { 159, 122, "Setters", 4, "System.Windows", "SetterBaseCollection" }, // DataTrigger_Setters
        { 160, 129, "KeyFrames", 3, "System.Windows.Media.Animation", "DecimalKeyFrameCollection" }, // DecimalAnimationUsingKeyFrames_KeyFrames
        { 161, 133, "Child", 3, "System.Windows", "UIElement" }, // Decorator_Child
        { 162, 163, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // DockPanel_Children
        { 163, 166, "Document", 3, "System.Windows.Documents", "IDocumentPaginatorSource" }, // DocumentViewer_Document
        { 164, 171, "KeyFrames", 3, "System.Windows.Media.Animation", "DoubleKeyFrameCollection" }, // DoubleAnimationUsingKeyFrames_KeyFrames
        { 165, 198, "Actions", 4, "System.Windows", "TriggerActionCollection" }, // EventTrigger_Actions
        { 166, 199, "Content", 0, "System", "Object" }, // Expander_Content
        { 167, 202, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // Figure_Blocks
        { 168, 205, "Pages", 4, "System.Windows.Documents", "PageContentCollection" }, // FixedDocument_Pages
        { 169, 206, "References", 4, "System.Windows.Documents", "DocumentReferenceCollection" }, // FixedDocumentSequence_References
        { 170, 207, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // FixedPage_Children
        { 171, 208, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // Floater_Blocks
        { 172, 209, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // FlowDocument_Blocks
        { 173, 210, "Document", 3, "System.Windows.Documents", "IDocumentPaginatorSource" }, // FlowDocumentPageViewer_Document
        { 174, 231, "VisualTree", 4, "System.Windows", "FrameworkElementFactory" }, // FrameworkTemplate_VisualTree
        { 175, 254, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // Grid_Children
        { 176, 258, "Columns", 4, "System.Windows.Controls", "GridViewColumnCollection" }, // GridView_Columns
        { 177, 260, "Content", 0, "System", "Object" }, // GridViewColumnHeader_Content
        { 178, 264, "Content", 0, "System", "Object" }, // GroupBox_Content
        { 179, 265, "Content", 0, "System", "Object" }, // GroupItem_Content
        { 180, 269, "Content", 0, "System", "Object" }, // HeaderedContentControl_Content
        { 181, 270, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // HeaderedItemsControl_Items
        { 182, 271, "VisualTree", 4, "System.Windows", "FrameworkElementFactory" }, // HierarchicalDataTemplate_VisualTree
        { 183, 273, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // Hyperlink_Inlines
        { 184, 288, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // InkCanvas_Children
        { 185, 289, "Child", 3, "System.Windows", "UIElement" }, // InkPresenter_Child
        { 186, 292, "Child", 3, "System.Windows", "UIElement" }, // InlineUIContainer_Child
        { 187, 300, "NameValue", 3, "System.Windows.Input", "InputScopeNameValue" }, // InputScopeName_NameValue
        { 188, 305, "KeyFrames", 3, "System.Windows.Media.Animation", "Int16KeyFrameCollection" }, // Int16AnimationUsingKeyFrames_KeyFrames
        { 189, 312, "KeyFrames", 3, "System.Windows.Media.Animation", "Int32KeyFrameCollection" }, // Int32AnimationUsingKeyFrames_KeyFrames
        { 190, 323, "KeyFrames", 3, "System.Windows.Media.Animation", "Int64KeyFrameCollection" }, // Int64AnimationUsingKeyFrames_KeyFrames
        { 191, 327, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // Italic_Inlines
        { 192, 329, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // ItemsControl_Items
        { 193, 330, "VisualTree", 4, "System.Windows", "FrameworkElementFactory" }, // ItemsPanelTemplate_VisualTree
        { 194, 346, "Content", 0, "System", "Object" }, // Label_Content
        { 195, 358, "GradientStops", 3, "System.Windows.Media", "GradientStopCollection" }, // LinearGradientBrush_GradientStops
        { 196, 372, "ListItems", 4, "System.Windows.Documents", "ListItemCollection" }, // List_ListItems
        { 197, 373, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // ListBox_Items
        { 198, 374, "Content", 0, "System", "Object" }, // ListBoxItem_Content
        { 199, 376, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // ListItem_Blocks
        { 200, 377, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // ListView_Items
        { 201, 378, "Content", 0, "System", "Object" }, // ListViewItem_Content
        { 202, 389, "KeyFrames", 3, "System.Windows.Media.Animation", "MatrixKeyFrameCollection" }, // MatrixAnimationUsingKeyFrames_KeyFrames
        { 203, 401, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // Menu_Items
        { 204, 402, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // MenuBase_Items
        { 205, 403, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // MenuItem_Items
        { 206, 409, "Children", 3, "System.Windows.Media.Media3D", "Visual3DCollection" }, // ModelVisual3D_Children
        { 207, 416, "Bindings", 0, "System.Collections.ObjectModel", "Collection`1" }, // MultiBinding_Bindings
        { 208, 418, "Setters", 4, "System.Windows", "SetterBaseCollection" }, // MultiDataTrigger_Setters
        { 209, 419, "Setters", 4, "System.Windows", "SetterBaseCollection" }, // MultiTrigger_Setters
        { 210, 428, "KeyFrames", 3, "System.Windows.Media.Animation", "ObjectKeyFrameCollection" }, // ObjectAnimationUsingKeyFrames_KeyFrames
        { 211, 435, "Child", 4, "System.Windows.Documents", "FixedPage" }, // PageContent_Child
        { 212, 436, "Content", 0, "System", "Object" }, // PageFunctionBase_Content
        { 213, 437, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // Panel_Children
        { 214, 438, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // Paragraph_Inlines
        { 215, 439, "Children", 3, "System.Windows.Media.Animation", "TimelineCollection" }, // ParallelTimeline_Children
        { 216, 460, "KeyFrames", 3, "System.Windows.Media.Animation", "Point3DKeyFrameCollection" }, // Point3DAnimationUsingKeyFrames_KeyFrames
        { 217, 470, "KeyFrames", 3, "System.Windows.Media.Animation", "PointKeyFrameCollection" }, // PointAnimationUsingKeyFrames_KeyFrames
        { 218, 487, "Bindings", 0, "System.Collections.ObjectModel", "Collection`1" }, // PriorityBinding_Bindings
        { 219, 497, "KeyFrames", 3, "System.Windows.Media.Animation", "QuaternionKeyFrameCollection" }, // QuaternionAnimationUsingKeyFrames_KeyFrames
        { 220, 502, "GradientStops", 3, "System.Windows.Media", "GradientStopCollection" }, // RadialGradientBrush_GradientStops
        { 221, 503, "Content", 0, "System", "Object" }, // RadioButton_Content
        { 222, 510, "KeyFrames", 3, "System.Windows.Media.Animation", "RectKeyFrameCollection" }, // RectAnimationUsingKeyFrames_KeyFrames
        { 223, 522, "Content", 0, "System", "Object" }, // RepeatButton_Content
        { 224, 527, "Document", 4, "System.Windows.Documents", "FlowDocument" }, // RichTextBox_Document
        { 225, 533, "KeyFrames", 3, "System.Windows.Media.Animation", "Rotation3DKeyFrameCollection" }, // Rotation3DAnimationUsingKeyFrames_KeyFrames
        { 226, 542, "Text", 0, "System", "String" }, // Run_Text
        { 227, 550, "Content", 0, "System", "Object" }, // ScrollViewer_Content
        { 228, 551, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // Section_Blocks
        { 229, 553, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // Selector_Items
        { 230, 562, "KeyFrames", 3, "System.Windows.Media.Animation", "SingleKeyFrameCollection" }, // SingleAnimationUsingKeyFrames_KeyFrames
        { 231, 571, "KeyFrames", 3, "System.Windows.Media.Animation", "SizeKeyFrameCollection" }, // SizeAnimationUsingKeyFrames_KeyFrames
        { 232, 580, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // Span_Inlines
        { 233, 601, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // StackPanel_Children
        { 234, 604, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // StatusBar_Items
        { 235, 605, "Content", 0, "System", "Object" }, // StatusBarItem_Content
        { 236, 608, "Children", 3, "System.Windows.Media.Animation", "TimelineCollection" }, // Storyboard_Children
        { 237, 614, "KeyFrames", 3, "System.Windows.Media.Animation", "StringKeyFrameCollection" }, // StringAnimationUsingKeyFrames_KeyFrames
        { 238, 620, "Setters", 4, "System.Windows", "SetterBaseCollection" }, // Style_Setters
        { 239, 623, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // TabControl_Items
        { 240, 624, "Content", 0, "System", "Object" }, // TabItem_Content
        { 241, 625, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // TabPanel_Children
        { 242, 626, "RowGroups", 4, "System.Windows.Documents", "TableRowGroupCollection" }, // Table_RowGroups
        { 243, 627, "Blocks", 4, "System.Windows.Documents", "BlockCollection" }, // TableCell_Blocks
        { 244, 629, "Cells", 4, "System.Windows.Documents", "TableCellCollection" }, // TableRow_Cells
        { 245, 630, "Rows", 4, "System.Windows.Documents", "TableRowCollection" }, // TableRowGroup_Rows
        { 246, 638, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // TextBlock_Inlines
        { 247, 654, "KeyFrames", 4, "System.Windows.Media.Animation", "ThicknessKeyFrameCollection" }, // ThicknessAnimationUsingKeyFrames_KeyFrames
        { 248, 668, "Content", 0, "System", "Object" }, // ToggleButton_Content
        { 249, 669, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // ToolBar_Items
        { 250, 670, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // ToolBarOverflowPanel_Children
        { 251, 671, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // ToolBarPanel_Children
        { 252, 672, "ToolBars", 0, "System.Collections.ObjectModel", "Collection`1" }, // ToolBarTray_ToolBars
        { 253, 673, "Content", 0, "System", "Object" }, // ToolTip_Content
        { 254, 686, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // TreeView_Items
        { 255, 687, "Items", 4, "System.Windows.Controls", "ItemCollection" }, // TreeViewItem_Items
        { 256, 688, "Setters", 4, "System.Windows", "SetterBaseCollection" }, // Trigger_Setters
        { 257, 702, "Inlines", 4, "System.Windows.Documents", "InlineCollection" }, // Underline_Inlines
        { 258, 703, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // UniformGrid_Children
        { 259, 706, "Content", 0, "System", "Object" }, // UserControl_Content
        { 260, 712, "KeyFrames", 3, "System.Windows.Media.Animation", "Vector3DKeyFrameCollection" }, // Vector3DAnimationUsingKeyFrames_KeyFrames
        { 261, 720, "KeyFrames", 3, "System.Windows.Media.Animation", "VectorKeyFrameCollection" }, // VectorAnimationUsingKeyFrames_KeyFrames
        { 262, 728, "Child", 3, "System.Windows", "UIElement" }, // Viewbox_Child
        { 263, 730, "Children", 3, "System.Windows.Media.Media3D", "Visual3DCollection" }, // Viewport3DVisual_Children
        { 264, 731, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // VirtualizingPanel_Children
        { 265, 732, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // VirtualizingStackPanel_Children
        { 266, 739, "Content", 0, "System", "Object" }, // Window_Content
        { 267, 742, "Children", 4, "System.Windows.Controls", "UIElementCollection" }, // WrapPanel_Children
        { 268, 754, "XmlSerializer", 5, "System.Xml.Serialization", "IXmlSerializable" }, // XmlDataProvider_XmlSerializer
    };
    ASSERT_EQ(std::size(Baml::KnownMembersTable), std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); i++) {
        const auto& entry = Baml::KnownMembersTable[i];
        EXPECT_EQ(static_cast<std::int16_t>(entry.Id), expected[i].id) << i;
        EXPECT_EQ(static_cast<std::int16_t>(entry.Row.Parent), expected[i].parent) << i;
        EXPECT_STREQ(entry.Row.Name, expected[i].name) << i;
        EXPECT_EQ(entry.Row.Type.AssemblyIndex, expected[i].assemblyIndex) << i;
        EXPECT_STREQ(entry.Row.Type.Namespace, expected[i].ns) << i;
        EXPECT_STREQ(entry.Row.Type.Name, expected[i].typeName) << i;
    }
}

// The KnownMembers 137 hole is the one structural trap a "tidy" edit closes:
// no member row carries id 137, and Viewport3D_Children = 136 is followed
// by AdornedElementPlaceholder_Child = 138.
TEST(KnownThingsTablesTest, MembersValue137HoleIsFaithful)
{
    const auto& table = Baml::KnownMembersTable;
    for (std::size_t i = 0; i + 1 < std::size(table); i++) {
        if (static_cast<std::int16_t>(table[i].Id) == 136) {
            ASSERT_STREQ(table[i].Row.Name, "Children");
            EXPECT_EQ(static_cast<std::int16_t>(table[i + 1].Id), 138) << i;
            return;
        }
    }
    FAIL() << "no member row with id 136";
}

// Three member rows name their property type as the generic metadata name
// "Collection`1" (mscorlib): the shipped engine's TopLevelTypeName lookup
// splits the arity off metadata names, so these resolve to a null type --
// the table keeps the C# rows verbatim and the port's future resolution
// must reproduce the null, not "fix" the rows.
TEST(KnownThingsTablesTest, GenericCollectionRowsKeepMetadataName)
{
    // MultiBinding_Bindings (id 207): System.Collections.ObjectModel.Collection`1
    // PriorityBinding_Bindings (id 218): System.Collections.ObjectModel.Collection`1
    // ToolBarTray_ToolBars (id 252): System.Collections.ObjectModel.Collection`1
    std::size_t found = 0;
    std::vector<std::int16_t> ids;
    for (std::size_t i = 0; i < std::size(Baml::KnownMembersTable); i++) {
        const auto& row = Baml::KnownMembersTable[i].Row.Type;
        if (row.AssemblyIndex == 0
            && std::string_view(row.Namespace) == "System.Collections.ObjectModel"
            && std::string_view(row.Name) == "Collection`1") {
            found++;
            ids.push_back(static_cast<std::int16_t>(Baml::KnownMembersTable[i].Id));
        }
    }
    EXPECT_EQ(found, 3u);
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 207) != ids.end()) << "MultiBinding_Bindings";
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 218) != ids.end()) << "PriorityBinding_Bindings";
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), 252) != ids.end()) << "ToolBarTray_ToolBars";
}

TEST(KnownThingsTablesTest, StringsMatchGold)
{
    ASSERT_EQ(std::size(Baml::KnownStringsTable), 2u);
    EXPECT_EQ(Baml::KnownStringsTable[0].Id, 1);
    EXPECT_STREQ(Baml::KnownStringsTable[0].Value, "Name");
    EXPECT_EQ(Baml::KnownStringsTable[1].Id, 2);
    EXPECT_STREQ(Baml::KnownStringsTable[1].Value, "Uid");
}

// The 227 resource rows: ascending ids over 1..235 with the eight holes
// the WPF schema context carries.
TEST(KnownThingsTablesTest, ResourcesRowGeometry)
{
    const auto& table = Baml::KnownResourcesTable;
    ASSERT_EQ(std::size(table), 227u);
    std::int16_t previous = 0;
    for (std::size_t i = 0; i < std::size(table); i++) {
        const auto id = table[i].Id;
        EXPECT_GT(id, previous) << i; // ascending
        previous = id;
    }
    EXPECT_EQ(previous, 235);
}

TEST(KnownThingsTablesTest, ResourcesRowsMatchGold)
{
    struct Expected { std::int16_t id; const char* cls; const char* key; const char* resource; };
    static constexpr Expected expected[] = {
        { 1, "SystemColors", "ActiveBorderBrushKey", "ActiveBorderBrush" },
        { 2, "SystemColors", "ActiveCaptionBrushKey", "ActiveCaptionBrush" },
        { 3, "SystemColors", "ActiveCaptionTextBrushKey", "ActiveCaptionTextBrush" },
        { 4, "SystemColors", "AppWorkspaceBrushKey", "AppWorkspaceBrush" },
        { 5, "SystemColors", "ControlBrushKey", "ControlBrush" },
        { 6, "SystemColors", "ControlDarkBrushKey", "ControlDarkBrush" },
        { 7, "SystemColors", "ControlDarkDarkBrushKey", "ControlDarkDarkBrush" },
        { 8, "SystemColors", "ControlLightBrushKey", "ControlLightBrush" },
        { 9, "SystemColors", "ControlLightLightBrushKey", "ControlLightLightBrush" },
        { 10, "SystemColors", "ControlTextBrushKey", "ControlTextBrush" },
        { 11, "SystemColors", "DesktopBrushKey", "DesktopBrush" },
        { 12, "SystemColors", "GradientActiveCaptionBrushKey", "GradientActiveCaptionBrush" },
        { 13, "SystemColors", "GradientInactiveCaptionBrushKey", "GradientInactiveCaptionBrush" },
        { 14, "SystemColors", "GrayTextBrushKey", "GrayTextBrush" },
        { 15, "SystemColors", "HighlightBrushKey", "HighlightBrush" },
        { 16, "SystemColors", "HighlightTextBrushKey", "HighlightTextBrush" },
        { 17, "SystemColors", "HotTrackBrushKey", "HotTrackBrush" },
        { 18, "SystemColors", "InactiveBorderBrushKey", "InactiveBorderBrush" },
        { 19, "SystemColors", "InactiveCaptionBrushKey", "InactiveCaptionBrush" },
        { 20, "SystemColors", "InactiveCaptionTextBrushKey", "InactiveCaptionTextBrush" },
        { 21, "SystemColors", "InfoBrushKey", "InfoBrush" },
        { 22, "SystemColors", "InfoTextBrushKey", "InfoTextBrush" },
        { 23, "SystemColors", "MenuBrushKey", "MenuBrush" },
        { 24, "SystemColors", "MenuBarBrushKey", "MenuBarBrush" },
        { 25, "SystemColors", "MenuHighlightBrushKey", "MenuHighlightBrush" },
        { 26, "SystemColors", "MenuTextBrushKey", "MenuTextBrush" },
        { 27, "SystemColors", "ScrollBarBrushKey", "ScrollBarBrush" },
        { 28, "SystemColors", "WindowBrushKey", "WindowBrush" },
        { 29, "SystemColors", "WindowFrameBrushKey", "WindowFrameBrush" },
        { 30, "SystemColors", "WindowTextBrushKey", "WindowTextBrush" },
        { 31, "SystemColors", "ActiveBorderColorKey", "ActiveBorderColor" },
        { 32, "SystemColors", "ActiveCaptionColorKey", "ActiveCaptionColor" },
        { 33, "SystemColors", "ActiveCaptionTextColorKey", "ActiveCaptionTextColor" },
        { 34, "SystemColors", "AppWorkspaceColorKey", "AppWorkspaceColor" },
        { 35, "SystemColors", "ControlColorKey", "ControlColor" },
        { 36, "SystemColors", "ControlDarkColorKey", "ControlDarkColor" },
        { 37, "SystemColors", "ControlDarkDarkColorKey", "ControlDarkDarkColor" },
        { 38, "SystemColors", "ControlLightColorKey", "ControlLightColor" },
        { 39, "SystemColors", "ControlLightLightColorKey", "ControlLightLightColor" },
        { 40, "SystemColors", "ControlTextColorKey", "ControlTextColor" },
        { 41, "SystemColors", "DesktopColorKey", "DesktopColor" },
        { 42, "SystemColors", "GradientActiveCaptionColorKey", "GradientActiveCaptionColor" },
        { 43, "SystemColors", "GradientInactiveCaptionColorKey", "GradientInactiveCaptionColor" },
        { 44, "SystemColors", "GrayTextColorKey", "GrayTextColor" },
        { 45, "SystemColors", "HighlightColorKey", "HighlightColor" },
        { 46, "SystemColors", "HighlightTextColorKey", "HighlightTextColor" },
        { 47, "SystemColors", "HotTrackColorKey", "HotTrackColor" },
        { 48, "SystemColors", "InactiveBorderColorKey", "InactiveBorderColor" },
        { 49, "SystemColors", "InactiveCaptionColorKey", "InactiveCaptionColor" },
        { 50, "SystemColors", "InactiveCaptionTextColorKey", "InactiveCaptionTextColor" },
        { 51, "SystemColors", "InfoColorKey", "InfoColor" },
        { 52, "SystemColors", "InfoTextColorKey", "InfoTextColor" },
        { 53, "SystemColors", "MenuColorKey", "MenuColor" },
        { 54, "SystemColors", "MenuBarColorKey", "MenuBarColor" },
        { 55, "SystemColors", "MenuHighlightColorKey", "MenuHighlightColor" },
        { 56, "SystemColors", "MenuTextColorKey", "MenuTextColor" },
        { 57, "SystemColors", "ScrollBarColorKey", "ScrollBarColor" },
        { 58, "SystemColors", "WindowColorKey", "WindowColor" },
        { 59, "SystemColors", "WindowFrameColorKey", "WindowFrameColor" },
        { 60, "SystemColors", "WindowTextColorKey", "WindowTextColor" },
        { 63, "SystemFonts", "CaptionFontSizeKey", "CaptionFontSize" },
        { 64, "SystemFonts", "CaptionFontFamilyKey", "CaptionFontFamily" },
        { 65, "SystemFonts", "CaptionFontStyleKey", "CaptionFontStyle" },
        { 66, "SystemFonts", "CaptionFontWeightKey", "CaptionFontWeight" },
        { 67, "SystemFonts", "CaptionFontTextDecorationsKey", "CaptionFontTextDecorations" },
        { 68, "SystemFonts", "SmallCaptionFontSizeKey", "SmallCaptionFontSize" },
        { 69, "SystemFonts", "SmallCaptionFontFamilyKey", "SmallCaptionFontFamily" },
        { 70, "SystemFonts", "SmallCaptionFontStyleKey", "SmallCaptionFontStyle" },
        { 71, "SystemFonts", "SmallCaptionFontWeightKey", "SmallCaptionFontWeight" },
        { 72, "SystemFonts", "SmallCaptionFontTextDecorationsKey", "SmallCaptionFontTextDecorations" },
        { 73, "SystemFonts", "MenuFontSizeKey", "MenuFontSize" },
        { 74, "SystemFonts", "MenuFontFamilyKey", "MenuFontFamily" },
        { 75, "SystemFonts", "MenuFontStyleKey", "MenuFontStyle" },
        { 76, "SystemFonts", "MenuFontWeightKey", "MenuFontWeight" },
        { 77, "SystemFonts", "MenuFontTextDecorationsKey", "MenuFontTextDecorations" },
        { 78, "SystemFonts", "StatusFontSizeKey", "StatusFontSize" },
        { 79, "SystemFonts", "StatusFontFamilyKey", "StatusFontFamily" },
        { 80, "SystemFonts", "StatusFontStyleKey", "StatusFontStyle" },
        { 81, "SystemFonts", "StatusFontWeightKey", "StatusFontWeight" },
        { 82, "SystemFonts", "StatusFontTextDecorationsKey", "StatusFontTextDecorations" },
        { 83, "SystemFonts", "MessageFontSizeKey", "MessageFontSize" },
        { 84, "SystemFonts", "MessageFontFamilyKey", "MessageFontFamily" },
        { 85, "SystemFonts", "MessageFontStyleKey", "MessageFontStyle" },
        { 86, "SystemFonts", "MessageFontWeightKey", "MessageFontWeight" },
        { 87, "SystemFonts", "MessageFontTextDecorationsKey", "MessageFontTextDecorations" },
        { 88, "SystemFonts", "IconFontSizeKey", "IconFontSize" },
        { 89, "SystemFonts", "IconFontFamilyKey", "IconFontFamily" },
        { 90, "SystemFonts", "IconFontStyleKey", "IconFontStyle" },
        { 91, "SystemFonts", "IconFontWeightKey", "IconFontWeight" },
        { 92, "SystemFonts", "IconFontTextDecorationsKey", "IconFontTextDecorations" },
        { 95, "SystemParameters", "ThinHorizontalBorderHeightKey", "ThinHorizontalBorderHeight" },
        { 96, "SystemParameters", "ThinVerticalBorderWidthKey", "ThinVerticalBorderWidth" },
        { 97, "SystemParameters", "CursorWidthKey", "CursorWidth" },
        { 98, "SystemParameters", "CursorHeightKey", "CursorHeight" },
        { 99, "SystemParameters", "ThickHorizontalBorderHeightKey", "ThickHorizontalBorderHeight" },
        { 100, "SystemParameters", "ThickVerticalBorderWidthKey", "ThickVerticalBorderWidth" },
        { 101, "SystemParameters", "FixedFrameHorizontalBorderHeightKey", "FixedFrameHorizontalBorderHeight" },
        { 102, "SystemParameters", "FixedFrameVerticalBorderWidthKey", "FixedFrameVerticalBorderWidth" },
        { 103, "SystemParameters", "FocusHorizontalBorderHeightKey", "FocusHorizontalBorderHeight" },
        { 104, "SystemParameters", "FocusVerticalBorderWidthKey", "FocusVerticalBorderWidth" },
        { 105, "SystemParameters", "FullPrimaryScreenWidthKey", "FullPrimaryScreenWidth" },
        { 106, "SystemParameters", "FullPrimaryScreenHeightKey", "FullPrimaryScreenHeight" },
        { 107, "SystemParameters", "HorizontalScrollBarButtonWidthKey", "HorizontalScrollBarButtonWidth" },
        { 108, "SystemParameters", "HorizontalScrollBarHeightKey", "HorizontalScrollBarHeight" },
        { 109, "SystemParameters", "HorizontalScrollBarThumbWidthKey", "HorizontalScrollBarThumbWidth" },
        { 110, "SystemParameters", "IconWidthKey", "IconWidth" },
        { 111, "SystemParameters", "IconHeightKey", "IconHeight" },
        { 112, "SystemParameters", "IconGridWidthKey", "IconGridWidth" },
        { 113, "SystemParameters", "IconGridHeightKey", "IconGridHeight" },
        { 114, "SystemParameters", "MaximizedPrimaryScreenWidthKey", "MaximizedPrimaryScreenWidth" },
        { 115, "SystemParameters", "MaximizedPrimaryScreenHeightKey", "MaximizedPrimaryScreenHeight" },
        { 116, "SystemParameters", "MaximumWindowTrackWidthKey", "MaximumWindowTrackWidth" },
        { 117, "SystemParameters", "MaximumWindowTrackHeightKey", "MaximumWindowTrackHeight" },
        { 118, "SystemParameters", "MenuCheckmarkWidthKey", "MenuCheckmarkWidth" },
        { 119, "SystemParameters", "MenuCheckmarkHeightKey", "MenuCheckmarkHeight" },
        { 120, "SystemParameters", "MenuButtonWidthKey", "MenuButtonWidth" },
        { 121, "SystemParameters", "MenuButtonHeightKey", "MenuButtonHeight" },
        { 122, "SystemParameters", "MinimumWindowWidthKey", "MinimumWindowWidth" },
        { 123, "SystemParameters", "MinimumWindowHeightKey", "MinimumWindowHeight" },
        { 124, "SystemParameters", "MinimizedWindowWidthKey", "MinimizedWindowWidth" },
        { 125, "SystemParameters", "MinimizedWindowHeightKey", "MinimizedWindowHeight" },
        { 126, "SystemParameters", "MinimizedGridWidthKey", "MinimizedGridWidth" },
        { 127, "SystemParameters", "MinimizedGridHeightKey", "MinimizedGridHeight" },
        { 128, "SystemParameters", "MinimumWindowTrackWidthKey", "MinimumWindowTrackWidth" },
        { 129, "SystemParameters", "MinimumWindowTrackHeightKey", "MinimumWindowTrackHeight" },
        { 130, "SystemParameters", "PrimaryScreenWidthKey", "PrimaryScreenWidth" },
        { 131, "SystemParameters", "PrimaryScreenHeightKey", "PrimaryScreenHeight" },
        { 132, "SystemParameters", "WindowCaptionButtonWidthKey", "WindowCaptionButtonWidth" },
        { 133, "SystemParameters", "WindowCaptionButtonHeightKey", "WindowCaptionButtonHeight" },
        { 134, "SystemParameters", "ResizeFrameHorizontalBorderHeightKey", "ResizeFrameHorizontalBorderHeight" },
        { 135, "SystemParameters", "ResizeFrameVerticalBorderWidthKey", "ResizeFrameVerticalBorderWidth" },
        { 136, "SystemParameters", "SmallIconWidthKey", "SmallIconWidth" },
        { 137, "SystemParameters", "SmallIconHeightKey", "SmallIconHeight" },
        { 138, "SystemParameters", "SmallWindowCaptionButtonWidthKey", "SmallWindowCaptionButtonWidth" },
        { 139, "SystemParameters", "SmallWindowCaptionButtonHeightKey", "SmallWindowCaptionButtonHeight" },
        { 140, "SystemParameters", "VirtualScreenWidthKey", "VirtualScreenWidth" },
        { 141, "SystemParameters", "VirtualScreenHeightKey", "VirtualScreenHeight" },
        { 142, "SystemParameters", "VerticalScrollBarWidthKey", "VerticalScrollBarWidth" },
        { 143, "SystemParameters", "VerticalScrollBarButtonHeightKey", "VerticalScrollBarButtonHeight" },
        { 144, "SystemParameters", "WindowCaptionHeightKey", "WindowCaptionHeight" },
        { 145, "SystemParameters", "KanjiWindowHeightKey", "KanjiWindowHeight" },
        { 146, "SystemParameters", "MenuBarHeightKey", "MenuBarHeight" },
        { 147, "SystemParameters", "SmallCaptionHeightKey", "SmallCaptionHeight" },
        { 148, "SystemParameters", "VerticalScrollBarThumbHeightKey", "VerticalScrollBarThumbHeight" },
        { 149, "SystemParameters", "IsImmEnabledKey", "IsImmEnabled" },
        { 150, "SystemParameters", "IsMediaCenterKey", "IsMediaCenter" },
        { 151, "SystemParameters", "IsMenuDropRightAlignedKey", "IsMenuDropRightAligned" },
        { 152, "SystemParameters", "IsMiddleEastEnabledKey", "IsMiddleEastEnabled" },
        { 153, "SystemParameters", "IsMousePresentKey", "IsMousePresent" },
        { 154, "SystemParameters", "IsMouseWheelPresentKey", "IsMouseWheelPresent" },
        { 155, "SystemParameters", "IsPenWindowsKey", "IsPenWindows" },
        { 156, "SystemParameters", "IsRemotelyControlledKey", "IsRemotelyControlled" },
        { 157, "SystemParameters", "IsRemoteSessionKey", "IsRemoteSession" },
        { 158, "SystemParameters", "ShowSoundsKey", "ShowSounds" },
        { 159, "SystemParameters", "IsSlowMachineKey", "IsSlowMachine" },
        { 160, "SystemParameters", "SwapButtonsKey", "SwapButtons" },
        { 161, "SystemParameters", "IsTabletPCKey", "IsTabletPC" },
        { 162, "SystemParameters", "VirtualScreenLeftKey", "VirtualScreenLeft" },
        { 163, "SystemParameters", "VirtualScreenTopKey", "VirtualScreenTop" },
        { 164, "SystemParameters", "FocusBorderWidthKey", "FocusBorderWidth" },
        { 165, "SystemParameters", "FocusBorderHeightKey", "FocusBorderHeight" },
        { 166, "SystemParameters", "HighContrastKey", "HighContrast" },
        { 167, "SystemParameters", "DropShadowKey", "DropShadow" },
        { 168, "SystemParameters", "FlatMenuKey", "FlatMenu" },
        { 169, "SystemParameters", "WorkAreaKey", "WorkArea" },
        { 170, "SystemParameters", "IconHorizontalSpacingKey", "IconHorizontalSpacing" },
        { 171, "SystemParameters", "IconVerticalSpacingKey", "IconVerticalSpacing" },
        { 172, "SystemParameters", "IconTitleWrapKey", "IconTitleWrap" },
        { 173, "SystemParameters", "KeyboardCuesKey", "KeyboardCues" },
        { 174, "SystemParameters", "KeyboardDelayKey", "KeyboardDelay" },
        { 175, "SystemParameters", "KeyboardPreferenceKey", "KeyboardPreference" },
        { 176, "SystemParameters", "KeyboardSpeedKey", "KeyboardSpeed" },
        { 177, "SystemParameters", "SnapToDefaultButtonKey", "SnapToDefaultButton" },
        { 178, "SystemParameters", "WheelScrollLinesKey", "WheelScrollLines" },
        { 179, "SystemParameters", "MouseHoverTimeKey", "MouseHoverTime" },
        { 180, "SystemParameters", "MouseHoverHeightKey", "MouseHoverHeight" },
        { 181, "SystemParameters", "MouseHoverWidthKey", "MouseHoverWidth" },
        { 182, "SystemParameters", "MenuDropAlignmentKey", "MenuDropAlignment" },
        { 183, "SystemParameters", "MenuFadeKey", "MenuFade" },
        { 184, "SystemParameters", "MenuShowDelayKey", "MenuShowDelay" },
        { 185, "SystemParameters", "ComboBoxAnimationKey", "ComboBoxAnimation" },
        { 186, "SystemParameters", "ClientAreaAnimationKey", "ClientAreaAnimation" },
        { 187, "SystemParameters", "CursorShadowKey", "CursorShadow" },
        { 188, "SystemParameters", "GradientCaptionsKey", "GradientCaptions" },
        { 189, "SystemParameters", "HotTrackingKey", "HotTracking" },
        { 190, "SystemParameters", "ListBoxSmoothScrollingKey", "ListBoxSmoothScrolling" },
        { 191, "SystemParameters", "MenuAnimationKey", "MenuAnimation" },
        { 192, "SystemParameters", "SelectionFadeKey", "SelectionFade" },
        { 193, "SystemParameters", "StylusHotTrackingKey", "StylusHotTracking" },
        { 194, "SystemParameters", "ToolTipAnimationKey", "ToolTipAnimation" },
        { 195, "SystemParameters", "ToolTipFadeKey", "ToolTipFade" },
        { 196, "SystemParameters", "UIEffectsKey", "UIEffects" },
        { 197, "SystemParameters", "MinimizeAnimationKey", "MinimizeAnimation" },
        { 198, "SystemParameters", "BorderKey", "Border" },
        { 199, "SystemParameters", "CaretWidthKey", "CaretWidth" },
        { 200, "SystemParameters", "ForegroundFlashCountKey", "ForegroundFlashCount" },
        { 201, "SystemParameters", "DragFullWindowsKey", "DragFullWindows" },
        { 202, "SystemParameters", "BorderWidthKey", "BorderWidth" },
        { 203, "SystemParameters", "ScrollWidthKey", "ScrollWidth" },
        { 204, "SystemParameters", "ScrollHeightKey", "ScrollHeight" },
        { 205, "SystemParameters", "CaptionWidthKey", "CaptionWidth" },
        { 206, "SystemParameters", "CaptionHeightKey", "CaptionHeight" },
        { 207, "SystemParameters", "SmallCaptionWidthKey", "SmallCaptionWidth" },
        { 208, "SystemParameters", "MenuWidthKey", "MenuWidth" },
        { 209, "SystemParameters", "MenuHeightKey", "MenuHeight" },
        { 210, "SystemParameters", "ComboBoxPopupAnimationKey", "ComboBoxPopupAnimation" },
        { 211, "SystemParameters", "MenuPopupAnimationKey", "MenuPopupAnimation" },
        { 212, "SystemParameters", "ToolTipPopupAnimationKey", "ToolTipPopupAnimation" },
        { 213, "SystemParameters", "PowerLineStatusKey", "PowerLineStatus" },
        { 215, "SystemParameters", "FocusVisualStyleKey", "FocusVisualStyle" },
        { 216, "SystemParameters", "NavigationChromeDownLevelStyleKey", "NavigationChromeDownLevelStyle" },
        { 217, "SystemParameters", "NavigationChromeStyleKey", "NavigationChromeStyle" },
        { 219, "MenuItem", "SeparatorStyleKey", "MenuItemSeparatorStyle" },
        { 220, "GridView", "GridViewScrollViewerStyleKey", "GridViewScrollViewerStyle" },
        { 221, "GridView", "GridViewStyleKey", "GridViewStyle" },
        { 222, "GridView", "GridViewItemContainerStyleKey", "GridViewItemContainerStyle" },
        { 223, "StatusBar", "SeparatorStyleKey", "StatusBarSeparatorStyle" },
        { 224, "ToolBar", "ButtonStyleKey", "ToolBarButtonStyle" },
        { 225, "ToolBar", "ToggleButtonStyleKey", "ToolBarToggleButtonStyle" },
        { 226, "ToolBar", "SeparatorStyleKey", "ToolBarSeparatorStyle" },
        { 227, "ToolBar", "CheckBoxStyleKey", "ToolBarCheckBoxStyle" },
        { 228, "ToolBar", "RadioButtonStyleKey", "ToolBarRadioButtonStyle" },
        { 229, "ToolBar", "ComboBoxStyleKey", "ToolBarComboBoxStyle" },
        { 230, "ToolBar", "TextBoxStyleKey", "ToolBarTextBoxStyle" },
        { 231, "ToolBar", "MenuStyleKey", "ToolBarMenuStyle" },
        { 234, "SystemColors", "InactiveSelectionHighlightBrushKey", "InactiveSelectionHighlightBrush" },
        { 235, "SystemColors", "InactiveSelectionHighlightTextBrushKey", "InactiveSelectionHighlightTextBrush" },
    };
    ASSERT_EQ(std::size(Baml::KnownResourcesTable), std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); i++) {
        const auto& row = Baml::KnownResourcesTable[i];
        EXPECT_EQ(row.Id, expected[i].id) << i;
        EXPECT_STREQ(row.ClassName, expected[i].cls) << i;
        EXPECT_STREQ(row.KeyName, expected[i].key) << i;
        EXPECT_STREQ(row.ResourceName, expected[i].resource) << i;
    }
}

} // namespace
