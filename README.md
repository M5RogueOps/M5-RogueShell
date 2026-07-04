\# RogueShell



\*\*RogueShell\*\* is a high-performance, scrollable SSH client firmware specifically designed for the \*\*M5Cardputer ADV\*\*. 



It was engineered to overcome the limitations of standard terminal firmware, providing essential features for field work: native terminal history, smooth command editing, and the ability to scroll through historical log output.



\## Features



\* \*\*Scrollable Terminal View:\*\* Use `FN + ;` (Up) and `FN + .` (Down) to traverse your terminal history buffer directly on the display.

\* \*\*Persistent Credentials:\*\* Stores Wi-Fi and SSH settings in NVS memory. No more hardcoding credentials into your firmware.

\* \*\*Command History Modal:\*\* Press `FN + h` to open a local command history. Scroll, select, edit, and transmit previous commands with ease.

\* \*\*Native Shell Integration:\*\* Hold `ALT` + Arrow Keys (`ALT + ;/. ,/ /`) to interact with your remote server's native history and cursor movement.

\* \*\*Robust Stability:\*\* Built as a dedicated FreeRTOS task with a 48KB stack to prevent crashes during complex cryptographic handshakes.

\* \*\*Field-Ready Editing:\*\* Full Backspace/Delete support (ASCII 127) and Enter-key transmission compatibility for a true "terminal" experience.



\## Quick Start Configuration



When you first boot \*\*RogueShell\*\*, it will enter the Settings Menu if no credentials are found.



1\. \*\*Navigate:\*\* Use `FN + ;` (Up) and `FN + .` (Down) to highlight fields.

2\. \*\*Edit:\*\* Press `Enter` to edit a field.

3\. \*\*Confirm:\*\* Press `Enter` again to save your input.

4\. \*\*Connect:\*\* Highlight `\[ SAVE \& CONNECT ]` and press `Enter`.

5\. \*\*Re-entry:\*\* You can return to the settings menu at any time by pressing `FN + s`.



\## Keyboard Controls



| Key Combo | Action |

| :--- | :--- |

| `FN + s` | Open Settings Menu |

| `FN + h` | Open Local Command History |

| `FN + ;` / `.` | Scroll terminal output (up/down) |

| `ALT + ;` / `.` | Navigate remote shell history (Up/Down) |

| `ALT + ,` / `/` | Move remote cursor (Left/Right) |

| `FN + ;` / `.` (in History) | Navigate history list |



\## Technical Requirements



\* \*\*Hardware:\*\* M5Stack Cardputer (ADV)

\* \*\*Libraries:\*\*

&#x20;   \* \[M5Cardputer](https://github.com/m5stack/M5Cardputer)

&#x20;   \* \[LibSSH-ESP32](https://github.com/ewan-parker/libssh-esp32)

&#x20;   \* ESP32 Board Manager (ESP32-S3)



\## License

Built for the field. Modify, adapt, and deploy as needed.



\---

\*Designed for those who need a tool that just works, every time.\*

