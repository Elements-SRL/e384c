/*! \file e384c_shim.cpp
 *  \brief extern "C" implementations. Compiled as C++, exposes C symbols.
 */
#include "e384c_internal.hpp"

#include <string>

extern "C" {

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
 *  Shape A: channel commands   *
 *==============================*/

E384C_WRAP_CHANNEL_MEAS_CMD(e384_setVoltageHoldTuner, setVoltageHoldTuner)

/*==============================*
 *  Shape E: ranged getters     *
 *==============================*/

E384C_WRAP_GET_RANGED(e384_getVCCurrentRange, getVCCurrentRange)

} /* extern "C" */
