#include <math.h>
#include <stdint.h>
#include "animation_preset.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/**
 * @brief Simple interpolated value noise (deterministic, smooth)
 *
 * Uses a hash-based approach for pseudo-random values with
 * cubic Hermite interpolation for smoothness.
 *
 * @param x Position to sample
 * @param seed Seed for variation
 * @return Noise value in range [0, 1]
 */
static float value_noise(float x, float seed)
{
    int xi = (int)floorf(x);
    float xf = x - xi;

    // Smooth interpolation (cubic Hermite)
    float t = xf * xf * (3.0f - 2.0f * xf);

    // Simple hash function for deterministic pseudo-random
    float h1 = sinf(xi * 12.9898f + seed * 78.233f) * 43758.5453f;
    float h2 = sinf((xi + 1) * 12.9898f + seed * 78.233f) * 43758.5453f;
    h1 = h1 - floorf(h1);
    h2 = h2 - floorf(h2);

    return h1 + (h2 - h1) * t;
}

/**
 * @brief Compute per-LED brightness values for the wave animation
 *
 * Core formula: brightness(x, t) = base + amplitude * sin(spatial + temporal) * variation
 *
 * @param brightness_out Output array of brightness values (0-100)
 * @param led_count Number of LEDs to compute
 * @param preset Animation preset with parameters
 * @param time_sec Current time in seconds (for temporal animation)
 */
void wave_generator_compute(
    uint8_t *brightness_out,
    uint16_t led_count,
    const animation_preset_t *preset,
    float time_sec)
{
    if (!brightness_out || !preset || led_count == 0) {
        return;
    }

    // Pre-calculate constants
    float spatial_freq = (2.0f * M_PI) / preset->wavelength;
    float temporal_phase = time_sec * preset->speed * 2.0f * M_PI;
    float amplitude = preset->amplitude / 100.0f;
    float base = preset->base_brightness / 100.0f;

    // Variation scales from 0-100% to 0-20% actual variation
    float variation_strength = preset->variation / 100.0f * 0.2f;

    for (uint16_t i = 0; i < led_count; i++) {
        // Spatial phase based on LED position
        float spatial_phase = i * spatial_freq;

        // Core sine wave
        float wave = sinf(spatial_phase + temporal_phase);

        // Organic variation using slow-moving noise
        float noise_x = i * 0.1f + time_sec * 0.3f;
        float variation = 1.0f + (value_noise(noise_x, 42.0f) - 0.5f) * 2.0f * variation_strength;

        // Combine: base + amplitude * wave * variation
        // Amplitude is halved because sine outputs [-1, 1] and we want symmetry around base
        float brightness = base + amplitude * 0.5f * wave * variation;

        // Clamp to valid range [0, 1]
        if (brightness < 0.0f) brightness = 0.0f;
        if (brightness > 1.0f) brightness = 1.0f;

        // Convert to percentage
        brightness_out[i] = (uint8_t)(brightness * 100.0f);
    }
}

/**
 * @brief Compute per-LED brightness values with base brightness override
 *
 * Same as wave_generator_compute() but allows overriding the base brightness.
 * Used for animated sunrise where base ramps from 0 to target over the duration.
 *
 * @param brightness_out Output array of brightness values (0-100)
 * @param led_count Number of LEDs to compute
 * @param preset Animation preset with parameters
 * @param time_sec Current time in seconds (for temporal animation)
 * @param base_override Base brightness override (0-100), or -1 to use preset value
 */
void wave_generator_compute_with_base(
    uint8_t *brightness_out,
    uint16_t led_count,
    const animation_preset_t *preset,
    float time_sec,
    int16_t base_override)
{
    if (!brightness_out || !preset || led_count == 0) {
        return;
    }

    // Pre-calculate constants
    float spatial_freq = (2.0f * M_PI) / preset->wavelength;
    float temporal_phase = time_sec * preset->speed * 2.0f * M_PI;
    float amplitude = preset->amplitude / 100.0f;

    // Use override if provided, otherwise use preset value
    float base = (base_override >= 0)
        ? (base_override / 100.0f)
        : (preset->base_brightness / 100.0f);

    // Variation scales from 0-100% to 0-20% actual variation
    float variation_strength = preset->variation / 100.0f * 0.2f;

    for (uint16_t i = 0; i < led_count; i++) {
        // Spatial phase based on LED position
        float spatial_phase = i * spatial_freq;

        // Core sine wave
        float wave = sinf(spatial_phase + temporal_phase);

        // Organic variation using slow-moving noise
        float noise_x = i * 0.1f + time_sec * 0.3f;
        float variation = 1.0f + (value_noise(noise_x, 42.0f) - 0.5f) * 2.0f * variation_strength;

        // Combine: base + amplitude * wave * variation
        // Amplitude is halved because sine outputs [-1, 1] and we want symmetry around base
        float brightness = base + amplitude * 0.5f * wave * variation;

        // Clamp to valid range [0, 1]
        if (brightness < 0.0f) brightness = 0.0f;
        if (brightness > 1.0f) brightness = 1.0f;

        // Convert to percentage
        brightness_out[i] = (uint8_t)(brightness * 100.0f);
    }
}
