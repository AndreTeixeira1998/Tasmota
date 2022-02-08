/*
  xnrg_12_solaxX1.ino - Solax X1 inverter RS485 support for Tasmota

  Copyright (C) 2021 by Pablo Zerón
  Copyright (C) 2022 by Stefan Wershoven

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_ENERGY_SENSOR
#ifdef USE_SOLAX_X1
/*********************************************************************************************\
 * Solax X1 Inverter
\*********************************************************************************************/

#define XNRG_12            12

#ifndef SOLAXX1_SPEED
#define SOLAXX1_SPEED      9600      // default solax rs485 speed
#endif

#define INVERTER_ADDRESS   0x0A

#define D_SOLAX_X1         "SolaxX1"

#include <TasmotaSerial.h>

union {
  uint32_t ErrMessage;
  struct {
    //BYTE0
    uint8_t TzProtectFault:1;//0
    uint8_t MainsLostFault:1;//1
    uint8_t GridVoltFault:1;//2
    uint8_t GridFreqFault:1;//3
    uint8_t PLLLostFault:1;//4
    uint8_t BusVoltFault:1;//5
    uint8_t ErrBit06:1;//6
    uint8_t OciFault:1;//7
    //BYTE1
    uint8_t Dci_OCP_Fault:1;//8
    uint8_t ResidualCurrentFault:1;//9
    uint8_t PvVoltFault:1;//10
    uint8_t Ac10Mins_Voltage_Fault:1;//11
    uint8_t IsolationFault:1;//12
    uint8_t TemperatureOverFault:1;//13
    uint8_t FanFault:1;//14
    uint8_t ErrBit15:1;//15
    //BYTE2
    uint8_t SpiCommsFault:1;//16
    uint8_t SciCommsFault:1;//17
    uint8_t ErrBit18:1;//18
    uint8_t InputConfigFault:1;//19
    uint8_t EepromFault:1;//20
    uint8_t RelayFault:1;//21
    uint8_t SampleConsistenceFault:1;//22
    uint8_t ResidualCurrent_DeviceFault:1;//23
    //BYTE3
    uint8_t ErrBit24:1;//24
    uint8_t ErrBit25:1;//25
    uint8_t ErrBit26:1;//26
    uint8_t ErrBit27:1;//27
    uint8_t ErrBit28:1;//28
    uint8_t DCI_DeviceFault:1;//29
    uint8_t OtherDeviceFault:1;//30
    uint8_t ErrBit31:1;//31
  };
} ErrCode;

const char kSolaxMode[] PROGMEM = D_OFF "|" D_SOLAX_MODE_0 "|" D_SOLAX_MODE_1 "|" D_SOLAX_MODE_2 "|" D_SOLAX_MODE_3 "|"
  D_SOLAX_MODE_4 "|" D_SOLAX_MODE_5 "|" D_SOLAX_MODE_6;

const char kSolaxError[] PROGMEM =
  D_SOLAX_ERROR_0 "|" D_SOLAX_ERROR_1 "|" D_SOLAX_ERROR_2 "|" D_SOLAX_ERROR_3 "|" D_SOLAX_ERROR_4 "|" D_SOLAX_ERROR_5 "|"
  D_SOLAX_ERROR_6 "|" D_SOLAX_ERROR_7 "|" D_SOLAX_ERROR_8;

struct SOLAXX1 {
  int16_t temperature = 0;
  float energy_today = 0;
  float dc1_voltage = 0;
  float dc2_voltage = 0;
  float dc1_current = 0;
  float dc2_current = 0;
  uint32_t runtime_total = 0;
  float dc1_power = 0;
  float dc2_power = 0;
  int16_t runMode = 0;
  uint32_t errorCode = 0;
} solaxX1;

uint8_t header[2] = {0xAA, 0x55};
uint8_t source[2] = {0x00, 0x00};
uint8_t destination[2] = {0x00, 0x00};
uint8_t controlCode[1] = {0x00};
uint8_t functionCode[1] = {0x00};
uint8_t dataLength[1] = {0x00};
uint8_t data[16] = {0};

TasmotaSerial *solaxX1Serial;
uint8_t message[30];
bool AddressAssigned = true;
uint8_t solaxX1_send_retry = 20;
uint8_t solaxX1_queryData_count = 0;
uint8_t solaxX1_QueryID_count = 240;
uint8_t solaxX1SerialNumber[16] = {0x6e, 0x2f, 0x61}; // "n/a"

/*********************************************************************************************/

void solaxX1_RS485Send(uint16_t msgLen)
{
  memcpy(message, header, 2);
  memcpy(message + 2, source, 2);
  memcpy(message + 4, destination, 2);
  memcpy(message + 6, controlCode, 1);
  memcpy(message + 7, functionCode, 1);
  memcpy(message + 8, dataLength, 1);
  memcpy(message + 9, data, sizeof(data));
  uint16_t crc = solaxX1_calculateCRC(message, msgLen); // calculate out crc bytes

  while (solaxX1Serial->available() > 0) { // read serial if any old data is available
    solaxX1Serial->read();
  }

  if (PinUsed(GPIO_SOLAXX1_RTS)) {
    digitalWrite(Pin(GPIO_SOLAXX1_RTS), HIGH);
  }
  solaxX1Serial->flush();
  solaxX1Serial->write(message, msgLen);
  solaxX1Serial->write(highByte(crc));
  solaxX1Serial->write(lowByte(crc));
  solaxX1Serial->flush();
  if (PinUsed(GPIO_SOLAXX1_RTS)) {
    digitalWrite(Pin(GPIO_SOLAXX1_RTS), LOW);
  }

  AddLogBuffer(LOG_LEVEL_DEBUG_MORE, message, msgLen);
}

bool solaxX1_RS485Receive(uint8_t *value)
{
  uint8_t len = 0;

  while (solaxX1Serial->available() > 0) {
    value[len++] = (uint8_t)solaxX1Serial->read();
  }

  AddLogBuffer(LOG_LEVEL_DEBUG_MORE, value, len);

  uint16_t crc = solaxX1_calculateCRC(value, len - 2); // calculate out crc bytes

  return !(value[len - 1] == lowByte(crc) && value[len - 2] == highByte(crc));
}

uint16_t solaxX1_calculateCRC(uint8_t *bExternTxPackage, uint8_t bLen)
{
  uint8_t i;
  uint16_t wChkSum;
  wChkSum = 0;

  for (i = 0; i < bLen; i++) {
    wChkSum = wChkSum + bExternTxPackage[i];
  }
  return wChkSum;
}

void solaxX1_QueryOfflineInverters(void)
{
  source[0] = 0x01;
  destination[0] = 0x00;
  destination[1] = 0x00;
  controlCode[0] = 0x10;
  functionCode[0] = 0x00;
  dataLength[0] = 0x00;
  solaxX1_RS485Send(9);
}

void solaxX1_SendInverterAddress(void)
{
  source[0] = 0x00;
  destination[0] = 0x00;
  destination[1] = 0x00;
  controlCode[0] = 0x10;
  functionCode[0] = 0x01;
  dataLength[0] = 0x0F;
  data[14] = INVERTER_ADDRESS; // Inverter Address, It must be unique in case of more inverters in the same rs485 net.
  solaxX1_RS485Send(24);
}

void solaxX1_QueryLiveData(void)
{
  source[0] = 0x01;
  destination[0] = 0x00;
  destination[1] = INVERTER_ADDRESS;
  controlCode[0] = 0x11;
  functionCode[0] = 0x02;
  dataLength[0] = 0x00;
  solaxX1_RS485Send(9);
}

void solaxX1_QueryIDData(void)
{
  source[0] = 0x01;
  destination[0] = 0x00;
  destination[1] = INVERTER_ADDRESS;
  controlCode[0] = 0x11;
  functionCode[0] = 0x03;
  dataLength[0] = 0x00;
  solaxX1_RS485Send(9);
}

uint8_t solaxX1_ParseErrorCode(uint32_t code){
  ErrCode.ErrMessage = code;

  if (code == 0) return 0;
  if (ErrCode.MainsLostFault) return 1;
  if (ErrCode.GridVoltFault) return 2;
  if (ErrCode.GridFreqFault) return 3;
  if (ErrCode.PvVoltFault) return 4;
  if (ErrCode.IsolationFault) return 5;
  if (ErrCode.TemperatureOverFault) return 6;
  if (ErrCode.FanFault) return 7;
  if (ErrCode.OtherDeviceFault) return 8;
  return 0;
}

/*********************************************************************************************/

void solaxX1250MSecond(void) // Every 250 milliseconds
{
  uint8_t value[70] = {0};
  uint8_t i;

  if (solaxX1Serial->available()) {
    if (solaxX1_RS485Receive(value)) { // CRC-error -> no further action
      DEBUG_SENSOR_LOG(PSTR("SX1: Data response CRC error"));
      return;
    }
  
    solaxX1_send_retry = 20; // Inverter is responding

    if (value[0] != 0xAA || value[1] != 0x55) { // Check for header
      DEBUG_SENSOR_LOG(PSTR("SX1: Check for header failed"));
      return;
    }

    if (value[6] == 0x11 && value[7] == 0x82) { // received "Response for query (live data)"
      Energy.data_valid[0] = 0;

      solaxX1.temperature =    (value[9] << 8) | value[10]; // Temperature
      solaxX1.energy_today =   (float)((value[11] << 8) | value[12]) * 0.1f; // Energy Today
      solaxX1.dc1_voltage =    (float)((value[13] << 8) | value[14]) * 0.1f; // PV1 Voltage
      solaxX1.dc2_voltage =    (float)((value[15] << 8) | value[16]) * 0.1f; // PV2 Voltage
      solaxX1.dc1_current =    (float)((value[17] << 8) | value[18]) * 0.1f; // PV1 Current
      solaxX1.dc2_current =    (float)((value[19] << 8) | value[20]) * 0.1f; // PV2 Current
      Energy.current[0] =      (float)((value[21] << 8) | value[22]) * 0.1f; // AC Current
      Energy.voltage[0] =      (float)((value[23] << 8) | value[24]) * 0.1f; // AC Voltage
      Energy.frequency[0] =    (float)((value[25] << 8) | value[26]) * 0.01f; // AC Frequency
      Energy.active_power[0] = (float)((value[27] << 8) | value[28]); // AC Power
      //temporal = (float)((value[29] << 8) | value[30]) * 0.1f; // Not Used
      Energy.import_active[0] = (float)((value[31] << 24) | (value[32] << 16) | (value[33] << 8) | value[34]) * 0.1f; // Energy Total
      solaxX1.runtime_total =  ((value[35] << 24) | (value[36] << 16) | (value[37] << 8) | value[38]); // Work Time Total
      solaxX1.runMode =        (value[39] << 8) | value[40]; // Work mode
      //temporal = (float)((value[41] << 8) | value[42]); // Grid voltage fault value 0.1V
      //temporal = (float)((value[43] << 8) | value[44]); // Gird frequency fault value 0.01Hz
      //temporal = (float)((value[45] << 8) | value[46]); // Dc injection fault value 1mA
      //temporal = (float)((value[47] << 8) | value[48]); // Temperature fault value
      //temporal = (float)((value[49] << 8) | value[50]); // Pv1 voltage fault value 0.1V
      //temporal = (float)((value[51] << 8) | value[52]); // Pv2 voltage fault value 0.1V
      //temporal = (float)((value[53] << 8) | value[54]); // GFC fault value
      solaxX1.errorCode =      (value[58] << 24) | (value[57] << 16) | (value[56] << 8) | value[55]; // Error Code

      solaxX1.dc1_power = solaxX1.dc1_voltage * solaxX1.dc1_current;
      solaxX1.dc2_power = solaxX1.dc2_voltage * solaxX1.dc2_current;

      EnergyUpdateTotal();  // 484.708 kWh
      DEBUG_SENSOR_LOG(PSTR("SX1: received live data"));
      return;
    } // end received "Response for query (live data)"

    if (value[6] == 0x11 && value[7] == 0x83) { // received "Response for query (ID data)"
      for (i = 49; i <= 62; i++) { // get "real" serial number
        solaxX1SerialNumber[i - 49] = value[i];
      }
      AddLog(LOG_LEVEL_INFO, PSTR("SX1: Inverter serial number: %s"),(char*)solaxX1SerialNumber);
      DEBUG_SENSOR_LOG(PSTR("SX1: received ID data"));
      return;
   } // end received "Response for query (ID data)"

    if (value[6] == 0x10 && value[7] == 0x80) { // received "register request"
      solaxX1_queryData_count = 5; // give time for next query
      for (i = 9; i <= 22; i++) { // store serial number for register
        data[i - 9] = value[i];
      }
      DEBUG_SENSOR_LOG(PSTR("SX1: received register request and send register address"));
      solaxX1_SendInverterAddress(); // "send register address"
      return;
    }

    if (value[6] == 0x10 && value[7] == 0x81 && value[9] == 0x06) { // received "address confirm (ACK)"
      solaxX1_queryData_count = 5; // give time for next query
      AddressAssigned = true;
      DEBUG_SENSOR_LOG(PSTR("SX1: received \"address confirm (ACK)\""));
      return;
    }

  } // end solaxX1Serial->available()

//  DEBUG_SENSOR_LOG(PSTR("SX1: AddressAssigned: %d, solaxX1_queryData_count: %d, solaxX1_send_retry: %d, solaxX1_QueryID_count: %d"), AddressAssigned, solaxX1_queryData_count, solaxX1_send_retry, solaxX1_QueryID_count);
  if (AddressAssigned) {
    if (!solaxX1_queryData_count) { // normal periodically query
      solaxX1_queryData_count = 5;
      if (solaxX1_QueryID_count) { // normal live query
        DEBUG_SENSOR_LOG(PSTR("SX1: Send periodically live query"));
        solaxX1_QueryLiveData();
      } else { // normal ID query
        DEBUG_SENSOR_LOG(PSTR("SX1: Send periodically ID query"));
        solaxX1_QueryIDData();
      }
      solaxX1_QueryID_count++; // query ID every 256th time
    }  // end normal periodically query
    solaxX1_queryData_count--;
    if (!solaxX1_send_retry) { // Inverter went "off"
      solaxX1_send_retry = 20;
      DEBUG_SENSOR_LOG(PSTR("SX1: Inverter went \"off\""));
      Energy.data_valid[0] = ENERGY_WATCHDOG;
      solaxX1.temperature = solaxX1.dc1_voltage = solaxX1.dc2_voltage = solaxX1.dc1_current = solaxX1.dc2_current = solaxX1.dc1_power = 0;
      solaxX1.dc2_power = Energy.current[0] = Energy.voltage[0] = Energy.frequency[0] = Energy.active_power[0] = 0;
      solaxX1.runMode = -1; // off(line)
      AddressAssigned = false;
    } // end Inverter went "off"
  } else { // sent query for inverters in offline status
    if (!solaxX1_send_retry) {
      solaxX1_send_retry = 20;
      DEBUG_SENSOR_LOG(PSTR("SX1: Sent query for inverters in offline state"));
      solaxX1_QueryOfflineInverters();
    }
  }
  solaxX1_send_retry--;

return;  
}

void solaxX1SnsInit(void)
{
  AddLog(LOG_LEVEL_INFO, PSTR("SX1: Init - RX-pin: %d, TX-pin: %d, RTS-pin: %d"), Pin(GPIO_SOLAXX1_RX), Pin(GPIO_SOLAXX1_TX), Pin(GPIO_SOLAXX1_RTS));
  solaxX1Serial = new TasmotaSerial(Pin(GPIO_SOLAXX1_RX), Pin(GPIO_SOLAXX1_TX), 1);
  if (solaxX1Serial->begin(SOLAXX1_SPEED)) {
    if (solaxX1Serial->hardwareSerial()) { ClaimSerial(); }
  } else {
    TasmotaGlobal.energy_driver = ENERGY_NONE;
  }
  if (PinUsed(GPIO_SOLAXX1_RTS)) {
    pinMode(Pin(GPIO_SOLAXX1_RTS), OUTPUT);
  }
}

void solaxX1DrvInit(void)
{
  if (PinUsed(GPIO_SOLAXX1_RX) && PinUsed(GPIO_SOLAXX1_TX)) {
    TasmotaGlobal.energy_driver = XNRG_12;
  }
}

#ifdef USE_WEBSERVER
const char HTTP_SNS_solaxX1_DATA1[] PROGMEM =
    "{s}" D_SOLAX_X1 " " D_SOLAR_POWER "{m}%s " D_UNIT_WATT "{e}"
    "{s}" D_SOLAX_X1 " " D_PV1_VOLTAGE "{m}%s " D_UNIT_VOLT "{e}"
    "{s}" D_SOLAX_X1 " " D_PV1_CURRENT "{m}%s " D_UNIT_AMPERE "{e}"
    "{s}" D_SOLAX_X1 " " D_PV1_POWER "{m}%s " D_UNIT_WATT "{e}";
#ifdef SOLAXX1_PV2
const char HTTP_SNS_solaxX1_DATA2[] PROGMEM =
    "{s}" D_SOLAX_X1 " " D_PV2_VOLTAGE "{m}%s " D_UNIT_VOLT "{e}"
    "{s}" D_SOLAX_X1 " " D_PV2_CURRENT "{m}%s " D_UNIT_AMPERE "{e}"
    "{s}" D_SOLAX_X1 " " D_PV2_POWER "{m}%s " D_UNIT_WATT "{e}";
#endif
const char HTTP_SNS_solaxX1_DATA3[] PROGMEM =
    "{s}" D_SOLAX_X1 " " D_UPTIME "{m}%s " D_UNIT_HOUR "{e}"
    "{s}" D_SOLAX_X1 " " D_STATUS "{m}%s"
    "{s}" D_SOLAX_X1 " " D_ERROR "{m}%s"
    "{s}" D_SOLAX_X1 " Inverter SN{m}%s";
#endif // USE_WEBSERVER

void solaxX1Show(bool json)
{
  char solar_power[33];
  dtostrfd(solaxX1.dc1_power + solaxX1.dc2_power, Settings->flag2.wattage_resolution, solar_power);
  char pv1_voltage[33];
  dtostrfd(solaxX1.dc1_voltage, Settings->flag2.voltage_resolution, pv1_voltage);
  char pv1_current[33];
  dtostrfd(solaxX1.dc1_current, Settings->flag2.current_resolution, pv1_current);
  char pv1_power[33];
  dtostrfd(solaxX1.dc1_power, Settings->flag2.wattage_resolution, pv1_power);
#ifdef SOLAXX1_PV2
  char pv2_voltage[33];
  dtostrfd(solaxX1.dc2_voltage, Settings->flag2.voltage_resolution, pv2_voltage);
  char pv2_current[33];
  dtostrfd(solaxX1.dc2_current, Settings->flag2.current_resolution, pv2_current);
  char pv2_power[33];
  dtostrfd(solaxX1.dc2_power, Settings->flag2.wattage_resolution, pv2_power);
#endif
  char runtime[33];
  dtostrfd(solaxX1.runtime_total, 0, runtime);
  char status[33];
  GetTextIndexed(status, sizeof(status), solaxX1.runMode + 1, kSolaxMode);

  if (json) {
    ResponseAppend_P(PSTR(",\"" D_JSON_SOLAR_POWER "\":%s,\"" D_JSON_PV1_VOLTAGE "\":%s,\"" D_JSON_PV1_CURRENT "\":%s,\"" D_JSON_PV1_POWER "\":%s"),
                                solar_power, pv1_voltage, pv1_current, pv1_power);
#ifdef SOLAXX1_PV2
    ResponseAppend_P(PSTR(",\"" D_JSON_PV2_VOLTAGE "\":%s,\"" D_JSON_PV2_CURRENT "\":%s,\"" D_JSON_PV2_POWER "\":%s"),
                                pv2_voltage, pv2_current, pv2_power);
#endif
    ResponseAppend_P(PSTR(",\"" D_JSON_TEMPERATURE "\":%d,\"" D_JSON_RUNTIME "\":%s,\"" D_JSON_STATUS "\":\"%s\",\"" D_JSON_ERROR "\":%d"),
                                solaxX1.temperature, runtime, status, solaxX1.errorCode);

#ifdef USE_DOMOTICZ
    // Avoid bad temperature report at beginning of the day (spikes of 1200 celsius degrees)
    if (0 == TasmotaGlobal.tele_period && solaxX1.temperature < 100) { DomoticzSensor(DZ_TEMP, solaxX1.temperature); }
#endif // USE_DOMOTICZ

#ifdef USE_WEBSERVER
  } else {
    WSContentSend_PD(HTTP_SNS_solaxX1_DATA1, solar_power, pv1_voltage, pv1_current, pv1_power);
#ifdef SOLAXX1_PV2
    WSContentSend_PD(HTTP_SNS_solaxX1_DATA2, pv2_voltage, pv2_current, pv2_power);
#endif
    WSContentSend_Temp(D_SOLAX_X1, solaxX1.temperature);
    char errorCodeString[33];
    WSContentSend_PD(HTTP_SNS_solaxX1_DATA3, runtime, status,
      GetTextIndexed(errorCodeString, sizeof(errorCodeString), solaxX1_ParseErrorCode(solaxX1.errorCode), kSolaxError),
      solaxX1SerialNumber);
#endif  // USE_WEBSERVER
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xnrg12(uint8_t function)
{
  bool result = false;

  switch (function) {
    case FUNC_EVERY_250_MSECOND:
      solaxX1250MSecond();
      break;
    case FUNC_JSON_APPEND:
      solaxX1Show(1);
      break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_SENSOR:
      solaxX1Show(0);
      break;
#endif  // USE_WEBSERVER
    case FUNC_INIT:
      solaxX1SnsInit();
      break;
    case FUNC_PRE_INIT:
      solaxX1DrvInit();
      break;
  }
  return result;
}

#endif  // USE_SOLAX_X1_NRG
#endif  // USE_ENERGY_SENSOR
