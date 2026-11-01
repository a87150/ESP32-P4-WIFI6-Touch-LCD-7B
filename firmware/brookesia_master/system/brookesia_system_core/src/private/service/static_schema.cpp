/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/service/static_schema.hpp"

#include <string>
#include <utility>

namespace esp_brookesia::system::core::static_schema {

std::vector<service::FunctionSchema> materialize_function_schemas(std::span<const FunctionSpec> specs)
{
    std::vector<service::FunctionSchema> schemas;
    schemas.reserve(specs.size());

    for (const auto &spec : specs) {
        service::FunctionSchema schema{
            .name = spec.name,
            .description = spec.description,
            .require_scheduler = spec.require_scheduler,
        };
        schema.parameters.reserve(spec.parameters.size());
        for (const auto &parameter_spec : spec.parameters) {
            service::FunctionParameterSchema parameter{
                .name = parameter_spec.name,
                .description = parameter_spec.description,
                .type = parameter_spec.type,
            };
            switch (parameter_spec.default_value.kind) {
            case DefaultValueKind::None:
                break;
            case DefaultValueKind::Boolean:
                parameter.default_value.emplace(parameter_spec.default_value.boolean_value);
                break;
            case DefaultValueKind::Number:
                parameter.default_value.emplace(parameter_spec.default_value.number_value);
                break;
            case DefaultValueKind::String:
                parameter.default_value.emplace(std::string(parameter_spec.default_value.string_value));
                break;
            }
            schema.parameters.emplace_back(std::move(parameter));
        }
        if (spec.return_value != nullptr) {
            schema.return_value.emplace(service::FunctionReturnSchema{
                .type = spec.return_value->type,
                .description = spec.return_value->description,
            });
        }
        schemas.emplace_back(std::move(schema));
    }

    return schemas;
}

} // namespace esp_brookesia::system::core::static_schema
