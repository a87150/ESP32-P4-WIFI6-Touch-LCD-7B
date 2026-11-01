/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/binding.hpp"

#include <cstdint>

namespace esp_brookesia::gui {

namespace {

struct BindingTargetSpec {
    std::string_view path;
    BindingTarget target;
    BindingApplyDomain domain;
    uint64_t mask;
    uint32_t accepted_node_types;
};

constexpr uint32_t node_type_mask(NodeType node_type)
{
    return 1U << static_cast<uint32_t>(node_type);
}

constexpr uint32_t LABEL_OR_CHECKBOX_NODE_TYPES =
    node_type_mask(NodeType::Label) | node_type_mask(NodeType::Checkbox);
constexpr uint32_t IMAGE_NODE_TYPES = node_type_mask(NodeType::Image);
constexpr uint32_t FRAME_VIEW_NODE_TYPES = node_type_mask(NodeType::FrameView);
constexpr uint32_t TEXT_INPUT_NODE_TYPES = node_type_mask(NodeType::TextInput);
constexpr uint32_t RANGE_NODE_TYPES =
    node_type_mask(NodeType::Slider) | node_type_mask(NodeType::ProgressBar) | node_type_mask(NodeType::Arc);
constexpr uint32_t TOGGLE_NODE_TYPES = node_type_mask(NodeType::Switch) | node_type_mask(NodeType::Checkbox);
constexpr uint32_t DROPDOWN_NODE_TYPES = node_type_mask(NodeType::Dropdown);
constexpr uint32_t TABLE_NODE_TYPES = node_type_mask(NodeType::Table);
constexpr uint32_t LINE_NODE_TYPES = node_type_mask(NodeType::Line);
constexpr uint32_t KEYBOARD_NODE_TYPES = node_type_mask(NodeType::Keyboard);
constexpr uint32_t CANVAS_NODE_TYPES = node_type_mask(NodeType::Canvas);

constexpr BindingTargetSpec make_binding_target_spec(
    std::string_view path,
    BindingTarget target,
    PropsApplyMask mask,
    uint32_t accepted_node_types = 0)
{
    return {path, target, BindingApplyDomain::Props, static_cast<uint64_t>(mask), accepted_node_types};
}

constexpr BindingTargetSpec make_binding_target_spec(
    std::string_view path,
    BindingTarget target,
    StyleApplyMask mask,
    uint32_t accepted_node_types = 0)
{
    return {path, target, BindingApplyDomain::Style, static_cast<uint64_t>(mask), accepted_node_types};
}

constexpr BindingTargetSpec make_binding_target_spec(
    std::string_view path,
    BindingTarget target,
    LayoutApplyMask mask,
    uint32_t accepted_node_types = 0)
{
    return {path, target, BindingApplyDomain::Layout, static_cast<uint64_t>(mask), accepted_node_types};
}

constexpr BindingTargetSpec make_binding_target_spec(
    std::string_view path,
    BindingTarget target,
    PlacementApplyMask mask,
    uint32_t accepted_node_types = 0)
{
    return {path, target, BindingApplyDomain::Placement, static_cast<uint64_t>(mask), accepted_node_types};
}

constexpr BindingTargetSpec BINDING_TARGET_SPECS[] = {
    make_binding_target_spec("commonProps.hidden", BindingTarget::CommonPropsHidden, PropsApplyMask::CommonHidden),
    make_binding_target_spec("commonProps.disabled", BindingTarget::CommonPropsDisabled, PropsApplyMask::CommonDisabled),
    make_binding_target_spec("commonProps.clickable", BindingTarget::CommonPropsClickable, PropsApplyMask::CommonClickable),
    make_binding_target_spec("commonProps.scrollable", BindingTarget::CommonPropsScrollable, PropsApplyMask::CommonScrollable),
    make_binding_target_spec("commonProps.pressLock", BindingTarget::CommonPropsPressLock, PropsApplyMask::CommonPressLock),
    make_binding_target_spec("commonProps.angle", BindingTarget::CommonPropsAngle, PropsApplyMask::CommonTransform),
    make_binding_target_spec("commonProps.zoom", BindingTarget::CommonPropsZoom, PropsApplyMask::CommonTransform),
    make_binding_target_spec("commonProps.pivotX", BindingTarget::CommonPropsPivotX, PropsApplyMask::CommonTransform),
    make_binding_target_spec("commonProps.pivotY", BindingTarget::CommonPropsPivotY, PropsApplyMask::CommonTransform),
    make_binding_target_spec("labelProps.text", BindingTarget::LabelPropsText, PropsApplyMask::LabelText, LABEL_OR_CHECKBOX_NODE_TYPES),
    make_binding_target_spec("imageProps.src", BindingTarget::ImagePropsSrc, PropsApplyMask::ImageSource, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.recolor", BindingTarget::ImagePropsRecolor, PropsApplyMask::ImageRecolor, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.recolorOpacity", BindingTarget::ImagePropsRecolorOpacity, PropsApplyMask::ImageRecolor, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.angle", BindingTarget::ImagePropsAngle, PropsApplyMask::ImageAngle, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.offsetX", BindingTarget::ImagePropsOffsetX, PropsApplyMask::ImageOffsetX, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.offsetY", BindingTarget::ImagePropsOffsetY, PropsApplyMask::ImageOffsetY, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.zoom", BindingTarget::ImagePropsZoom, PropsApplyMask::ImageZoom, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.pivotX", BindingTarget::ImagePropsPivotX, PropsApplyMask::ImagePivot, IMAGE_NODE_TYPES),
    make_binding_target_spec("imageProps.pivotY", BindingTarget::ImagePropsPivotY, PropsApplyMask::ImagePivot, IMAGE_NODE_TYPES),
    make_binding_target_spec("frameViewProps.autoRegisterOutput", BindingTarget::FrameViewPropsAutoRegisterOutput, PropsApplyMask::FrameViewConfig, FRAME_VIEW_NODE_TYPES),
    make_binding_target_spec("frameViewProps.outputName", BindingTarget::FrameViewPropsOutputName, PropsApplyMask::FrameViewConfig, FRAME_VIEW_NODE_TYPES),
    make_binding_target_spec("frameViewProps.colorFormat", BindingTarget::FrameViewPropsColorFormat, PropsApplyMask::FrameViewConfig, FRAME_VIEW_NODE_TYPES),
    make_binding_target_spec("textInputProps.text", BindingTarget::TextInputPropsText, PropsApplyMask::TextInputText, TEXT_INPUT_NODE_TYPES),
    make_binding_target_spec("textInputProps.placeholder", BindingTarget::TextInputPropsPlaceholder, PropsApplyMask::TextInputPlaceholder, TEXT_INPUT_NODE_TYPES),
    make_binding_target_spec("textInputProps.password", BindingTarget::TextInputPropsPassword, PropsApplyMask::TextInputPassword, TEXT_INPUT_NODE_TYPES),
    make_binding_target_spec("textInputProps.multiline", BindingTarget::TextInputPropsMultiline, PropsApplyMask::TextInputMultiline, TEXT_INPUT_NODE_TYPES),
    make_binding_target_spec("textInputProps.maxLength", BindingTarget::TextInputPropsMaxLength, PropsApplyMask::TextInputMaxLength, TEXT_INPUT_NODE_TYPES),
    make_binding_target_spec("rangeProps.value", BindingTarget::RangePropsValue, PropsApplyMask::RangeValue, RANGE_NODE_TYPES),
    make_binding_target_spec("rangeProps.min", BindingTarget::RangePropsMin, PropsApplyMask::RangeRange, RANGE_NODE_TYPES),
    make_binding_target_spec("rangeProps.max", BindingTarget::RangePropsMax, PropsApplyMask::RangeRange, RANGE_NODE_TYPES),
    make_binding_target_spec("rangeProps.step", BindingTarget::RangePropsStep, PropsApplyMask::RangeRange, RANGE_NODE_TYPES),
    make_binding_target_spec("toggleProps.checked", BindingTarget::TogglePropsChecked, PropsApplyMask::ToggleChecked, TOGGLE_NODE_TYPES),
    make_binding_target_spec("dropdownProps.options", BindingTarget::DropdownPropsOptions, PropsApplyMask::DropdownOptions, DROPDOWN_NODE_TYPES),
    make_binding_target_spec("dropdownProps.selectedIndex", BindingTarget::DropdownPropsSelectedIndex, PropsApplyMask::DropdownSelectedIndex, DROPDOWN_NODE_TYPES),
    make_binding_target_spec("tableProps.rows", BindingTarget::TablePropsRows, PropsApplyMask::TableRows, TABLE_NODE_TYPES),
    make_binding_target_spec("tableProps.columns", BindingTarget::TablePropsColumns, PropsApplyMask::TableColumns, TABLE_NODE_TYPES),
    make_binding_target_spec("tableProps.cells", BindingTarget::TablePropsCells, PropsApplyMask::TableCells, TABLE_NODE_TYPES),
    make_binding_target_spec("lineProps.points", BindingTarget::LinePropsPoints, PropsApplyMask::LinePoints, LINE_NODE_TYPES),
    make_binding_target_spec("keyboardProps.mode", BindingTarget::KeyboardPropsMode, PropsApplyMask::KeyboardMode, KEYBOARD_NODE_TYPES),
    make_binding_target_spec("keyboardProps.popovers", BindingTarget::KeyboardPropsPopovers, PropsApplyMask::KeyboardPopovers, KEYBOARD_NODE_TYPES),
    make_binding_target_spec("keyboardProps.allowedModes", BindingTarget::KeyboardPropsAllowedModes, PropsApplyMask::KeyboardConfig, KEYBOARD_NODE_TYPES),
    make_binding_target_spec("keyboardProps.iconSize", BindingTarget::KeyboardPropsIconSize, PropsApplyMask::KeyboardConfig, KEYBOARD_NODE_TYPES),
    make_binding_target_spec("canvasProps.commands", BindingTarget::CanvasPropsCommands, PropsApplyMask::CanvasCommands, CANVAS_NODE_TYPES),
    make_binding_target_spec("style.bgColor", BindingTarget::StyleBgColor, StyleApplyMask::Color),
    make_binding_target_spec("style.bgGradientColor", BindingTarget::StyleBgGradientColor, StyleApplyMask::All),
    make_binding_target_spec("style.bgGradientDirection", BindingTarget::StyleBgGradientDirection, StyleApplyMask::All),
    make_binding_target_spec("style.textColor", BindingTarget::StyleTextColor, StyleApplyMask::Color),
    make_binding_target_spec("style.borderColor", BindingTarget::StyleBorderColor, StyleApplyMask::Color),
    make_binding_target_spec("style.lineColor", BindingTarget::StyleLineColor, StyleApplyMask::Color),
    make_binding_target_spec("style.arcColor", BindingTarget::StyleArcColor, StyleApplyMask::All),
    make_binding_target_spec("style.arcGradientColor", BindingTarget::StyleArcGradientColor, StyleApplyMask::All),
    make_binding_target_spec("style.font", BindingTarget::StyleFont, StyleApplyMask::Font),
    make_binding_target_spec("style.bgMainStop", BindingTarget::StyleBgMainStop, StyleApplyMask::All),
    make_binding_target_spec("style.bgGradientStop", BindingTarget::StyleBgGradientStop, StyleApplyMask::All),
    make_binding_target_spec("style.bgGradientOpacity", BindingTarget::StyleBgGradientOpacity, StyleApplyMask::All),
    make_binding_target_spec("style.borderWidth", BindingTarget::StyleBorderWidth, StyleApplyMask::Border),
    make_binding_target_spec("style.radius", BindingTarget::StyleRadius, StyleApplyMask::Radius),
    make_binding_target_spec("style.padding", BindingTarget::StylePadding, StyleApplyMask::Padding),
    make_binding_target_spec("style.paddingLeft", BindingTarget::StylePaddingLeft, StyleApplyMask::Padding),
    make_binding_target_spec("style.paddingRight", BindingTarget::StylePaddingRight, StyleApplyMask::Padding),
    make_binding_target_spec("style.paddingTop", BindingTarget::StylePaddingTop, StyleApplyMask::Padding),
    make_binding_target_spec("style.paddingBottom", BindingTarget::StylePaddingBottom, StyleApplyMask::Padding),
    make_binding_target_spec("style.margin", BindingTarget::StyleMargin, StyleApplyMask::Margin),
    make_binding_target_spec("style.marginLeft", BindingTarget::StyleMarginLeft, StyleApplyMask::Margin),
    make_binding_target_spec("style.marginRight", BindingTarget::StyleMarginRight, StyleApplyMask::Margin),
    make_binding_target_spec("style.marginTop", BindingTarget::StyleMarginTop, StyleApplyMask::Margin),
    make_binding_target_spec("style.marginBottom", BindingTarget::StyleMarginBottom, StyleApplyMask::Margin),
    make_binding_target_spec("style.shadowWidth", BindingTarget::StyleShadowWidth, StyleApplyMask::Shadow),
    make_binding_target_spec("style.shadowOffsetX", BindingTarget::StyleShadowOffsetX, StyleApplyMask::Shadow),
    make_binding_target_spec("style.shadowOffsetY", BindingTarget::StyleShadowOffsetY, StyleApplyMask::Shadow),
    make_binding_target_spec("style.shadowColor", BindingTarget::StyleShadowColor, StyleApplyMask::Shadow | StyleApplyMask::Color),
    make_binding_target_spec("style.opacity", BindingTarget::StyleOpacity, StyleApplyMask::Opacity),
    make_binding_target_spec("style.lineWidth", BindingTarget::StyleLineWidth, StyleApplyMask::Line),
    make_binding_target_spec("style.imageOpacity", BindingTarget::StyleImageOpacity, StyleApplyMask::ImageOpacity),
    make_binding_target_spec("style.imageRecolor", BindingTarget::StyleImageRecolor, StyleApplyMask::ImageRecolor),
    make_binding_target_spec("style.imageRecolorOpacity", BindingTarget::StyleImageRecolorOpacity, StyleApplyMask::ImageRecolor),
    make_binding_target_spec("style.fontSize", BindingTarget::StyleFontSize, StyleApplyMask::Font),
    make_binding_target_spec("style.imageFontSize", BindingTarget::StyleImageFontSize, StyleApplyMask::Font),
    make_binding_target_spec("style.arcWidth", BindingTarget::StyleArcWidth, StyleApplyMask::All),
    make_binding_target_spec("style.arcOpacity", BindingTarget::StyleArcOpacity, StyleApplyMask::All),
    make_binding_target_spec("style.arcGradientSegments", BindingTarget::StyleArcGradientSegments, StyleApplyMask::All),
    make_binding_target_spec("style.arcRounded", BindingTarget::StyleArcRounded, StyleApplyMask::All),
    make_binding_target_spec("layout.type", BindingTarget::LayoutType, LayoutApplyMask::All),
    make_binding_target_spec("layout.flexFlow", BindingTarget::LayoutFlexFlow, LayoutApplyMask::FlexFlow),
    make_binding_target_spec("layout.mainAlign", BindingTarget::LayoutMainAlign, LayoutApplyMask::Align),
    make_binding_target_spec("layout.crossAlign", BindingTarget::LayoutCrossAlign, LayoutApplyMask::Align),
    make_binding_target_spec("layout.gap", BindingTarget::LayoutGap, LayoutApplyMask::Gap),
    make_binding_target_spec("layout.gridTemplateColumns", BindingTarget::LayoutGridTemplateColumns, LayoutApplyMask::GridTracks),
    make_binding_target_spec("layout.gridTemplateRows", BindingTarget::LayoutGridTemplateRows, LayoutApplyMask::GridTracks),
    make_binding_target_spec("placement.mode", BindingTarget::PlacementMode, PlacementApplyMask::All),
    make_binding_target_spec("placement.width", BindingTarget::PlacementWidth, PlacementApplyMask::Size),
    make_binding_target_spec("placement.height", BindingTarget::PlacementHeight, PlacementApplyMask::Size),
    make_binding_target_spec("placement.aspectRatio", BindingTarget::PlacementAspectRatio, PlacementApplyMask::Size),
    make_binding_target_spec("placement.x", BindingTarget::PlacementX, PlacementApplyMask::Position),
    make_binding_target_spec("placement.y", BindingTarget::PlacementY, PlacementApplyMask::Position),
    make_binding_target_spec("placement.align", BindingTarget::PlacementAlign, PlacementApplyMask::Align),
    make_binding_target_spec("placement.relativeTo", BindingTarget::PlacementRelativeTo, PlacementApplyMask::RelativeTo),
    make_binding_target_spec("placement.gridColumn", BindingTarget::PlacementGridColumn, PlacementApplyMask::GridCell),
    make_binding_target_spec("placement.gridRow", BindingTarget::PlacementGridRow, PlacementApplyMask::GridCell),
    make_binding_target_spec("placement.gridColumnSpan", BindingTarget::PlacementGridColumnSpan, PlacementApplyMask::GridCell),
    make_binding_target_spec("placement.gridRowSpan", BindingTarget::PlacementGridRowSpan, PlacementApplyMask::GridCell),
    make_binding_target_spec("placement.alignSelf", BindingTarget::PlacementAlignSelf, PlacementApplyMask::AlignSelf),
    make_binding_target_spec("placement.flexGrow", BindingTarget::PlacementFlexGrow, PlacementApplyMask::FlexGrow),
};

static_assert(sizeof(BINDING_TARGET_SPECS) / sizeof(BINDING_TARGET_SPECS[0]) == 103);

bool supports_node_type(NodeType node_type, uint32_t accepted_node_types)
{
    if (accepted_node_types == 0) {
        return true;
    }

    const auto raw_node_type = static_cast<uint32_t>(node_type);
    return raw_node_type < 32 && (accepted_node_types & (1U << raw_node_type)) != 0;
}

BindingTargetInfo make_binding_target_info(const BindingTargetSpec &spec)
{
    BindingTargetInfo info;
    info.target = spec.target;
    info.domain = spec.domain;

    switch (spec.domain) {
    case BindingApplyDomain::Props:
        info.props_mask = static_cast<PropsApplyMask>(spec.mask);
        break;
    case BindingApplyDomain::Style:
        info.style_mask = static_cast<StyleApplyMask>(spec.mask);
        break;
    case BindingApplyDomain::Layout:
        info.layout_mask = static_cast<LayoutApplyMask>(spec.mask);
        break;
    case BindingApplyDomain::Placement:
        info.placement_mask = static_cast<PlacementApplyMask>(spec.mask);
        break;
    }

    return info;
}

std::expected<BindingTargetInfo, std::string> resolve_state_style_target(
    NodeType node_type,
    std::string_view full_path,
    std::string_view remaining)
{
    const auto separator = remaining.find('.');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= remaining.size()) {
        return std::unexpected(
                   "Binding path '" + std::string(full_path) +
                   "' must use stateStyles.<state>.<styleField>"
               );
    }

    const std::string state_name(remaining.substr(0, separator));
    if (!is_supported_style_state_name(state_name)) {
        return std::unexpected("Unsupported stateStyles state in binding path: " + state_name);
    }

    const std::string style_field(remaining.substr(separator + 1));
    auto base_target = resolve_binding_target(node_type, "style." + style_field);
    if (!base_target) {
        return std::unexpected(base_target.error());
    }

    if (base_target->domain != BindingApplyDomain::Style ||
            base_target->target == BindingTarget::StyleFont ||
            base_target->target == BindingTarget::StyleFontSize) {
        return std::unexpected(
                   "Binding path '" + std::string(full_path) +
                   "' uses a style field that is not supported by stateStyles"
               );
    }

    base_target->style_state = state_name;
    base_target->style_mask = StyleApplyMask::All;
    return base_target;
}

std::expected<BindingTargetInfo, std::string> resolve_part_style_target(
    NodeType node_type,
    std::string_view path)
{
    constexpr std::string_view PREFIX = "partStyles.";
    const auto remaining = path.substr(PREFIX.size());
    const auto separator = remaining.find('.');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= remaining.size()) {
        return std::unexpected(
                   "Binding path '" + std::string(path) +
                   "' must use partStyles.<part>.<styleField> or "
                   "partStyles.<part>.stateStyles.<state>.<styleField>"
               );
    }

    const std::string part_name(remaining.substr(0, separator));
    if (!is_supported_style_part_name(part_name)) {
        return std::unexpected("Unsupported partStyles part in binding path: " + part_name);
    }

    auto style_field = remaining.substr(separator + 1);
    std::expected<BindingTargetInfo, std::string> target =
        std::unexpected(std::string("Invalid partStyles binding target"));
    if (style_field.starts_with("stateStyles.")) {
        target = resolve_state_style_target(
                     node_type,
                     path,
                     style_field.substr(std::string_view("stateStyles.").size())
                 );
    } else {
        if (style_field.starts_with("style.")) {
            style_field = style_field.substr(std::string_view("style.").size());
        }
        target = resolve_binding_target(node_type, "style." + std::string(style_field));
        if (target && target->domain == BindingApplyDomain::Style &&
                (target->target == BindingTarget::StyleFont || target->target == BindingTarget::StyleFontSize)) {
            return std::unexpected(
                       "Binding path '" + std::string(path) +
                       "' uses a style field that is not supported by partStyles"
                   );
        }
    }

    if (!target) {
        return std::unexpected(target.error());
    }
    if (target->domain != BindingApplyDomain::Style) {
        return std::unexpected("Binding path '" + std::string(path) + "' must target a style field");
    }

    target->style_part = part_name;
    target->style_mask = StyleApplyMask::All;
    return target;
}

} // namespace

std::expected<BindingTargetInfo, std::string> resolve_binding_target(
    NodeType node_type,
    std::string_view path)
{
    if (path.empty()) {
        return std::unexpected("Binding path must not be empty");
    }
    if (path.find('_') != std::string_view::npos) {
        return std::unexpected(
                   "Binding path '" + std::string(path) +
                   "' must use public camelCase field names; snake_case is no longer supported"
               );
    }
    if (path == "text") {
        return std::unexpected(
                   "Binding path 'text' is no longer supported; use 'labelProps.text' or "
                   "'textInputProps.text'"
               );
    }
    if (path == "src") {
        return std::unexpected("Binding path 'src' is no longer supported; use 'imageProps.src'");
    }
    if (path.starts_with("props.")) {
        return std::unexpected(
                   "Binding path '" + std::string(path) +
                   "' still uses the legacy 'props' object; move it to the new props domain"
               );
    }

    if (path.starts_with("partStyles.")) {
        return resolve_part_style_target(node_type, path);
    }
    if (path.starts_with("stateStyles.")) {
        constexpr std::string_view PREFIX = "stateStyles.";
        return resolve_state_style_target(node_type, path, path.substr(PREFIX.size()));
    }

    for (const auto &spec : BINDING_TARGET_SPECS) {
        if (spec.path != path) {
            continue;
        }
        if (!supports_node_type(node_type, spec.accepted_node_types)) {
            return std::unexpected(
                       "Binding path '" + std::string(path) + "' is not supported for node type '" +
                       BROOKESIA_DESCRIBE_ENUM_TO_STR(node_type) + "'"
                   );
        }
        return make_binding_target_info(spec);
    }

    return std::unexpected("Unsupported binding path: " + std::string(path));
}

} // namespace esp_brookesia::gui
