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

/*! ChannelModel/BoardModel getters are not const-qualified in the C++
 *  library, so cm()/bm() strip constness from the borrowed pointer the
 *  same way the library's own (non-const) methods require. */
inline ChannelModel* cm(const E384ChannelModel* c) {
    return const_cast<ChannelModel*>(reinterpret_cast<const ChannelModel*>(c));
}

inline E384ChannelModel* handle(ChannelModel* p) {
    return reinterpret_cast<E384ChannelModel*>(p);
}

inline BoardModel* bm(const E384BoardModel* b) {
    return const_cast<BoardModel*>(reinterpret_cast<const BoardModel*>(b));
}

inline E384BoardModel* handle(BoardModel* p) {
    return reinterpret_cast<E384BoardModel*>(p);
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
 *  PidParams converter         *
 *==============================*/

inline PidParams_t from_c(const E384PidParams& c) {
    PidParams_t p;
    p.proportionalGain   = c.proportionalGain;
    p.integralGain        = c.integralGain;
    p.derivativeGain      = c.derivativeGain;
    p.integralAntiWindUp  = c.integralAntiWindUp;
    return p;
}

/*==============================*
 *  ChannelSources / Comp ctrl  *
 *==============================*/

inline E384ChannelSources to_c(const ChannelSources_t& s) {
    E384ChannelSources c;
    c.voltageFromVoltageClamp             = s.VoltageFromVoltageClamp;
    c.currentFromVoltageClamp             = s.CurrentFromVoltageClamp;
    c.voltageFromCurrentClamp             = s.VoltageFromCurrentClamp;
    c.currentFromCurrentClamp             = s.CurrentFromCurrentClamp;
    c.voltageFromDynamicClamp             = s.VoltageFromDynamicClamp;
    c.currentFromDynamicClamp             = s.CurrentFromDynamicClamp;
    c.voltageFromVoltagePlusDynamicClamp  = s.VoltageFromVoltagePlusDynamicClamp;
    c.currentFromCurrentPlusDynamicClamp  = s.CurrentFromCurrentPlusDynamicClamp;
    return c;
}

inline E384CompensationControl to_c(const CompensationControl_t& ctrl) {
    E384CompensationControl c;
    c.implemented     = ctrl.implemented ? 1 : 0;
    c.min             = ctrl.min;
    c.max             = ctrl.max;
    c.minCompensable  = ctrl.minCompensable;
    c.maxCompensable  = ctrl.maxCompensable;
    c.steps           = ctrl.steps;
    c.step            = ctrl.step;
    c.decimals        = ctrl.decimals;
    c.value           = ctrl.value;
    c.prefix          = static_cast<int32_t>(ctrl.prefix);
    c.unit            = static_cast<int32_t>(unit_to_c(ctrl.unit));
    return c;
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

/*! int32_t array -> vector<EnumT> for the handful of methods that take an
 *  array of enum values (e.g. setAdcCore's ClampingModality_t vector). */
template <typename EnumT>
inline std::vector<EnumT> vec_enum(const int32_t* p, size_t n) {
    std::vector<EnumT> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        v.push_back(static_cast<EnumT>(p[i]));
    }
    return v;
}

/*! vector<EnumT> -> int32_t out array, two-call size/fill protocol (see
 *  fill_ranged_list for the protocol description). */
template <typename EnumT>
inline void fill_enum_list(const std::vector<EnumT>& src, int32_t* out, size_t* count) {
    const size_t available = src.size();
    if (out != nullptr) {
        const size_t n = std::min(*count, available);
        for (size_t i = 0; i < n; ++i) {
            out[i] = static_cast<int32_t>(src[i]);
        }
    }
    *count = available;
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

/*! Same two-call size/fill logic, for a plain std::vector<Measurement_t>. */
inline void fill_meas_list(const std::vector<Measurement_t>& src,
                           E384Measurement* out,
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

/*! Same two-call size/fill logic, for std::vector<uint16_t>/<uint32_t>. */
inline void fill_u16_list(const std::vector<uint16_t>& src, uint16_t* out, size_t* count) {
    const size_t available = src.size();
    if (out != nullptr) {
        const size_t n = std::min(*count, available);
        for (size_t i = 0; i < n; ++i) {
            out[i] = src[i];
        }
    }
    *count = available;
}

inline void fill_channel_handle_list(const std::vector<ChannelModel*>& src,
                                     E384ChannelModel** out, size_t* count) {
    const size_t available = src.size();
    if (out != nullptr) {
        const size_t n = std::min(*count, available);
        for (size_t i = 0; i < n; ++i) {
            out[i] = handle(src[i]);
        }
    }
    *count = available;
}

inline void fill_board_handle_list(const std::vector<BoardModel*>& src,
                                   E384BoardModel** out, size_t* count) {
    const size_t available = src.size();
    if (out != nullptr) {
        const size_t n = std::min(*count, available);
        for (size_t i = 0; i < n; ++i) {
            out[i] = handle(src[i]);
        }
    }
    *count = available;
}

inline void fill_u32_list(const std::vector<uint32_t>& src, uint32_t* out, size_t* count) {
    const size_t available = src.size();
    if (out != nullptr) {
        const size_t n = std::min(*count, available);
        for (size_t i = 0; i < n; ++i) {
            out[i] = src[i];
        }
    }
    *count = available;
}

} // namespace e384c

/*! Opaque-in-C list of device IDs; owns its strings. */
struct E384DeviceList {
    std::vector<std::string> ids;
};

/*! Opaque-in-C owned string; see E384String in e384c.h. */
struct E384String {
    std::string value;
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

/*! (vector<uint16_t> channels, vector<uint16_t> values, bool applyFlag)
 *  -> ErrorCodes_t. Same shape as Shape A but with a uint16_t vector
 *  instead of Measurement_t (e.g. the per-channel setVCCurrentRange
 *  overload). */
#define E384C_WRAP_CHANNEL_U16_CMD(cname, method)                            \
    E384C_API E384Err cname(E384Device* device,                              \
                            const uint16_t* channelIndexes,                  \
                            const uint16_t* values,                          \
                            size_t count,                                    \
                            int32_t applyFlag) {                             \
        E384C_CHECK_DEVICE(device)                                           \
        E384C_GUARD_BEGIN                                                    \
        return e384c::to_c(e384c::md(device)->method(                        \
            e384c::vec_u16(channelIndexes, count),                           \
            e384c::vec_u16(values, count),                                   \
            applyFlag != 0));                                                \
        E384C_GUARD_END                                                      \
    }

/*! vector<uint32_t> out-param getter, two-call protocol, no input. */
#define E384C_WRAP_GET_U32_LIST(cname, method)                                \
    E384C_API E384Err cname(E384Device* device, uint32_t* out, size_t* count) { \
        E384C_CHECK_DEVICE(device)                                           \
        if (count == nullptr) {                                              \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        std::vector<uint32_t> values;                                        \
        const auto err = e384c::md(device)->method(values);                  \
        if (err == e384CommLib::Success) {                                   \
            e384c::fill_u32_list(values, out, count);                        \
        }                                                                    \
        return e384c::to_c(err);                                             \
        E384C_GUARD_END                                                      \
    }

/*! Shape D: single uint16_t scalar-in (idx) + applyFlag -> ErrorCodes_t. */
#define E384C_WRAP_U16_APPLY(cname, method)                                   \
    E384C_API E384Err cname(E384Device* device,                              \
                            uint16_t value,                                  \
                            int32_t applyFlag) {                             \
        E384C_CHECK_DEVICE(device)                                           \
        E384C_GUARD_BEGIN                                                    \
        return e384c::to_c(e384c::md(device)->method(value, applyFlag != 0)); \
        E384C_GUARD_END                                                      \
    }

/*! Single uint32_t scalar-in, no apply flag -> ErrorCodes_t. */
#define E384C_WRAP_U32(cname, method)                                        \
    E384C_API E384Err cname(E384Device* device, uint32_t value) {            \
        E384C_CHECK_DEVICE(device)                                           \
        E384C_GUARD_BEGIN                                                    \
        return e384c::to_c(e384c::md(device)->method(value));                \
        E384C_GUARD_END                                                      \
    }

/*! Two bool scalars in (e.g. resetFlag/onValue + applyFlag) -> ErrorCodes_t. */
#define E384C_WRAP_BOOL2(cname, method)                                      \
    E384C_API E384Err cname(E384Device* device,                              \
                            int32_t value1,                                  \
                            int32_t value2) {                                \
        E384C_CHECK_DEVICE(device)                                           \
        E384C_GUARD_BEGIN                                                    \
        return e384c::to_c(e384c::md(device)->method(value1 != 0, value2 != 0)); \
        E384C_GUARD_END                                                      \
    }

/*! Single bool scalar in, no apply flag -> ErrorCodes_t. */
#define E384C_WRAP_BOOL1(cname, method)                                      \
    E384C_API E384Err cname(E384Device* device, int32_t value) {             \
        E384C_CHECK_DEVICE(device)                                           \
        E384C_GUARD_BEGIN                                                    \
        return e384c::to_c(e384c::md(device)->method(value != 0));           \
        E384C_GUARD_END                                                      \
    }

/*! Single Measurement_t scalar in + bool (applyFlag or enable) -> ErrorCodes_t. */
#define E384C_WRAP_MEAS_BOOL(cname, method)                                  \
    E384C_API E384Err cname(E384Device* device,                              \
                            E384Measurement value,                           \
                            int32_t flag) {                                  \
        E384C_CHECK_DEVICE(device)                                           \
        E384C_GUARD_BEGIN                                                    \
        return e384c::to_c(e384c::md(device)->method(e384c::from_c(value), flag != 0)); \
        E384C_GUARD_END                                                      \
    }

/*! Single uint32_t out-param getter. */
#define E384C_WRAP_GET_U32(cname, method)                                     \
    E384C_API E384Err cname(E384Device* device, uint32_t* out) {             \
        E384C_CHECK_DEVICE(device)                                           \
        if (out == nullptr) {                                                \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        uint32_t value = 0;                                                  \
        const auto err = e384c::md(device)->method(value);                   \
        if (err == e384CommLib::Success) {                                   \
            *out = value;                                                    \
        }                                                                    \
        return e384c::to_c(err);                                             \
        E384C_GUARD_END                                                      \
    }

/*! Single Measurement_t out-param getter. */
#define E384C_WRAP_GET_MEAS(cname, method)                                   \
    E384C_API E384Err cname(E384Device* device, E384Measurement* out) {      \
        E384C_CHECK_DEVICE(device)                                           \
        if (out == nullptr) {                                                \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        e384CommLib::Measurement_t value;                                    \
        const auto err = e384c::md(device)->method(value);                   \
        if (err == e384CommLib::Success) {                                   \
            *out = e384c::to_c(value);                                       \
        }                                                                    \
        return e384c::to_c(err);                                             \
        E384C_GUARD_END                                                      \
    }

/*! vector<Measurement_t> out-param getter, two-call protocol (no input
 *  vector, no default index) -- see E384C_DECL_GET_RANGED_LIST for the
 *  protocol description; identical, just Measurement_t instead of
 *  RangedMeasurement_t and no default-index out-param. */
#define E384C_WRAP_GET_MEAS_LIST(cname, method)                              \
    E384C_API E384Err cname(E384Device* device,                              \
                            E384Measurement* out,                            \
                            size_t* count) {                                 \
        E384C_CHECK_DEVICE(device)                                           \
        if (count == nullptr) {                                              \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        std::vector<e384CommLib::Measurement_t> values;                      \
        const auto err = e384c::md(device)->method(values);                  \
        if (err == e384CommLib::Success) {                                   \
            e384c::fill_meas_list(values, out, count);                       \
        }                                                                    \
        return e384c::to_c(err);                                             \
        E384C_GUARD_END                                                      \
    }

/*! vector<RangedMeasurement_t> out-param getter, two-call protocol, no
 *  default-index out-param (unlike E384C_WRAP_GET_RANGED_LIST). */
#define E384C_WRAP_GET_RANGED_LIST_NODEF(cname, method)                       \
    E384C_API E384Err cname(E384Device* device,                              \
                            E384RangedMeasurement* out,                      \
                            size_t* count) {                                 \
        E384C_CHECK_DEVICE(device)                                           \
        if (count == nullptr) {                                              \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        std::vector<e384CommLib::RangedMeasurement_t> ranges;                \
        const auto err = e384c::md(device)->method(ranges);                  \
        if (err == e384CommLib::Success) {                                   \
            e384c::fill_ranged_list(ranges, out, count);                     \
        }                                                                    \
        return e384c::to_c(err);                                            \
        E384C_GUARD_END                                                      \
    }

/*! RangedMeasurement_t + uint32_t index, both out-params (e.g. getMaxVCCurrentRange). */
#define E384C_WRAP_GET_RANGED_WITH_IDX(cname, method)                        \
    E384C_API E384Err cname(E384Device* device,                              \
                            E384RangedMeasurement* outRange,                 \
                            uint32_t* outIdx) {                              \
        E384C_CHECK_DEVICE(device)                                           \
        if (outRange == nullptr || outIdx == nullptr) {                      \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        e384CommLib::RangedMeasurement_t range;                              \
        uint32_t idx = 0;                                                    \
        const auto err = e384c::md(device)->method(range, idx);              \
        if (err == e384CommLib::Success) {                                   \
            *outRange = e384c::to_c(range);                                  \
            *outIdx = idx;                                                   \
        }                                                                    \
        return e384c::to_c(err);                                             \
        E384C_GUARD_END                                                      \
    }

/*! Capability probe: has_/is_ style methods that use ErrorCodes_t itself as a
 *  boolean success signal (Success = available/true). Per the locked-in
 *  convention, the wrapper never surfaces that raw code: *outResult is set
 *  to 0/1 and E384_SUCCESS is always returned unless the handle/pointer is
 *  invalid or an exception was thrown. */
#define E384C_WRAP_PROBE(cname, method)                                      \
    E384C_API E384Err cname(E384Device* device, int32_t* outResult) {        \
        E384C_CHECK_DEVICE(device)                                           \
        if (outResult == nullptr) {                                          \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        E384C_GUARD_BEGIN                                                    \
        const auto err = e384c::md(device)->method();                       \
        *outResult = (err == e384CommLib::Success) ? 1 : 0;                  \
        return E384_SUCCESS;                                                 \
        E384C_GUARD_END                                                      \
    }

/*! std::string-returning getter with no possible failure (e.g. getDeviceName):
 *  always allocates an E384String and succeeds. */
#define E384C_WRAP_GET_STRING_DIRECT(cname, method)                          \
    E384C_API E384Err cname(E384Device* device, E384String** outStr) {       \
        E384C_CHECK_DEVICE(device)                                           \
        if (outStr == nullptr) {                                             \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        *outStr = nullptr;                                                   \
        E384C_GUARD_BEGIN                                                    \
        auto* str = new E384String;                                          \
        str->value = e384c::md(device)->method();                            \
        *outStr = str;                                                       \
        return E384_SUCCESS;                                                 \
        E384C_GUARD_END                                                      \
    }

/*! ErrorCodes_t-returning getter with a std::string& out-param. */
#define E384C_WRAP_GET_STRING_ERR(cname, method)                             \
    E384C_API E384Err cname(E384Device* device, E384String** outStr) {       \
        E384C_CHECK_DEVICE(device)                                           \
        if (outStr == nullptr) {                                             \
            return static_cast<E384Err>(e384CommLib::ErrorUnknown);          \
        }                                                                    \
        *outStr = nullptr;                                                   \
        E384C_GUARD_BEGIN                                                    \
        std::string value;                                                   \
        const auto err = e384c::md(device)->method(value);                   \
        if (err == e384CommLib::Success) {                                   \
            auto* str = new E384String;                                      \
            str->value = std::move(value);                                   \
            *outStr = str;                                                   \
        }                                                                    \
        return e384c::to_c(err);                                             \
        E384C_GUARD_END                                                      \
    }

#endif /* E384C_INTERNAL_HPP */
