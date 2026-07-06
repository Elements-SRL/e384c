# e384c — C shim for e384CommLib

Reference notes for the project: what it is, how it's built, the conventions
locked in so far, and what's left to port. Written so a future session (or
another person) can pick this up without re-deriving the design. This file is
meant to be a complete reference — if something isn't here, check `e384c.h`
directly (it's the only file bindgen consumes and is kept exhaustively
documented inline), but the intent is that you shouldn't need to.

## 1. Purpose

`e384commLib` is a C++ library for talking to electrophysiology hardware.
The goal is a Rust wrapper via `bindgen`. Rather than porting the C++ library
itself, we write a thin `extern "C"` **shim** (`e384c`) that wraps it. Only
the shim's header is C-clean; the C++ library and its types never leak
across the boundary.

## 2. File layout

```
e384c/
├── CMakeLists.txt
├── include/
│   └── e384c.h              ← pure C. The ONLY file bindgen consumes.
├── src/
│   ├── e384c_internal.hpp   ← C++-only converters + wrapper macros.
│   │                          Never installed, never seen by bindgen.
│   └── e384c_shim.cpp       ← extern "C" implementations, one line per
│                               method once a macro shape exists.
└── PROJECT_NOTES.md         ← this file (repo root, not docs/)
```

`e384c.h` is guarded with `extern "C" { ... }` under `#ifdef __cplusplus`, so
it compiles unchanged as C99/C11 or as C++.

## 3. Build

- `e384c` is a **CMake project**, C++17, built inside Qt Creator
  (`Desktop_Qt_6_7_3_MSVC2022_64bit-Debug` kit in this setup).
- `e384commLib` is **always built as a static library**. It's linked
  `PRIVATE` into `e384c`:
  ```cmake
  target_link_libraries(e384c PRIVATE e384commlib::e384commlib)
  ```
  `PRIVATE` matters: it keeps e384commlib's headers/symbols out of e384c's
  public interface. Consumers only ever see `e384c`.
- `e384c` itself is currently built as a **SHARED** library (a DLL on
  Windows) — decided deliberately; see §6.
- `find_package(e384commlib REQUIRED CONFIG)` locates it via
  `CMAKE_PREFIX_PATH`, populated from the `E384COMMLIB_PATH` environment
  variable. In this environment it's `C:\ElemLibraries\e384CommLib\`, and
  the real headers used for reading exact signatures live under
  `C:\ElemLibraries\e384CommLib\include\` (`messagedispatcher.h`,
  `e384commlib_global.h`, `e384commlib_global_addendum.h`,
  `e384commlib_errorcodes.h`, `model/channelmodel.h`, `model/boardmodel.h`).
- To verify the imported target is really static (not accidentally a DLL):
  ```cmake
  get_target_property(_type e384commlib::e384commlib TYPE)
  message(STATUS "e384commlib target type: ${_type}")  # want STATIC_LIBRARY
  ```

### Rebuilding after adding shim functions
Always **clean + rebuild** `e384c` after adding functions to
`e384c_shim.cpp` — an incremental/stale build has silently produced a
`.lib` missing new symbols before (`LNK2019: unresolved external symbol`
on a function that *is* in the source). Confirm with:
```
dumpbin /symbols e384c.lib | findstr <new_function_name>
```
Then, on the Rust side, `cargo clean` if `build.rs` might be caching a
reference to the old `.lib`.

### Building outside Qt Creator (command line / this agent environment)
`cmake`/`ninja`/`cl.exe` are **not on PATH** by default in this environment
even though Qt Creator's kit has them configured — they were only ever
invoked through Qt Creator's own environment setup. To build headlessly:

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && set" > "$env:TEMP\vcvars_env.txt"
Get-Content "$env:TEMP\vcvars_env.txt" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:PATH = "C:\Qt\Tools\CMake_64\bin;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
$env:E384COMMLIB_PATH = "C:\ElemLibraries\e384CommLib\"
Set-Location "C:\Users\lross\development\e384c\build\Desktop_Qt_6_7_3_MSVC2022_64bit-Debug"
ninja -t clean   # for a clean rebuild; omit for incremental
ninja -j4
```

Key paths discovered this session:
- `cmake.exe`: `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- `ninja.exe`: bundled with VS at
  `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- `vcvars64.bat`: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`
  — running it directly with `cmd /c "... && set"` and capturing the output
  is the way to get `INCLUDE`/`LIB`/`PATH` into a PowerShell session's
  process environment (PowerShell has no native equivalent of sourcing a
  `.bat`). Without this, `cl.exe` fails with
  `fatal error C1083: Cannot open include file: 'string'` because it can't
  find the standard library headers.
- The existing Qt-Creator-generated `build/Desktop_Qt_6_7_3_MSVC2022_64bit-Debug`
  directory already has a valid `CMakeCache.txt`/`build.ninja` — no need to
  re-run `cmake` from scratch, `ninja` alone (re-)configures if needed.

## 4. Core design decisions

| Decision | Rationale |
|---|---|
| Opaque handle = `MessageDispatcher*` itself (`reinterpret_cast` to `E384Device*`) | No wrapper allocation. Virtual dispatch on the concrete device subclass works automatically. |
| `E384Err` = raw `uint32_t`, values passed through unmodified from `ErrorCodes_t` | Values are stable/explicit (see `e384commlib_errorcodes.h`); no remapping needed. `0xFFFFFFFF` (`ErrorUnknown`) requires unsigned, not signed. |
| `E384_WARNING_VALUE_CLIPPED` (`0x8000000B`) is success, not failure | It has the high bit set but the operation *did* apply (clipped). Every caller must check `err == E384_SUCCESS \|\| err == E384_WARNING_VALUE_CLIPPED`, not just `err == 0`. |
| `Measurement_t` / `RangedMeasurement_t` → POD mirror structs (`E384Measurement`, `E384RangedMeasurement`) | The C++ originals hold a `std::string unit`, so they aren't POD. Mirrors use `int32_t unit` (see next row) instead of a string, making them genuinely `Copy`-safe in Rust. |
| Units are an **enum** (`E384Unit`: `NONE/V/A/HZ/S/C`, plus output-only `UNKNOWN = -1`), not a `char[N]` buffer | Confirmed by grepping the actual unit strings used across the codebase — only `V`, `A`, `Hz`, `s`, `C` ever appear. An enum is smaller, exhaustively matchable in Rust, and avoids any embedded-string / truncation questions entirely. `UNKNOWN` exists only as an output sentinel if the library ever surprises us; never construct it as an input. |
| `RxOutput_t` is already pure POD in the C++ library, mirrored 1:1 as `E384RxOutput` | Field-for-field identical, no conversion logic needed. |
| `ChannelSources_t` also POD, mirrored 1:1 as `E384ChannelSources` | An earlier version of this doc said "no mirror needed" for this type — that was a documentation gap, not a real design choice: bindgen still needs a POD struct *declared in the C header*, since `e384c.h` can never include the C++ header that defines `ChannelSources_t`. The mirror has no conversion logic (straight field copy) but it does exist, as `E384ChannelSources`. |
| `PidParams_t` mirrored 1:1 as `E384PidParams` | Already all-`double` POD in C++; mirrored purely so `e384c.h` doesn't need to include the C++ header, same reasoning as `ChannelSources_t`. |
| `CompensationControl_t` mirrored as `E384CompensationControl`, **dropping the `name` field** | The C++ struct has `std::string name`/`std::string unit`, so isn't POD. `unit`/`prefix` map through the same `E384Unit`/`E384UnitPfx` enum machinery as `Measurement_t`. `name` is dropped entirely rather than given a string-out mechanism: the caller already supplies the identifying `CompensationUserParams_t` enum value to get the control in the first place, so the human-readable name is redundant across this boundary. |
| `CalibrationParams_t` — deferred, **out of scope for this session by explicit instruction** | 4-level nested tree + `unordered_map`. Touches only `getCalibParams`/`setCalibParams`. When ported: **opaque handle + accessor functions**, never flatten the tree into a C struct. |
| Data acquisition buffer: **caller-provided, library-allocated, fixed-size** | Sized via `e384_getRxDataBufferSize` (backed by a fixed circular buffer), allocated once via `e384_allocateRxDataBuffer`, reused across every `e384_getNextMessage` call, released with `e384_deallocateRxDataBuffer`. No two-call protocol here — the size is knowable up front and this is the hot path. |
| Cold-path list getters (ranges, device lists, measurement lists, enum lists, model-handle lists, flattened matrices): **two-call size/fill protocol** | Call once with `out = NULL` → `*count` (or `*rows`/`*cols`) receives available size. Allocate. Call again with a real buffer → fills `min(capacity, available)`, size out-param still reports what's available (so a caller can detect "buffer too small, retry"). Chosen over caller-guesses-capacity because sizes aren't knowable in advance and this is called rarely (setup time). Note: the shim is stateless, so the underlying C++ getter runs on *both* calls — fine for cold-path methods. This protocol is now used, with the same two-call shape, for: `RangedMeasurement_t` lists (with and without a default index), `Measurement_t` lists, `uint32_t` lists, enum lists (as `int32_t`), `ChannelModel*`/`BoardModel*` handle lists, and flattened `vector<vector<T>>` matrices (which report `(rows, cols)` instead of a single count). |
| Device string lists (`detectDevices`, `listAllDevices`, and now reused generically): **opaque `E384DeviceList*` handle** | Queried with `_count`/`_get`, released with `_free`. Avoids the "who owns/frees each `char*`" question that a raw `char**` out-param would raise. Borrowed pointers from `_get` are valid only until `_free`. **This session generalized `E384DeviceList` beyond device discovery**: it's now the return type for every plain `vector<std::string>` getter (`getCalibFileNames`, the names list in `getTemperatureChannelsFeatures`/`getCustomFlags`/`getCustomDoubles`, the options list in `getCompOptionsFeatures`). No new type was introduced for this — same struct, same `_count`/`_get`/`_free` functions, just documented as dual-purpose in `e384c.h`. |
| Single `std::string`-returning getters: **new opaque `E384String*` handle** | `e384_string_get`/`e384_string_free`, identical borrow contract to `E384DeviceList` (copy out before freeing). Covers `getDeviceName`, `getDeviceSerial` (both direct `std::string`-returning, no `ErrorCodes_t` — always succeed, wrapped with `E384C_WRAP_GET_STRING_DIRECT`), and `getSerialNumber`, `getCalibMappingFileDir`, `getCalibMappingFilePath` (all `ErrorCodes_t`-returning with a `std::string&` out-param, wrapped with `E384C_WRAP_GET_STRING_ERR`). |
| Capability probes (`has*`/`is*`, plus a couple of not-`has`/`is`-named ones that behave identically): **`E384Err cname(device, int32_t* outResult)`**, never a raw `bool` return | The underlying C++ methods report availability by returning `ErrorCodes_t` itself as the boolean signal (`Success` = available/true). The wrapper never surfaces that raw code as the function's return value — `*outResult` is set to 0/1, and the C function's own `E384Err` return is always `E384_SUCCESS` unless the device/pointer is NULL or an exception was thrown. Applied to 16 genuinely `has*`/`is*`-named methods, to `getCalibrationStatus` (same "no out-param, only success/fail matters" shape despite the name), and — with an added `int32_t` enum-in parameter — to `hasCompFeature(CompensationUserParams_t)`. |
| Enum values crossing the boundary (as parameters or out-values): always **`int32_t`**, `static_cast` on the C++ side | No per-enum C mirror type was created (`ClampingModality_t`, `MsgTypeId_t`, `CompensationTypes_t`, `CompensationUserParams_t`, `OffsetRecalibStatus_t`, `LiquidJunctionStatus_t` — none of these get an `E384*` enum). Keeps `e384c.h` from needing one enum definition per C++ enum; callers are expected to know the numeric values (they're stable, `typedef enum`s with no gaps) or a higher-level Rust wrapper defines its own enum matching the values. |
| Jagged `vector<vector<T>>` getters (`getCalibFilesFlags` → `bool`, `getCompValueMatrix` → `double`): **flattened to a row-major buffer**, two-call protocol reporting `(rows, maxCols)` | Short rows (if any) are padded with a zero value (`false`/`0.0`) up to `maxCols`, computed at runtime from the actual C++ vectors — no assumption about a fixed enum-derived width. `getCustomOptions` (`vector<string>` + **ragged** `vector<vector<string>>` + `vector<uint16_t>`) was **NOT** given this treatment and is deferred — see §8/§9, its raggedness is in string counts per control, not just numeric width, which is a different (and harder) problem. |
| `ChannelModel*`/`BoardModel*` → **new opaque, borrowed handles** `E384ChannelModel`/`E384BoardModel` | `reinterpret_cast` wrappers, identical no-allocation pattern to `E384Device`/`MessageDispatcher*`. Returned via the two-call protocol as arrays of handles (not value structs). **Borrowed, not owned**: the underlying `vector<ChannelModel*>`/`vector<BoardModel*>` lives inside `MessageDispatcher` itself (`channelModels`/`boardModels` fields, populated by `fillChannelList`/`fillBoardList` at connect time), so there is **no `_free` function** — a handle is valid only for the owning `E384Device`'s lifetime and must not be used after `e384_disconnect`. `ChannelModel`/`BoardModel`'s own getters/setters are NOT `ErrorCodes_t`-returning (they're plain C++ getters/setters on a data-holder class), so the wrappers mirror that: they return the value directly (or `void` for setters), with a NULL handle yielding a zero-valued default rather than an error code (there's no error channel to report one through). `BoardModel::setChannelsOnBoard(vector<ChannelModel*>)` is the one method in this family intentionally NOT wrapped — it would require constructing owned `ChannelModel*` instances from the Rust side, which the borrowed-handle model doesn't support, and it's internal wiring set up by the library itself at connect time with no legitimate external caller. |
| Overload disambiguation: **parameter-shape suffixes**, chosen per-overload-set rather than one global scheme | See §4a below for the full mapping of all 15 originally-overloaded C++ method names. |
| `e384commLib` linked statically **into** `e384c.dll`; CRT models must match | See §6. |

### 4a. The 15 overloaded C++ method names — full mapping

C has no overloading; every one of these needed a distinct C name. Found by
grepping `messagedispatcher.h` for every `ErrorCodes_t <name>(...)` and
counting duplicates — exactly 15 names have more than one overload (14 with
2, one — `getChannelNumberFeatures` — with 3), matching this doc's original
"~15 overloaded methods" estimate. Table below is the complete, final,
implemented mapping (all of it is in `e384c.h` now, no longer just a
proposal):

| C++ name | Overload signatures | C names |
|---|---|---|
| `getChannelNumberFeatures` | `(uint16_t&,uint16_t&)` / `(int&,int&)` / `(int&,int&,int&)` | `e384_getChannelNumberFeatures_u16` / `_int` / `_intGp` |
| `setVCCurrentRange` | `(uint16_t,bool)` / `(vector<u16>,vector<u16>,bool)` | `e384_setVCCurrentRange_all` (Shape D) / `e384_setVCCurrentRange_perChannel` |
| `setCCVoltageRange` | `(uint16_t,bool)` / `(vector<u16>,vector<u16>,bool)` | `e384_setCCVoltageRange_all` (Shape D) / `e384_setCCVoltageRange_perChannel` |
| `setClampingModality` | `(uint32_t idx,bool,bool)` / `(ClampingModality_t,bool,bool=true)` | `e384_setClampingModality_byIdx` / `e384_setClampingModality_byEnum` |
| `convertVoltageValue` | `(int16_t,double&)` / `(int16_t,uint16_t chIdx,double&)` | `e384_convertVoltageValue` / `e384_convertVoltageValue_byChannel` |
| `convertCurrentValue` | `(int16_t,double&)` / `(int16_t,uint16_t chIdx,double&)` | `e384_convertCurrentValue` / `e384_convertCurrentValue_byChannel` |
| `getDeviceInfo` | static `(string,uint&×5)` / instance `(uint&×5)` | `e384_getDeviceInfoForId` (static, no device handle) / `e384_getDeviceInfo` (instance) |
| `getVCCurrentRange` | `(RangedMeasurement_t&)` / `(vector<Ranged>&)` | `e384_getVCCurrentRange` (pre-existing, Shape E single) / `e384_getVCCurrentRange_list` |
| `getCCVoltageRange` | `(RangedMeasurement_t&)` / `(vector<Ranged>&)` | `e384_getCCVoltageRange` / `e384_getCCVoltageRange_list` |
| `getVoltageRange` | `(RangedMeasurement_t&)` / `(vector<Ranged>&)` | `e384_getVoltageRange` / `e384_getVoltageRange_list` |
| `getCurrentRange` | `(RangedMeasurement_t&)` / `(vector<Ranged>&)` | `e384_getCurrentRange` / `e384_getCurrentRange_list` |
| `getVCCurrentRangeIdx` | `(uint32_t&)` / `(vector<uint32_t>&)` | `e384_getVCCurrentRangeIdx` / `e384_getVCCurrentRangeIdx_list` |
| `getCCVoltageRangeIdx` | `(uint32_t&)` / `(vector<uint32_t>&)` | `e384_getCCVoltageRangeIdx` / `e384_getCCVoltageRangeIdx_list` |
| `getVCCurrentRanges` | `(vector<Ranged>&,uint16_t&)` deprecated / `(vector<Ranged>&,vector<uint16_t>&)` | `e384_getVCCurrentRanges` (pre-existing, Shape E list — kept on the deprecated overload) / `e384_getVCCurrentRanges_perChannel` (two independent two-call outputs: ranges list and per-channel default-index list are not necessarily the same length) |
| `getBoardsNumberFeatures` | `(uint16_t&)` / `(int&)` | `e384_getBoardsNumberFeatures_u16` / `e384_getBoardsNumberFeatures_int` |

## 5. Wrapper macro shapes (in `e384c_internal.hpp`)

Each macro takes `(cname, method)` and expands to a full `extern "C"`
function body. Adding a new method that fits an existing shape is a single
line in `e384c_shim.cpp` plus a declaration in `e384c.h` (usually via the
matching `E384C_DECL_*` macro).

| Macro | C++ signature shape | Used for |
|---|---|---|
| `E384C_WRAP_CHANNEL_MEAS_CMD` | `(vector<uint16_t>, vector<Measurement_t>, bool) -> ErrorCodes_t` | `set*HoldTuner`, `set*Half`, `setGateVoltages`, `setCalib*Gain/Offset` (17 methods) |
| `E384C_WRAP_CHANNEL_UPDATE` | `(vector<uint16_t>, bool) -> ErrorCodes_t` | `updateCalib*`, `reset*` (12 methods) |
| `E384C_WRAP_CHANNEL_BOOL_CMD` | `(vector<uint16_t>, vector<bool>, bool) -> ErrorCodes_t` | `enableStimulus`, `turn*On`, `*Compensation` (9 methods) |
| `E384C_WRAP_CHANNEL_U16_CMD` | `(vector<uint16_t>, vector<uint16_t>, bool) -> ErrorCodes_t` | Same shape as Shape A but a `uint16_t` vector instead of `Measurement_t`: `setVCCurrentRange_perChannel`, `setCCVoltageRange_perChannel` (2 methods, part of the overload table in §4a) |
| `E384C_WRAP_GET_RANGED` | `(RangedMeasurement_t&) -> ErrorCodes_t` | Single ranged-measurement out-param getters: `getVCCurrentRange`, `getGateVoltagesFeatures`, `getSourceVoltagesFeatures`, `getVCVoltageRange`, `getLiquidJunctionRange`, `getCCCurrentRange`, `getOnTimeFeatures`, `getTimeProtocolRangeFeature`, `getFrequencyProtocolRangeFeature`, `getZapFeatures`, `getCoolingFansSpeedRange`, plus (from the overload table) `getCCVoltageRange`, `getVoltageRange`, `getCurrentRange` (13 methods total) |
| `E384C_WRAP_GET_RANGED_LIST` | `(vector<RangedMeasurement_t>&, uint16_t& defaultIdx) -> ErrorCodes_t`, two-call protocol, WITH default index | `get*Ranges` (4 methods: `getVCCurrentRanges`, `getVCVoltageRanges`, `getCCCurrentRanges`, `getCCVoltageRanges`) |
| `E384C_WRAP_GET_RANGED_LIST_NODEF` | `(vector<RangedMeasurement_t>&) -> ErrorCodes_t`, two-call protocol, NO default index | `getVoltageHoldTunerFeatures`, `getVoltageHalfFeatures`, `getCurrentHoldTunerFeatures`, `getCurrentHalfFeatures`, `getLiquidJunctionRangesFeatures`, plus (from the overload table) `getVCCurrentRange_list`, `getCCVoltageRange_list`, `getVoltageRange_list`, `getCurrentRange_list` (9 methods total) |
| `E384C_WRAP_GET_RANGED_WITH_IDX` | `(RangedMeasurement_t&, uint32_t&) -> ErrorCodes_t` | `getMax/MinVCCurrentRange`, `getMax/MinVCVoltageRange`, `getMax/MinCCCurrentRange`, `getMax/MinCCVoltageRange` (8 methods) |
| `E384C_WRAP_ACTION` | `() -> ErrorCodes_t` | `sendCommands`, `startProtocol`, `stopProtocol`, `startStateArray`, `okMoveCalibration*`, `okReadCalibrationRam` |
| `E384C_WRAP_U16` | `(uint16_t) -> ErrorCodes_t` | `okSelectCalibrationRam` |
| `E384C_WRAP_U16_APPLY` | `(uint16_t, bool applyFlag) -> ErrorCodes_t` (Shape D) | `setVCCurrentRange_all`, `setVCVoltageRange`, `setCCCurrentRange`, `setCCVoltageRange_all`, `setVoltageStimulusLpf`, `setCurrentStimulusLpf`, `setSourceForVoltageChannel`, `setSourceForCurrentChannel`, `setSamplingRate` (9 methods) |
| `E384C_WRAP_U32` | `(uint32_t) -> ErrorCodes_t` | `setDownsamplingRatio` |
| `E384C_WRAP_BOOL2` | `(bool, bool) -> ErrorCodes_t` | `resetAsic`, `resetFpga`, `turnVoltageReaderOn`, `turnCurrentReaderOn`, `turnVoltageStimulusOn`, `turnCurrentStimulusOn`, `enableVcCompensations`, `enableCcCompensations` (8 methods) |
| `E384C_WRAP_BOOL1` | `(bool) -> ErrorCodes_t` | `subtractLiquidJunctionFromCc`, `setCalibrationMode` (2 methods) |
| `E384C_WRAP_MEAS_BOOL` | `(Measurement_t, bool) -> ErrorCodes_t` | `setVoltageReference`, `setCoolingFansSpeed`, `setTemperatureControl` (3 methods) |
| `E384C_WRAP_GET_U32` | `(uint32_t&) -> ErrorCodes_t` | 12 index/count getters: `getClampingModalityIdx`, `getVCVoltageRangeIdx`, `getCCCurrentRangeIdx`, `getSamplingRateIdx`, `getMaxDownsamplingRatioFeature`, `getDownsamplingRatio`, `get{VC,CC}{Voltage,Current}FilterIdx` (4), `getMaxProtocolItemsFeature`, `getCalibrationEepromSize`, plus (from overload table) `getVCCurrentRangeIdx`, `getCCVoltageRangeIdx` |
| `E384C_WRAP_GET_U32_LIST` | `(vector<uint32_t>&) -> ErrorCodes_t`, two-call protocol | Overload-table list variants: `getVCCurrentRangeIdx_list`, `getCCVoltageRangeIdx_list` (2 methods) |
| `E384C_WRAP_GET_MEAS` | `(Measurement_t&) -> ErrorCodes_t` | `getSamplingRate`, `get{VC,CC}{Voltage,Current}Filter` (4) — 5 methods total |
| `E384C_WRAP_GET_MEAS_LIST` | `(vector<Measurement_t>&) -> ErrorCodes_t`, two-call protocol | `getSamplingRatesFeatures`, `getRealSamplingRatesFeatures`, `get{VC,CC}{Voltage,Current}Filters` (4), `getVoltageHoldTuner` (7 methods) |
| `E384C_WRAP_PROBE` | `() -> ErrorCodes_t` from C++, exposed as `(int32_t* outResult) -> E384Err` | 16 `has*`/`is*` methods + `getCalibrationStatus` (17 methods) — see the probe-convention row in §4 |
| `E384C_WRAP_GET_STRING_DIRECT` | `() -> std::string` (no error path) | `getDeviceName`, `getDeviceSerial` (2 methods) |
| `E384C_WRAP_GET_STRING_ERR` | `(std::string&) -> ErrorCodes_t` | `getSerialNumber`, `getCalibMappingFileDir`, `getCalibMappingFilePath` (3 methods) |

Shared helpers used by every macro:
- `E384C_CHECK_DEVICE(device)` — null-handle guard, returns `ErrorDeviceNotConnected`.
- `E384C_GUARD_BEGIN` / `E384C_GUARD_END` — the try/catch-all wrapper.
- `e384c::vec_u16`, `e384c::vec_meas` — array→vector marshaling for inputs.
- `e384c::vec_bool` — **must** convert element-by-element (`vector<bool>` is
  bit-packed in C++; a `reinterpret_cast` of a `uint8_t*` would be UB).
- `e384c::vec_enum<EnumT>` — `int32_t` array → `vector<EnumT>`, for the
  handful of methods taking an array of enum values (e.g. `setAdcCore`'s
  `ClampingModality_t` vector).
- `e384c::to_c` / `e384c::from_c` (overloaded) — `Measurement_t` /
  `RangedMeasurement_t` / `PidParams_t` (from_c only, one-directional since
  it's input-only) / `ChannelSources_t` / `CompensationControl_t` ↔ their
  POD mirrors.
- `e384c::unit_to_c` / `e384c::unit_from_c` — `std::string` ↔ `E384Unit`.
  `unit_from_c` maps anything unrecognized to `""`; only relevant when
  reconstructing a C++ object from a C struct carrying `E384_UNIT_UNKNOWN`,
  which should never happen for values that originated inside the library.
- `e384c::fill_ranged_list` / `fill_meas_list` / `fill_u16_list` /
  `fill_u32_list` / `fill_enum_list<EnumT>` / `fill_channel_handle_list` /
  `fill_board_handle_list` — implement the two-call size/fill logic once per
  element type, shared by every corresponding `E384C_WRAP_GET_*_LIST`
  instantiation.
- `flatten_matrix<T>` (in `e384c_shim.cpp`, anonymous namespace, NOT in
  `e384c_internal.hpp` since it's only used by the two matrix getters) —
  shared row-major flatten-with-padding logic for `getCompValueMatrix`.
  `getCalibFilesFlags` hand-inlines the equivalent logic instead, because its
  source is `vector<vector<bool>>` (bit-packed) flattening into `uint8_t*`,
  a different element-type pairing than the generic template handles.
- `e384c::md(device)` / `e384c::handle(MessageDispatcher*)` — `E384Device*` ↔ `MessageDispatcher*`.
- `e384c::cm(chHandle)` / `e384c::handle(ChannelModel*)` — `E384ChannelModel*` ↔ `ChannelModel*`. Strips `const` because `ChannelModel`'s own getters aren't const-qualified in the C++ library.
- `e384c::bm(boardHandle)` / `e384c::handle(BoardModel*)` — `E384BoardModel*` ↔ `BoardModel*`, same const-stripping reasoning.

### Adding a new function that fits an existing shape
1. Add a declaration to `e384c.h` (or reuse an existing `E384C_DECL_*` macro
   if one exists for that shape).
2. Add one line to `e384c_shim.cpp`: `E384C_WRAP_<SHAPE>(e384_cname, cppMethodName)`.
3. Clean-rebuild `e384c` (see §3's headless build recipe if not working inside Qt Creator).

### Adding a function with a genuinely new shape
Hand-write it in `e384c_shim.cpp` following the pattern of
`e384_okWriteCalibrationRam` (two mixed scalars), the data-acquisition
functions (raw pointers, no vector marshaling), or any of the protocol
builders / `convert*Values` functions / complex multi-output getters (see
§8) for progressively more involved examples. If the shape looks like
it'll recur, promote it to a macro afterward — this is exactly how all ~15
macro shapes beyond the original 7 came to exist.

## 6. Windows-specific gotchas encountered so far

- **`STATUS_DLL_NOT_FOUND` (`0xc0000135`)** at process start, before `main`
  runs, means the loader can't resolve a DLL somewhere in the static import
  chain (`exe → e384c.dll → e384commlib's deps → ...`). Diagnose with
  [Dependencies](https://github.com/lucasg/Dependencies)
  (`Dependencies.exe -chain target\debug\your.exe`) rather than guessing.
- **Debug builds pull in the debug CRT** (`msvcp140d.dll`,
  `vcruntime140d.dll`, `ucrtbased.dll`), which is not redistributable and
  isn't on `PATH` outside a Visual Studio environment/Developer shell. This
  is why the exact same shim can fail in Debug and work in Release. Fix
  (chosen here): pin `e384c`'s `MSVC_RUNTIME_LIBRARY` to the **non-debug**
  DLL CRT even in Debug configs, since the shim is consumed across a C ABI
  from Rust and there's little reason to need the C++ debug CRT there.
- **CRT model must match between `e384c` and `e384commlib`** now that
  e384commlib is linked in statically — its objects are compiled into
  `e384c.dll`, so if e384commlib used `/MT` and `e384c` defaults to `/MD`,
  expect `LNK4098` warnings or worse (mismatched heaps for anything that
  allocates, though the `Measurement_t` converters mostly avoid
  cross-heap frees by construction — the `std::string` never crosses the
  boundary). Check e384commlib's flag via `dumpbin /directives` on its
  `.lib` (`/DEFAULTLIB:LIBCMT` = static, `/DEFAULTLIB:MSVCRT` = dynamic) and
  match `e384c`'s `MSVC_RUNTIME_LIBRARY` to it.
- Because e384commlib is folded into `e384c.dll`, **Rust's own CRT model is
  irrelevant** here — Rust only ever sees `e384c.dll` across a clean C ABI,
  no C++ objects cross into Rust directly.
- The FTDI SDK is often only available as `ftd2xx.lib` (import stub) +
  `ftd2xx.dll` (loaded at runtime) — if so, that DLL still needs to be
  discoverable at runtime even after everything else is static. Check for
  a `ftd2xx_static.lib` in the SDK if you want to eliminate this too.
- **A literal `*/` substring anywhere inside a `/*! ... */` doc comment
  prematurely closes the comment**, even mid-word. Hit twice this session
  writing comments like `has*/is*` (meant as "has-star, is-star", i.e.
  glob-style shorthand for the method-name prefixes) and
  `ChannelModel*/BoardModel*` (meant as "ChannelModel-pointer or
  BoardModel-pointer"). Both produced a cascade of confusing downstream
  syntax errors (`missing ';' before '*'`, `C4430: missing type specifier`,
  `redefinition` errors on totally unrelated code later in the file) because
  the compiler kept parsing "commented-out" code as real code from that
  point on. Fix: never write `X*/Y` in a doc comment — write `X* / Y*`,
  `X_` style prefixes, or rephrase entirely (`ChannelModel* or BoardModel*`).
  If you see a wall of unrelated-looking syntax errors starting at some
  line number, suspect this first and grep for `'\*/[A-Za-z]'` across the
  changed files before investigating anything else.
- **`cmake`/`ninja`/`cl.exe` are not on PATH** in this environment even
  though a working Qt-Creator-configured build directory exists — see the
  headless build recipe in §3. The symptom without it: `cmake`/`ninja` not
  found at all (not on PATH), or if only `vcvars` was skipped, `cl.exe`
  fails to find `<string>` and other standard headers.
- **Templates cannot have C linkage.** A `template <typename T> ...` helper
  function must live in an anonymous namespace *before* the `extern "C" {`
  block in `e384c_shim.cpp`, never inside it — `error C2894: templates
  cannot be declared to have 'C' linkage` if placed inside. This bit the
  `flatten_matrix<T>` helper once; the existing `discover<Fn>` template
  helper (used by `detectDevices`/`listAllDevices`) was already correctly
  placed outside the `extern "C"` block, which is why it didn't have this
  problem in the pre-existing code.

## 7. Rust-side notes

- `bindgen` does **not** emit plain `#define NAME value` macros as Rust
  items by default (it doesn't know the type). Either:
  - hardcode the handful you need as local `const`s in Rust (done for
    `E384_SUCCESS` / `E384_WARNING_VALUE_CLIPPED` in the example), or
  - set `.default_macro_constant_type(bindgen::MacroTypeVariation::Signed)`
    in `build.rs` so bindgen emits them automatically.
- Success/warning check pattern used everywhere:
  ```rust
  fn check(err: E384Err) -> Result<(), E384Err> {
      if err == E384_SUCCESS || err == E384_WARNING_VALUE_CLIPPED {
          Ok(())
      } else {
          Err(err)
      }
  }
  ```
- Two-call protocol from Rust: sizing call with a null/zeroed out-pointer,
  allocate a `Vec` of the reported length, fill call with
  `vec.as_mut_ptr()`. See `e384_getVCCurrentRanges` usage in the example
  program. The same pattern now applies to every `*_list`/`*Features`/
  `getChannels`/`getBoards`-style getter, and to the two flattened-matrix
  getters (`getCalibFilesFlags`/`getCompValueMatrix`) except there you get
  back **two** size out-params (`rows`, `cols`) instead of one, and index
  the filled buffer as `buf[row * cols + col]`.
- **Capability probes are NOT `bool`-returning functions.** Every
  `has*`/`is*`-shaped function (plus `getCalibrationStatus`,
  `hasCompFeature`) has signature `E384Err cname(device, int32_t* outResult)`.
  Check the `E384Err` return for `E384_SUCCESS` first (as always), THEN read
  `*outResult` (0 or 1) for the actual answer — the `E384Err` return here
  reflects only "did the probe itself run cleanly", not "is the feature
  available".
- `E384DeviceList` strings and `E384String` contents: copy out to owned
  `String`s (via `CStr::from_ptr(...).to_string_lossy().into_owned()`)
  **before** calling `e384_deviceList_free` / `e384_string_free` — the
  borrowed pointers from `_get` (or from `e384_string_get`) are invalidated
  by the free. `E384DeviceList` is reused for several non-device-discovery
  getters now (see §4) — same contract every time.
- `E384ChannelModel*`/`E384BoardModel*` handles from `e384_getChannels`,
  `e384_getBoards`, etc. are **borrowed, never freed** — there is no
  `_free` function for either type. They stay valid only as long as the
  owning `E384Device` handle is connected; do not retain one across an
  `e384_disconnect` call, and do not call any accessor on one after that
  point.
- Always disconnect on every exit path, including error paths — a
  reasonable pattern is running the fallible work in a closure and calling
  `e384_disconnect` unconditionally afterward, then returning the
  closure's result.

## 8. Status

### Coverage summary
- `messagedispatcher.h` has **253 public method signatures** (counting every
  overload separately, plus the 2 `std::string`-returning getters that don't
  show up in a plain `ErrorCodes_t` grep).
- **246 of those 253 are now wrapped** (~97%). The 7 that aren't are listed
  under "Deferred / out of scope" below, each deliberately.
- Additionally, **36 `ChannelModel`/`BoardModel` methods** are wrapped via
  the new opaque-handle family (Proposal B) — these are outside the 253
  count above since they belong to different classes entirely.

### Done, by category
- **Device discovery**: `detectDevices`, `listAllDevices`, device-list accessors.
- **Connection lifecycle**: `connect`, `disconnect`.
- **Data acquisition**: buffer size/alloc/dealloc, `getNextMessage`, `purgeData`.
- **Shape A** (17): all `set*HoldTuner/Half`, gate/source voltages, all `setCalib*Gain/Offset`.
- **Shape B** (12): all `updateCalib*`, both channel resets.
- **Shape C** (9): stimulus/switch/compensation enable commands.
- **Shape D** (9): `setVCCurrentRange`/`setVCVoltageRange`/`setCCCurrentRange`/`setCCVoltageRange` (scalar overloads), stimulus LPFs, source-for-channel selectors, `setSamplingRate`.
- **Shape E** (13 via `E384C_WRAP_GET_RANGED` + 4 via `E384C_WRAP_GET_RANGED_LIST` + 9 via `E384C_WRAP_GET_RANGED_LIST_NODEF` + 8 via `E384C_WRAP_GET_RANGED_WITH_IDX`): all range/feature getters shaped around `RangedMeasurement_t`.
- **`E384C_WRAP_CHANNEL_U16_CMD`** (2): the per-channel range-index overloads.
- **Small scalar/bool commands** (`U32`×1, `BOOL2`×8, `BOOL1`×2, `MEAS_BOOL`×3): resets, readers/stimulus on/off, compensations, voltage/temperature/cooling references.
- **`GET_U32`/`GET_U32_LIST`** (12 + 2): every plain index/count/ratio getter.
- **`GET_MEAS`/`GET_MEAS_LIST`** (5 + 7): sampling rate and filter getters, `getVoltageHoldTuner`.
- **Capability probes** (17 via `E384C_WRAP_PROBE` + `hasCompFeature`): every `has*`/`is*` method plus `getCalibrationStatus`.
- **String-out getters** (5): `getDeviceName`, `getDeviceSerial`, `getSerialNumber`, `getCalibMappingFileDir`, `getCalibMappingFilePath`.
- **Misc hand-written one-offs** (13): `enableRxMessageType`, `setAdcCore`, `sendSpiCommand`, `setCustomFlag/Option/Double`, `setDebugBit/Word`, `setStateArrayEnabled/Structure`, `setSateArrayState`, `setTemperatureControlPid`, `zap`.
- **Enum-vector getters** (6): `getReadoutOffsetRecalibrationStatuses`, `getLiquidJunctionStatuses`, `getLiquidJunctionVoltages`, `getClampingModalitiesFeatures`, `getClampingModality`, `getCompensationEnables`.
- **Complex / multi-output getters and setters** (19): `getAvailableChannelsSourcesFeatures`, `getTemperatureChannelsFeatures`, `writeCalibrationEeprom`/`readCalibrationEeprom`, `getCalibFileNames`, `getCalibFilesFlags`, `getCompFeatures`, `getCompOptionsFeatures`, `getCompValueMatrix`, `getCompensationControl`, `getCustomFlags`, `getCustomDoubles`, `getVoltageProtocolRangeFeature`/`getCurrentProtocolRangeFeature`, `getVoltageRampTunerFeatures`, `enableCompensation`, `setCompValues`, `setCompRanges`, `setCompOptions`.
- **Protocol builders** (8): `set{Voltage,Current}ProtocolStructure/Step/Ramp/Sin`.
- **`convert*Values` family** (4): `convertVoltageValues`, `convertCurrentValues`, `convertTemperatureValues`, `convertOnTimeValue`.
- **The 15 overloaded C++ names** (§4a): fully resolved and wrapped, ~31 total C functions across the 15 C++ names.
- **`ChannelModel`/`BoardModel` family** (36): 4 list getters (`getChannels`, `getChannelsOnBoard`, `getChannelsOnRow`, `getBoards`) + 14 `ChannelModel` getters + 15 `ChannelModel` setters + 4 `BoardModel` getters (incl. `getChannelsOnBoard` on the handle itself) + 3 `BoardModel` setters (`setChannelsOnBoard` intentionally excluded).
- **OK calibration RAM/EEPROM**: `okMoveCalibrationEepromToRams/RamsToEeprom`, `okSelectCalibrationRam`, `okWriteCalibrationRam`, `okReadCalibrationRam`.
- Working Rust example: scan → print → connect → query VC current range(s) → disconnect. (Not yet extended to exercise any of this session's new surface — see §9.)

### Deferred / out of scope (7 methods, all deliberate)
- `getCalibParams` / `setCalibParams` — **explicitly out of scope** per the
  original task instruction; `CalibrationParams_t` is a 4-level nested tree
  + `unordered_map`, deferred to an opaque-handle design (see §4) that
  hasn't been started.
- `initialize` / `deinitialize` — internal-only per the library's own doc
  comment ("Internal method used by the commlib during the connection. Must
  not be called."); never in scope.
- `isDeviceUpgradable` / `upgradeDevice` — static methods taking a bare
  `deviceId` string (not an `E384Device*`), adjacent to device discovery.
  Never named in the original scope; flagged during inventory but not
  wrapped since nobody asked for them.
- `getCustomOptions` — `(vector<string>&, vector<vector<string>>&,
  vector<uint16_t>&)`. Its per-control description list is **ragged in
  string count**, not just numeric width — ` getCalibFilesFlags`/
  `getCompValueMatrix`'s row-major-with-padding trick doesn't transfer
  cleanly to "a variable number of owned strings per row". This is a
  genuinely new nested opaque-tree problem, structurally similar to
  `CalibrationParams_t`, and was deferred rather than forcing a bad design.
- Also **not wrapped**, but for a different reason (not a missing
  `MessageDispatcher` method — it's a `BoardModel` method, so not counted
  in the 253/7 figures above): `BoardModel::setChannelsOnBoard(vector
  <ChannelModel*>)`. Would require constructing owned `ChannelModel*`
  instances from Rust, which the borrowed-handle model doesn't support; it's
  internal wiring set up by the library at connect time with no legitimate
  external caller.

## 9. Open decisions for next session

Resolved this session (kept here for history, not because they're still open):
- ~~String-out convention~~ — resolved: `E384String` opaque handle, see §4.
- ~~Overload disambiguation naming scheme~~ — resolved: parameter-shape
  suffixes, chosen per-overload-set, see §4a for the complete table.
- ~~`ChannelModel*`/`BoardModel*` design~~ — resolved: borrowed opaque
  handles, see §4.

Genuinely still open:
- Whether to eventually wrap `isDeviceUpgradable`/`upgradeDevice` (static,
  deviceId-based, adjacent to device discovery but never requested).
- Whether `getCustomOptions` should get a proper nested opaque-tree design
  later — same shape of problem as `CalibrationParams_t` (see Deferred
  list above), not attempted this session.
- Whether `CalibrationParams_t` itself (`getCalibParams`/`setCalibParams`)
  should be tackled next, now that essentially everything else is done —
  it's the largest remaining gap by far.
- The Rust example program only exercises the pre-session surface (scan →
  connect → VC current range → disconnect). None of this session's ~190
  new functions have been exercised from Rust yet; consider extending the
  example, at least spot-checking one function per new macro shape, before
  fully trusting the bindgen-generated signatures end-to-end.
- Whether `okReadCalibrationRam`'s result-retrieval path (does the read
  value come back through a separate getter or the message stream?) needs
  its own wrapper — flagged in the original notes, still not resolved, and
  not touched this session.
