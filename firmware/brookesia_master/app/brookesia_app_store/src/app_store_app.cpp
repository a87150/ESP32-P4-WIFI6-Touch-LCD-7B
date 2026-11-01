/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/app_store_details.hpp"
#include "private/app_store_impl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "boost/json.hpp"

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_helper/system/device.hpp"
#include "brookesia/service_helper/network/http.hpp"
#include "brookesia/service_helper/system/storage.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::app::app_store {
namespace detail {

using HttpHelper = service::helper::Http;
using DeviceHelper = service::helper::Device;
using StorageHelper = service::helper::Storage;

std::string make_app_version()
{
    return std::to_string(BROOKESIA_APP_STORE_VER_MAJOR) + "." +
           std::to_string(BROOKESIA_APP_STORE_VER_MINOR) + "." +
           std::to_string(BROOKESIA_APP_STORE_VER_PATCH);
}

void add_binding(
    std::vector<gui::BindingValueUpdate> &updates,
    std::string absolute_path,
    std::string key,
    std::string value
)
{
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = std::move(absolute_path),
        .key = std::move(key),
        .value = std::move(value),
    });
}

std::string bool_value(bool value)
{
    return value ? "true" : "false";
}

size_t get_list_page_size()
{
    return static_cast<size_t>(std::clamp(BROOKESIA_APP_STORE_LIST_PAGE_SIZE, 1, 64));
}

size_t get_page_count(size_t total_count)
{
    const auto page_size = get_list_page_size();
    if (total_count == 0) {
        return 1;
    }
    return (total_count + page_size - 1) / page_size;
}

size_t get_page_start(size_t page)
{
    return page * get_list_page_size();
}

size_t get_page_visible_count(size_t total_count, size_t page)
{
    const auto start = get_page_start(page);
    if (start >= total_count) {
        return 0;
    }
    return std::min(get_list_page_size(), total_count - start);
}

std::string make_page_text(size_t page, size_t page_count)
{
    const auto safe_page_count = std::max<size_t>(page_count, 1);
    const auto display_page = std::min(page + 1, safe_page_count);
    return std::to_string(display_page) + " / " + std::to_string(safe_page_count);
}

std::string join_path(std::string_view parent, std::string_view child)
{
    std::string path(parent);
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    path += child;
    return path;
}

std::string safe_name(std::string_view value)
{
    std::string result;
    for (const auto ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('_');
        }
    }
    return result.empty() ? "app" : result;
}

std::string read_text_file(const std::filesystem::path &path)
{
    auto result = StorageHelper::fs_read_text(path.generic_string());
    return result ? result.value() : std::string();
}

bool write_text_file(const std::filesystem::path &path, std::string_view text)
{
    auto parent_result = StorageHelper::fs_mkdir(path.parent_path().generic_string());
    if (!parent_result) {
        return false;
    }
    auto result = StorageHelper::fs_write_text(path.generic_string(), std::string(text));
    return result.has_value();
}

void append_unique_path(std::vector<std::filesystem::path> &paths, std::filesystem::path path)
{
    path = path.lexically_normal();
    const auto path_text = path.generic_string();
    const auto exists = std::any_of(
                            paths.begin(),
                            paths.end(),
    [&path_text](const std::filesystem::path & item) {
        return item.generic_string() == path_text;
    }
                        );
    if (!exists) {
        paths.push_back(std::move(path));
    }
}

bool directory_is_writable(const std::filesystem::path &path)
{
    auto mkdir_result = StorageHelper::fs_mkdir(path.generic_string());
    return mkdir_result.has_value();
}

bool is_http_time_syncing_error(std::string_view error)
{
    return error.find("state(TimeSyncing)") != std::string_view::npos;
}

std::string string_array_join(const std::vector<std::string> &items)
{
    std::string result;
    for (const auto &item : items) {
        if (!result.empty()) {
            result += ", ";
        }
        result += item;
    }
    return result;
}

void append_detail(std::string &detail, std::string item)
{
    if (item.empty()) {
        return;
    }
    if (!detail.empty()) {
        detail += " | ";
    }
    detail += std::move(item);
}

std::optional<uint64_t> display_size_from_file_size(uintmax_t bytes)
{
    if (bytes == 0) {
        return std::nullopt;
    }
    if (bytes > static_cast<uintmax_t>(std::numeric_limits<uint64_t>::max())) {
        return std::numeric_limits<uint64_t>::max();
    }
    return static_cast<uint64_t>(bytes);
}

bool is_decimal_segment(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch));
    });
}

uint64_t parse_decimal_segment(std::string_view value)
{
    uint64_t result = 0;
    for (const auto ch : value) {
        const auto digit = static_cast<uint64_t>(ch - '0');
        if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return std::numeric_limits<uint64_t>::max();
        }
        result = result * 10 + digit;
    }
    return result;
}

std::vector<std::string> split_version(std::string_view version)
{
    std::vector<std::string> segments;
    size_t start = 0;
    while (start <= version.size()) {
        const auto end = version.find('.', start);
        if (end == std::string_view::npos) {
            segments.emplace_back(version.substr(start));
            break;
        }
        segments.emplace_back(version.substr(start, end - start));
        start = end + 1;
    }
    return segments;
}

int compare_versions(std::string_view lhs, std::string_view rhs)
{
    if (lhs == rhs) {
        return 0;
    }
    if (lhs.empty() || rhs.empty()) {
        return 0;
    }

    const auto lhs_segments = split_version(lhs);
    const auto rhs_segments = split_version(rhs);
    const auto count = std::max(lhs_segments.size(), rhs_segments.size());
    for (size_t i = 0; i < count; ++i) {
        const std::string lhs_segment = i < lhs_segments.size() ? lhs_segments[i] : "0";
        const std::string rhs_segment = i < rhs_segments.size() ? rhs_segments[i] : "0";
        if (is_decimal_segment(lhs_segment) && is_decimal_segment(rhs_segment)) {
            const auto lhs_value = parse_decimal_segment(lhs_segment);
            const auto rhs_value = parse_decimal_segment(rhs_segment);
            if (lhs_value != rhs_value) {
                return lhs_value < rhs_value ? -1 : 1;
            }
            continue;
        }
        if (lhs_segment != rhs_segment) {
            return lhs_segment < rhs_segment ? -1 : 1;
        }
    }
    return 0;
}

std::string format_bytes(uint64_t bytes)
{
    static constexpr std::array<std::string_view, 4> UNITS = {"B", "KiB", "MiB", "GiB"};
    double value = static_cast<double>(bytes);
    size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < UNITS.size()) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream oss;
    if (unit_index == 0 || value >= 100.0) {
        oss << static_cast<uint64_t>(value);
    } else {
        oss << std::fixed << std::setprecision(value >= 10.0 ? 1 : 2) << value;
    }
    oss << ' ' << UNITS[unit_index];
    return oss.str();
}

std::string normalize_path_for_prefix(std::filesystem::path path)
{
    auto text = path.lexically_normal().generic_string();
    while (text.size() > 1 && text.back() == '/') {
        text.pop_back();
    }
    return text;
}

bool path_has_prefix(std::string_view path, std::string_view prefix)
{
    if (prefix.empty() || path.size() < prefix.size() || path.substr(0, prefix.size()) != prefix) {
        return false;
    }
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

std::string first_utf8_character(std::string_view text)
{
    if (text.empty()) {
        return "A";
    }
    const auto first = static_cast<unsigned char>(text.front());
    size_t length = 1;
    if ((first & 0x80U) == 0) {
        length = 1;
    } else if ((first & 0xE0U) == 0xC0U) {
        length = 2;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
    }
    length = std::min(length, text.size());
    return std::string(text.substr(0, length));
}

std::map<std::string, std::string> parse_localized_map(const boost::json::value &value)
{
    std::map<std::string, std::string> result;
    if (value.is_object()) {
        for (const auto &[key, item] : value.as_object()) {
            if (item.is_string()) {
                auto locale = std::string(key);
                if (locale == "zh-CN") {
                    locale = "zh_CN";
                }
                result[locale] = item.as_string().c_str();
            }
        }
        return result;
    }
    if (value.is_array()) {
        for (const auto &item : value.as_array()) {
            if (!item.is_array() || item.as_array().size() < 2) {
                continue;
            }
            const auto &locale_value = item.as_array()[0];
            const auto &text_value = item.as_array()[1];
            if (!locale_value.is_string() || !text_value.is_string()) {
                continue;
            }
            auto locale = std::string(locale_value.as_string().c_str());
            if (locale == "zh-CN") {
                locale = "zh_CN";
            }
            result[locale] = text_value.as_string().c_str();
        }
    }
    return result;
}

std::string get_string_field(const boost::json::object &object, std::string_view key)
{
    auto it = object.find(key);
    if (it == object.end() || !it->value().is_string()) {
        return {};
    }
    return it->value().as_string().c_str();
}

std::optional<uint64_t> get_size_field(const boost::json::object &object, std::string_view key)
{
    auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }

    const auto &value = it->value();
    if (value.is_uint64()) {
        const auto size = value.as_uint64();
        return size > 0 ? std::optional<uint64_t>(size) : std::nullopt;
    }
    if (value.is_int64()) {
        const auto size = value.as_int64();
        return size > 0 ? std::optional<uint64_t>(static_cast<uint64_t>(size)) : std::nullopt;
    }
    return std::nullopt;
}

std::expected<std::unordered_map<std::string, std::string>, std::string> load_i18n_strings(
    const std::filesystem::path &resource_dir,
    std::string_view locale
)
{
    auto path = resource_dir / "res" / "i18n" / (std::string(locale) + ".json");
    auto text = read_text_file(path);
    if (text.empty() && locale != LOCALE_EN) {
        path = resource_dir / "res" / "i18n" / (std::string(LOCALE_EN) + ".json");
        text = read_text_file(path);
    }
    if (text.empty()) {
        return std::unexpected("Failed to read App Store i18n file: " + path.generic_string());
    }

    boost::system::error_code error_code;
    auto parsed = boost::json::parse(text, error_code);
    if (error_code || !parsed.is_object()) {
        return std::unexpected("Failed to parse App Store i18n file: " + path.generic_string());
    }

    const auto &root = parsed.as_object();
    const auto *data_value = root.if_contains("data");
    if (data_value == nullptr || !data_value->is_object()) {
        return std::unexpected("App Store i18n file must contain object data: " + path.generic_string());
    }
    const auto &data = data_value->as_object();
    const auto *app_store_value = data.if_contains("appStore");
    if (app_store_value == nullptr || !app_store_value->is_object()) {
        return std::unexpected("App Store i18n file must contain object data.appStore: " + path.generic_string());
    }
    const auto &app_store = app_store_value->as_object();
    const auto *i18n_value = app_store.if_contains("i18n");
    if (i18n_value == nullptr || !i18n_value->is_object()) {
        return std::unexpected("App Store i18n file must contain object data.appStore.i18n: " + path.generic_string());
    }

    std::unordered_map<std::string, std::string> strings;
    for (const auto &[key, value] : i18n_value->as_object()) {
        if (value.is_string()) {
            strings.emplace(std::string(key), value.as_string().c_str());
        }
    }
    return strings;
}

std::string localized_app_name(std::string_view locale)
{
    const std::array resource_roots = {
        std::filesystem::path(BROOKESIA_APP_STORE_PACKAGE_DIR),
        std::filesystem::path(BROOKESIA_APP_STORE_RESOURCE_DIR),
    };
    for (const auto &resource_root : resource_roots) {
        if (resource_root.empty()) {
            continue;
        }

        auto strings = load_i18n_strings(resource_root, locale);
        if (strings) {
            auto it = strings->find(APP_NAME_I18N_KEY);
            if (it != strings->end()) {
                return it->second;
            }
        }
    }
    return (locale == LOCALE_ZH_CN) ? APP_NAME_ZH_CN : APP_NAME;
}

std::vector<std::string> get_string_array_field(const boost::json::object &object, std::string_view key)
{
    std::vector<std::string> result;
    auto it = object.find(key);
    if (it == object.end() || !it->value().is_array()) {
        return result;
    }
    for (const auto &item : it->value().as_array()) {
        if (item.is_string()) {
            result.push_back(item.as_string().c_str());
        }
    }
    return result;
}

std::optional<std::pair<int32_t, int32_t>> read_png_size(const std::filesystem::path &path)
{
    auto info = StorageHelper::fs_stat(path.generic_string());
    if (!info || info->type != StorageHelper::FileType::File || info->size < 24) {
        return std::nullopt;
    }

    unsigned char header[24] = {};
    auto read_result = StorageHelper::fs_read(path.generic_string(), service::RawBuffer(header, sizeof(header)));
    if (!read_result || read_result.value() != sizeof(header)) {
        return std::nullopt;
    }
    static constexpr unsigned char PNG_MAGIC[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!std::equal(std::begin(PNG_MAGIC), std::end(PNG_MAGIC), header)) {
        return std::nullopt;
    }
    const auto width = static_cast<int32_t>(
                           (static_cast<uint32_t>(header[16]) << 24) |
                           (static_cast<uint32_t>(header[17]) << 16) |
                           (static_cast<uint32_t>(header[18]) << 8) |
                           static_cast<uint32_t>(header[19])
                       );
    const auto height = static_cast<int32_t>(
                            (static_cast<uint32_t>(header[20]) << 24) |
                            (static_cast<uint32_t>(header[21]) << 16) |
                            (static_cast<uint32_t>(header[22]) << 8) |
                            static_cast<uint32_t>(header[23])
                        );
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    return std::make_pair(width, height);
}

void remove_invalid_icon_file(const std::filesystem::path &path, std::string_view reason)
{
    auto info = StorageHelper::fs_stat(path.generic_string());
    if (!info || !info->exists) {
        return;
    }
    auto remove_result = StorageHelper::fs_remove(path.generic_string());
    if (remove_result) {
        BROOKESIA_LOGW(
            "Removed invalid App Store icon cache file: path(%1%), reason(%2%)",
            path.generic_string(), reason
        );
    } else {
        BROOKESIA_LOGW(
            "Failed to remove invalid App Store icon cache file: path(%1%), reason(%2%), error(%3%)",
            path.generic_string(), reason, remove_result.error()
        );
    }
}

void remove_cache_file_if_exists(const std::filesystem::path &path, std::string_view reason)
{
    if (path.empty()) {
        return;
    }

    auto info = StorageHelper::fs_stat(path.generic_string());
    if (!info || !info->exists) {
        return;
    }
    auto remove_result = StorageHelper::fs_remove(path.generic_string());
    if (remove_result) {
        BROOKESIA_LOGI(
            "Removed App Store cache file: path(%1%), reason(%2%)",
            path.generic_string(), reason
        );
    } else {
        BROOKESIA_LOGW(
            "Failed to remove App Store cache file: path(%1%), reason(%2%), error(%3%)",
            path.generic_string(), reason, remove_result.error()
        );
    }
}

HttpHelper::HttpRequest make_get_request(std::string url)
{
    HttpHelper::HttpRequest request;
    request.url = std::move(url);
    request.method = HttpHelper::HttpMethod::Get;
    request.timeout_ms = HTTP_TIMEOUT_MS;
    request.retry_count = 1;
    request.retry_on_status_codes = {408, 429, 500, 502, 503, 504};
    return request;
}

bool is_http_url(std::string_view url)
{
    return url.starts_with("http://") || url.starts_with("https://");
}

std::string strip_query_and_fragment(std::string_view url)
{
    auto end = url.find_first_of("?#");
    if (end == std::string_view::npos) {
        return std::string(url);
    }
    return std::string(url.substr(0, end));
}

std::string trim_trailing_slash(std::string value)
{
    while (value.size() > std::string("https://").size() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string url_directory(std::string_view url)
{
    const auto clean_url = strip_query_and_fragment(url);
    const auto scheme_end = clean_url.find("://");
    if (scheme_end == std::string::npos) {
        return {};
    }
    const auto authority_start = scheme_end + 3;
    const auto path_start = clean_url.find('/', authority_start);
    if (path_start == std::string::npos) {
        return clean_url;
    }
    const auto last_slash = clean_url.rfind('/');
    if (last_slash == std::string::npos || last_slash <= scheme_end + 2) {
        return clean_url.substr(0, path_start);
    }
    return clean_url.substr(0, last_slash);
}

std::string append_url_path(std::string_view base, std::string_view path)
{
    if (base.empty()) {
        return {};
    }
    auto result = trim_trailing_slash(std::string(base));
    if (!path.empty() && path.front() != '/') {
        result += '/';
    }
    result += path;
    return result;
}

std::string configured_index_url()
{
    const std::string legacy_index_url = BROOKESIA_APP_STORE_INDEX_URL;
    if (!legacy_index_url.empty()) {
        return legacy_index_url;
    }
    return append_url_path(BROOKESIA_APP_STORE_SERVER_ROOT_URL, "index.json");
}

std::string server_root_for_index(std::string_view index_url)
{
    const std::string legacy_index_url = BROOKESIA_APP_STORE_INDEX_URL;
    if (!legacy_index_url.empty()) {
        return url_directory(index_url);
    }
    return trim_trailing_slash(BROOKESIA_APP_STORE_SERVER_ROOT_URL);
}

std::string resolve_relative_url(std::string_view base_url, std::string_view value)
{
    if (value.empty() || is_http_url(value)) {
        return std::string(value);
    }
    if (!is_http_url(base_url)) {
        return {};
    }

    const auto clean_base_url = strip_query_and_fragment(base_url);
    const auto scheme_end = clean_base_url.find("://");
    if (scheme_end == std::string::npos) {
        return {};
    }
    const auto authority_start = scheme_end + 3;
    const auto path_start = clean_base_url.find('/', authority_start);
    const auto origin = path_start == std::string::npos ?
                        clean_base_url :
                        clean_base_url.substr(0, path_start);
    if (value.front() == '/') {
        return origin + std::string(value);
    }

    std::string directory = origin + "/";
    if (path_start != std::string::npos) {
        const auto last_slash = clean_base_url.rfind('/');
        if (last_slash != std::string::npos && last_slash >= path_start) {
            directory = clean_base_url.substr(0, last_slash + 1);
        }
    }
    return directory + std::string(value);
}

std::string make_metadata_url(std::string_view index_url, std::string_view package_name, std::string_view version)
{
    const auto root = server_root_for_index(index_url);
    return append_url_path(
               root,
               "apps/" + std::string(package_name) + "/versions/" + std::string(version) + "/metadata.json"
           );
}

} // namespace detail

AppStoreApp::AppStoreApp()
    : impl_(std::make_unique<Impl>())
{
}

AppStoreApp::~AppStoreApp() = default;

} // namespace esp_brookesia::app::app_store
