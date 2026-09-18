#include <Arduino.h>
#include "WS_RTC.h"
#include "WS_GPIO.h"
#include "freertos/semphr.h"

Timing_RTC Events_State[Timing_events_Number_MAX];       // Set a maximum of Timing_events_Number_MAX timers
char Event_str[Timing_events_Number_MAX][1000];
static Timing_RTC Events_State_Default = {0};            // Event initial state
const unsigned char Event_cycle[8][13] = {"Aperiodicity", "Milliseconds", "Seconds", "Minutes", "Hours", "Everyday", "Weekly", "Monthly"};

uint32_t Cycle_duration = 1;
static bool Rtc_Alarm_Update_Suspended = false;
static SemaphoreHandle_t RTCMutex = NULL;

bool RTC_LockEvents(uint32_t timeout_ms)
{
  if (RTCMutex == NULL) {
    return true;
  }
  return xSemaphoreTake(RTCMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void RTC_UnlockEvents(void)
{
  if (RTCMutex != NULL) {
    xSemaphoreGive(RTCMutex);
  }
}

static bool Is_Hardware_RTC_Event(Repetition_event repetition)
{
  return repetition == Repetition_NONE ||
         repetition == Repetition_Everyday ||
         repetition == Repetition_Weekly ||
         repetition == Repetition_Monthly;
}

static void TimerEvent_Del_Unlocked(Timing_RTC event);

static uint32_t RTC_Seconds_Of_Day(datetime_t time)
{
  return (uint32_t)time.hour * 3600 + (uint32_t)time.minute * 60 + time.second;
}

static bool RTC_Is_Leap_Year(uint16_t year)
{
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static uint8_t RTC_Days_In_Month(uint16_t year, uint8_t month)
{
  static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 0 || month > 12) {
    return 0;
  }
  if (month == 2 && RTC_Is_Leap_Year(year)) {
    return 29;
  }
  return days_in_month[month - 1];
}

static int64_t RTC_Days_From_Civil(uint16_t year, uint8_t month, uint8_t day)
{
  int32_t y = year;
  uint32_t m = month;
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const int32_t shifted_month = (int32_t)m + (m > 2 ? -3 : 9);
  const uint32_t doy = (uint32_t)((153 * shifted_month + 2) / 5 + day - 1);
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + (int64_t)doe - 719468LL;
}

static int64_t RTC_DateTime_To_Seconds(datetime_t time)
{
  return RTC_Days_From_Civil(time.year, time.month, time.day) * 86400LL + RTC_Seconds_Of_Day(time);
}

static void RTC_Add_One_Month(uint16_t *year, uint8_t *month)
{
  if (year == NULL || month == NULL) {
    return;
  }
  if (*month >= 12) {
    *month = 1;
    (*year)++;
  } else {
    (*month)++;
  }
}

static bool RTC_Get_Event_Delta(Timing_RTC event, datetime_t now, uint32_t *delta_seconds)
{
  if (delta_seconds == NULL || !event.Enable_Flag || !Is_Hardware_RTC_Event(event.repetition_State)) {
    return false;
  }

  int64_t now_abs = RTC_DateTime_To_Seconds(now);
  int64_t target_abs = 0;
  uint32_t now_tod = RTC_Seconds_Of_Day(now);
  uint32_t event_tod = RTC_Seconds_Of_Day(event.Time);

  switch(event.repetition_State){
    case Repetition_NONE:
      target_abs = RTC_DateTime_To_Seconds(event.Time);
      if (target_abs <= now_abs) {
        return false;
      }
      break;
    case Repetition_Everyday:
      target_abs = now_abs + (int64_t)((event_tod + 86400 - now_tod) % 86400);
      if (target_abs <= now_abs) {
        target_abs += 86400;
      }
      break;
    case Repetition_Weekly: {
      uint32_t day_delta = (event.Time.dotw + 7 - now.dotw) % 7;
      target_abs = now_abs + (int64_t)day_delta * 86400 + (int64_t)event_tod - now_tod;
      if (target_abs <= now_abs) {
        target_abs += 7 * 86400;
      }
      break;
    }
    case Repetition_Monthly: {
      uint16_t year = now.year;
      uint8_t month = now.month;
      for (uint8_t i = 0; i < 24; i++) {
        uint8_t days_in_month = RTC_Days_In_Month(year, month);
        if (event.Time.day > 0 && event.Time.day <= days_in_month) {
          datetime_t candidate = event.Time;
          candidate.year = year;
          candidate.month = month;
          candidate.day = event.Time.day;
          target_abs = RTC_DateTime_To_Seconds(candidate);
          if (target_abs > now_abs) {
            break;
          }
        }
        target_abs = 0;
        RTC_Add_One_Month(&year, &month);
      }
      if (target_abs <= now_abs) {
        return false;
      }
      break;
    }
    default:
      return false;
  }

  int64_t delta = target_abs - now_abs;
  if (delta <= 0 || delta > UINT32_MAX) {
    return false;
  }
  *delta_seconds = (uint32_t)delta;
  return true;
}

static bool RTC_Event_Due(Timing_RTC event, datetime_t now)
{
  if (!event.Enable_Flag || !Is_Hardware_RTC_Event(event.repetition_State)) {
    return false;
  }
  if (event.Time.hour != now.hour || event.Time.minute != now.minute || event.Time.second != now.second) {
    return false;
  }

  switch(event.repetition_State){
    case Repetition_NONE:
      return event.Time.year == now.year && event.Time.month == now.month && event.Time.day == now.day;
    case Repetition_Everyday:
      return true;
    case Repetition_Weekly:
      return event.Time.dotw == now.dotw;
    case Repetition_Monthly:
      return event.Time.day == now.day;
    default:
      return false;
  }
}

static void RTC_Handle_Alarm_Events(void)
{
  if (!RTC_LockEvents(1000)) {
    Serial.printf("RTC event mutex timeout while handling alarm\r\n");
    return;
  }
  Rtc_Alarm_Update_Suspended = true;
  for (int i = 0; i < Timing_events_Number_MAX; ) {
    if (RTC_Event_Due(Events_State[i], datetime)) {
      Timing_RTC current_event = Events_State[i];
      TimerEvent_handling(current_event);
      if (current_event.repetition_State == Repetition_NONE) {
        TimerEvent_Del_Unlocked(current_event);
        continue;
      }
    }
    i++;
  }
  Rtc_Alarm_Update_Suspended = false;
  RTC_Update_Alarm();
  RTC_UnlockEvents();
}

void RTC_Update_Alarm(void)
{
  if (Rtc_Alarm_Update_Suspended) {
    return;
  }

  int alarm_event = -1;
  uint32_t best_delta = 0xFFFFFFFFUL;

  for (int i = 0; i < Timing_events_Number_MAX; i++) {
    if (!Events_State[i].Enable_Flag || !Is_Hardware_RTC_Event(Events_State[i].repetition_State)) {
      continue;
    }

    uint32_t delta = 0;
    if (!RTC_Get_Event_Delta(Events_State[i], datetime, &delta)) {
      continue;
    }
    if (delta < best_delta) {
      best_delta = delta;
      alarm_event = i;
    }
  }

  if (alarm_event >= 0) {
    PCF85063_Set_Alarm(Events_State[alarm_event].Time);
    PCF85063_Enable_Alarm();
    Serial.printf("RTC alarm armed on GPIO%d for Event %d at %02u:%02u:%02u\r\n",
           RTC_INT,
           Events_State[alarm_event].Event_Number,
           (unsigned int)Events_State[alarm_event].Time.hour,
           (unsigned int)Events_State[alarm_event].Time.minute,
           (unsigned int)Events_State[alarm_event].Time.second);
  } else {
    PCF85063_Disable_Alarm();
  }
}

void RTC_Init(void){
  if (RTCMutex == NULL) {
    RTCMutex = xSemaphoreCreateMutex();
    if (RTCMutex == NULL) {
      Serial.printf("RTC event mutex create failed\r\n");
    }
  }
  PCF85063_Init();
  RTC_Update_Alarm();
  if (xTaskCreatePinnedToCore(
    RTCTask,    
    "RTCTask",   
    4096,                
    NULL,                 
    3,                   
    NULL,                 
    0                   
  ) != pdPASS) {
    Serial.printf("RTC failed to create RTCTask\r\n");
  }
  
  if (xTaskCreatePinnedToCore(
    Continuous_Task,
    "Continuous Task",
    4096,          
    NULL,     
    3,
    NULL,
    0
  ) != pdPASS) {
    Serial.printf("RTC failed to create Continuous Task\r\n");
  }
}
uint8_t Timing_events_Num = 0;
void RTCTask(void *parameter)
{ 
  while(1){
    if(digitalRead(RTC_INT) == LOW){
      vTaskDelay(pdMS_TO_TICKS(5));
      if(digitalRead(RTC_INT) == LOW){
        uint8_t alarm_flag = PCF85063_Get_Alarm_Flag();
        if(alarm_flag & RTC_CTRL_2_AF){
          PCF85063_Read_Time(&datetime);
          PCF85063_Clear_Alarm_Flag();
          RTC_Handle_Alarm_Events();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  vTaskDelete(NULL);
}
void Continuous_Task(void *parameter){
  while(1){
    uint32_t task_delay = Cycle_duration;
    if(RTC_LockEvents(100)){
      task_delay = Cycle_duration;
      if(Timing_events_Num){
      for (int i = 0; i < Timing_events_Number_MAX; i++){
        if(Events_State[i].Enable_Flag && Events_State[i].Web_Data.repetition_Time[0] != 0 && (Events_State[i].repetition_State == Repetition_Hours || Events_State[i].repetition_State == Repetition_Minutes || Events_State[i].repetition_State == Repetition_Seconds || Events_State[i].repetition_State == Repetition_Milliseconds)){
          Events_State[i].Web_Data.repetition_Time[1] = Events_State[i].Web_Data.repetition_Time[1] + Cycle_duration;
          if(Events_State[i].Web_Data.repetition_Time[0] <= Events_State[i].Web_Data.repetition_Time[1]){
            Events_State[i].Web_Data.repetition_Time[1] = 0;
            TimerEvent_handling(Events_State[i]);
          }
        }
      }
    }
      RTC_UnlockEvents();
    }
    vTaskDelay(pdMS_TO_TICKS(task_delay));
  }
  vTaskDelete(NULL);

}
void TimerEvent_handling(Timing_RTC event){         // event trigger
  if(event.repetition_State != Repetition_Hours && event.repetition_State != Repetition_Minutes && event.repetition_State != Repetition_Seconds && event.repetition_State != Repetition_Milliseconds)
    Serial.printf("Event %d : triggered\r\n", event.Event_Number);
  // char datetime_str[50];
  // datetime_to_str(datetime_str,event.Time);
  if(event.Web_Data.SerialPort){     // CAN CH1
    twai_message_t Web_message = {0};
    Web_message.identifier = event.Web_Data.CAN_ID ;
    Web_message.extd = event.Web_Data.CAN_extd;                              // Frame_type : 1锛欵xtended frames   0锛歋tandard frames
    if (event.Web_Data.DataLength > UINT8_MAX) {
      Serial.printf("CAN CH1 event payload too long\r\n");
      return;
    }
    if (event.Web_Data.DataLength > 8) {
      send_message(event.Web_Data.CAN_ID, event.Web_Data.SerialData, (uint8_t)event.Web_Data.DataLength, event.Web_Data.CAN_extd);
      return;
    }
    Web_message.data_length_code = event.Web_Data.DataLength;                  // valueBytes[0] to valueBytes[6] are configuration parameters
    for (int i = 0; i < Web_message.data_length_code; i++) {
      Web_message.data[i] = event.Web_Data.SerialData[i];
    }
    send_message_Bytes(Web_message);                                  // Send data from CAN CH1
  }
  else{                                // CAN CH2
    if (event.Web_Data.DataLength > UINT8_MAX) {
      Serial.printf("CAN CH2 event payload too long\r\n");
      return;
    }
    send_can2_message(event.Web_Data.CAN_ID, event.Web_Data.SerialData, (uint8_t)event.Web_Data.DataLength, event.Web_Data.CAN_extd);
  }
}
uint32_t calculate_repetition_gcd(void) {
  uint32_t gcd = 0;
  for (int i = 0; i < Timing_events_Number_MAX; i++) {
    if (Events_State[i].Enable_Flag && (Events_State[i].repetition_State == Repetition_Hours || Events_State[i].repetition_State == Repetition_Minutes || Events_State[i].repetition_State == Repetition_Seconds || Events_State[i].repetition_State == Repetition_Milliseconds)) {
      uint32_t value = Events_State[i].Web_Data.repetition_Time[0];
      if (value > 0) {
        if (gcd == 0) {
          gcd = value;
        } else {
          // 杈楄浆鐩搁櫎娉曟眰鏈€澶у叕鍥犳暟
          uint32_t a = gcd, b = value;
          while (b != 0) {
            uint32_t temp = b;
            b = a % b;
            a = temp;
          }
          gcd = a;
        }
      }
    }
  }

  return gcd;
}

bool TimerEvent_Serial_Set(datetime_t time, Web_Receive* SerialData, Repetition_event Repetition)
{
  char datetime_str[50];
  datetime_to_str(datetime_str,datetime);
  Serial.printf("Now Time: %s!!!!\r\n", datetime_str);
  if (!RTC_LockEvents(1000)) {
    Serial.printf("RTC event mutex timeout while adding event\r\n");
    return false;
  }
  if(Timing_events_Num >= Timing_events_Number_MAX)
  {
    Serial.printf("Note : The number of scheduled events is full.\r\n");
    RTC_UnlockEvents();
    return false;
  }
  else{
    Events_State[Timing_events_Num].Enable_Flag = true;
    Events_State[Timing_events_Num].Event_Number = Timing_events_Num + 1;                                       // Event Serial number
    Events_State[Timing_events_Num].Web_Data = *SerialData;
    Events_State[Timing_events_Num].Time = time;
    Events_State[Timing_events_Num].repetition_State = Repetition;
    datetime_to_str(datetime_str,time);
    Serial.printf("New timing event%d :\r\n       %s \r\n",Timing_events_Num, datetime_str);
    if(Events_State[Timing_events_Num].Web_Data.SerialPort == 0){                                                // CAN CH2
      if(Events_State[Timing_events_Num].Web_Data.DataType){
        Serial.printf("        CAN CH2 Send Data: hex\r\n");
        if(Events_State[Timing_events_Num].Web_Data.CAN_extd){
          Serial.printf("        CAN CH2 Type: Extended   CAN ID: 0x%lX \r\n", Events_State[Timing_events_Num].Web_Data.CAN_ID);
        }
        else{
          Serial.printf("        CAN CH2 Type: Standard   CAN ID: 0x%lX  \r\n", Events_State[Timing_events_Num].Web_Data.CAN_ID);
        }
        Serial.printf("        CAN CH2 Data:");
        for(int i=0;i<Events_State[Timing_events_Num].Web_Data.DataLength;i++){
          Serial.printf(" 0x%.02X ", Events_State[Timing_events_Num].Web_Data.SerialData[i]);
          if ((i + 1) % 10 == 0 && (i + 1) < Events_State[Timing_events_Num].Web_Data.DataLength) {
            Serial.printf("\n                  ");
          }
        }
      }
      else{
        Serial.printf("        CAN CH2 Send Data: char\r\n");
        Serial.printf("        CAN CH2 Data: %.*s ", (int)SerialData->DataLength, (const char *)Events_State[Timing_events_Num].Web_Data.SerialData);
      }
    }
    else if(Events_State[Timing_events_Num].Web_Data.SerialPort == 1){                                                    // CAN CH1
      if(Events_State[Timing_events_Num].Web_Data.DataType){
        Serial.printf("        CAN CH1 Send Data: hex\r\n");
        if(Events_State[Timing_events_Num].Web_Data.CAN_extd){
          Serial.printf("        CAN CH1 Type: Extended   CAN ID: 0x%lX \r\n", Events_State[Timing_events_Num].Web_Data.CAN_ID);
        }
        else{
          Serial.printf("        CAN CH1 Type: Standard   CAN ID: 0x%lX  \r\n", Events_State[Timing_events_Num].Web_Data.CAN_ID);
        }
        Serial.printf("        CAN CH1 Data:");
        for(int i=0;i<Events_State[Timing_events_Num].Web_Data.DataLength;i++){
          Serial.printf(" 0x%lX ", Events_State[Timing_events_Num].Web_Data.SerialData[i]);
          if ((i + 1) % 10 == 0 && (i + 1) < Events_State[Timing_events_Num].Web_Data.DataLength) {
            Serial.printf("\n                 ");
          }
        }
      }
    }
    Serial.printf("\r\n");
    Serial.printf("\r\n");
    if(Events_State[Timing_events_Num].repetition_State == Repetition_Hours)
      Serial.printf("        ----- %ld %s\r\n\r\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0] / 3600000, Event_cycle[Repetition]);
    else if(Events_State[Timing_events_Num].repetition_State == Repetition_Minutes)
      Serial.printf("        ----- %ld %s\r\n\r\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0] / 60000, Event_cycle[Repetition]);
    else if(Events_State[Timing_events_Num].repetition_State == Repetition_Seconds)
      Serial.printf("        ----- %ld %s\r\n\r\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0]/ 1000, Event_cycle[Repetition]);
    else if(Events_State[Timing_events_Num].repetition_State == Repetition_Milliseconds)
      Serial.printf("        ----- %ld %s\r\n\r\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0], Event_cycle[Repetition]);
    else
      Serial.printf("        ----- %s\r\n\r\n", Event_cycle[Repetition]);
    Serial.printf("\r\n");
    if(Events_State[Timing_events_Num].repetition_State == Repetition_Hours || Events_State[Timing_events_Num].repetition_State == Repetition_Minutes || Events_State[Timing_events_Num].repetition_State == Repetition_Seconds || Events_State[Timing_events_Num].repetition_State == Repetition_Milliseconds){
      uint32_t Cycle_duration_GCD = calculate_repetition_gcd();
      if(Cycle_duration_GCD){
        Serial.printf("calculate repetition gcd:%ld\r\n",Cycle_duration_GCD);
        Cycle_duration = Cycle_duration_GCD; 
      }
    }
    
    int len = 0;  
    int total_len = 1000;
    char *Event_content = (char *)heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM);
    if (Event_content == NULL) {
      Event_content = (char *)malloc(total_len);
    }
    if (Event_content == NULL) {
        Serial.printf("Memory allocation failed\n");
        Events_State[Timing_events_Num] = Events_State_Default;
        RTC_UnlockEvents();
        return false;
    }
    if(SerialData->SerialPort == 0) {
      if (SerialData->DataType == 1)   // hex
        len += snprintf(Event_content + len, total_len - len, "&nbsp;&nbsp;&nbsp;&nbsp;CAN&nbsp;CH2&nbsp;Send&nbsp;Data&nbsp;&nbsp;(hex):\\n&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
      else
        len += snprintf(Event_content + len, total_len - len, "&nbsp;&nbsp;&nbsp;&nbsp;CAN&nbsp;CH2&nbsp;Send&nbsp;Data&nbsp;(char):\\n&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
    }    
    else if(SerialData->SerialPort == 1){                                                    // CAN CH1
      if (SerialData->DataType == 1)   // hex
        len += snprintf(Event_content + len, total_len - len, "&nbsp;&nbsp;&nbsp;&nbsp;CAN&nbsp;CH1&nbsp;Send&nbsp;Data&nbsp;&nbsp;(hex):\\n&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
      // else                                               // CAN
      //   len += snprintf(Event_content + len, total_len - len, "&nbsp;&nbsp;&nbsp;&nbsp;CAN&nbsp;Send&nbsp;Data&nbsp;(char):\\n&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
    }   
    
    size_t temp_len = SerialData->DataLength * 12 + 1;
    char *temp = (char *)heap_caps_malloc(temp_len, MALLOC_CAP_SPIRAM);
    if (temp == NULL) {
      temp = (char *)malloc(temp_len);
    }
    if (temp == NULL) {
      Serial.printf("RTC event temp buffer allocation failed\n");
      free(Event_content);
      Events_State[Timing_events_Num] = Events_State_Default;
      RTC_UnlockEvents();
      return false;
    }
    temp[0] = '\0';
    if (SerialData->DataType == 1) {  // hex
      for (int i = 0; i < SerialData->DataLength; i++) {
        char hex_byte[6];
        snprintf(hex_byte, sizeof(hex_byte), "0x%02X ", SerialData->SerialData[i]);
        strcat(temp, hex_byte);
        // 姣?0涓瓧鑺傛坊鍔犳崲琛屽拰缂╄繘
        if ((i + 1) % 10 == 0 && (i + 1) < SerialData->DataLength) {
            strcat(temp, "\\n&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
        }
      }
    } 
    else {
      memcpy(temp, SerialData->SerialData, SerialData->DataLength);
      temp[SerialData->DataLength] = '\0';
    }
    len += snprintf(Event_content + len, total_len - len, " %s ", temp);
    if(Events_State[Timing_events_Num].repetition_State == Repetition_Hours)
      len += snprintf(Event_content + len, total_len - len, "\\n&nbsp;&nbsp;&nbsp;&nbsp;----- %ld %s\\n\\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0] / 3600000, Event_cycle[Repetition]);
    else if(Events_State[Timing_events_Num].repetition_State == Repetition_Minutes)
      len += snprintf(Event_content + len, total_len - len, "\\n&nbsp;&nbsp;&nbsp;&nbsp;----- %ld %s\\n\\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0] / 60000, Event_cycle[Repetition]);
    else if(Events_State[Timing_events_Num].repetition_State == Repetition_Seconds)
      len += snprintf(Event_content + len, total_len - len, "\\n&nbsp;&nbsp;&nbsp;&nbsp;----- %ld %s\\n\\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0] / 1000, Event_cycle[Repetition]);
    else if(Events_State[Timing_events_Num].repetition_State == Repetition_Milliseconds)
      len += snprintf(Event_content + len, total_len - len, "\\n&nbsp;&nbsp;&nbsp;&nbsp;----- %ld %s\\n\\n", Events_State[Timing_events_Num].Web_Data.repetition_Time[0], Event_cycle[Repetition]);
    else
      len += snprintf(Event_content + len, total_len - len, "\\n&nbsp;&nbsp;&nbsp;&nbsp;----- %s\\n\\n", Event_cycle[Repetition]);
    // Serial.printf("%s\r\n", Event_content);  

    snprintf(Event_str[Timing_events_Num], sizeof(Event_str[Timing_events_Num]), "Event %d : %s \\n%s\\r\\n", Timing_events_Num + 1, datetime_str,Event_content);
    free(temp);
    free(Event_content);
    Timing_events_Num ++;
    RTC_Update_Alarm();
    RTC_UnlockEvents();
    return true;
  }
  RTC_UnlockEvents();
  return false;
}

void replace_special_chars(char* str) {
    char* pos;
    while ((pos = strstr(str, "&nbsp;")) != NULL) {
        memmove(pos, pos + 6, strlen(pos + 6) + 1);  // 6 = length of "&nbsp;"
        *(pos) = ' ';
    }
    while ((pos = strstr(str, "\\\\")) != NULL) {
        memmove(pos + 1, pos + 2, strlen(pos + 2) + 1);
        *(pos) = '\\';
    }
}
  
void TimerEvent_printf(Timing_RTC event){
  // Replace all "&nbsp;" with a space before printing
  if (event.Event_Number == 0 || event.Event_Number > Timing_events_Number_MAX) {
    return;
  }
  uint8_t event_index = event.Event_Number - 1;
  char event_content_copy[sizeof(Event_str[event_index])];
  strcpy(event_content_copy, Event_str[event_index]);
  replace_special_chars(event_content_copy);

  Serial.printf("%s\r\n", event_content_copy);
}

void TimerEvent_printf_ALL(void)
{
  if (!RTC_LockEvents(1000)) {
    Serial.printf("RTC event mutex timeout while printing events\r\n");
    return;
  }
  Serial.printf("/******************* Current RTC event *******************/ \r\n");
  for (int i = 0; i < Timing_events_Number_MAX; i++) {
    if(Events_State[i].Enable_Flag)
      TimerEvent_printf(Events_State[i]);
  }
  Serial.printf("/******************* Current RTC event *******************/\r\n\r\n ");
  RTC_UnlockEvents();
}
static void TimerEvent_Del_Unlocked(Timing_RTC event){
  Serial.printf("Example Delete an RTC event%d\r\n\r\n",event.Event_Number);
  uint8_t Event_Number = event.Event_Number - 1;
  if (Events_State[Event_Number].Web_Data.SerialData) {                // 娓呴櫎
    free(Events_State[Event_Number].Web_Data.SerialData);
    Events_State[Event_Number].Web_Data.SerialData = NULL;
  }
  for (int i = Event_Number; i < Timing_events_Number_MAX - 1; i++) {
    Events_State[i+1].Event_Number = Events_State[i+1].Event_Number -1;
    Events_State[i] = Events_State[i+1];  
    strcpy(Event_str[i], Event_str[i+1]);
  }  
  Events_State[Timing_events_Number_MAX - 1] = Events_State_Default;
  memset(Event_str[Timing_events_Number_MAX - 1], 0, sizeof(Event_str[Timing_events_Number_MAX - 1]));
  Timing_events_Num --;
  uint32_t Cycle_duration_GCD = calculate_repetition_gcd();
  if(Cycle_duration_GCD){
    Serial.printf("calculate repetition gcd:%ld\r\n",Cycle_duration_GCD);
    Cycle_duration = Cycle_duration_GCD; 
  }
}

void TimerEvent_Del(Timing_RTC event){
  if (!RTC_LockEvents(1000)) {
    Serial.printf("RTC event mutex timeout while deleting event\r\n");
    return;
  }
  TimerEvent_Del_Unlocked(event);
  RTC_Update_Alarm();
  RTC_UnlockEvents();
}
bool TimerEvent_Del_Number(uint8_t Event_Number){
  if (!RTC_LockEvents(1000)) {
    Serial.printf("RTC event mutex timeout while deleting event number\r\n");
    return false;
  }
  if (Event_Number == 0 || Event_Number > Timing_events_Num) {
    Serial.printf("Invalid RTC event number:%u\r\n", Event_Number);
    RTC_UnlockEvents();
    return false;
  }
  TimerEvent_Del_Unlocked(Events_State[Event_Number-1]);
  RTC_Update_Alarm();
  RTC_UnlockEvents();
  return true;
}
