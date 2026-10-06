# PORTING INSTRUCTIONS: JK Active Balancer + CoulombSOC (+ later Nissan Leaf MOD) from Battery-Emulator 8.13 (this fork, Arduino IDE) to Battery-Emulator v13 (PlatformIO)

Audience: a future Claude session doing the port. Not written for humans. Everything marked VERIFIED was read from the
v13.0.1 source tree (tag v13.0.1, published 2026-10-01) during the session that wrote this file; everything marked
CHECK must be re-verified against the actual target tag before relying on it.

Written: 2026-10-06. Source fork: `BE 8.13 JK BMS v.3.4 LUCRU/Software` (Arduino sketch `Software.ino`).

---------------------------------------------------------------------------------------------------------------------
## 0. Decisions already taken by the user (do not re-ask)

- Target: upstream https://github.com/dalathegreat/Battery-Emulator, latest release (v13.0.1 at time of writing).
  Fork it, branch `jk-active-balancer`, keep upstream merge-able: minimal edits to shared files, all JK code in its own files.
- Port the JK Active Balancer driver and the custom `CoulombSOC` library. Nissan Leaf MOD comes LATER (section 10), same
  method.
- CAB500 CAN current sensor ONLY. Do NOT port the analog CT path (ADC on GPIO32/33, Vref mode, CT ratio, temperature
  compensation, no-ref mode). Remove every analog-CT setting and code branch.
- Do NOT touch any GPIO / HAL. v13 default LilyGo pins apply (contactors 32/33/25, BMS power 2). The 8.13 custom
  contactor pins (25/15/14) existed only because the analog CT needed GPIO32/33; that reason is gone.
- JK settings go on the v13 common Settings page using the upstream pattern (section 7), not a custom form.
- Port the JK settings 1:1 (all of them, same semantics, same defaults) even where v13 has overlapping upstream features
  (chemistry selector, user pack/cell voltage limits, SOC charge taper, Deye workaround). Section 8 says how to keep
  them from fighting.
- Port double-battery support (two JK instances on two CAN interfaces).
- Drop the other custom edits of this fork (serial link RS485 LilyGo-to-LilyGo changes, `safety.cpp` "battery full =
  50 W" edit, `comm_rs485.cpp` RS485_BAUDRATE define, `include.h` #error). v13 handles those cases itself.
- No migration of 8.13 NVS keys: v13 needs a USB install anyway (partition layout changed at v10). Embedded defaults are
  the 8.13 defaults (section 7.5).

---------------------------------------------------------------------------------------------------------------------
## 1. What to carry over from the 8.13 fork (inventory)

### 1.1 Files that ARE the feature (copy, then transform per section 6)
| 8.13 path (under `Software/`) | Role | Carry? |
|---|---|---|
| `src/lib/CoulombSOC/CoulombSOC.h`, `.cpp` | Custom coulomb-counting SOC library (NMC + LFP voltage LUTs, one-shots, rest recalibration, cycle counter). Pure C++ + `millis()`. | YES, as-is (section 5) |
| `src/battery/JK-Active-Balancer.h`, `.cpp` | Driver: polls up to 15 balancers (CAN ID 0x01..0x0F request / reply frames data types 0x01-0x04), CAB500 parsing (ID 0x3C2), SOC via CoulombSOC, power limit ramps, protections, config validation, web-setting intake. | YES, transformed |
| `src/datalayer/datalayer_extended.h` : `DATALAYER_INFO_JK_ACTIVE_BALANCER_TYPE` (+ `jk_active_balancer`, `jk_active_balancer2` members, `MAX_BALANCERS_WEB` define) | Web/MQTT data for balancers, CAB500 status, cycles, config_error, ct_cab500_active | YES |
| `src/datalayer/datalayer.h` : fields `max_user_set_charge_W/discharge_W`, `mod_*` (13 + loaded flags), `mod_invert_*`, `mod_lfp_*`, `mod_limits_loaded` + 9 voltage fields, `jk_*` (layout/sensor), `system.settings.can_*250k` | 8.13 storage of the web settings | Replace by v13 `user_selected_jk_*` globals (section 7). Do not copy the fields. |
| `src/communication/nvm/comm_nvm.cpp` : key groups `MAXCHARGEW/MAXDISCHARGEW`, `M_*`, `J_*`, `CAN_NAT250/CAN_ADD250` | NVS load/store | Replace by v13 settings store + `/saveSettings` key lists (section 7). Keep key NAMES where convenient (<=15 chars). |
| `src/devboard/webserver/webserver.cpp` : routes `/updateModSettings` (full validation incl. voltage order + layout rules), `/resetJK_BMSCycles` | Save + validation logic | Validation logic YES (moved: section 7.4); routes NO. |
| `src/devboard/webserver/advanced_battery_html.cpp` : `add_mod_settings_form()`, JK status block (`print_jk_balancer` lambda), config-error banner, CAB500 block | Status page | Status block + banner YES into `JkHtmlRenderer` (section 6.6). Settings form NO. |
| `src/devboard/safety/safety.cpp` : `#ifdef JK_ACTIVE_BALANCER` SOC plausibility block (margin = `max_design_voltage_dV/40`) | Plausibility check | YES, but as `Battery::soc_plausible()` override (section 6.5). Do not edit v13 safety.cpp. |
| `src/communication/can/comm_can.cpp` : runtime 250 kbps selection | CAN speed | NO. v13 sets speed per receiver in the `CanBattery` constructor (section 6.2). |
| `src/devboard/hal/hw_lilygo.h` : JK pin block | Pins | NO (decision). |
| `USER_SETTINGS.h` JK comment block, `BATTERIES.h/.cpp` JK `#ifdef` glue | Compile-time selection | NO. v13 selects battery at runtime (`BatteryType` enum). |
| `src/charger/NISSAN-LEAF-CHARGER.cpp` `#ifndef NISSAN_LEAF_BATTERY_MOD` blocks (lines ~119, ~198) | Leaf MOD only | Later (section 10). |
| `src/battery/NISSAN-LEAF-BATTERY-MOD.h/.cpp` | Leaf MOD | Later (section 10). |

### 1.2 Everything that is NOT ported (explicit)
- Analog CT: in 8.13 driver: `setup()` branch `else { pinMode(jk_ct_pin...) ... }`, members `jk_ct_pin`, `jk_ct_ref_pin`,
  `zeroOffsetADC`, `sum_adc_*`, `adc_sample_count`, `CT_SENSOR_SENSITIVITY`, `TEMP_COEFFICIENT_V_PER_C`, `TEMP_VREF_C`,
  `calibrationTempC`, `Current_Raw`, `currentAmps`, `ARDUINO_VOLTAGE`, `ADC_RESOLUTION`, `ct_use_ref_pin`, the whole
  `else` branch of `transmit_can()` (10 ms ADC sampling / 100 ms processing), `previousMillis10/100`,
  `JK_CT_CAB500/JK_CT_NO_REF/JK_CT_USE_TEMP_COMPENSATION` members and the settings `jk_ct_cab500`, `jk_ct_ratio`,
  `jk_ct_temp_comp`, `jk_ct_no_ref`, web rows `cab500/ctratio/cttemp/ctnoref`, NVS keys `J_CAB500/J_CTRATIO/J_CTTEMP/J_CTNOREF`.
  The constructor parameter `int ct_pin` and `JK_1_CT_PIN/JK_2_CT_PIN` go too.
- CAN speed settings `can_native_250k/can_addon_250k` (+ keys, + web rows `can250n/can250a`).
- `EVENT_UNKNOWN_EVENT_SET` usage (8.13 driver sets/clears it as a generic fault marker). v13 has the enum but use
  `EVENT_BATTERY_CHG_DISCHG_STOP_REQ` only.
- The 8.13 `datalayer.battery.settings.mod_settings_loaded`-style "write defaults to NVS on first boot" mechanism. v13
  pattern: globals with compile-time defaults, NVS overrides when key exists (`BatteryEmulatorSettingsStore::getUInt(name, default)`).

### 1.3 Known bugs in the 8.13 driver to FIX during the port (not before; keep 8.13 untouched)
- `update_values()`: `battery_Full_CHARGE_flag == 1;` / `battery_Capacity_Empty == 1;` (and `== 0`) are comparisons,
  not assignments. Flags never set -> `EVENT_BATTERY_FULL/EMPTY` never raised by the driver. Fix with `=` (the following
  `if (battery_Full_CHARGE_flag)` blocks then set charge/discharge power to 0 W; verify that is wanted together with the
  ramps, which already return 0 W above SOC max / below 7 %).
- Discharge ramp bottom is hardcoded `7` (`getDischargePowerLimit(..., 7, ...)`) instead of `soc_cfg.soc_min_percent`.
  Decide with the user; 1:1 means keep 7 unless told otherwise. Put it in a named constant.
- `getDischargePowerLimit/getChargePowerLimit/battery_Wh_Remaining_by_soc` are free functions declared at file scope
  in the `.cpp` (outside the class, after `#endif // JK_ACTIVE_BALANCER`). In v13 (no `#ifdef`, all drivers always
  compiled) they would collide with the Leaf MOD `_Mod` variants only by name suffix; make them `static` or private
  static members of `JKActiveBalancer` to avoid ODR trouble.
- `uint16_t temp_sum = (uint16_t)min_temp + max_temp;` averaging of int16 temps through uint16: negative temps wrap.
  Use `int32_t`.
- With `-Werror -Wall -Wextra` (v13 esp32 envs, VERIFIED in platformio.ini `[common_esp32]`) the 8.13 code will not
  compile until: unused variables removed (`Current_Raw`, etc. go anyway), signed/unsigned comparisons fixed
  (`for (int i...; i < number_of_balancers` is fine; check `cells_processed >= JK_NUMBER_OF_CELLS` types),
  `[[fallthrough]]` not needed (no switch fallthrough present), unused parameters (`transmit_can(unsigned long)` is used),
  `float MAX_PACK_VOLTAGE_DV = 12 * 41.9` narrowing (make it `12 * 41.9f`).

---------------------------------------------------------------------------------------------------------------------
## 2. v13 architecture facts (VERIFIED on tag v13.0.1 unless marked CHECK)

### 2.1 Repo / build
- Layout: `platformio.ini` at repo root, `src_dir = ./Software`, main file `Software/Software.cpp` (not `.ino`),
  sources under `Software/src/{battery,charger,communication,datalayer,devboard,inverter,lib,shunt}`, `system_settings.h`.
- No `USER_SETTINGS.h` / `USER_SETTINGS.cpp` anymore. Battery, inverter, charger, shunt, CAN interfaces, contactor control,
  WiFi etc. are runtime settings in NVS chosen on the web Settings page. First boot has `BatteryType::None`.
- PlatformIO: `platform = pioarduino 55.03.312-1` (release notes: PlatformIO Core 6.2.0 required), `framework = arduino`,
  `lib_ldf_mode = chain+`, `extra_scripts = pre:tools/identify_build.py, pre:tools/embed_help_json.py`,
  `custom_sdkconfig = file://sdkconfig.be_size.defaults`, build_flags `-Wall -Wextra -Wimplicit-fallthrough -fno-exceptions`.
  `[common_esp32]` adds `-Werror`. Environments: `esp32devkit_330`, `lilygo_330` (`-D HW_LILYGO -D SMALL_FLASH_DEVICE -D SDCARD`,
  partitions `min_spiffs.csv`), `stark_330`, `lilygo_2CAN_330` (ESP32-S3), `BECom_330`, ... The LilyGo T-CAN485 target is
  `lilygo_330` => `SMALL_FLASH_DEVICE` is defined => some features are compiled out (e.g. `supports_reset_SOH()` is
  inside `#ifndef SMALL_FLASH_DEVICE`, MG5 and ChargebyteCCS batteries excluded). Watch flash size.
- Libraries live in `Software/src/lib/<name>/` as plain sources (no library.json), e.g. `mcp2515_lite`, `uds_isotp`,
  `pierremolinaro-acan-esp32`, `ESP32Async-ESPAsyncWebServer`. Note: `miwagner-ESP32-Arduino-CAN` and
  `pierremolinaro-acan2515` from 8.13 are GONE (replaced by `pierremolinaro-acan-esp32` and `mcp2515_lite`).
  CoulombSOC goes to `Software/src/lib/CoulombSOC/`.
- Unit tests: `test/` (CMake, native build, `test/battery/*Test.cpp`, e.g. `NissanLeafTest.cpp`). Optional for JK.
- OTA to v13 requires the board to already be on >= v10 (USB install via BE-Web-Installer once).

### 2.2 Battery object model
- `Software/src/battery/Battery.h`: `enum class BatteryType { None = 0, BmwI3 = 2, ..., GrowattLv = 57, Highest }`.
  Add `JkActiveBalancer = 58` (and later `NissanLeafMod = 59`) BEFORE `Highest`. Values are persisted in NVS key
  `BATTTYPE` and used in CSS selectors `form[data-battery="58"]` on the settings page.
- Abstract `class Battery { virtual void setup(); virtual void update_values(); virtual const char* interface_name(); ... }`
  with many `supports_*()` / action virtuals (all default false/NOP), `virtual bool soc_plausible() { return true; }`,
  `virtual void safety_current_range_dA(int16_t& max_dA, int16_t& min_dA);`, `virtual bool supports_charged_energy()`,
  `virtual BatteryHtmlRenderer& get_status_renderer()`, `uint8_t battery_index = 1` (set centrally: 1/2/3), and
  `static constexpr const char* Name = "..."` convention in each driver (used by `name_for_battery_type`).
- `Software/src/battery/CanBattery.h/.cpp`: `class CanBattery : public Battery, Transmitter, CanReceiver`.
  Constructors: `CanBattery(CAN_Speed speed = CAN_SPEED_500KBPS)` (uses `can_config.battery`) and
  `CanBattery(CAN_Interface interface, CAN_Speed speed = ...)`. The constructor calls `register_transmitter(this)` and
  `register_can_receiver(this, can_interface, speed)` => the CAN interface speed is decided per registered receiver.
  JK + CAB500 run at 250 kbps => construct with `CAN_Speed::CAN_SPEED_250KBPS`. Pure virtuals to implement:
  `handle_incoming_can_frame(CAN_frame)`, `transmit_can(unsigned long currentMillis)`. Send with member
  `transmit_can_frame(const CAN_frame* frame)` (interface implied). `change_can_speed()/reset_can_speed()` exist.
- `Software/src/battery/BATTERIES.h/.cpp`: `Battery* battery, *battery2, *battery3`; `create_battery(BatteryType)`
  (switch `return new XxxBattery();`), `name_for_battery_type()` (switch returning `Xxx::Name`),
  `battery_supports_double(type)` + the `switch` in `setup_battery()` constructing `battery2` with
  `(&datalayer.battery2, &datalayer_extended.xxx_2, can_config.battery_double)`, `battery_supports_triple(type)`,
  `battery_detects_capacity(type)` (true => settings page hides "Battery capacity"; JK does NOT detect capacity, it
  needs the Wh setting => leave JK out of that list). Per-battery user settings are plain globals declared `extern` in
  `BATTERIES.h` and defined at the bottom of `BATTERIES.cpp` (e.g. `int user_selected_daly_power_per_percent = 50;`,
  `uint16_t user_selected_pylon_baudrate = 500;`). Follow that for `user_selected_jk_*`.
- `datalayer.system.status.battery_allows_contactor_closing` (bool) and `battery2_allowed_contactor_closing` exist;
  drivers hold a `bool*` to it (same idea as 8.13 `allows_contactor_closing`).
- Chemistry: `enum battery_chemistry_enum { Autodetect = 0, NCA = 1, NMC = 2, LFP = 3, ZEBRA = 4, Highest }` in
  `devboard/utils/types.h`; `user_selected_battery_chemistry` (NVS `BATTCHEM`) is copied into
  `datalayer.batteryN.info.chemistry` in `setup_battery()` before `battery->setup()`; the driver may override.
- `CAN_frame` struct unchanged (`.FD .ext_ID .DLC .ID .data.u8[]`) - CHECK `types.h` for any added field.

### 2.3 Datalayer differences vs 8.13
- `datalayer.battery.settings` (8.13) moved to top-level `datalayer.battery_settings` (`DATALAYER_BATTERY_SETTINGS_TYPE`):
  `max_user_set_charge_dA`, `max_user_set_discharge_dA` (default 300), `max_remote_set_*`,
  `max_user_set_charge_voltage_dV`, `max_user_set_discharge_voltage_dV` (default 3000 = 300.0 V!),
  `user_set_voltage_limits_active`, `soc_scaling_active`, `min/max_percentage`, `user_settings_limit_charge/discharge`,
  `inverter_limits_charge/discharge`, `sofar_user_specified_battery_id`, `user_set_bms_reset_duration_ms`, ...
- `datalayer.battery.status`: `max_charge_power_W`, `max_discharge_power_W` (uint32), new `bms_max_charge_power_W` /
  `bms_max_discharge_power_W` (snapshot of what the driver asked, filled centrally), `real_soc`, `reported_soc`,
  `soh_pptt` (default 9900) + a "SOH available" flag (CHECK exact name around datalayer.h:159), `voltage_dV`,
  `current_dA` (sign: >0 charging, <0 discharging - same as 8.13 JK convention), `cell_voltages_mV[MAX_AMOUNT_CELLS]`,
  `cell_max/min_voltage_mV`, `temperature_max/min_dC`, `remaining_capacity_Wh`, `real_bms_status` (enum
  `BMS_DISCONNECTED/BMS_ACTIVE/BMS_FAULT...`), `CAN_battery_still_alive`, `active_power_W`.
- `datalayer.battery.info`: `total_capacity_Wh` (default 30000; settings key `BATTERY_WH_MAX` loads into battery,
  battery2, battery3), `max/min_design_voltage_dV`, `max/min_cell_voltage_mV`, `max_cell_voltage_deviation_mV`
  (default 500), `number_of_cells`, `chemistry`.
- New `datalayer.aggregate` (installation totals for multi-pack; inverter reads from it). Driver writes only its own pack.
- `datalayer.system.info.configured_batteries` set by `setup_battery()`.

### 2.4 Main loop / limit pipeline (Software.cpp, VERIFIED)
Order per cycle: `battery->update_values()` (and battery2/3) -> `update_calculated_values()` (snapshots BMS limits,
converts `max_*_power_W` to `max_*_current_dA` using live `voltage_dV` else `max_design_voltage_dV`, clamps by
`max_user_set_*_dA` / remote limits, computes limiting-factor flags, active power) -> `update_machineryprotection()`
(safety layer; calls `battery->soc_plausible()`, uses `safety_current_range_dA()`, cell/pack voltage + deviation +
temperature events, battery full/empty) -> `filter_charge_taper_soc()` (upstream SOC-based charge taper, settings
`CHGTAPERSOC/CHGTAPERSTART/CHGTAPERFLOOR`, `charge_taper_soc`, `charge_taper_band_pptt`, `charge_taper_floor_W`;
`Battery::mandatory_charge_taper()` can force it on) -> low-pass filter -> `inverter->update_values()`.
Consequence: the driver's own ramps run first; the upstream taper may reduce further (lower wins). See section 8.

### 2.5 Settings store + web pattern (VERIFIED)
- `Software/src/communication/nvm/comm_nvm.h`: RAII `class BatteryEmulatorSettingsStore { getInt/saveInt, getUInt/saveUInt,
  getBool/saveBool, getString/saveString, settingExists, removeKey, clearAll }` on Preferences namespace
  `"batterySettings"`. No float API => store floats scaled as integers (e.g. amps x1000, percent x10).
- `init_stored_settings()` in `comm_nvm.cpp` reads every key into globals/datalayer at boot
  (e.g. `user_selected_pylon_baudrate = settings.getUInt("PYLONBAUD", 500);`). `store_settings()` exists for the few
  runtime-written values (CHECK which); the Settings page itself writes NVS directly in the `/saveSettings` route.
- `webserver.cpp` `/saveSettings` (HTTP_POST): iterates request params; names listed in the arrays
  `boolSettingNames[]` (checkbox, value "on"), `uintSettingNames[]`, `stringSettingNames[]` are saved generically
  (`saveUInt(name, atoi)` etc.); special names handled in the `if (p->name() == ...)` chain (e.g. `BATTPVMAX` stored
  x10). A checkbox absent from the POST => saved false (CHECK the exact loop: v13 iterates params present; missing
  checkbox handling is done after the loop for `boolSettingNames` - read the code).
- `settings_html.cpp`: HTML template with `%PLACEHOLDER%` tokens resolved in `settings_processor(const String& var)`
  (`if (var == "PYLONBAUD") return String(settings.getUInt("PYLONBAUD", 500));`), per-battery sections wrapped in
  `<div class="if-daly">...</div>`, visibility by CSS generated in the same file:
  `form .if-daly { display: none; } form[data-battery="23"] .if-daly { display: contents; }` (CHECK exact rule style
  near line 1484-1545; e.g. `form[data-battery="21"] .if-nissan`). The `<form data-battery="%BATTTYPE%">` attribute is
  updated by JS when the battery dropdown changes (CHECK the script).
- Help texts: `tools/embed_help_json.py` + `web_data/` (ⓘ help per setting; on 4 MB boards fetched from GitHub). Adding
  help entries is optional; CHECK the JSON format in `web_data/help*.json` if adding.
- Battery limits rows `BATTPVMAX/BATTPVMIN/BATTCVMAX/BATTCVMIN` are shown only for `if-cbms` battery types (generic BMS
  list: 6, 11, 22, 23, 24, 31, 41, 48, 49, 51). JK (58) is not in that list => those rows stay hidden for JK unless added.
  Since JK ports its own limits 1:1, do NOT add 58 to `if-cbms`.

### 2.6 "More battery info" page (VERIFIED)
- `Software/src/devboard/webserver/BatteryHtmlRenderer.h`: `class BatteryHtmlRenderer { virtual String get_status_html() = 0;
  virtual String get_dtc_html(); virtual String get_command_prefix_html(const char* identifier); virtual bool
  html_render_failed() const; virtual bool renders_own_battery_data() { return false; } }`. A renderer that reads data
  through pointers given at construction must return `true` from `renders_own_battery_data()` or the page shows a
  "limited to the Main Battery" notice for battery 2/3. Example: `PYLON-HTML.h` (`PylonHtmlRenderer(PylonExtendedData*)`),
  `NISSAN-LEAF-HTML.h` (`NissanLeafHtmlRenderer(&datalayer.battery, &datalayer_extended.nissanleaf)`, returns true).
  Use `CheckedHtml` (`checked_html.h`) for large strings (tracks allocation failure).
- Buttons: `advanced_battery_html.cpp` has a global `std::vector<BatteryCommand> battery_commands` =
  `{identifier, title, confirm text, supports-lambda, action-lambda}`; each is rendered when
  `supports_*()` returns true and served at `PUT /<identifier>` (CHECK the route registration in webserver.cpp, it is
  generic over the vector). To add "Reset BMS cycles": add `virtual bool supports_reset_cycles()` / `virtual void
  reset_cycles()` to `Battery.h` (default false/NOP) and one entry to `battery_commands`. (Alternative without touching
  Battery.h: reuse `supports_reset_SOH()/reset_SOH()` - but it is compiled out on `SMALL_FLASH_DEVICE` = LilyGo, so do
  not.)

### 2.7 Events (VERIFIED names exist in v13 `events.h`)
`EVENT_BATTERY_CHG_DISCHG_STOP_REQ`, `EVENT_BATTERY_FULL`, `EVENT_BATTERY_EMPTY`, `EVENT_SOC_PLAUSIBILITY_ERROR`,
`EVENT_PERSISTENT_SAVE_INFO`, `EVENT_UNKNOWN_EVENT_SET` (do not use), `EVENT_EQUIPMENT_STOP`.
Signatures: `set_event(EVENTS_ENUM_TYPE, int16_t data)`, `set_event(EVENTS_ENUM_TYPE, int16_t data, uint8_t battery)`,
`clear_event(EVENTS_ENUM_TYPE)`, `clear_event(EVENTS_ENUM_TYPE, uint8_t battery)`, `set_event_latched(...)`.
In a driver always pass `battery_index` as the third argument. `events.h` comment says ordering of some events is
static_asserted (EVENT_BATTERY_EMPTY first in a group) - do not reorder, and if a new JK event is needed append it at the
end of the list and add its level/text in `events.cpp` (CHECK the `XX()` macro list and `get_event_message_string`).

### 2.8 HAL (VERIFIED)
`Software/src/devboard/hal/hal.h` has `class Esp32Hal` with virtual pin getters (`POSITIVE_CONTACTOR_PIN()`,
`BMS_POWER()`, ...) and `esp32hal->` global; board files `hw_lilygo.h`, `hw_lilygo2can.h`, `hw_stark.h`, `hw_devkit.h`,
`hw_becom.h`, `hw_waveshare.h`, `hw_dfrobot_edge101.h`, `hw_3LB.h`. Nothing to change (decision). `MAX_AMOUNT_CELLS`
in `system_settings.h` (CHECK value, was 192 in 8.13).

---------------------------------------------------------------------------------------------------------------------
## 3. Port plan (ordered checklist)

- [ ] 3.1 Clone upstream at the latest release tag, create branch. Install PlatformIO Core >= 6.2.0. Build the untouched
      tree for `lilygo_330` first (`pio run -e lilygo_330`) to get a baseline and cache the toolchain. Record flash usage.
- [ ] 3.2 Copy `CoulombSOC` (section 5). Build. Fix `-Werror` findings inside the lib only.
- [ ] 3.3 Add datalayer_extended JK struct + members (section 6.1). Build.
- [ ] 3.4 Add `user_selected_jk_*` globals + defaults (section 7.5) to `BATTERIES.h/.cpp`. Build.
- [ ] 3.5 Create `JK-ACTIVE-BALANCER.h/.cpp` + `JK-ACTIVE-BALANCER-HTML.h` by transforming the 8.13 files (section 6).
      Build with the type NOT yet registered (dead code still compiles).
- [ ] 3.6 Register the type (section 6.7). Build. Flash to a bench board, select "JK Active Balancer" on Settings, set
      BATTCOMM to the add-on CAN, reboot, confirm polling at 250 kbps on a CAN analyzer.
- [ ] 3.7 Settings page section + NVS load + save keys (section 7). Build. Verify every field round-trips.
- [ ] 3.8 Status renderer + cycles reset command (section 6.6). Verify page for battery 1 and (double) battery 2.
- [ ] 3.9 Coexistence checks (section 8). Bench test list (section 9).
- [ ] 3.10 Optional: unit test `test/battery/JkActiveBalancerTest.cpp` modelled on `NissanLeafTest.cpp` (CHECK harness).
- [ ] 3.11 Leaf MOD (section 10) as a separate PR after JK is stable.

---------------------------------------------------------------------------------------------------------------------
## 4. Git / PR hygiene
- One commit per checklist item. Shared upstream files touched: `Battery.h` (enum + 2 virtuals), `BATTERIES.h/.cpp`
  (registration + globals), `datalayer_extended.h` (struct + members), `comm_nvm.cpp` (load keys), `webserver.cpp`
  (key arrays, optional validation), `settings_html.cpp` (section + placeholders + CSS), `advanced_battery_html.cpp`
  (one `battery_commands` entry). Everything else is new files. Keep diffs in shared files minimal and grouped under a
  `// --- JK Active Balancer ---` comment so upstream merges stay easy.
- Run `clang-format` with the repo `.clang-format` on new files (upstream CI checks formatting - CHECK `.github/workflows`).

---------------------------------------------------------------------------------------------------------------------
## 5. CoulombSOC library port
- Copy `Software/src/lib/CoulombSOC/CoulombSOC.h` and `.cpp` verbatim to the same relative path in v13.
- API used by the driver (keep): `CoulombSOCConfig` fields `capacity_Ah, charge_efficiency, current_deadband_A,
  invert_current_sign (driver sets true, fixed), soc_max_percent, soc_min_percent, vfull_on_mV, vfull_off_mV,
  vempty_on_mV, vempty_off_mV, sample_period_ms, init_min_cell_mV, LFP_Chemestry_Type, recal_rest_ms,
  recal_rest_current_A, recal_soc_low_pct, recal_soc_high_pct`; methods `begin(max_mV, min_mV)`,
  `update(max_mV, min_mV, current_A)`, `getSoCPercent()`, `getAvgCellmV()`, `getCapacity()`, `getCycleCount()`,
  `setCycleCount()`, `setSoCPercent()`, `resetToFull()`, `resetToEmpty()`, `setCapacity()`.
- Semantics to remember: library current convention is +discharge/-charge; driver feeds +charge/-discharge and
  `invert_current_sign = true` fixes that. `checkAndRecalibrate(I, cell_max, cell_min)` runs each `update()`: rest when
  `|I| < recal_rest_current_A` for `recal_rest_ms`, then snaps SOC to LUT(avg cell) if cell_max >= vfull_off OR
  cell_min <= vempty_off OR SOC in [soc_min..recal_soc_low] OR [recal_soc_high..soc_max]; repeats every rest period.
- `-Werror` candidates in the lib: `isFiniteF` uses `isnan/isinf` from `<math.h>` (fine); template `myMax` unused
  warning if only one instantiation (fine); `estimateSoCFromCellmV` loop `int i` vs `uint16_t` compares - cast;
  unused parameter warnings none. `#include <Arduino.h>` is OK under the arduino framework.
- Memory: two 101-entry `uint16_t` LUTs x2 chemistries = 808 bytes flash. Fine.

---------------------------------------------------------------------------------------------------------------------
## 6. Driver transformation (8.13 `JK-Active-Balancer.*` -> v13 `JK-ACTIVE-BALANCER.*`)

### 6.1 datalayer_extended.h
Add (copy from 8.13, minus analog CT fields):
```
#define MAX_BALANCERS_WEB 15
typedef struct {
  int number_of_balancers = 0;
  uint8_t config_error = 0;          // JKConfigError
  bool ct_cab500_active = true;      // keep for the HTML (always true now) or drop the field and the HTML condition
  int16_t balancer_temperatures_dC[MAX_BALANCERS_WEB]; int fault_counters[..]; bool is_connected[..];
  uint16_t max_balance_current_mA[..]; bool balance_switch_status[..]; bool charge_balancing_status[..];
  bool discharge_balancing_status[..]; bool cell_count_mismatch_alarm[..]; bool wire_resistance_alarm[..];
  uint32_t BMS_Cycles = 0; uint32_t Calc_capacity_Wh = 0; bool UserRequestBMSCyclesReset = false;
  bool ct_cab500_comms_fault, ct_cab500_hardware_error; uint8_t ct_cab500_error_code; int ct_cab500_fault_counter;
  bool ct_cab500_is_connected;
} DATALAYER_INFO_JK_ACTIVE_BALANCER_TYPE;
```
and members `jk_active_balancer`, `jk_active_balancer_2` (v13 naming uses `_2`, e.g. `nissanleaf_2`) in
`class DataLayerExtended`. Drop `UserRequestBMSCyclesReset` if the reset goes through `reset_cycles()` directly.

### 6.2 Header `JK-ACTIVE-BALANCER.h`
- Includes: `"../datalayer/datalayer.h"`, `"../datalayer/datalayer_extended.h"`, `"CanBattery.h"`,
  `"JK-ACTIVE-BALANCER-HTML.h"`, `"../lib/CoulombSOC/CoulombSOC.h"`. Remove `../include.h`, `BATTERY_SELECTED`,
  `SELECTED_BATTERY_CLASS`, `EXTENDED_DATA_PTR` macros (8.13 glue).
- Keep: `POLL_INTERVAL_MS 200`, `POLL_TIMEOUT_MS 1000`, `MAX_FAULT_COUNT 10`, `MAX_BALANCERS 15`,
  `JK_MAX_CELLS_PER_BALANCER 24`, `enum JKConfigError`, `jk_apply_chemistry_defaults(bool lfp)` (rewrite it to fill
  the `user_selected_jk_*` globals instead of `datalayer.battery.settings.mod_*`).
  Make the `#define`s class-scoped `static constexpr` to avoid polluting the global namespace (v13 drivers do this,
  e.g. `static const int MAX_CELL_DEVIATION_MV = 150;` in PylonBattery).
- Class:
```
class JKActiveBalancer : public CanBattery {
 public:
  // battery 2
  JKActiveBalancer(DATALAYER_BATTERY_TYPE* datalayer_ptr, DATALAYER_INFO_JK_ACTIVE_BALANCER_TYPE* extended,
                   bool* contactor_allowed_ptr, CAN_Interface targetCan)
      : CanBattery(targetCan, CAN_Speed::CAN_SPEED_250KBPS), renderer(datalayer_ptr, extended) { ... }
  // battery 1
  JKActiveBalancer() : CanBattery(CAN_Speed::CAN_SPEED_250KBPS), renderer(&datalayer.battery, &datalayer_extended.jk_active_balancer) { ... }
  virtual void setup(); virtual void handle_incoming_can_frame(CAN_frame); virtual void update_values();
  virtual void transmit_can(unsigned long);
  static constexpr const char* Name = "JK Active Balancer (CAB500)";
  BatteryHtmlRenderer& get_status_renderer() { return renderer; }
  bool supports_real_BMS_status() { return true; }
  bool soc_plausible();                       // section 6.5
  void safety_current_range_dA(int16_t&, int16_t&);   // section 6.5
  bool supports_reset_cycles() { return true; }  void reset_cycles();   // section 6.6
  bool supports_charged_energy() { return false; }
 private: ... (8.13 members minus analog CT, see 1.2)
};
```
- `can_interface` is a protected member of `CanBattery`; drop the 8.13 `int can_interface` member and all
  `transmit_can_frame(&JK_REQUEST_FRAME, can_interface)` => `transmit_can_frame(&JK_REQUEST_FRAME)`.
- 8.13 `#ifdef DOUBLE_BATTERY` in the driver existed only for the analog ref pin => gone.

### 6.3 setup()
Order (8.13 had web-settings intake first; keep):
1. Protocol name: only for battery 1: `if (battery_index == 1) strncpy(datalayer.system.info.battery_protocol, Name, 63);`
   (8.13 used `allows_contactor_closing != nullptr` as the "primary" test; v13 provides `battery_index`).
2. Read settings from the `user_selected_jk_*` globals into the members (`JK_NUMBER_OF_CELLS`, `JK_CELLS_BALANCER`,
   `JK_BRIDGE_MODE`, `JK_BALANCER_LOW_VOLTAGE`, `JK_FW_VERSION`, `JK_FW_MASK`, `max_balancers_active`,
   `reverse_current_active`, limits, SOC config). No "write defaults to NVS" step.
3. Validate layout exactly as 8.13 (`config_error`), compute `JK_BRIDGE_CELLS`, `JK_SERIES_CELLS`, `number_of_balancers`,
   cap by `max_balancers_active`, init `balancers[]`, `fw_inverted` per balancer from version/mask.
4. `ct_cab500_boot_millis = millis();` (30 s grace).
5. `MAX_PACK_VOLTAGE_DV = JK_SERIES_CELLS * (pack_max_mVpc / 100.0f)` etc.; CoulombSOC config as 8.13 with
   `sample_period_ms = 50`, `invert_current_sign = true`, `LFP_Chemestry_Type = user_selected_jk_lfp`; `soc = CoulombSOC(soc_cfg)`.
6. Fill `datalayer_battery->info`: `number_of_cells`, `max/min_design_voltage_dV`, `max/min_cell_voltage_mV`,
   `max_cell_voltage_deviation_mV`, and `chemistry = user_selected_jk_lfp ? LFP : NMC` (overrides BATTCHEM).
   `total_capacity_Wh` comes from NVS `BATTERY_WH_MAX` already (do not write it).
7. `datalayer_jk->config_error`, `ct_cab500_active = true`.
Do NOT call `register_can_receiver` yourself (CanBattery ctor did). Do not read `datalayer.battery.settings.*`
(does not exist); the discharge-voltage cut-off reads `datalayer.battery_settings.max_user_set_discharge_voltage_dV`.

### 6.4 handle_incoming_can_frame / transmit_can / update_values
- `handle_incoming_can_frame`: identical to 8.13 minus `if (JK_CT_CAB500 && ...)` => CAB500 branch unconditional;
  keep `if (config_error) return;`. Reverse sign application stays where `BMS_current_A` is assigned (50 ms averaging
  block in `transmit_can`).
- `transmit_can`: identical minus the analog `else` branch; the CAB500 fault counting stays. `transmit_can_frame(&frame)`.
- `update_values`: identical logic (config error early-return, fault aggregation incl. CAB500, aggregation of cells
  with bridge/low-voltage modes, datalayer status writes, extended data, cycles persistence, SOC init on first alive,
  SOH from cycles, remaining Wh, power-limit decision chain, full/empty). Changes:
  - Replace every `set_event(X, 0)` with `set_event(X, 0, battery_index)` and `clear_event(X)` with
    `clear_event(X, battery_index)`.
  - Remove `set_event(EVENT_UNKNOWN_EVENT_SET...)` / `clear_event(EVENT_UNKNOWN_EVENT_SET)` lines.
  - Cycles persistence: 8.13 called `store_settings()` after changing `datalayer_jk->BMS_Cycles`, with the key
    `JK_BMS_Cycles` written in `comm_nvm.cpp`. v13: write directly here:
    `{ BatteryEmulatorSettingsStore s; s.saveUInt(battery_index == 1 ? "JK_CYCLES" : "JK_CYCLES2", cycles); }`
    and read in `init_stored_settings()` (or lazily in `setup()` with a `BatteryEmulatorSettingsStore s(true)`).
    Rate-limit: cycles change at most once per full discharge, fine.
  - `datalayer_battery->status.real_bms_status` assignments as 8.13 (`BMS_FAULT/BMS_ACTIVE/BMS_DISCONNECTED`).
  - `datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;` on every valid balancer frame (as 8.13).
  - The "50 W instead of 0 W for Deye" values in the fault branches: keep 1:1 (decision), but see section 8.4.
  - Fix the `==` assignment bug (1.3).
- `power_W_to_current_dA` conversion is central (`update_calculated_values`), nothing to do in the driver.

### 6.5 Safety hooks (replace the 8.13 edits in safety.cpp)
```
bool JKActiveBalancer::soc_plausible() {
  // 8.13 safety.cpp JK block: pack > max_design - max_design/40 (2.5 %) and real_soc < 65.00 % => implausible
  const uint16_t vmax = datalayer_battery->info.max_design_voltage_dV;
  if (datalayer_battery->status.voltage_dV > (vmax - vmax / 40)) return datalayer_battery->status.real_soc >= 6500;
  return true;
}
```
CHECK in v13 `safety.cpp` how the result is used (sets `EVENT_SOC_PLAUSIBILITY_ERROR` when false; verify whether it
clears when true again - 8.13 only cleared inside the voltage window).
`safety_current_range_dA(max, min)`: the driver publishes a 50 ms mean of CAB500 samples; track per-window min/max mA
in the RX path and return them here so the safety layer sees peaks. Default implementation returns the published
current - acceptable for a first port; note it as TODO.

### 6.6 HTML renderer + cycles reset
- New `JK-ACTIVE-BALANCER-HTML.h`: `class JkActiveBalancerHtmlRenderer : public BatteryHtmlRenderer {
  JkActiveBalancerHtmlRenderer(DATALAYER_BATTERY_TYPE* b, DATALAYER_INFO_JK_ACTIVE_BALANCER_TYPE* d); String
  get_status_html(); bool renders_own_battery_data() { return true; } }`. Body = 8.13 `print_jk_balancer` lambda
  content: config-error banner (text table from 8.13 `cfg_err[]`), per-balancer blocks, CAB500 block (unconditional),
  BMS cycles count (button comes from `battery_commands`), plus the 8.13 "In use since last boot" limits line.
  Use the data pointer, never `datalayer_extended.jk_active_balancer` directly (double battery).
  No `%` characters in output unless `&#37;` (template processor) - CHECK whether v13 advanced page still runs a
  `%`-template processor; if it streams raw, `%` is fine.
- Cycles reset: `Battery.h` add `virtual bool supports_reset_cycles() { return false; }` and `virtual void reset_cycles() {}`;
  `advanced_battery_html.cpp` `battery_commands` add
  `{"resetCycles", "Reset BMS cycles", "reset the cycle counter to zero? SOH returns to 100 %",
    [](Battery* b){ return b && b->supports_reset_cycles(); }, [](Battery* b){ b->reset_cycles(); }}`.
  Driver `reset_cycles()`: `soc.setCycleCount(0); datalayer_jk->BMS_Cycles = 0; save key; recompute Battery_Calc_Ah; soc.setCapacity(...)`
  (8.13 `UserRequestBMSCyclesReset` block). CHECK that the generic PUT route dispatches to battery 2 by `batteryNum`.

### 6.7 Registration (BATTERIES.h/.cpp, Battery.h)
- `Battery.h`: `JkActiveBalancer = 58,` before `Highest`.
- `BATTERIES.cpp`: `#include "JK-ACTIVE-BALANCER.h"`; `name_for_battery_type`: `case BatteryType::JkActiveBalancer: return JKActiveBalancer::Name;`;
  `create_battery`: `return new JKActiveBalancer();`; `battery_supports_double`: add case; `setup_battery()` battery2
  switch: `battery2 = new JKActiveBalancer(&datalayer.battery2, &datalayer_extended.jk_active_balancer_2,
  &datalayer.system.status.battery2_allowed_contactor_closing, can_config.battery_double);`. Not in `battery_detects_capacity`.
  Triple: not supported (two CAB500 share ID 0x3C2; three interfaces would be needed and the 8.13 code never did it).
- `supported_battery_types()` iterates 0..Highest => the new value appears in the dropdown automatically.

---------------------------------------------------------------------------------------------------------------------
## 7. Settings (v13 Settings page pattern)

### 7.1 Globals (BATTERIES.h extern / BATTERIES.cpp definition) with 8.13 defaults
```
uint32_t user_selected_jk_max_charge_W = 20000;      uint32_t user_selected_jk_max_discharge_W = 20000;
uint16_t user_selected_jk_soc_max_dpct = 990;        uint16_t user_selected_jk_soc_min_dpct = 10;        // 0.1 %
uint16_t user_selected_jk_deadband_mA = 100;                                                           // CAB500 default 0.1 A
uint32_t user_selected_jk_recal_rest_ms = 10000;     uint16_t user_selected_jk_recal_rest_mA = 100;
uint16_t user_selected_jk_recal_soc_bottom_dpct = 100; uint16_t user_selected_jk_recal_soc_top_dpct = 950;
uint16_t user_selected_jk_dis_ramp_dpct = 30;        uint32_t user_selected_jk_dis_min_W = 0;
uint16_t user_selected_jk_chg_ramp_dpct = 50;        uint32_t user_selected_jk_chg_min_W = 4000;
uint8_t  user_selected_jk_max_balancers = 8;
bool     user_selected_jk_reverse_current = false;
bool     user_selected_jk_lfp = false;
uint16_t user_selected_jk_cell_max_mV = 4250; cell_min_mV = 2700; cell_dev_mV = 150; pack_max_mVpc = 4190; pack_min_mVpc = 2900;
uint16_t user_selected_jk_vfull_on_mV = 4130; vfull_off_mV = 4100; vempty_on_mV = 3350; vempty_off_mV = 3400;
uint8_t  user_selected_jk_cells = 24;  user_selected_jk_cells_per_balancer = 12;
bool     user_selected_jk_low_voltage = true;  user_selected_jk_bridge = false;
uint8_t  user_selected_jk_fw_version = 1;  uint16_t user_selected_jk_fw_mask = 0xF000;
```
Floats of 8.13 are stored scaled: percent x10 (`dpct`), amps x1000 (`mA`). Convert back in `setup()`.

### 7.2 NVS keys (<= 15 chars) and `init_stored_settings()` lines
`JKCHGW, JKDCHGW, JKSOCMAX, JKSOCMIN, JKDEADBAND, JKRESTMS, JKRESTMA, JKRCBOT, JKRCTOP, JKDRAMP, JKDMINW, JKCRAMP,
JKCMINW, JKMAXBAL, JKREVCUR(bool), JKLFP(bool), JKCELLMAX, JKCELLMIN, JKCELLDEV, JKPACKMAX, JKPACKMIN, JKVFON,
JKVFOFF, JKVEMON, JKVEMOFF, JKCELLS, JKCPB, JKLOWV(bool), JKBRIDGE(bool), JKFWVER, JKFWMASK, JK_CYCLES, JK_CYCLES2`.
Load: `user_selected_jk_x = settings.getUInt("JKX", default);` / `getBool`. Place the block after the DALY lines.

### 7.3 settings_html.cpp
- Section `<div class="if-jk"> ... </div>` after the DALY section, one `<label>` + `<input>` per setting, `name='JKX'`,
  `value="%JKX%"`, HTML `min/max/step` = 8.13 ranges (chgW/dchgW 1000-200000; SOC max 50-100 step 0.1; SOC min 0-50;
  deadband 0-5 step 0.01; rest ms 1000-3600000; rest A 0-5; bottom 0-50; top 50-100; ramps 0.5-20; min W 0-200000;
  max balancers 1-15; cell/threshold mV 2000-4300; deviation 10-500; cells 1-MAX_AMOUNT_CELLS; cpb 1-24; fwver 1-3).
  Checkboxes: `<input type='checkbox' name='JKREVCUR' value='on' %JKREVCUR% />` (processor returns `"checked"` or `""`).
  FW mask: 15 checkboxes `JKFWB1..15` client-side combined into hidden input `JKFWMASK` by a small script on submit
  (bit 15 = balancer 1), OR 15 bool keys - prefer the hidden input (one key).
  Chemistry: `JKLFP` checkbox; on change, JS fills the 9 voltage inputs with that chemistry's defaults
  (replaces the 8.13 server-side "reset on chemistry change").
- Placeholders in `settings_processor`: `if (var == "JKCHGW") return String(settings.getUInt("JKCHGW", 20000));` ...
  for percent/amps show scaled back (`String(x / 10.0, 1)`), checkboxes return `checked`.
  Form posts scaled values back? No: post human values and convert in `/saveSettings` special-cases (`JKSOCMAX` ->
  `toFloat()*10`, `JKDEADBAND` -> `*1000`, like `BATTPVMAX` does `*10`).
- CSS: `form .if-jk { display: none; }` and `form[data-battery="58"] .if-jk { display: contents; }` next to the
  `if-daly` rules (copy the exact style used there).
- Help: optional entries in `web_data/` help JSON (CHECK format).

### 7.4 webserver.cpp `/saveSettings`
- Add integer keys to `uintSettingNames[]` (those posted as plain integers), bools to `boolSettingNames[]`, and
  special-case the scaled ones in the `if (p->name() == ...)` chain.
- Validation: upstream relies on HTML min/max. The 8.13 cross-field rules (voltage order `cellmin < vemon < vemoff <
  vfoff < vfon < cellmax`, `packmin < packmax`, `socmin < socmax`, bridge xor low-voltage, cells multiple of cpb in
  low-voltage mode, <= 15 balancers) must still be enforced: implement them in the driver `setup()` as `config_error`
  codes (extend `JKConfigError` with `JK_CFG_BAD_VOLTAGE_ORDER = 6`, `JK_CFG_BAD_SOC_WINDOW = 7`) so a bad save never
  runs the battery, AND optionally pre-check in `/saveSettings` before the generic loop (return 400 with text; CHECK
  how the page displays a 400 - 8.13 showed the text inline).

### 7.5 Defaults = 8.13 first-boot values (Appendix A of the user manual): charge/discharge 20000 W, SOC 99/1 %,
deadband 0.1 A, rest 10000 ms / 0.1 A, SOC bottom 10 % / top 95 %, discharge ramp 3 % / 0 W, charge ramp 5 % / 4000 W,
max balancers 8, not reversed, NCM (4250/2700/150, 4190/2900 mV per cell, 4130/4100, 3350/3400), 24 cells, 12 per
balancer, low-voltage on, bridge off, FW version 1, mask 0xF000.

---------------------------------------------------------------------------------------------------------------------
## 8. Coexistence with overlapping upstream features (1:1 port chosen)
1. Chemistry: upstream `BATTCHEM` selector exists for all batteries. JK overrides `info.chemistry` in `setup()` from
   `JKLFP`. Document on the page ("JK uses the LFP box below"). Do not hide BATTCHEM (shared markup) unless trivial.
2. Pack/cell limits: upstream `BATTPVMAX/..` rows are hidden for type 58 (not in `if-cbms`). `user_selected_max_pack_voltage_dV`
   etc. are therefore 0 and unused by JK. JK writes `info.*` limits from its own settings. OK.
3. Charge taper: upstream `filter_charge_taper_soc()` runs after the driver and can only lower the limit. If the user
   enables CHGTAPERSOC, both apply. Note in help text. Leave `mandatory_charge_taper()` false.
4. Deye: upstream `DEYEBYD` (inverter setting) forces reported SOC to 100 %/0 % when a limit is 0 A. The 8.13 driver
   instead reports 50 W in fault/deviation states. Both can be on; 50 W != 0 A so the upstream forcing does not trigger
   in those states - intended 1:1 behaviour. The 8.13 `safety.cpp` "full => 50 W" edit is dropped (decision); upstream
   full handling sets 0 W and, with DEYEBYD, forces SOC 100 %.
5. Discharge voltage cut-off: still driven by `datalayer.battery_settings.max_user_set_discharge_voltage_dV` (default
   3000 = 300 V) even when manual limits are off. Keep 1:1 but put a warning in the JK help text and the manual. CHECK
   whether v13's settings page already validates target voltages against the pack.
6. SOC scaling / min-max percentage: upstream, unchanged, applies to `reported_soc`. JK writes `real_soc`.
7. Contactor control: `CNTCTRL` setting; driver only drives `*allows_contactor_closing`. Same as 8.13.

---------------------------------------------------------------------------------------------------------------------
## 9. Verification
- Build: `pio run -e lilygo_330` (and `-e lilygo_2CAN_330`, `-e stark_330` to catch non-SMALL_FLASH paths). Zero warnings.
- Flash size: compare to baseline; JK + CoulombSOC should add < 25 kB.
- Static: `grep -rn "datalayer.battery.settings\." Software/src/battery/JK-*` must be empty; no `#ifdef JK_ACTIVE_BALANCER`
  anywhere; no `analogRead`, `jk_ct_pin` in the tree.
- Bench (CAN analyzer + 2 balancers + CAB500 at 250 kbps on the add-on CAN):
  1. Settings: battery = JK Active Balancer, BATTCOMM = add-on MCP2515, inverter BYD CAN (+DEYEBYD if Deye), CNTCTRL on,
     capacity Wh, max A, target discharge voltage. Save, reboot.
  2. Serial log shows request frames ID 0x01/0x02 every 200 ms, replies parsed, page shows both Connected, CAB500 Connected.
  3. Pack voltage = multimeter; current sign + while charging (else JKREVCUR); SOC moves; rest recalibration after 10 s idle.
  4. Fault injection: unplug balancer 2 -> after 10 s comms fault -> 50/50 W, BMS_FAULT, event; replug -> recovers.
     Unplug CAB500 -> after 30 s grace + 10 s -> fault. Set an inconsistent layout -> red banner, 0 W, contactors open.
  5. Double battery: second pack on native CAN (`BATT2COMM`), page shows battery 2 blocks, cycles key `JK_CYCLES2`.
  6. OTA from the built `firmware.bin` works (v13 OTA page).

---------------------------------------------------------------------------------------------------------------------
## 10. Nissan Leaf MOD (later)
- Do NOT copy `NISSAN-LEAF-BATTERY-MOD.cpp` (it is an old Leaf driver + MOD edits). v13 `NISSAN-LEAF-BATTERY.cpp` has
  moved on (auto current offset `LEAFAUTOOFS`, `CHGSTARQ`, interlock setting, HTML renderer, double/triple support,
  unit test). Instead: new type `BatteryType::NissanLeafMod = 59`, class `NissanLeafModBattery : public NissanLeafBattery`
  (make the needed members `protected` upstream, or copy the v13 file and apply deltas - prefer subclass if
  `update_values()` can be overridden cleanly).
- MOD deltas to re-apply (from 8.13 `NISSAN-LEAF-BATTERY-MOD.cpp`, search `SILVIU`):
  1. CoulombSOC instance + config (96S NCM fixed: vfull 4150/4120, vempty 3202/3230, deadband 0.5 A, soc 99/5 %,
     same recal/ramp settings as JK), `soc.begin()` on first valid cell data, `soc.update()` in the 100 ms block with
     `BMS_current_A = Battery_current_1` (sign convention: check), `real_soc` from CoulombSOC instead of LBC SOC.
  2. Power limits via `getDischargePowerLimit_Mod/getChargePowerLimit_Mod` (same code as JK; share one static helper),
     max W settings (15000/15000 defaults), SOH from cycle counter (`DEGRADATION_PER_CYCLE 0.0001`), Wh calc
     (`total_capacity_Wh / 403`), cycles key `BMS_Cycles` (8.13 name) -> `LEAFMOD_CYCLES`.
  3. Fault handling with 50 W for Deye, `EVENT_BATTERY_CHG_DISCHG_STOP_REQ`.
  4. Charger: 8.13 `NISSAN-LEAF-CHARGER.cpp` skips the VCM wake (`LEAF_50B`) and the `LEAF_55B` byte-6 write under
     `#ifndef NISSAN_LEAF_BATTERY_MOD`. In v13 chargers are runtime objects: add `if (user_selected_battery_type !=
     BatteryType::NissanLeafMod)` around those two blocks in the v13 charger (CHECK the v13 charger still has them).
  5. 8.13 Leaf MOD web form shares the JK form (`add_mod_settings_form`); in v13 give it its own `if-leafmod` section
     with the subset: power W, SOC window, deadband, recal, ramps (no balancers / chemistry / layout).
  6. `safety.cpp` Leaf-MOD plausibility block (fixed 100 dV margin, 96S) -> `soc_plausible()` override.

---------------------------------------------------------------------------------------------------------------------
## 11. Pitfalls / gotchas
- v13 compiles ALL drivers always: no `#ifdef` guards, every symbol must be unique (prefix free functions or make them
  class statics), every file must compile warning-free on all envs.
- `SMALL_FLASH_DEVICE` (LilyGo T-CAN485): keep HTML strings lean; use `CheckedHtml`; no large tables in flash.
- `BatteryEmulatorSettingsStore` key max 15 chars; `saveUInt` only writes when changed (fine).
- The settings page `<form data-battery=...>` CSS gating hides sections client-side only; the server saves whatever is
  posted. Hidden inputs of other batteries are still posted (CHECK: upstream may disable hidden inputs). Not an issue
  for JK keys.
- `datalayer.battery.info.total_capacity_Wh` default 30000 (was 77000 in 8.13 USER_SETTINGS). Manual must tell the
  installer to set Wh.
- `max_user_set_discharge_voltage_dV` default 300 V: on a 12S pack discharge is 0 W until the installer sets it (same
  trap as 8.13).
- Two CAB500 sensors on one bus both transmit ID 0x3C2 - double battery requires separate CAN interfaces; v13 LilyGo
  T-CAN485 has native + one add-on => exactly two.
- `register_can_receiver` with 250 kbps on the interface the inverter also uses would change the inverter bus speed:
  battery and inverter must be on different interfaces (they are: BATTCOMM add-on, INVCOMM native). CHECK whether v13
  warns on conflicting speeds on one interface.
- `millis()` wrap: 8.13 code uses unsigned subtraction everywhere - keep it that way.
- `-Werror=implicit-fallthrough`: the balancer `switch(data_type)` has `break`s - ok.
- Do not name the files with spaces or lowercase mix: upstream convention `UPPER-KEBAB-BATTERY.cpp` + `-HTML.h`.
- The user manual `JK-Active-Balancer-Manual.docx` (written for 8.13) must be revised after the port: chapters 4
  (compile-time settings) and 5 (pins) change completely; 6-7 move to the Settings page; CAN speed rows and analog CT
  rows disappear.
