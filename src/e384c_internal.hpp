/*! \file e384c_internal.hpp
 *  \brief C++-only helpers for the shim: type converters and wrapper macros.
 *         Never installed, never seen by bindgen.
 */
#ifndef E384C_INTERNAL_HPP
#define E384C_INTERNAL_HPP

#include <string>
#include <vector>
#include <cstdint>

#include "e384c.h"

#include "e384commlib_errorcodes.h"
#include "e384commlib_global.h"
#include "messagedispatcher.h"

namespace e384c {

using namespace e384CommLib;

/*==============================*
 *  Handle <-> pointer          *
 *==============================*/

inline MessageDispatcher* md(E384Device* d) {
    return reinterpret_cast<MessageDispatcher*>(d);
}

inline E384Device* handle(MessageDispatcher* p) {
    return reinterpret_cast<E384Device*>(p);
}

/*==============================*
 *  Error code passthrough      *
 *==============================*/

inline E384Err to_c(ErrorCodes_t e) {
    return static_cast<E384Err>(e);
}

/*==============================*
 *  Unit string <-> enum        *
 *==============================*/

inline E384Unit unit_to_c(const std::string& u) {
    if (u.empty())  return E384_UNIT_NONE;
    if (u == "V")   return E384_UNIT_V;
    if (u == "A")   return E384_UNIT_A;
    if (u == "Hz")  return E384_UNIT_HZ;
    if (u == "s")   return E384_UNIT_S;
    if (u == "C")   return E384_UNIT_C;
    return E384_UNIT_UNKNOWN;
}

inline const char* unit_from_c(int32_t u) {
    switch (static_cast<E384Unit>(u)) {
        case E384_UNIT_V:    return "V";
        case E384_UNIT_A:    return "A";
        case E384_UNIT_HZ:   return "Hz";
        case E384_UNIT_S:    return "s";
        case E384_UNIT_C:    return "C";
        case E384_UNIT_NONE: return "";
        default:             return ""; /* UNKNOWN is output-only */
    }
}

/*==============================*
 *  Measurement converters      *
 *==============================*/

inline E384Measurement to_c(const Measurement_t& m) {
    E384Measurement c;
    c.value  = m.value;
    c.prefix = static_cast<int32_t>(m.prefix);
    c.unit   = static_cast<int32_t>(unit_to_c(m.unit));
    return c;
}

inline Measurement_t from_c(const E384Measurement& c) {
    Measurement_t m;
    m.value  = c.value;
    m.prefix = static_cast<UnitPfx_t>(c.prefix);
    m.unit   = unit_from_c(c.unit);
    return m;
}

inline E384RangedMeasurement to_c(const RangedMeasurement_t& r) {
    E384RangedMeasurement c;
    c.min    = r.min;
    c.max    = r.max;
    c.step   = r.step;
    c.prefix = static_cast<int32_t>(r.prefix);
    c.unit   = static_cast<int32_t>(unit_to_c(r.unit));
    return c;
}

inline RangedMeasurement_t from_c(const E384RangedMeasurement& c) {
    RangedMeasurement_t r;
    r.min    = c.min;
    r.max    = c.max;
    r.step   = c.step;
    r.prefix = static_cast<UnitPfx_t>(c.prefix);
    r.unit   = unit_from_c(c.unit);
    return r;
}

/*==============================*
 *  Array -> vector marshaling  *
 *==============================*/

inline std::vector<uint16_t> vec_u16(const uint16_t* p, size_t n) {
    return std::vector<uint16_t>(p, p + n);
}

inline std::vector<Measurement_t> vec_meas(const E384Measurement* p, size_t n) {
    std::vector<Measurement_t> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        v.push_back(from_c(p[i]));
    }
    return v;
}

} // namespace e384c

/*==================================================================*
 *  Wrapper macros                                                  *
 *                                                                  *
 *  The library reports failures via ErrorCodes_t, not exceptions,  *
 *  but the shim still guards against anything leaking through      *
 *  (std::bad_alloc, bugs) because unwinding across the C boundary  *
 *  is undefined behavior.                                          *
 *==================================================================*/

#define E384C_GUARD_BEGIN try {
#define E384C_GUARD_END                                   \
    } catch (...) {                                       \
        return static_cast<E384Err>(e384CommLib::ErrorUnknown); \
    }

/*! Null-handle check shared by every instance method. */
#define E384C_CHECK_DEVICE(device)                                  \
    if ((device) == nullptr) {                                      \
        return static_cast<E384Err>(e384CommLib::ErrorDeviceNotConnected); \
    }

/*! Shape A: (vector<uint16_t> channels, vector<Measurement_t> values,
 *  bool applyFlag) -> ErrorCodes_t */
#define E384C_WRAP_CHANNEL_MEAS_CMD(cname, method)                            \
    E384C_API E384Err cname(E384Device* device,                               \
                            const uint16_t* channelIndexes,                   \
                            const E384Measurement* values,                    \
                            size_t count,                                     \
                            int32_t applyFlag) {                              \
        E384C_CHECK_DEVICE(device)                                            \
        E384C_GUARD_BEGIN                                                     \
        return e384c::to_c(e384c::md(device)->method(                         \
            e384c::vec_u16(channelIndexes, count),                            \
            e384c::vec_meas(values, count),                                   \
            applyFlag != 0));                                                 \
        E384C_GUARD_END                                                       \
    }

/*! Shape E: single RangedMeasurement_t out-param getter. */
#define E384C_WRAP_GET_RANGED(cname, method)                                  \
    E384C_API E384Err cname(E384Device* device,                               \
                            E384RangedMeasurement* out) {                     \
        E384C_CHECK_DEVICE(device)                                            \
        if (out == nullptr) {                                                 \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);           \
        }                                                                     \
        E384C_GUARD_BEGIN                                                     \
        e384CommLib::RangedMeasurement_t range;                               \
        const auto err = e384c::md(device)->method(range);                    \
        if (err == e384CommLib::Success) {                                    \
            *out = e384c::to_c(range);                                        \
        }                                                                     \
        return e384c::to_c(err);                                              \
        E384C_GUARD_END                                                       \
    }

#endif /* E384C_INTERNAL_HPP */
