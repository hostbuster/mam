#include <cstdio>
#include <string>
#include "../src/core/ParamMap.hpp"

static void emitTable(const ParamMap& map) {
  std::printf("### %s\n\n", map.nodeType);
  std::printf("| id | name | unit | min | max | default | smoothing |\n");
  std::printf("|---:|------|------|----:|----:|--------:|-----------|\n");
  for (size_t i = 0; i < map.count; ++i) {
    const auto& d = map.defs[i];
    std::printf("| %u | %s | %s | %.2f | %.2f | %.2f | %s |\n",
                d.id, d.name, d.unit, d.minValue, d.maxValue, d.defaultValue, d.smoothing);
  }
  std::printf("\n");
}

int main() {
  std::printf("# Parameter Tables\n\n");
  std::printf("Auto-generated from ParamMap.hpp. Do not edit by hand.\n\n");
  emitTable(kKickParamMap);
  emitTable(kClapParamMap);
  return 0;
}


