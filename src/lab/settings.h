#pragma once
#if __has_include("lab_config.h")
#include "lab_config.h"
#else
#include "lab_config.example.h"
#endif

// Defaults also apply to existing local network-only lab_config.h files.
#ifndef LAB_LASER_TIMEOUT_MS
#define LAB_LASER_TIMEOUT_MS 3200UL
#endif
#ifndef LAB_IMU_TIMEOUT_MS
#define LAB_IMU_TIMEOUT_MS 1000UL
#endif
#ifndef IMU_LASER_AXIS_BX
#define IMU_LASER_AXIS_BX 1.0f
#endif
#ifndef IMU_LASER_AXIS_BY
#define IMU_LASER_AXIS_BY 0.0f
#endif
#ifndef IMU_LASER_AXIS_BZ
#define IMU_LASER_AXIS_BZ 0.0f
#endif
static_assert(LAB_LASER_TIMEOUT_MS >= 100 && LAB_LASER_TIMEOUT_MS <= 3200, "Laser timeout: 100..3200 ms");
static_assert(LAB_IMU_TIMEOUT_MS >= 100 && LAB_IMU_TIMEOUT_MS <= 1000, "IMU timeout: 100..1000 ms");
