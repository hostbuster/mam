#pragma once

#include <nlohmann/json.hpp>

// Minimal compatibility shim for json-schema-validator API.
// Replace with https://github.com/pboettch/json-schema-validator for real enforcement.
namespace json_schema {
class json_validator {
public:
  void set_root_schema(const nlohmann::json&) {}
  void validate(const nlohmann::json&) {}
};
}


