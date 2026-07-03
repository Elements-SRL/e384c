/*! \file e384c.h
 *  \brief Pure-C API over e384CommLib. This is the ONLY header consumed by
 *         bindgen / C callers. It must never include C++ headers.
 */
#ifndef E384C_H
#define E384C_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#  if defined(E384C_BUILD)
#    define E384C_API __declspec(dllexport)
#  else
#    define E384C_API __declspec(dllimport)
#  endif
#else
#  define E384C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*==============================*
 *  Error codes                 *
 *==============================*/

/*! Raw ErrorCodes_t value from e384commlib, passed through unmodified.
 *  0 = success. 0x8000000B = WarningValueClipped (success with warning,
 *  NOT a failure). See e384commlib_errorcodes.h for the full list. */
typedef uint32_t E384Err;

#define E384_SUCCESS               ((E384Err)0u)
#define E384_WARNING_VALUE_CLIPPED ((E384Err)0x8000000Bu)

/*! Group mask helpers: (code & E384_ERR_GROUP_MASK) yields the group. */
#define E384_ERR_GROUP_MASK        ((E384Err)0x7FFF0000u)

/*==============================*
 *  Boundary value types (POD)  *
 *==============================*/

/*! Mirrors e384CommLib::UnitPfx_t (values are identical). */
typedef enum E384UnitPfx {
    E384_PFX_FEMTO = 0,
    E384_PFX_PICO  = 1,
    E384_PFX_NANO  = 2,
    E384_PFX_MICRO = 3,
    E384_PFX_MILLI = 4,
    E384_PFX_NONE  = 5,
    E384_PFX_KILO  = 6,
    E384_PFX_MEGA  = 7,
    E384_PFX_GIGA  = 8,
    E384_PFX_TERA  = 9,
    E384_PFX_PETA  = 10
} E384UnitPfx;

/*! Enumerated units. The C++ library stores units as free strings, but the
 *  only values it actually uses are the ones below. E384_UNIT_UNKNOWN is an
 *  output-only sentinel: the shim returns it if the library ever hands back
 *  a string not in this set (should not happen; assert/log if it does).
 *  Never pass E384_UNIT_UNKNOWN as an input. */
typedef enum E384Unit {
    E384_UNIT_UNKNOWN = -1,
    E384_UNIT_NONE    = 0,  /* ""   dimensionless */
    E384_UNIT_V       = 1,  /* "V"  volt          */
    E384_UNIT_A       = 2,  /* "A"  ampere        */
    E384_UNIT_HZ      = 3,  /* "Hz" hertz         */
    E384_UNIT_S       = 4,  /* "s"  second        */
    E384_UNIT_C       = 5   /* "C"  celsius       */
} E384Unit;

/*! POD mirror of e384CommLib::Measurement_t. */
typedef struct E384Measurement {
    double  value;
    int32_t prefix; /* E384UnitPfx */
    int32_t unit;   /* E384Unit    */
} E384Measurement;

/*! POD mirror of e384CommLib::RangedMeasurement_t. */
typedef struct E384RangedMeasurement {
    double  min;
    double  max;
    double  step;
    int32_t prefix; /* E384UnitPfx */
    int32_t unit;   /* E384Unit    */
} E384RangedMeasurement;

/*==============================*
 *  Device handle               *
 *==============================*/

/*! Opaque handle to a connected device (wraps MessageDispatcher*). */
typedef struct E384Device E384Device;

/*==============================*
 *  Connection lifecycle        *
 *==============================*/

/*! Connect to a device by serial number (NUL-terminated string).
 *  On success, *outDevice holds the handle; it MUST be released with
 *  e384_disconnect(). On failure, *outDevice is set to NULL. */
E384C_API E384Err e384_connect(const char* deviceId, E384Device** outDevice);

/*! Disconnect and destroy the handle. The handle is invalid afterwards,
 *  regardless of the returned code. NULL is tolerated (returns success).
 *  overheatFlag != 0 enables overheating countermeasures if available. */
E384C_API E384Err e384_disconnect(E384Device* device, int32_t overheatFlag);

/*==============================*
 *  Proof-of-shape methods      *
 *==============================*/

/*! Shape A — "channel command": vector-in of channels + vector-in of
 *  measurements + apply flag.
 *  channelIndexes and voltages are parallel arrays of length count. */
E384C_API E384Err e384_setVoltageHoldTuner(E384Device* device,
                                           const uint16_t* channelIndexes,
                                           const E384Measurement* voltages,
                                           size_t count,
                                           int32_t applyFlag);

/*! Shape E — single out-param getter. */
E384C_API E384Err e384_getVCCurrentRange(E384Device* device,
                                         E384RangedMeasurement* outRange);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* E384C_H */
