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

// Pinos do Semáforo, Copo e Áudio
const int pinoLedR = 27;
const int pinoLedY = 26;
const int pinoLedG = 25;
const int pinoMicroSwitch = 33; 
const int pinoBuzzer = 5; 

volatile bool pilulaDetectada = false;
bool esperandoRetiradaCopo = false;

// --- VARIÁVEIS: CONTROLE DA TRAVA DE SEGURANÇA ---
unsigned long tempoSucessoDispencacao = 0;
bool alertaEsquecimentoDisparado = false;
int contadorBipsEsquecimento = 0; // Novo contador
const unsigned long TIMEOUT_ESQUECIMENTO = 600000; // 10 minutos (Debug: 10000 para 10s)

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

// Helpers Visuais e Sonoros
void atualizarSemaforo(bool r, bool y, bool g) {
    digitalWrite(pinoLedR, r);
    digitalWrite(pinoLedY, y);
    digitalWrite(pinoLedG, g);
}

void tocarBuzzer(int repeticoes, int tempoLigado, int tempoDesligado) {
    for (int i = 0; i < repeticoes; i++) {
        digitalWrite(pinoBuzzer, HIGH);
        delay(tempoLigado);
        digitalWrite(pinoBuzzer, LOW);
        if (i < repeticoes - 1) { 
            delay(tempoDesligado);
        }
    }
}

// ======================================================================
// INTERFACE HTML (MANTIDA)
// ======================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MedDispenser Pro</title>
    <style>
        :root { --primary: #2c3e50; --secondary: #3498db; --success: #27ae60; --warning: #f39c12; --danger: #e74c3c; --light: #ecf0f1; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; background: #f4f7f6; color: var(--primary); }
        nav { background: var(--primary); color: white; padding: 1rem; display: flex; justify-content: space-around; position: sticky; top: 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); z-index: 10; }
        nav a { color: white; text-decoration: none; font-weight: bold; cursor: pointer; padding: 5px 15px; border-radius: 20px; transition: 0.3s; }
        nav a:hover { background: rgba(255,255,255,0.2); }
        .container { max-width: 550px; margin: 20px auto; padding: 0 15px; }
        .card { background: white; padding: 25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); margin-bottom: 20px; }
        .card h2, .card h3 { margin-top: 0; color: var(--primary); border-bottom: 2px solid var(--light); padding-bottom: 10px; }
        input, select { width: 100%; padding: 12px; margin: 10px 0; border: 1px solid #ddd; border-radius: 8px; box-sizing: border-box; font-size: 1rem; transition: border-color 0.3s; }
        input:focus { border-color: var(--secondary); outline: none; }
        .btn { border: none; padding: 12px 25px; border-radius: 8px; cursor: pointer; font-size: 1rem; width: 100%; transition: transform 0.1s, opacity 0.3s; margin-top: 10px; color: white; font-weight: bold; }
        .btn:active { transform: scale(0.98); }
        .btn-success { background: var(--success); }
        .btn-success:hover { opacity: 0.9; }
        .btn-primary { background: var(--secondary); }
        .btn-primary:hover { opacity: 0.9; }
        .btn-danger { background: var(--danger); padding: 6px 12px; width: auto; margin: 0; border-radius: 6px; }
        .hidden { display: none !important; }
        .horario-item { display: flex; justify-content: space-between; align-items: center; background: #f8f9fa; padding: 12px 15px; border-radius: 8px; margin-bottom: 8px; border: 1px solid #eee; font-size: 1.1rem; }
        
        /* Cores Dinâmicas do Estoque */
        .stock-good { color: var(--success); }
        .stock-warning { color: var(--warning); }
        .stock-danger { color: var(--danger); }
        
        /* Info Device */
        .device-info { display: flex; justify-content: space-between; font-size: 0.9rem; color: #7f8c8d; margin-bottom: 15px; }
        
        /* Toast Notification */
        #toast { visibility: hidden; min-width: 250px; background-color: #34495e; color: #fff; text-align: center; border-radius: 8px; padding: 16px; position: fixed; z-index: 100; left: 50%; bottom: 30px; transform: translateX(-50%); font-size: 1rem; font-weight: bold; box-shadow: 0 4px 12px rgba(0,0,0,0.2); opacity: 0; transition: opacity 0.3s, bottom 0.3s; }
        #toast.show { visibility: visible; opacity: 1; bottom: 50px; }
    </style>
</head>
<body>
    <div id="activation-screen" class="hidden">
        <div style="text-align: center; padding: 50px 20px;">
            <h1 style="color: var(--primary);">⚙️ Setup MedDispenser</h1>
            <p>Conecte o dispositivo à rede local.</p>
            <input type="text" id="wifi_ssid" placeholder="Nome do seu Wi-Fi">
            <input type="password" id="wifi_pass" placeholder="Senha do Wi-Fi">
            <input type="number" id="tutor_id_input" placeholder="Seu ID do Telegram">
            <button class="btn btn-success" onclick="salvarConfig()">Ativar Dispositivo</button>
        </div>
    </div>

    <div id="main-app" class="hidden">
        <nav>
            <a onclick="showTab('home')">📊 Status</a>
            <a onclick="showTab('config')">⚙️ Ajustes</a>
        </nav>
        
        <div class="container">
            <div id="home" class="tab-content">
                <div class="card">
                    <div class="device-info">
                        <span>Status: <strong style="color: var(--success);">Online</strong></span>
                        <span id="device-time">Hora: --:--</span>
                    </div>
                    <h2>Visão Geral</h2>
                    <p style="font-size: 1.2rem;">📦 Estoque Atual: <strong id="stock-val" style="font-size: 1.8em;">--</strong></p>
                    <h3 style="margin-top: 25px;">🕒 Próximos Disparos</h3>
                    <div id="lista-horarios-home">Carregando...</div>
                </div>
            </div>

            <div id="config" class="tab-content hidden">
                <div class="card">
                    <h3>📦 Abastecimento</h3>
                    <p style="font-size: 0.9rem; color: #7f8c8d;">Insira a quantidade total de pílulas colocadas no reservatório.</p>
                    <input type="number" id="add-stock" placeholder="Ex: 30">
                    <button class="btn btn-success" onclick="updateStock()">Confirmar Abastecimento</button>
                </div>

                <div class="card">
                    <h3>⏰ Cronograma de Doses</h3>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <input type="time" id="novo-horario">
                        <button class="btn btn-primary" style="width: auto; margin-top: 0;" onclick="addHorario()">Adicionar</button>
                    </div>
                    <div id="lista-horarios-edit" style="margin-top: 20px;"></div>
                    <button class="btn btn-success" style="margin-top: 15px;" onclick="salvarCronograma()">Salvar na Memória</button>
                </div>
            </div>
        </div>
    </div>

    <div id="toast"></div>

    <script>
        let horarios = [];

        function showToast(message) {
            const toast = document.getElementById("toast");
            toast.innerText = message;
            toast.className = "show";
            setTimeout(function(){ toast.className = toast.className.replace("show", ""); }, 3000);
        }

        function showTab(tabId) {
            document.querySelectorAll('.tab-content').forEach(t => t.classList.add('hidden'));
            document.getElementById(tabId).classList.remove('hidden');
        }

        fetch('/getStatus')
            .then(res => res.json())
            .then(data => {
                if(data.configurado) {
                    document.getElementById('main-app').classList.remove('hidden');
                    
                    // Atualiza a hora do dispositivo
                    if(data.hora_rtc) document.getElementById('device-time').innerText = "Hora: " + data.hora_rtc;

                    // Lógica de Cor do Estoque
                    let stockEl = document.getElementById('stock-val');
                    stockEl.innerText = data.estoque;
                    stockEl.className = ""; // Limpa classes antigas
                    if (data.estoque <= 3) {
                        stockEl.classList.add('stock-danger');
                    } else if (data.estoque <= 10) {
                        stockEl.classList.add('stock-warning');
                    } else {
                        stockEl.classList.add('stock-good');
                    }

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
            
            document.getElementById('lista-horarios-edit').innerHTML = htmlEdit || "<p style='color:#7f8c8d;'>Nenhum horário cadastrado.</p>";
            document.getElementById('lista-horarios-home').innerHTML = htmlHome || "<p style='color:#7f8c8d;'>Sem alarmes ativos no momento.</p>";
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
            }).then(() => showToast('✅ Agenda Salva com Sucesso!'));
        }

        function salvarConfig() {
            let s = encodeURIComponent(document.getElementById('wifi_ssid').value); 
            let p = encodeURIComponent(document.getElementById('wifi_pass').value); 
            let t = encodeURIComponent(document.getElementById('tutor_id_input').value);
            fetch(`/saveConfig?ssid=${s}&pass=${p}&tutor=${t}`).then(() => { 
                showToast("✅ Salvo! Reiniciando o aparelho..."); 
                setTimeout(() => location.reload(), 5000); 
            });
        }

        function updateStock() {
            let val = document.getElementById('add-stock').value; 
            if(val === "") return;
            fetch(`/setEstoque?valor=${val}`).then(() => {
                showToast("📦 Estoque Atualizado!");
                setTimeout(() => location.reload(), 1500);
            });
        }
    </script>
</body>
</html>
)rawliteral";

// ======================================================================
// LÓGICA DE PERSISTÊNCIA E TELEGRAM
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
    JsonDocument doc; deserializeJson(doc, file); file.close();
    doc["estoque"] = estoqueTotal; doc["debug"] = modoDebug; 
    file = LittleFS.open("/config.json", "w"); serializeJson(doc, file); file.close();
}

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

        if (text == "/start") { bot.sendMessage(chat_id, "🤖 Olá! Sou o MedDispenser.\nEnvie /informacoes para status.", ""); } 
        else if (text == "/informacoes") {
            String resposta = "📋 Status do Sistema\n📦 Estoque: " + String(estoqueTotal) + " pílulas\n🕒 Alertas programados: " + String(totalDoses) + "\n";
            DateTime now = rtc.now(); char horaAtual[20]; sprintf(horaAtual, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
            resposta += "🕰️ Hora do RTC: " + String(horaAtual) + "\n🔧 Modo Debug: " + String(modoDebug ? "ATIVADO" : "DESATIVADO");
            bot.sendMessage(chat_id, resposta, "");
        }
        else if (text == "/debug") {
            modoDebug = !modoDebug; salvarEstoqueEDebug(); 
            if (modoDebug) bot.sendMessage(chat_id, "🔧 Modo Debug ATIVADO.", "");
            else bot.sendMessage(chat_id, "🔇 Modo Debug DESATIVADO.", "");
        }
    }
}

// ======================================================================
// LÓGICA CENTRAL: DISPENSAÇÃO
// ======================================================================

void executarCicloDispencacao(String motivo) {
    enviarMensagemTelegram("💊 Iniciando Ciclo...\nMotivo: " + motivo, true);
    bool sucesso = false;
    
    atualizarSemaforo(LOW, HIGH, LOW); 
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
        
        atualizarSemaforo(LOW, LOW, HIGH); 
        tocarBuzzer(3, 500, 300);
        
        // --- PREPARAÇÃO DO ESTADO DE ESPERA ---
        esperandoRetiradaCopo = true; 
        tempoSucessoDispencacao = millis(); 
        alertaEsquecimentoDisparado = false; 
        contadorBipsEsquecimento = 0; // Zera o contador para o novo ciclo
        
        enviarMensagemTelegram("✅ SUCESSO! Pílula detectada.\n📦 Novo estoque: " + String(estoqueTotal), true);
    } else {
        atualizarSemaforo(HIGH, LOW, LOW); 
        tocarBuzzer(5, 100, 100);
        enviarMensagemTelegram("🚨 ERRO CRÍTICO!\nO motor girou 2 vezes e a pílula não caiu.", false);
    }
}

// ======================================================================
// SETUP E LOOP 
// ======================================================================

void setup() {
    Serial.begin(115200);

    pinMode(botaoBoot, INPUT_PULLUP);
    pinMode(pinoSensor, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pinoSensor), sensorISR, FALLING);
    
    pinMode(pinoLedR, OUTPUT);
    pinMode(pinoLedY, OUTPUT);
    pinMode(pinoLedG, OUTPUT);
    pinMode(pinoBuzzer, OUTPUT); 
    pinMode(pinoMicroSwitch, INPUT_PULLUP);

    atualizarSemaforo(LOW, LOW, LOW);
    digitalWrite(pinoBuzzer, LOW);

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
            }
            enviarMensagemTelegram("🤖 MedDispenser Online e Sincronizado!", true);
        } else {
            WiFi.softAP("Dispenser_Setup_Erro");
            configurado = false; 
        }
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    server.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest *request){ 
        JsonDocument doc; 
        doc["configurado"] = configurado; 
        doc["tutor_id"] = tutor_id; 
        doc["estoque"] = estoqueTotal;
        
        // --- Envia a hora atual do RTC para o painel ---
        DateTime now = rtc.now(); 
        char horaAtual[6]; 
        sprintf(horaAtual, "%02d:%02d", now.hour(), now.minute());
        doc["hora_rtc"] = String(horaAtual);

        JsonArray arr = doc["horarios"].to<JsonArray>();
        for (int i = 0; i < totalDoses; i++) { 
            char buffer[6]; 
            sprintf(buffer, "%02d:%02d", cronograma[i].hora, cronograma[i].minuto); 
            arr.add(String(buffer)); 
        }
        String res; serializeJson(doc, res); request->send(200, "application/json", res);
    });
    server.on("/saveConfig", HTTP_GET, [](AsyncWebServerRequest *request){ 
        JsonDocument doc; doc["ssid"] = request->hasParam("ssid") ? request->getParam("ssid")->value() : "";
        doc["password"] = request->hasParam("pass") ? request->getParam("pass")->value() : "";
        doc["tutor_id"] = request->hasParam("tutor") ? request->getParam("tutor")->value() : "";
        doc["estoque"] = 0; doc["debug"] = true; doc["horarios"] = JsonArray(); 
        File file = LittleFS.open("/config.json", "w"); serializeJson(doc, file); file.close();
        request->send(200, "text/plain", "OK"); resetPendente = true; tempoReset = millis();
    });
    server.on("/setEstoque", HTTP_GET, [](AsyncWebServerRequest *request){ 
        estoqueTotal = request->hasParam("valor") ? request->getParam("valor")->value().toInt() : 0;
        salvarEstoqueEDebug(); request->send(200, "text/plain", "OK");
    });
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){ 
        if (request->url() == "/saveCronograma") {
            JsonDocument reqDoc; deserializeJson(reqDoc, (const char*)data);
            File file = LittleFS.open("/config.json", "r"); JsonDocument fileDoc; deserializeJson(fileDoc, file); file.close();
            fileDoc["horarios"] = reqDoc["horarios"];
            file = LittleFS.open("/config.json", "w"); serializeJson(fileDoc, file); file.close();
            carregarConfiguracoes(); request->send(200, "text/plain", "Cronograma Updated");
        }
    });

    server.begin();
}

void loop() {
    if (resetPendente && (millis() - tempoReset > 2000)) ESP.restart();

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

    // --- MÁQUINA DE ESTADOS: MONITORAMENTO E TRAVA COM LIMITE DE BIPS ---
    if (esperandoRetiradaCopo) {
        
        // 1. Sucesso: Paciente retira o copo
        if (digitalRead(pinoMicroSwitch) == HIGH) {
            delay(100); 
            if (digitalRead(pinoMicroSwitch) == HIGH) {
                digitalWrite(pinoBuzzer, LOW);    
                atualizarSemaforo(LOW, LOW, LOW); 
                
                tocarBuzzer(1, 150, 0); 
                
                esperandoRetiradaCopo = false;
                alertaEsquecimentoDisparado = false;
                contadorBipsEsquecimento = 0; // Limpa o contador
                enviarMensagemTelegram("✅ Confirmação: O paciente retirou o copo de medicamento.", true);
            }
        }
        
        // 2. Esquecimento: Estouro do tempo limite (Timeout)
        else if (millis() - tempoSucessoDispencacao > TIMEOUT_ESQUECIMENTO) {
            
            if (!alertaEsquecimentoDisparado) {
                alertaEsquecimentoDisparado = true;
                contadorBipsEsquecimento = 0; // Prepara para iniciar as 3 sequências sonoras
                enviarMensagemTelegram("🚨 ALERTA CRÍTICO!\nO medicamento foi liberado com sucesso, mas o paciente AINDA NÃO RETIROU o copo do dispenser após 10 minutos!", false);
            }
            
            // Só apita as 3 vezes (6 alternâncias: 3 On, 3 Off)
            if (contadorBipsEsquecimento < 6) {
                static unsigned long ultimoFlicker = 0;
                if (millis() - ultimoFlicker > 500) { 
                    ultimoFlicker = millis();
                    
                    if (contadorBipsEsquecimento % 2 == 0) { // Estados pares: Liga tudo
                        atualizarSemaforo(HIGH, LOW, LOW); // Vermelho aceso
                        digitalWrite(pinoBuzzer, HIGH);    // Bipe
                    } else { // Estados ímpares: Desliga bipe e troca pro Verde
                        atualizarSemaforo(LOW, LOW, HIGH); // Verde aceso para chamar atenção pra pílula
                        digitalWrite(pinoBuzzer, LOW);     // Silêncio
                    }
                    contadorBipsEsquecimento++;
                }
            } else {
                // Fim dos 3 bipes: Crava o semáforo no Vermelho sólido e fica mudo até o copo sair
                atualizarSemaforo(HIGH, LOW, LOW);
                digitalWrite(pinoBuzzer, LOW);
            }
        }
    }

    if (digitalRead(botaoBoot) == LOW) {
        delay(200); 
        executarCicloDispencacao("Acionamento Manual (Botão Placa)");
    }

    if (millis() - lastTimeBotRan > botRequestDelay) {
        int numNovasMensagens = bot.getUpdates(bot.last_message_received + 1);
        while (numNovasMensagens) {
            tratarMensagensTelegram(numNovasMensagens);
            numNovasMensagens = bot.getUpdates(bot.last_message_received + 1);
        }
        lastTimeBotRan = millis();
    }
}