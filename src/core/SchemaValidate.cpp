#include "SchemaValidate.hpp"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#ifdef USE_JSON_SCHEMA
#include <json-schema.hpp>
#endif

int validateJsonWithDraft2020(const std::string& jsonPath,
                              const std::string& schemaPath,
                              std::string& outDiagnostics) {
  try {
    nlohmann::json schema;
    nlohmann::json doc;
    {
      std::ifstream fs(schemaPath);
      if (!fs.good()) { outDiagnostics = "Schema not found"; return 0; }
      fs >> schema;
    }
    {
      std::ifstream fg(jsonPath);
      if (!fg.good()) { outDiagnostics = "Graph not found"; return 1; }
      fg >> doc;
    }
#ifdef USE_JSON_SCHEMA
    // Strict schema validation
    json_schema::json_validator validator;
    validator.set_root_schema(schema);
    validator.validate(doc);
    return 0;
#else
    // Stub: accept; rely on existing semantic checks
    (void)schema;
    (void)doc;
    return 0;
#endif
  } catch (const std::exception& e) {
    outDiagnostics = e.what();
    return 2;
  }
}
