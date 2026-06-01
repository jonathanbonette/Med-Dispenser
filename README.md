# 💊 MedDispenser

![Versão](https://img.shields.io/badge/version-1.0.0-blue.svg)
![Plataforma](https://img.shields.io/badge/platform-ESP32-lightgrey.svg)
![Linguagem](https://img.shields.io/badge/language-C%2B%2B-00599C.svg)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)
![Licença](https://img.shields.io/badge/license-MIT-green.svg)

O **MedDispenser** é um Sistema Ciber-Físico (CPS) projetado para a dispensação automatizada e segura de medicamentos. Desenvolvido como Trabalho de Conclusão de Curso (TCC) em Engenharia Eletrônica pelo Instituto Federal de Santa Catarina (IFSC), o projeto une precisão mecânica, tolerância a falhas de rede e rastreabilidade total de dados para garantir a adesão ao tratamento de pacientes idosos ou com mobilidade reduzida.

---

## ✨ Principais Funcionalidades

*   **⏱️ Sincronização Híbrida de Tempo:** Utiliza protocolo NTP (Internet) com *fallback* automático para um módulo RTC DS3231 (Hardware), garantindo a execução do cronograma mesmo sem conexão Wi-Fi.
*   **🛡️ Máquina de Estados Não-Bloqueante:** Processamento assíncrono que evita travamentos (sem uso de `delay()` no loop principal), gerenciando simultaneamente o semáforo, o buzzer, o motor e o servidor web.
*   **🤖 Integração com Telegram:** Bot interativo que envia alertas de erro, confirmações de ingestão e permite consultas remotas de histórico e estoque.
*   **📊 Rastreabilidade e Auditoria (Log Rotation):** Gravação de eventos na memória Flash (LittleFS) com inteligência de rotação de logs para evitar desgaste físico do silício (Write Amplification).
*   **🌐 Interface Web Embarcada (Captive Portal):** Painel de controle responsivo hospedado diretamente no ESP32. Em caso de falha de rede, o dispositivo levanta seu próprio Ponto de Acesso (AP) para reconfiguração local.
*   **🚨 Trava de Segurança Crítica:** Monitoramento via micro switch que detecta se o paciente esqueceu de retirar o copo de medicação, disparando alertas sonoros/visuais e notificações remotas após 10 minutos.

---

## 🛠️ Arquitetura de Hardware

O projeto foi montado sob a plataforma ESP32 e validado com peças modeladas e impressas em 3D (PLA/PETG), garantindo tolerâncias mecânicas exatas.

*   **Microcontrolador:** ESP32 (Dual-core, Wi-Fi integrado).
*   **Atuador:** Servo Motor MG90S (Controle rotativo da gaveta de dispensação).
*   **Sensoriamento Óptico:** Sensor Breakbeam IR (Feixe colimado para confirmação da queda da pílula).
*   **Sensoriamento Mecânico:** Micro Switch (Detecção da presença do copo de coleta).
*   **Tempo Real:** Módulo RTC DS3231 (Comunicação I2C).
*   **Feedback Humano-Máquina:**
    *   Buzzer Ativo (Sinalização sonora de sucessos, erros e esquecimentos).
    *   Módulo Semáforo LED (Vermelho, Amarelo, Verde).

---

## 💻 Arquitetura de Software e Bibliotecas

A aplicação foi desenvolvida em C/C++ utilizando a abstração do framework Arduino. As seguintes dependências são necessárias para a compilação no **PlatformIO** ou **Arduino IDE**:

*   `ESPAsyncWebServer` e `AsyncTCP` (Servidor Web Assíncrono).
*   `ArduinoJson` (Manipulação e persistência do arquivo `config.json`).
*   `ESP32Servo` (Controle preciso de PWM).
*   `UniversalTelegramBot` (Comunicação via API do Telegram).
*   `RTClib` (Integração com o DS3231).

---

## 🚀 Instalação e Uso

### 1. Preparação do Ambiente
1. Clone este repositório: `git clone https://github.com/seu-usuario/med-dispenser.git`
2. Abra o projeto no VS Code com a extensão **PlatformIO**.
3. Compile e faça o upload do firmware (`main.cpp`) para o seu ESP32.
4. Faça o upload do *Filesystem Image* (para formatar a partição LittleFS inicial).

### 2. Configuração Inicial (Modo AP)
* Ao ser ligado pela primeira vez (ou se não encontrar a rede), o ESP32 criará uma rede Wi-Fi chamada `Dispenser_Setup`.
* Conecte-se a esta rede e acesse `http://192.168.4.1` no navegador.
* Insira as credenciais da sua rede Wi-Fi local e o seu `Chat ID` do Telegram.
* O dispositivo reiniciará e se conectará à rede configurada.

### 3. Comandos do Telegram
Inicie uma conversa com o seu Bot no Telegram para gerenciar o dispositivo remotamente:
*   `/start` - Inicia a interação com o bot.
*   `/informacoes` - Exibe o status do RTC, estoque atual e agendamentos.
*   `/historico` - Retorna o log de auditoria físico das últimas dispensações e retiradas.
*   `/debug` - Alterna o nível de verbosidade dos logs enviados para o seu celular.

---

## 📂 Estrutura de Arquivos Internos (LittleFS)

O sistema de arquivos embarcado gerencia os dados de forma resiliente:
*   `/config.json`: Armazena SSID, Senha, Tutor ID, Estoque e Cronograma.
*   `/logs.txt`: Arquivo de texto em modo *append-only* que registra a vida útil do sistema.
*   `/logs.old`: Backup gerado automaticamente quando `/logs.txt` atinge o limite de 50KB.

---

## 📚 Documentação Técnica (Doxygen)

A base de código deste projeto está documentada de acordo com os padrões da indústria utilizando o **Doxygen**. 
Para visualizar os grafos de chamadas, estruturas de dados e detalhamento das funções:
1. Navegue até o diretório `html/` na raiz do projeto.
2. Abra o arquivo `index.html` em qualquer navegador web moderno.

---

## 👨‍💻 Autor

**Jonathan Bonette**  
*Desenvolvedor Backend & Estudante de Engenharia Eletrônica (IFSC)*  
* Construído com dedicação para melhorar a qualidade de vida e a rotina de medicação de idosos.