// Small always-on-top overlay indicator
// Displays in bottom-right corner with subtle pulsing effect
#pragma once

// Color options for the indicator
enum class IndicatorColor {
    Green, // Success/positive action (default)
    Red    // Error/no response
};

// Show a small always-on-top indicator in the bottom-right corner for
// `duration_ms` milliseconds. This function returns immediately; the overlay
// runs on a detached thread.
void ShowOverlayIndicator(int duration_ms = 1500, IndicatorColor color = IndicatorColor::Green,
                          const char *text = nullptr);
