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

} /* extern "C" */
