/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 전처리문 블럭 /////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// ESP32 WiFi 연결 및 AWS_IoT Core 연결
#include <AWS_IOT.h>
#include <WiFi.h>
#include <Arduino_JSON.h>

// 앰프+스피커 출력 (mp3파일을 ESP32의 SPIFFS (DAC 저장장치)에 저장 후 출력 (음성 출력 가능)
#define MP3_FILENAME_1 "/iotFoodTimeSound.mp3"
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

// NTP (시간)
#include "time.h";


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// WiFi 및 AWS-IoT Core ////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// AWS_IoT Shadow (이 Shadow랑 JSON + Topic 부분들은 AWS랑 맞춰서 수정해야함)
AWS_IOT testShadow;

// WiFi 설정 (본인 wifi에 맞추기)
const char* ssid = "KT_GiGA_B366";
const char* password = "dga27xh293";

// Team AWS Thing
char HOST_ADDRESS[] = "a28y105is1rb18-ats.iot.ap-northeast-2.amazonaws.com";
char CLIENT_ID[] = "chichi_save";
char sTOPIC_NAME[] = "chichi_save/up";
char pTOPIC_NAME[] = "chichi_save/down";

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


/////////////////////////////////////////////////////////////////////////////////
/////////////// 바이탈 사인 센싱 관련 변수 선언 /////////// ///////////////////////////
//////////////// 평균 심박 수, 체온, 측정 시간 ///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
// 바이탈 사인 검사 시간
// 오전 7시
const int sensingHour_1 = 7;
// 오후 10시
const int sensingHour_2 = 22;

// 심박 수 측정
const int PulseSensorPin = A0;   // A0: ESP32의 VP 핀 (Analog input)
int Signal;                     // raw data. (센싱 데이터) Signal 값 범위: 0-4095
const int Threshold = 2740;     // 박동 수로 판단할 Signal 최소 값. Threshold 미만이면 무시
const int limit = 4095;         // ESP32 의 Signal 값 상한. 센서에서 손을 떼면 이 값이 읽히므로 비트가 카운트 되지 않게 하기 위해 필요.

int bpmCnt = 0;                 // 심장 박동 count
int bpm = 0;                    // (bpmCnt / 측정 기간(분)) ==> 분당 평균 심박 수
unsigned long preMil = 0;       // 현재 시간

int pulseFlag = 0;              // Signal >= Threshold: 1 , Signal < Threshold: 0

boolean pulseSensingMode = false;  // while 문 내에서 3분 간 센싱이 끝나면 더 이상 센싱 x
boolean tempSensingMode = false;   // 기본값(센싱X): false , 센싱 모드: true

// 체온 측정

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
  msgReceived = 1;
  
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
  
  if(testShadow.connect(HOST_ADDRESS, CLIENT_ID) == 0) {
    Serial.println("Connected to AWS");
    delay(1000);
    
    if(0 == testShadow.subscribe(sTOPIC_NAME, mySubCallBackHandler)) {
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
///// AWS subscribe (shadow) (강아지 건강 상태, 밥 시간 알람 설정) ////////
///// AWS subscribe (센싱 데이터 (평균 심박수 + 체온 + 측정 시간) /////////
///////////////////////////////////////////////////////////////////////
void loop() {
  ///////////////////// NTP 동기화 시간 타이머 //////////////////////////
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
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
    JSONVar state = myObj["state"];

    //////////////// JSON 바이탈사인 정상 or 이상 ///////////////////
    String healthStatus = (const char*) state["healthStatus"];
    Serial.print("Dog's Heart Beat is ");
    Serial.print(healthStatus);

    // 바이탈 사인이 이상할 경우 & 정상일 경우 각각 음성 메세지 출력
    if(healthStatus == "abnormal") {
      alarmPlay(abnormalAlarmMP3);
    } else if (healthStatus == "normal") {
      alarmPlay(normalAlarmMP3);
      abnormalAlarmDay = 0;  // abnormal 알람 설정 초기화 (다시 abnormal 상태가 될 때 충돌 방지)
      
    }
    
    //////////////// JSON 밥 먹는 알람 시간 ///////////////////
    String foodTimeSet = (const char*) state["foodTime1"];
    if (foodTime[0] != foodTimeSet.toInt()) {
      foodTime[0] = foodTimeSet.toInt();
      if (foodTime[0] < 0) {
        foodHour[0] = -1;
        foodHour[0] = -1;
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
      
    }

    foodTimeSet = (const char*) state["foodTime2"];
    if (foodTime[1] != foodTimeSet.toInt()) {
      foodTime[1] = foodTimeSet.toInt();
      if (foodTime[1] < 0) {
        foodHour[1] = -1;
        foodHour[1] = -1;
      } else {
        foodHour[1] = foodTime[1]/60;
        foodMin[1] = foodTime[1]%60;
      }
      if(!((foodHour[1] == timeinfo.tm_hour) && (foodMin[1] <= timeinfo.tm_min))) {
        foodAlarmDay[1] = 0;
      }   
      
    }

    foodTimeSet = (const char*) state["foodTime3"];
    if (foodTime[2] != foodTimeSet.toInt()) {
      foodTime[2] = foodTimeSet.toInt();
      if (foodTime[2] < 0) {
        foodHour[2] = -1;
        foodHour[2] = -1;
      } else {
        foodHour[2] = foodTime[2]/60;
        foodMin[2] = foodTime[2]%60;
      }
      if(!((foodHour[2] == timeinfo.tm_hour) && (foodMin[2] <= timeinfo.tm_min))) {
        foodAlarmDay[2] = 0;
      }
      
    }
    
    foodTimeSet = (const char*) state["foodTime4"];
    if (foodTime[3] != foodTimeSet.toInt()) {
      foodTime[3] = foodTimeSet.toInt();
      if (foodTime[3] < 0) {
        foodHour[3] = -1;
        foodHour[3] = -1;
      } else {
        foodHour[3] = foodTime[3]/60;
        foodMin[3] = foodTime[3]%60;
      }
      if(!((foodHour[3] == timeinfo.tm_hour) && (foodMin[3] <= timeinfo.tm_min))) {
        foodAlarmDay[3] = 0;
      }
      
    }
    
    foodTimeSet = (const char*) state["foodTime5"];
    if (foodTime[4] != foodTimeSet.toInt()) {
      foodTime[4] = foodTimeSet.toInt();
      if (foodTime[4] < 0) {
        foodHour[4] = -1;
        foodHour[4] = -1;
      } else {
        foodHour[4] = foodTime[4]/60;
        foodMin[4] = foodTime[4]%60;
      }
      if(!((foodHour[4] == timeinfo.tm_hour) && (foodMin[4] <= timeinfo.tm_min))) {
        foodAlarmDay[4] = 0;
      }
      
    }

    if ((foodTime[0] != -1) || (foodTime[1] != -1) || (foodTime[2] != -1) || (foodTime[3] != -1) || (foodTime[4] != -1)) {
      foodAlarmMode = true;
    } else {
      foodAlarmMode = false;
    }

    // AWS-IoT에서 메세지 subscribe 끝나면
    // 그 메세지에 따라 바뀐 상태 다시 JSON 으로 publish 해줘서 shadow 바꾸기.
    // publish 구현 (state만 보내면 됨)
    
    
  }  // if(msgReceived == 1) 끝 (delta message subscribe하는 경우 끝)


  ////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////// AWS JSON Subscribe 부분 끝 ////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////// loop 내 esp32 동작 부분 시작 ////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////// 바이탈사인 검사 ///////////////////////////////
  // 센싱을 3~5분 할 예정이므로 min이 같아서 반복 센싱할 경우 x
  // 만약 반복 센싱된다면 중간에 에러로 센싱이 끊긴 것. 다시 센싱하게 됨.
  // 센싱 시작이 제대로 안되면 day state 변수를 둬서 min을 <= 로 조건을 두고 하면 될 듯
  // 아니면 day state 변수 두고 min을 비교안하고 hour만 비교해도 될듯
  if ((sensingHour_1 == timeinfo.tm_hour) || (sensingHour_2 == timeinfo.tm_hour)) {
    // 오전 or 오후 구분
    if (sensingHour_1 == timeinfo.tm_hour) {
      sensingPublishStatus_1 = true;
      
    } else if (sensingHour_2 == timeinfo.tm_hour) {
      sensingPublishStatus_2 = true;
    }

    pulseSensingMode = true;
    tempSensingMode = true;

    while(true){
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

        if((millis()-preMil) > /*60000*/ 180000) { // 3분동안 센싱 후 평균 계산
          preMil = millis();
          bpm = bpmCnt;
          Serial.print("심박 수 센싱 결과 BPM : ");
          Serial.println(bpm);  // Serial monitor 에 bpm 출력하고 싶으면 주석 제거.
          bpmCnt = 0;             // count 초기화
          pulseSensingMode = false;
        
        }
        
        
      }  // 심박 수 측정 부분 끝

      if ((tempSensingMode)) {  // 체온 측정
        // 구현하기 이거는 재는 시간은 이따 실험해보고 결정
        

        
      }  // 체온 측정 부분 끝

      delay(20);  // 센싱에 delay를 줘서 값 안정화에 기여
      
    }  // 센싱 완료

    // 센싱 완료 후 데이터 publish (평균 심박수: bpm , 평균 체온: temp , 측정 시간: sensingHour_1 (or _2) * 60)
    if ((sensingPublishStatus_1) || (sensingPublishStatus_2)) {
      if ((sensingPublishStatus_1)) {
        // 서버에 보낼 측정 시간 세팅 ( 측정 시간 변수 따로 두기): (sensingHour_1 * 60)
        // JSON으로 PUBLISH
        
      } else if ((sensingPublishStatus_2)) {
        // 서버에 보낼 측정 시간 세팅 ( 측정 시간 변수 따로 두기): (sensingHour_2 * 60)
      }

      // (평균 심박수 변수 + 평균 체온 변수 + 측정 시간 변수) ==> AWS로 publish


      // publish 끝나고 다시 sensingPublishStatus = false 로 초기화
      sensingPublishStatus_1 = false;
      sensingPublishStatus_2 = false;
    
    }
    
    
  }  // 바이탈사인 센싱 + publish 끝

  
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


}  // loop() 끝
