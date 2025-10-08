/*
  ESP32 OBD2 Web Dashboard
  Основано на ESP-Watch-OBD2, но с веб-интерфейсом вместо экрана
  Использует ESP32, ELM327 Bluetooth и AsyncWebServer для отображения
  данных OBD2 на веб-странице
*/

#include "BluetoothSerial.h"
#include "ELMduino.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// WiFi настройки
const char* ssid = "elm327";
const char* password = "123456789";

// Bluetooth и OBD2 настройки
BluetoothSerial SerialBT;
#define ELM_PORT SerialBT
#define DEBUG_PORT Serial
ELM327 myELM327;

// Веб-сервер
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Переменные для данных OBD2
uint32_t rpm = 0;
uint32_t kph = 0;
uint32_t ECT = 0;
float fuelLevel = 0.0;
float engineLoad = 0.0;
float batteryVoltage = 0.0;
bool dataUpdated = false;

// Функция перезагрузки
void(* resetFunc) (void) = 0;

void setup() {
  Serial.begin(115200);
  
  // Подключение к WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Подключение к WiFi...");
  }
  Serial.println("WiFi подключен!");
  Serial.print("IP адрес: ");
  Serial.println(WiFi.localIP());
  
  // Инициализация Bluetooth и ELM327
  initializeOBD2();
  
  // Настройка веб-сервера
  setupWebServer();
  
  // Запуск веб-сервера
  server.begin();
  Serial.println("Веб-сервер запущен");
}

void initializeOBD2() {
  Serial.println("Подключение к OBD2...");
  
  ELM_PORT.begin("ESP32OBD2", true);
  
  if (!ELM_PORT.connect("OBDII")) {
    Serial.println("Ошибка подключения к OBD сканеру - Фаза 1");
    delay(5000);
    resetFunc();
  }
  
  if (!myELM327.begin(ELM_PORT)) {
    Serial.println("Ошибка подключения к OBD сканеру - Фаза 2");
    delay(5000);
    resetFunc();
  }
  
  Serial.println("Подключен к ELM327");
}

void setupWebServer() {
  // Обработка WebSocket соединений
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  // Главная страница
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  
  // API для получения данных
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = createJsonData();
    request->send(200, "application/json", json);
  });
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch(type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket клиент подключен: %u\n", client->id());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket клиент отключен: %u\n", client->id());
      break;
    case WS_EVT_ERROR:
      Serial.printf("WebSocket ошибка(%u): %s\n", client->id(), (char*)data);
      break;
    case WS_EVT_PONG:
    case WS_EVT_DATA:
      break;
  }
}

String createJsonData() {
  StaticJsonDocument<200> doc;
  doc["rpm"] = rpm;
  doc["speed"] = kph;
  doc["coolantTemp"] = ECT > 0 ? ECT - 40 : 0;
  doc["fuelLevel"] = fuelLevel;
  doc["engineLoad"] = engineLoad;
  doc["batteryVoltage"] = batteryVoltage;
  doc["timestamp"] = millis();
  
  String json;
  serializeJson(doc, json);
  return json;
}

void loop() {
  // Чтение данных OBD2
  readOBD2Data();
  
  // Отправка данных через WebSocket каждые 500мс
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 500 && dataUpdated) {
    String json = createJsonData();
    ws.textAll(json);
    dataUpdated = false;
    lastUpdate = millis();
  }
  
  delay(100);
}

void readOBD2Data() {
  // Чтение RPM
  float tempRPM = myELM327.rpm();
  if (myELM327.status == ELM_SUCCESS) {
    rpm = (uint32_t)tempRPM;
    dataUpdated = true;
  }
  
  // Чтение скорости
  float tempSpeed = myELM327.kph();
  if (myELM327.status == ELM_SUCCESS) {
    kph = (uint32_t)tempSpeed;
    dataUpdated = true;
  }
  
  // Чтение температуры охлаждающей жидкости
  if (myELM327.queryPID(SERVICE_01, ENGINE_COOLANT_TEMP)) {
    int32_t tempECT = myELM327.findResponse();
    if (myELM327.status == ELM_SUCCESS) {
      ECT = (uint32_t)tempECT;
      dataUpdated = true;
    }
  }
  
  // Чтение уровня топлива
  if (myELM327.queryPID(SERVICE_01, FUEL_TANK_LEVEL_INPUT)) {
    fuelLevel = myELM327.findResponse() * 100.0 / 255.0;
    if (myELM327.status == ELM_SUCCESS) {
      dataUpdated = true;
    }
  }
  
  // Чтение нагрузки двигателя
  float tempLoad = myELM327.engineLoad();
  if (myELM327.status == ELM_SUCCESS) {
    engineLoad = tempLoad;
    dataUpdated = true;
  }
  
  // Чтение напряжения батареи
  float tempVoltage = myELM327.batteryVoltage();
  if (myELM327.status == ELM_SUCCESS) {
    batteryVoltage = tempVoltage;
    dataUpdated = true;
  }
}

// HTML страница с интерфейсом
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 OBD2 Dashboard</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #1a1a1a;
            color: #ffffff;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
        }
        .gauge {
            background-color: #2a2a2a;
            border-radius: 10px;
            padding: 20px;
            text-align: center;
            border: 2px solid #444;
        }
        .gauge h3 {
            margin-top: 0;
            color: #4CAF50;
        }
        .value {
            font-size: 2.5em;
            font-weight: bold;
            margin: 10px 0;
        }
        .unit {
            font-size: 1.2em;
            color: #888;
        }
        .status {
            margin-top: 20px;
            padding: 10px;
            background-color: #333;
            border-radius: 5px;
        }
        .connected {
            color: #4CAF50;
        }
        .disconnected {
            color: #f44336;
        }
        .rpm { color: #FF9800; }
        .speed { color: #2196F3; }
        .temp { color: #f44336; }
        .fuel { color: #4CAF50; }
        .load { color: #9C27B0; }
        .voltage { color: #FFEB3B; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🚗 ESP32 OBD2 Dashboard</h1>
            <div class="status" id="status">
                <span id="connectionStatus" class="disconnected">Подключение...</span>
            </div>
        </div>
        
        <div class="dashboard">
            <div class="gauge">
                <h3>Обороты двигателя</h3>
                <div class="value rpm" id="rpm">0</div>
                <div class="unit">об/мин</div>
            </div>
            
            <div class="gauge">
                <h3>Скорость</h3>
                <div class="value speed" id="speed">0</div>
                <div class="unit">км/ч</div>
            </div>
            
            <div class="gauge">
                <h3>Температура ОЖ</h3>
                <div class="value temp" id="coolantTemp">0</div>
                <div class="unit">°C</div>
            </div>
            
            <div class="gauge">
                <h3>Уровень топлива</h3>
                <div class="value fuel" id="fuelLevel">0</div>
                <div class="unit">%</div>
            </div>
            
            <div class="gauge">
                <h3>Нагрузка двигателя</h3>
                <div class="value load" id="engineLoad">0</div>
                <div class="unit">%</div>
            </div>
            
            <div class="gauge">
                <h3>Напряжение АКБ</h3>
                <div class="value voltage" id="batteryVoltage">0</div>
                <div class="unit">В</div>
            </div>
        </div>
    </div>

    <script>
        let websocket;
        let reconnectInterval;
        
        function initWebSocket() {
            websocket = new WebSocket('ws://' + window.location.hostname + '/ws');
            
            websocket.onopen = function(event) {
                console.log('WebSocket подключен');
                document.getElementById('connectionStatus').textContent = 'Подключен к OBD2';
                document.getElementById('connectionStatus').className = 'connected';
                clearInterval(reconnectInterval);
            };
            
            websocket.onclose = function(event) {
                console.log('WebSocket отключен');
                document.getElementById('connectionStatus').textContent = 'Соединение потеряно';
                document.getElementById('connectionStatus').className = 'disconnected';
                // Попытка переподключения каждые 3 секунды
                reconnectInterval = setInterval(initWebSocket, 3000);
            };
            
            websocket.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    updateDashboard(data);
                } catch (e) {
                    console.error('Ошибка парсинга JSON:', e);
                }
            };
            
            websocket.onerror = function(error) {
                console.error('WebSocket ошибка:', error);
            };
        }
        
        function updateDashboard(data) {
            document.getElementById('rpm').textContent = data.rpm || 0;
            document.getElementById('speed').textContent = data.speed || 0;
            document.getElementById('coolantTemp').textContent = data.coolantTemp || 0;
            document.getElementById('fuelLevel').textContent = Math.round(data.fuelLevel * 10) / 10 || 0;
            document.getElementById('engineLoad').textContent = Math.round(data.engineLoad * 10) / 10 || 0;
            document.getElementById('batteryVoltage').textContent = Math.round(data.batteryVoltage * 100) / 100 || 0;
        }
        
        // Инициализация при загрузке страницы
        window.addEventListener('load', initWebSocket);
        
        // Альтернативный способ получения данных через HTTP API
        function fetchDataHTTP() {
            fetch('/api/data')
                .then(response => response.json())
                .then(data => updateDashboard(data))
                .catch(error => console.error('Ошибка получения данных:', error));
        }
        
        // Резервный метод обновления каждые 2 секунды через HTTP
        setInterval(fetchDataHTTP, 2000);
    </script>
</body>
</html>
)rawliteral";

// Дополнительные функции для расширения функционала

// Функция для отправки команд AT ELM327
void sendATCommand(String command) {
  ELM_PORT.print(command + "\r");
  delay(100);
  while (ELM_PORT.available()) {
    Serial.write(ELM_PORT.read());
  }
}

// Функция для получения VIN номера автомобиля
String getVIN() {
  String vin = "";
  if (myELM327.queryPID(SERVICE_09, 0x02)) {
    // Обработка ответа для VIN
    // Код для парсинга VIN номера
  }
  return vin;
}

// Функция для получения кодов ошибок DTC
void getDTCCodes() {
  myELM327.currentDTCCodes(true);
  if (myELM327.DTC_Response.codesFound > 0) {
    Serial.println("Найдены коды ошибок:");
    for (int i = 0; i < myELM327.DTC_Response.codesFound; i++) {
      Serial.println(myELM327.DTC_Response.codes[i]);
    }
  }
}

// Функция для сброса кодов ошибок
void clearDTCCodes() {
  myELM327.resetDTC();
  Serial.println("Коды ошибок сброшены");
}

// Расширенная функция чтения дополнительных параметров
void readExtendedOBD2Data() {
  // Давление воздуха во впускном коллекторе
  float manifoldPressure = myELM327.manifoldPressure();
  if (myELM327.status == ELM_SUCCESS) {
    // Обработка данных
  }
  
  // Температура воздуха на впуске
  float intakeTemp = myELM327.intakeAirTemp();
  if (myELM327.status == ELM_SUCCESS) {
    // Обработка данных
  }
  
  // Массовый расход воздуха
  float mafRate = myELM327.mafRate();
  if (myELM327.status == ELM_SUCCESS) {
    // Обработка данных
  }
  
  // Положение дроссельной заслонки
  float throttlePos = myELM327.throttle();
  if (myELM327.status == ELM_SUCCESS) {
    // Обработка данных
  }
}

// Функция для настройки точки доступа WiFi
void setupAccessPoint() {
  WiFi.softAP("ESP32-OBD2-Dashboard", "12345678");
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());
}