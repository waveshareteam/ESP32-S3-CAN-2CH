#include <Arduino.h>
#include "WS_WIFI.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <cstring>
#include <stdarg.h>

// The WiFi SSID and password for STA mode
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

char ipStr[16];
WebServer server(80);

static void WIFI_PrintSTAIP(const char *prefix)
{
  IPAddress myIP = WiFi.localIP();
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
  Serial.printf("%s%s\r\n", prefix, ipStr);
}

static void WIFI_OnEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      WIFI_PrintSTAIP("WiFi connected, IP: ");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("WiFi disconnected, reason: %d\r\n", info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

#define WEB_CAN_TX_QUEUE_LEN 12
#define WEB_CAN_TX_DATA_MAX 255

typedef struct {
  uint8_t channel;
  uint32_t can_id;
  uint8_t extd;
  uint8_t len;
  uint8_t data[WEB_CAN_TX_DATA_MAX];
} WebCanTxItem;

static QueueHandle_t WebCanTxQueue = NULL;
static TaskHandle_t WebCanTxTaskHandle = NULL;

static bool enqueueWebCanTx(uint8_t channel, const CAN_Receive *canData)
{
  if (WebCanTxQueue == NULL || canData == NULL) {
    return false;
  }
  if (canData->DataLength > WEB_CAN_TX_DATA_MAX) {
    Serial.printf("CAN CH%u Web TX data too long:%u bytes\r\n", (unsigned int)channel, (unsigned int)canData->DataLength);
    return false;
  }

  WebCanTxItem item = {0};
  item.channel = channel;
  item.can_id = canData->CAN_ID;
  item.extd = canData->CAN_extd;
  item.len = (uint8_t)canData->DataLength;
  if (item.len > 0 && canData->Read_Data != NULL) {
    memcpy(item.data, canData->Read_Data, item.len);
  }

  return xQueueSend(WebCanTxQueue, &item, 0) == pdTRUE;
}

static void WebCanTxTask(void *parameter)
{
  WebCanTxItem item;
  while (1) {
    if (xQueueReceive(WebCanTxQueue, &item, portMAX_DELAY) == pdTRUE) {
      if (item.channel == 2) {
        send_can2_message(item.can_id, item.data, item.len, item.extd);
      } else {
        send_message(item.can_id, item.data, item.len, item.extd);
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void handleRoot() {
  String myhtmlPage = R"rawliteral(
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32-S3-CAN-2CH</title>
  <style>
    body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }
    .header { text-align: center; padding: 20px 0; background-color: #333; color: #fff; margin-bottom: 20px; }
    .container { max-width: 600px; margin: 10px auto; padding: 20px; background-color: #fff; border-radius: 5px; box-shadow: 0 0 5px rgba(0, 0, 0, 0.3); }
    .form-group label { display: block; font-weight: bold; margin-top: 10px; }
    input, select, textarea { margin: 4px 0 10px; padding: 5px; border: 1px solid #ccc; border-radius: 3px; }
    textarea { width: 500px; height: 100px; resize: vertical; word-break: break-word; }
    .btn { padding: 5px 10px; background-color: #333; color: #fff; font-size: 14px; font-weight: bold; border: none; border-radius: 3px; cursor: pointer; }
    .btn:disabled { background-color: #777; cursor: wait; }
    .receive-title { display: flex; align-items: center; gap: 10px; margin-top: 10px; }
    .receive-title label { margin-top: 0; }
    nav { margin: 15px 0; text-align: center; }
    nav a { padding: 10px 50px; background-color: #333; color: white; text-decoration: none; font-weight: bold; border-radius: 5px; }
    nav a.SerialControlActive { background-color: #fff; color: #333; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3), 0 1px 3px rgba(0, 0, 0, 0.1); transform: translateY(-4px); transition: all 0.2s ease-in-out; }
    .wide { width: 500px; }
  </style>
</head>
<body>
  <script>
    function formatHexInput(input) {
      var raw = input.value.replace(/[^0-9a-fA-F]/g, '');
      var spaced = raw.match(/.{1,2}/g);
      input.value = spaced ? spaced.join(' ') : '';
    }
    var requestBusy = {};
    var readBusy = {};
    function sendRequest(path, payload, button) {
      if (requestBusy[path]) return;
      requestBusy[path] = true;
      if (button) button.disabled = true;
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (xhr.readyState === 4) {
          requestBusy[path] = false;
          if (button) button.disabled = false;
        }
      };
      xhr.onerror = function() {
        requestBusy[path] = false;
        if (button) button.disabled = false;
      };
      xhr.open('GET', path + '?data=' + encodeURIComponent(payload), true);
      xhr.send();
    }
    function setRate(selectId, path, button) {
      var rate = document.getElementById(selectId).value;
      if (path === '/CANSetAllRate') {
        document.getElementById('CAN2Rate').value = rate;
        document.getElementById('CANUpdateRate').value = rate;
      }
      sendRequest(path, 'CAN Rate: ' + rate + '  \nWeb End\n', button);
    }
    function sendCan(idPrefix, dataId, path, button) {
      var canID = document.getElementById(idPrefix + 'id').value;
      var canExtd = document.getElementById(idPrefix + 'extd').value;
      var canData = document.getElementById(dataId).value;
      var payload =
        'CAN ID: 0x' + canID.toUpperCase() + '  \n' +
        'CAN Extd: ' + canExtd + '  \n' +
        'CAN Data: ' + canData + '  \n' +
        'Web End\n';
      sendRequest(path, payload, button);
    }
    function readCan(path, textAreaId) {
      if (readBusy[path]) return;
      readBusy[path] = true;
      var xhr = new XMLHttpRequest();
      xhr.open('GET', path, true);
      xhr.onreadystatechange = function() {
        if (xhr.readyState === 4) readBusy[path] = false;
        if (xhr.readyState === 4 && xhr.status === 200) {
          var dataArray = JSON.parse(xhr.responseText);
          if (dataArray.length > 0 && dataArray[0] !== '') {
            var textarea = document.getElementById(textAreaId);
            var isAtBottom = (textarea.scrollHeight - textarea.scrollTop - textarea.clientHeight) < 10;
            textarea.value += dataArray;
            if (isAtBottom) textarea.scrollTop = textarea.scrollHeight;
          }
        }
      };
      xhr.onerror = function() { readBusy[path] = false; };
      xhr.send();
    }
    function clearCanLog(textAreaId, path) {
      document.getElementById(textAreaId).value = '';
      var xhr = new XMLHttpRequest();
      xhr.open('GET', path, true);
      xhr.send();
    }
    function readConfig() {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', '/getRateConfig', true);
      xhr.onreadystatechange = function() {
        if (xhr.readyState === 4 && xhr.status === 200) {
          var data = JSON.parse(xhr.responseText);
          if (data.can2_rate !== undefined) document.getElementById('CAN2Rate').value = data.can2_rate;
          if (data.can_rate !== undefined) document.getElementById('CANUpdateRate').value = data.can_rate;
        }
      };
      xhr.send();
    }
    window.addEventListener('load', function() {
      readConfig();
      setInterval(function(){ readCan('/getCAN2Data', 'CAN2ReadData'); }, 500);
      setInterval(function(){ readCan('/getCANData', 'CANReadData'); }, 500);
    });
  </script>
  <div class="header"><h1>ESP32-S3-CAN-2CH</h1></div>
  <nav>
    <a href="/" id="SerialControlLink" class="SerialControlActive">Serial Control</a>
    <a href="/RTC_Event" id="rtcEventLink" class="rtcEventActive">RTC Event</a>
  </nav>
  <div class="container">
    <div class="form-group">
      <label>CAN CH1 (ESP32-S3 TWAI):</label>
      <select id="CANUpdateRate" style="width: 120px; text-align: left;">
        <option value="25">25Kbps</option>
        <option value="50">50Kbps</option>
        <option value="100">100Kbps</option>
        <option value="125">125Kbps</option>
        <option value="250">250Kbps</option>
        <option value="500">500Kbps</option>
        <option value="800">800Kbps</option>
        <option value="1000">1Mbps</option>
      </select>
      <button class="btn" onclick="setRate('CANUpdateRate', '/CANSetRate', this)">Set Rate</button>
      <button class="btn" onclick="setRate('CANUpdateRate', '/CANSetAllRate', this)">Set Both Rate</button>
      <label>CAN CH1 Send Data:</label>
      <span>CAN ID : 0x</span>
      <input type="text" id="CANid" style="width: 160px; text-align: left;" value="00000000">
      <select id="CANextd" style="width: 200px; text-align: left;">
        <option value="0">Standard frames</option>
        <option value="1">Extended frames</option>
      </select>
      <input type="text" id="CANSendData" class="wide" value="12" oninput="formatHexInput(this)">
      <button class="btn" onclick="sendCan('CAN', 'CANSendData', '/CANSend', this)">Send Data</button>
      <div class="receive-title">
        <label>CAN CH1 Receive Data:</label>
        <button class="btn" onclick="clearCanLog('CANReadData', '/clearCANData')">Clear Log</button>
      </div>
      <textarea id="CANReadData" placeholder="No data was received..."></textarea>
    </div>
  </div>
  <div class="container">
    <div class="form-group">
      <label>CAN CH2 (XL2515):</label>
      <select id="CAN2Rate" style="width: 120px; text-align: left;">
        <option value="25">25Kbps</option>
        <option value="50">50Kbps</option>
        <option value="100">100Kbps</option>
        <option value="125">125Kbps</option>
        <option value="250">250Kbps</option>
        <option value="500">500Kbps</option>
        <option value="800">800Kbps</option>
        <option value="1000">1Mbps</option>
      </select>
      <button class="btn" onclick="setRate('CAN2Rate', '/CAN2SetRate', this)">Set Rate</button>
      <button class="btn" onclick="setRate('CAN2Rate', '/CANSetAllRate', this)">Set Both Rate</button>
      <label>CAN CH2 Send Data:</label>
      <span>CAN ID : 0x</span>
      <input type="text" id="CAN2id" style="width: 160px; text-align: left;" value="00000000">
      <select id="CAN2extd" style="width: 200px; text-align: left;">
        <option value="0">Standard frames</option>
        <option value="1">Extended frames</option>
      </select>
      <input type="text" id="CAN2SendData" class="wide" value="12" oninput="formatHexInput(this)">
      <button class="btn" onclick="sendCan('CAN2', 'CAN2SendData', '/CAN2Send', this)">Send Data</button>
      <div class="receive-title">
        <label>CAN CH2 Receive Data:</label>
        <button class="btn" onclick="clearCanLog('CAN2ReadData', '/clearCAN2Data')">Clear Log</button>
      </div>
      <textarea id="CAN2ReadData" placeholder="No data was received..."></textarea>
    </div>
  </div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", myhtmlPage);
  Serial.printf("The user visited the home page\r\n");
  return;
}
void handleRTCPage() {      
    String rtcPage = String("") + 
    "<html>" + 
    "<head>" + 
    "    <meta charset=\"utf-8\">" + 
    "    <title>ESP32-S3-CAN-2CH</title>" + 
    "    <style>" + 
    "        body {" + 
    "            font-family: Arial, sans-serif;" + 
    "            background-color: #f0f0f0;" + 
    "            margin: 0;" + 
    "            padding: 0;" + 
    "        }" + 
    "        .header {" + 
    "            text-align: center;" + 
    "            padding: 20px 0;" + 
    "            background-color: #333;" + 
    "            color: #fff;" + 
    "            margin-bottom: 20px;" + 
    "        }" + 
    "        .container {" + 
    "            max-width: 600px;" + 
    "            margin: 10px auto;" + 
    "            padding: 20px;" + 
    "            background-color: #fff;" + 
    "            border-radius: 5px;" + 
    "            box-shadow: 0 0 5px rgba(0, 0, 0, 0.3);" + 
    "        }" + 
    "        .form-group {" + 
    "            margin-bottom: 15px;" + 
    "        }" + 
    "        .form-group label {" + 
    "            display: block;" + 
    "            font-weight: bold;" + 
    "        }" + 
    "        .form-group input {" + 
    "            width: 80px;" + 
    "            height: 25px;" + 
    "            padding: 4px;" + 
    "            margin-top: 5px;" + 
    "            border: 1px solid #ddd;" + 
    "            border-radius: 4px;" + 
    "            box-sizing: border-box;" + 
    "            text-align: right; " + 
    "        }" + 
    "        .form-group select {" + 
    "            width: 80px;" + 
    "            height: 25px;" + 
    "            padding: 4px;" + 
    "            margin-top: 5px;" + 
    "            border: 1px solid #ddd;" + 
    "            border-radius: 4px;" + 
    "            box-sizing: border-box;" + 
    "            text-align: right; " + 
    "        }" + 
    "        .form-group .btn {" + 
    "            padding: 10px 20px;" + 
    "            background-color: #333;" + 
    "            color: white;" + 
    "            border: none;" + 
    "            border-radius: 5px;" + 
    "            cursor: pointer;" + 
    "        }" + 
    "        .form-group .btn:hover {" + 
    "            background-color: #555;" + 
    "        }" + 
    "        .Events{"+
    "            font-size: 13px;"+
    "            word-wrap: break-word;"+
    "            overflow-wrap: break-word;"+
    "            max-width: 100%;"+
    "            white-space: nowrap;"+
    "            padding: 2px;"+
    "        }"+
    "        .Events button {"+
    "            float: right;" + 
    "            margin-left: 1px;" + 
    "        }"+
    "        .Events li {"+
    "            font-size: 13px;" + 
    "        }"+
    "        nav {" + 
    "            margin: 15px 0;" + 
    "            text-align: center;" + 
    "        }" + 
    "        nav a {" + 
    "            padding: 10px 50px;" + 
    "            background-color: #333;" + 
    "            color: white;" + 
    "            text-decoration: none;" + 
    "            font-weight: bold;" + 
    "            border-radius: 5px;" + 
    "        }" + 
    "        nav a.relayControlActive {" + 
    "            background-color: #555;" + 
    "        }" + 
    "        nav a.rtcEventActive {" + 
    "            background-color: #fff;" + 
    "            color: #333;" + 
    "            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3), 0 1px 3px rgba(0, 0, 0, 0.1);" + 
    "            transform: translateY(-4px);" + 
    "            transition: all 0.2s ease-in-out;" + 
    "        }" + 
    "    </style>" + 
    "</head>" + 
    "<body>" +
    "    <script defer=\"defer\">" +
    "        function getRtcEventData() {" +
    "            var dateBox1 = document.getElementById('DateBox1').value;" +
    "            var dateBox2 = document.getElementById('DateBox2').value;" +
    "            var dateBox3 = document.getElementById('DateBox3').value;" +
    "            var week = document.getElementById('Week').value;" +
    "            var timeBox1 = document.getElementById('TimeBox1').value;" +
    "            var timeBox2 = document.getElementById('TimeBox2').value;" +
    "            var timeBox3 = document.getElementById('TimeBox3').value;" +
    "            var serialPort = document.getElementById('SerialPort').value;" +
    "            var canID = document.getElementById('CANid').value;" +
    "            var canExtd = document.getElementById('CANextd').value;" +
    "            var serialData = document.getElementById('SerialData').value;" +
    "            var dataType = document.getElementById('DataType').value;" +
    "            var cycleDuration = document.getElementById('CycleDuration').value;" +
    "            var cycleBox = document.getElementById('CycleBox1').value;" +
    "            var WebData = " +
    "                'Date: ' + dateBox1 + '/' + dateBox2 + '/' + dateBox3 + '  ' + '\\n' + " + 
    "                'Week: ' + week + '  ' + '\\n' + " + 
    "                'Time: ' + timeBox1 + ':' + timeBox2 + ':' + timeBox3 +  '  ' + '\\n' + " + 
    "                'Serial Port: ' + serialPort + '  ' + '\\n' + " + 
    "                'CAN ID: ' + '0x' + canID.toUpperCase() + '  ' + '\\n' + " + 
    "                'CAN Extd: ' + canExtd + '  ' + '\\n' + " + 
    "                'Serial Data: ' + serialData + '  ' + '\\n' + " + 
    "                'Data Type: ' + dataType + '  ' + '\\n' + " + 
    "                'Cycle Duration: ' + cycleDuration + '  ' + '\\n' + " + 
    "                'Cycle: ' + cycleBox + '  ' + '\\n' ;" + 
    "            var xhr = new XMLHttpRequest();" +
    "            xhr.open('GET', '/NewEvent?data=' + WebData, true);" +
    "            xhr.send();" +
    "        }" +
    "        function handleSerialInput(input) {"+
    "            const dataType = document.getElementById(\"DataType\").value;"+
    "            if (dataType === \"1\") {"+
    "                let raw = input.value.replace(/[^0-9a-fA-F]/g, '');"+
    "                let spaced = raw.match(/.{1,2}/g);"+
    "                input.value = spaced ? spaced.join(' ') : '';"+
    "            }"+
    "        }"+
    "        function deleteEvent(eventId) {"+
    "            var xhr = new XMLHttpRequest();"+
    "            var EventId = eventId;"+
    "            xhr.open('GET', '/DeleteEvent?id=' + EventId, true);"+
    "            xhr.send();"+
    "        }"+
    "        function updateList(data) {"+
    "            var list = document.getElementById(\"myList\");"+
    "            list.innerHTML = \'\'; "+
    "            for (let i = 0; i < data.eventCount; i++) {"+
    "                var newItem = document.createElement(\"li\");"+
    "                var eventContent = data[\"eventStr\" + (i + 1)].replace(/\\n/g, \"<br>\");"+
    "                newItem.innerHTML = eventContent;"+
    "                var eventButton = document.createElement(\"button\");"+
    "                eventButton.textContent = \"Delete\" + \"Event\" + (i + 1);"+
    "                eventButton.onclick = function() {"+
    "                    deleteEvent(i + 1);"+
    "                };"+
    "                newItem.style.display = 'flex';"+
    "                newItem.style.justifyContent = 'space-between';"+
    "                newItem.style.alignItems = 'center';"+
    "                newItem.appendChild(eventButton);"+
    "                list.appendChild(newItem);"+
    "            }"+
    "        }"+
    "        function upTime() {"+
    "            var xhr = new XMLHttpRequest();"+
    "            xhr.open('GET', '/getTimeAndEvent', true); "+
    "            xhr.onreadystatechange = function() {"+
    "                if (xhr.readyState === 4 && xhr.status === 200) {"+
    "                    var data = JSON.parse(xhr.responseText); "+
    "                    document.getElementById(\"Time\").textContent = data.time;"+
    "                    updateList(data); "+
    "                }"+
    "            };"+
    "            xhr.send();"+
    // "        fetch(\'/getTimeAndEvent\')"+
    // "            .then(response => response.json())"+
    // "            .then(data => {"+
    // "                document.getElementById(\"Time\").textContent = data.time;"+
    // "                updateList(data);"+
    // "            })"+
    // "            .catch(error => {"+
    // "                console.error(\'Error fetching time and events:\', error);"+
    // "            });"+
    "        }"+
    "        function toggleCANFrame() {"+   
    "            var serialPort = document.getElementById('SerialPort').value;"+   
    "            var canFrameDiv = document.getElementById('CANFrame');"+   
    "            var dataTypeDiv = document.getElementById('DataType');"+   
    "            canFrameDiv.style.display = 'block';"+   
    "            dataTypeDiv.value = \"1\";  "+  
    "            dataTypeDiv.disabled = true; "+  
    "        }"+      
    "        function UpDataRtcTime() {" +
    "            var dateBox1 = document.getElementById('RtcDateBox1').value;" +
    "            var dateBox2 = document.getElementById('RtcDateBox2').value;" +
    "            var dateBox3 = document.getElementById('RtcDateBox3').value;" +
    "            var week = document.getElementById('RtcWeek').value;" +
    "            var timeBox1 = document.getElementById('RtcTimeBox1').value;" +
    "            var timeBox2 = document.getElementById('RtcTimeBox2').value;" +
    "            var timeBox3 = document.getElementById('RtcTimeBox3').value;" +
    "            var WebData = " +
    "                'Date: ' + dateBox1 + '/' + dateBox2 + '/' + dateBox3 + '  ' + '\\n' + " + 
    "                'Week: ' + week + '  ' + '\\n' + " + 
    "                'Time: ' + timeBox1 + ':' + timeBox2 + ':' + timeBox3 +  '  ' + '\\n' ; " + 
    "            var xhr = new XMLHttpRequest();" +
    "            xhr.open('GET', '/SetRtcTime?data=' + WebData, true);" +
    "            xhr.send();" +
    "        }" +
    "        function DisplayRtcConfig() {"+    
    "            var RtcConfigDiv = document.getElementById('RtcTimeConfig');"+   
    "            RtcConfigDiv.style.display = (RtcConfigDiv.style.display === 'none' || RtcConfigDiv.style.display === '') ? 'block' : 'none';"+    
    "        }"+  
    // "        function HideRtcConfig() {"+    
    // "            var RtcConfigDiv = document.getElementById('RtcTimeConfig');"+   
    // "            RtcConfigDiv.style.display = 'none';"+    
    // "        }"+  
    "        function DisplayCycleDuration() {"+    
    "            var CycleBoxDiv = document.getElementById('CycleBox1').value;"+   
    "            var cycleDurationDiv = document.getElementById('CycleDuration');"+    
    "            cycleDurationDiv.style.display = (CycleBoxDiv === '1' || CycleBoxDiv === '2' || CycleBoxDiv === '3' || CycleBoxDiv === '4' ) ? 'block' : 'none';"+    
    "        }"+ 
    "        var refreshInterval = 400;"+                                     
    "        setInterval(upTime, refreshInterval);"+                        
    "        setInterval(DisplayCycleDuration, refreshInterval);"+                          
    "        setInterval(toggleCANFrame, refreshInterval);"+         
    "    </script>" +
    "    <div class=\"header\">"+
    "        <h1>ESP32-S3-CAN-2CH</h1>" + 
    "    </div>" + 
    "    <nav>" + 
    "        <a href=\"/\" id=\"SerialControlLink\" class=\"SerialControlActive\">Serial Control</a>" +  
    "        <a href=\"/RTC_Event\" id=\"rtcEventLink\" class=\"rtcEventActive\">RTC Event</a>" +  
    "    </nav>" + 
    "    <div class=\"container\">" +  
    "        <div class=\"form-group\">" + 
    "            <label for=\"Date\">Date:(example:2024/12/20)</label>" + 
    "            <input type=\"text\" id=\"DateBox1\" style=\"width: 50px;\" value=\"2024\">" + 
    "            <span>/</span>" + 
    "            <input type=\"text\" id=\"DateBox2\" style=\"width: 50px;\" value=\"12\">" + 
    "            <span>/</span>" + 
    "            <input type=\"text\" id=\"DateBox3\" style=\"width: 50px;\" value=\"20\">" + 
    "            <span>&nbsp;&nbsp;&nbsp;</span>" + 
    "            <select id=\"Week\" style=\"width: 150px;\">" + 
    "                <option value=\"1\">閺勭喐婀℃稉鈧?Monday)</option>" + 
    "                <option value=\"2\">閺勭喐婀℃禍?Tuesday)</option>" + 
    "                <option value=\"3\">閺勭喐婀℃稉?Wednesday)</option>" + 
    "                <option value=\"4\">閺勭喐婀￠崶?Thursday)</option>" + 
    "                <option value=\"5\">閺勭喐婀℃禍?Friday)</option>" + 
    "                <option value=\"6\">閺勭喐婀￠崗?Saturday)</option>" + 
    "                <option value=\"0\">閺勭喐婀￠弮?Sunday)</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"Time\">Time:(example:16:51:21)</label>" + 
    "            <input type=\"text\" id=\"TimeBox1\" style=\"width: 50px;\" value=\"0\">" + 
    "            <span>:</span>" + 
    "            <input type=\"text\" id=\"TimeBox2\" style=\"width: 50px;\" value=\"0\">" + 
    "            <span>:</span>" + 
    "            <input type=\"text\" id=\"TimeBox3\" style=\"width: 50px;\" value=\"0\">" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"SerialBox\">SerialPort:</label>" +
    "            <select id=\"SerialPort\" style=\"width: 200px; text-align: left;\">" + 
    "                <option value=\"0\">CAN CH2 Send</option>" + 
    "                <option value=\"1\">CAN CH1 Send</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\"  id=\"CANFrame\" style=\"display:block;\">" + 
    "            <label for=\"CAN\">CAN frame information:</label>" + 
    "            <span>CAN ID :0x</span>" + 
    "            <input type=\"text\" id=\"CANid\" style=\"width: 160px; text-align: left;\" value=\"00000000\">" + 
    "            <select id=\"CANextd\" style=\"width: 200px; text-align: left;\">" + 
    "                <option value=\"0\">閺嶅洤鍣敮?Standard frames)</option>" + 
    "                <option value=\"1\">閹碘晛鐫嶇敮?Extended frames)</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"SendDate\">Data:(hex bytes)</label>" + 
    "            <input type=\"text\" id=\"SerialData\" style=\"width: 500px; text-align: left;\" value=\"12\" oninput=\"handleSerialInput(this)\">" + 
    "            <span><br></span>" + 
    "            <select id=\"DataType\" style=\"width: 120px; text-align: left;\" disabled>" + 
    "                <option value=\"1\" selected>hex</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"cycleBox\">Cycle:</label>" + 
    "            <div style=\"display: flex; align-items: center; gap: 10px; margin-top: 5px;\">" +
    "                <input type=\"text\" id=\"CycleDuration\" style=\"width: 100px; display:none;\" value=\"100\">" + 
    "                <select id=\"CycleBox1\" style=\"width: 150px; text-align: left;\">" + 
    "                    <option value=\"0\">閺冪娀鍣告径?Aperiodicity)</option>" + 
    "                    <option value=\"1\">濮ｎ偆顫?Milliseconds)</option>" +  
    "                    <option value=\"2\">缁?Seconds)</option>" + 
    "                    <option value=\"3\">閸?Minutes)</option>" + 
    "                    <option value=\"4\">鐏忓繑妞?Hours)</option>" + 
    "                    <option value=\"5\">濮ｅ繐銇?Everyday)</option>" + 
    "                    <option value=\"6\">濮ｅ繐鎳?Weekly)</option>" + 
    "                    <option value=\"7\">濮ｅ繑婀€(Monthly)</option>" + 
    "                </select>" + 
    "            </div>" +
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <button class=\"btn\" id=\"NewEvent\" onclick=\"getRtcEventData()\">New Event</button>" + 
    "        </div>" + 
    "    </div>" + 
    "    <div class=\"container\"  id=\"RtcTimeConfig\" style=\"display:none;\">" +  
    "        <div class=\"form-group\">" + 
    "            <label for=\"Date\">Date:(example:2024/12/20)</label>" + 
    "            <input type=\"text\" id=\"RtcDateBox1\" style=\"width: 50px;\" value=\"2024\">" + 
    "            <span>/</span>" + 
    "            <input type=\"text\" id=\"RtcDateBox2\" style=\"width: 50px;\" value=\"12\">" + 
    "            <span>/</span>" + 
    "            <input type=\"text\" id=\"RtcDateBox3\" style=\"width: 50px;\" value=\"20\">" + 
    "            <span>&nbsp;&nbsp;&nbsp;</span>" + 
    "            <select id=\"RtcWeek\" style=\"width: 150px;\">" + 
    "                <option value=\"1\">閺勭喐婀℃稉鈧?Monday)</option>" + 
    "                <option value=\"2\">閺勭喐婀℃禍?Tuesday)</option>" + 
    "                <option value=\"3\">閺勭喐婀℃稉?Wednesday)</option>" + 
    "                <option value=\"4\">閺勭喐婀￠崶?Thursday)</option>" + 
    "                <option value=\"5\">閺勭喐婀℃禍?Friday)</option>" + 
    "                <option value=\"6\">閺勭喐婀￠崗?Saturday)</option>" + 
    "                <option value=\"0\">閺勭喐婀￠弮?Sunday)</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"Time\">Time:(example:16:51:21)</label>" + 
    "            <input type=\"text\" id=\"RtcTimeBox1\" style=\"width: 50px;\" value=\"0\">" + 
    "            <span>:</span>" + 
    "            <input type=\"text\" id=\"RtcTimeBox2\" style=\"width: 50px;\" value=\"0\">" + 
    "            <span>:</span>" + 
    "            <input type=\"text\" id=\"RtcTimeBox3\" style=\"width: 50px;\" value=\"0\">" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <button class=\"btn\" id=\"UpDateTime\" style=\"margin-right: 50px;\" onclick=\"UpDataRtcTime()\">UpDate Time</button>" + 
    // "            <button class=\"btn\" id=\"HideConfig\" onclick=\"HideRtcConfig()\">Hide Config</button>" + 
    "        </div>" + 
    "    </div>" + 
    "    <div class=\"container\">" +  
    "        <div class=\"form-group\">" + 
    "            <span id=\"Time\" style=\"margin-right: 20px;\"></span> "+
    "            <button id=\"RtcConfig\" onclick=\"DisplayRtcConfig()\">RTC Config</button>" + 
    "        </div>" + 
    "        <div class=\"Events\">" + 
    "            <ul id=\"myList\"> "+
    "            </ul> "+
    "        </div>" + 
    "    </div>" + 
    "</body>" + 
    "</html>"; 

    server.send(200, "text/html", rtcPage);   
    Serial.printf("The user visited the RTC Event page\r\n"); 
}

String escapeJson(const char* input) {
  String output = "";
  while (*input) {
    char c = *input++;
    switch (c) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      case '/':  output += "\\/"; break;
      default:
        if ((uint8_t)c <= 0x1F) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          output += buf;
        } else {
          output += c;
        }
    }
  }
  return output;
}
void handleGetRateConfig() {
  // 闁哄瀚伴埀?JSON 闁告繂绉寸花鑼偓娑欘殘椤戜焦绋?
  String json = "{";
  json += "\"can2_rate\": \"" + String(CAN2_bitrate_kbps) + "\",";
  json += "\"can_rate\": \"" + String(CAN_bitrate_kbps) + "\"";
  json += "}";

  // 闁告瑦鍨块埀顑跨閹奸攱鎯?
  server.send(200, "application/json", json);
}

void handleGetCAN2Data() {
  char can_data[CAN2_Received_Len_MAX] = {0};
  if (!CAN2_CopyAndClearReadData(can_data, sizeof(can_data))) {
    server.send(200, "application/json", "[]");
    return;
  }
  String safeString = escapeJson(can_data);

  String json = "[\"" + safeString + "\"]";
  server.send(200, "application/json", json);
}

void handleClearCAN2Data() {
  CAN2_ClearReadData();
  server.send(200, "text/plain", "OK");
}

void handleGetCANData() {
  char can_data[CAN_Received_Len_MAX] = {0};
  if (!CAN_CopyAndClearReadData(can_data, sizeof(can_data))) {
    // If empty, don't perform any operation and exit
    server.send(200, "application/json", "[]");  // Respond with an empty JSON array
    return;
  }
  String safeString = String(can_data);
  safeString.replace("\\", "\\\\");
  safeString.replace("\"", "\\\"");
  safeString.replace("\n", "\\n");
  safeString.replace("\r", "\\r");
  safeString.replace("\t", "\\t");
  String json = "[\"" + safeString + "\"]";
  server.send(200, "application/json", json);
}

void handleClearCANData() {
  CAN_ClearReadData();
  server.send(200, "text/plain", "OK");
}

void handleCAN2SetRate() {
  char Text[1000] = {0};
  if (server.hasArg("data")) {
    String newData = server.arg("data");
    newData.toCharArray(Text, sizeof(Text));
  }

  uint32_t bitrate_kbps = CAN2_bitrate_kbps;
  bool ret = ParseCANRateConfig(Text, &bitrate_kbps);
  if(ret){
    CAN2_UpdateRate(bitrate_kbps);
  } else {
    Serial.printf("CAN CH2 rate parse failed\r\n");
  }
  server.send(200, "text/plain", "OK");
}
void handleCAN2Send() {
  char Text[1000] = {0};
  if (server.hasArg("data")) {
    String newData = server.arg("data");
    newData.toCharArray(Text, sizeof(Text));
  }

  CAN_Receive CANData;
  bool queued = false;
  bool parsed = false;
  if (ParseCANData(Text, &CANData)) {
    parsed = true;
    queued = enqueueWebCanTx(2, &CANData);
    if (!queued) {
      Serial.printf("CAN CH2 Web TX queue full\r\n");
    }
    free(CANData.Read_Data);
  } else {
    Serial.printf("CAN CH2 Web TX parse failed\r\n");
  }
  
  server.send(queued ? 200 : (parsed ? 503 : 400), "text/plain", queued ? "OK" : (parsed ? "BUSY" : "ERROR"));
}
void handleCANSetRate() {
  char Text[1000] = {0};
  if (server.hasArg("data")) {
    String newData = server.arg("data");
    newData.toCharArray(Text, sizeof(Text));
  }

  uint32_t bitrate_kbps = CAN_bitrate_kbps;
  bool ret = ParseCANRateConfig(Text, &bitrate_kbps);
  if(ret){
    CAN_UpdateRate(bitrate_kbps);
  } else {
    Serial.printf("CAN CH1 rate parse failed\r\n");
  }
  server.send(200, "text/plain", "OK");
}
void handleCANSetAllRate() {
  char Text[1000] = {0};
  if (server.hasArg("data")) {
    String newData = server.arg("data");
    newData.toCharArray(Text, sizeof(Text));
  }

  uint32_t bitrate_kbps = CAN_bitrate_kbps;
  bool ret = ParseCANRateConfig(Text, &bitrate_kbps);
  if(ret){
    CAN2_UpdateRate(bitrate_kbps);
    CAN_UpdateRate(bitrate_kbps);
  } else {
    Serial.printf("CAN CH1/CH2 rate parse failed\r\n");
  }
  server.send(200, "text/plain", "OK");
}
void handleCANSend() {
  char Text[1000] = {0};
  if (server.hasArg("data")) {
    String newData = server.arg("data");
    newData.toCharArray(Text, sizeof(Text));
  }

  CAN_Receive CANData;
  bool queued = false;
  bool parsed = false;
  if (ParseCANData(Text, &CANData)) {
    parsed = true;
    queued = enqueueWebCanTx(1, &CANData);
    if (!queued) {
      Serial.printf("CAN CH1 Web TX queue full\r\n");
    }
    free(CANData.Read_Data);
  } else {
    Serial.printf("CAN CH1 Web TX parse failed\r\n");
  }
  server.send(queued ? 200 : (parsed ? 503 : 400), "text/plain", queued ? "OK" : (parsed ? "BUSY" : "ERROR"));
}

void handleNewEvent(void) {
  char Text[1000] = {0};
  if (!server.hasArg("data")) {
    server.send(400, "text/plain", "ERROR");
    return;
  }
  String newData = server.arg("data");
  newData.toCharArray(Text, sizeof(Text));

  Serial.printf("Text=%s.\r\n",Text);  // Text=Date: 2024/12/20  Week: 0  Time: 0:0:0  Relay CH1: 0  Relay CH2: 2  Relay CH3: 2  Relay CH4: 2  Relay CH5: 2  Relay CH6: 2  Relay CH7: 2  Relay CH8: 2  Cycle: 0.
  datetime_t Event_Time;
  Repetition_event cycleEvent;
  Web_Receive SerialData;
  if (!ParseRTCData(Text, &Event_Time, &SerialData, &cycleEvent)) {
    server.send(400, "text/plain", "ERROR");
    return;
  }
  // Print decoded values
  // Serial.printf("Decoded datetime:\n");
  // Serial.printf("Year: %d, Month: %d, Day: %d, Week: %d\r\n", Event_Time.year, Event_Time.month, Event_Time.day, Event_Time.dotw);
  // Serial.printf("Time: %d:%d:%d\r\n", Event_Time.hour, Event_Time.minute, Event_Time.second);
  // Serial.printf("Relay States:\r\n");
  // for (int i = 0; i < 8; i++) {
  //   Serial.printf("Relay CH%d: %d\r\n", i + 1, Relay_n[i]);
  // }
  // Serial.printf("Cycle Event: %d\r\n", cycleEvent);
  if(Event_Time.month > 12 || Event_Time.day > 31 || Event_Time.dotw > 6 || Event_Time.month == 0 || Event_Time.day == 0) {
    Serial.printf("Error parsing Event_Time !!!!\r\n");
    free(SerialData.SerialData);
    server.send(400, "text/plain", "ERROR");
  }
  else if(Event_Time.hour > 23 || Event_Time.minute > 59 || Event_Time.second > 59 ) {
    Serial.printf("Error parsing Event_Time !!!!\r\n");
    free(SerialData.SerialData);
    server.send(400, "text/plain", "ERROR");
  }
  else {
    if (TimerEvent_Serial_Set(Event_Time, &SerialData, cycleEvent)) {
      server.send(200, "text/plain", "OK");
    } else {
      free(SerialData.SerialData);
      server.send(503, "text/plain", "BUSY");
    }
  }
}

void handleSetRtcTime(void) {
  char Text[200] = {0};
  if (!server.hasArg("data")) {
    server.send(400, "text/plain", "ERROR");
    return;
  }
  String newData = server.arg("data");
  newData.toCharArray(Text, sizeof(Text));

  Serial.printf("Text=%s.\r\n",Text);  // Text=Date: 2024/12/20  Week: 0  Time: 0:0:0  Relay CH1: 0  Relay CH2: 2  Relay CH3: 2  Relay CH4: 2  Relay CH5: 2  Relay CH6: 2  Relay CH7: 2  Relay CH8: 2  Cycle: 0.
  datetime_t Rtc_Time;
  if (!ParseRtcConfig(Text, &Rtc_Time)) {
    server.send(400, "text/plain", "ERROR");
    return;
  }
  // Print decoded values
  Serial.printf("Decoded datetime:\n");
  Serial.printf("Year: %d, Month: %d, Day: %d, Week: %d\r\n", Rtc_Time.year, Rtc_Time.month, Rtc_Time.day, Rtc_Time.dotw);
  Serial.printf("Time: %d:%d:%d\r\n", Rtc_Time.hour, Rtc_Time.minute, Rtc_Time.second);
  if(Rtc_Time.month > 12 || Rtc_Time.day > 31 || Rtc_Time.dotw > 6 || Rtc_Time.month == 0 || Rtc_Time.day == 0) {
    Serial.printf("Error parsing Rtc_Time !!!!\r\n");
    server.send(400, "text/plain", "ERROR");
  }
  else if(Rtc_Time.hour > 23 || Rtc_Time.minute > 59 || Rtc_Time.second > 59 ) {
    Serial.printf("Error parsing RTC Time !!!!\r\n");
    server.send(400, "text/plain", "ERROR");
  }
  else {
    PCF85063_Set_All(Rtc_Time);
    if (RTC_LockEvents(1000)) {
      datetime = Rtc_Time;
      RTC_Update_Alarm();
      RTC_UnlockEvents();
    } else {
      Serial.printf("RTC event mutex timeout while setting RTC time\r\n");
    }
    server.send(200, "text/plain", "OK");
  }
}

void handleUpTimeAndEvent() {
  // Format the datetime string
  char datetime_str[50];
  if (!RTC_LockEvents(1000)) {
    server.send(503, "text/plain", "{\"eventCount\":0}");
    return;
  }
  snprintf(datetime_str, sizeof(datetime_str), " %d/%d/%d  %s  %d:%d:%d", datetime.year, datetime.month, datetime.day, Week[datetime.dotw], datetime.hour, datetime.minute, datetime.second);

  int eventCount = Timing_events_Num;  // Get the event count (e.g., Timing_events_Num)

  // Create a JSON response
  String jsonResponse = "{";
  
  jsonResponse += "\"time\":\"" + String(datetime_str) + "\",";
  for (int i = 0; i < eventCount; i++) {
    jsonResponse += "\"eventStr" + String(i + 1) + "\":\"" + escapeJson(Event_str[i]) + "\",";
  }
  jsonResponse += "\"eventCount\":" + String(eventCount);
  jsonResponse += "}";
  RTC_UnlockEvents();
  // Send the datetime string as a response
  server.send(200, "text/plain", jsonResponse );
}
void handleDeleteEvent() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id > 0) {
      if (TimerEvent_Del_Number((uint8_t)id)) {
        server.send(200, "text/plain", "Event " + String(id) + " deleted.");
        Serial.printf("Event %d deleted.\r\n", id);
      } else {
        server.send(404, "text/plain", "Event not found.");
      }
    } else {
      server.send(400, "text/plain", "Invalid event ID.");
    }
  } else {
    server.send(400, "text/plain", "Event ID not provided.");
  }
}


void WIFI_Init()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(WIFI_OnEvent);
  Serial.printf("Connecting to WiFi SSID: %s\r\n", ssid);
  WiFi.begin(ssid, password);

  uint32_t startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startTime > 20000) {
      Serial.printf("\r\nWiFi connection timeout, please check SSID/password.\r\n");
      break;
    }
  }
  Serial.printf("\r\n");

  if (WiFi.status() == WL_CONNECTED) {
    WIFI_PrintSTAIP("WiFi connected, IP: ");
  }

  server.on("/", handleRoot);            // Relay Control page
  server.on("/getRateConfig"     , handleGetRateConfig);
  server.on("/CAN2SetRate"      , handleCAN2SetRate);
  server.on("/CAN2Send"         , handleCAN2Send);
  server.on("/getCAN2Data"      , handleGetCAN2Data);
  server.on("/clearCAN2Data"    , handleClearCAN2Data);
  server.on("/CANSetRate"       , handleCANSetRate);
  server.on("/CANSetAllRate"    , handleCANSetAllRate);
  server.on("/CANSend"          , handleCANSend);
  server.on("/getCANData"       , handleGetCANData);
  server.on("/clearCANData"     , handleClearCANData);
  
  server.on("/RTC_Event"        , handleRTCPage);      // RTC Event page
  server.on("/NewEvent"         , handleNewEvent);
  server.on("/SetRtcTime"       , handleSetRtcTime);
  server.on("/getTimeAndEvent"  , handleUpTimeAndEvent);
  server.on("/DeleteEvent"      , handleDeleteEvent);
  
  server.begin(); 
  Serial.printf("Web server started\r\n");  
  if (WebCanTxQueue == NULL) {
    WebCanTxQueue = xQueueCreate(WEB_CAN_TX_QUEUE_LEN, sizeof(WebCanTxItem));
  }
  if (WebCanTxQueue != NULL && WebCanTxTaskHandle == NULL) {
    if (xTaskCreatePinnedToCore(
      WebCanTxTask,
      "WebCanTxTask",
      4096,
      NULL,
      3,
      &WebCanTxTaskHandle,
      0
    ) != pdPASS) {
      Serial.printf("CAN Web TX task create failed\r\n");
      WebCanTxTaskHandle = NULL;
    }
  }
  if (WebCanTxQueue == NULL) {
    Serial.printf("CAN Web TX queue create failed\r\n");
  }
  if (xTaskCreatePinnedToCore(
    WebTask,    
    "WebTask",   
    4096,                
    NULL,                 
    4,                   
    NULL,                 
    0                   
  ) != pdPASS) {
    Serial.printf("WebTask create failed\r\n");
  }
}


void WebTask(void *parameter) {
  while(1){
    server.handleClient(); // Processing requests from clients
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(NULL);
}

static bool scanMarker(const char *text, const char *marker, const char *format, ...)
{
  if (text == NULL || marker == NULL || format == NULL) {
    return false;
  }

  const char *start = strstr(text, marker);
  if (start == NULL) {
    return false;
  }

  va_list args;
  va_start(args, format);
  int ret = vsscanf(start, format, args);
  va_end(args);
  return ret == 1;
}

static bool hexCharToNibble(char c, uint8_t *value) {
  if (value == NULL) {
    return false;
  }
  if (c >= '0' && c <= '9') {
    *value = (uint8_t)(c - '0');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *value = (uint8_t)(c - 'A' + 10);
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *value = (uint8_t)(c - 'a' + 10);
    return true;
  }
  return false;
}

static bool hexPairToByte(char high, char low, uint8_t *out) {
  uint8_t high_value = 0;
  uint8_t low_value = 0;
  if (out == NULL || !hexCharToNibble(high, &high_value) || !hexCharToNibble(low, &low_value)) {
    return false;
  }
  *out = (uint8_t)((high_value << 4) | low_value);
  return true;
}

// String decoding
bool ParseRTCData(const char* Text, datetime_t* dt, Web_Receive* SerialData, Repetition_event* cycleEvent) {    
  int ret;
  if (Text == NULL || dt == NULL || SerialData == NULL || cycleEvent == NULL) {
    return false;
  }
  SerialData->SerialData = NULL;
  SerialData->DataLength = 0;
  // Parse Date: YYYY/MM/DD
  ret = sscanf(Text, "Date: %hd/%hhd/%hhd", &dt->year, &dt->month, &dt->day);
  if (ret != 3) {
    Serial.printf("Error parsing date\n");
    return false;
  }
  // Parse Week: W (day of the week)
  if (!scanMarker(Text, "Week: ", "Week: %hhd", &dt->dotw)) {
    Serial.printf("Error parsing week\n");
    return false;
  }
  // Parse Time: HH:MM:SS
  const char *time_field = strstr(Text, "Time: ");
  if (time_field == NULL || sscanf(time_field, "Time: %hhd:%hhd:%hhd", &dt->hour, &dt->minute, &dt->second) != 3) {
    Serial.printf("Error parsing time\n");
    return false;
  }
  // Parse Serial Port: CAN CH2/CAN CH1
  if (!scanMarker(Text, "Serial Port: ", "Serial Port: %hhu", &SerialData->SerialPort)) {
    Serial.printf("Error parsing Serial Port\n");
    return false;
  }
  
  unsigned long can_id = 0;
  if (!scanMarker(Text, "CAN ID: ", "CAN ID: 0x%lx", &can_id)) {
    Serial.printf("Error parsing CAN ID\n");
    return false;
  }
  SerialData->CAN_ID = (uint32_t)can_id;
  if(SerialData->CAN_ID > 0x1FFFFFFF) {
    Serial.printf("CAN ID error:%lX\n",SerialData->CAN_ID);
    return false;
  }
  if (!scanMarker(Text, "CAN Extd: ", "CAN Extd: %hhu", &SerialData->CAN_extd)) {
    Serial.printf("Error parsing CAN Extd\n");
    return false;
  }
  // Parse Serial Data Type: char / hex
  if (!scanMarker(Text, "Data Type: ", "Data Type: %hhu", &SerialData->DataType)) {
    Serial.printf("Error parsing Serial Type\n");
    return false;
  }


  // Parse Serial Data Length: 
  const char* start = strstr(Text, "Serial Data: ");
  if (!start) {
    Serial.printf("Serial Data not found\n");
    return false;
  }
  start += strlen("Serial Data: ");
  const char* end = strstr(start, "  Data Type: ");
  if (!end) {
    Serial.printf("RTC event Serial Data end marker not found\n");
    return false;
  }
  SerialData->DataLength = end - start;

  if(SerialData->DataType){
    bool missing = 0;
    char* cleanHex = (char*)malloc(SerialData->DataLength + 1);  
    if (cleanHex == NULL) {
      Serial.printf("RTC event hex buffer allocation failed\n");
      return false;
    }
    strncpy(cleanHex, start, SerialData->DataLength);
    cleanHex[SerialData->DataLength] = '\0';  
    char* src = cleanHex;
    char* dst = cleanHex;
    while (*src) {
      if (*src != ' ') {
        *dst++ = *src;
      }
      src++;
    }
    *dst = '\0'; 

    SerialData->DataLength = strlen(cleanHex);
    if (SerialData->DataLength % 2) {
      SerialData->DataLength += 1;
      missing = 1;
    }
    SerialData->DataLength /= 2;
    SerialData->SerialData = (uint8_t*)malloc(SerialData->DataLength);
    if (SerialData->SerialData == NULL && SerialData->DataLength > 0) {
      Serial.printf("RTC event serial data allocation failed\n");
      free(cleanHex);
      return false;
    }
    if (missing) {
      for (size_t i = 0; i < SerialData->DataLength - 1; i++) {
        if (!hexPairToByte(cleanHex[2 * i], cleanHex[2 * i + 1], &SerialData->SerialData[i])) {
          Serial.printf("RTC event hex data contains invalid character\n");
          free(cleanHex);
          free(SerialData->SerialData);
          SerialData->SerialData = NULL;
          return false;
        }
      }
      if (!hexPairToByte(cleanHex[2 * (SerialData->DataLength - 1)], '0', &SerialData->SerialData[SerialData->DataLength - 1])) {
        Serial.printf("RTC event hex data contains invalid character\n");
        free(cleanHex);
        free(SerialData->SerialData);
        SerialData->SerialData = NULL;
        return false;
      }
    } 
    else {
      for (size_t i = 0; i < SerialData->DataLength; i++) {
        if (!hexPairToByte(cleanHex[2 * i], cleanHex[2 * i + 1], &SerialData->SerialData[i])) {
          Serial.printf("RTC event hex data contains invalid character\n");
          free(cleanHex);
          free(SerialData->SerialData);
          SerialData->SerialData = NULL;
          return false;
        }
      }
    }
    free(cleanHex);
  }
  else{
    // Parse actual Serial Data (e.g., char or hex values)
    SerialData->SerialData = (uint8_t *)malloc(SerialData->DataLength);
    if (SerialData->SerialData == NULL && SerialData->DataLength > 0) {
      Serial.printf("RTC event serial data allocation failed\n");
      return false;
    }
    if (SerialData->DataLength > 0) {
      memcpy(SerialData->SerialData, start, SerialData->DataLength);
    }
  }
  // Parse Cycle: C
  uint8_t cycle;
  if (scanMarker(Text, "Cycle: ", "Cycle: %hhd", &cycle)) {
    *cycleEvent = (Repetition_event)cycle;
  }
  else{
    Serial.printf("Error parsing cycle\n");
    free(SerialData->SerialData);
    SerialData->SerialData = NULL;
    return false;
  }
  if(*cycleEvent == Repetition_Hours || *cycleEvent == Repetition_Minutes || *cycleEvent == Repetition_Seconds || *cycleEvent == Repetition_Milliseconds){
    unsigned long cycle_duration = 0;
    if (!scanMarker(Text, "Cycle Duration: ", "Cycle Duration: %lu", &cycle_duration)) {
      Serial.printf("Error parsing Cycle Duration\n");
      free(SerialData->SerialData);
      SerialData->SerialData = NULL;
      return false;
    }
    uint64_t cycle_duration_ms = (uint64_t)cycle_duration;
    switch(*cycleEvent){
      case Repetition_Hours: 
        cycle_duration_ms *= 3600000ULL;
        break;
      case Repetition_Minutes: 
        cycle_duration_ms *= 60000ULL;
        break;
      case Repetition_Seconds: 
        cycle_duration_ms *= 1000ULL;
        break;
      case Repetition_Milliseconds: 
        break;
      default:
        Serial.printf("Event error!!!!\n");
        break;
    }
    if (cycle_duration_ms > UINT32_MAX) {
      Serial.printf("Cycle Duration overflow\n");
      free(SerialData->SerialData);
      SerialData->SerialData = NULL;
      return false;
    }
    SerialData->repetition_Time[0] = (uint32_t)cycle_duration_ms;
  }
  return true;
}
// String decoding
bool ParseRtcConfig(const char* Text, datetime_t* dt) {    
  int ret;
  if (Text == NULL || dt == NULL) {
    return false;
  }
  // Parse Date: YYYY/MM/DD
  ret = sscanf(Text, "Date: %hd/%hhd/%hhd", &dt->year, &dt->month, &dt->day);
  if (ret != 3) {
    Serial.printf("Error parsing date\n");
    return false;
  }
  // Parse Week: W (day of the week)
  if (!scanMarker(Text, "Week: ", "Week: %hhd", &dt->dotw)) {
    Serial.printf("Error parsing week\n");
    return false;
  }
  // Parse Time: HH:MM:SS
  const char *time_field = strstr(Text, "Time: ");
  if (time_field == NULL || sscanf(time_field, "Time: %hhd:%hhd:%hhd", &dt->hour, &dt->minute, &dt->second) != 3) {
    Serial.printf("Error parsing time\n");
    return false;
  }
  return true;
}


// String decoding
bool ParseCANRateConfig(const char* Text,  uint32_t * CAN_bitrate_kbps) {    
  if (Text == NULL || CAN_bitrate_kbps == NULL) {
    return false;
  }
  // Parse Serial Data Type: char / hex
  unsigned long bitrate = 0;
  if (!scanMarker(Text, "CAN Rate: ", "CAN Rate: %lu", &bitrate)) {
    Serial.printf("Error parsing CAN rate\n");
    return false;
  }
  *CAN_bitrate_kbps = (uint32_t)bitrate;
  switch (*CAN_bitrate_kbps) {
    case 25:
    case 50:
    case 100:
    case 125:
    case 250:
    case 500:
    case 800:
    case 1000:
      return true;
    default:
      Serial.printf("Unsupported CAN rate:%lu kbps\r\n", *CAN_bitrate_kbps);
      return false;
  }
  return true;
}
// String decoding
bool ParseCANData(const char* Text, CAN_Receive* CANData) {    
  if (Text == NULL || CANData == NULL) {
    return false;
  }
  CANData->Read_Data = NULL;
  CANData->DataLength = 0;

  unsigned long can_id = 0;
  if (!scanMarker(Text, "CAN ID: ", "CAN ID: 0x%lx", &can_id)) {
    Serial.printf("Error parsing CAN ID\n");
    return false;
  }
  CANData->CAN_ID = (uint32_t)can_id;
  if(CANData->CAN_ID > 0x1FFFFFFF) {
    Serial.printf("CAN ID error:%lX\n",CANData->CAN_ID);
    return false;
  }
  if (!scanMarker(Text, "CAN Extd: ", "CAN Extd: %hhu", &CANData->CAN_extd)) {
    Serial.printf("Error parsing CAN Extd\n");
    return false;
  }
  
  // Parse Serial Data Length: 
  const char* start = strstr(Text, "CAN Data: ");
  if (!start) {
    Serial.printf("CAN Data not found\n");
    return false;
  }
  start += strlen("CAN Data: ");
  const char* end = strstr(start, "Web End");
  if (!end) {
    Serial.printf("CAN Data end marker not found\n");
    return false;
  }
  
  size_t dataLength = end - start;
  while (dataLength > 0) {
    char c = start[dataLength - 1];
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
      dataLength--;
    } else {
      break;
    }
  }
  bool missing = 0;
  char* cleanHex = (char*)malloc(dataLength + 1);  
  if (cleanHex == NULL) {
    Serial.printf("CAN data hex buffer allocation failed\n");
    return false;
  }
  strncpy(cleanHex, start, dataLength);
  cleanHex[dataLength] = '\0';  

  char* src = cleanHex;
  char* dst = cleanHex;
  while (*src) {
    if (*src != ' ' && *src != '\r' && *src != '\n' && *src != '\t') {
      *dst++ = *src;
    }
    src++;
  }
  *dst = '\0';  

  CANData->DataLength = strlen(cleanHex);
  if (CANData->DataLength % 2) {
    CANData->DataLength += 1;
    missing = 1;
  }
  CANData->DataLength /= 2;
  CANData->Read_Data = (uint8_t*)malloc(CANData->DataLength);
  if (CANData->Read_Data == NULL && CANData->DataLength > 0) {
    Serial.printf("CAN data buffer allocation failed\n");
    free(cleanHex);
    return false;
  }
  if (missing) {
    for (size_t i = 0; i < CANData->DataLength - 1; i++) {
      if (!hexPairToByte(cleanHex[2 * i], cleanHex[2 * i + 1], &CANData->Read_Data[i])) {
        Serial.printf("CAN data contains invalid hex character\n");
        free(cleanHex);
        free(CANData->Read_Data);
        CANData->Read_Data = NULL;
        return false;
      }
    }
    if (!hexPairToByte(cleanHex[2 * (CANData->DataLength - 1)], '0', &CANData->Read_Data[CANData->DataLength - 1])) {
      Serial.printf("CAN data contains invalid hex character\n");
      free(cleanHex);
      free(CANData->Read_Data);
      CANData->Read_Data = NULL;
      return false;
    }
  } else {
    for (size_t i = 0; i < CANData->DataLength; i++) {
      if (!hexPairToByte(cleanHex[2 * i], cleanHex[2 * i + 1], &CANData->Read_Data[i])) {
        Serial.printf("CAN data contains invalid hex character\n");
        free(cleanHex);
        free(CANData->Read_Data);
        CANData->Read_Data = NULL;
        return false;
      }
    }
  }
  free(cleanHex);
  return true;
}

