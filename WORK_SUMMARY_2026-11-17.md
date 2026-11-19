# 2026-11-17 工作总结

## 一、今日工作概述

今天的工作围绕 Petoi Bittle 机器狗的语音控制固件移植展开：把旧定制 fork `xiaozhi0930`（基于 xiaozhi-esp32 ~2.0.4）中的机器人控制、表情脸、回复抑制、唤醒词等定制能力，选择性移植到新上游 `xiaozhi-esp32`（v2.6.0，branch `bittle`），并保留 2.6.0 的新功能（notify、led、新板卡、新语言包等）。期间完成了两仓库差异分析、机器人控制核心、表情脸 UI、回复抑制、开发环境搭建，并逐一修复了构建和运行时内存崩溃等问题。固件已在 ESP32-C3 开发板上编译、烧录并验证通过，无崩溃。

## 二、两仓库差异分析与补丁

- `xiaozhi0930`（borntoleave fork，~2.0.4）含 Petoi 定制：SecondUart UART1 GPIO21/20、MCP `self.robot.send_command`、本地指令语法、表情脸、运行时语言、回复抑制等。
- `xiaozhi-esp32`（he11bunny fork 自 78/xiaozhi-esp32，2.6.0）为新上游，架构已大改：事件组主循环、`AudioServiceCallbacks`、板卡 Kconfig + CMake 注册、`SetEmotion` 情绪系统。
- 两仓库无共享 git 历史，生成了 `xiaozhi0930_to_xiaozhi-esp32.patch`（0930→esp32 方向，含二进制，可 `git apply`）。
- 移植策略：采用 2.6.0 官方「板卡内控制器类 + 专用 FreeRTOS 任务 + 队列」模式重做集成，机器人逻辑收进板卡目录，几乎不改 `application.cc` 核心。

## 三、机器人控制核心移植

### 1. 新建板卡 `lckfb/lichuang-c3-robot`

- `config.h/config.json`：立创 ESP32-C3 1.69" 240×280 屏 + ES8311 编解码，16MB flash。
- `second_uart.h/.cc`：机器人串口单例（UART1 GPIO21/20，NyBoard `XA\x15` 前缀 @9600），支持命令去重与完成令牌等待。
- `petoi_robot_controller.cc/.h`：专用 FreeRTOS 任务 + 队列，MCP `self.robot.send_command` 只入队不阻塞主循环，支持逗号分隔的多动作序列。
- `lichuang_c3_robot_board.cc`：I2C/SPI/显示/按键/UART 初始化 + `DECLARE_BOARD`。
- Kconfig `BOARD_TYPE_LICHUANG_C3_ROBOT` + CMake `BOARD_DIR` 分支。

### 2. 本地语音指令与开机起立

- 移植 `BuildRobotCommandFromUserText`（中英文动作词 → 指令码），新增 `Board::OnUserTranscript` 钩子并接入 `stt` 处理器。
- 开机自动发送 `kup` 起立。
- 修复 SecondUart 跨轮去重问题（补上 `BeginConversationTurn` 调用）。

## 四、回复抑制（Phase 2e）

- 新增 `boards/common/robot_reply.h`：文本清洗 + 误导性回复词表（"I can't actually move"、"动作失败" 等）+ 工具收尾词表。
- `application` 增加 hold 音频缓冲、抑制标志、`sentence_start` 决策点、8s 机器人窗口、多个复位点。
- 移动类本地指令（前进/后退/转向）静音整段回复。

## 五、表情脸 UI 与表情 MCP 工具（Phase 2c）

- 移植 `face_expression.h`（纯 LVGL 圆角矩形绘制的动画脸：耳朵/眼睛/虹膜/瞳孔/嘴，已做 LVGL 9.5 适配与半径写入一次性化）。
- `PetoiFaceDisplay` 子类：隐藏系统 UI 全屏显示表情，`SetEmotion` 把 19 种情绪名映射为 look/blink/mouth 组合。
- 上游 seam：`display.h` 加 6 个空 virtual；`audio_service` 加 `on_audio_output` 回调（说话时嘴部随音量动画）。
- MCP 工具 `self.expression.look / blink / set_mouth_width / set_mouth_style`。

## 六、唤醒词「Hi Jason」

- 确认 `wn9s_hijason`（Hi, Jason）模型已内置在 ESP-SR 组件，无需外部模型文件。
- 构建时通过 `--wake-word wn9s_hijason` 指定，启动日志确认 `Wake word(wn9s_hijason)`。

## 七、语言方案调整（改为编译选项控制）

- 曾实现运行时 GeoIP 语言切换（zh-CN / en-US）。
- 因 C3 屏幕字体为拉丁字体无法渲染中文，且用户要求文本/语音全英文，最终撤销运行时切换，改由编译选项 `--language` 控制（默认 `en-US`）。
- 残留中文仅存在于输入匹配（本地指令语法、误导回复词表）与未编译的 Quaddle 分支，不影响任何输出。

## 八、开发环境搭建

- 安装 ESP-IDF `release/v6.1`（clone + submodule，经 socks5 代理）。
- `install.sh esp32c3` 安装 riscv32 工具链；`brew install cmake`（4.5.0）。

## 九、问题排查与修复

1. cmake 缺失 → `brew install cmake`。
2. idf.py 解释器路径不匹配（python vs python3）→ 用 `python3 .../idf.py` 烧录。
3. `FaceExpression` 不完整类型 → 头文件直接 include。
4. `CONNECT_TO_HOTSPOT` 宏前缀碰撞 → 修正为完整键名。
5. `L_SOUND` 宏缺 `OGG_` 前缀 → 宏内拼 `OGG_##KEY`。
6. **工具列表内存崩溃（std::bad_alloc）**：新增的机器人 + 表情工具使 `tools/list` 响应 JSON 过大，C3 在 MQTT-TLS 握手后堆碎片化导致分配失败。修复：精简机器人工具描述 + 工具列表分页从 8000 降到 2000 字节。

## 十、测试与验证结果

编译命令（本机 ESP-IDF v6.1 + cmake 4.5.0）：

```text
python3 scripts/build.py lckfb/lichuang-c3-robot --name lichuang-c3-robot \
  --language en-US --wake-word wn9s_hijason
```

烧录到 ESP32-C3 开发板（/dev/cu.usbmodem2101）后串口验证：

```text
Board: SKU=lichuang-c3-robot
SecondUart: Robot control UART1 initialized on pins TX:21, RX:20
[UART] >> XA\x15kup                         （开机起立）
PetoiFaceDisplay: Expression face enabled (240x280)
MCP: 14 tools registered (9 visible + 5 user-only)
MQTT: Connected to endpoint
Wake word(wn9s_hijason)
Startup wake word hint: Say "Hi,Jason" to wake me.
无 abort、无崩溃循环、输出无中文
```

未执行（需硬件/服务器环境）：

- 真实机器人（Bittle/NyBoard）动作联调。
- 与 xiaozhi.me 服务器的完整对话验证（语音识别、TTS、LLM 调工具全链路）。

## 十一、文档与工程规范

- 板卡 README：`main/boards/lckfb/lichuang-c3-robot/README.md`（硬件、串口协议、唤醒词、构建说明）。
- 改动范围：板卡目录 + 少量共享 seam（`board.h`、`display.h`、`audio_service.h/.cc`、`mcp_server.cc`、`application.cc/h`、`CMakeLists.txt`、`Kconfig.projbuild`），未触碰其他板卡目录。

## 十二、当前状态与后续安排

### 已完成

- 机器人控制核心（板卡 + SecondUart + MCP 工具 + 本地指令 + 开机起立）。
- 回复抑制、表情脸 UI + 表情 MCP 工具。
- Hi Jason 唤醒词、16MB flash。
- 语言改为编译选项控制（英文）。

### 后续需跟进

1. `P0`：在真实机器人硬件上验证串口指令（kup/ksit/kwkF 等）与动作执行。
2. `P0`：配网后与服务器联调，验证「语音指令 → 机器人动作 → 回复抑制」全链路。
3. `P1`：将改动组织成 commit（当前 `bittle` 分支工作区未提交）。
4. `P2`：如需要屏幕显示中文，需为 C3 引入 CJK 字体（内存占用较大，需评估）。
5. `P2`：补齐 0930 中未移植的 `self.expression.set_quiet_after_speech` 工具、文本内嵌表情微指令（`HandleExpressionCommandFromText`）。

当前 `xiaozhi-esp32` 仓库所有修改仍位于 `bittle` 分支且尚未提交，应继续保留并避免被重置或覆盖。
