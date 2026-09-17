# EcoFluxo — Guia de Configuração

Sistema de monitoramento de gases de chaminé: ESP32-S3 + DHT11 + MQ-135 → Node.js/Express → MongoDB Atlas → Dashboard web.

## 1. Criar o banco no MongoDB Atlas (gratuito)

1. Crie uma conta em https://www.mongodb.com/cloud/atlas/register
2. Crie um cluster gratuito (M0)
3. Em **Database Access**, crie um usuário com senha
4. Em **Network Access**, adicione `0.0.0.0/0` (permite qualquer IP — ok para projeto de estudo)
5. Em **Database > Connect > Drivers**, copie a *connection string* (algo como `mongodb+srv://usuario:senha@cluster0.xxxxx.mongodb.net/...`)

## 2. Configurar o backend

```bash
cd backend
npm install
cp .env.example .env
```

Edite o `.env` e cole sua connection string do Atlas em `MONGODB_URI`.

Rode o servidor:
```bash
npm start
```

Você verá: `Servidor EcoFluxo rodando na porta 3000` e `Conectado ao MongoDB Atlas com sucesso!`

Abra `http://localhost:3000` no navegador — é o dashboard.

## 3. Descobrir o IP do seu computador (para o ESP32 achar o servidor)

- Windows: `ipconfig` → procure "Endereço IPv4" (ex: 192.168.1.100)
- O ESP32 e o computador precisam estar na **mesma rede WiFi**

## 4. Configurar o código do ESP32-S3

No arquivo `esp32/ecofluxo_esp32.ino`, edite:
```cpp
const char* WIFI_SSID     = "NOME_DA_SUA_REDE";
const char* WIFI_PASSWORD = "SENHA_DA_SUA_REDE";
const char* SERVER_URL    = "http://192.168.1.100:3000/api/dados"; // seu IP aqui
```

### Bibliotecas necessárias (Gerenciador de Bibliotecas do Arduino IDE)
- `DHT sensor library` (Adafruit)
- `Adafruit Unified Sensor`
- `ArduinoJson` (por Benoit Blanchon)

Essas já vêm nativas na plataforma ESP32: `WiFi.h`, `HTTPClient.h`.

## 5. Calibrar os limites

No `.ino`, ajuste conforme os testes reais com fumaça/calor controlados:
```cpp
#define GAS_LIMITE_ATENCAO   1500
#define GAS_LIMITE_CRITICO   2800
#define TEMP_LIMITE_ATENCAO  40.0
#define TEMP_LIMITE_CRITICO  55.0
```

## 6. Deixar o site acessível de qualquer lugar (opcional, recomendado para apresentação)

Rodando só no seu PC, o dashboard e o ESP32 só funcionam na mesma rede local.
Para apresentar o projeto (ex: banca do Projeto Integrador) sem depender da sua rede:

1. Suba o código do `backend/` para o GitHub
2. Crie uma conta gratuita em https://render.com
3. Crie um "Web Service" apontando pro seu repositório
4. Configure a variável de ambiente `MONGODB_URI` no painel do Render
5. Troque o `SERVER_URL` no `.ino` pela URL pública gerada (ex: `https://ecofluxo.onrender.com/api/dados`)

Assim o site fica acessível de qualquer navegador, e o ESP32 pode enviar dados de qualquer rede com internet.

## Endpoints da API

| Método | Rota | Descrição |
|---|---|---|
| POST | /api/dados | Recebe uma leitura do ESP32 |
| GET | /api/dados?limite=30 | Retorna as últimas N leituras |
| GET | /api/dados/ultimo | Retorna a leitura mais recente |
| GET | /api/dados/estatisticas | Contagem de leituras por status |
