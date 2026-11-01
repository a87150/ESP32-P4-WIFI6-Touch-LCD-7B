/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "brookesia/gui_interface/backend.hpp"
#include "brookesia/gui_interface/document.hpp"

namespace esp_brookesia::gui {

enum class BindingTarget {
    CommonPropsHidden,
    CommonPropsDisabled,
    CommonPropsClickable,
    CommonPropsScrollable,
    CommonPropsPressLock,
    CommonPropsAngle,
    CommonPropsZoom,
    CommonPropsPivotX,
    CommonPropsPivotY,
    LabelPropsText,
    ImagePropsSrc,
    ImagePropsRecolor,
    ImagePropsRecolorOpacity,
    ImagePropsAngle,
    ImagePropsOffsetX,
    ImagePropsOffsetY,
    ImagePropsZoom,
    ImagePropsPivotX,
    ImagePropsPivotY,
    FrameViewPropsAutoRegisterOutput,
    FrameViewPropsOutputName,
    FrameViewPropsColorFormat,
    TextInputPropsText,
    TextInputPropsPlaceholder,
    TextInputPropsPassword,
    TextInputPropsMultiline,
    TextInputPropsMaxLength,
    RangePropsValue,
    RangePropsMin,
    RangePropsMax,
    RangePropsStep,
    TogglePropsChecked,
    DropdownPropsOptions,
    DropdownPropsSelectedIndex,
    TablePropsRows,
    TablePropsColumns,
    TablePropsCells,
    LinePropsPoints,
    KeyboardPropsMode,
    KeyboardPropsPopovers,
    KeyboardPropsAllowedModes,
    KeyboardPropsIconSize,
    CanvasPropsCommands,
    StyleBgColor,
    StyleBgGradientColor,
    StyleBgGradientDirection,
    StyleTextColor,
    StyleBorderColor,
    StyleLineColor,
    StyleArcColor,
    StyleArcGradientColor,
    StyleFont,
    StyleBgMainStop,
    StyleBgGradientStop,
    StyleBgGradientOpacity,
    StyleBorderWidth,
    StyleRadius,
    StylePadding,
    StylePaddingLeft,
    StylePaddingRight,
    StylePaddingTop,
    StylePaddingBottom,
    StyleMargin,
    StyleMarginLeft,
    StyleMarginRight,
    StyleMarginTop,
    StyleMarginBottom,
    StyleShadowWidth,
    StyleShadowOffsetX,
    StyleShadowOffsetY,
    StyleShadowColor,
    StyleOpacity,
    StyleLineWidth,
    StyleImageOpacity,
    StyleImageRecolor,
    StyleImageRecolorOpacity,
    StyleFontSize,
    StyleImageFontSize,
    StyleArcWidth,
    StyleArcOpacity,
    StyleArcGradientSegments,
    StyleArcRounded,
    LayoutType,
    LayoutFlexFlow,
    LayoutMainAlign,
    LayoutCrossAlign,
    LayoutGap,
    LayoutGridTemplateColumns,
    LayoutGridTemplateRows,
    PlacementMode,
    PlacementWidth,
    PlacementHeight,
    PlacementAspectRatio,
    PlacementX,
    PlacementY,
    PlacementAlign,
    PlacementRelativeTo,
    PlacementGridColumn,
    PlacementGridRow,
    PlacementGridColumnSpan,
    PlacementGridRowSpan,
    PlacementAlignSelf,
    PlacementFlexGrow,
};

enum class BindingApplyDomain {
    Props,
    Style,
    Layout,
    Placement,
};

struct BindingTargetInfo {
    BindingTarget target;
    BindingApplyDomain domain;
    PropsApplyMask props_mask = PropsApplyMask::None;
    StyleApplyMask style_mask = StyleApplyMask::None;
    LayoutApplyMask layout_mask = LayoutApplyMask::None;
    PlacementApplyMask placement_mask = PlacementApplyMask::None;
    std::string style_part;
    std::string style_state;
};

inline bool binding_uses_legacy_store_prefix(std::string_view expression)
{
    return expression.starts_with("store.");
}

inline std::expected<std::string, std::string> normalize_binding_store_key(std::string_view expression)
{
    if (expression.empty()) {
        return std::unexpected("Binding store key must not be empty");
    }
    if (binding_uses_legacy_store_prefix(expression)) {
        return std::unexpected(
                   "Binding value '" + std::string(expression) +
                   "' must not use the legacy 'store.' prefix; use the bare store key instead"
               );
    }
    return std::string(expression);
}

std::expected<BindingTargetInfo, std::string> resolve_binding_target(
    NodeType node_type,
    std::string_view path);

} // namespace esp_brookesia::gui
