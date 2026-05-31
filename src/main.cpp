#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <time.h> 
#include <Wire.h>
#include "RTClib.h"

// --- CONFIGURAÇÕES DE TEMPO (NTP) ---
const char* ntpServer = "br.pool.ntp.org"; 
const long  gmtOffset_sec = -10800; // UTC-3
const int   daylightOffset_sec = 0;

// Instância do Servidor Web, Bot e RTC
AsyncWebServer server(80);
#define BOT_TOKEN "8786688182:AAEPucGKr2TqNSwUycBdkxG6ZhG5BotQs1c"
WiFiClientSecure clientTelegram;
UniversalTelegramBot bot(BOT_TOKEN, clientTelegram);
RTC_DS3231 rtc;

int botRequestDelay = 2000; 
unsigned long lastTimeBotRan = 0;

// Variáveis Globais
String ssid = "";
String password = "";
String tutor_id = "";
int estoqueTotal = 0;
bool configurado = false;
bool resetPendente = false;
unsigned long tempoReset = 0;
bool modoDebug = true; 

// --- PINOS DE HARDWARE ---
Servo motorDispenser;
const int pinoMotor = 12; 
const int pinoSensor = 14; // Sensor Breakbeam IR
const int botaoBoot = 0;   // Botão de Acionamento Manual / Intervenção

// Pinos do Semáforo e Copo
const int pinoLedR = 27;
const int pinoLedY = 26;
const int pinoLedG = 25;
const int pinoMicroSwitch = 33; // Sensor do copinho

volatile bool pilulaDetectada = false;
bool esperandoRetiradaCopo = false;

// --- REGRA DE NEGÓCIO: AGENDAMENTO ---
struct Dose {
    int hora;
    int minuto;
    bool processada; 
};

const int MAX_DOSES = 10;
Dose cronograma[MAX_DOSES];
int totalDoses = 0; 

// Interrupção do Sensor IR
void IRAM_ATTR sensorISR() {
    pilulaDetectada = true;
}

// Helper do Semáforo
void atualizarSemaforo(bool r, bool y, bool g) {
    digitalWrite(pinoLedR, r);
    digitalWrite(pinoLedY, y);
    digitalWrite(pinoLedG, g);
}

// ======================================================================
// INTERFACE HTML 
// ======================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MedDispenser Pro</title>
    <style>
        :root { --primary: #2c3e50; --secondary: #3498db; --success: #27ae60; --danger: #e74c3c; --light: #ecf0f1; }
        body { font-family: 'Segoe UI', sans-serif; margin: 0; background: var(--light); color: var(--primary); }
        nav { background: var(--primary); color: white; padding: 1rem; display: flex; justify-content: space-around; position: sticky; top: 0; }
        nav a { color: white; text-decoration: none; font-weight: bold; cursor: pointer; }
        .container { max-width: 500px; margin: 20px auto; padding: 20px; }
        .card { background: white; padding: 20px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); margin-bottom: 20px; }
        input, select { width: 100%; padding: 12px; margin: 10px 0; border: 1px solid #ddd; border-radius: 6px; box-sizing: border-box; }
        .btn { border: none; padding: 12px 25px; border-radius: 8px; cursor: pointer; font-size: 1rem; width: 100%; transition: 0.3s; margin-top: 10px; color: white;}
        .btn-success { background: var(--success); }
        .btn-primary { background: var(--secondary); }
        .btn-danger { background: var(--danger); padding: 5px 10px; width: auto; margin: 0;}
        .hidden { display: none !important; }
        .horario-item { display: flex; justify-content: space-between; align-items: center; background: #f8f9fa; padding: 10px; border-radius: 6px; margin-bottom: 5px; border: 1px solid #ddd;}
    </style>
</head>
<body>
    <div id="activation-screen" class="hidden">
        <div style="text-align: center; padding: 50px 20px;">
            <h1>MedDispenser Setup</h1>
            <input type="text" id="wifi_ssid" placeholder="Nome do seu Wi-Fi">
            <input type="password" id="wifi_pass" placeholder="Senha do Wi-Fi">
            <input type="number" id="tutor_id_input" placeholder="Seu ID do Telegram">
            <button class="btn btn-success" onclick="salvarConfig()">Ativar</button>
        </div>
    </div>

    <div id="main-app" class="hidden">
        <nav>
            <a onclick="showTab('home')">Status</a>
            <a onclick="showTab('config')">Ajustes</a>
        </nav>
        <div class="container">
            <div id="home" class="tab-content">
                <div class="card">
                    <h2>Visão Geral</h2>
                    <p>📦 Estoque: <strong id="stock-val" style="font-size: 1.5em; color: var(--success);">--</strong></p>
                    <p>🕒 Próximos Disparos:</p>
                    <div id="lista-horarios-home">Carregando...</div>
                </div>
            </div>

            <div id="config" class="tab-content hidden">
                <div class="card">
                    <h3>Abastecimento</h3>
                    <input type="number" id="add-stock" placeholder="Novo total de pílulas na gaveta">
                    <button class="btn btn-success" onclick="updateStock()">Salvar Estoque</button>
                </div>

                <div class="card">
                    <h3>Cronograma de Doses</h3>
                    <div style="display: flex; gap: 10px;">
                        <input type="time" id="novo-horario">
                        <button class="btn btn-primary" style="width: auto;" onclick="addHorario()">Adicionar</button>
                    </div>
                    <div id="lista-horarios-edit" style="margin-top: 15px;"></div>
                    <button class="btn btn-success" onclick="salvarCronograma()">Salvar Agenda</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let horarios = [];

        function showTab(tabId) {
            document.querySelectorAll('.tab-content').forEach(t => t.classList.add('hidden'));
            document.getElementById(tabId).classList.remove('hidden');
        }

        fetch('/getStatus').then(res => res.json()).then(data => {
            if(data.configurado) {
                document.getElementById('main-app').classList.remove('hidden');
                document.getElementById('stock-val').innerText = data.estoque;
                horarios = data.horarios || [];
                renderHorarios();
            } else {
                document.getElementById('activation-screen').classList.remove('hidden');
            }
        });

        function renderHorarios() {
            horarios.sort((a, b) => a.localeCompare(b));
            const htmlEdit = horarios.map((h, index) => `
                <div class="horario-item">
                    <strong>${h}</strong>
                    <button class="btn btn-danger" onclick="removerHorario(${index})">X</button>
                </div>
            `).join('');
            const htmlHome = horarios.map(h => `<div class="horario-item">⏰ ${h}</div>`).join('');
            document.getElementById('lista-horarios-edit').innerHTML = htmlEdit || "<p>Nenhum horário cadastrado.</p>";
            document.getElementById('lista-horarios-home').innerHTML = htmlHome || "<p>Sem alarmes ativos.</p>";
        }

        function addHorario() {
            const val = document.getElementById('novo-horario').value;
            if(val && !horarios.includes(val)) {
                horarios.push(val);
                renderHorarios();
                document.getElementById('novo-horario').value = "";
            }
        }

        function removerHorario(index) {
            horarios.splice(index, 1);
            renderHorarios();
        }

        function salvarCronograma() {
            fetch('/saveCronograma', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ horarios: horarios })
            }).then(() => alert('Agenda Atualizada!'));
        }

        function salvarConfig() {
            let s = encodeURIComponent(document.getElementById('wifi_ssid').value);
            let p = encodeURIComponent(document.getElementById('wifi_pass').value);
            let t = encodeURIComponent(document.getElementById('tutor_id_input').value);
            fetch(`/saveConfig?ssid=${s}&pass=${p}&tutor=${t}`).then(() => {
                alert("Salvo! Reiniciando..."); setTimeout(() => location.reload(), 5000);
            });
        }

        function updateStock() {
            let val = document.getElementById('add-stock').value;
            fetch(`/setEstoque?valor=${val}`).then(() => location.reload());
        }
    </script>
</body>
</html>
)rawliteral";

// ======================================================================
// LÓGICA DE PERSISTÊNCIA (LITTLEFS) E RTC
// ======================================================================

void carregarConfiguracoes() {
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, file);
        if (!error) {
            ssid = doc["ssid"].as<String>();
            password = doc["password"].as<String>();
            tutor_id = doc["tutor_id"].as<String>();
            estoqueTotal = doc["estoque"].as<int>() | 0;
            modoDebug = doc["debug"] | true; 
            configurado = true;

            totalDoses = 0;
            JsonArray arr = doc["horarios"];
            for (JsonVariant v : arr) {
                if (totalDoses < MAX_DOSES) {
                    String hStr = v.as<String>();
                    int h = hStr.substring(0, 2).toInt();
                    int m = hStr.substring(3, 5).toInt();
                    cronograma[totalDoses] = {h, m, false};
                    totalDoses++;
                }
            }
        }
        file.close();
    }
}

void salvarEstoqueEDebug() {
    File file = LittleFS.open("/config.json", "r");
    JsonDocument doc; 
    deserializeJson(doc, file);
    file.close();
    
    doc["estoque"] = estoqueTotal;
    doc["debug"] = modoDebug; 
    
    file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();
}

// ======================================================================
// TELEGRAM: MENSAGENS E COMANDOS
// ======================================================================
void enviarMensagemTelegram(String texto, bool isLogRestrito = false) {
    if (tutor_id != "" && WiFi.status() == WL_CONNECTED) {
        if (isLogRestrito && !modoDebug) return; 
        bot.sendMessage(tutor_id, texto, "");
    }
}

void tratarMensagensTelegram(int numNovasMensagens) {
    for (int i = 0; i < numNovasMensagens; i++) {
        String chat_id = String(bot.messages[i].chat_id);
        String text = bot.messages[i].text;
        
        if (chat_id != tutor_id) { bot.sendMessage(chat_id, "🚫 Acesso Negado.", ""); continue; }

        if (text == "/start") {
            bot.sendMessage(chat_id, "🤖 Olá! Sou o MedDispenser.\nEnvie /informacoes para status.", "");
        } 
        else if (text == "/informacoes") {
            String resposta = "📋 Status do Sistema\n";
            resposta += "📦 Estoque: " + String(estoqueTotal) + " pílulas\n";
            resposta += "🕒 Alertas programados: " + String(totalDoses) + "\n";
            
            DateTime now = rtc.now();
            char horaAtual[20];
            sprintf(horaAtual, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
            resposta += "🕰️ Hora do RTC: " + String(horaAtual) + "\n";
            
            resposta += "🔧 Modo Debug: " + String(modoDebug ? "ATIVADO" : "DESATIVADO");
            bot.sendMessage(chat_id, resposta, "");
        }
        else if (text == "/debug") {
            modoDebug = !modoDebug;
            salvarEstoqueEDebug(); 
            if (modoDebug) bot.sendMessage(chat_id, "🔧 Modo Debug ATIVADO.", "");
            else bot.sendMessage(chat_id, "🔇 Modo Debug DESATIVADO.", "");
        }
    }
}

// ======================================================================
// LÓGICA CENTRAL: DISPENSAÇÃO (AGORA COM SEMÁFORO)
// ======================================================================

void executarCicloDispencacao(String motivo) {
    enviarMensagemTelegram("💊 Iniciando Ciclo...\nMotivo: " + motivo, true);
    bool sucesso = false;
    
    // Inicia a operação: Luz Amarela (Preparando/Girando)
    atualizarSemaforo(LOW, HIGH, LOW); 
    
    // Garante que o status do copo está resetado para esse novo ciclo
    esperandoRetiradaCopo = false; 

    for (int tentativa = 1; tentativa <= 2; tentativa++) {
        if (tentativa == 2) enviarMensagemTelegram("🔄 Tentando novamente. Pílula não detectada.", true);
        
        pilulaDetectada = false; 
        enviarMensagemTelegram("⚙️ Acionando motor (Tentativa " + String(tentativa) + " de 2)...", true);
        
        motorDispenser.write(0);   
        delay(800);
        motorDispenser.write(90);  

        unsigned long inicioEspera = millis();
        while (millis() - inicioEspera < 10000) {
            if (pilulaDetectada) { sucesso = true; break; }
            delay(10); 
        }
        if (sucesso) break; 
    }

    if (sucesso) {
        if (estoqueTotal > 0) estoqueTotal--;
        salvarEstoqueEDebug(); 
        
        // Pílula caiu: Luz Verde e entra no modo de espera do copo
        atualizarSemaforo(LOW, LOW, HIGH); 
        esperandoRetiradaCopo = true; 
        
        enviarMensagemTelegram("✅ SUCESSO! Pílula detectada.\n📦 Novo estoque: " + String(estoqueTotal), true);
    } else {
        // Falha Total: Luz Vermelha até intervenção manual (apertar botão BOOT)
        atualizarSemaforo(HIGH, LOW, LOW); 
        enviarMensagemTelegram("🚨 ERRO CRÍTICO!\nO motor girou 2 vezes e a pílula não caiu.", false);
    }
}

// ======================================================================
// SETUP E LOOP 
// ======================================================================

void setup() {
    Serial.begin(115200);

    // Configuração dos Pinos
    pinMode(botaoBoot, INPUT_PULLUP);
    pinMode(pinoSensor, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pinoSensor), sensorISR, FALLING);
    
    pinMode(pinoLedR, OUTPUT);
    pinMode(pinoLedY, OUTPUT);
    pinMode(pinoLedG, OUTPUT);
    pinMode(pinoMicroSwitch, INPUT_PULLUP);

    // Estado Inicial: Tudo apagado
    atualizarSemaforo(LOW, LOW, LOW);

    motorDispenser.setPeriodHertz(50);    
    motorDispenser.attach(pinoMotor, 500, 2400); 
    motorDispenser.write(90); 

    if (!rtc.begin()) {
        Serial.println("❌ Erro: RTC DS3231 não encontrado!");
    } else if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    LittleFS.begin(true);
    carregarConfiguracoes();

    if (!configurado || ssid == "") {
        WiFi.softAP("Dispenser_Setup");
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
        int t = 0;
        while (WiFi.status() != WL_CONNECTED && t < 20) { delay(500); Serial.print("."); t++; }
        
        if (WiFi.status() == WL_CONNECTED) {
            clientTelegram.setInsecure(); 
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
                Serial.println("✅ RTC DS3231 sincronizado via NTP da Internet.");
            }
            enviarMensagemTelegram("🤖 MedDispenser Online e Sincronizado!", true);
        } else {
            WiFi.softAP("Dispenser_Setup_Erro");
            configurado = false; 
        }
    }

    // Rotas da Web API
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    server.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest *request){ /* ... mantido ... */ 
        JsonDocument doc; doc["configurado"] = configurado; doc["tutor_id"] = tutor_id; doc["estoque"] = estoqueTotal;
        JsonArray arr = doc["horarios"].to<JsonArray>();
        for (int i = 0; i < totalDoses; i++) {
            char buffer[6]; sprintf(buffer, "%02d:%02d", cronograma[i].hora, cronograma[i].minuto); arr.add(String(buffer));
        }
        String res; serializeJson(doc, res); request->send(200, "application/json", res);
    });
    server.on("/saveConfig", HTTP_GET, [](AsyncWebServerRequest *request){ /* ... mantido ... */
        JsonDocument doc; doc["ssid"] = request->hasParam("ssid") ? request->getParam("ssid")->value() : "";
        doc["password"] = request->hasParam("pass") ? request->getParam("pass")->value() : "";
        doc["tutor_id"] = request->hasParam("tutor") ? request->getParam("tutor")->value() : "";
        doc["estoque"] = 0; doc["debug"] = true; doc["horarios"] = JsonArray(); 
        File file = LittleFS.open("/config.json", "w"); serializeJson(doc, file); file.close();
        request->send(200, "text/plain", "OK"); resetPendente = true; tempoReset = millis();
    });
    server.on("/setEstoque", HTTP_GET, [](AsyncWebServerRequest *request){ /* ... mantido ... */
        estoqueTotal = request->hasParam("valor") ? request->getParam("valor")->value().toInt() : 0;
        salvarEstoqueEDebug(); request->send(200, "text/plain", "OK");
    });
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){ /* ... mantido ... */
        if (request->url() == "/saveCronograma") {
            JsonDocument reqDoc; deserializeJson(reqDoc, (const char*)data);
            File file = LittleFS.open("/config.json", "r"); JsonDocument fileDoc; deserializeJson(fileDoc, file); file.close();
            fileDoc["horarios"] = reqDoc["horarios"];
            file = LittleFS.open("/config.json", "w"); serializeJson(fileDoc, file); file.close();
            carregarConfiguracoes(); request->send(200, "text/plain", "Cronograma Atualizado");
        }
    });

    server.begin();
}

void loop() {
    if (resetPendente && (millis() - tempoReset > 2000)) ESP.restart();

    // Verificação de Agendamento (Sempre verifica o RTC Local)
    static unsigned long ultimaVerificacaoHora = 0;
    if (millis() - ultimaVerificacaoHora > 30000) {
        ultimaVerificacaoHora = millis();
        DateTime now = rtc.now(); 

        for (int i = 0; i < totalDoses; i++) {
            if (now.hour() == cronograma[i].hora && now.minute() == cronograma[i].minuto) {
                if (!cronograma[i].processada) {
                    char msgBuf[30];
                    sprintf(msgBuf, "Agenda: %02d:%02d", cronograma[i].hora, cronograma[i].minuto);
                    executarCicloDispencacao(String(msgBuf));
                    cronograma[i].processada = true; 
                }
            } 
            else if (now.minute() != cronograma[i].minuto) {
                cronograma[i].processada = false;
            }
        }
    }

    // --- NOVA LÓGICA: DETECÇÃO DE RETIRADA DO COPO ---
    if (esperandoRetiradaCopo) {
        // Se o pino for HIGH (Significa que o circuito abriu, o copo saiu)
        // OBS: Para testar na bancada sem o switch, puxe o fio do GND que está no pino 33!
        if (digitalRead(pinoMicroSwitch) == HIGH) {
            delay(100); // Anti-bounce simples
            if (digitalRead(pinoMicroSwitch) == HIGH) {
                atualizarSemaforo(LOW, LOW, LOW); // Apaga as luzes
                esperandoRetiradaCopo = false;
                
                // Futura implementação: Cancelar o alarme de 10 minutos entra aqui!
                enviarMensagemTelegram("✅ Confirmação: O paciente retirou o copo de medicamento.", true);
            }
        }
    }

    // Acionamento Manual / Intervenção após Falha (Limpa a luz Vermelha)
    if (digitalRead(botaoBoot) == LOW) {
        delay(200); 
        executarCicloDispencacao("Acionamento Manual (Botão Placa)");
    }

    // Polling do Telegram
    if (millis() - lastTimeBotRan > botRequestDelay) {
        int numNovasMensagens = bot.getUpdates(bot.last_message_received + 1);
        while (numNovasMensagens) {
            tratarMensagensTelegram(numNovasMensagens);
            numNovasMensagens = bot.getUpdates(bot.last_message_received + 1);
        }
        lastTimeBotRan = millis();
    }
}