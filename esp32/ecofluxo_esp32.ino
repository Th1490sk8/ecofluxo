/*
  ==========================================================
  PROJETO ECOFLUXO - Monitoramento de Gases de Chaminé
  ESP32-S3 + DHT11 + MQ-135 + LEDs indicadores
  ==========================================================
  Envia leituras via HTTP POST (JSON) para um servidor
  Node.js/Express, que grava no MongoDB Atlas.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ---------------- CONFIGURAÇÃO WIFI ----------------
const char* WIFI_SSID     = "NOME_DA_SUA_REDE";
const char* WIFI_PASSWORD = "SENHA_DA_SUA_REDE";

// ---------------- CONFIGURAÇÃO DO SERVIDOR ----------------
// Se o servidor rodar no seu PC na mesma rede: http://IP_DO_PC:3000/api/dados
// Se estiver hospedado (Render/Railway): https://seu-app.onrender.com/api/dados
const char* SERVER_URL = "http://192.168.1.100:3000/api/dados";

// ---------------- PINOS ----------------
#define DHTPIN      4     // Sensor DHT11
#define DHTTYPE     DHT11
#define MQ135_PIN   5     // Sensor de gás (analógico)

#define LED_VERDE     15
#define LED_AMARELO   16
#define LED_VERMELHO  17

// ---------------- LIMITES (ajustar após calibração) ----------------
// MQ-135: leitura bruta do ADC (0-4095 no ESP32-S3, resolução 12 bits)
#define GAS_LIMITE_ATENCAO   1500   // acima disso = amarelo
#define GAS_LIMITE_CRITICO   2800   // acima disso = vermelho

#define TEMP_LIMITE_ATENCAO  40.0   // °C
#define TEMP_LIMITE_CRITICO  55.0   // °C

// Intervalo entre envios (ms)
const unsigned long INTERVALO_ENVIO = 10000; // 10 segundos

DHT dht(DHTPIN, DHTTYPE);
unsigned long ultimoEnvio = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  dht.begin();
  analogReadResolution(12); // ESP32-S3: 0-4095

  conectarWiFi();
}

void loop() {
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = millis();
    lerEEnviar();
  }
}

void conectarWiFi() {
  Serial.print("Conectando ao WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado!");
    Serial.print("IP do ESP32-S3: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha ao conectar no WiFi. Reiniciando...");
    ESP.restart();
  }
}

void lerEEnviar() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  int gasLeitura = analogRead(MQ135_PIN);

  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("Erro ao ler o DHT11!");
    temperatura = -1;
    umidade = -1;
  }

  // Define o status geral com base nos limites
  String status = definirStatus(temperatura, gasLeitura);
  atualizarLEDs(status);

  Serial.println("---------------------------------");
  Serial.printf("Temperatura: %.1f C\n", temperatura);
  Serial.printf("Umidade: %.1f %%\n", umidade);
  Serial.printf("Gas (MQ-135): %d\n", gasLeitura);
  Serial.printf("Status: %s\n", status.c_str());

  enviarParaServidor(temperatura, umidade, gasLeitura, status);
}

String definirStatus(float temperatura, int gas) {
  if (temperatura >= TEMP_LIMITE_CRITICO || gas >= GAS_LIMITE_CRITICO) {
    return "critico";
  } else if (temperatura >= TEMP_LIMITE_ATENCAO || gas >= GAS_LIMITE_ATENCAO) {
    return "atencao";
  } else {
    return "normal";
  }
}

void atualizarLEDs(String status) {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARELO, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  if (status == "normal") {
    digitalWrite(LED_VERDE, HIGH);
  } else if (status == "atencao") {
    digitalWrite(LED_AMARELO, HIGH);
  } else if (status == "critico") {
    digitalWrite(LED_VERMELHO, HIGH);
  }
}

void enviarParaServidor(float temperatura, float umidade, int gas, String status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Tentando reconectar...");
    conectarWiFi();
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperatura"] = temperatura;
  doc["umidade"] = umidade;
  doc["gas"] = gas;
  doc["status"] = status;

  String corpoJson;
  serializeJson(doc, corpoJson);

  int codigoResposta = http.POST(corpoJson);

  if (codigoResposta > 0) {
    Serial.printf("Dados enviados! Código HTTP: %d\n", codigoResposta);
  } else {
    Serial.printf("Erro ao enviar dados: %s\n", http.errorToString(codigoResposta).c_str());
  }

  http.end();
}
