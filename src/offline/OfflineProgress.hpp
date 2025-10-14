#pragma once

#include <cstdint>

// Global inline controls for offline progress reporting
inline int gOfflineProgressMs = 100;      // print progress roughly every N ms (<=0 disables)
inline bool gOfflineProgressEnabled = true; // master switch
inline bool gOfflineSummaryEnabled = true;  // print final speedup summary line


