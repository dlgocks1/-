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
#define MP3_FILENAME_4 "/iotLostModeSound.mp3"
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
// AWS_IoT Shadow
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
const int lostModeAlarmMP3 = 4;


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// NTP 시간 동기화  /////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600*9;
const int daylightOffset_sec = 0;


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 상태 변수 ////////////////////////////////////////////
///////// 강아지 건강 상태, 센싱 모드, publish 모드, 강아지 분실 모드 ////////////////
/////////////////////////////////////////////////////////////////////////////////
// 강아지 바이탈사인
String healthStatus = "normal";  // 기본값: normal , 이상값 검출 시: abnormal

// 센싱데이터 AWS로 publish 해야하는 상태
// 이미 했거나 안해도 되면 false , 측정완료해서 보내야하면 true
// 오전 or 오후 구분 (시간도 서버에 보낼 것)
boolean sensingPublishStatus_1 = false;
boolean sensingPublishStatus_2 = false;

// 강아지 분실 모드
boolean lostStatus = false;  // 기본값: false , 분실 시: true


/////////////////////////////////////////////////////////////////////////////////
/////////////////////////// 알람 시간 선언 /////////// ////////////////////////////////
/////// 바이탈사인 검사 시간, 건강 이상 시 소리 알람 시간, 밥 시간 (사용자 설정) ////////
/////////////////////////////////////////////////////////////////////////////////
// 바이탈 사인 검사 시간
// 오전 7시
int sensingHour_1 = 7;
int sensingMin_1 = 0;

// 오후 10시
int sensingHour_2 = 22;
int sensingMin_2 = 0;

// 건강 이상 감지 시 알람 주기
// abnormalAlarmStatus는 매일 초기화 (abnormalAlarmDay = 0: 기본값)
int abnormalAlarmDay = 0;
// 오전 10시
int abnormalHour_1 = 10;
boolean abnormalAlarmStatus_1 = false;
// 오후 2시
int abnormalHour_2 = 14;
boolean abnormalAlarmStatus_2 = false;
// 오후 5시
int abnormalHour_3 = 17;
boolean abnormalAlarmStatus_3 = false;
// 오후 8시
int abnormalHour_4 = 20;
boolean abnormalAlarmStatus_4 = false;

// 밥 시간
// 사용자 설정 (App -> AWS subscribe)
// 0: 알람 미설정 (시간 * 60 + 분 ==> 시간 = foodTime / 60 , 분 = foodTime % 60)
// foodAlarmStatus는 매일 초기화 (foodAlarmDay = 0: 기본값)
int foodAlarmDay = 0;
int foodTime_1 = 0;
boolean foodAlarmStatus_1 = false;
int foodTime_2 = 0;
boolean foodAlarmStatus_2 = false;
int foodTime_3 = 0;
boolean foodAlarmStatus_3 = false;
int foodTime_4 = 0;
boolean foodAlarmStatus_4 = false;
int foodTime_5 = 0;
boolean foodAlarmStatus_5 = false;


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
// parameter (abnormalAlarmMP3, normalAlarmMP3, foodTimeAlarmMP3, lostModeAlarmMP3)
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
  } else if (mp3Idx == 4) {
    Serial.println("lost mode alarm play");
    file = new AudioFileSourceSPIFFS(MP3_FILENAME_4);
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
      Serial.printf("MP3 done\n");
      mpState = false;
      delete file;
      delete id3;
      delete out;
      delete mp3;
      mpLoop = false;
    }
  }
  
  mpLoop = true;
  Serial.print("mechanism test OK: ");
  delay(2000);
  Serial.print("delay 2000");

  
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
///// AWS subscribe (강아지 건강 상태, 밥 시간 알람 설정, 분실 상태) ///////
///////////////////////////////////////////////////////////////////////
void loop() {
  ///////////////////////////// 분실 모드 음성 출력 ///////////////////////////////
  // 분실 모드가 되면 deepsleep X 계속 주기적으로 알람 발생 (delay를 준다 millis()로)
  // 모듈 케이스에 전화번호 써넣게 하고 음성으로 모듈 케이스를 확인해서 전화해달라고 음성 출력
  // 분실 모드에서는 Deepsleep X
  if ((lostStatus)) {
    alarmPlay(lostModeAlarmMP3);
    // 계속 알람 발생 (millis() 이용)
    // 이 강아지를 발견하신 분께서는 강아지 등에 있는 기기에 써진 번호로 연락해주세요
    // 어플로 알람을 끌 수 있음 (분실 모드를 끄면 알람도 꺼짐)
    // 구현해야함
    
    
  }

  
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

    // AWS-IoT Core에서 바이탈사인 이상값 검출 시 subscribe (AWS -> ESP32)
    // 또한 이상값 검출되고 난 후에 다시 정상값 검출되면 다시 subscribe (AWS -> ESP32)
    String healthStatus = (const char*) state["healthStatus"];
    Serial.print("Dog's Heart Beat is ");
    Serial.print(healthStatus);

    // 바이탈 사인이 이상할 경우 & 정상일 경우 각각 음성 메세지 출력
    if(healthStatus == "abnormal") {
      alarmPlay(abnormalAlarmMP3);
    } else if (healthStatus == "normal") {
      alarmPlay(normalAlarmMP3);
      int abnormalAlarmDay = 0;  // abnormal 알람 설정 초기화 (다시 abnormal 상태가 될 때 충돌 방지)
    }
    
    // 일단 시간으로만 구현해서 분까지 추가 구현해야 할듯.
    // 밥 시간 알람 설정
    String foodTime = (const char*) state["foodTime1"];
    int foodTime_1 = foodTime.toInt();
    foodTime = (const char*) state["foodTime2"];
    int foodTime_2 = foodTime.toInt();
    foodTime = (const char*) state["foodTime3"];
    int foodTime_3 = foodTime.toInt();
    foodTime = (const char*) state["foodTime4"];
    int foodTime_4 = foodTime.toInt();
    foodTime = (const char*) state["foodTime5"];
    int foodTime_5 = foodTime.toInt();

    // 분실 모드 설정 (false: 기본값 , true: 강아지 분실 상태)
    boolean lostStatus = (const char*) state["lostStatus"];
    if((lostStatus)) {
      alarmPlay(lostModeAlarmMP3);
    }

    // AWS-IoT에서 메세지 subscribe 끝나면
    // 그 메세지에 따라 바뀐 상태 다시 JSON 으로 publish 해줘서 shadow 바꾸기.
    // publish 구현 (state만 보내면 됨)
    
    
  }  // if(msgReceived == 1) 끝 (delta message subscribe하는 경우 끝)


  ////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////// loop 내 esp32 동작 부분 ///////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////// 바이탈사인 검사 ///////////////////////////////
  // 센싱을 3~5분 할 예정이므로 min이 같아서 반복 센싱할 경우 x
  // 만약 반복 센싱된다면 중간에 에러로 센싱이 끊긴 것. 다시 센싱하게 됨.
  // 센싱 시작이 제대로 안되면 day state 변수를 둬서 min을 <= 로 조건을 두고 하면 될 듯
  // 아니면 day state 변수 두고 min을 비교안하고 hour만 비교해도 될듯
  if (((sensingHour_1 == timeinfo.tm_hour) && (sensingMin_1 == timeinfo.tm_hour)) || ((sensingHour_2 == timeinfo.tm_hour) && (sensingMin_2 == timeinfo.tm_hour))) {
    // 오전 or 오후 구분
    if (sensingHour_1 == timeinfo.tm_hour) {
      sensingPublishStatus_1 = true;
      
    } else if (sensingHour_2 == timeinfo.tm_hour) {
      sensingPublishStatus_2 = true;
      
    }

    // 센싱 코드 (심박수, 체온 timer를 둬서 3~5분간 측정 후 평균 심박수 & 평균 체온 계산해서 변수에 저장)
    // 구현해야함.
    
  }

  // 센싱 완료 후 publish
  if ((sensingPublishStatus_1) || (sensingPublishStatus_2)) {
    if ((sensingPublishStatus_1)) {
      // 서버에 보낼 측정 시간 세팅 ( 측정 시간 변수 따로 두기): (sensingHour_1 + sensingMin_1)
      // JSON으로 PUBLISH
      
    } else if ((sensingPublishStatus_2)) {
      // 서버에 보낼 측정 시간 세팅 ( 측정 시간 변수 따로 두기): (sensingHour_2 + sensingMin_2)
    }

    // (평균 심박수 변수 + 평균 체온 변수 + 측정 시간 변수) ==> AWS로 publish


    // publish 끝나고 다시 sensingPublishStatus = false 로 초기화
    sensingPublishStatus_1 = false;
    sensingPublishStatus_2 = false;
    
  }
 

  ///////////////////////////// 건강 이상 주기적 알람 ///////////////////////////////
  // 바이탈 사인 이상 시 주기적 알람 (아침 저녁에 이상 상태를 aws에서 받을 때 + 점심쯤 2번
  if (healthStatus =="abnormal") {
    // 매일 abnormalAlarmStatus 초기화
    if (abnormalAlarmDay != timeinfo.tm_mday) {
      int abnormalAlarmDay = timeinfo.tm_mday;
      boolean abnormalAlarmStatus_1 = true;
      boolean abnormalAlarmStatus_2 = true;
      boolean abnormalAlarmStatus_3 = true;
      boolean abnormalAlarmStatus_4 = true;
    }

    if (((abnormalAlarmStatus_1) && abnormalHour_1 == timeinfo.tm_hour) || ((abnormalAlarmStatus_2) && abnormalHour_2 == timeinfo.tm_hour) || ((abnormalAlarmStatus_3) && abnormalHour_3 == timeinfo.tm_hour) || ((abnormalAlarmStatus_4) && abnormalHour_4 == timeinfo.tm_hour)) {
      // 소리 알람 발생
      alarmPlay(abnormalAlarmMP3);  

      if ((abnormalAlarmStatus_1) && abnormalHour_1 == timeinfo.tm_hour) {
        boolean abnormalAlarmStatus_1 = false;
      } else if ((abnormalAlarmStatus_2) && abnormalHour_2 == timeinfo.tm_hour) {
        boolean abnormalAlarmStatus_2 = false;
      } else if ((abnormalAlarmStatus_3) && abnormalHour_3 == timeinfo.tm_hour) {
        boolean abnormalAlarmStatus_1 = false;
      } else if ((abnormalAlarmStatus_4) && abnormalHour_4 == timeinfo.tm_hour) {
        boolean abnormalAlarmStatus_1 = false;
      }
        
    }  // abnormal 알람 시간되면 소리 알람 발생 끝
            
  }  // abnormal 주기적 알람 끝
  
  
  ///////////////////////////// 밥 시간 알람 ///////////////////////////////
  if ((foodTime_1 != 0) || (foodTime_2 != 0) || (foodTime_3 != 0) || (foodTime_4 != 0) || (foodTime_5 != 0)) {
    if (foodAlarmDay != timeinfo.tm_mday) {
      int foodAlarmDay = timeinfo.tm_mday;
      if (foodTime_1 != 0) {
        boolean foodAlarmStatus_1 = true;
      }
      if (foodTime_2 != 0) {
        boolean foodAlarmStatus_2 = true;
      }
      if (foodTime_3 != 0) {
        boolean foodAlarmStatus_3 = true;
      }
      if (foodTime_4 != 0) {
        boolean foodAlarmStatus_4 = true;
      }
      if (foodTime_5 != 0) {
        boolean foodAlarmStatus_5 = true;
      }
    }

    if (((foodAlarmStatus_1) && foodTime_1 == timeinfo.tm_hour) || ((foodAlarmStatus_2) && foodTime_2 == timeinfo.tm_hour) || ((foodAlarmStatus_3) && foodTime_3 == timeinfo.tm_hour) || ((foodAlarmStatus_4) && foodTime_4 == timeinfo.tm_hour) || ((foodAlarmStatus_5) && foodTime_5 == timeinfo.tm_hour)) {
      // 소리 알람 발생
      alarmPlay(foodTimeAlarmMP3);

      if ((foodAlarmStatus_1) && foodTime_1 == timeinfo.tm_hour) {
        boolean foodAlarmStatus_1 = false;
      } else if ((foodAlarmStatus_2) && foodTime_2 == timeinfo.tm_hour) {
        boolean foodAlarmStatus_2 = false;
      } else if ((foodAlarmStatus_3) && foodTime_3 == timeinfo.tm_hour) {
        boolean foodAlarmStatus_1 = false;
      } else if ((foodAlarmStatus_4) && foodTime_4 == timeinfo.tm_hour) {
        boolean foodAlarmStatus_1 = false;
      } else if ((foodAlarmStatus_4) && foodTime_4 == timeinfo.tm_hour) {
        boolean foodAlarmStatus_2 = false;
      }
      
    } // foodTime 되면 밥달라고 소리 알람 발생 끝
    
  }  // 밥 시간 알람 끝


}  // loop() 끝
