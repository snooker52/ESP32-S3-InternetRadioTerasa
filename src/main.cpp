#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Audio.h>
#include "main.h"
#include "github_ota.h"

#define HOSTNAME  "radio-terasa"

#define I2S_DOUT  D1 // A1 // GPIO2 connect to DAC pin DIN
#define I2S_BCLK  D2 // A2 // GPIO3 connect to DAC pin BCK
#define I2S_LRC   D3 // A3 // GPIO4 connect to DAC pin LCK
#define NEX_RX   D9
#define NEX_TX   D10
#define nexDisplay Serial2

#define NEX_RET_CMD_FINISHED            (0x01)
#define NEX_RET_EVENT_LAUNCHED          (0x88)
#define NEX_RET_EVENT_UPGRADED          (0x89)
#define NEX_RET_EVENT_TOUCH_HEAD            (0x65)     
#define NEX_RET_EVENT_POSITION_HEAD         (0x67)
#define NEX_RET_EVENT_SLEEP_POSITION_HEAD   (0x68)
#define NEX_RET_CURRENT_PAGE_ID_HEAD        (0x66)
#define NEX_RET_STRING_HEAD                 (0x70)
#define NEX_RET_NUMBER_HEAD                 (0x71)
#define NEX_RET_INVALID_CMD             (0x00)
#define NEX_RET_INVALID_COMPONENT_ID    (0x02)
#define NEX_RET_INVALID_PAGE_ID         (0x03)
#define NEX_RET_INVALID_PICTURE_ID      (0x04)
#define NEX_RET_INVALID_FONT_ID         (0x05)
#define NEX_RET_INVALID_BAUD            (0x11)
#define NEX_RET_INVALID_VARIABLE        (0x1A)
#define NEX_RET_INVALID_OPERATION       (0x1B)

#define NEX_TXT_INTERPRET "interpret"
#define NEX_TXT_STANICE "stanice"
#define NEX_TXT_PISNICKA "pisnicka"
#define NEX_TXT_SIGNAL "signal"

/*
PCM5102A zapojení pinů:
VCC > 3.3v
3.3V > 3.3v
GND > GND
FLT > GND
DMP > GND
SCL > GND
BCK > D2
DIN > D1
LCK > D3
FMT > GND
XMT > 3.3v
*/

//uint8_t max_volume   = 21;
uint8_t max_stations = sizeof(stations) / sizeof(stations[0]);
uint8_t cur_station  = 0;   //index of current stations(nr)
//uint8_t cur_volume   = 0;   //0...21 will be set from stored preferences
//int8_t cur_balance  = 0;   // -16 ...16

IPAddress local_IP(192, 168, 1, 99);
IPAddress gateway(192, 168, 1, 20);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 20); 
IPAddress secondaryDNS(8, 8, 8, 8);

String ssid = WIFI_SSID;
String password = WIFI_PASS;

unsigned long millissignal = 0;
int millisinterval = 2000;

unsigned long millisOtaCheck = 0;
const unsigned long otaCheckInterval = 6UL * 60UL * 60UL * 1000UL;  // kontrola aktualizace a 6 hodin
bool otaCheckProbehl = false;   // prvni kontrola hned po pripojeni k WiFi

TaskHandle_t thRadioPlay = NULL;
//bool WifiConnected = false;
volatile bool pozadavekPripojitStream = false;  // nastavuje se z WiFi eventu / audio tasku, zpracovava loop()
SemaphoreHandle_t nexMutex = NULL;              // chrani zapis na Serial2 z vice tasku

void setup() {
  Serial.begin(115200);
  log_i("Firmware verze: %s", FIRMWARE_VERSION);
  nexDisplay.begin(115200, SERIAL_8N1, NEX_RX, NEX_TX);

  /*
  pref.begin("WebRadio", false);  // instance of preferences for defaults (station, volume ...)
  if (pref.getShort("volume", 1000) == 1000) { 
      pref.putShort("volume", 11);
      pref.putShort("balance", 0);
      pref.putShort("station", 0);
  }
    
  cur_station = pref.getShort("station");
  cur_volume = pref.getShort("volume");    
  cur_balance = pref.getShort("balance");  

  ssid = pref.getString("ssid", "rt-software");
  password = pref.getString("password", "Supertajneheslo2");
  local_IP = pref.getUInt("ip", (uint32_t) local_IP);
  gateway = pref.getUInt("brana", (uint32_t) gateway);
  subnet = pref.getUInt("maska", (uint32_t) subnet);
  primaryDNS = pref.getUInt("pdns", (uint32_t) primaryDNS);
  secondaryDNS = pref.getUInt("sdns", (uint32_t) secondaryDNS);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  if (!WiFi.setHostname(HOSTNAME)) {
    log_e("Hostname failed to configure");
  }
  log_d("Nastavuji Hostname %s", HOSTNAME);
  send_text(NEX_TXT_INTERPRET, "Připojuji k WiFi síti....");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {    
    log_i("Cekam na pripojeni k siti ...");
    delay(1500);
  }
  
  send_text(NEX_TXT_INTERPRET, "WiFi připojeno, spouštím rádio.");
  log_d("Pripojeno, spoustim radio.");
  log_d("RSSI=%d", WiFi.RSSI());
  //setValue(cur_volume, NEX_TXT_HLASITOST);
  */

  nexMutex = xSemaphoreCreateMutex();

  audioInit();
  audioSetVolume(12);
  audioSetBalance(0); // -16 ...16
  nastavWiFi();   // stream se pripoji az po ziskani IP adresy (viz WiFiGotIP a loop)

}

void loop() {

  unsigned long milliscurrent = millis();
  if ((milliscurrent - millissignal) > millisinterval) {
    millissignal = milliscurrent;
    int8_t rssi = WiFi.RSSI();
    send_text(NEX_TXT_SIGNAL, String(rssi));
  }

  if (pozadavekPripojitStream && WiFi.status() == WL_CONNECTED) {
    pozadavekPripojitStream = false;
    audioConnecttohost(stations[cur_station].c_str());
    send_text(NEX_TXT_STANICE, names[cur_station].c_str());
  }

  // kontrola nove verze na GitHubu - hned po pripojeni a pak kazdych 6 hodin
  if (WiFi.status() == WL_CONNECTED && !pozadavekPripojitStream
      && (!otaCheckProbehl || (milliscurrent - millisOtaCheck) > otaCheckInterval)) {
    otaCheckProbehl = true;
    millisOtaCheck = milliscurrent;
    gitHubOtaCheck();
  }
}

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void CreateQueues(){
  log_i("CreateQueues()");
    audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
    audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

void audioTask(void *parameter) {
  log_i("spoustim audioTask()");

    struct audioMessage audioRxTaskMessage;
    struct audioMessage audioTxTaskMessage;

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    //audio.setVolumeSteps(64);
    audio.setVolume(12); // 0...21
    audio.setBalance(0); // -16 ...16

    while(true){
        if(xQueueReceive(audioSetQueue, &audioRxTaskMessage, 2) == pdPASS) {
            switch (audioRxTaskMessage.cmd) {
                case SET_VOLUME:
                    audioTxTaskMessage.cmd = SET_VOLUME;
                    audio.setVolume(audioRxTaskMessage.uint8value);
                    audioTxTaskMessage.ret = 1;
                    xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
                    break;
                case SET_BALANCE:
                    audioTxTaskMessage.cmd = SET_BALANCE;
                    audio.setBalance(audioRxTaskMessage.int8value);
                    audioTxTaskMessage.ret = 1;
                    xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
                    break;
                case GET_VOLUME:
                    audioTxTaskMessage.cmd = GET_VOLUME;
                    audioTxTaskMessage.ret = audio.getVolume();
                    xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
                    break;
                case CONNECTTOHOST:
                    audioTxTaskMessage.cmd = CONNECTTOHOST;
                    audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
                    xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
                    break;
                case CONNECTTOSD:
                    audioTxTaskMessage.cmd = CONNECTTOSD;
                    audioTxTaskMessage.ret = audio.connecttoFS(SD, audioRxTaskMessage.txt);
                    xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
                    break;
                case STOPSONG:
                    audioTxTaskMessage.cmd = STOPSONG;
                    audio.stopSong();
                    audioTxTaskMessage.ret = 1;
                    xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
                    break;
                default:
                    log_i("error");
                    break;
            }            
        }
        audio.loop();
        //vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void audioInit() {
  log_i("audioInit()");
  CreateQueues();   // fronty musi existovat drive, nez je kdokoliv pouzije
  if(!audioSetQueue || !audioGetQueue){
      log_e("queues are not initialized");
      while(true){;}  // endless loop
  }
  xTaskCreatePinnedToCore(
      audioTask,             /* Function to implement the task */
      "audioplay",           /* Name of the task */
      5000,                  /* Stack size in words */
      NULL,                  /* Task input parameter */
      5 | portPRIVILEGE_BIT, /* Priority of the task */
      &thRadioPlay,          /* Task handle. */
      1                      /* Core where the task should run */
   );
}

audioMessage transmitReceive(audioMessage msg){
  xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
  if(xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS){
    if(msg.cmd != audioRxMessage.cmd){
      log_e("wrong reply from message queue");
      }
  }
  return audioRxMessage;
}

void audioSetVolume(uint8_t vol){
    audioTxMessage.cmd = SET_VOLUME;
    audioTxMessage.uint8value = vol;
    audioMessage RX = transmitReceive(audioTxMessage);
}

void audioSetBalance(int8_t bal){
    audioTxMessage.cmd = SET_BALANCE;
    audioTxMessage.int8value = bal;
    audioMessage RX = transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume(){
    audioTxMessage.cmd = GET_VOLUME;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioConnecttohost(const char* host){
    audioTxMessage.cmd = CONNECTTOHOST;
    audioTxMessage.txt = host;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioConnecttoSD(const char* filename){
    audioTxMessage.cmd = CONNECTTOSD;
    audioTxMessage.txt = filename;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioStopSong(){
    audioTxMessage.cmd = STOPSONG;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

// stav OTA aktualizace na displej (volano z github_ota.cpp)
void otaStatus(String zprava){
    send_text(NEX_TXT_INTERPRET, zprava);
}

// prubeh stahovani/instalace OTA aktualizace na displej (volano z github_ota.cpp)
void otaProgress(String zprava){
    send_text(NEX_TXT_PISNICKA, zprava);
}

//*****************************************************************************************
//                                  E V E N T S                                           *
//*****************************************************************************************
void audio_info(const char *info){
  if (strstr(info, "slow stream") != NULL) {
    int8_t rssi = WiFi.RSSI();
    log_d("RSSI=%d", rssi);
    send_text(NEX_TXT_INTERPRET, "Slabý signál RSSI=", String(rssi));
  }
}

void audio_showstreamtitle(const char *info){
    log_d("streamtitle %s", info);
    String strinfo(info);
    int first = strinfo.indexOf(" - ");
    if (first < 0) {   // titulek bez oddelovace - zobraz cely jako pisnicku
        send_text(NEX_TXT_INTERPRET, "");
        send_text(NEX_TXT_PISNICKA, strinfo);
        return;
    }
    send_text(NEX_TXT_INTERPRET, strinfo.substring(0, first));
    send_text(NEX_TXT_PISNICKA, strinfo.substring(first + 3));
}

void audio_eof_stream(const char *info){
  log_d("eof_stream %s", info);
  send_text(NEX_TXT_INTERPRET, "Konec streamu, připojuji znovu.");
  pozadavekPripojitStream = true;   // reconnect provede loop(), ne audio task
}
/*
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl = ");Serial.println(info);
    send_text(NEX_TXT_STANICE, info);
}

void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
    send_text(NEX_TXT_STANICE, info);
}
*/
//*****************************************************************************
//*                        Nextion funkce
//*****************************************************************************

void send_text(String pole, String zprava) {
  zprava.replace('"', '\'');   // uvozovka v textu by ukoncila Nextion prikaz
  if (nexMutex) xSemaphoreTake(nexMutex, portMAX_DELAY);
  nexDisplay.print(pole);
  nexDisplay.print(".txt=\" ");
  nexDisplay.print(zprava);
  nexDisplay.print("\"\xFF\xFF\xFF");
  if (nexMutex) xSemaphoreGive(nexMutex);
}

void send_text(String pole, String zprava, String hodnota) {
  send_text(pole, zprava + hodnota);
}

/*
bool getValue(uint32_t *number, String name)
{
    String cmd = String("get ");
    cmd += name;
    cmd += ".val";
    log_d("getValue()= %s", cmd);
    sendCommand(cmd.c_str());
    return recvRetNumber(number);
}

bool setValue(uint32_t number, String name)
{
    char buf[10] = {0};
    String cmd;
    
    utoa(number, buf, 10);
    cmd += name;
    cmd += ".val=";
    cmd += buf;
    log_d("setValue()= %s", cmd);
    sendCommand(cmd.c_str());
    return recvRetCommandFinished();
}
*/

void sendCommand(const char* cmd)
{
    if (nexMutex) xSemaphoreTake(nexMutex, portMAX_DELAY);
    while (nexDisplay.available())
    {
        nexDisplay.read();
    }

    nexDisplay.print(cmd);
    nexDisplay.write(0xFF);
    nexDisplay.write(0xFF);
    nexDisplay.write(0xFF);
    if (nexMutex) xSemaphoreGive(nexMutex);
}

/*
bool recvRetNumber(uint32_t *number)
{
    bool ret = false;
    uint8_t temp[8] = {0};

    if (!number)
    {
        goto __return;
    }
    
    //nexDisplay.setTimeout(timeout);
    if (sizeof(temp) != nexDisplay.readBytes((char *)temp, sizeof(temp)))
    {
        goto __return;
    }

    if (temp[0] == NEX_RET_NUMBER_HEAD
        && temp[5] == 0xFF
        && temp[6] == 0xFF
        && temp[7] == 0xFF
        )
    {
        *number = ((uint32_t)temp[4] << 24) | ((uint32_t)temp[3] << 16) | (temp[2] << 8) | (temp[1]);
        ret = true;
    }

__return:

    if (ret) 
    {
        //Serial.print("recvRetNumber :");
        //Serial.println(*number);
        log_i("recvRetNumber : %d", *number);
    }
    else
    {
        //Serial.println("recvRetNumber err");
        log_e("recvRetNumber err");
    }
    
    return ret;
}

bool recvRetCommandFinished()
{    
    bool ret = false;
    uint8_t temp[4] = {0};
    
    //nexSerial.setTimeout(timeout);
    if (sizeof(temp) != nexDisplay.readBytes((char *)temp, sizeof(temp)))
    {
        ret = false;
    }

    if (temp[0] == NEX_RET_CMD_FINISHED
        && temp[1] == 0xFF
        && temp[2] == 0xFF
        && temp[3] == 0xFF
        )
    {
        ret = true;
    }

    if (ret) 
    {
        log_i("recvRetCommandFinished ok");
    }
    else
    {
        log_e("recvRetCommandFinished err");
    }
    
    return ret;
}
*/

void nastav_hlasitost(uint8_t number) {
    //pref.putShort("volume", number);
    audioSetVolume(number);
}

void nastav_balance(int8_t number) {
  //pref.putShort("volume", number);
  audioSetBalance(number);
}

/********************************************************************* */
/*                        WiFi                                         */
/********************************************************************* */
void nastavWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(WiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.setHostname(HOSTNAME);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  log_i("Hostname nastaven na %s", HOSTNAME);
  WiFi.setAutoReconnect(true);

  // WiFi.begin() bez BSSID nechava vyber AP na ESP-IDF, ktery casto zvoli
  // vzdalenejsi/slabsi AP misto nejsilnejsi se stejnym SSID - proto radeji
  // sami vyhledame vsechny AP se shodnym SSID a pripojime se primo na tu
  // s nejlepsim RSSI (kanal + BSSID).
  int32_t nejlepsiIndex = -1;
  int32_t nejlepsiRssi = -1000;
  int pocetSiti = WiFi.scanNetworks();
  for (int i = 0; i < pocetSiti; i++) {
    if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > nejlepsiRssi) {
      nejlepsiRssi = WiFi.RSSI(i);
      nejlepsiIndex = i;
    }
  }

  if (nejlepsiIndex >= 0) {
    log_i("Nejsilnejsi AP pro '%s': kanal=%d RSSI=%d BSSID=%s", ssid.c_str(),
          WiFi.channel(nejlepsiIndex), nejlepsiRssi, WiFi.BSSIDstr(nejlepsiIndex).c_str());
    WiFi.begin(ssid.c_str(), password.c_str(), WiFi.channel(nejlepsiIndex), WiFi.BSSID(nejlepsiIndex));
  } else {
    log_i("Sit '%s' nenalezena skenem, pripojuji bez BSSID", ssid.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  WiFi.scanDelete();

  log_i("Cekam na pripojeni k siti ...");
}

void WiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  log_i("WiFiDisconnected()");
  send_text(NEX_TXT_INTERPRET, "Odpojeno od WiFi sítě.");
  // reconnect zajistuje WiFi.setAutoReconnect(true) v nastavWiFi()
}

void WiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  log_i("WiFiConnected()");
  send_text(NEX_TXT_INTERPRET, "Připojeno k WiFi síti.");
  //WifiConnected = true;
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  log_i("WiFiGotIP()");
  //send_text(NEX_TXT_INTERPRET, "Získána IP adresa.");
  log_i("IP adresa: %s", WiFi.localIP().toString().c_str());
  log_i("RSSI=%d", WiFi.RSSI());
  pozadavekPripojitStream = true;   // stream pripoji loop(), event handler nesmi blokovat
  //setValue(cur_volume, NEX_TXT_HLASITOST);
}
