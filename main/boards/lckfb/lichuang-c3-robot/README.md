# LCKFB ESP32-C3 1.69" Robot Dog（立创·机器人 1.69"）

基于立创 ESP32-C3 开发板的 Petoi Bittle 机器狗定制板卡（从 xiaozhi0930 移植）。

## 硬件

- 芯片：ESP32-C3
- 屏幕：ST7789 1.69"（240×280，Y 偏移 20）
- 音频编解码：ES8311（I2S，24 kHz）
- 机器人串口：UART1，TX=GPIO21、RX=GPIO20

## 机器人控制

- MCP 工具 `self.robot.send_command`：LLM 经云端调用，发送动作指令（如 `kup`、`ksit`、`kwkF 3`）。
- 本地语音→指令：用户转写文本命中动作词（"坐下"、"向左转30度"等）时本地直接发送，不依赖 LLM。
- 开机自动发 `kup` 起立。

串口协议由 `second_uart.h` 配置（接 BiBoard V1 的 Grove UART2 / OpenCat `Serial2`）：
- 115200，帧格式 `动作串 + '\n'`，例如 `ksit\n`、`kup\n`、`kwkF 3\n`
- 狗端 `moduleManager.h:read_serial()` 解析：首字符为 token、其余为参数
- 注意：板载语音模块口是 `Serial1`（26/25 @9600），与本口不同，不要混接

## 唤醒词

2.6.0 的唤醒词由 `scripts/build.py` 和 ESP-SR 的 model 分区决定，C3 仅支持 wakenet 模型（`USE_ESP_WAKE_WORD`）。

- 列出可用唤醒词：
  ```bash
  python scripts/build.py --list-wake-words
  ```
- 选择唤醒词（例如默认 "你好小智"/"hi esp"）：
  ```bash
  python scripts/build.py --wake-word "nihaoxiaozhi"
  ```

> **"Hi Jason" 注意**：原 xiaozhi0930 使用的 `wn9s_hijason` 是外部自定义模型，不在本仓库中。如需使用，需自行将 `hijason` wakenet 模型烧录到 model 分区，再经 build.py 指定。未提供该模型时，会回退到已烧录的默认 wakenet 模型。

## 构建

```bash
idf.py set-target esp32c3
idf.py menuconfig   # Target Board → LCKFB ESP32-C3 1.69" Robot Dog
idf.py build
idf.py flash
```
