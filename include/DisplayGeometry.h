#pragma once
#include <Arduino.h>

// Shared physical/layout constants any Page implementation can use. Kept
// separate from DisplayManager.h so pages don't need to depend on the
// manager itself just to know the screen dimensions.
namespace DisplayGeometry {
constexpr uint8_t kScreenWidth = 128;
constexpr uint8_t kScreenHeight = 64;

// Header/chrome, drawn by DisplayManager before handing off to the active
// page -- pages should start their own content below kContentTopY so they
// don't overlap the title/IP bar and rule line.
constexpr uint8_t kHeaderY = 8;
constexpr uint8_t kHeaderRuleY = 11;
constexpr uint8_t kContentTopY = 23;

constexpr uint8_t kRightMargin = 2;
}  // namespace DisplayGeometry
