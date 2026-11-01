/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

#include "boost/json.hpp"

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_helper/system/device.hpp"
#include "brookesia/service_helper/system/storage.hpp"

#include "app_store_details.hpp"
#include "app_store_impl.hpp"
#include "app_store_impl_access.hpp"
#include "utils.hpp"
