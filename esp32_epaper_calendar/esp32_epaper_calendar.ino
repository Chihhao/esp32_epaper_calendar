#include <GxEPD.h>
#include "SPI.h"
#include <WiFi.h>
#include "time.h"

const char* CONST_SSID   = "*****";
const char* CONST_PSWD   = "*****";

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  15        /* Time ESP32 will go to sleep (in seconds) */

#include <GxGDEH0213B73/GxGDEH0213B73.h>  // 2.13" b/w newer panel
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

#define SPI_MOSI 23
#define SPI_MISO -1
#define SPI_CLK 18

#define ELINK_SS 5
#define ELINK_BUSY 4
#define ELINK_RESET 16
#define ELINK_DC 17

#define BUTTON_PIN  39
#define PIN_BAT_ADC 35

GxIO_Class io(SPI, /*CS=5*/ ELINK_SS, /*DC=*/ ELINK_DC, /*RST=*/ ELINK_RESET);
GxEPD_Class display(io, /*RST=*/ ELINK_RESET, /*BUSY=*/ ELINK_BUSY);

// RTC_DATA_ATTR bool NTP_Setup_OK = false;
struct tm timeinfo;
int TIME_TO_MIDNIGHT = 10;   // 到午夜還剩多少秒

unsigned long ulReconnectInterval = 20000;  // 重連WIFI時間

typedef enum{ RIGHT_ALIGNMENT = 0, LEFT_ALIGNMENT, CENTER_ALIGNMENT } Text_alignment;
void displayText(const String &str, uint16_t y, uint8_t alignment){
  int16_t x = 0;
  int16_t x1, y1;
  uint16_t w, h;
  display.setCursor(x, y);
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h);

  switch (alignment){
    case RIGHT_ALIGNMENT:
      display.setCursor(display.width() - w - x1, y);
      break;
    case LEFT_ALIGNMENT:
      display.setCursor(0, y);
      break;
    case CENTER_ALIGNMENT:
      display.setCursor(display.width() / 2 - ((w + x1) / 2), y);
      break;
    default:
      break;
  }
  display.println(str);
}

void initNTPServer(){
  // const char*     ntpServer = "time.stdtime.gov.tw";
  // const uint32_t  gmtOffset_sec = 8*3600;  // GMT+08:00
  // const uint16_t  daylightOffset_sec = 0;
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  configTime(0, 0, "time.stdtime.gov.tw");

}

void setTimeZone(){
    Serial.println("Setting Timezone to CST-8");
    setenv("TZ", "CST-8", 1);  
    tzset();
}

void printLocalTime(){  
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%F %T %A");  

  int remainHour = 23 - timeinfo.tm_hour;
  int remainMin = 59 - timeinfo.tm_min;
  int remainSec = 60 - timeinfo.tm_sec;
  TIME_TO_MIDNIGHT = remainHour * 3600 + remainMin*60 + remainSec;
  TIME_TO_MIDNIGHT += 30;
  
//  Serial.println("remainHour: " + String(remainHour));
//  Serial.println("remainMin : " + String(remainMin));
//  Serial.println("remainSec : " + String(remainSec));
  Serial.println("TIME_TO_MIDNIGHT: " + String(TIME_TO_MIDNIGHT));  
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setSleep(false);
  delay(100); 

  WiFi.begin(CONST_SSID, CONST_PSWD);
  Serial.println("Connecting to WiFi ..");

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    if (++timeout > ulReconnectInterval/1000) return;
  }
  Serial.println(WiFi.localIP());  
}

void initScreen(){
    SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, ELINK_SS);
    display.init(); // enable diagnostic output on Serial
    display.setRotation(1);
    display.update();
}

void setup(){
    Serial.begin(115200);
    Serial.println();
    pinMode(PIN_BAT_ADC, INPUT);   
    
    initScreen();    // 初始化螢幕 
    setTimeZone();  // 設定時區
    display.powerDown();
}

bool RtcSetupOk() { 
  printLocalTime();
  Serial.println("timeinfo.tm_year = " + String(timeinfo.tm_year+1900) );
  return (timeinfo.tm_year+1900 > 1980); 
}

// 是否為366天的閏年
bool IsLeapYear(int _year){  return (_year%4==0);}

// 本月有幾天
int daysOfMonth(int _year, int _month){  
    // input 0~11
    if(_month==0)     {  return 31; }
    else if(_month==1){ if(IsLeapYear(_year)) { return 29; }
                        else                  { return 28; } }  
    else if(_month==2){  return 31; }
    else if(_month==3){  return 30; }
    else if(_month==4){  return 31; }
    else if(_month==5){  return 30; }
    else if(_month==6){  return 31; }
    else if(_month==7){  return 31; }
    else if(_month==8){  return 30; }
    else if(_month==9){  return 31; }
    else if(_month==10){ return 30; }
    else if(_month==11){ return 31; }
    else{ return 0; }    
}

// 今天18號是禮拜3，請問1號是禮拜幾
int weekDayOfDay1(int _mDay, int _wDay){
    // _mDay : 1-31
    // _wDay : 0-6, 0為週日  
    // Serial.println("----------");  
    for(int i=_mDay; i > 0; i--){
        // Serial.println("i: " + String(i) + ", _mDay: " + String(_mDay)+ ", _wDay: " + String(_wDay));    
        if(i==1) break;   
        if(--_wDay < 0) {
            _wDay = 6;
        }
    }    
    // Serial.println("return _wDay: " + String(_wDay));
    // Serial.println("----------");
    return _wDay; 
}

double mapf(double x, double in_min, double in_max, double out_min, double out_max){
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

double getBatteryVolts(){
  int bat = analogRead(PIN_BAT_ADC) * 2.2 ;  // 2.2 是因為兩個分壓電阻有誤差，不一樣大
  double adc_ratio = ((double)bat/4096.0);  
  double volts = adc_ratio * 3.3;
  return volts;
}

int getBatteryPersentage(double volts){
  double persentage = mapf(volts, 3.2, 3.7, 0, 99);
  if(persentage >= 99){ persentage = 99; }
  if(persentage <= 0){ persentage = 0; }
  return  (int)persentage;
}

void UpdateWindowFull(int _times){
    for(int i=0; i<_times; i++){
      display.updateWindow(0, 0, display.width(), display.height());    
    }
}

void loop(){
    // 獲取電量
    double dBatVolts = getBatteryVolts();
    int dBatPeresntage = getBatteryPersentage(dBatVolts);  
    Serial.println(String(dBatVolts) + "V, " + String(dBatPeresntage) + "%");
//    delay(300);
//    return;
            
    // 連線獲取時間
    if(!RtcSetupOk()){
        display.fillScreen(GxEPD_WHITE);        
        display.setTextColor(GxEPD_BLACK);   
        display.setFont(&FreeMonoBold18pt7b);     
        
        displayText("WIFI", 30, CENTER_ALIGNMENT);
        displayText("Connect...", 70, CENTER_ALIGNMENT);        
        display.updateWindow(0, 0,  250,  122, true);          
        initWiFi(); 
        if(WiFi.status() == WL_CONNECTED){
            initNTPServer();   
            setTimeZone();  // 設定時區    
            printLocalTime();     
        }        
                
        // 顯示 IP
        if(WiFi.status() == WL_CONNECTED){           
          displayText(WiFi.localIP().toString(), 110, CENTER_ALIGNMENT);          
          display.updateWindow(0, 0,  250,  122, true);     
          delay(1000);
        }              
        display.powerDown();  
        return;    
    }
    
//    timeinfo.tm_year = 2024-1900;
//    timeinfo.tm_mon = 1;
//    timeinfo.tm_mday = 28;
//    timeinfo.tm_wday = 3;
    
    // 清空畫布 
    display.fillScreen(GxEPD_WHITE);
    display.update();    
    // UpdateWindowFull(10);
    
    // 畫月曆框框    
    display.setFont(&FreeMonoBold9pt7b); 
    int w = 27, h = 17;
    int top = 2, left = 0;    
    int daysOfThisMonth = daysOfMonth(timeinfo.tm_year, timeinfo.tm_mon);    // 本月有幾天
    int dayAlreadyPrint = 0;  
    int day1_weekday = weekDayOfDay1(timeinfo.tm_mday, timeinfo.tm_wday);
    for(int r = 0; r<7; r++){        
        for(int c = 0; c<7; c++){
            if(r==0){
                int x = left + c * (w - 1);
                int y = top;
                display.fillRect( x, y, w, h-1, GxEPD_BLACK);  
                display.setTextColor(GxEPD_WHITE);
                display.setCursor(x+3, y+13);
                if(c==0){ display.print("Su"); }
                else if(c==1){ display.print("Mo"); }
                else if(c==2){ display.print("Tu"); }
                else if(c==3){ display.print("We"); }
                else if(c==4){ display.print("Th"); }
                else if(c==5){ display.print("Fr"); }
                else if(c==6){ display.print("Sa"); }                   
            }
            else{
                int x = left + c * (w - 1);
                int y = top  + r * (h - 1) + 1;                

                if(c == day1_weekday){
                    if(dayAlreadyPrint == 0) { dayAlreadyPrint++; }
                }
                
                if(dayAlreadyPrint == timeinfo.tm_mday){
                    display.fillRect( x, y, w, h, GxEPD_BLACK);   
                    display.setTextColor(GxEPD_WHITE);
                }
                else{
                    display.drawRect( x, y, w, h, GxEPD_BLACK);  
                    display.setTextColor(GxEPD_BLACK);
                }
                
                if(dayAlreadyPrint>0 && dayAlreadyPrint<=daysOfThisMonth){ 
                    if(dayAlreadyPrint < 10){
                        display.setCursor(x+9, y+14);                 
                    }
                    else{
                        display.setCursor(x+3, y+14);
                    }
                    
                    display.print(dayAlreadyPrint);
                    dayAlreadyPrint++;
                }
                
            }
        }
    }

    // 畫年分框框
    w=66; h=16;
    display.fillRect     (250-w, top, w, h, GxEPD_BLACK);

    // 畫年分   
    left = 250 - w + 11;    
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b); 
    display.setCursor(left, top+13);
    display.print(timeinfo.tm_year + 1900);  

    // 畫右側框框
    top = top + h + 1;
    w=66; h=41;    
    display.fillRect     (250-w, top, w, h, GxEPD_BLACK);
    display.fillRect     (250-w, top + h +1, w, h, GxEPD_BLACK); 

    // 畫右側月份    
    if(timeinfo.tm_mon < 9){ //0~8表示1~9月
        left = 250 - w + 18;
    }
    else{
        left = 250 - w + 5;
    }    
    int h1 = top + h - 7;
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold24pt7b); 
    display.setCursor(left, h1);
    display.print(timeinfo.tm_mon+1);    

    // 畫右側日期    
    if(timeinfo.tm_mday < 10){ 
        left = 250 - w + 18;
    }
    else{
        left = 250 - w + 5;
    }    
    int h2 = top + h + 1 + h - 7;    
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold24pt7b);  
    display.setCursor(left, h2);
    display.print(timeinfo.tm_mday);

    // 畫電池框框
    int bh=17;
    display.fillRect     (250-w, top + h + h + 2, w, bh, GxEPD_BLACK); 
    
    // 畫電池百分比    
    left = 250 - w + 18;    
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b); 
    display.setCursor(left, 122-5);
    display.print(String(dBatPeresntage) + "%");   

    // 畫底線
    display.fillRect (0, 122-5, 250-w-1, 3, GxEPD_BLACK);     
    //display.fillRect (0, 122-7, 250-w-1, 1, GxEPD_BLACK); 
    
    display.update();
    //UpdateWindowFull(10);
    display.powerDown();

    //delay(5000);
    printLocalTime();  
    //esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_sleep_enable_timer_wakeup(TIME_TO_MIDNIGHT * uS_TO_S_FACTOR);    
    Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_MIDNIGHT) +  " Seconds");
    esp_deep_sleep_start();    
  
}
