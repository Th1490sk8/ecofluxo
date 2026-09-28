/*
  ==========================================================
  PROJETO ECOFLUXO - Monitoramento de Gases de Chaminé
  ESP32 (Dev Module) + DHT11 + MQ-135 + LEDs + Exaustor 12V
  ==========================================================
  OBS: o projeto foi migrado do ESP32-S3 para um ESP32 "normal"
  (Dev Module / WROOM-32) depois que o pino 5V da placa S3 se
  mostrou danificado durante os testes (parava de alimentar
  módulos externos, embora o resto da placa funcionasse). No
  ESP32 normal, sensor e relé foram validados e funcionam.

  Envia leituras via HTTP POST (JSON) para um servidor
  Node.js/Express, que grava no MongoDB Atlas.

  O exaustor (ventoinha 12V, acionada por um módulo relé) liga
  automaticamente a partir do nível de ATENÇÃO (amarelo) e
  permanece ligado também no nível CRÍTICO (vermelho).

  ------------------------------------------------------------
  ACIONAMENTO DA VENTOINHA: módulo relé (lógica invertida)
  ------------------------------------------------------------
  Chegamos a testar um módulo L298N como alternativa (o relé
  tinha apresentado falhas no bloco de contatos de potência em
  testes anteriores), mas no teste físico final o relé voltou a
  funcionar normalmente acionando a ventoinha de 12V — só que com
  o sinal de controle INVERTIDO (o contato usado fecha com LOW e
  abre com HIGH, o oposto do "natural"). Por isso o projeto ficou
  com o relé, e a inversão foi resolvida em software (ver
  atualizarExaustor() mais abaixo), sem precisar mexer na fiação.

    VCC do relé -> 5V/VIN do ESP32
    GND do relé -> GND comum (junto com o preto da fonte ATX e o
                   GND do ESP32)
    IN1         -> GPIO26
    COM         -> fio amarelo (+12V) da fonte ATX ALL-550 TPW
    NA/NF       -> positivo da ventoinha (o contato validado no
                   teste físico)
    GND da fonte ATX e da ventoinha -> GND comum
  ------------------------------------------------------------

  ------------------------------------------------------------
  SENSOR DE GÁS: módulo "Flying Fish" com sensor MQ-135
  ------------------------------------------------------------
  Pinagem impressa na placa (da esquerda p/ direita): A0 D0 GND VCC

    VCC -> 3V3 do ESP32 (NÃO o pino 5V/VIN!)
    GND -> GND comum do ESP32
    D0  -> não utilizado neste projeto (saída digital com limiar
           fixo por um trimpot; não precisa calibrar, pois não
           está em uso)
    A0  -> ligado DIRETO no GPIO34, sem divisor de tensão

  Por que GPIO34: nesse ESP32 os pinos 34-39 são só de entrada,
  ideais para leitura analógica e sem conflito com o WiFi.

  Por que sem divisor: alimentando o VCC do sensor com 3,3V (em vez
  dos 5V "de manual"), a saída do A0 nunca ultrapassa 3,3V — que é
  o limite seguro do ADC do ESP32 — então dá pra ligar direto.
  A troca é que o aquecedor interno esquenta um pouco menos que
  com 5V, reduzindo a sensibilidade — mas como o projeto usa
  limiares calibrados na prática (e não ppm calibrado), isso não
  compromete o funcionamento.

  Os limites de GAS_LIMITE_ATENCAO/CRITICO abaixo são valores
  iniciais — recalibre observando o Serial Monitor com o sensor
  já aquecido (alguns minutos ligado) em ambiente normal.
  ------------------------------------------------------------

  OBS: o botão de emergência (GPIO4) foi retirado por enquanto pra
  isolar um problema de brownout (reset por queda de tensão) que
  apareceu depois de ligá-lo. Assim que o brownout for resolvido
  (movendo o VCC do relé para a fonte ATX em vez do 5V do ESP32),
  o botão pode voltar.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ---------------- CONFIGURAÇÃO WIFI ----------------
const char* WIFI_SSID     = "NOME_DA_SUA_REDE";
const char* WIFI_PASSWORD = "SENHA_DA_SUA_REDE";

// ---------------- CONFIGURAÇÃO DO SERVIDOR ----------------
// Backend hospedado no Render (HTTPS). Se algum dia for testar contra
// um servidor local na mesma rede, troque para http://IP_DO_PC:3000/api/dados
// (nesse caso o WiFiClientSecure/setInsecure() em enviarParaServidor()
// deixa de ser necessário, mas não atrapalha usar http.begin comum também).
const char* SERVER_URL = "https://ecofluxo.onrender.com/api/dados";

// ---------------- PINOS ----------------
#define DHTPIN      5     // Sensor DHT11
#define DHTTYPE     DHT11
#define MQ135_PIN   34    // Ligado direto no A0 do sensor (VCC do sensor em 3V3, ver nota acima)

#define LED_VERDE     25
#define LED_AMARELO   27
#define LED_VERMELHO  33

#define EXAUSTOR_PIN 26   // IN1 do módulo relé, que aciona a ventoinha 12V (lógica invertida)

// ---------------- LIMITES (ajustar após calibração) ----------------
// MQ-135: leitura bruta do ADC (0-4095, resolução 12 bits)
#define GAS_LIMITE_ATENCAO   1500   // acima disso = amarelo
#define GAS_LIMITE_CRITICO   2800   // acima disso = vermelho

#define TEMP_LIMITE_ATENCAO  40.0   // °C
#define TEMP_LIMITE_CRITICO  55.0   // °C

// Intervalo entre envios (ms)
const unsigned long INTERVALO_ENVIO = 10000; // 10 segundos

// ---------------- MÉDIA MÓVEL DO SENSOR DE GÁS ----------------
// Em vez de decidir o status com uma única leitura (que pode variar por
// ruído), guardamos as últimas N leituras e usamos a média. Isso evita
// que o status "pisque" entre níveis (ex: normal -> atencao -> normal)
// por causa de um pico isolado no ADC.
#define TAMANHO_MEDIA_MOVEL 8
int bufferGas[TAMANHO_MEDIA_MOVEL];
int indiceBufferGas = 0;
bool bufferGasCheio = false;

DHT dht(DHTPIN, DHTTYPE);
unsigned long ultimoEnvio = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  pinMode(EXAUSTOR_PIN, OUTPUT);
  digitalWrite(EXAUSTOR_PIN, HIGH); // exaustor começa desligado (lógica invertida, ver nota abaixo)

  dht.begin();
  analogReadResolution(12); // 0-4095

  // Pré-preenche o buffer da média móvel com a primeira leitura real,
  // pra não começar com zeros e distorcer a média nos primeiros ciclos.
  int leituraInicial = analogRead(MQ135_PIN);
  for (int i = 0; i < TAMANHO_MEDIA_MOVEL; i++) {
    bufferGas[i] = leituraInicial;
  }

  conectarWiFi();
}

void loop() {
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = millis();
    lerEEnviar();
  }
}

// Conecta ao WiFi. Retorna true se conseguiu, false se não (sem travar
// o ESP32 pra sempre nem reiniciar a placa).
bool conectarWiFi() {
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
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nNão foi possível conectar ao WiFi agora. Vou continuar");
    Serial.println("lendo os sensores e tentar reconectar sozinho no próximo ciclo.");
    return false;
  }
}

// Adiciona uma leitura ao buffer circular e retorna a média das últimas
// TAMANHO_MEDIA_MOVEL leituras (suaviza ruído do MQ-135).
int atualizarMediaMovelGas(int novaLeitura) {
  bufferGas[indiceBufferGas] = novaLeitura;
  indiceBufferGas = (indiceBufferGas + 1) % TAMANHO_MEDIA_MOVEL;
  if (indiceBufferGas == 0) bufferGasCheio = true;

  long soma = 0;
  int quantidade = bufferGasCheio ? TAMANHO_MEDIA_MOVEL : indiceBufferGas;
  for (int i = 0; i < quantidade; i++) {
    soma += bufferGas[i];
  }
  return soma / quantidade;
}

void lerEEnviar() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  int gasBruto = analogRead(MQ135_PIN);
  int gasLeitura = atualizarMediaMovelGas(gasBruto); // já suavizado

  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("Erro ao ler o DHT11!");
    temperatura = -1;
    umidade = -1;
  }

  // Define o status geral com base nos limites (usando a média móvel do gás)
  String status = definirStatus(temperatura, gasLeitura);
  atualizarLEDs(status);
  atualizarExaustor(status);

  Serial.println("---------------------------------");
  Serial.printf("Temperatura: %.1f C\n", temperatura);
  Serial.printf("Umidade: %.1f %%\n", umidade);
  Serial.printf("Gas bruto: %d   |   Gas (media movel): %d\n", gasBruto, gasLeitura);
  Serial.printf("Status: %s\n", status.c_str());
  Serial.printf("Exaustor: %s\n", (status == "atencao" || status == "critico") ? "LIGADO" : "desligado");

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

// Liga o exaustor a partir do nível de ATENÇÃO (amarelo) e mantém ligado no
// CRÍTICO (vermelho). Desliga apenas quando o status volta a NORMAL.
//
// OBS: lógica INVERTIDA no pino físico (LOW = ligado, HIGH = desligado).
// O acionamento usado (contato NF do relé) fecha o circuito quando o pino
// está em LOW e abre quando está em HIGH — o oposto do que seria "natural".
// O significado de "exaustor ligado" no JSON/dashboard continua normal
// (true = ligado); só a escrita no pino físico é que é ao contrário.
void atualizarExaustor(String status) {
  if (status == "atencao" || status == "critico") {
    digitalWrite(EXAUSTOR_PIN, LOW);  // LOW = ligado (lógica invertida)
  } else {
    digitalWrite(EXAUSTOR_PIN, HIGH); // HIGH = desligado (lógica invertida)
  }
}

void enviarParaServidor(float temperatura, float umidade, int gas, String status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Tentando reconectar (sem reiniciar a placa)...");
    bool reconectou = conectarWiFi();
    if (!reconectou) {
      Serial.println("Ainda sem WiFi. Essa leitura não foi enviada, mas os");
      Serial.println("sensores, LEDs e exaustor continuam funcionando normalmente.");
      return;
    }
  }

  WiFiClientSecure clienteSeguro;
  // O Render usa um certificado público válido, mas o ESP32 não carrega
  // uma lista de autoridades certificadoras por padrão. setInsecure()
  // pula a verificação da cadeia do certificado (aceitável aqui: o risco
  // real é baixo para um protótipo escolar enviando leituras de sensor,
  // e o volume de dados não é sensível).
  clienteSeguro.setInsecure();

  HTTPClient http;
  http.begin(clienteSeguro, SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperatura"] = temperatura;
  doc["umidade"] = umidade;
  doc["gas"] = gas;
  doc["status"] = status;
  doc["exaustor"] = (status == "atencao" || status == "critico");

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
