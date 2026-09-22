#include <GxEPD.h>
#include "SPI.h"
#include <WiFi.h>
#include "time.h"
#include <sys/time.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// WiFi 只是備援；留 "*****" 代表不用 WiFi
const char* CONST_SSID   = "*****";
const char* CONST_PSWD   = "*****";

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define SYNC_RETRY_SEC (30*60)   /* 完全沒有時間可用時，休眠多久再重試校時 (秒) */

// 每天兩次醒來：00:00:30 換日重畫 (只用 RTC)，09:00:30 開 BLE 讓 PC 校時
#define WAKE_MARGIN_SEC 30
#define SYNC_HOUR       9

// BLE 校時：PC 端 (tools/ble_time_sync.py) 掃到 BLE_NAME 後，
// 往 BLE_CHR_UUID 寫入 UTC epoch 秒數 (ASCII 十進位，或 4 bytes little-endian)
#define BLE_NAME        "EPD-CAL"
#define BLE_SVC_UUID    "a7c1f0e0-1d2b-4c3d-8e9f-0000c0ffee01"
#define BLE_CHR_UUID    "a7c1f0e0-1d2b-4c3d-8e9f-0000c0ffee02"
#define BLE_WINDOW_SEC  30

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

// 先宣告，讓 loop() 能呼叫後面才定義的函式
void UpdateScreen();
void SetDeepSleep();
void DeepSleepFor(int _seconds);
bool bleTimeSync(int _seconds);
bool wifiTimeSync();

struct tm timeinfo;
RTC_DATA_ATTR bool rtcWakeForSync = false;  // 這次醒來是不是 09:00:30 的校時
RTC_DATA_ATTR int  rtcLastDrawnDay = -1;    // 上次畫的日期 (年*1000+一年中的第幾天)，沒變就不重畫
volatile bool bleGotTime = false;

unsigned long ulReconnectInterval = 20000;  // 重連WIFI時間

int iBatPeresntage;

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

void setTimeZone(){
    //Serial.println("Setting Timezone to CST-8");
    setenv("TZ", "CST-8", 1);  
    tzset();
}

void initWiFi() {  
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setSleep(false);
  delay(100); 

  Serial.println("Connecting to WiFi ..");
  WiFi.begin(CONST_SSID, CONST_PSWD);  

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
}

void GPIO_Reset(){
  // https://zhuanlan.zhihu.com/p/521640890
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_2);
    gpio_reset_pin(GPIO_NUM_4);
    gpio_reset_pin(GPIO_NUM_12);
    gpio_reset_pin(GPIO_NUM_13);
    gpio_reset_pin(GPIO_NUM_14);
    gpio_reset_pin(GPIO_NUM_15);
    gpio_reset_pin(GPIO_NUM_25);
    gpio_reset_pin(GPIO_NUM_26);
    gpio_reset_pin(GPIO_NUM_27);
    gpio_reset_pin(GPIO_NUM_32);
    gpio_reset_pin(GPIO_NUM_33);
    gpio_reset_pin(GPIO_NUM_34);
    gpio_reset_pin(GPIO_NUM_35);
    gpio_reset_pin(GPIO_NUM_36);
    gpio_reset_pin(GPIO_NUM_37);
    gpio_reset_pin(GPIO_NUM_38);
    gpio_reset_pin(GPIO_NUM_39);
}

void setup(){
    Serial.begin(115200);
    delay(100);
    Serial.println();
    
    GPIO_Reset();
    pinMode(PIN_BAT_ADC, INPUT);   
    
    initScreen();    // 初始化螢幕 
    setTimeZone();  // 設定時區
    display.powerDown();
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
    iBatPeresntage = getBatteryPersentage(dBatVolts);  
    Serial.println(String(dBatVolts) + "V, " + String(iBatPeresntage) + "%");

    // RTC 時間是否可用 (deep sleep 期間會保留，冷開機後歸零)
    bool haveTime = getLocalTime(&timeinfo, 0);
    Serial.println(String("haveTime=") + haveTime + ", wakeForSync=" + rtcWakeForSync);

    if(!haveTime || rtcWakeForSync){
        if(!haveTime){  // 冷開機：告訴使用者在等校時
            display.fillScreen(GxEPD_WHITE);        
            display.setTextColor(GxEPD_BLACK);   
            display.setFont(&FreeMonoBold18pt7b); 
            displayText("Time Sync", 30, CENTER_ALIGNMENT);
            displayText("BLE:" BLE_NAME, 70, CENTER_ALIGNMENT);        
            display.updateWindow(0, 0,  250,  122, true);   
        }
        bool synced = bleTimeSync(BLE_WINDOW_SEC);
        if(!synced && String(CONST_SSID) != "*****"){
            synced = wifiTimeSync();
        }
        Serial.println(String("synced=") + synced);
        haveTime = getLocalTime(&timeinfo, 0);
    }

    if(!haveTime){
        // 冷開機又沒校到時間，先睡再試
        Serial.println("No time, retry after " + String(SYNC_RETRY_SEC) + " Seconds");
        rtcWakeForSync = true;
        DeepSleepFor(SYNC_RETRY_SEC);
    }

    Serial.println(&timeinfo, "%F %T %A");

    int today = (timeinfo.tm_year + 1900) * 1000 + timeinfo.tm_yday;
    if(today != rtcLastDrawnDay){
        UpdateScreen();
        rtcLastDrawnDay = today;
    }
    else{
        Serial.println("Same day, skip drawing");
    }
    SetDeepSleep();  
}

// 開 BLE 廣播等 PC 寫入時間，最多等 _seconds 秒
class TimeWriteCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c, esp_ble_gatts_cb_param_t* p) override {
        String v = c->getValue();
        time_t epoch = 0;
        if(v.length() == 4){
            epoch = (uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
        }
        else{
            epoch = strtoul(v.c_str(), NULL, 10);
        }
        if(epoch < 1600000000UL){  // 2020 年以前一律當成錯的
            Serial.println("BLE: bad value");
            return;
        }
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        bleGotTime = true;
        Serial.println("BLE: got epoch " + String((unsigned long)epoch));
    }
};

bool bleTimeSync(int _seconds){
    bleGotTime = false;
    BLEDevice::init(BLE_NAME);
    BLEServer* server = BLEDevice::createServer();
    BLEService* svc = server->createService(BLE_SVC_UUID);
    BLECharacteristic* chr = svc->createCharacteristic(BLE_CHR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
    chr->setCallbacks(new TimeWriteCallback());
    chr->setValue("epoch?");
    svc->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SVC_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.println("BLE: advertising " + String(_seconds) + "s");

    unsigned long t0 = millis();
    while(!bleGotTime && millis() - t0 < (unsigned long)_seconds * 1000){
        delay(100);
    }
    delay(300);  // 讓寫入的回應送出去再關
    BLEDevice::deinit(true);
    return bleGotTime;
}

// WiFi + NTP 校時 (備援)
bool wifiTimeSync(){
    initWiFi(); 
    if(WiFi.status() != WL_CONNECTED){
        WiFi.mode(WIFI_OFF);
        return false;
    }
    configTime(0, 0, "time.stdtime.gov.tw"); 
    setTimeZone();
    bool ok = getLocalTime(&timeinfo);  // 最多等 5 秒
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return ok;
}

void UpdateScreen(){
    // 清空畫布 (只清 buffer，最後 update() 全刷時一起顯示)
    display.fillScreen(GxEPD_WHITE);
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
    display.print(String(iBatPeresntage) + "%");   

    // 畫底線
    display.fillRect (0, 122-5, 250-w-1, 3, GxEPD_BLACK);     
    //display.fillRect (0, 122-7, 250-w-1, 1, GxEPD_BLACK); 
    
    display.update();
    //UpdateWindowFull(10);
    display.powerDown();
}

void SetDeepSleep(){
    int nowSec  = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
    int syncSec = SYNC_HOUR * 3600 + WAKE_MARGIN_SEC;   // 09:00:30
    int dayEnd  = 24 * 3600 + WAKE_MARGIN_SEC;          // 隔天 00:00:30
    int sleepSec;
    if(nowSec < syncSec){
        sleepSec = syncSec - nowSec;
        rtcWakeForSync = true;
    }
    else{
        sleepSec = dayEnd - nowSec;
        rtcWakeForSync = false;
    }
    Serial.println("Next wake in " + String(sleepSec) + "s, sync=" + String(rtcWakeForSync));
    DeepSleepFor(sleepSec);
}

void DeepSleepFor(int _seconds){
    // 不呼叫 esp_sleep_pd_config()：ESP32 core 3.x (IDF 5) 改成引用計數，
    // 沒 ON 過就 OFF 會 assert 當機；deep sleep 預設就會關掉這些電源域
    esp_sleep_enable_timer_wakeup((uint64_t)_seconds * uS_TO_S_FACTOR);    
    Serial.println("Setup ESP32 to sleep for " + String(_seconds) +  " Seconds");
    Serial.flush();
    esp_deep_sleep_start();    
}

