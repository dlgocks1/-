/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 전처리문 블럭 /////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// ESP32 WiFi 연결 및 AWS_IoT Core 연결
#include <AWS_IOT.h>
#include <WiFi.h>
#include <Arduino_JSON.h>

// 앰프+스피커 출력 (mp3파일을 ESP32의 SPIFFS (DAC 저장장치)에 저장 후 출력 (음성 출력 가능)
#define MP3_FILENAME_1 "/iotAbnormalAlarmSound.mp3"
#define MP3_FILENAME_2 "/iotNormalAlarmSound.mp3"
#define MP3_FILENAME_3 "/iotFoodTimeSound.mp3"

#include <Arduino.h>
#ifdef ESP32
#include "SPIFFS.h";
#else
#include <ESP8266WiFi.h>
#endif
#include "AudioFileSourceSPIFFS.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2SNoDAC.h"
#include "AudioOutputI2S.h"

// NTP (시간), 딥슬립 타이머
#include "time.h";

#define BUTTON_PIN_BITMASK 0x200000000 // 2^33 in hex

// 온도 센서
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <math.h>



/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// WiFi 및 AWS-IoT Core ////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// AWS_IoT
AWS_IOT chichiShadow;

// WiFi 설정 (본인 wifi에 맞추기)
const char* ssid = "seed";
const char* password = "seed0518";

// Team AWS Thing
char HOST_ADDRESS[] = "a28y105is1rb18-ats.iot.ap-northeast-2.amazonaws.com";
char CLIENT_ID[] = "chichi_save/download";
char sTOPIC_NAME[] = "chichi_save/download";
char pTOPIC_NAME[] = "chichi_save/upload";

int status = WL_IDLE_STATUS;
int msgCount = 0;
int msgReceived = 0;
char payload[512];
char rcvdPayload[512];


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 앰프 + 스피커 출력 ////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
AudioGeneratorMP3 *mp3;
AudioFileSourceSPIFFS *file;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;

// mp3 출력이 알아서 시작되고 끝나면 멈추는게 아니라
// loop가 돌면서 계속 출력되는 메커니즘을 가짐
// 이 loop에 대한 플래그
boolean mpState = false;
boolean mpLoop = true;

// 알람 소리 설정 (void alarmPlay() 함수의 파라미터로 사용
const int abnormalAlarmMP3 = 1;
const int normalAlarmMP3 = 2;
const int foodTimeAlarmMP3 = 3;


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// NTP 시간 동기화  /////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600*9;
const int daylightOffset_sec = 0;

// deep sleep
// uint64_t == unsigned long long
//const uint64_t uS_TO_S_FACTOR = 1000000;  // 곱해서 ms를 sec로 바꿔줌
//uint64_t TIME_TO_SLEEP;


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 상태 변수 ////////////////////////////////////////////
///////// 강아지 건강 상태, 센싱 모드, publish 모드, 밥 시간 알람 모드 ////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// 강아지 바이탈사인
String healthStatus = "normal";  // 기본값: normal , 이상값 검출 시: abnormal

// 센싱데이터 AWS로 publish 해야하는 상태
// 이미 했거나 안해도 되면 false , 측정완료해서 보내야하면 true
// 오전 or 오후 구분 (시간도 서버에 보낼 것)
boolean sensingPublishStatus_1 = false;
boolean sensingPublishStatus_2 = false;

// 밥 시간 알람 모드 (기본값(알람이 모두 미설정): false, 하나라도 설정: true)
boolean foodAlarmMode = false;

// readNow (실시간 센싱모드)
// 기본: false , 실시간 센싱: true
boolean sensingNow = false;



/////////////////////////////////////////////////////////////////////////////////
/////////////// 바이탈 사인 센싱 관련 변수 선언 /////////// ///////////////////////////
//////////////// 평균 심박 수, 체온, 측정 시간 ///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// 바이탈 사인 검사 시간
// 측정 월
int sensingMonth = 0;

// 오전 7시
const int sensingHour_1 = 7;
int sensingDay_1 = 0;
boolean sensingMode_Day1 = false;

// 오후 10시
const int sensingHour_2 = 2;
int sensingDay_2 = 0;
boolean sensingMode_Day2 = false;

// 심박 수 측정
const int PulseSensorPin = A0;   // A0: ESP32의 VP 핀 (Analog input)
int Signal;                     // raw data. (센싱 데이터) Signal 값 범위: 0-4095
const int Threshold = /*2740*/3000;     // 박동 수로 판단할 Signal 최소 값. Threshold 미만이면 무시
const int limit = 4095;         // ESP32 의 Signal 값 상한. 센서에서 손을 떼면 이 값이 읽히므로 비트가 카운트 되지 않게 하기 위해 필요.

int bpmCnt = 0;                 // 심장 박동 count
int bpm = 0;                    // (bpmCnt / 측정 기간(분)) ==> 분당 평균 심박 수
unsigned long preMil = 0;       // 현재 시간

int pulseFlag = 0;              // Signal >= Threshold: 1 , Signal < Threshold: 0

boolean pulseSensingMode = false;  // while 문 내에서 3분 간 센싱이 끝나면 더 이상 센싱 x
boolean tempSensingMode = false;   // 기본값(센싱X): false , 센싱 모드: true

// 체온 측정
Adafruit_MLX90614 TempSensor = Adafruit_MLX90614();

// float sensingTemp = 0.0;
float temp = 0.0;


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 알람 시간 선언 /////////// /////////////////////////////
//////////////////// 건강 이상 시 소리 알람 시간, 밥 시간 (사용자 설정) //////////////////
/////////////////////////////////////////////////////////////////////////////////
// 건강 이상 감지 시 알람 주기
// abnormalAlarmStatus는 매일 초기화 (abnormalAlarmDay = 0: 기본값)
int abnormalAlarmDay = 0;

// 오전 10시 , 오후 2시, 오후 5시, 오후 8시
const int abnormalHour[4] = {10, 14, 17, 20};
boolean abnormalAlarmStatus[4] = {false, false, false, false};


// 밥 시간
// 사용자 설정 (App -> AWS subscribe)
// 0: 알람 미설정 (시간 * 60 + 분 ==> 시간 = foodTime / 60 , 분 = foodTime % 60)
// foodAlarmStatus는 매일 초기화 (foodAlarmDay = 0: 기본값)
int foodAlarmDay[5] = {0, 0, 0, 0, 0};
int foodTime[5] = {-1, -1, -1, -1, -1};
boolean foodAlarmStatus[5] = {false, false, false, false, false};
int foodHour[5] = {-1, -1, -1, -1, -1};
int foodMin[5] = {-1, -1, -1, -1, -1};


///////////////////////////////////////////////////////////////////////////
////////////////////////// Call Back 함수 /////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// AWS-IoT Core Callback Handler
void mySubCallBackHandler(char* topicName, int payloadLen, char* payLoad) {
  strncpy(rcvdPayload, payLoad, payloadLen);
  rcvdPayload[payloadLen] = 0;
  
  // 테스트 코드
  Serial.print("test: 구독한 토픽 (1: upload , 2: falg): ");
  msgReceived = 1;
  Serial.println(msgReceived);
  Serial.println("test: 이 윗줄에 1 또는 2가 나왔어야 성공");
}

// 앰프+스피커 출력 Callback 함수 (metadata event occurs)
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
  (void)cbData;
  Serial.printf("ID3 callback for: %s = '", type);
  
  if (isUnicode) {
    string += 2;
  }
  
  while (*string) {
    char a = *(string++);
    if (isUnicode) {
      string++;
    }
    Serial.printf("%c", a);
  }
  
  Serial.printf("'\n");
  Serial.flush();
}
//
//// Wake up 된 이유 출력.
//void print_wakeup_reason() {
//  esp_sleep_wakeup_cause_t wakeup_reason;
//  wakeup_reason = esp_sleep_get_wakeup_cause();
//
//  switch(wakeup_reason) {
//    // 사용자가 스위치를 눌러서 깨운 경우 아래 메세지만 출력되어야 함.
//    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("스위치를 눌러서 깨어남"); break;
//    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
//    // Timer 인 경우는 아래 메세지만 출력되어야 함.
//    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("타이머에 의해 깨어남"); break;
//    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
//    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
//    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
//  }
//}


///////////////////////////////////////////////////////////////////////////
////////////////////////// 알람 소리 MP3 재생 함수 //////////////////////////
//////////////////////////////////////////////////////////////////////////
// 알람 소리 mp3 파일 재생 함수
// parameter (abnormalAlarmMP3, normalAlarmMP3, foodTimeAlarmMP3)
void alarmPlay(int mp3Idx) {
  if (mp3Idx == 1) {
    Serial.println("abnormal alarm play");
    file = new AudioFileSourceSPIFFS(MP3_FILENAME_1);
  } else if (mp3Idx == 2) {
    Serial.println("normal alarm play");
    file = new AudioFileSourceSPIFFS(MP3_FILENAME_2);
  } else if (mp3Idx == 3) {
    Serial.println("food time alarm play");
    file = new AudioFileSourceSPIFFS(MP3_FILENAME_3);
  }

  while((mpLoop)) {
    if (mpState == false) {
      mpState = true;
      id3 = new AudioFileSourceID3(file);
      id3->RegisterMetadataCB(MDCallback, (void*)"ID3TAG");
      out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
      mp3 = new AudioGeneratorMP3();
      mp3->begin(id3, out);
      out->SetChannels(1);
      out->SetGain(0.15);
    }
  
    if (mp3->isRunning()) {
      if (!mp3->loop()) mp3->stop();
    } else {
      Serial.printf("Alarm Play Done\n");
      mpState = false;
      delete file;
      delete id3;
      delete out;
      delete mp3;
      mpLoop = false;
    }
  }
  
  mpLoop = true;
 
}


///////////////////////////////////////////////////////////////////////
///////////////////////////// 셋 업 ////////////////////////////////////
///////////// WiFi 및 AWS-IoT Core 연결, NTP 동기화 /////////////////////
///////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);

  // 알람 소리 설정
  WiFi.mode(WIFI_OFF);  // 만약 이 줄이 코드에 악영향을 끼치면 지우고 하기 (원래 예제 코드에 포함되어있어서 혹시 몰라서 일단 넣음, 예제 코드에서 지우고 해도 잘 되긴 했음)
  delay(1000);
  if (!SPIFFS.begin(false)) 
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  audioLogger = &Serial;
  
  // Wifi 및 AWS-IoT Core 연결
  Serial.print("WIFI status = ");
  Serial.println(WiFi.getMode());
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(1000);
  Serial.print("WIFI status = ");
  Serial.println(WiFi.getMode());
  WiFi.begin(ssid, password);
  
  while(WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  
  Serial.println("Connected to WiFi");
  
  if(chichiShadow.connect(HOST_ADDRESS, CLIENT_ID) == 0) {
    Serial.println("Connected to AWS");
    delay(1000);
    
    if(0 == chichiShadow.subscribe(sTOPIC_NAME, mySubCallBackHandler)) {
      Serial.println("Subscribe Successfull");
    } else {
      Serial.println("Subscribe Failed, check the Thing Name and Certificates");
      while(1);
    }
    
  } else {
    Serial.println("AWS connection failed, Check the HOST Address");
    while(1);
  }
  
  delay(2000);

  // NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  
}  // setup() 끝


///////////////////////////////////////////////////////////////////////
///////////////////////////// 루프 ////////////////////////////////////
//////////// NTP 동기화 시간 타이머 (struct tm timeinfo) ////////////////
///// AWS subscribe (Shadow) (강아지 건강 상태, 밥 시간 알람 설정) ////////
///// AWS subscribe (센싱 데이터 (평균 심박수 + 체온 + 측정 시간) /////////
///////////////////////////////////////////////////////////////////////
void loop() {
  ///////////////////// NTP 동기화 시간 타이머 //////////////////////////
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {  // 현재 시간 받아오기 (실패하면 아래 에러 출력)
    Serial.println("Failed to obtain time");
    return;
  }

  ///////////////////// AWS subscribe //////////////////////////
  // AWS Core <--> ESP32 (subscribe, publish)
  if(msgReceived == 1) {
    msgReceived = 0;
    Serial.print("Received Message: ");
    Serial.println(rcvdPayload);

    // Parse JSON
    JSONVar myObj = JSON.parse(rcvdPayload);
    String messageVar = (const char*) myObj["message"];

    if (messageVar == "stateMessage") {  // 건강 상태 메세지
      String healthStatus = (const char*) myObj["status"];
      
      Serial.print("Dog's Heart Beat is ");
      Serial.print(healthStatus);
      // 테스트 코드
      Serial.println("test: 이 위에 abnormal이나 normal이 나왔어야 성공");

      // 바이탈 사인이 이상할 경우 & 정상일 경우 각각 음성 메세지 출력
      if(healthStatus == "abnormal") {
        alarmPlay(abnormalAlarmMP3);
        // 테스트 코드
        Serial.println("test: 앞에서 abnormal 알람이 울렸어야 성공");
      } else if (healthStatus == "normal") {
        alarmPlay(normalAlarmMP3);
        abnormalAlarmDay = 0;  // abnormal 알람 설정 초기화 (다시 abnormal 상태가 될 때 충돌 방지)
        Serial.println("이 앞에서 normal 알람이 울렸어야 성공");
      
      }

    } else if (messageVar == "nowMessage") {  // 실시간 측정 메세지
      String readNow = (const char*) myObj["readNow"];

      // 테스트 코드
      Serial.print("JSON으로 받아온 readNow 값: ");
      Serial.println(readNow);
      
      if (readNow == "0") {  // 실시간 측정 모드 OFF
        sensingNow = false;

        // 테스트 코드
        Serial.println("test: 실시간 측정 모드 OFF");
      
      } else if (readNow == "1") {  // 실시간 측정 모드 ON
        sensingNow = true;

        // 테스트 코드
        Serial.println("test: 실시간 측정 모드 ON");
      }

      // 테스트 코드
      Serial.println("test: 이 위에 실시간 측정 모드 ON or OFF 가 나왔어야 성공");
      
    } else if (messageVar == "foodMessage") {  // 밥 시간 알람 설정
      String foodTimeSet = (const char*) myObj["foodTime1"];
      if (foodTime[0] != foodTimeSet.toInt()) {
        foodTime[0] = foodTimeSet.toInt();
        if (foodTime[0] < 0) {
          foodHour[0] = -1;
          foodMin[0] = -1;
        } else {
          foodHour[0] = foodTime[0]/60;
          foodMin[0] = foodTime[0]%60;
        }
      
        // 설정한 알람 시간에 정확히 알람 소리를 못 낼 가능성이 있음
        // ex> 센싱 도중 or 다른 알람 소리 출력 중 등
        // 이를 해결하기 위해 hour는 같고 min은 지났으면 알람 소리를 출력하게 함
        // 그리고 알람을 새로 설정했을 때 시간은 같은데 분이 지났을 때 설정하고 바로 소리가 출력될 수 있어서
        // 아래는 if문은 이를 방지하기 위한 코드임
        if(!((foodHour[0] == timeinfo.tm_hour) && (foodMin[0] <= timeinfo.tm_min))) {
          foodAlarmDay[0] = 0;
        } 
        
      } // foodTime 1번 알람 세팅

      foodTimeSet = (const char*) myObj["foodTime2"];
      if (foodTime[1] != foodTimeSet.toInt()) {
        foodTime[1] = foodTimeSet.toInt();
        if (foodTime[1] < 0) {
          foodHour[1] = -1;
          foodMin[1] = -1;
        } else {
          foodHour[1] = foodTime[1]/60;
          foodMin[1] = foodTime[1]%60;
        }
        if(!((foodHour[1] == timeinfo.tm_hour) && (foodMin[1] <= timeinfo.tm_min))) {
          foodAlarmDay[1] = 0;
        }   
      
      } // foodTime 2번 알람 세팅

      foodTimeSet = (const char*) myObj["foodTime3"];
      if (foodTime[2] != foodTimeSet.toInt()) {
        foodTime[2] = foodTimeSet.toInt();
        if (foodTime[2] < 0) {
          foodHour[2] = -1;
          foodMin[2] = -1;
        } else {
          foodHour[2] = foodTime[2]/60;
          foodMin[2] = foodTime[2]%60;
        }
        if(!((foodHour[2] == timeinfo.tm_hour) && (foodMin[2] <= timeinfo.tm_min))) {
          foodAlarmDay[2] = 0;
        }
      
      }  // foodTime 3번 알람 세팅

      foodTimeSet = (const char*) myObj["foodTime4"];
      if (foodTime[3] != foodTimeSet.toInt()) {
        foodTime[3] = foodTimeSet.toInt();
        if (foodTime[3] < 0) {
          foodHour[3] = -1;
          foodMin[3] = -1;
        } else {
          foodHour[3] = foodTime[3]/60;
          foodMin[3] = foodTime[3]%60;
        }
        if(!((foodHour[3] == timeinfo.tm_hour) && (foodMin[3] <= timeinfo.tm_min))) {
          foodAlarmDay[3] = 0;
        }
      
      }  // foodTime 4번 알람 세팅

      foodTimeSet = (const char*) myObj["foodTime5"];
      if (foodTime[4] != foodTimeSet.toInt()) {
        foodTime[4] = foodTimeSet.toInt();
        if (foodTime[4] < 0) {
          foodHour[4] = -1;
          foodMin[4] = -1;
        } else {
          foodHour[4] = foodTime[4]/60;
          foodMin[4] = foodTime[4]%60;
        }
        if(!((foodHour[4] == timeinfo.tm_hour) && (foodMin[4] <= timeinfo.tm_min))) {
          foodAlarmDay[4] = 0;
        }
      
      }  // foodTime 5번 알람 세팅

      if ((foodTime[0] != -1) || (foodTime[1] != -1) || (foodTime[2] != -1) || (foodTime[3] != -1) || (foodTime[4] != -1)) {
        foodAlarmMode = true;
      } else {
        foodAlarmMode = false;
      }

      // 테스트 코드
      Serial.println("test: 테스트를 위해 대표적으로 foodTime 1 알람만 테스트");
      Serial.print("test: food 알람 1번: ");
      Serial.println(foodHour[0]);
      Serial.println("test: 이 위에서 1번 알람 설정한거 시간이 나왔어야 성공");
      Serial.println("test: JSON 감지 끝");
      
       
    }  

    
  }  // if(msgReceived == 1) 끝 (delta message subscribe하는 경우 끝)

 
  ////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////// AWS JSON Subscribe 부분 끝 ////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////// loop 내 esp32 동작 부분 시작 ////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////// 바이탈사인 검사 (실시간) ///////////////////////////////
  if ((sensingNow)) {
    Serial.println("test: 실시간 검사모드입니데이");
    float nowTemp = 0.0;
    // int tempCnt = 0;
    // float sensingTemp = 0.0;
    delay(300);

    // 테스트 코드
    Serial.println("test: 실시간 검사 시작");
    Serial.println("test: 체온 측정 시작");
    
    while (tempCnt < 5) {  // 실시간 검사 (신속성을 위해 4번만 측정 (2.0초 소요)
      sensingTemp += TempSensor.readObjectTempC();
      tempCnt++;
      delay(500);
      
    }  // 체온 측정 while 끝
    
    // 4회 측정 평균 체온 계산 (소수점 2자리 반올림)
    nowTemp = roundf((nowTemp / 4) * 100) / 100;
    
//    // 오버플로우 대책
//    delay(20);
//    nowTemp = 37.3;
//    delay(500);

    // 테스트 코드
    Serial.println("test: 심박수 측정 시작");

    unsigned long preMil_Now = millis();
    int nowBpm = 0;
    int nowBpmCnt = 0;
    
    while(true) {  // 심박 수 측정
      Signal = analogRead(PulseSensorPin);

      if (Signal >= Threshold && Signal < limit) {  // Signal이 Threshold 이상이면 pulseFlag = 1 로 세팅
        pulseFlag = 1;
        
      } else if (Signal < Threshold) {
        if (pulseFlag == 1) {
          nowBpmCnt++;
          
        }
        pulseFlag = 0;  // pulseFlag 초기화
        
      }

      if ((millis()-preMil_Now > 6000)) {  // 6초동안 센싱 후 평균 분당 심박수 예측 계산
        preMil_Now = 0;
        nowBpm = nowBpmCnt*10;
        Serial.print("실시간 심박 수 센싱 결과 BPM: ");
        Serial.println(nowBpm);
        nowBpmCnt = 0;
        break;
        
      }
      
    }  // 심박 수 측정 while 끝

    // nowTemp, nowBpm publish
    // 측정 시간 + 평균 심박수 + 체온
    int nowSensingMon = timeinfo.tm_mon + 1;
    int nowSensingDay = timeinfo.tm_mday;
    int nowSensingHour = timeinfo.tm_hour;

    // 테스트 코드
    Serial.println("test: 실시간 센싱 결과 전송 시작");

    
    sprintf(payload, "{\"month\":%d, \"day\":%d, \"hour\":%d, \"bpm\":%d, \"temp\":%f}", nowSensingMon, nowSensingDay, nowSensingHour, nowBpm, nowTemp);
    while(true) {
      delay(500);
      if (chichiShadow.publish(pTOPIC_NAME, payload) == 0) {
        Serial.print("Publish Message (실시간 센싱 결과값 AWS로 전송): ");
        Serial.println(payload);
        break;
      } else {
        Serial.println("Publish failed");
        delay(500);
      }
    }
    

    nowBpm = 0;
    nowTemp = 0;

    // 테스트 코드
    Serial.println("test: 이 위에 전송한 JSON 나왔어야 성공");
    
  }  // 실시간 센싱 + publish 완료
  // while문으로 묶기


  ///////////////////////////// 바이탈사인 검사 (주기) ///////////////////////////////
  if ((sensingDay_1 != timeinfo.tm_mday) || (sensingDay_2 != timeinfo.tm_mday)) {
    // 모드 설정
    sensingDay_1 = timeinfo.tm_mday;
    sensingMode_Day1 = true;
    
    sensingDay_2 = timeinfo.tm_mday;
    sensingMode_Day2 = true;
    
  }
  
  if (((sensingMode_Day1) && (sensingHour_1 == timeinfo.tm_hour)) || ((sensingMode_Day2) && (sensingHour_2 == timeinfo.tm_hour))) {
    // 오전 or 오후 구분
    if (sensingHour_1 == timeinfo.tm_hour) {
      sensingDay_1 = timeinfo.tm_mday;
      sensingPublishStatus_1 = true;
      
    } else if (sensingHour_2 == timeinfo.tm_hour) {
      sensingDay_2 = timeinfo.tm_mday;
      sensingPublishStatus_2 = true;
    }

    sensingMonth = timeinfo.tm_mon + 1;
    pulseSensingMode = true;
    tempSensingMode = true;
    


    // 5초(체온) + 3분(심박 수) 센싱
    while((pulseSensingMode) || (tempSensingMode)){
      // 센싱 끝났으면 while문 나가기
      if (!(pulseSensingMode) && !(tempSensingMode)) {
        
        // test code
        Serial.println("test: 주기적 센싱 끝");
        
        break;
      }

      if ((tempSensingMode)) {  // 체온 측정
        float sensingTemp = 0.0;
        // 제대로 회로 구성이 안되있으면 (단선, 접촉 불량 등) 아래 에러코드 출력
        if (!TempSensor.begin()) {
          Serial.println("Error connecting to MLX sensor. Check wiring.");
          while (1);
          
        }
        delay(300);
        int tempCnt = 0;
        while(tempCnt < 10) {  // 10회 측정 (0.5초 간격 , 총 5초)
          sensingTemp += TempSensor.readObjectTempC();
          tempCnt++;
          delay(500);
          
        }
        sensingTemp = sensingTemp / 10;  // 10회 측정 평균 체온 계산
        temp = roundf(sensingTemp * 100) / 100;  // 소수점 2자리 반올림
        sensingTemp = 0;  // sensingTemp 초기화
        tempSensingMode = false;
        preMil = millis();  // 타이머 시작 (바로 심박 수 센싱 시작)
        
      }  // 체온 측정 부분 끝

      while(true) {
        if((pulseSensingMode)) {  // 심박 수 측정
          Signal = analogRead(PulseSensorPin);

          if(Signal >= Threshold && Signal < limit) { // Signal 이 Threshold 이상이면 pulseFlag = 1 로 세팅
            pulseFlag = 1;
        
          } else if (Signal < Threshold) {
            if (pulseFlag == 1) { // pulseFlag 확인 후 1이라면 bpmCnt++
              bpmCnt++;           // ( Signal >= Threshold --> Signal < Threshold ==> Count++)
          
            }        
            pulseFlag = 0;  // pulseFlag 초기화
                
          }

          if((millis()-preMil) > /*60000*/ 60000) { // 3분동안 센싱 후 평균 계산
            preMil = 0;
            bpm = bpmCnt/1;
            Serial.print("심박 수 센싱 결과 BPM : ");
            Serial.println(bpm);  // Serial monitor 에 bpm 출력하고 싶으면 주석 제거.
            bpmCnt = 0;             // count 초기화
            pulseSensingMode = false;
            break;
        
          }
          delay(20);  // 센싱에 delay를 줘서 값 안정화에 기여      
        
        }  // 심박 수 측정 모드 끝
        
      }  // 심박 수 측정 부분 끝 (while(true) {)
    
      delay(20);  // 센싱에 delay를 줘서 값 안정화에 기여
      
    }  // 센싱 완료 (while문 끝)

    // 센싱 완료 후 데이터 publish (평균 심박수: bpm , 평균 체온: temp , 측정 시간: sensingHour_1 (or _2))
    // 측정 월, 측정 일, 측정 시간
    if ((sensingPublishStatus_1) || (sensingPublishStatus_2)) {

      // test code
      Serial.println("test: 주기적 센싱 결과 JSON으로 보내기 시작");
      
      int sensingDay = 0;
      int sensingHour = 0;
      if ((sensingPublishStatus_1)) {
        // 서버에 보낼 측정 시간 세팅 ( 측정 시간 변수 따로 두기): (sensingHour_1)
        // JSON으로 PUBLISH
        sensingDay = sensingDay_1;
        sensingHour = sensingHour_1;
        sensingMode_Day1 = false;
              
      } else if ((sensingPublishStatus_2)) {
        // 서버에 보낼 측정 시간 세팅 ( 측정 시간 변수 따로 두기): (sensingHour_2)
        sensingDay = sensingDay_2;
        sensingHour = sensingHour_2;
        sensingMode_Day2 = false;
        
      }

      // (측정 시간 + 평균 심박수 + 체온) ==> AWS로 publish
      
      sprintf(payload, "{\"month\":%d, \"day\":%d, \"hour\":%d, \"bpm\":%d, \"temp\":%f}", sensingMonth, sensingDay, sensingHour, bpm, temp);
      while(true) {
        if (chichiShadow.publish(pTOPIC_NAME, payload) == 0) {
          Serial.print("Publish Message (센싱 결과값 AWS로 전송): ");
          Serial.println(payload);
          // publish 끝나고 다시 sensingPublishStatus = false 로 초기화
          sensingPublishStatus_1 = false;
          sensingPublishStatus_2 = false;

          // 테스트 코드
          Serial.println("test: 주기적 센싱 결과 JSON 전송 완료");
          delay(1000);
          break;
        
        } else {
          Serial.println("Publish failed");
          delay(1000);
        }
      }
      
    
    }  // publish 완료
    
  }  // 센싱 부분 + publish 부분 끝
  

  ///////////////////////////// 건강 이상 주기적 알람 ///////////////////////////////
  // 바이탈 사인 이상 시 주기적 알람 (아침 저녁에 이상 상태를 aws에서 받을 때 + 점심쯤 2번
  if (healthStatus =="abnormal") {
    // 매일 abnormalAlarmStatus 초기화
    if (abnormalAlarmDay != timeinfo.tm_mday) {
      abnormalAlarmDay = timeinfo.tm_mday;
      abnormalAlarmStatus[0] = true;
      abnormalAlarmStatus[1] = true;
      abnormalAlarmStatus[2] = true;
      abnormalAlarmStatus[3] = true;
    }

    if (((abnormalAlarmStatus[0]) && abnormalHour[0] == timeinfo.tm_hour) || ((abnormalAlarmStatus[1]) && abnormalHour[1] == timeinfo.tm_hour) || ((abnormalAlarmStatus[2]) && abnormalHour[2] == timeinfo.tm_hour) || ((abnormalAlarmStatus[3]) && abnormalHour[3] == timeinfo.tm_hour)) {
      // 해당 일에 아직 발생하지 않은 소리 알람만 발생됨
      alarmPlay(abnormalAlarmMP3);  

      if ((abnormalAlarmStatus[0]) && abnormalHour[0] == timeinfo.tm_hour) {
        abnormalAlarmStatus[0] = false;
      } else if ((abnormalAlarmStatus[1]) && abnormalHour[1] == timeinfo.tm_hour) {
        abnormalAlarmStatus[1] = false;
      } else if ((abnormalAlarmStatus[2]) && abnormalHour[2] == timeinfo.tm_hour) {
        abnormalAlarmStatus[2] = false;
      } else if ((abnormalAlarmStatus[3]) && abnormalHour[3] == timeinfo.tm_hour) {
        abnormalAlarmStatus[4] = false;
      }
        
    }  // abnormal 알람 시간되면 소리 알람 발생 끝
            
  }  // abnormal 주기적 알람 끝
  
  
  ///////////////////////////// 밥 시간 알람 ///////////////////////////////
  // 알람이 하나라도 설정되어 있어야만 코드 동작 (foodAlarmMode == true 일 때)
  // 미설정된 알람은 무시 (foodTime[0] < 0)
  if ((foodAlarmMode)) {
    if ((foodTime[0] >= 0) && (foodAlarmDay[0] != timeinfo.tm_mday)) {
      foodAlarmDay[0] = timeinfo.tm_mday;
      foodAlarmStatus[0] = true;
    }
    if ((foodTime[1] >= 0) && (foodAlarmDay[1] != timeinfo.tm_mday)) {
      foodAlarmDay[1] = timeinfo.tm_mday;
      foodAlarmStatus[1] = true;
    }
    if ((foodTime[2] >= 0) && (foodAlarmDay[2] != timeinfo.tm_mday)) {
      foodAlarmDay[2] = timeinfo.tm_mday;
      foodAlarmStatus[2] = true;
    }
    if ((foodTime[3] >= 0) && (foodAlarmDay[3] != timeinfo.tm_mday)) {
      foodAlarmDay[3] = timeinfo.tm_mday;
      foodAlarmStatus[3] = true;
    }
    if ((foodTime[4] >= 0) && (foodAlarmDay[4] != timeinfo.tm_mday)) {
      foodAlarmDay[4] = timeinfo.tm_mday;
      foodAlarmStatus[4] = true;
    }

    // 각 알람 시간이 되면 소리 알람 발생
    if ((foodAlarmStatus[0]) && (foodHour[0] == timeinfo.tm_hour) && (foodMin[0] <= timeinfo.tm_min)) {
      alarmPlay(foodTimeAlarmMP3);
      foodAlarmStatus[0] = false;
      
    } else if ((foodAlarmStatus[1]) && (foodHour[1] == timeinfo.tm_hour) && (foodMin[1] <= timeinfo.tm_min)) {
      alarmPlay(foodTimeAlarmMP3);
      foodAlarmStatus[1] = false;
      
    } else if ((foodAlarmStatus[2]) && (foodHour[2] == timeinfo.tm_hour) && (foodMin[2] <= timeinfo.tm_min)) {
      alarmPlay(foodTimeAlarmMP3);
      foodAlarmStatus[2] = false;
      
    } else if ((foodAlarmStatus[3]) && (foodHour[3] == timeinfo.tm_hour) && (foodMin[3] <= timeinfo.tm_min)) {
      alarmPlay(foodTimeAlarmMP3);
      foodAlarmStatus[3] = false;
      
    } else if ((foodAlarmStatus[4]) && (foodHour[4] == timeinfo.tm_hour) && (foodMin[4] <= timeinfo.tm_min)) {
      alarmPlay(foodTimeAlarmMP3);
      foodAlarmStatus[4] = false;
      
    }  // foodTime 되면 밥달라고 소리 알람 발생 끝
    
  }  // 밥 시간 알람 끝


//  // 딥 슬립
//  if (!sensingNow) {  // 실시간 측정 모드라면 딥 슬립에 빠지면 안됨
//    int nowHour_d = timeinfo.tm_hour;
//    int nowMin_d = timeinfo.tm_min;
//    int nowSec_d = timeinfo.tm_sec;
//
//    // 현재 시간을 제대로 받았는지 확인
//    Serial.printf("now time:    %d : %d : %d", nowHour_d, nowMin_d, nowSec_d);
//
//    // wakeup timer 설정
    
    
    
    
  }


}  // loop() 끝
