#include <M5Atom.h>
#include "Ambient.h"
//#include <Ticker.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>      
#include <ArduinoJson.h>

#define INPUT_PIN 32
#define PUMP_PIN 26

WiFiClient client;
Ambient ambient;
//Ticker tic_Ambient;

const char* ssid     = "ssid"; // Wi-Fi SSID
const char* password = "password"; // Wi-Fi Password

unsigned int channelId = 12345; // AmbientのチャネルID
const char* writeKey = "xxxxxxxxxxxxxxxx"; // Ambientのライトキー

int rawADC;
float soilVOL;
bool isWorking = false, wasWaterd = false;

// Time
char ntpServer[] = "ntp.jst.mfeed.ad.jp";
const long gmtOffset_sec = 9 * 3600;
const int  daylightOffset_sec = 0;
struct tm timeinfo;
time_t t_Water = 0, t_IoT = 0; // 水やり開始時刻, 天気情報取得&Ambient通信時刻

// OpenWeather
const String sitehost = "api.openweathermap.org";
const String Api_KEY  = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"; // OpenWeatherのAPIキー
const String lang = "ja";
char geo_lat[10] = "35.6828"; // 緯度（東京）
char geo_lon[10] = "139.759"; // 経度（東京）
float Temp=20, Pres=1010, Humi=50;

const size_t mem_cap = 1024; // 1024 or 2048
StaticJsonDocument<mem_cap> n_jsondata;

// OpenWeatherのルート証明書
const char* ow_rootca = \
"-----BEGIN CERTIFICATE-----\n" \
"Please correct it to the value that suits your environment." \
"-----END CERTIFICATE-----\n";

void sendAmbient()
{
  ambient.set(1, rawADC);
  ambient.set(2, soilVOL);
  ambient.set(3, isWorking);
  ambient.set(4, Temp);
  ambient.set(5, Pres);
  ambient.set(6, Humi);
  
  ambient.send();

  Serial.println("Sent data to Ambient.");
}

bool Https_GetRes(String url_str, String *payload)
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  
  WiFiClientSecure *client_s = new WiFiClientSecure;
  
  if(client_s)
  {
    client_s -> setCACert(ow_rootca);
    HTTPClient https;
    Serial.print("[HTTPS] begin...\n");
    
    if (https.begin(*client_s, url_str))
    {
      Serial.print("[HTTPS] GET...\n");
      int httpCode = https.GET(); // ここで落ちる
      
      if (httpCode > 0)
      {
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
  
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
          *payload = https.getString();
          Serial.println(*payload);
          return true;
        }
      }
      else
      {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        return false;
      }
      https.end();
    }
    else
    {
      Serial.printf("[HTTPS] Unable to connect\n");
      return false;
    }
    delete client_s;
  }
  else
  {
    Serial.println("Unable to create client");
    return false;
  }
}

void getWeatherInfo()
{
  String url = "https://" + String(sitehost);
  url += "/data/2.5/weather?";
  url += "lat=" + String(geo_lat) + "&lon=" + String(geo_lon);
  url += "&units=metric&lang=" + lang;
  url += "&appid=" + Api_KEY;
  //Serial.println(url);
  
  String payload;
  
  if ( Https_GetRes(url, &payload) )
  {
    DeserializationError error = deserializeJson(n_jsondata, payload);
    
    if ( error )
    {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.f_str());
    }
    else
    {
      const int weather_id = n_jsondata["weather"][0]["id"];              // 天気ID
      const String weather_str = n_jsondata["weather"][0]["description"]; // 天気概況
      const double temp_v = n_jsondata["main"]["temp"];                   // 気温
      const double temp_p = n_jsondata["main"]["pressure"];               // 気圧
      const double temp_h = n_jsondata["main"]["humidity"];               // 湿度

      Temp = (float)temp_v;
      Pres = (float)temp_p;
      Humi = (float)temp_h;

      Serial.print("Temp.: "); Serial.print(Temp); Serial.print("C, ");
      Serial.print("Pres.: "); Serial.print(Pres); Serial.print("hPa, ");
      Serial.print("Humi.: "); Serial.print(Humi); Serial.println("%.");
    }
  }
  else
  {
    Serial.println("Connection failed (OpenWeather)");
  }
}

void setup()
{
  M5.begin();
  
  pinMode(INPUT_PIN,INPUT);
  pinMode(PUMP_PIN,OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  
  Serial.println("");
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while ( WiFi.status() != WL_CONNECTED )
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.print("WiFi connected\r\nIP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("");

  // NTP sync.
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  while ( !getLocalTime(&timeinfo) ) {
    delay(1000);
  }
  //time(&t);
  
  ambient.begin(channelId, writeKey, &client); // チャネルIDとライトキーを指定してAmbientの初期化
  
  // 天気情報取得&Ambient通信時刻の初期化
  //tic_Ambient.attach(300, sendAmbient); // 300 秒ごとに sendAmbient() を呼び出し
  t_Water = mktime(&timeinfo);
  t_IoT   = mktime(&timeinfo); // getWeatherInfo()をTickerで呼ぶと落ちる
}

void loop()
{ 
  // 現在時刻の取得
  getLocalTime(&timeinfo);
  Serial.print(timeinfo.tm_hour); Serial.print(":");
  Serial.print(timeinfo.tm_min);  Serial.print(":");
  Serial.print(timeinfo.tm_sec);  Serial.print(", ");
  
  // 土壌水分の測定
  rawADC = analogRead(INPUT_PIN);
  Serial.print("ADC:");
  Serial.print(rawADC);
  Serial.print(", ");
  
  soilVOL = rawADC * 3.3 / 4095.0;
  Serial.print(soilVOL);
  Serial.print("[V], ");
  
  M5.update();
  
  // ボタンが押されたらポンプをOn/Off
  if ( M5.Btn.wasPressed() )
  {
    isWorking = !isWorking;
    digitalWrite(PUMP_PIN, isWorking);

    if ( isWorking )
    {
      wasWaterd = true;
      t_Water = mktime(&timeinfo); // 水やり開始時刻 (UNIX時間)
      sendAmbient();
    }
  }
  
  // AM0600~0900にrawADC>1,600でポンプ未稼働で水やり未実施ならポンプをOn
  if ( timeinfo.tm_hour > 6 && timeinfo.tm_hour < 9 && rawADC > 1600 && isWorking == false && wasWaterd == false )
  {
    isWorking = true;
    wasWaterd = true;
    digitalWrite(PUMP_PIN, HIGH);
    t_Water = mktime(&timeinfo); // 水やり開始時刻 (UNIX時間)
    sendAmbient();
  }
  
  // ポンプ稼働時間が60秒を越えてポンプ稼働状態ならポンプをOff
  if ( mktime(&timeinfo) - t_Water > 60 && isWorking == true )
  {
    isWorking = false;
    digitalWrite(PUMP_PIN, LOW);
    sendAmbient();
  }
  
  // 1時間経ったら水やり実施済み判定をリセット
  if ( mktime(&timeinfo) - t_Water > 3600 && wasWaterd == true )
  {
    wasWaterd = false;
  }
  
  Serial.print("isWorking:"); Serial.print(isWorking); Serial.print(", ");
  Serial.print("wasWaterd:"); Serial.print(wasWaterd); Serial.print(", ");
  Serial.print("eps:"); Serial.print(mktime(&timeinfo)-t_Water); Serial.println("[s]");
  
  // 天気情報取得&Ambient通信 (300秒毎)
  if ( mktime(&timeinfo) - t_IoT > 300 )
  {
    t_IoT = mktime(&timeinfo);
    getWeatherInfo();
    sendAmbient();
  }
  
  delay(100);
}
