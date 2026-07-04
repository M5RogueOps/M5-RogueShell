#include <M5Cardputer.h>
#include <WiFi.h>
#include <libssh_esp32.h>
#include <libssh/libssh.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <vector>
#include "TerminalBuffer.h"

// --- Embedded LibSSH-ESP32 C++ Wrapper ---
class SSHSession {
private:
    ssh_session session = nullptr;
    ssh_channel channel = nullptr;
    bool connected = false;

public:
    SSHSession() {}
    ~SSHSession() { close(); }

    bool connect(const char* host, int port, const char* user, const char* pass) {
        close();
        session = ssh_new();
        if (session == nullptr) return false;

        ssh_options_set(session, SSH_OPTIONS_HOST, host);
        ssh_options_set(session, SSH_OPTIONS_PORT, &port);
        ssh_options_set(session, SSH_OPTIONS_USER, user);
        
        long timeout = 5; 
        ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);

        if (ssh_connect(session) != SSH_OK) {
            ssh_free(session);
            session = nullptr;
            return false;
        }

        if (ssh_userauth_password(session, nullptr, pass) != SSH_AUTH_SUCCESS) {
            ssh_disconnect(session);
            ssh_free(session);
            session = nullptr;
            return false;
        }

        connected = true;
        return true;
    }

    bool openShell() {
        if (!connected || session == nullptr) return false;

        channel = ssh_channel_new(session);
        if (channel == nullptr) return false;

        if (ssh_channel_open_session(channel) != SSH_OK) {
            ssh_channel_free(channel);
            channel = nullptr;
            return false;
        }

        if (ssh_channel_request_pty_size(channel, "vt100", 40, 15) != SSH_OK) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            channel = nullptr;
            return false;
        }

        if (ssh_channel_request_shell(channel) != SSH_OK) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            channel = nullptr;
            return false;
        }

        return true;
    }

    int read(uint8_t* buffer, size_t max_len) {
        if (channel == nullptr || !ssh_channel_is_open(channel)) {
            connected = false;
            return -1;
        }
        return ssh_channel_read_nonblocking(channel, buffer, max_len, 0);
    }

    int write(const void* data, size_t len) {
        if (channel == nullptr || !ssh_channel_is_open(channel)) {
            connected = false;
            return -1;
        }
        return ssh_channel_write(channel, data, len);
    }

    bool isConnected() {
        if (session == nullptr || channel == nullptr) return false;
        if (!ssh_channel_is_open(channel) || ssh_channel_is_eof(channel)) {
            connected = false;
        }
        return connected;
    }

    void close() {
        if (channel != nullptr) {
            if (ssh_channel_is_open(channel)) {
                ssh_channel_send_eof(channel);
                ssh_channel_close(channel);
            }
            ssh_channel_free(channel);
            channel = nullptr;
        }
        if (session != nullptr) {
            ssh_disconnect(session);
            ssh_free(session);
            session = nullptr;
        }
        connected = false;
    }
};

// --- System States ---
enum AppState {
    STATE_CONNECTING,
    STATE_TERMINAL,
    STATE_SETTINGS,
    STATE_HISTORY
};

AppState currentState = STATE_CONNECTING;
Preferences prefs;
M5Canvas sprite(&M5Cardputer.Display);
TerminalBuffer term;
SSHSession ssh;

// Credential variables loaded from NVS memory
String wifi_ssid     = "";
String wifi_pass     = "";
String ssh_host      = "";
int    ssh_port      = 22;
String ssh_user      = "";
String ssh_pass      = "";

// Settings UI state
int selected_field = 0;
const int TOTAL_FIELDS = 7;
String edit_buffer = "";
bool is_editing = false;

// --- Command History State ---
std::vector<String> command_history;
int history_selected_idx = 0;
String current_input_line = ""; 
String history_edit_buffer = "";
bool history_is_editing = false;

void loadCredentials() {
    prefs.begin("ssh_cfg", true);
    wifi_ssid = prefs.getString("ssid", "");
    wifi_pass = prefs.getString("w_pass", "");
    ssh_host  = prefs.getString("host", "192.168.1.100");
    ssh_port  = prefs.getInt("port", 22);
    ssh_user  = prefs.getString("user", "root");
    ssh_pass  = prefs.getString("s_pass", "");
    prefs.end();
}

void saveCredentials() {
    prefs.begin("ssh_cfg", false);
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("w_pass", wifi_pass);
    prefs.putString("host", ssh_host);
    prefs.putInt("port", ssh_port);
    prefs.putString("user", ssh_user);
    prefs.putString("s_pass", ssh_pass);
    prefs.end();
}

void drawStatusBar(const char* status, uint16_t color) {
    sprite.fillRect(0, 120, 240, 15, color);
    sprite.setTextColor(WHITE, color);
    sprite.drawString(status, 4, 124);
    if (term.getScrollOffset() > 0 && currentState == STATE_TERMINAL) {
        sprite.setTextColor(YELLOW, color);
        sprite.drawString(" [SCROLLING]", 160, 124);
    }
    sprite.pushSprite(0, 0);
}

void renderSettingsMenu() {
    sprite.fillScreen(BLACK);
    sprite.setTextSize(1);
    sprite.setTextColor(TFT_CYAN, BLACK);
    sprite.drawString("--- CONFIGURATION SETTINGS ---", 30, 5);

    const char* labels[] = {"WiFi SSID:", "WiFi Pass:", "SSH Host :", "SSH Port :", "SSH User :", "SSH Pass :", "[ SAVE & CONNECT ]"};
    String values[] = {
        wifi_ssid, 
        "********", 
        ssh_host, 
        String(ssh_port), 
        ssh_user, 
        "********", 
        ""
    };

    for (int i = 0; i < TOTAL_FIELDS; i++) {
        int y = 20 + (i * 13);
        if (i == selected_field) {
            sprite.fillRect(0, y - 1, 240, 13, TFT_DARKGREY);
            sprite.setTextColor(TFT_YELLOW, TFT_DARKGREY);
            sprite.drawString(">", 4, y);
        } else {
            sprite.setTextColor(WHITE, BLACK);
        }
        sprite.drawString(labels[i], 15, y);
        if (i < 6) {
            String displayVal = (i == selected_field && is_editing) ? edit_buffer + "_" : values[i];
            sprite.drawString(displayVal, 85, y);
        }
    }
    drawStatusBar(is_editing ? "Type & press ENTER to confirm" : "UP/DOWN: Navigate | ENTER: Edit", TFT_BLUE);
}

void renderHistoryMenu() {
    sprite.fillScreen(BLACK);
    sprite.setTextSize(1);
    sprite.setTextColor(TFT_CYAN, BLACK);
    sprite.drawString("--- COMMAND HISTORY [FN+h: Exit] ---", 10, 5);

    int start_idx = 0;
    if (history_selected_idx >= 7) {
        start_idx = history_selected_idx - 6;
    }
    int max_items = std::min((int)command_history.size() - start_idx, 7);

    for (int i = 0; i < max_items; i++) {
        int actual_idx = start_idx + i;
        int y = 20 + (i * 12);

        if (actual_idx == history_selected_idx) {
            sprite.fillRect(0, y - 1, 240, 13, TFT_DARKGREY);
            sprite.setTextColor(TFT_YELLOW, TFT_DARKGREY);
            sprite.drawString(">", 4, y);
        } else {
            sprite.setTextColor(WHITE, BLACK);
        }

        String cmd_display = command_history[actual_idx];
        if (cmd_display.length() > 35) cmd_display = cmd_display.substring(0, 32) + "...";
        sprite.drawString(cmd_display, 15, y);
    }

    if (history_is_editing) {
        sprite.fillRect(0, 103, 240, 17, TFT_DARKGREEN);
        sprite.setTextColor(WHITE, TFT_DARKGREEN);
        String disp_edit = history_edit_buffer;
        if (disp_edit.length() > 32) disp_edit = disp_edit.substring(disp_edit.length() - 32);
        sprite.drawString("EDIT: " + disp_edit + "_", 4, 107);
        drawStatusBar("ENTER: Send Cmd | BACKSPACE: Del", TFT_BLUE);
    } else {
        drawStatusBar("UP/DOWN: Navigate | ENTER: Edit Cmd", TFT_BLUE);
    }
    sprite.pushSprite(0, 0);
}

void handleSettingsKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    bool fn_pressed = M5Cardputer.Keyboard.isKeyPressed(KEY_FN);
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    if (!is_editing) {
        if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed(';')) { 
            selected_field = (selected_field - 1 + TOTAL_FIELDS) % TOTAL_FIELDS;
            renderSettingsMenu();
        } else if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed('.')) { 
            selected_field = (selected_field + 1) % TOTAL_FIELDS;
            renderSettingsMenu();
        } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
            if (selected_field == 6) { 
                saveCredentials();
                currentState = STATE_CONNECTING;
                return;
            }
            is_editing = true;
            if (selected_field == 0) edit_buffer = wifi_ssid;
            else if (selected_field == 1) edit_buffer = wifi_pass;
            else if (selected_field == 2) edit_buffer = ssh_host;
            else if (selected_field == 3) edit_buffer = String(ssh_port);
            else if (selected_field == 4) edit_buffer = ssh_user;
            else if (selected_field == 5) edit_buffer = ssh_pass;
            renderSettingsMenu();
        }
    } else {
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
            is_editing = false;
            if (selected_field == 0) wifi_ssid = edit_buffer;
            else if (selected_field == 1) wifi_pass = edit_buffer;
            else if (selected_field == 2) ssh_host = edit_buffer;
            else if (selected_field == 3) ssh_port = edit_buffer.toInt();
            else if (selected_field == 4) ssh_user = edit_buffer;
            else if (selected_field == 5) ssh_pass = edit_buffer;
            renderSettingsMenu();
            return;
        }

        if (status.del || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
            if (edit_buffer.length() > 0) {
                edit_buffer.remove(edit_buffer.length() - 1);
                renderSettingsMenu();
            }
            return;
        }

        for (auto c : status.word) {
            if (c >= 32 && c <= 126 && edit_buffer.length() < 64) {
                edit_buffer += c;
            }
        }
        renderSettingsMenu();
    }
}

void handleHistoryKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    bool fn_pressed = M5Cardputer.Keyboard.isKeyPressed(KEY_FN);
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed('h')) {
        currentState = STATE_TERMINAL;
        term.clearDirty();
        return;
    }

    if (!history_is_editing) {
        if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed(';')) { 
            if (history_selected_idx > 0) history_selected_idx--;
            renderHistoryMenu();
        } else if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed('.')) { 
            if (history_selected_idx < (int)command_history.size() - 1) history_selected_idx++;
            renderHistoryMenu();
        } else if (status.enter || M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
            history_is_editing = true;
            history_edit_buffer = command_history[history_selected_idx];
            renderHistoryMenu();
        }
    } else {
        if (status.enter || M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
            String to_send = history_edit_buffer + "\r";
            ssh.write(to_send.c_str(), to_send.length());
            
            if (command_history.empty() || command_history.back() != history_edit_buffer) {
                command_history.push_back(history_edit_buffer);
                if (command_history.size() > 30) command_history.erase(command_history.begin());
            }
            
            history_is_editing = false;
            currentState = STATE_TERMINAL;
            term.scrollToBottom();
            return;
        }

        if (status.del || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
            if (history_edit_buffer.length() > 0) {
                history_edit_buffer.remove(history_edit_buffer.length() - 1);
                renderHistoryMenu();
            }
            return;
        }

        for (auto c : status.word) {
            if (c >= 32 && c <= 126 && history_edit_buffer.length() < 128) {
                history_edit_buffer += c;
            }
        }
        renderHistoryMenu();
    }
}

bool connectAll() {
    if (wifi_ssid.isEmpty() || ssh_host.isEmpty() || ssh_user.isEmpty()) {
        return false;
    }

    drawStatusBar("Connecting WiFi...", TFT_BLUE);
    WiFi.disconnect(true);
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    
    unsigned long start_time = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start_time > 8000) return false; 
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    drawStatusBar("WiFi Connected. Connecting SSH...", TFT_DARKGREEN);
    
    if (ssh.connect(ssh_host.c_str(), ssh_port, ssh_user.c_str(), ssh_pass.c_str())) {
        ssh.openShell();
        return true;
    }
    return false;
}

void handleTerminalKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    bool fn_pressed   = M5Cardputer.Keyboard.isKeyPressed(KEY_FN);
    bool ctrl_pressed = M5Cardputer.Keyboard.isKeyPressed(KEY_LEFT_CTRL);
    bool alt_pressed  = M5Cardputer.Keyboard.isKeyPressed(KEY_LEFT_ALT);
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    // 1. Open Settings Menu with FN + s
    if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed('s')) {
        ssh.close();
        currentState = STATE_SETTINGS;
        renderSettingsMenu();
        return;
    }

    // 2. Open Local Command History Modal with FN + h
    if (fn_pressed && M5Cardputer.Keyboard.isKeyPressed('h')) {
        if (!command_history.empty()) {
            currentState = STATE_HISTORY;
            history_selected_idx = command_history.size() - 1; 
            history_is_editing = false;
            renderHistoryMenu();
        } else {
            drawStatusBar("History is empty! Type commands first.", TFT_RED);
        }
        return;
    }

    // 3. Handle Screen Scrolling (FN + ; for UP, FN + . for DOWN)
    if (fn_pressed) {
        if (M5Cardputer.Keyboard.isKeyPressed(';')) { 
            term.scrollUp(3);
            return;
        } else if (M5Cardputer.Keyboard.isKeyPressed('.')) { 
            term.scrollDown(3);
            return;
        }
        // Explicit return: Holding FN without pressing ; or . does nothing!
        return;
    }

    // 4. Send Native VT100 Arrow Keys over SSH (ALT + Cardputer Arrow keys)
    if (alt_pressed) {
        term.scrollToBottom();
        if (M5Cardputer.Keyboard.isKeyPressed(';')) { ssh.write("\x1b[A", 3); return; } // Up
        if (M5Cardputer.Keyboard.isKeyPressed('.')) { ssh.write("\x1b[B", 3); return; } // Down
        if (M5Cardputer.Keyboard.isKeyPressed(',')) { ssh.write("\x1b[D", 3); return; } // Left
        if (M5Cardputer.Keyboard.isKeyPressed('/')) { ssh.write("\x1b[C", 3); return; } // Right
        return;
    }

    // --- REMOVED THE ROGUE STANDALONE scrollToBottom() CHECK HERE ---

    // 5. Transmit Carriage Return on Enter & Save Command to History
    if (status.enter || M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        term.scrollToBottom();
        if (current_input_line.length() > 0) {
            if (command_history.empty() || command_history.back() != current_input_line) {
                command_history.push_back(current_input_line);
                if (command_history.size() > 30) command_history.erase(command_history.begin());
            }
            current_input_line = "";
        }
        char enter_char = '\r';
        ssh.write(&enter_char, 1);
        return;
    }

    // 6. Transmit ASCII 127 (DEL) on Backspace
    if (status.del || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
        term.scrollToBottom();
        if (current_input_line.length() > 0) {
            current_input_line.remove(current_input_line.length() - 1);
        }
        char del_char = 0x7F; 
        ssh.write(&del_char, 1);
        return;
    }

    // 7. Transmit Standard Characters
    if (status.word.size() > 0) {
        term.scrollToBottom();
        for (auto c : status.word) {
            if (ctrl_pressed && c >= 'a' && c <= 'z') {
                char ctrl_char = c - 'a' + 1;
                ssh.write(&ctrl_char, 1);
            } else {
                current_input_line += c;
                ssh.write(&c, 1);
            }
        }
    }
}

// --- Dedicated FreeRTOS Task with 48KB Stack ---
void sshMainTask(void* pvParameters) {
    esp_task_wdt_add(NULL);

    while (true) {
        esp_task_wdt_reset();

        switch (currentState) {
            case STATE_CONNECTING:
                if (connectAll()) {
                    currentState = STATE_TERMINAL;
                    term.clear();
                    drawStatusBar("SSH Connected [FN+s: Settings | FN+h: History]", TFT_GREEN);
                } else {
                    currentState = STATE_SETTINGS;
                    selected_field = 0;
                    is_editing = false;
                    renderSettingsMenu();
                }
                break;

            case STATE_SETTINGS:
                handleSettingsKeyboard();
                vTaskDelay(pdMS_TO_TICKS(10));
                break;

            case STATE_HISTORY:
                handleHistoryKeyboard();
                vTaskDelay(pdMS_TO_TICKS(10));
                break;

            case STATE_TERMINAL:
                uint8_t rx_buf[256];
                int bytes_read = ssh.read(rx_buf, sizeof(rx_buf) - 1);
                if (bytes_read > 0) {
                    rx_buf[bytes_read] = '\0';
                    term.write((const char*)rx_buf, bytes_read);
                }

                handleTerminalKeyboard();

                if (term.isDirty()) {
                    term.render(sprite);
                    drawStatusBar("SSH Connected [FN+s: Settings | FN+h: History]", TFT_GREEN);
                    sprite.pushSprite(0, 0);
                }

                if (!ssh.isConnected()) {
                    drawStatusBar("Connection Lost! Retrying...", TFT_RED);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    currentState = STATE_CONNECTING;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                break;
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    
    libssh_begin(); 
    
    sprite.createSprite(240, 135);
    sprite.fillScreen(BLACK);
    
    loadCredentials();

    xTaskCreatePinnedToCore(
        sshMainTask,    
        "sshMainTask",  
        49152,          
        NULL,           
        1,              
        NULL,           
        1               
    );
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}