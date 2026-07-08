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

/*! POD mirror of e384CommLib::PidParams_t (already all-double POD in the
 *  C++ library; mirrored only so e384c.h never includes the C++ header). */
typedef struct E384PidParams {
    double proportionalGain;
    double integralGain;
    double derivativeGain;
    double integralAntiWindUp;
} E384PidParams;

/*! POD mirror of e384CommLib::ChannelSources_t (field-for-field identical;
 *  -1 means "source not available"). */
typedef struct E384ChannelSources {
    int16_t voltageFromVoltageClamp;
    int16_t currentFromVoltageClamp;
    int16_t voltageFromCurrentClamp;
    int16_t currentFromCurrentClamp;
    int16_t voltageFromDynamicClamp;
    int16_t currentFromDynamicClamp;
    int16_t voltageFromVoltagePlusDynamicClamp;
    int16_t currentFromCurrentPlusDynamicClamp;
} E384ChannelSources;

/*! Mirror of e384CommLib::CompensationControl_t, minus the `name` field:
 *  the caller already knows which CompensationUserParams_t it asked for,
 *  so the human-readable name is redundant across this boundary (the same
 *  simplification the library's own LabVIEW wrapper does NOT need to make,
 *  but we do, exactly like Measurement_t's unit was turned into an enum). */
typedef struct E384CompensationControl {
    int32_t implemented;      /* bool as 0/1 */
    double  min;
    double  max;
    double  minCompensable;
    double  maxCompensable;
    double  steps;
    double  step;
    int32_t decimals;
    double  value;
    int32_t prefix; /* E384UnitPfx */
    int32_t unit;   /* E384Unit */
} E384CompensationControl;

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

/*! Opaque handles wrapping ChannelModel* / BoardModel* (reinterpret_cast,
 *  same no-allocation pattern as E384Device/MessageDispatcher*). These are
 *  BORROWED: they point into the owning E384Device's internal model lists
 *  (populated at connect time) and stay valid only for that device's
 *  lifetime. There is no _free function -- do not call one, and do not
 *  use a handle after e384_disconnect. */
typedef struct E384ChannelModel E384ChannelModel;
typedef struct E384BoardModel E384BoardModel;

/*! Opaque owned string, returned by the single std::string-returning
 *  getters (device name/serial, calibration file paths). Read it with
 *  e384_string_get and release it with e384_string_free; copy out to an
 *  owned buffer before freeing, same contract as E384DeviceList. */
typedef struct E384String E384String;

/*! Borrow the NUL-terminated contents. NULL if str is NULL.
 *  The pointer is owned by str and valid until e384_string_free. */
E384C_API const char* e384_string_get(const E384String* str);

/*! Destroy the string. NULL is tolerated. */
E384C_API void e384_string_free(E384String* str);

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

/*==================================================================*
 *  Shape D — scalar setters: uint16_t index + apply flag.          *
 *==================================================================*/

#define E384C_DECL_U16_APPLY(cname)                               \
    E384C_API E384Err cname(E384Device* device,                   \
                            uint16_t value,                       \
                            int32_t applyFlag);

E384C_DECL_U16_APPLY(e384_setVCCurrentRange_all)
E384C_DECL_U16_APPLY(e384_setVCVoltageRange)
E384C_DECL_U16_APPLY(e384_setCCCurrentRange)
E384C_DECL_U16_APPLY(e384_setCCVoltageRange_all)
E384C_DECL_U16_APPLY(e384_setVoltageStimulusLpf)
E384C_DECL_U16_APPLY(e384_setCurrentStimulusLpf)
E384C_DECL_U16_APPLY(e384_setSourceForVoltageChannel)
E384C_DECL_U16_APPLY(e384_setSourceForCurrentChannel)
E384C_DECL_U16_APPLY(e384_setSamplingRate)

/*==================================================================*
 *  Small scalar/boolean commands.                                  *
 *==================================================================*/

#define E384C_DECL_U32(cname)                                     \
    E384C_API E384Err cname(E384Device* device, uint32_t value);

E384C_DECL_U32(e384_setDownsamplingRatio)

#define E384C_DECL_BOOL2(cname)                                   \
    E384C_API E384Err cname(E384Device* device,                   \
                            int32_t value1,                       \
                            int32_t value2);

E384C_DECL_BOOL2(e384_resetAsic)
E384C_DECL_BOOL2(e384_resetFpga)
E384C_DECL_BOOL2(e384_turnVoltageReaderOn)
E384C_DECL_BOOL2(e384_turnCurrentReaderOn)
E384C_DECL_BOOL2(e384_turnVoltageStimulusOn)
E384C_DECL_BOOL2(e384_turnCurrentStimulusOn)
E384C_DECL_BOOL2(e384_enableVcCompensations)
E384C_DECL_BOOL2(e384_enableCcCompensations)

#define E384C_DECL_BOOL1(cname)                                   \
    E384C_API E384Err cname(E384Device* device, int32_t value);

E384C_DECL_BOOL1(e384_subtractLiquidJunctionFromCc)
E384C_DECL_BOOL1(e384_setCalibrationMode)

#define E384C_DECL_MEAS_BOOL(cname)                                \
    E384C_API E384Err cname(E384Device* device,                    \
                            E384Measurement value,                 \
                            int32_t flag);

E384C_DECL_MEAS_BOOL(e384_setVoltageReference)
E384C_DECL_MEAS_BOOL(e384_setCoolingFansSpeed)
E384C_DECL_MEAS_BOOL(e384_setTemperatureControl)

/*==================================================================*
 *  Small scalar/Measurement getters.                                *
 *==================================================================*/

#define E384C_DECL_GET_U32(cname)                                 \
    E384C_API E384Err cname(E384Device* device, uint32_t* out);

E384C_DECL_GET_U32(e384_getClampingModalityIdx)
E384C_DECL_GET_U32(e384_getVCVoltageRangeIdx)
E384C_DECL_GET_U32(e384_getCCCurrentRangeIdx)
E384C_DECL_GET_U32(e384_getSamplingRateIdx)
E384C_DECL_GET_U32(e384_getMaxDownsamplingRatioFeature)
E384C_DECL_GET_U32(e384_getDownsamplingRatio)
E384C_DECL_GET_U32(e384_getVCVoltageFilterIdx)
E384C_DECL_GET_U32(e384_getVCCurrentFilterIdx)
E384C_DECL_GET_U32(e384_getCCVoltageFilterIdx)
E384C_DECL_GET_U32(e384_getCCCurrentFilterIdx)
E384C_DECL_GET_U32(e384_getMaxProtocolItemsFeature)
E384C_DECL_GET_U32(e384_getCalibrationEepromSize)

/*! vector<uint32_t> out-param getter, two-call size/fill protocol, no input. */
#define E384C_DECL_GET_U32_LIST(cname)                             \
    E384C_API E384Err cname(E384Device* device, uint32_t* out, size_t* count);

#define E384C_DECL_GET_MEAS(cname)                                \
    E384C_API E384Err cname(E384Device* device, E384Measurement* out);

E384C_DECL_GET_MEAS(e384_getSamplingRate)
E384C_DECL_GET_MEAS(e384_getVCVoltageFilter)
E384C_DECL_GET_MEAS(e384_getVCCurrentFilter)
E384C_DECL_GET_MEAS(e384_getCCVoltageFilter)
E384C_DECL_GET_MEAS(e384_getCCCurrentFilter)

/*! List variant, two-call size/fill protocol identical to
 *  E384C_DECL_GET_RANGED_LIST but with no default-index out-param. */
#define E384C_DECL_GET_MEAS_LIST(cname)                            \
    E384C_API E384Err cname(E384Device* device,                    \
                            E384Measurement* out,                  \
                            size_t* count);

E384C_DECL_GET_MEAS_LIST(e384_getSamplingRatesFeatures)
E384C_DECL_GET_MEAS_LIST(e384_getRealSamplingRatesFeatures)
E384C_DECL_GET_MEAS_LIST(e384_getVCVoltageFilters)
E384C_DECL_GET_MEAS_LIST(e384_getVCCurrentFilters)
E384C_DECL_GET_MEAS_LIST(e384_getCCVoltageFilters)
E384C_DECL_GET_MEAS_LIST(e384_getCCCurrentFilters)
E384C_DECL_GET_MEAS_LIST(e384_getVoltageHoldTuner)

/*! Same two-call protocol as E384C_DECL_GET_RANGED_LIST, but without the
 *  default-index out-param (the underlying C++ getters have no default idx). */
#define E384C_DECL_GET_RANGED_LIST_NODEF(cname)                    \
    E384C_API E384Err cname(E384Device* device,                    \
                            E384RangedMeasurement* out,             \
                            size_t* count);

E384C_DECL_GET_RANGED_LIST_NODEF(e384_getVoltageHoldTunerFeatures)
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getVoltageHalfFeatures)
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getCurrentHoldTunerFeatures)
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getCurrentHalfFeatures)
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getLiquidJunctionRangesFeatures)

/*! RangedMeasurement_t + its index, both out-params. */
#define E384C_DECL_GET_RANGED_WITH_IDX(cname)                      \
    E384C_API E384Err cname(E384Device* device,                    \
                            E384RangedMeasurement* outRange,        \
                            uint32_t* outIdx);

E384C_DECL_GET_RANGED_WITH_IDX(e384_getMaxVCCurrentRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMinVCCurrentRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMaxVCVoltageRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMinVCVoltageRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMaxCCCurrentRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMinCCCurrentRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMaxCCVoltageRange)
E384C_DECL_GET_RANGED_WITH_IDX(e384_getMinCCVoltageRange)

/*==================================================================*
 *  Capability probes: has_/is_ style methods -> *outResult is 0/1.  *
 *  own E384Err return is E384_SUCCESS unless device/out is NULL or  *
 *  an exception was thrown -- it is NOT the underlying method's     *
 *  ErrorCodes_t (that value IS the 0/1 signal, folded into *outResult). *
 *==================================================================*/

#define E384C_DECL_PROBE(cname)                                    \
    E384C_API E384Err cname(E384Device* device, int32_t* outResult);

E384C_DECL_PROBE(e384_hasCalSw)
E384C_DECL_PROBE(e384_hasGateVoltages)
E384C_DECL_PROBE(e384_hasSourceVoltages)
E384C_DECL_PROBE(e384_isEpisodic)
E384C_DECL_PROBE(e384_hasProperHeaderPackets)
E384C_DECL_PROBE(e384_hasIndependentVCCurrentRanges)
E384C_DECL_PROBE(e384_hasIndependentCCVoltageRanges)
E384C_DECL_PROBE(e384_hasChannelSwitches)
E384C_DECL_PROBE(e384_hasStimulusSwitches)
E384C_DECL_PROBE(e384_hasOffsetCompensation)
E384C_DECL_PROBE(e384_hasStimulusHalf)
E384C_DECL_PROBE(e384_hasProtocols)
E384C_DECL_PROBE(e384_hasProtocolStepFeature)
E384C_DECL_PROBE(e384_hasProtocolRampFeature)
E384C_DECL_PROBE(e384_hasProtocolSinFeature)
E384C_DECL_PROBE(e384_isStateArrayAvailable)
E384C_DECL_PROBE(e384_getCalibrationStatus)

/*==================================================================*
 *  String-out getters. All follow the E384String opaque handle      *
 *  convention described above.                                     *
 *==================================================================*/

#define E384C_DECL_GET_STRING(cname)                                \
    E384C_API E384Err cname(E384Device* device, E384String** outStr);

E384C_DECL_GET_STRING(e384_getDeviceName)
E384C_DECL_GET_STRING(e384_getDeviceSerial)
E384C_DECL_GET_STRING(e384_getSerialNumber)
E384C_DECL_GET_STRING(e384_getCalibMappingFileDir)
E384C_DECL_GET_STRING(e384_getCalibMappingFilePath)

/*==================================================================*
 *  Misc hand-written one-offs (no reusable shape).                 *
 *==================================================================*/

/*! messageType is a MsgTypeId_t value (see e384commlib_global.h). */
E384C_API E384Err e384_enableRxMessageType(E384Device* device, int32_t messageType, int32_t flag);

/*! clampingModes[i] is a ClampingModality_t value, parallel to channelIndexes[i]. */
E384C_API E384Err e384_setAdcCore(E384Device* device,
                                  const uint16_t* channelIndexes,
                                  const int32_t* clampingModes,
                                  size_t count,
                                  int32_t applyFlag);

E384C_API E384Err e384_sendSpiCommand(E384Device* device, uint32_t command, uint32_t dataLoad);

E384C_API E384Err e384_setCustomFlag(E384Device* device, uint16_t idx, int32_t flag, int32_t applyFlag);
E384C_API E384Err e384_setCustomOption(E384Device* device, uint16_t idx, uint16_t value, int32_t applyFlag);
E384C_API E384Err e384_setCustomDouble(E384Device* device, uint16_t idx, double value, int32_t applyFlag);

E384C_API E384Err e384_setDebugBit(E384Device* device, uint16_t wordOffset, uint16_t bitOffset,
                                   int32_t status, int32_t applyFlag);
E384C_API E384Err e384_setDebugWord(E384Device* device, uint16_t wordOffset, uint16_t wordValue);

E384C_API E384Err e384_setStateArrayEnabled(E384Device* device, int32_t chIdx, int32_t enabledFlag);

E384C_API E384Err e384_setTemperatureControlPid(E384Device* device, E384PidParams params);

/*! duration applies to every channel in channelIndexes. */
E384C_API E384Err e384_zap(E384Device* device,
                           const uint16_t* channelIndexes,
                           size_t count,
                           E384Measurement duration);

E384C_API E384Err e384_setStateArrayStructure(E384Device* device,
                                              int32_t numberOfStates,
                                              int32_t initialState,
                                              E384Measurement reactionTime);

E384C_API E384Err e384_setSateArrayState(E384Device* device,
                                         int32_t stateIdx,
                                         E384Measurement voltage,
                                         int32_t timeoutStateFlag,
                                         E384Measurement timeout,
                                         int32_t timeoutState,
                                         E384Measurement minTriggerValue,
                                         E384Measurement maxTriggerValue,
                                         int32_t triggerState,
                                         int32_t triggerFlag,
                                         int32_t deltaFlag);

/*! feature is a CompensationUserParams_t value. Probe convention: see
 *  E384C_DECL_PROBE above -- *outResult is 0/1, return is E384_SUCCESS
 *  unless device/outResult is NULL or an exception was thrown. */
E384C_API E384Err e384_hasCompFeature(E384Device* device, int32_t feature, int32_t* outResult);

/*! outStatuses[i] is an OffsetRecalibStatus_t value for channelIndexes[i].
 *  outStatuses must have room for `count` elements (size == input count,
 *  no two-call protocol needed). */
E384C_API E384Err e384_getReadoutOffsetRecalibrationStatuses(E384Device* device,
                                                              const uint16_t* channelIndexes,
                                                              size_t count,
                                                              int32_t* outStatuses);

/*! outStatuses[i] is a LiquidJunctionStatus_t value for channelIndexes[i]. */
E384C_API E384Err e384_getLiquidJunctionStatuses(E384Device* device,
                                                 const uint16_t* channelIndexes,
                                                 size_t count,
                                                 int32_t* outStatuses);

/*! outVoltages[i] is the compensated voltage for channelIndexes[i]. */
E384C_API E384Err e384_getLiquidJunctionVoltages(E384Device* device,
                                                 const uint16_t* channelIndexes,
                                                 size_t count,
                                                 E384Measurement* outVoltages);

/*! ClampingModality_t values, two-call size/fill protocol (see
 *  E384C_DECL_GET_RANGED_LIST for the protocol description). */
E384C_API E384Err e384_getClampingModalitiesFeatures(E384Device* device,
                                                     int32_t* out,
                                                     size_t* count);

/*! Single ClampingModality_t out-param. */
E384C_API E384Err e384_getClampingModality(E384Device* device, int32_t* out);

/*! type is a CompensationTypes_t value. outOnValues[i] (0/1) is the enable
 *  state of the compensation for channelIndexes[i]; size == input count. */
E384C_API E384Err e384_getCompensationEnables(E384Device* device,
                                              const uint16_t* channelIndexes,
                                              size_t count,
                                              int32_t type,
                                              uint8_t* outOnValues);

/*==================================================================*
 *  Complex / multi-output getters and setters.                     *
 *                                                                  *
 *  E384DeviceList is reused here as a generic opaque owned string  *
 *  list (not just for device discovery) -- same _count/_get/_free  *
 *  contract, for every plain vector<std::string> getter below.     *
 *==================================================================*/

E384C_API E384Err e384_getAvailableChannelsSourcesFeatures(E384Device* device,
                                                            E384ChannelSources* outVoltageSources,
                                                            E384ChannelSources* outCurrentSources);

/*! Two-call protocol on outRanges/count (see E384C_DECL_GET_RANGED_LIST_NODEF).
 *  outNames is populated only on the fill call (outRanges != NULL); on the
 *  sizing call (outRanges == NULL) *outNames is set to NULL if non-NULL. */
E384C_API E384Err e384_getTemperatureChannelsFeatures(E384Device* device,
                                                       E384RangedMeasurement* outRanges,
                                                       size_t* count,
                                                       E384DeviceList** outNames);

/*! value/address/size are parallel arrays of length count, all inputs. */
E384C_API E384Err e384_writeCalibrationEeprom(E384Device* device,
                                              const uint32_t* value,
                                              const uint32_t* address,
                                              const uint32_t* size,
                                              size_t count);

/*! address/size are parallel input arrays of length count; outValue must
 *  have room for count elements. */
E384C_API E384Err e384_readCalibrationEeprom(E384Device* device,
                                             const uint32_t* address,
                                             const uint32_t* size,
                                             size_t count,
                                             uint32_t* outValue);

E384C_API E384Err e384_getCalibFileNames(E384Device* device, E384DeviceList** outList);

/*! Flattened row-major bool matrix (rows = clamping modalities, cols = max
 *  row length found; short rows are padded with 0). Two-call protocol:
 *  outFlags == NULL sizes (*outRows, *outCols filled, nothing copied);
 *  outFlags != NULL fills up to (*outRows)*(*outCols) entries -- caller
 *  must have sized outFlags from a prior sizing call. */
E384C_API E384Err e384_getCalibFilesFlags(E384Device* device,
                                          uint8_t* outFlags,
                                          size_t* outRows,
                                          size_t* outCols);

/*! feature is a CompensationUserParams_t value. Two-call protocol on
 *  outRanges/count; outDefaultValue is populated whenever non-NULL,
 *  on either call (it's a plain scalar, not part of the sizing dance). */
E384C_API E384Err e384_getCompFeatures(E384Device* device,
                                       int32_t feature,
                                       E384RangedMeasurement* outRanges,
                                       size_t* count,
                                       double* outDefaultValue);

/*! type is a CompensationTypes_t value. */
E384C_API E384Err e384_getCompOptionsFeatures(E384Device* device,
                                              int32_t type,
                                              E384DeviceList** outOptions);

/*! Flattened row-major double matrix (rows = channels, cols =
 *  CompensationUserParams-family count). Same two-call protocol as
 *  e384_getCalibFilesFlags. */
E384C_API E384Err e384_getCompValueMatrix(E384Device* device,
                                          double* outValues,
                                          size_t* outRows,
                                          size_t* outCols);

/*! param is a CompensationUserParams_t value. */
E384C_API E384Err e384_getCompensationControl(E384Device* device,
                                              int32_t param,
                                              E384CompensationControl* outControl);

/*! Two-call protocol on outDefaults/count; outNames populated only on the
 *  fill call (outDefaults != NULL). */
E384C_API E384Err e384_getCustomFlags(E384Device* device,
                                      uint8_t* outDefaults,
                                      size_t* count,
                                      E384DeviceList** outNames);

/*! Two-call protocol on outRanges+outDefaults/count (same count for both);
 *  outNames populated only on the fill call (outRanges != NULL). */
E384C_API E384Err e384_getCustomDoubles(E384Device* device,
                                        E384RangedMeasurement* outRanges,
                                        double* outDefaults,
                                        size_t* count,
                                        E384DeviceList** outNames);

/*! NOTE: getCustomOptions (vector<string> customOptions, vector<vector
 *  <string>> customOptionsDescriptions, vector<uint16_t> customOptionsDefault)
 *  is NOT wrapped. Its per-control description list is ragged (a variable
 *  number of option strings per control), which is a genuinely new nested
 *  opaque-tree problem like CalibrationParams_t -- deferred rather than
 *  forcing a flattened design. See PROJECT_NOTES.md open decisions. */

E384C_API E384Err e384_getVoltageProtocolRangeFeature(E384Device* device,
                                                       uint16_t rangeIdx,
                                                       E384RangedMeasurement* outRange);
E384C_API E384Err e384_getCurrentProtocolRangeFeature(E384Device* device,
                                                       uint16_t rangeIdx,
                                                       E384RangedMeasurement* outRange);

/*! Two-call protocol on outVoltageRanges/count; outDurationRange populated
 *  whenever non-NULL, on either call. */
E384C_API E384Err e384_getVoltageRampTunerFeatures(E384Device* device,
                                                    E384RangedMeasurement* outVoltageRanges,
                                                    size_t* count,
                                                    E384RangedMeasurement* outDurationRange);

/*! type is a CompensationTypes_t value. */
E384C_API E384Err e384_enableCompensation(E384Device* device,
                                          const uint16_t* channelIndexes,
                                          const uint8_t* onValues,
                                          size_t count,
                                          int32_t type,
                                          int32_t applyFlag);

/*! paramToUpdate is a CompensationUserParams_t value. */
E384C_API E384Err e384_setCompValues(E384Device* device,
                                     const uint16_t* channelIndexes,
                                     const double* newParamValues,
                                     size_t count,
                                     int32_t paramToUpdate,
                                     int32_t applyFlag);

/*! paramToUpdate is a CompensationUserParams_t value. */
E384C_API E384Err e384_setCompRanges(E384Device* device,
                                     const uint16_t* channelIndexes,
                                     const uint16_t* newRanges,
                                     size_t count,
                                     int32_t paramToUpdate,
                                     int32_t applyFlag);

/*! type is a CompensationTypes_t value. */
E384C_API E384Err e384_setCompOptions(E384Device* device,
                                      const uint16_t* channelIndexes,
                                      const uint16_t* options,
                                      size_t count,
                                      int32_t type,
                                      int32_t applyFlag);

/*==================================================================*
 *  Protocol builders (hand-written, no vector marshaling of        *
 *  Measurement_t needed -- each item is scalar-shaped except for   *
 *  the trailing digital-outputs index array).                      *
 *==================================================================*/

E384C_API E384Err e384_setVoltageProtocolStructure(E384Device* device,
                                                    uint16_t protId,
                                                    uint16_t itemsNum,
                                                    uint16_t sweepsNum,
                                                    E384Measurement vRest,
                                                    int32_t stopProtocolFlag);

E384C_API E384Err e384_setVoltageProtocolStep(E384Device* device,
                                              uint16_t itemIdx,
                                              uint16_t nextItemIdx,
                                              uint16_t loopReps,
                                              int32_t applyStepsFlag,
                                              E384Measurement v0,
                                              E384Measurement v0Step,
                                              E384Measurement t0,
                                              E384Measurement t0Step,
                                              int32_t vHalfFlag,
                                              const uint16_t* activeDigitalOutputs,
                                              size_t activeDigitalOutputsCount);

E384C_API E384Err e384_setVoltageProtocolRamp(E384Device* device,
                                              uint16_t itemIdx,
                                              uint16_t nextItemIdx,
                                              uint16_t loopReps,
                                              int32_t applyStepsFlag,
                                              E384Measurement v0,
                                              E384Measurement v0Step,
                                              E384Measurement vFinal,
                                              E384Measurement vFinalStep,
                                              E384Measurement t0,
                                              E384Measurement t0Step,
                                              int32_t vHalfFlag,
                                              const uint16_t* activeDigitalOutputs,
                                              size_t activeDigitalOutputsCount);

E384C_API E384Err e384_setVoltageProtocolSin(E384Device* device,
                                             uint16_t itemIdx,
                                             uint16_t nextItemIdx,
                                             uint16_t loopReps,
                                             int32_t applyStepsFlag,
                                             E384Measurement v0,
                                             E384Measurement v0Step,
                                             E384Measurement vAmp,
                                             E384Measurement vAmpStep,
                                             E384Measurement f0,
                                             E384Measurement f0Step,
                                             int32_t vHalfFlag,
                                             const uint16_t* activeDigitalOutputs,
                                             size_t activeDigitalOutputsCount);

E384C_API E384Err e384_setCurrentProtocolStructure(E384Device* device,
                                                    uint16_t protId,
                                                    uint16_t itemsNum,
                                                    uint16_t sweepsNum,
                                                    E384Measurement iRest,
                                                    int32_t stopProtocolFlag);

E384C_API E384Err e384_setCurrentProtocolStep(E384Device* device,
                                              uint16_t itemIdx,
                                              uint16_t nextItemIdx,
                                              uint16_t loopReps,
                                              int32_t applyStepsFlag,
                                              E384Measurement i0,
                                              E384Measurement i0Step,
                                              E384Measurement t0,
                                              E384Measurement t0Step,
                                              int32_t cHalfFlag,
                                              const uint16_t* activeDigitalOutputs,
                                              size_t activeDigitalOutputsCount);

E384C_API E384Err e384_setCurrentProtocolRamp(E384Device* device,
                                              uint16_t itemIdx,
                                              uint16_t nextItemIdx,
                                              uint16_t loopReps,
                                              int32_t applyStepsFlag,
                                              E384Measurement i0,
                                              E384Measurement i0Step,
                                              E384Measurement iFinal,
                                              E384Measurement iFinalStep,
                                              E384Measurement t0,
                                              E384Measurement t0Step,
                                              int32_t cHalfFlag,
                                              const uint16_t* activeDigitalOutputs,
                                              size_t activeDigitalOutputsCount);

E384C_API E384Err e384_setCurrentProtocolSin(E384Device* device,
                                             uint16_t itemIdx,
                                             uint16_t nextItemIdx,
                                             uint16_t loopReps,
                                             int32_t applyStepsFlag,
                                             E384Measurement i0,
                                             E384Measurement i0Step,
                                             E384Measurement iAmp,
                                             E384Measurement iAmpStep,
                                             E384Measurement f0,
                                             E384Measurement f0Step,
                                             int32_t cHalfFlag,
                                             const uint16_t* activeDigitalOutputs,
                                             size_t activeDigitalOutputsCount);

/*==================================================================*
 *  Protocol / command execution control.                           *
 *==================================================================*/

/*! Send any pending device commands. */
E384C_API E384Err e384_sendCommands(E384Device* device);

/*! Start the currently configured stimulus protocol. */
E384C_API E384Err e384_startProtocol(E384Device* device);

/*! Stop the currently running stimulus protocol. */
E384C_API E384Err e384_stopProtocol(E384Device* device);

/*! Start the currently configured state array. */
E384C_API E384Err e384_startStateArray(E384Device* device);

/*==================================================================*
 *  convert*Values family: raw int16_t<->double buffer conversion.  *
 *  These already cross the C boundary cleanly -- no marshaling.    *
 *==================================================================*/

E384C_API E384Err e384_convertVoltageValues(E384Device* device,
                                            int16_t* intValues,
                                            double* fltValues,
                                            int32_t valuesNum);

E384C_API E384Err e384_convertCurrentValues(E384Device* device,
                                            int16_t* intValues,
                                            double* fltValues,
                                            int32_t valuesNum);

/*! Fixed size = the device's temperature channel count (see
 *  e384_getTemperatureChannelsFeatures); caller must size both buffers
 *  accordingly, no count param here (matches the C++ signature). */
E384C_API E384Err e384_convertTemperatureValues(E384Device* device,
                                                int16_t* intValues,
                                                double* fltValues);

/*! Fixed size: intValues has 2 elements in, fltValue has 1 element out
 *  (matches the C++ signature exactly). */
E384C_API E384Err e384_convertOnTimeValue(E384Device* device,
                                          int16_t* intValues,
                                          double* fltValue);

/*==================================================================*
 *  The 15 overloaded C++ method names -- disambiguated by parameter *
 *  shape suffix, per the approved naming table.                    *
 *==================================================================*/

/* -- getChannelNumberFeatures: 3 overloads -- */
E384C_API E384Err e384_getChannelNumberFeatures_u16(E384Device* device,
                                                     uint16_t* outVoltageChannelNumber,
                                                     uint16_t* outCurrentChannelNumber);
E384C_API E384Err e384_getChannelNumberFeatures_int(E384Device* device,
                                                     int32_t* outVoltageChannelNumber,
                                                     int32_t* outCurrentChannelNumber);
E384C_API E384Err e384_getChannelNumberFeatures_intGp(E384Device* device,
                                                       int32_t* outVoltageChannelNumber,
                                                       int32_t* outCurrentChannelNumber,
                                                       int32_t* outGpChannelNumber);

/* -- setVCCurrentRange: (uint16_t,bool) already E384C_DECL_U16_APPLY's
 *    e384_setVCCurrentRange_all; here is the per-channel vector overload. */
E384C_API E384Err e384_setVCCurrentRange_perChannel(E384Device* device,
                                                     const uint16_t* channelIndexes,
                                                     const uint16_t* currentRangeIdx,
                                                     size_t count,
                                                     int32_t applyFlag);

/* -- setCCVoltageRange: (uint16_t,bool) is e384_setCCVoltageRange_all;
 *    here is the per-channel vector overload. */
E384C_API E384Err e384_setCCVoltageRange_perChannel(E384Device* device,
                                                     const uint16_t* channelIndexes,
                                                     const uint16_t* voltageRangeIdx,
                                                     size_t count,
                                                     int32_t applyFlag);

/* -- setClampingModality: 2 overloads -- */
E384C_API E384Err e384_setClampingModality_byIdx(E384Device* device,
                                                  uint32_t idx,
                                                  int32_t applyFlag,
                                                  int32_t stopProtocolFlag);
/*! mode is a ClampingModality_t value. */
E384C_API E384Err e384_setClampingModality_byEnum(E384Device* device,
                                                   int32_t mode,
                                                   int32_t applyFlag,
                                                   int32_t stopProtocolFlag);

/* -- convertVoltageValue: 2 overloads -- */
E384C_API E384Err e384_convertVoltageValue(E384Device* device,
                                           int16_t intValue,
                                           double* outFltValue);
E384C_API E384Err e384_convertVoltageValue_byChannel(E384Device* device,
                                                     int16_t intValue,
                                                     uint16_t channelIdx,
                                                     double* outFltValue);

/* -- convertCurrentValue: 2 overloads -- */
E384C_API E384Err e384_convertCurrentValue(E384Device* device,
                                           int16_t intValue,
                                           double* outFltValue);
E384C_API E384Err e384_convertCurrentValue_byChannel(E384Device* device,
                                                     int16_t intValue,
                                                     uint16_t channelIdx,
                                                     double* outFltValue);

/* -- getDeviceInfo: static (by deviceId, no handle) + instance -- */
E384C_API E384Err e384_getDeviceInfoForId(const char* deviceId,
                                          uint32_t* outDeviceVersion,
                                          uint32_t* outDeviceSubVersion,
                                          uint32_t* outFwMajor,
                                          uint32_t* outFwMinor,
                                          uint32_t* outFwPatch);
E384C_API E384Err e384_getDeviceInfo(E384Device* device,
                                     uint32_t* outDeviceVersion,
                                     uint32_t* outDeviceSubVersion,
                                     uint32_t* outFwMajor,
                                     uint32_t* outFwMinor,
                                     uint32_t* outFwPatch);

/* -- getVCCurrentRange: single is the existing e384_getVCCurrentRange;
 *    here is the list-without-default-idx overload. -- */
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getVCCurrentRange_list)

/* -- getCCVoltageRange: 2 overloads -- */
E384C_API E384Err e384_getCCVoltageRange(E384Device* device, E384RangedMeasurement* outRange);
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getCCVoltageRange_list)

/* -- getVoltageRange: 2 overloads -- */
E384C_API E384Err e384_getVoltageRange(E384Device* device, E384RangedMeasurement* outRange);
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getVoltageRange_list)

/* -- getCurrentRange: 2 overloads -- */
E384C_API E384Err e384_getCurrentRange(E384Device* device, E384RangedMeasurement* outRange);
E384C_DECL_GET_RANGED_LIST_NODEF(e384_getCurrentRange_list)

/* -- getVCCurrentRangeIdx: 2 overloads -- */
E384C_DECL_GET_U32(e384_getVCCurrentRangeIdx)
E384C_DECL_GET_U32_LIST(e384_getVCCurrentRangeIdx_list)

/* -- getCCVoltageRangeIdx: 2 overloads -- */
E384C_DECL_GET_U32(e384_getCCVoltageRangeIdx)
E384C_DECL_GET_U32_LIST(e384_getCCVoltageRangeIdx_list)

/* -- getVCCurrentRanges: (vector<Ranged>&,uint16_t&) is the existing
 *    e384_getVCCurrentRanges (deprecated overload); here is the
 *    per-channel vector<uint16_t> defaults overload. Two independent
 *    two-call size/fill outputs -- ranges and per-channel default idxs
 *    are not necessarily the same length. -- */
E384C_API E384Err e384_getVCCurrentRanges_perChannel(E384Device* device,
                                                     E384RangedMeasurement* outRanges,
                                                     size_t* rangesCount,
                                                     uint16_t* outDefaultIdxs,
                                                     size_t* idxCount);

/* -- getBoardsNumberFeatures: 2 overloads -- */
E384C_API E384Err e384_getBoardsNumberFeatures_u16(E384Device* device, uint16_t* out);
E384C_API E384Err e384_getBoardsNumberFeatures_int(E384Device* device, int32_t* out);

/*==================================================================*
 *  OK calibration RAM / EEPROM.                                    *
 *==================================================================*/

/*! Copy the calibration EEPROM contents into the RAMs. */
E384C_API E384Err e384_okMoveCalibrationEepromToRams(E384Device* device);

/*! Copy the calibration RAMs back into the EEPROM. */
E384C_API E384Err e384_okMoveCalibrationRamsToEeprom(E384Device* device);

/*! Select the active calibration RAM by index. */
E384C_API E384Err e384_okSelectCalibrationRam(E384Device* device, uint16_t ramIdx);

/*! Write one byte to the selected calibration RAM at the given address. */
E384C_API E384Err e384_okWriteCalibrationRam(E384Device* device,
                                             uint16_t address,
                                             uint8_t value);

/*! Trigger a read of the selected calibration RAM. */
E384C_API E384Err e384_okReadCalibrationRam(E384Device* device);

/*==================================================================*
 *  ChannelModel / BoardModel -- opaque, borrowed handles.           *
 *                                                                  *
 *  List getters follow the standard two-call size/fill protocol,   *
 *  but fill an array of HANDLES rather than value structs. Field    *
 *  accessors below mirror ChannelModel/BoardModel's own getters/    *
 *  setters 1:1 -- those C++ methods return plain values (not         *
 *  ErrorCodes_t), so these wrappers do too: NULL handles yield a     *
 *  zero-valued default rather than an error code, since there is    *
 *  no error channel to report one through.                          *
 *==================================================================*/

E384C_API E384Err e384_getChannels(E384Device* device, E384ChannelModel** out, size_t* count);
E384C_API E384Err e384_getChannelsOnBoard(E384Device* device, uint16_t boardIdx,
                                          E384ChannelModel** out, size_t* count);
E384C_API E384Err e384_getChannelsOnRow(E384Device* device, uint16_t rowIdx,
                                        E384ChannelModel** out, size_t* count);
E384C_API E384Err e384_getBoards(E384Device* device, E384BoardModel** out, size_t* count);

/* -- ChannelModel field accessors -- */

E384C_API uint16_t e384_channelModel_getId(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isOn(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isRecalibratingReadoutOffset(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isCompensatingLiquidJunction(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isCompensatingCfast(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isCompensatingCslowRs(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isCompensatingRsCp(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isCompensatingRsPg(const E384ChannelModel* ch);
E384C_API int32_t  e384_channelModel_isStimActive(const E384ChannelModel* ch);
E384C_API E384Measurement e384_channelModel_getVhold(const E384ChannelModel* ch);
E384C_API E384Measurement e384_channelModel_getChold(const E384ChannelModel* ch);
E384C_API E384Measurement e384_channelModel_getVhalf(const E384ChannelModel* ch);
E384C_API E384Measurement e384_channelModel_getChalf(const E384ChannelModel* ch);
E384C_API E384Measurement e384_channelModel_getLiquidJunctionVoltage(const E384ChannelModel* ch);

E384C_API void e384_channelModel_setId(E384ChannelModel* ch, uint16_t id);
E384C_API void e384_channelModel_setOn(E384ChannelModel* ch, int32_t on);
E384C_API void e384_channelModel_setRecalibratingReadoutOffset(E384ChannelModel* ch, int32_t recalibrating);
E384C_API void e384_channelModel_setCompensatingLiquidJunction(E384ChannelModel* ch, int32_t compensating);
E384C_API void e384_channelModel_setCompensatingCfast(E384ChannelModel* ch, int32_t compensating);
E384C_API void e384_channelModel_setCompensatingCslowRs(E384ChannelModel* ch, int32_t compensating);
E384C_API void e384_channelModel_setCompensatingRsCp(E384ChannelModel* ch, int32_t compensating);
E384C_API void e384_channelModel_setCompensatingRsPg(E384ChannelModel* ch, int32_t compensating);
E384C_API void e384_channelModel_setCompensatingCcCfast(E384ChannelModel* ch, int32_t compensating);
E384C_API void e384_channelModel_setStimActive(E384ChannelModel* ch, int32_t active);
E384C_API void e384_channelModel_setVhold(E384ChannelModel* ch, E384Measurement vHold);
E384C_API void e384_channelModel_setChold(E384ChannelModel* ch, E384Measurement cHold);
E384C_API void e384_channelModel_setVhalf(E384ChannelModel* ch, E384Measurement vHalf);
E384C_API void e384_channelModel_setChalf(E384ChannelModel* ch, E384Measurement cHalf);
E384C_API void e384_channelModel_setLiquidJunctionVoltage(E384ChannelModel* ch, E384Measurement voltage);

/* -- BoardModel field accessors -- */

E384C_API uint16_t e384_boardModel_getId(const E384BoardModel* board);
/*! Two-call size/fill protocol, same as e384_getChannels. */
E384C_API E384Err e384_boardModel_getChannelsOnBoard(const E384BoardModel* board,
                                                     E384ChannelModel** out, size_t* count);
E384C_API E384Measurement e384_boardModel_getGateVoltage(const E384BoardModel* board);
E384C_API E384Measurement e384_boardModel_getSourceVoltage(const E384BoardModel* board);

E384C_API void e384_boardModel_setId(E384BoardModel* board, uint16_t id);
E384C_API void e384_boardModel_setGateVoltage(E384BoardModel* board, E384Measurement gateVoltage);
E384C_API void e384_boardModel_setSourceVoltage(E384BoardModel* board, E384Measurement sourceVoltage);

/*! NOTE: BoardModel::setChannelsOnBoard(vector<ChannelModel*>) is NOT
 *  wrapped -- it would require constructing owned ChannelModel* instances
 *  from the Rust side, which the borrowed-handle model here doesn't
 *  support. This wiring is set up internally by the library at connect
 *  time; there's no legitimate external caller for it. */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* E384C_H */
