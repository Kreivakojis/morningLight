#ifndef ANIMATION_PRESET_H
#define ANIMATION_PRESET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANIMATION_MAX_PRESETS 5

/**
 * @brief Animation preset configuration
 *
 * Defines parameters for a parametric sine-wave animation.
 * Total size: 24 bytes per preset
 */
typedef struct {
    char name[12];           /**< User-friendly name (null-terminated) */
    float wavelength;        /**< 2.0 - 300.0 LEDs (distance between peaks) */
    uint8_t amplitude;       /**< 0-100% (contrast between peaks/valleys) */
    float speed;             /**< 0.0 - 5.0 cycles/sec */
    uint8_t base_brightness; /**< 0-100% (midpoint brightness) */
    uint8_t variation;       /**< 0-100% (organic noise amount) */
    uint16_t color_temp;     /**< 2000-6500K */
} animation_preset_t;

#ifdef __cplusplus
}
#endif

#endif // ANIMATION_PRESET_H
