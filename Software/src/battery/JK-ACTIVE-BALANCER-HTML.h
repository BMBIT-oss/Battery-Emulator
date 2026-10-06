#ifndef _JK_ACTIVE_BALANCER_HTML_H
#define _JK_ACTIVE_BALANCER_HTML_H

#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/webserver/BatteryHtmlRenderer.h"

class JkActiveBalancerHtmlRenderer : public BatteryHtmlRenderer {
 public:
  JkActiveBalancerHtmlRenderer(DATALAYER_BATTERY_TYPE* battery_dl, DATALAYER_INFO_JK_ACTIVE_BALANCER* dl)
      : battery_dl(battery_dl), jk_dl(dl) {}

  bool renders_own_battery_data() { return true; }

  String get_status_html() {
    CheckedHtml content;
    if (!jk_dl) {
      return content.take();
    }
    content.reserve(1200 + jk_dl->number_of_balancers * 700);

    if (jk_dl->config_error != 0) {
      static const char* cfg_err[] = {
          "",
          "Bridge mode and low-voltage mode cannot both be enabled",
          "Low-voltage mode: number of cells must be a multiple of cells per balancer",
          "Too many balancers needed for this cell count (max 15)",
          "Number of cells or cells per balancer out of range",
          "Firmware version must be 1, 2 or 3",
          "Voltage limits out of order (need min cell < empty on < empty off < full off < full on < max cell, "
          "and pack min < pack max)",
          "SOC window invalid (SOC min must be below SOC max)"};
      const char* txt = (jk_dl->config_error < 8) ? cfg_err[jk_dl->config_error] : "Unknown";
      content += "<div style='background-color: #7A1F1F; padding: 10px; margin-bottom: 10px; border-radius: 20px;'>";
      content += "<h4>BATTERY NOT RUNNING - JK settings are inconsistent: ";
      content += txt;
      content += ". Correct the JK settings on the Settings page and reboot.</h4></div>";
    }

    for (int i = 0; i < jk_dl->number_of_balancers && i < MAX_BALANCERS_WEB; i++) {
      content += "<div style='background-color: #404E57; padding: 10px; margin-bottom: 10px; border-radius: 20px;'>";
      content += "<h4>Balancer #" + String(i + 1) + "</h4>";
      content += "<h4>Status: ";
      content += jk_dl->is_connected[i] ? "<span style='color:lightgreen;'>Connected</span>"
                                        : "<span style='color:red;'>Disconnected</span>";
      content += "</h4><h4>Temperature: " + String(jk_dl->balancer_temperatures_dC[i] / 10.0, 1) + " &deg;C</h4>";
      content += "<h4>Balancing: ";
      if (jk_dl->charge_balancing_status[i]) {
        content += "Charge Balancing";
      } else if (jk_dl->discharge_balancing_status[i]) {
        content += "Discharge Balancing";
      } else {
        content += "Idle";
      }
      content += "</h4><h4>Balance Switch: ";
      content += jk_dl->balance_switch_status[i] ? "ON" : "OFF";
      content += "</h4><h4>Max Balance Current: " + String(jk_dl->max_balance_current_mA[i]) + " mA</h4>";
      content += "<h4>Communication Faults: " + String(jk_dl->fault_counters[i]) + "</h4>";
      content += "<h4>Cell Count Mismatch: ";
      content += jk_dl->cell_count_mismatch_alarm[i] ? "<span style='color:red;'>ALARM</span>"
                                                     : "<span style='color:lightgreen;'>OK</span>";
      content += "</h4><h4>Wire Resistance: ";
      content += jk_dl->wire_resistance_alarm[i] ? "<span style='color:red;'>ALARM</span>"
                                                 : "<span style='color:lightgreen;'>OK</span>";
      content += "</h4></div>";
    }

    // --- CAB500 CAN current sensor ---
    content += "<div style='background-color: #404E57; padding: 10px; margin-bottom: 10px; border-radius: 20px;'>";
    content += "<h4>CAB500 CT Sensor (CAN 0x3C2)</h4><h4>Status: ";
    content += jk_dl->ct_cab500_is_connected ? "<span style='color:lightgreen;'>Connected</span>"
                                             : "<span style='color:red;'>Disconnected</span>";
    content += "</h4><h4>Communication: ";
    content += jk_dl->ct_cab500_comms_fault ? "<span style='color:red;'>FAULT</span>"
                                            : "<span style='color:lightgreen;'>OK</span>";
    content += "</h4><h4>Communication Faults: " + String(jk_dl->ct_cab500_fault_counter) + "</h4>";
    content += "<h4>Hardware: ";
    content += jk_dl->ct_cab500_hardware_error ? "<span style='color:red;'>ERROR (latched)</span>"
                                               : "<span style='color:lightgreen;'>OK</span>";
    content += "</h4>";
    if (jk_dl->ct_cab500_hardware_error) {
      const char* dtc_text;
      switch (jk_dl->ct_cab500_error_code) {
        case 0x41:
          dtc_text = "Overcurrent Detection (Ip > ~520A)";
          break;
        case 0x42:
          dtc_text = "Closed-loop reference voltage over range";
          break;
        case 0x44:
          dtc_text = "Signal not available for more than 100ms";
          break;
        case 0x46:
          dtc_text = "Supply voltage out of range";
          break;
        default:
          dtc_text = "Unknown code";
          break;
      }
      content += "<h4>DTC: <span style='color:red;'>0x" + String(jk_dl->ct_cab500_error_code, HEX) + " - ";
      content += dtc_text;
      content += "</span></h4>";
    }
    content += "</div>";

    // --- Coulomb counter ---
    content += "<h4>BMS Cycles: " + String(jk_dl->BMS_Cycles) + "</h4>";
    content += "<h4>Calculated capacity: " + String(jk_dl->Calc_capacity_Wh) + " Wh</h4>";

    // Limits the driver derived from the settings at boot
    content += "<p style='color:#ccc'>In use since last boot: pack max " +
               String(battery_dl->info.max_design_voltage_dV / 10.0, 1) + " V, pack min " +
               String(battery_dl->info.min_design_voltage_dV / 10.0, 1) + " V, cell max " +
               String(battery_dl->info.max_cell_voltage_mV) + " mV, cell min " +
               String(battery_dl->info.min_cell_voltage_mV) + " mV, max deviation " +
               String(battery_dl->info.max_cell_voltage_deviation_mV) + " mV.</p>";

    return content.take();
  }

 private:
  DATALAYER_BATTERY_TYPE* battery_dl;
  DATALAYER_INFO_JK_ACTIVE_BALANCER* jk_dl;
};

#endif
