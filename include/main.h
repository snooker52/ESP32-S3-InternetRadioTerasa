
//Preferences pref;
Audio audio; 

String stations[] ={
        "ice.abradio.cz/blanikcz128.mp3",
        "0n-80s.radionetz.de:8000/0n-70s.mp3",
        "www.surfmusic.de/m3u/100-5-das-hitradio,4529.m3u",
        "www.antenne.de/webradio/antenne.m3u",
        "edge.audio.3qsdn.com/senderkw-mp3",
        "macslons-irish-pub-radio.com/media.asx",
};
String names[] = {
    "Rádio BLANÍK CZ",
    "Radionetz DE",
    "Das Hitradio",
    "Antenne",
    "Sender KW",
    "Irish Pub Radio"
};

struct audioMessage{
    uint8_t     cmd;
    const char* txt;
    uint8_t     uint8value;
    int8_t      int8value;
    uint32_t    ret;
} audioTxMessage, audioRxMessage;

enum : uint8_t { SET_VOLUME, SET_BALANCE, GET_VOLUME, CONNECTTOHOST, CONNECTTOSD, STOPSONG };

void CreateQueues();
void audioTask(void *parameter);
void audioInit();
audioMessage transmitReceive(audioMessage msg);
void audioSetVolume(uint8_t vol);
void audioSetBalance(int8_t bal);
uint8_t audioGetVolume();
bool audioConnecttohost(const char* host);
bool audioConnecttoSD(const char* filename);
bool audioStopSong();

void send_text(String pole, String zprava);
void send_text(String pole, String zprava, String hodnota);
bool getValue(uint32_t *number, String name);
bool setValue(uint32_t number, String name);
void sendCommand(const char* cmd);
bool recvRetNumber(uint32_t *number);
bool recvRetCommandFinished();
void nastav_hlasitost(uint8_t number);

void nastavWiFi();
void WiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);