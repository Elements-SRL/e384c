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

/*! Group mask helper: (code & E384_ERR_GROUP_MASK) yields the group. */
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
 *  a string not in this set. Never pass E384_UNIT_UNKNOWN as an input. */
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

/*! POD mirror of e384CommLib::RxOutput_t (field-for-field identical).
 *  Filled by e384_getNextMessage; dataLen tells how many int16_t samples
 *  landed in the caller-provided data buffer. */
typedef struct E384RxOutput {
    uint16_t msgTypeId;
    uint16_t channelIdx;
    uint16_t protocolId;
    uint16_t protocolItemIdx;
    uint16_t protocolRepsIdx;
    uint16_t protocolSweepIdx;
    uint32_t totalMessages;
    uint32_t firstSampleOffset;
    uint32_t dataLen;
} E384RxOutput;

/*==============================*
 *  Opaque handles              *
 *==============================*/

/*! Opaque handle to a connected device (wraps MessageDispatcher*). */
typedef struct E384Device E384Device;

/*! Opaque list of device-ID strings returned by discovery functions.
 *  The list owns its strings; free everything at once with
 *  e384_deviceList_free. */
typedef struct E384DeviceList E384DeviceList;

/*==============================*
 *  Device discovery            *
 *==============================*/

/*! Detect connectable devices (recognizable and not already owned).
 *  On success *outList holds a list handle (possibly with count 0) that
 *  MUST be released with e384_deviceList_free. On failure *outList is NULL.
 *  Note: the library returns ErrorNoDeviceFound (0x00010001) when nothing
 *  is plugged in; that still produces a valid empty list here. */
E384C_API E384Err e384_detectDevices(E384DeviceList** outList);

/*! List all plugged-in devices (including owned ones). Same contract. */
E384C_API E384Err e384_listAllDevices(E384DeviceList** outList);

/*! Number of device IDs in the list. NULL list yields 0. */
E384C_API size_t e384_deviceList_count(const E384DeviceList* list);

/*! Borrow the NUL-terminated device ID at index (NULL if out of range).
 *  The pointer is owned by the list and valid until e384_deviceList_free. */
E384C_API const char* e384_deviceList_get(const E384DeviceList* list, size_t index);

/*! Destroy the list and every string it owns. NULL is tolerated. */
E384C_API void e384_deviceList_free(E384DeviceList* list);

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

/*=========================================*
 *  Data acquisition (hot path)            *
 *                                         *
 *  Buffer protocol: the library allocates *
 *  the fixed-size RX buffer (so it is     *
 *  freed by the same CRT that allocated   *
 *  it). The caller reuses it across every *
 *  e384_getNextMessage call and releases  *
 *  it with e384_deallocateRxDataBuffer.   *
 *=========================================*/

/*! Size (in int16_t samples) of the RX data buffer. */
E384C_API E384Err e384_getRxDataBufferSize(E384Device* device, uint32_t* outSize);

/*! Allocate the RX data buffer (library-side). On success *outData points
 *  to a buffer of e384_getRxDataBufferSize samples. */
E384C_API E384Err e384_allocateRxDataBuffer(E384Device* device, int16_t** outData);

/*! Release the RX data buffer. *data is set to NULL. NULL-tolerant. */
E384C_API E384Err e384_deallocateRxDataBuffer(E384Device* device, int16_t** data);

/*! Pop the next message from the device. rxOut is always written on
 *  success; up to rxOut->dataLen samples are written into data.
 *  msgType filters for one message type; pass -1 for "any"
 *  (maps to MsgTypeIdInvalid, the library's own default). */
E384C_API E384Err e384_getNextMessage(E384Device* device,
                                      E384RxOutput* rxOut,
                                      int16_t* data,
                                      int32_t msgType);

/*! Discard all buffered RX data. */
E384C_API E384Err e384_purgeData(E384Device* device);

/*==================================================================*
 *  Shape A — channel commands:                                     *
 *  parallel arrays channelIndexes[count], values[count] + apply.   *
 *==================================================================*/

#define E384C_DECL_CHANNEL_MEAS_CMD(cname)                        \
    E384C_API E384Err cname(E384Device* device,                   \
                            const uint16_t* channelIndexes,       \
                            const E384Measurement* values,        \
                            size_t count,                         \
                            int32_t applyFlag);

E384C_DECL_CHANNEL_MEAS_CMD(e384_setVoltageHoldTuner)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCurrentHoldTuner)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setVoltageHalf)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCurrentHalf)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setLiquidJunctionVoltage)
/*! For the two below, the index array is BOARD indexes, not channels. */
E384C_DECL_CHANNEL_MEAS_CMD(e384_setGateVoltages)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setSourceVoltages)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibVcCurrentGain)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibVcCurrentOffset)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibVcVoltageGain)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibVcVoltageOffset)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibCcCurrentGain)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibCcCurrentOffset)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibCcVoltageGain)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibCcVoltageOffset)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibRsCorrOffsetDac)
E384C_DECL_CHANNEL_MEAS_CMD(e384_setCalibRShuntConductance)

/*==================================================================*
 *  Shape B — channel updates: channelIndexes[count] + apply.       *
 *==================================================================*/

#define E384C_DECL_CHANNEL_UPDATE(cname)                          \
    E384C_API E384Err cname(E384Device* device,                   \
                            const uint16_t* channelIndexes,       \
                            size_t count,                         \
                            int32_t applyFlag);

E384C_DECL_CHANNEL_UPDATE(e384_updateCalibVcCurrentGain)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibVcCurrentOffset)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibVcVoltageGain)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibVcVoltageOffset)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibCcCurrentGain)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibCcCurrentOffset)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibCcVoltageGain)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibCcVoltageOffset)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibRsCorrOffsetDac)
E384C_DECL_CHANNEL_UPDATE(e384_updateCalibRShuntConductance)
E384C_DECL_CHANNEL_UPDATE(e384_resetOffsetRecalibration)
E384C_DECL_CHANNEL_UPDATE(e384_resetLiquidJunctionVoltage)

/*==================================================================*
 *  Shape C — channel on/off commands:                              *
 *  parallel arrays channelIndexes[count], onValues[count] (0/1).   *
 *==================================================================*/

#define E384C_DECL_CHANNEL_BOOL_CMD(cname)                        \
    E384C_API E384Err cname(E384Device* device,                   \
                            const uint16_t* channelIndexes,       \
                            const uint8_t* onValues,              \
                            size_t count,                         \
                            int32_t applyFlag);

E384C_DECL_CHANNEL_BOOL_CMD(e384_enableStimulus)
E384C_DECL_CHANNEL_BOOL_CMD(e384_turnChannelsOn)
E384C_DECL_CHANNEL_BOOL_CMD(e384_turnCalSwOn)
E384C_DECL_CHANNEL_BOOL_CMD(e384_turnVcSwOn)
E384C_DECL_CHANNEL_BOOL_CMD(e384_turnCcSwOn)
E384C_DECL_CHANNEL_BOOL_CMD(e384_enableCcStimulus)
E384C_DECL_CHANNEL_BOOL_CMD(e384_readoutOffsetRecalibration)
E384C_DECL_CHANNEL_BOOL_CMD(e384_liquidJunctionCompensation)
E384C_DECL_CHANNEL_BOOL_CMD(e384_digitalOffsetCompensation)

/*==================================================================*
 *  Shape E — feature getters.                                      *
 *==================================================================*/

/* Single out-param variant. */
E384C_API E384Err e384_getVCCurrentRange(E384Device* device,
                                         E384RangedMeasurement* outRange);

/*! List variant — two-call size/fill protocol:
 *  - out == NULL : sizing call. *count receives the number of available
 *    elements; nothing is copied. outDefaultIdx is written if non-NULL.
 *  - out != NULL : fill call. On entry *count is the capacity of out;
 *    min(capacity, available) elements are copied and *count receives
 *    the available element count (which may exceed what was copied —
 *    treat *count > capacity as "buffer was too small, resize + retry").
 *  count must never be NULL. */
#define E384C_DECL_GET_RANGED_LIST(cname)                         \
    E384C_API E384Err cname(E384Device* device,                   \
                            E384RangedMeasurement* out,           \
                            size_t* count,                        \
                            uint16_t* outDefaultIdx);

E384C_DECL_GET_RANGED_LIST(e384_getVCCurrentRanges)
E384C_DECL_GET_RANGED_LIST(e384_getVCVoltageRanges)
E384C_DECL_GET_RANGED_LIST(e384_getCCCurrentRanges)
E384C_DECL_GET_RANGED_LIST(e384_getCCVoltageRanges)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* E384C_H */
