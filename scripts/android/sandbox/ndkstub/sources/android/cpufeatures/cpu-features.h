/* Minimal stand-in for the NDK's cpu-features (arm64 sandbox build). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ANDROID_CPU_FAMILY_UNKNOWN = 0, ANDROID_CPU_FAMILY_ARM, ANDROID_CPU_FAMILY_X86,
  ANDROID_CPU_FAMILY_MIPS, ANDROID_CPU_FAMILY_ARM64, ANDROID_CPU_FAMILY_X86_64,
  ANDROID_CPU_FAMILY_MIPS64, ANDROID_CPU_FAMILY_MAX } AndroidCpuFamily;
enum { ANDROID_CPU_ARM_FEATURE_ARMv7 = (1 << 0), ANDROID_CPU_ARM_FEATURE_VFPv3 = (1 << 1),
  ANDROID_CPU_ARM_FEATURE_NEON = (1 << 2) };
enum { ANDROID_CPU_ARM64_FEATURE_FP = (1 << 0), ANDROID_CPU_ARM64_FEATURE_ASIMD = (1 << 1) };
AndroidCpuFamily android_getCpuFamily(void);
uint64_t android_getCpuFeatures(void);
int android_getCpuCount(void);
#ifdef __cplusplus
}
#endif
