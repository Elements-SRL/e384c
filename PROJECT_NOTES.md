# e384c — C shim for e384CommLib

Reference notes for the project: what it is, how it's built, the conventions
locked in so far, and what's left to port. Written so a future session (or
another person) can pick this up without re-deriving the design.

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
└── docs/
    └── PROJECT_NOTES.md     ← this file
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
  variable.
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

## 4. Core design decisions

| Decision | Rationale |
|---|---|
| Opaque handle = `MessageDispatcher*` itself (`reinterpret_cast` to `E384Device*`) | No wrapper allocation. Virtual dispatch on the concrete device subclass works automatically. |
| `E384Err` = raw `uint32_t`, values passed through unmodified from `ErrorCodes_t` | Values are stable/explicit (see `e384commlib_errorcodes.h`); no remapping needed. `0xFFFFFFFF` (`ErrorUnknown`) requires unsigned, not signed. |
| `E384_WARNING_VALUE_CLIPPED` (`0x8000000B`) is success, not failure | It has the high bit set but the operation *did* apply (clipped). Every caller must check `err == E384_SUCCESS \|\| err == E384_WARNING_VALUE_CLIPPED`, not just `err == 0`. |
| `Measurement_t` / `RangedMeasurement_t` → POD mirror structs (`E384Measurement`, `E384RangedMeasurement`) | The C++ originals hold a `std::string unit`, so they aren't POD. Mirrors use `int32_t unit` (see next row) instead of a string, making them genuinely `Copy`-safe in Rust. |
| Units are an **enum** (`E384Unit`: `NONE/V/A/HZ/S/C`, plus output-only `UNKNOWN = -1`), not a `char[N]` buffer | Confirmed by grepping the actual unit strings used across the codebase — only `V`, `A`, `Hz`, `s`, `C` ever appear. An enum is smaller, exhaustively matchable in Rust, and avoids any embedded-string / truncation questions entirely. `UNKNOWN` exists only as an output sentinel if the library ever surprises us; never construct it as an input. |
| `RxOutput_t` and `ChannelSources_t` are already pure POD in the C++ library | No mirror needed — used as-is across the boundary (field-for-field identical `E384RxOutput`/etc.). |
| `CalibrationParams_t` — deferred | 4-level nested tree + `unordered_map`. Touches only `getCalibParams`/`setCalibParams`. When ported: **opaque handle + accessor functions**, never flatten the tree into a C struct. |
| Data acquisition buffer: **caller-provided, library-allocated, fixed-size** | Sized via `e384_getRxDataBufferSize` (backed by a fixed circular buffer), allocated once via `e384_allocateRxDataBuffer`, reused across every `e384_getNextMessage` call, released with `e384_deallocateRxDataBuffer`. No two-call protocol here — the size is knowable up front and this is the hot path. |
| Cold-path list getters (ranges, device lists): **two-call size/fill protocol** | Call once with `out = NULL` → `*count` receives available size. Allocate. Call again with a real buffer → fills `min(capacity, available)`, `*count` still reports what's available (so a caller can detect "buffer too small, retry"). Chosen over caller-guesses-capacity because sizes aren't knowable in advance and this is called rarely (setup time). Note: the shim is stateless, so the underlying C++ getter runs on *both* calls — fine for cold-path methods. |
| Device string lists (`detectDevices`, `listAllDevices`): **opaque `E384DeviceList*` handle** | Queried with `_count`/`_get`, released with `_free`. Avoids the "who owns/frees each `char*`" question that a raw `char**` out-param would raise. Borrowed pointers from `_get` are valid only until `_free`. |
| Every wrapped function guards against C++ exceptions with catch-all → `ErrorUnknown` | The library itself reports failure via `ErrorCodes_t`, not exceptions — but `std::bad_alloc` or a bug could still throw, and unwinding across the `extern "C"` boundary into Rust is undefined behavior. |
| `e384_disconnect(device, overheatFlag)` also `delete`s the dispatcher | `connectDevice` heap-allocates internally; the shim owns destruction. NULL-tolerant like `free()`. |
| `e384commLib` linked statically **into** `e384c.dll`; CRT models must match | See §6. |

## 5. Wrapper macro shapes (in `e384c_internal.hpp`)

Each macro takes `(cname, method)` and expands to a full `extern "C"`
function body. Adding a new method that fits an existing shape is a single
line in `e384c_shim.cpp` plus a declaration in `e384c.h`.

| Macro | C++ signature shape | Used for |
|---|---|---|
| `E384C_WRAP_CHANNEL_MEAS_CMD` | `(vector<uint16_t>, vector<Measurement_t>, bool) -> ErrorCodes_t` | `set*HoldTuner`, `set*Half`, `setGateVoltages`, `setCalib*Gain/Offset` (17 methods) |
| `E384C_WRAP_CHANNEL_UPDATE` | `(vector<uint16_t>, bool) -> ErrorCodes_t` | `updateCalib*`, `reset*` (12 methods) |
| `E384C_WRAP_CHANNEL_BOOL_CMD` | `(vector<uint16_t>, vector<bool>, bool) -> ErrorCodes_t` | `enableStimulus`, `turn*On`, `*Compensation` (9 methods) |
| `E384C_WRAP_GET_RANGED` | `(RangedMeasurement_t&) -> ErrorCodes_t` | `getVCCurrentRange` |
| `E384C_WRAP_GET_RANGED_LIST` | `(vector<RangedMeasurement_t>&, uint16_t& defaultIdx) -> ErrorCodes_t`, two-call protocol | `get*Ranges` (4 methods) |
| `E384C_WRAP_ACTION` | `() -> ErrorCodes_t` | `okMoveCalibration*`, `okReadCalibrationRam` |
| `E384C_WRAP_U16` | `(uint16_t) -> ErrorCodes_t` | `okSelectCalibrationRam` |

Shared helpers used by every macro:
- `E384C_CHECK_DEVICE(device)` — null-handle guard, returns `ErrorDeviceNotConnected`.
- `E384C_GUARD_BEGIN` / `E384C_GUARD_END` — the try/catch-all wrapper.
- `e384c::vec_u16`, `e384c::vec_meas` — array→vector marshaling for inputs.
- `e384c::vec_bool` — **must** convert element-by-element (`vector<bool>` is
  bit-packed in C++; a `reinterpret_cast` of a `uint8_t*` would be UB).
- `e384c::to_c` / `e384c::from_c` (overloaded) — `Measurement_t` /
  `RangedMeasurement_t` ↔ their POD mirrors.
- `e384c::unit_to_c` / `e384c::unit_from_c` — `std::string` ↔ `E384Unit`.
  `unit_from_c` maps anything unrecognized to `""`; only relevant when
  reconstructing a C++ object from a C struct carrying `E384_UNIT_UNKNOWN`,
  which should never happen for values that originated inside the library.
- `e384c::fill_ranged_list` — implements the two-call size/fill logic once,
  shared by every `E384C_WRAP_GET_RANGED_LIST` instantiation.

### Adding a new function that fits an existing shape
1. Add a declaration to `e384c.h` (or reuse an existing `E384C_DECL_*` macro
   if one exists for that shape).
2. Add one line to `e384c_shim.cpp`: `E384C_WRAP_<SHAPE>(e384_cname, cppMethodName)`.
3. Clean-rebuild `e384c`.

### Adding a function with a genuinely new shape
Hand-write it in `e384c_shim.cpp` following the pattern of
`e384_okWriteCalibrationRam` (two mixed scalars) or the data-acquisition
functions (raw pointers, no vector marshaling). If the shape looks like
it'll recur, promote it to a macro afterward.

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
  program.
- `E384DeviceList` strings: copy out to owned `String`s (via
  `CStr::from_ptr(...).to_string_lossy().into_owned()`) **before** calling
  `e384_deviceList_free` — the borrowed pointers from `e384_deviceList_get`
  are invalidated by the free.
- Always disconnect on every exit path, including error paths — a
  reasonable pattern is running the fallible work in a closure and calling
  `e384_disconnect` unconditionally afterward, then returning the
  closure's result.

## 8. Status

### Done (implemented + verified to compile against stub headers)
- Device discovery: `detectDevices`, `listAllDevices`, device-list accessors.
- Connection lifecycle: `connect`, `disconnect`.
- Data acquisition: buffer size/alloc/dealloc, `getNextMessage`, `purgeData`.
- Shape A (17 methods): all `set*HoldTuner/Half`, gate/source voltages, all `setCalib*Gain/Offset`.
- Shape B (12 methods): all `updateCalib*`, both channel resets.
- Shape C (9 methods): stimulus/switch/compensation enable commands.
- Shape E: `getVCCurrentRange` (single), 4x `get*Ranges` (list).
- OK calibration RAM/EEPROM: `okMoveCalibrationEepromToRams/RamsToEeprom`, `okSelectCalibrationRam`, `okWriteCalibrationRam`, `okReadCalibrationRam`.
- Working Rust example: scan → print → connect → query VC current range(s) → disconnect.

### Not yet ported (from the original ~248-method inventory)
- **Shape D** (scalar setters, e.g. `set*Range(uint16_t idx, bool applyFlag)`) — not yet macro'd, though `E384C_WRAP_U16` covers the no-apply-flag variant.
- **~80 misc single-out-param getters** (`get*(uint32_t&)`, `get*(Measurement_t&)`, `has*()`/`is*()` capability probes) — need 2-3 more small macros (a `Measurement_t`-out variant of `E384C_WRAP_GET_RANGED`, a `uint32_t`-out variant, a bool-returning probe variant).
- **Protocol builders** (e.g. `setVoltageProtocolStep`, many scalar params) — hand-written, no vector marshaling needed but no macro shape fits cleanly either.
- **`convert*Values` family** — raw `int16_t*`/`double*` buffer methods. These should be easy (already cross to C cleanly, no marshaling) but haven't been written yet.
- **15 overloaded method names** in the original C++ API (C has no overloading) — need a naming convention (suggested: suffix by parameter shape, e.g. `_byIdx` / `_byVector`) before wrapping. Not yet decided/applied.
- **`ChannelModel*` / `BoardModel*`-returning methods** — need their own (smaller) opaque-handle wrapper, not yet started.
- **`getDeviceName` / `getDeviceSerial`** (`std::string`-returning) — need a string-out convention (buffer+length, or borrowed-pointer-with-lifetime like the device list). Not yet decided.
- **`CalibrationParams_t`** (`getCalibParams`/`setCalibParams`) — deferred opaque-handle design, not started (see §4 table).

## 9. Open decisions for next session
- String-out convention for simple `std::string`-returning getters (`getDeviceName`/`getDeviceSerial`) — buffer+length vs. a small owned-string handle like `E384DeviceList` uses.
- Overload disambiguation naming scheme (needed before wrapping the 15 overloaded C++ names).
- Whether to promote `okReadCalibrationRam`'s result-retrieval path (if the read value comes back through a separate getter or the message stream) — flagged but not resolved.
