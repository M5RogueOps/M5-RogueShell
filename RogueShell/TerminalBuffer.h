#ifndef TERMINAL_BUFFER_H
#define TERMINAL_BUFFER_H

#include <Arduino.h>
#include <vector>

#define TERM_COLS 40
#define TERM_ROWS 15
#define MAX_HISTORY_LINES 250

struct CharCell {
    char c = ' ';
    uint16_t fg = 0xFFFF; // White
    uint16_t bg = 0x0000; // Black
};

class TerminalBuffer {
private:
    std::vector<std::vector<CharCell>> buffer;
    int write_head = 0;
    int total_lines_written = 0;
    int cursor_x = 0;
    int cursor_y = 0;
    int scroll_offset = 0; 
    bool dirty = true;
    
    uint16_t cur_fg = 0xFFFF;
    uint16_t cur_bg = 0x0000;

    void newLine() {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= TERM_ROWS) {
            cursor_y = TERM_ROWS - 1;
            write_head = (write_head + 1) % MAX_HISTORY_LINES;
            total_lines_written++;
            for (int i = 0; i < TERM_COLS; i++) {
                buffer[write_head][i] = {' ', cur_fg, cur_bg};
            }
            if (scroll_offset > 0) {
                scroll_offset++;
                int max_scroll = std::min(total_lines_written, MAX_HISTORY_LINES - TERM_ROWS);
                if (scroll_offset > max_scroll) scroll_offset = max_scroll;
            }
        }
        dirty = true;
    }

public:
    TerminalBuffer() {
        buffer.resize(MAX_HISTORY_LINES, std::vector<CharCell>(TERM_COLS));
        clear();
    }

    void clear() {
        for (auto& row : buffer) {
            for (auto& cell : row) {
                cell = {' ', 0xFFFF, 0x0000};
            }
        }
        write_head = 0;
        total_lines_written = 0;
        cursor_x = 0;
        cursor_y = 0;
        scroll_offset = 0;
        dirty = true;
    }

    void write(const char* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            char c = data[i];
            
            if (c == '\r') {
                cursor_x = 0;
            } else if (c == '\n') {
                newLine();
            } else if (c == '\b' || c == 0x7F) {
                // Check for BOTH standard Backspace (ASCII 8) and Delete (ASCII 127)
                if (cursor_x > 0) {
                    cursor_x--;
                    int phys_row = (write_head - (TERM_ROWS - 1 - cursor_y) + MAX_HISTORY_LINES) % MAX_HISTORY_LINES;
                    buffer[phys_row][cursor_x] = {' ', cur_fg, cur_bg};
                }
            } else if (c == 0x1B) {
                if (i + 1 < len && data[i+1] == '[') {
                    i += 2;
                    while (i < len && ((data[i] >= '0' && data[i] <= '9') || data[i] == ';' || data[i] == '?')) {
                        i++;
                    }
                }
            } else if (c >= 32 && c <= 126) {
                int phys_row = (write_head - (TERM_ROWS - 1 - cursor_y) + MAX_HISTORY_LINES) % MAX_HISTORY_LINES;
                buffer[phys_row][cursor_x] = {c, cur_fg, cur_bg};
                cursor_x++;
                if (cursor_x >= TERM_COLS) {
                    newLine();
                }
            }
        }
        dirty = true;
    }

    void scrollUp(int lines = 1) {
        int max_scroll = std::min(total_lines_written, MAX_HISTORY_LINES - TERM_ROWS);
        if (max_scroll <= 0) return;
        
        scroll_offset += lines;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
        dirty = true;
    }

    void scrollDown(int lines = 1) {
        if (scroll_offset == 0) return;
        scroll_offset -= lines;
        if (scroll_offset < 0) scroll_offset = 0;
        dirty = true;
    }

    void scrollToBottom() {
        if (scroll_offset != 0) {
            scroll_offset = 0;
            dirty = true;
        }
    }

    bool isDirty() const { return dirty; }
    void clearDirty() { dirty = false; }
    int getScrollOffset() const { return scroll_offset; }

    void render(M5Canvas& canvas) {
        if (!dirty) return;
        canvas.fillScreen(BLACK);
        canvas.setTextSize(1);
        canvas.setTextFont(0); 

        for (int row = 0; row < TERM_ROWS; row++) {
            int history_offset = (TERM_ROWS - 1 - row) + scroll_offset;
            int phys_row = (write_head - history_offset + MAX_HISTORY_LINES * 2) % MAX_HISTORY_LINES;

            for (int col = 0; col < TERM_COLS; col++) {
                CharCell& cell = buffer[phys_row][col];
                if (cell.c != ' ') {
                    canvas.setTextColor(cell.fg, cell.bg);
                    canvas.drawChar(cell.c, col * 6, row * 8);
                }
            }
        }

        if (scroll_offset > 0) {
            canvas.fillRect(235, 0, 5, 120, TFT_DARKGREY);
            int bar_y = map(scroll_offset, 0, MAX_HISTORY_LINES, 110, 0);
            canvas.fillRect(235, bar_y, 5, 10, TFT_YELLOW);
        }
        
        clearDirty();
    }
};

#endif // TERMINAL_BUFFER_H
