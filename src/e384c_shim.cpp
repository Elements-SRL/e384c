/*! \file e384c_shim.cpp
 *  \brief extern "C" implementations. Compiled as C++, exposes C symbols.
 */
#include "e384c_internal.hpp"

#include <string>
#include <vector>

namespace {

/*! Shared body for the two discovery functions, which differ only in the
 *  static method they call. An empty result is a valid (empty) list. */
template <typename Fn>
E384Err discover(E384DeviceList** outList, Fn&& staticCall) {
    if (outList == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    *outList = nullptr;
    try {
        std::vector<std::string> ids;
        const auto err = staticCall(ids);
        if (err == e384CommLib::Success ||
            err == e384CommLib::ErrorNoDeviceFound) {
            auto* list = new E384DeviceList;
            list->ids = std::move(ids);
            *outList = list;
        }
        return e384c::to_c(err);
    } catch (...) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
}

/*! Shared flattening logic for vector<vector<T>> getters: reports
 *  (rows, maxCols) on the sizing call, fills a row-major buffer padded
 *  with a fill value on the fill call. */
template <typename T>
void flatten_matrix(const std::vector<std::vector<T>>& src,
                    T* out, size_t* outRows, size_t* outCols, T padValue) {
    const size_t rows = src.size();
    size_t cols = 0;
    for (const auto& row : src) {
        cols = std::max(cols, row.size());
    }
    if (out != nullptr) {
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                out[r * cols + c] = (c < src[r].size()) ? src[r][c] : padValue;
            }
        }
    }
    *outRows = rows;
    *outCols = cols;
}

} // namespace

extern "C" {

/*==============================*
 *  Device discovery            *
 *==============================*/

E384C_API E384Err e384_detectDevices(E384DeviceList** outList) {
    return discover(outList, [](std::vector<std::string>& ids) {
        return MessageDispatcher::detectDevices(ids);
    });
}

E384C_API E384Err e384_listAllDevices(E384DeviceList** outList) {
    return discover(outList, [](std::vector<std::string>& ids) {
        return MessageDispatcher::listAllDevices(ids);
    });
}

E384C_API size_t e384_deviceList_count(const E384DeviceList* list) {
    return list ? list->ids.size() : 0;
}

E384C_API const char* e384_deviceList_get(const E384DeviceList* list, size_t index) {
    if (list == nullptr || index >= list->ids.size()) {
        return nullptr;
    }
    return list->ids[index].c_str();
}

E384C_API void e384_deviceList_free(E384DeviceList* list) {
    delete list;
}

/*==============================*
 *  Owned strings                *
 *==============================*/

E384C_API const char* e384_string_get(const E384String* str) {
    return str ? str->value.c_str() : nullptr;
}

E384C_API void e384_string_free(E384String* str) {
    delete str;
}

/*==============================*
 *  Connection lifecycle        *
 *==============================*/

E384C_API E384Err e384_connect(const char* deviceId, E384Device** outDevice) {
    if (outDevice == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    *outDevice = nullptr;
    if (deviceId == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorDeviceNotFound);
    }
    E384C_GUARD_BEGIN
    MessageDispatcher* dispatcher = nullptr;
    const auto err = MessageDispatcher::connectDevice(std::string(deviceId), dispatcher);
    if (err == e384CommLib::Success && dispatcher != nullptr) {
        *outDevice = e384c::handle(dispatcher);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_disconnect(E384Device* device, int32_t overheatFlag) {
    if (device == nullptr) {
        return E384_SUCCESS; /* tolerate double/NULL free, like free() */
    }
    E384C_GUARD_BEGIN
    MessageDispatcher* dispatcher = e384c::md(device);
    const auto err = dispatcher->disconnectDevice(overheatFlag != 0);
    delete dispatcher;
    return e384c::to_c(err);
    E384C_GUARD_END
}

/*==============================*
 *  Data acquisition (hot path) *
 *==============================*/

E384C_API E384Err e384_getRxDataBufferSize(E384Device* device, uint32_t* outSize) {
    E384C_CHECK_DEVICE(device)
    if (outSize == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->getRxDataBufferSize(*outSize));
    E384C_GUARD_END
}

E384C_API E384Err e384_allocateRxDataBuffer(E384Device* device, int16_t** outData) {
    E384C_CHECK_DEVICE(device)
    if (outData == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    *outData = nullptr;
    E384C_GUARD_BEGIN
    int16_t* data = nullptr;
    const auto err = e384c::md(device)->allocateRxDataBuffer(data);
    if (err == e384CommLib::Success) {
        *outData = data;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_deallocateRxDataBuffer(E384Device* device, int16_t** data) {
    E384C_CHECK_DEVICE(device)
    if (data == nullptr || *data == nullptr) {
        return E384_SUCCESS; /* NULL-tolerant, like free() */
    }
    E384C_GUARD_BEGIN
    const auto err = e384c::md(device)->deallocateRxDataBuffer(*data);
    *data = nullptr;
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getNextMessage(E384Device* device,
                                      E384RxOutput* rxOut,
                                      int16_t* data,
                                      int32_t msgType) {
    E384C_CHECK_DEVICE(device)
    if (rxOut == nullptr || data == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    const auto type = (msgType < 0)
        ? e384CommLib::MsgTypeIdInvalid
        : static_cast<e384CommLib::MsgTypeId_t>(msgType);
    e384CommLib::RxOutput_t rx;
    const auto err = e384c::md(device)->getNextMessage(rx, data, type);
    /* Copy field-by-field: E384RxOutput mirrors RxOutput_t exactly. */
    rxOut->msgTypeId         = rx.msgTypeId;
    rxOut->channelIdx        = rx.channelIdx;
    rxOut->protocolId        = rx.protocolId;
    rxOut->protocolItemIdx   = rx.protocolItemIdx;
    rxOut->protocolRepsIdx   = rx.protocolRepsIdx;
    rxOut->protocolSweepIdx  = rx.protocolSweepIdx;
    rxOut->totalMessages     = rx.totalMessages;
    rxOut->firstSampleOffset = rx.firstSampleOffset;
    rxOut->dataLen           = rx.dataLen;
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_purgeData(E384Device* device) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->purgeData());
    E384C_GUARD_END
}

/*==============================*
 *  Shape A: channel commands   *
 *==============================*/

E384C_WRAP_CHANNEL_MEAS_CMD(e384_setVoltageHoldTuner,     setVoltageHoldTuner)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCurrentHoldTuner,     setCurrentHoldTuner)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setVoltageHalf,          setVoltageHalf)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCurrentHalf,          setCurrentHalf)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setLiquidJunctionVoltage, setLiquidJunctionVoltage)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setGateVoltages,         setGateVoltages)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setSourceVoltages,       setSourceVoltages)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibVcCurrentGain,   setCalibVcCurrentGain)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibVcCurrentOffset, setCalibVcCurrentOffset)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibVcVoltageGain,   setCalibVcVoltageGain)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibVcVoltageOffset, setCalibVcVoltageOffset)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibCcCurrentGain,   setCalibCcCurrentGain)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibCcCurrentOffset, setCalibCcCurrentOffset)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibCcVoltageGain,   setCalibCcVoltageGain)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibCcVoltageOffset, setCalibCcVoltageOffset)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibRsCorrOffsetDac, setCalibRsCorrOffsetDac)
E384C_WRAP_CHANNEL_MEAS_CMD(e384_setCalibRShuntConductance, setCalibRShuntConductance)

/*==============================*
 *  Shape B: channel updates    *
 *==============================*/

E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibVcCurrentGain,   updateCalibVcCurrentGain)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibVcCurrentOffset, updateCalibVcCurrentOffset)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibVcVoltageGain,   updateCalibVcVoltageGain)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibVcVoltageOffset, updateCalibVcVoltageOffset)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibCcCurrentGain,   updateCalibCcCurrentGain)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibCcCurrentOffset, updateCalibCcCurrentOffset)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibCcVoltageGain,   updateCalibCcVoltageGain)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibCcVoltageOffset, updateCalibCcVoltageOffset)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibRsCorrOffsetDac, updateCalibRsCorrOffsetDac)
E384C_WRAP_CHANNEL_UPDATE(e384_updateCalibRShuntConductance, updateCalibRShuntConductance)
E384C_WRAP_CHANNEL_UPDATE(e384_resetOffsetRecalibration,   resetOffsetRecalibration)
E384C_WRAP_CHANNEL_UPDATE(e384_resetLiquidJunctionVoltage, resetLiquidJunctionVoltage)

/*==============================*
 *  Shape C: on/off commands    *
 *==============================*/

E384C_WRAP_CHANNEL_BOOL_CMD(e384_enableStimulus,             enableStimulus)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_turnChannelsOn,             turnChannelsOn)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_turnCalSwOn,                turnCalSwOn)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_turnVcSwOn,                 turnVcSwOn)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_turnCcSwOn,                 turnCcSwOn)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_enableCcStimulus,           enableCcStimulus)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_readoutOffsetRecalibration, readoutOffsetRecalibration)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_liquidJunctionCompensation, liquidJunctionCompensation)
E384C_WRAP_CHANNEL_BOOL_CMD(e384_digitalOffsetCompensation,  digitalOffsetCompensation)

/*==============================*
 *  Shape E: feature getters    *
 *==============================*/

E384C_WRAP_GET_RANGED(e384_getVCCurrentRange, getVCCurrentRange)

E384C_WRAP_GET_RANGED_LIST(e384_getVCCurrentRanges, getVCCurrentRanges)
E384C_WRAP_GET_RANGED_LIST(e384_getVCVoltageRanges, getVCVoltageRanges)
E384C_WRAP_GET_RANGED_LIST(e384_getCCCurrentRanges, getCCCurrentRanges)
E384C_WRAP_GET_RANGED_LIST(e384_getCCVoltageRanges, getCCVoltageRanges)

/*==============================*
 *  Shape D: scalar setters     *
 *==============================*/

E384C_WRAP_U16_APPLY(e384_setVCCurrentRange_all,      setVCCurrentRange)
E384C_WRAP_U16_APPLY(e384_setVCVoltageRange,          setVCVoltageRange)
E384C_WRAP_U16_APPLY(e384_setCCCurrentRange,          setCCCurrentRange)
E384C_WRAP_U16_APPLY(e384_setCCVoltageRange_all,      setCCVoltageRange)
E384C_WRAP_U16_APPLY(e384_setVoltageStimulusLpf,      setVoltageStimulusLpf)
E384C_WRAP_U16_APPLY(e384_setCurrentStimulusLpf,      setCurrentStimulusLpf)
E384C_WRAP_U16_APPLY(e384_setSourceForVoltageChannel, setSourceForVoltageChannel)
E384C_WRAP_U16_APPLY(e384_setSourceForCurrentChannel, setSourceForCurrentChannel)
E384C_WRAP_U16_APPLY(e384_setSamplingRate,            setSamplingRate)

/*==============================*
 *  Small scalar/bool commands  *
 *==============================*/

E384C_WRAP_U32(e384_setDownsamplingRatio, setDownsamplingRatio)

E384C_WRAP_BOOL2(e384_resetAsic,              resetAsic)
E384C_WRAP_BOOL2(e384_resetFpga,              resetFpga)
E384C_WRAP_BOOL2(e384_turnVoltageReaderOn,    turnVoltageReaderOn)
E384C_WRAP_BOOL2(e384_turnCurrentReaderOn,    turnCurrentReaderOn)
E384C_WRAP_BOOL2(e384_turnVoltageStimulusOn,  turnVoltageStimulusOn)
E384C_WRAP_BOOL2(e384_turnCurrentStimulusOn,  turnCurrentStimulusOn)
E384C_WRAP_BOOL2(e384_enableVcCompensations,  enableVcCompensations)
E384C_WRAP_BOOL2(e384_enableCcCompensations,  enableCcCompensations)

E384C_WRAP_BOOL1(e384_subtractLiquidJunctionFromCc, subtractLiquidJunctionFromCc)
E384C_WRAP_BOOL1(e384_setCalibrationMode,           setCalibrationMode)

E384C_WRAP_MEAS_BOOL(e384_setVoltageReference,   setVoltageReference)
E384C_WRAP_MEAS_BOOL(e384_setCoolingFansSpeed,   setCoolingFansSpeed)
E384C_WRAP_MEAS_BOOL(e384_setTemperatureControl, setTemperatureControl)

/*==============================*
 *  Small scalar/Measurement    *
 *  getters                     *
 *==============================*/

E384C_WRAP_GET_U32(e384_getClampingModalityIdx,        getClampingModalityIdx)
E384C_WRAP_GET_U32(e384_getVCVoltageRangeIdx,          getVCVoltageRangeIdx)
E384C_WRAP_GET_U32(e384_getCCCurrentRangeIdx,          getCCCurrentRangeIdx)
E384C_WRAP_GET_U32(e384_getSamplingRateIdx,            getSamplingRateIdx)
E384C_WRAP_GET_U32(e384_getMaxDownsamplingRatioFeature, getMaxDownsamplingRatioFeature)
E384C_WRAP_GET_U32(e384_getDownsamplingRatio,          getDownsamplingRatio)
E384C_WRAP_GET_U32(e384_getVCVoltageFilterIdx,         getVCVoltageFilterIdx)
E384C_WRAP_GET_U32(e384_getVCCurrentFilterIdx,         getVCCurrentFilterIdx)
E384C_WRAP_GET_U32(e384_getCCVoltageFilterIdx,         getCCVoltageFilterIdx)
E384C_WRAP_GET_U32(e384_getCCCurrentFilterIdx,         getCCCurrentFilterIdx)
E384C_WRAP_GET_U32(e384_getMaxProtocolItemsFeature,    getMaxProtocolItemsFeature)
E384C_WRAP_GET_U32(e384_getCalibrationEepromSize,      getCalibrationEepromSize)

E384C_WRAP_GET_MEAS(e384_getSamplingRate,     getSamplingRate)
E384C_WRAP_GET_MEAS(e384_getVCVoltageFilter,  getVCVoltageFilter)
E384C_WRAP_GET_MEAS(e384_getVCCurrentFilter,  getVCCurrentFilter)
E384C_WRAP_GET_MEAS(e384_getCCVoltageFilter,  getCCVoltageFilter)
E384C_WRAP_GET_MEAS(e384_getCCCurrentFilter,  getCCCurrentFilter)

E384C_WRAP_GET_MEAS_LIST(e384_getSamplingRatesFeatures,     getSamplingRatesFeatures)
E384C_WRAP_GET_MEAS_LIST(e384_getRealSamplingRatesFeatures, getRealSamplingRatesFeatures)
E384C_WRAP_GET_MEAS_LIST(e384_getVCVoltageFilters,          getVCVoltageFilters)
E384C_WRAP_GET_MEAS_LIST(e384_getVCCurrentFilters,          getVCCurrentFilters)
E384C_WRAP_GET_MEAS_LIST(e384_getCCVoltageFilters,          getCCVoltageFilters)
E384C_WRAP_GET_MEAS_LIST(e384_getCCCurrentFilters,          getCCCurrentFilters)
E384C_WRAP_GET_MEAS_LIST(e384_getVoltageHoldTuner,          getVoltageHoldTuner)

E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getVoltageHoldTunerFeatures,    getVoltageHoldTunerFeatures)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getVoltageHalfFeatures,         getVoltageHalfFeatures)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getCurrentHoldTunerFeatures,    getCurrentHoldTunerFeatures)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getCurrentHalfFeatures,         getCurrentHalfFeatures)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getLiquidJunctionRangesFeatures, getLiquidJunctionRangesFeatures)

E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMaxVCCurrentRange, getMaxVCCurrentRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMinVCCurrentRange, getMinVCCurrentRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMaxVCVoltageRange, getMaxVCVoltageRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMinVCVoltageRange, getMinVCVoltageRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMaxCCCurrentRange, getMaxCCCurrentRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMinCCCurrentRange, getMinCCCurrentRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMaxCCVoltageRange, getMaxCCVoltageRange)
E384C_WRAP_GET_RANGED_WITH_IDX(e384_getMinCCVoltageRange, getMinCCVoltageRange)

/*==============================*
 *  Capability probes           *
 *==============================*/

E384C_WRAP_PROBE(e384_hasCalSw,                     hasCalSw)
E384C_WRAP_PROBE(e384_hasGateVoltages,              hasGateVoltages)
E384C_WRAP_PROBE(e384_hasSourceVoltages,            hasSourceVoltages)
E384C_WRAP_PROBE(e384_isEpisodic,                   isEpisodic)
E384C_WRAP_PROBE(e384_hasProperHeaderPackets,       hasProperHeaderPackets)
E384C_WRAP_PROBE(e384_hasIndependentVCCurrentRanges, hasIndependentVCCurrentRanges)
E384C_WRAP_PROBE(e384_hasIndependentCCVoltageRanges, hasIndependentCCVoltageRanges)
E384C_WRAP_PROBE(e384_hasChannelSwitches,           hasChannelSwitches)
E384C_WRAP_PROBE(e384_hasStimulusSwitches,          hasStimulusSwitches)
E384C_WRAP_PROBE(e384_hasOffsetCompensation,        hasOffsetCompensation)
E384C_WRAP_PROBE(e384_hasStimulusHalf,              hasStimulusHalf)
E384C_WRAP_PROBE(e384_hasProtocols,                 hasProtocols)
E384C_WRAP_PROBE(e384_hasProtocolStepFeature,       hasProtocolStepFeature)
E384C_WRAP_PROBE(e384_hasProtocolRampFeature,       hasProtocolRampFeature)
E384C_WRAP_PROBE(e384_hasProtocolSinFeature,        hasProtocolSinFeature)
E384C_WRAP_PROBE(e384_isStateArrayAvailable,        isStateArrayAvailable)
E384C_WRAP_PROBE(e384_getCalibrationStatus,         getCalibrationStatus)

/*==============================*
 *  String-out getters          *
 *==============================*/

E384C_WRAP_GET_STRING_DIRECT(e384_getDeviceName,   getDeviceName)
E384C_WRAP_GET_STRING_DIRECT(e384_getDeviceSerial, getDeviceSerial)

E384C_WRAP_GET_STRING_ERR(e384_getSerialNumber,          getSerialNumber)
E384C_WRAP_GET_STRING_ERR(e384_getCalibMappingFileDir,   getCalibMappingFileDir)
E384C_WRAP_GET_STRING_ERR(e384_getCalibMappingFilePath,  getCalibMappingFilePath)

/*==============================*
 *  Misc hand-written one-offs  *
 *==============================*/

E384C_API E384Err e384_enableRxMessageType(E384Device* device, int32_t messageType, int32_t flag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->enableRxMessageType(
        static_cast<e384CommLib::MsgTypeId_t>(messageType), flag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setAdcCore(E384Device* device,
                                  const uint16_t* channelIndexes,
                                  const int32_t* clampingModes,
                                  size_t count,
                                  int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setAdcCore(
        e384c::vec_u16(channelIndexes, count),
        e384c::vec_enum<e384CommLib::ClampingModality_t>(clampingModes, count),
        applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_sendSpiCommand(E384Device* device, uint32_t command, uint32_t dataLoad) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->sendSpiCommand(command, dataLoad));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCustomFlag(E384Device* device, uint16_t idx, int32_t flag, int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCustomFlag(idx, flag != 0, applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCustomOption(E384Device* device, uint16_t idx, uint16_t value, int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCustomOption(idx, value, applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCustomDouble(E384Device* device, uint16_t idx, double value, int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCustomDouble(idx, value, applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setDebugBit(E384Device* device, uint16_t wordOffset, uint16_t bitOffset,
                                   int32_t status, int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setDebugBit(wordOffset, bitOffset, status != 0, applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setDebugWord(E384Device* device, uint16_t wordOffset, uint16_t wordValue) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setDebugWord(wordOffset, wordValue));
    E384C_GUARD_END
}

E384C_API E384Err e384_setStateArrayEnabled(E384Device* device, int32_t chIdx, int32_t enabledFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setStateArrayEnabled(chIdx, enabledFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setTemperatureControlPid(E384Device* device, E384PidParams params) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setTemperatureControlPid(e384c::from_c(params)));
    E384C_GUARD_END
}

E384C_API E384Err e384_zap(E384Device* device,
                           const uint16_t* channelIndexes,
                           size_t count,
                           E384Measurement duration) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->zap(
        e384c::vec_u16(channelIndexes, count), e384c::from_c(duration)));
    E384C_GUARD_END
}

E384C_API E384Err e384_setStateArrayStructure(E384Device* device,
                                              int32_t numberOfStates,
                                              int32_t initialState,
                                              E384Measurement reactionTime) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setStateArrayStructure(
        numberOfStates, initialState, e384c::from_c(reactionTime)));
    E384C_GUARD_END
}

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
                                         int32_t deltaFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setSateArrayState(
        stateIdx, e384c::from_c(voltage), timeoutStateFlag != 0, e384c::from_c(timeout),
        timeoutState, e384c::from_c(minTriggerValue), e384c::from_c(maxTriggerValue),
        triggerState, triggerFlag != 0, deltaFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_hasCompFeature(E384Device* device, int32_t feature, int32_t* outResult) {
    E384C_CHECK_DEVICE(device)
    if (outResult == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    const auto err = e384c::md(device)->hasCompFeature(
        static_cast<MessageDispatcher::CompensationUserParams_t>(feature));
    *outResult = (err == e384CommLib::Success) ? 1 : 0;
    return E384_SUCCESS;
    E384C_GUARD_END
}

E384C_API E384Err e384_getReadoutOffsetRecalibrationStatuses(E384Device* device,
                                                              const uint16_t* channelIndexes,
                                                              size_t count,
                                                              int32_t* outStatuses) {
    E384C_CHECK_DEVICE(device)
    if (outStatuses == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::OffsetRecalibStatus_t> statuses;
    const auto err = e384c::md(device)->getReadoutOffsetRecalibrationStatuses(
        e384c::vec_u16(channelIndexes, count), statuses);
    if (err == e384CommLib::Success) {
        for (size_t i = 0; i < statuses.size() && i < count; ++i) {
            outStatuses[i] = static_cast<int32_t>(statuses[i]);
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getLiquidJunctionStatuses(E384Device* device,
                                                 const uint16_t* channelIndexes,
                                                 size_t count,
                                                 int32_t* outStatuses) {
    E384C_CHECK_DEVICE(device)
    if (outStatuses == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::LiquidJunctionStatus_t> statuses;
    const auto err = e384c::md(device)->getLiquidJunctionStatuses(
        e384c::vec_u16(channelIndexes, count), statuses);
    if (err == e384CommLib::Success) {
        for (size_t i = 0; i < statuses.size() && i < count; ++i) {
            outStatuses[i] = static_cast<int32_t>(statuses[i]);
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getLiquidJunctionVoltages(E384Device* device,
                                                 const uint16_t* channelIndexes,
                                                 size_t count,
                                                 E384Measurement* outVoltages) {
    E384C_CHECK_DEVICE(device)
    if (outVoltages == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::Measurement_t> voltages;
    const auto err = e384c::md(device)->getLiquidJunctionVoltages(
        e384c::vec_u16(channelIndexes, count), voltages);
    if (err == e384CommLib::Success) {
        for (size_t i = 0; i < voltages.size() && i < count; ++i) {
            outVoltages[i] = e384c::to_c(voltages[i]);
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getClampingModalitiesFeatures(E384Device* device,
                                                     int32_t* out,
                                                     size_t* count) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::ClampingModality_t> modalities;
    const auto err = e384c::md(device)->getClampingModalitiesFeatures(modalities);
    if (err == e384CommLib::Success) {
        e384c::fill_enum_list(modalities, out, count);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getClampingModality(E384Device* device, int32_t* out) {
    E384C_CHECK_DEVICE(device)
    if (out == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    e384CommLib::ClampingModality_t modality;
    const auto err = e384c::md(device)->getClampingModality(modality);
    if (err == e384CommLib::Success) {
        *out = static_cast<int32_t>(modality);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCompensationEnables(E384Device* device,
                                              const uint16_t* channelIndexes,
                                              size_t count,
                                              int32_t type,
                                              uint8_t* outOnValues) {
    E384C_CHECK_DEVICE(device)
    if (outOnValues == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<bool> onValues;
    const auto err = e384c::md(device)->getCompensationEnables(
        e384c::vec_u16(channelIndexes, count),
        static_cast<MessageDispatcher::CompensationTypes_t>(type),
        onValues);
    if (err == e384CommLib::Success) {
        for (size_t i = 0; i < onValues.size() && i < count; ++i) {
            outOnValues[i] = onValues[i] ? 1 : 0;
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

/*==============================*
 *  Complex / multi-output      *
 *  getters and setters         *
 *==============================*/

E384C_API E384Err e384_getAvailableChannelsSourcesFeatures(E384Device* device,
                                                            E384ChannelSources* outVoltageSources,
                                                            E384ChannelSources* outCurrentSources) {
    E384C_CHECK_DEVICE(device)
    if (outVoltageSources == nullptr || outCurrentSources == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    e384CommLib::ChannelSources_t voltageSources;
    e384CommLib::ChannelSources_t currentSources;
    const auto err = e384c::md(device)->getAvailableChannelsSourcesFeatures(voltageSources, currentSources);
    if (err == e384CommLib::Success) {
        *outVoltageSources = e384c::to_c(voltageSources);
        *outCurrentSources = e384c::to_c(currentSources);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getTemperatureChannelsFeatures(E384Device* device,
                                                       E384RangedMeasurement* outRanges,
                                                       size_t* count,
                                                       E384DeviceList** outNames) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    if (outNames != nullptr) {
        *outNames = nullptr;
    }
    E384C_GUARD_BEGIN
    std::vector<std::string> names;
    std::vector<e384CommLib::RangedMeasurement_t> ranges;
    const auto err = e384c::md(device)->getTemperatureChannelsFeatures(names, ranges);
    if (err == e384CommLib::Success) {
        e384c::fill_ranged_list(ranges, outRanges, count);
        if (outRanges != nullptr && outNames != nullptr) {
            auto* list = new E384DeviceList;
            list->ids = std::move(names);
            *outNames = list;
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_writeCalibrationEeprom(E384Device* device,
                                              const uint32_t* value,
                                              const uint32_t* address,
                                              const uint32_t* size,
                                              size_t count) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    std::vector<uint32_t> v(value, value + count);
    std::vector<uint32_t> a(address, address + count);
    std::vector<uint32_t> s(size, size + count);
    return e384c::to_c(e384c::md(device)->writeCalibrationEeprom(v, a, s));
    E384C_GUARD_END
}

E384C_API E384Err e384_readCalibrationEeprom(E384Device* device,
                                             const uint32_t* address,
                                             const uint32_t* size,
                                             size_t count,
                                             uint32_t* outValue) {
    E384C_CHECK_DEVICE(device)
    if (outValue == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<uint32_t> a(address, address + count);
    std::vector<uint32_t> s(size, size + count);
    std::vector<uint32_t> value;
    const auto err = e384c::md(device)->readCalibrationEeprom(value, a, s);
    if (err == e384CommLib::Success) {
        for (size_t i = 0; i < value.size() && i < count; ++i) {
            outValue[i] = value[i];
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCalibFileNames(E384Device* device, E384DeviceList** outList) {
    if (outList == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    *outList = nullptr;
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    std::vector<std::string> names;
    const auto err = e384c::md(device)->getCalibFileNames(names);
    if (err == e384CommLib::Success) {
        auto* list = new E384DeviceList;
        list->ids = std::move(names);
        *outList = list;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCalibFilesFlags(E384Device* device,
                                          uint8_t* outFlags,
                                          size_t* outRows,
                                          size_t* outCols) {
    E384C_CHECK_DEVICE(device)
    if (outRows == nullptr || outCols == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<std::vector<bool>> flags;
    const auto err = e384c::md(device)->getCalibFilesFlags(flags);
    if (err == e384CommLib::Success) {
        const size_t rows = flags.size();
        size_t cols = 0;
        for (const auto& row : flags) {
            cols = std::max(cols, row.size());
        }
        if (outFlags != nullptr) {
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    outFlags[r * cols + c] = (c < flags[r].size() && flags[r][c]) ? 1 : 0;
                }
            }
        }
        *outRows = rows;
        *outCols = cols;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCompFeatures(E384Device* device,
                                       int32_t feature,
                                       E384RangedMeasurement* outRanges,
                                       size_t* count,
                                       double* outDefaultValue) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::RangedMeasurement_t> ranges;
    double defaultValue = 0.0;
    const auto err = e384c::md(device)->getCompFeatures(
        static_cast<MessageDispatcher::CompensationUserParams_t>(feature), ranges, defaultValue);
    if (err == e384CommLib::Success) {
        e384c::fill_ranged_list(ranges, outRanges, count);
        if (outDefaultValue != nullptr) {
            *outDefaultValue = defaultValue;
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCompOptionsFeatures(E384Device* device,
                                              int32_t type,
                                              E384DeviceList** outOptions) {
    if (outOptions == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    *outOptions = nullptr;
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    std::vector<std::string> options;
    const auto err = e384c::md(device)->getCompOptionsFeatures(
        static_cast<MessageDispatcher::CompensationTypes_t>(type), options);
    if (err == e384CommLib::Success) {
        auto* list = new E384DeviceList;
        list->ids = std::move(options);
        *outOptions = list;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCompValueMatrix(E384Device* device,
                                          double* outValues,
                                          size_t* outRows,
                                          size_t* outCols) {
    E384C_CHECK_DEVICE(device)
    if (outRows == nullptr || outCols == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<std::vector<double>> matrix;
    const auto err = e384c::md(device)->getCompValueMatrix(matrix);
    if (err == e384CommLib::Success) {
        flatten_matrix(matrix, outValues, outRows, outCols, 0.0);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCompensationControl(E384Device* device,
                                              int32_t param,
                                              E384CompensationControl* outControl) {
    E384C_CHECK_DEVICE(device)
    if (outControl == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    e384CommLib::CompensationControl_t control;
    const auto err = e384c::md(device)->getCompensationControl(
        static_cast<MessageDispatcher::CompensationUserParams_t>(param), control);
    if (err == e384CommLib::Success) {
        *outControl = e384c::to_c(control);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCustomFlags(E384Device* device,
                                      uint8_t* outDefaults,
                                      size_t* count,
                                      E384DeviceList** outNames) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    if (outNames != nullptr) {
        *outNames = nullptr;
    }
    E384C_GUARD_BEGIN
    std::vector<std::string> names;
    std::vector<bool> defaults;
    const auto err = e384c::md(device)->getCustomFlags(names, defaults);
    if (err == e384CommLib::Success) {
        const size_t available = defaults.size();
        if (outDefaults != nullptr) {
            const size_t n = std::min(*count, available);
            for (size_t i = 0; i < n; ++i) {
                outDefaults[i] = defaults[i] ? 1 : 0;
            }
        }
        *count = available;
        if (outDefaults != nullptr && outNames != nullptr) {
            auto* list = new E384DeviceList;
            list->ids = std::move(names);
            *outNames = list;
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCustomDoubles(E384Device* device,
                                        E384RangedMeasurement* outRanges,
                                        double* outDefaults,
                                        size_t* count,
                                        E384DeviceList** outNames) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    if (outNames != nullptr) {
        *outNames = nullptr;
    }
    E384C_GUARD_BEGIN
    std::vector<std::string> names;
    std::vector<e384CommLib::RangedMeasurement_t> ranges;
    std::vector<double> defaults;
    const auto err = e384c::md(device)->getCustomDoubles(names, ranges, defaults);
    if (err == e384CommLib::Success) {
        const size_t available = ranges.size();
        if (outRanges != nullptr) {
            const size_t n = std::min(*count, available);
            for (size_t i = 0; i < n; ++i) {
                outRanges[i] = e384c::to_c(ranges[i]);
                if (outDefaults != nullptr) {
                    outDefaults[i] = defaults[i];
                }
            }
        }
        *count = available;
        if (outRanges != nullptr && outNames != nullptr) {
            auto* list = new E384DeviceList;
            list->ids = std::move(names);
            *outNames = list;
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getVoltageProtocolRangeFeature(E384Device* device,
                                                       uint16_t rangeIdx,
                                                       E384RangedMeasurement* outRange) {
    E384C_CHECK_DEVICE(device)
    if (outRange == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    e384CommLib::RangedMeasurement_t range;
    const auto err = e384c::md(device)->getVoltageProtocolRangeFeature(rangeIdx, range);
    if (err == e384CommLib::Success) {
        *outRange = e384c::to_c(range);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getCurrentProtocolRangeFeature(E384Device* device,
                                                       uint16_t rangeIdx,
                                                       E384RangedMeasurement* outRange) {
    E384C_CHECK_DEVICE(device)
    if (outRange == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    e384CommLib::RangedMeasurement_t range;
    const auto err = e384c::md(device)->getCurrentProtocolRangeFeature(rangeIdx, range);
    if (err == e384CommLib::Success) {
        *outRange = e384c::to_c(range);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getVoltageRampTunerFeatures(E384Device* device,
                                                    E384RangedMeasurement* outVoltageRanges,
                                                    size_t* count,
                                                    E384RangedMeasurement* outDurationRange) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::RangedMeasurement_t> voltageRanges;
    e384CommLib::RangedMeasurement_t durationRange;
    const auto err = e384c::md(device)->getVoltageRampTunerFeatures(voltageRanges, durationRange);
    if (err == e384CommLib::Success) {
        e384c::fill_ranged_list(voltageRanges, outVoltageRanges, count);
        if (outDurationRange != nullptr) {
            *outDurationRange = e384c::to_c(durationRange);
        }
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_enableCompensation(E384Device* device,
                                          const uint16_t* channelIndexes,
                                          const uint8_t* onValues,
                                          size_t count,
                                          int32_t type,
                                          int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->enableCompensation(
        e384c::vec_u16(channelIndexes, count),
        static_cast<MessageDispatcher::CompensationTypes_t>(type),
        e384c::vec_bool(onValues, count),
        applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCompValues(E384Device* device,
                                     const uint16_t* channelIndexes,
                                     const double* newParamValues,
                                     size_t count,
                                     int32_t paramToUpdate,
                                     int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    std::vector<double> values(newParamValues, newParamValues + count);
    return e384c::to_c(e384c::md(device)->setCompValues(
        e384c::vec_u16(channelIndexes, count),
        static_cast<MessageDispatcher::CompensationUserParams_t>(paramToUpdate),
        values,
        applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCompRanges(E384Device* device,
                                     const uint16_t* channelIndexes,
                                     const uint16_t* newRanges,
                                     size_t count,
                                     int32_t paramToUpdate,
                                     int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCompRanges(
        e384c::vec_u16(channelIndexes, count),
        static_cast<MessageDispatcher::CompensationUserParams_t>(paramToUpdate),
        e384c::vec_u16(newRanges, count),
        applyFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCompOptions(E384Device* device,
                                      const uint16_t* channelIndexes,
                                      const uint16_t* options,
                                      size_t count,
                                      int32_t type,
                                      int32_t applyFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCompOptions(
        e384c::vec_u16(channelIndexes, count),
        static_cast<MessageDispatcher::CompensationTypes_t>(type),
        e384c::vec_u16(options, count),
        applyFlag != 0));
    E384C_GUARD_END
}

/*==============================*
 *  Protocol builders           *
 *==============================*/

E384C_API E384Err e384_setVoltageProtocolStructure(E384Device* device,
                                                    uint16_t protId,
                                                    uint16_t itemsNum,
                                                    uint16_t sweepsNum,
                                                    E384Measurement vRest,
                                                    int32_t stopProtocolFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setVoltageProtocolStructure(
        protId, itemsNum, sweepsNum, e384c::from_c(vRest), stopProtocolFlag != 0));
    E384C_GUARD_END
}

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
                                              size_t activeDigitalOutputsCount) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setVoltageProtocolStep(
        itemIdx, nextItemIdx, loopReps, applyStepsFlag != 0,
        e384c::from_c(v0), e384c::from_c(v0Step), e384c::from_c(t0), e384c::from_c(t0Step),
        vHalfFlag != 0, e384c::vec_u16(activeDigitalOutputs, activeDigitalOutputsCount)));
    E384C_GUARD_END
}

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
                                              size_t activeDigitalOutputsCount) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setVoltageProtocolRamp(
        itemIdx, nextItemIdx, loopReps, applyStepsFlag != 0,
        e384c::from_c(v0), e384c::from_c(v0Step), e384c::from_c(vFinal), e384c::from_c(vFinalStep),
        e384c::from_c(t0), e384c::from_c(t0Step),
        vHalfFlag != 0, e384c::vec_u16(activeDigitalOutputs, activeDigitalOutputsCount)));
    E384C_GUARD_END
}

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
                                             size_t activeDigitalOutputsCount) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setVoltageProtocolSin(
        itemIdx, nextItemIdx, loopReps, applyStepsFlag != 0,
        e384c::from_c(v0), e384c::from_c(v0Step), e384c::from_c(vAmp), e384c::from_c(vAmpStep),
        e384c::from_c(f0), e384c::from_c(f0Step),
        vHalfFlag != 0, e384c::vec_u16(activeDigitalOutputs, activeDigitalOutputsCount)));
    E384C_GUARD_END
}

E384C_API E384Err e384_setCurrentProtocolStructure(E384Device* device,
                                                    uint16_t protId,
                                                    uint16_t itemsNum,
                                                    uint16_t sweepsNum,
                                                    E384Measurement iRest,
                                                    int32_t stopProtocolFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCurrentProtocolStructure(
        protId, itemsNum, sweepsNum, e384c::from_c(iRest), stopProtocolFlag != 0));
    E384C_GUARD_END
}

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
                                              size_t activeDigitalOutputsCount) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCurrentProtocolStep(
        itemIdx, nextItemIdx, loopReps, applyStepsFlag != 0,
        e384c::from_c(i0), e384c::from_c(i0Step), e384c::from_c(t0), e384c::from_c(t0Step),
        cHalfFlag != 0, e384c::vec_u16(activeDigitalOutputs, activeDigitalOutputsCount)));
    E384C_GUARD_END
}

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
                                              size_t activeDigitalOutputsCount) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCurrentProtocolRamp(
        itemIdx, nextItemIdx, loopReps, applyStepsFlag != 0,
        e384c::from_c(i0), e384c::from_c(i0Step), e384c::from_c(iFinal), e384c::from_c(iFinalStep),
        e384c::from_c(t0), e384c::from_c(t0Step),
        cHalfFlag != 0, e384c::vec_u16(activeDigitalOutputs, activeDigitalOutputsCount)));
    E384C_GUARD_END
}

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
                                             size_t activeDigitalOutputsCount) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setCurrentProtocolSin(
        itemIdx, nextItemIdx, loopReps, applyStepsFlag != 0,
        e384c::from_c(i0), e384c::from_c(i0Step), e384c::from_c(iAmp), e384c::from_c(iAmpStep),
        e384c::from_c(f0), e384c::from_c(f0Step),
        cHalfFlag != 0, e384c::vec_u16(activeDigitalOutputs, activeDigitalOutputsCount)));
    E384C_GUARD_END
}

/*==================================================================*
 *  Protocol / command execution control.                           *
 *==================================================================*/

E384C_WRAP_ACTION(e384_sendCommands,    sendCommands)
E384C_WRAP_ACTION(e384_startProtocol,   startProtocol)
E384C_WRAP_ACTION(e384_stopProtocol,    stopProtocol)
E384C_WRAP_ACTION(e384_startStateArray, startStateArray)

/*==============================*
 *  convert*Values family       *
 *==============================*/

E384C_API E384Err e384_convertVoltageValues(E384Device* device,
                                            int16_t* intValues,
                                            double* fltValues,
                                            int32_t valuesNum) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->convertVoltageValues(intValues, fltValues, valuesNum));
    E384C_GUARD_END
}

E384C_API E384Err e384_convertCurrentValues(E384Device* device,
                                            int16_t* intValues,
                                            double* fltValues,
                                            int32_t valuesNum) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->convertCurrentValues(intValues, fltValues, valuesNum));
    E384C_GUARD_END
}

E384C_API E384Err e384_convertTemperatureValues(E384Device* device,
                                                int16_t* intValues,
                                                double* fltValues) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->convertTemperatureValues(intValues, fltValues));
    E384C_GUARD_END
}

E384C_API E384Err e384_convertOnTimeValue(E384Device* device,
                                          int16_t* intValues,
                                          double* fltValue) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->convertOnTimeValue(intValues, fltValue));
    E384C_GUARD_END
}

/*==============================*
 *  Proposal A: 15 overloaded   *
 *  C++ method names            *
 *==============================*/

E384C_API E384Err e384_getChannelNumberFeatures_u16(E384Device* device,
                                                     uint16_t* outVoltageChannelNumber,
                                                     uint16_t* outCurrentChannelNumber) {
    E384C_CHECK_DEVICE(device)
    if (outVoltageChannelNumber == nullptr || outCurrentChannelNumber == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    uint16_t v = 0, c = 0;
    const auto err = e384c::md(device)->getChannelNumberFeatures(v, c);
    if (err == e384CommLib::Success) {
        *outVoltageChannelNumber = v;
        *outCurrentChannelNumber = c;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getChannelNumberFeatures_int(E384Device* device,
                                                     int32_t* outVoltageChannelNumber,
                                                     int32_t* outCurrentChannelNumber) {
    E384C_CHECK_DEVICE(device)
    if (outVoltageChannelNumber == nullptr || outCurrentChannelNumber == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    int v = 0, c = 0;
    const auto err = e384c::md(device)->getChannelNumberFeatures(v, c);
    if (err == e384CommLib::Success) {
        *outVoltageChannelNumber = v;
        *outCurrentChannelNumber = c;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getChannelNumberFeatures_intGp(E384Device* device,
                                                       int32_t* outVoltageChannelNumber,
                                                       int32_t* outCurrentChannelNumber,
                                                       int32_t* outGpChannelNumber) {
    E384C_CHECK_DEVICE(device)
    if (outVoltageChannelNumber == nullptr || outCurrentChannelNumber == nullptr ||
        outGpChannelNumber == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    int v = 0, c = 0, gp = 0;
    const auto err = e384c::md(device)->getChannelNumberFeatures(v, c, gp);
    if (err == e384CommLib::Success) {
        *outVoltageChannelNumber = v;
        *outCurrentChannelNumber = c;
        *outGpChannelNumber = gp;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_WRAP_CHANNEL_U16_CMD(e384_setVCCurrentRange_perChannel, setVCCurrentRange)
E384C_WRAP_CHANNEL_U16_CMD(e384_setCCVoltageRange_perChannel, setCCVoltageRange)

E384C_API E384Err e384_setClampingModality_byIdx(E384Device* device,
                                                  uint32_t idx,
                                                  int32_t applyFlag,
                                                  int32_t stopProtocolFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setClampingModality(idx, applyFlag != 0, stopProtocolFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_setClampingModality_byEnum(E384Device* device,
                                                   int32_t mode,
                                                   int32_t applyFlag,
                                                   int32_t stopProtocolFlag) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->setClampingModality(
        static_cast<e384CommLib::ClampingModality_t>(mode), applyFlag != 0, stopProtocolFlag != 0));
    E384C_GUARD_END
}

E384C_API E384Err e384_convertVoltageValue(E384Device* device, int16_t intValue, double* outFltValue) {
    E384C_CHECK_DEVICE(device)
    if (outFltValue == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    double flt = 0.0;
    const auto err = e384c::md(device)->convertVoltageValue(intValue, flt);
    if (err == e384CommLib::Success) {
        *outFltValue = flt;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_convertVoltageValue_byChannel(E384Device* device, int16_t intValue,
                                                     uint16_t channelIdx, double* outFltValue) {
    E384C_CHECK_DEVICE(device)
    if (outFltValue == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    double flt = 0.0;
    const auto err = e384c::md(device)->convertVoltageValue(intValue, channelIdx, flt);
    if (err == e384CommLib::Success) {
        *outFltValue = flt;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_convertCurrentValue(E384Device* device, int16_t intValue, double* outFltValue) {
    E384C_CHECK_DEVICE(device)
    if (outFltValue == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    double flt = 0.0;
    const auto err = e384c::md(device)->convertCurrentValue(intValue, flt);
    if (err == e384CommLib::Success) {
        *outFltValue = flt;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_convertCurrentValue_byChannel(E384Device* device, int16_t intValue,
                                                     uint16_t channelIdx, double* outFltValue) {
    E384C_CHECK_DEVICE(device)
    if (outFltValue == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    double flt = 0.0;
    const auto err = e384c::md(device)->convertCurrentValue(intValue, channelIdx, flt);
    if (err == e384CommLib::Success) {
        *outFltValue = flt;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getDeviceInfoForId(const char* deviceId,
                                          uint32_t* outDeviceVersion,
                                          uint32_t* outDeviceSubVersion,
                                          uint32_t* outFwMajor,
                                          uint32_t* outFwMinor,
                                          uint32_t* outFwPatch) {
    if (deviceId == nullptr || outDeviceVersion == nullptr || outDeviceSubVersion == nullptr ||
        outFwMajor == nullptr || outFwMinor == nullptr || outFwPatch == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    unsigned int deviceVersion = 0, deviceSubVersion = 0, fwMajor = 0, fwMinor = 0, fwPatch = 0;
    const auto err = MessageDispatcher::getDeviceInfo(
        std::string(deviceId), deviceVersion, deviceSubVersion, fwMajor, fwMinor, fwPatch);
    if (err == e384CommLib::Success) {
        *outDeviceVersion = deviceVersion;
        *outDeviceSubVersion = deviceSubVersion;
        *outFwMajor = fwMajor;
        *outFwMinor = fwMinor;
        *outFwPatch = fwPatch;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getDeviceInfo(E384Device* device,
                                     uint32_t* outDeviceVersion,
                                     uint32_t* outDeviceSubVersion,
                                     uint32_t* outFwMajor,
                                     uint32_t* outFwMinor,
                                     uint32_t* outFwPatch) {
    E384C_CHECK_DEVICE(device)
    if (outDeviceVersion == nullptr || outDeviceSubVersion == nullptr ||
        outFwMajor == nullptr || outFwMinor == nullptr || outFwPatch == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    unsigned int deviceVersion = 0, deviceSubVersion = 0, fwMajor = 0, fwMinor = 0, fwPatch = 0;
    const auto err = e384c::md(device)->getDeviceInfo(
        deviceVersion, deviceSubVersion, fwMajor, fwMinor, fwPatch);
    if (err == e384CommLib::Success) {
        *outDeviceVersion = deviceVersion;
        *outDeviceSubVersion = deviceSubVersion;
        *outFwMajor = fwMajor;
        *outFwMinor = fwMinor;
        *outFwPatch = fwPatch;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getVCCurrentRange_list, getVCCurrentRange)

E384C_WRAP_GET_RANGED(e384_getCCVoltageRange, getCCVoltageRange)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getCCVoltageRange_list, getCCVoltageRange)

E384C_WRAP_GET_RANGED(e384_getVoltageRange, getVoltageRange)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getVoltageRange_list, getVoltageRange)

E384C_WRAP_GET_RANGED(e384_getCurrentRange, getCurrentRange)
E384C_WRAP_GET_RANGED_LIST_NODEF(e384_getCurrentRange_list, getCurrentRange)

E384C_WRAP_GET_U32(e384_getVCCurrentRangeIdx, getVCCurrentRangeIdx)
E384C_WRAP_GET_U32_LIST(e384_getVCCurrentRangeIdx_list, getVCCurrentRangeIdx)

E384C_WRAP_GET_U32(e384_getCCVoltageRangeIdx, getCCVoltageRangeIdx)
E384C_WRAP_GET_U32_LIST(e384_getCCVoltageRangeIdx_list, getCCVoltageRangeIdx)

E384C_API E384Err e384_getVCCurrentRanges_perChannel(E384Device* device,
                                                     E384RangedMeasurement* outRanges,
                                                     size_t* rangesCount,
                                                     uint16_t* outDefaultIdxs,
                                                     size_t* idxCount) {
    E384C_CHECK_DEVICE(device)
    if (rangesCount == nullptr || idxCount == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<e384CommLib::RangedMeasurement_t> ranges;
    std::vector<uint16_t> defaultIdxs;
    const auto err = e384c::md(device)->getVCCurrentRanges(ranges, defaultIdxs);
    if (err == e384CommLib::Success) {
        e384c::fill_ranged_list(ranges, outRanges, rangesCount);
        e384c::fill_u16_list(defaultIdxs, outDefaultIdxs, idxCount);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getBoardsNumberFeatures_u16(E384Device* device, uint16_t* out) {
    E384C_CHECK_DEVICE(device)
    if (out == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    uint16_t value = 0;
    const auto err = e384c::md(device)->getBoardsNumberFeatures(value);
    if (err == e384CommLib::Success) {
        *out = value;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getBoardsNumberFeatures_int(E384Device* device, int32_t* out) {
    E384C_CHECK_DEVICE(device)
    if (out == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    int value = 0;
    const auto err = e384c::md(device)->getBoardsNumberFeatures(value);
    if (err == e384CommLib::Success) {
        *out = value;
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

/*==============================*
 *  OK calibration RAM/EEPROM   *
 *==============================*/

E384C_WRAP_ACTION(e384_okMoveCalibrationEepromToRams, okMoveCalibrationEepromToRams)
E384C_WRAP_ACTION(e384_okMoveCalibrationRamsToEeprom, okMoveCalibrationRamsToEeprom)
E384C_WRAP_ACTION(e384_okReadCalibrationRam,          okReadCalibrationRam)

E384C_WRAP_U16(e384_okSelectCalibrationRam, okSelectCalibrationRam)

/*! Two-scalar (uint16_t address, uint8_t value); hand-written, no macro. */
E384C_API E384Err e384_okWriteCalibrationRam(E384Device* device,
                                             uint16_t address,
                                             uint8_t value) {
    E384C_CHECK_DEVICE(device)
    E384C_GUARD_BEGIN
    return e384c::to_c(e384c::md(device)->okWriteCalibrationRam(address, value));
    E384C_GUARD_END
}

/*==============================*
 *  ChannelModel / BoardModel   *
 *==============================*/

E384C_API E384Err e384_getChannels(E384Device* device, E384ChannelModel** out, size_t* count) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<ChannelModel*> channels;
    const auto err = e384c::md(device)->getChannels(channels);
    if (err == e384CommLib::Success) {
        e384c::fill_channel_handle_list(channels, out, count);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getChannelsOnBoard(E384Device* device, uint16_t boardIdx,
                                          E384ChannelModel** out, size_t* count) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<ChannelModel*> channels;
    const auto err = e384c::md(device)->getChannelsOnBoard(boardIdx, channels);
    if (err == e384CommLib::Success) {
        e384c::fill_channel_handle_list(channels, out, count);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getChannelsOnRow(E384Device* device, uint16_t rowIdx,
                                        E384ChannelModel** out, size_t* count) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<ChannelModel*> channels;
    const auto err = e384c::md(device)->getChannelsOnRow(rowIdx, channels);
    if (err == e384CommLib::Success) {
        e384c::fill_channel_handle_list(channels, out, count);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API E384Err e384_getBoards(E384Device* device, E384BoardModel** out, size_t* count) {
    E384C_CHECK_DEVICE(device)
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<BoardModel*> boards;
    const auto err = e384c::md(device)->getBoards(boards);
    if (err == e384CommLib::Success) {
        e384c::fill_board_handle_list(boards, out, count);
    }
    return e384c::to_c(err);
    E384C_GUARD_END
}

E384C_API uint16_t e384_channelModel_getId(const E384ChannelModel* ch) {
    return ch ? e384c::cm(ch)->getId() : 0;
}
E384C_API int32_t e384_channelModel_isOn(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isOn()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isRecalibratingReadoutOffset(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isRecalibratingReadoutOffset()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isCompensatingLiquidJunction(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isCompensatingLiquidJunction()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isCompensatingCfast(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isCompensatingCfast()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isCompensatingCslowRs(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isCompensatingCslowRs()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isCompensatingRsCp(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isCompensatingRsCp()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isCompensatingRsPg(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isCompensatingRsPg()) ? 1 : 0;
}
E384C_API int32_t e384_channelModel_isStimActive(const E384ChannelModel* ch) {
    return (ch && e384c::cm(ch)->isStimActive()) ? 1 : 0;
}

E384C_API E384Measurement e384_channelModel_getVhold(const E384ChannelModel* ch) {
    if (!ch) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::cm(ch)->getVhold());
}
E384C_API E384Measurement e384_channelModel_getChold(const E384ChannelModel* ch) {
    if (!ch) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::cm(ch)->getChold());
}
E384C_API E384Measurement e384_channelModel_getVhalf(const E384ChannelModel* ch) {
    if (!ch) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::cm(ch)->getVhalf());
}
E384C_API E384Measurement e384_channelModel_getChalf(const E384ChannelModel* ch) {
    if (!ch) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::cm(ch)->getChalf());
}
E384C_API E384Measurement e384_channelModel_getLiquidJunctionVoltage(const E384ChannelModel* ch) {
    if (!ch) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::cm(ch)->getLiquidJunctionVoltage());
}

E384C_API void e384_channelModel_setId(E384ChannelModel* ch, uint16_t id) {
    if (ch) e384c::cm(ch)->setId(id);
}
E384C_API void e384_channelModel_setOn(E384ChannelModel* ch, int32_t on) {
    if (ch) e384c::cm(ch)->setOn(on != 0);
}
E384C_API void e384_channelModel_setRecalibratingReadoutOffset(E384ChannelModel* ch, int32_t recalibrating) {
    if (ch) e384c::cm(ch)->setRecalibratingReadoutOffset(recalibrating != 0);
}
E384C_API void e384_channelModel_setCompensatingLiquidJunction(E384ChannelModel* ch, int32_t compensating) {
    if (ch) e384c::cm(ch)->setCompensatingLiquidJunction(compensating != 0);
}
E384C_API void e384_channelModel_setCompensatingCfast(E384ChannelModel* ch, int32_t compensating) {
    if (ch) e384c::cm(ch)->setCompensatingCfast(compensating != 0);
}
E384C_API void e384_channelModel_setCompensatingCslowRs(E384ChannelModel* ch, int32_t compensating) {
    if (ch) e384c::cm(ch)->setCompensatingCslowRs(compensating != 0);
}
E384C_API void e384_channelModel_setCompensatingRsCp(E384ChannelModel* ch, int32_t compensating) {
    if (ch) e384c::cm(ch)->setCompensatingRsCp(compensating != 0);
}
E384C_API void e384_channelModel_setCompensatingRsPg(E384ChannelModel* ch, int32_t compensating) {
    if (ch) e384c::cm(ch)->setCompensatingRsPg(compensating != 0);
}
E384C_API void e384_channelModel_setCompensatingCcCfast(E384ChannelModel* ch, int32_t compensating) {
    if (ch) e384c::cm(ch)->setCompensatingCcCfast(compensating != 0);
}
E384C_API void e384_channelModel_setStimActive(E384ChannelModel* ch, int32_t active) {
    if (ch) e384c::cm(ch)->setStimActive(active != 0);
}
E384C_API void e384_channelModel_setVhold(E384ChannelModel* ch, E384Measurement vHold) {
    if (ch) e384c::cm(ch)->setVhold(e384c::from_c(vHold));
}
E384C_API void e384_channelModel_setChold(E384ChannelModel* ch, E384Measurement cHold) {
    if (ch) e384c::cm(ch)->setChold(e384c::from_c(cHold));
}
E384C_API void e384_channelModel_setVhalf(E384ChannelModel* ch, E384Measurement vHalf) {
    if (ch) e384c::cm(ch)->setVhalf(e384c::from_c(vHalf));
}
E384C_API void e384_channelModel_setChalf(E384ChannelModel* ch, E384Measurement cHalf) {
    if (ch) e384c::cm(ch)->setChalf(e384c::from_c(cHalf));
}
E384C_API void e384_channelModel_setLiquidJunctionVoltage(E384ChannelModel* ch, E384Measurement voltage) {
    if (ch) e384c::cm(ch)->setLiquidJunctionVoltage(e384c::from_c(voltage));
}

E384C_API uint16_t e384_boardModel_getId(const E384BoardModel* board) {
    return board ? e384c::bm(board)->getId() : 0;
}

E384C_API E384Err e384_boardModel_getChannelsOnBoard(const E384BoardModel* board,
                                                     E384ChannelModel** out, size_t* count) {
    if (count == nullptr) {
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    if (board == nullptr) {
        *count = 0;
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);
    }
    E384C_GUARD_BEGIN
    std::vector<ChannelModel*> channels = e384c::bm(board)->getChannelsOnBoard();
    e384c::fill_channel_handle_list(channels, out, count);
    return E384_SUCCESS;
    E384C_GUARD_END
}

E384C_API E384Measurement e384_boardModel_getGateVoltage(const E384BoardModel* board) {
    if (!board) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::bm(board)->getGateVoltage());
}
E384C_API E384Measurement e384_boardModel_getSourceVoltage(const E384BoardModel* board) {
    if (!board) { E384Measurement z{}; return z; }
    return e384c::to_c(e384c::bm(board)->getSourceVoltage());
}

E384C_API void e384_boardModel_setId(E384BoardModel* board, uint16_t id) {
    if (board) e384c::bm(board)->setId(id);
}
E384C_API void e384_boardModel_setGateVoltage(E384BoardModel* board, E384Measurement gateVoltage) {
    if (board) e384c::bm(board)->setGateVoltage(e384c::from_c(gateVoltage));
}
E384C_API void e384_boardModel_setSourceVoltage(E384BoardModel* board, E384Measurement sourceVoltage) {
    if (board) e384c::bm(board)->setSourceVoltage(e384c::from_c(sourceVoltage));
}

} /* extern "C" */
