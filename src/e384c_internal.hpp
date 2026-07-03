/*! \file e384c_internal.hpp
 *  \brief C++-only helpers for the shim: type converters and wrapper macros.
 *         Never installed, never seen by bindgen.
 */
#ifndef E384C_INTERNAL_HPP
#define E384C_INTERNAL_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

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

/*! std::vector<bool> is bit-packed: element-wise conversion is mandatory,
 *  a reinterpret_cast of the uint8_t array would be undefined behavior. */
inline std::vector<bool> vec_bool(const uint8_t* p, size_t n) {
    std::vector<bool> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = (p[i] != 0);
    }
    return v;
}

/*==================================================*
 *  Vector-out helper for the two-call protocol     *
 *  (see E384C_DECL_GET_RANGED_LIST in e384c.h)     *
 *==================================================*/

inline void fill_ranged_list(const std::vector<RangedMeasurement_t>& src,
                             E384RangedMeasurement* out,
                             size_t* count) {
    const size_t available = src.size();
    if (out != nullptr) {
        const size_t n = std::min(*count, available);
        for (size_t i = 0; i < n; ++i) {
            out[i] = to_c(src[i]);
        }
    }
    *count = available;
}

} // namespace e384c

/*! Opaque-in-C list of device IDs; owns its strings. */
struct E384DeviceList {
    std::vector<std::string> ids;
};

/*==================================================================*
 *  Wrapper macros                                                  *
 *                                                                  *
 *  The library reports failures via ErrorCodes_t, not exceptions,  *
 *  but the shim still guards against anything leaking through      *
 *  (std::bad_alloc, bugs) because unwinding across the C boundary  *
 *  is undefined behavior.                                          *
 *==================================================================*/

#define E384C_GUARD_BEGIN try {
#define E384C_GUARD_END                                            \
    } catch (...) {                                                \
        return static_cast<E384Err>(e384CommLib::ErrorUnknown);    \
    }

/*! Null-handle check shared by every instance method. */
#define E384C_CHECK_DEVICE(device)                                          \
    if ((device) == nullptr) {                                              \
        return static_cast<E384Err>(e384CommLib::ErrorDeviceNotConnected);  \
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

/*! Shape B: (vector<uint16_t> channels, bool applyFlag) -> ErrorCodes_t */
#define E384C_WRAP_CHANNEL_UPDATE(cname, method)                              \
    E384C_API E384Err cname(E384Device* device,                               \
                            const uint16_t* channelIndexes,                   \
                            size_t count,                                     \
                            int32_t applyFlag) {                              \
        E384C_CHECK_DEVICE(device)                                            \
        E384C_GUARD_BEGIN                                                     \
        return e384c::to_c(e384c::md(device)->method(                         \
            e384c::vec_u16(channelIndexes, count),                            \
            applyFlag != 0));                                                 \
        E384C_GUARD_END                                                       \
    }

/*! Shape C: (vector<uint16_t> channels, vector<bool> onValues,
 *  bool applyFlag) -> ErrorCodes_t */
#define E384C_WRAP_CHANNEL_BOOL_CMD(cname, method)                            \
    E384C_API E384Err cname(E384Device* device,                               \
                            const uint16_t* channelIndexes,                   \
                            const uint8_t* onValues,                          \
                            size_t count,                                     \
                            int32_t applyFlag) {                              \
        E384C_CHECK_DEVICE(device)                                            \
        E384C_GUARD_BEGIN                                                     \
        return e384c::to_c(e384c::md(device)->method(                         \
            e384c::vec_u16(channelIndexes, count),                            \
            e384c::vec_bool(onValues, count),                                 \
            applyFlag != 0));                                                 \
        E384C_GUARD_END                                                       \
    }

/*! Shape D0: no-arg action () -> ErrorCodes_t. Device is the only input. */
#define E384C_WRAP_ACTION(cname, method)                                      \
    E384C_API E384Err cname(E384Device* device) {                             \
        E384C_CHECK_DEVICE(device)                                            \
        E384C_GUARD_BEGIN                                                     \
        return e384c::to_c(e384c::md(device)->method());                      \
        E384C_GUARD_END                                                       \
    }

/*! Shape D1: single uint16_t scalar-in (idx) -> ErrorCodes_t. */
#define E384C_WRAP_U16(cname, method)                                         \
    E384C_API E384Err cname(E384Device* device, uint16_t value) {             \
        E384C_CHECK_DEVICE(device)                                            \
        E384C_GUARD_BEGIN                                                     \
        return e384c::to_c(e384c::md(device)->method(value));                 \
        E384C_GUARD_END                                                       \
    }

/*! Shape E, single out-param RangedMeasurement getter. */
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

/*! Shape E, list variant with default-index out-param, two-call protocol.
 *  The underlying C++ getter runs on BOTH the sizing and the fill call;
 *  the shim is stateless by design. Fine for these cold-path getters. */
#define E384C_WRAP_GET_RANGED_LIST(cname, method)                             \
    E384C_API E384Err cname(E384Device* device,                               \
                            E384RangedMeasurement* out,                       \
                            size_t* count,                                    \
                            uint16_t* outDefaultIdx) {                        \
        E384C_CHECK_DEVICE(device)                                            \
        if (count == nullptr) {                                               \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);           \
        }                                                                     \
        E384C_GUARD_BEGIN                                                     \
        std::vector<e384CommLib::RangedMeasurement_t> ranges;                 \
        uint16_t defaultIdx = 0;                                              \
        const auto err = e384c::md(device)->method(ranges, defaultIdx);       \
        if (err == e384CommLib::Success) {                                    \
            e384c::fill_ranged_list(ranges, out, count);                      \
            if (outDefaultIdx != nullptr) {                                   \
                *outDefaultIdx = defaultIdx;                                  \
            }                                                                 \
        }                                                                     \
        return e384c::to_c(err);                                              \
        E384C_GUARD_END                                                       \
    }

#endif /* E384C_INTERNAL_HPP */
