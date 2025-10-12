#pragma once

#include <string>

// Returns 0 on success, 2 on validation errors, 1 on fatal errors (I/O, parser).
// If a JSON Schema validator is available (e.g., pboettch/json-schema-validator),
// define USE_JSON_SCHEMA in the build and ensure its header is on include path.
int validateJsonWithDraft2020(const std::string& jsonPath,
                              const std::string& schemaPath,
                              std::string& outDiagnostics);


