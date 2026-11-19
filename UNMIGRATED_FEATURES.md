# xiaozhi0930 中未迁移到 2.6.0 的功能清单

> 本清单记录旧定制 fork `xiaozhi0930`（基于 xiaozhi-esp32 ~2.0.4）中存在、但在移植到新上游 `xiaozhi-esp32`（v2.6.0）时**未迁移**或**迁移后撤销**的功能，供后续需要时补齐参考。

---

## 一、回复抑制（Phase 2e，部分未迁移）

以下为 0930 中更复杂的回复抑制逻辑，因 2.6.0 的 MCP 结果无"延迟返回"机制、以及目标控制器立即返回 `ok`，暂未移植：

| 功能 | 0930 位置 | 说明 |
|------|-----------|------|
| 3s 说话空闲看门狗 | `application.cc:1032-1067` | Speaking 状态 3s 无音频输出 → 强制恢复聆听，防止"全部被抑制但一直卡在 Speaking" |
| 每轮工具调用配额 | `BeginRobotToolCall` `:2148-2162` | 每轮 MCP 工具调用上限 1 次（多动作请求 4 次），防止重复动作 |
| premature-sequence-summary 抑制 | `:683-692` | 多动作序列进行中，静音模型提前发来的总结 |
| deferred MCP 结果 | `mcp_server.cc:390-572` | 工具结果延迟到 UART 动作完成令牌返回后才回给云端 |
| 会话退出请求 | `IsConversationExitRequest` `:1388-1395` | 识别"再见/退出对话"并关闭音频通道 |

> 已迁移：误导性回复词表、hold 音频缓冲、8s 机器人窗口、移动指令静默、工具泄漏行检测。

---

## 二、表情相关（Phase 2c，部分未迁移）

| 功能 | 0930 位置 | 说明 |
|------|-----------|------|
| `self.expression.set_quiet_after_speech` 工具 | `mcp_server.cc:224-234` | 说话结束后嘴收窄到 5px 的持久状态，需 Application 层追踪"说完后" |
| `HandleExpressionCommandFromText` | `application.cc:2533-2614` | 从 TTS 文本解析表情微指令（看/眨眼/闭嘴/张大嘴/缩小嘴/恢复嘴） |

> 已迁移：`self.expression.look / blink / set_mouth_width / set_mouth_style`、19 种情绪名 → 表情映射。

---

## 三、运行时语言（Phase 2d，已实现后按需求撤销）

用户要求"文本和语音全英文、编译选项控制"，故撤销了运行时语言切换：

| 功能 | 状态 |
|------|------|
| 运行时 GeoIP 语言检测（zh-CN / en-US 自动切换） | 已撤销 |
| `language_selection.h` 编译期 38 语言选择 | 未迁移（2.6.0 用 Kconfig `--language`） |
| `language_sounds_zh_cn.h`（16 个中文提示音嵌入） | 已撤销 |
| `esp_wake_word` 按语言自动选择唤醒词模型（nihaoxiaozhi vs hijason） | 未迁移 |

> 原因：C3 屏幕字体为拉丁字体（`font_noto_sans_basic_16_4`）无法渲染中文；改为 `--language` 编译选项控制。

---

## 四、设备状态事件（未迁移）

| 功能 | 0930 位置 | 说明 |
|------|-----------|------|
| `device_state_event.h/.cc` | `DeviceStateEventManager` | ESP 事件包装 + 回调注册；0930 中**无任何订阅者**，纯脚手架，无实际用途 |

---

## 五、IoT（未迁移）

| 功能 | 0930 位置 | 说明 |
|------|-----------|------|
| `main/iot/`（thing / thing_manager / speaker / screen） | `iot/*` | MCP 协议下 `CreateThing()` 返回 `nullptr`，实际未使用 |

---

## 六、音频栈（未迁移，C3 不适用或存在隐患）

| 功能 | 说明 |
|------|------|
| `afe_wake_word.*`（AFE 唤醒词） | S3/P4/S31 专用，C3 不支持，0930 中也未编译 |
| `afe_audio_processor.*`（AFE 音频处理器） | 同上 |
| `main/audio_codecs/`（重复编解码头文件） | 与 `main/audio/codecs/` 重复且结构不一致，存在 ODR/sizeof 隐患，移植时已改用正规 `audio/codecs/` |

---

## 七、OTA 禁用（未迁移）

| 功能 | 0930 位置 | 说明 |
|------|-----------|------|
| `CONFIG_DISABLE_DEVICE_FIRMWARE_OTA` | `Kconfig.projbuild`、`ota.cc:251-258` | 产品决策：默认禁用设备自动固件下载/启动，仍保留 OTA 检查 API |

---

## 八、其他（未迁移）

| 功能 | 说明 |
|------|------|
| `ExtractRobotCommands`（TTS 文本内嵌命令提取） | 0930 中已是空 stub（`application.cc:2164-2172`），本地命令改走 STT 语法 |
| Quaddle 机器人型号词汇表 | `robot_model.h` 的 `ROBOT_MODEL_QUADDLE=0`，Quaddle 分支代码保留在 `#if` 中未编译，未启用 |

---

## 补充说明

- **未迁移 ≠ 丢失**：上述功能均可在 `xiaozhi0930` 仓库中找到完整实现，需要时可按本清单位置逐一找回并移植。
- **优先级建议**（如后续要补）：
  1. `P1` 3s 说话空闲看门狗（配合已迁移的 hold 缓冲，防止卡在 Speaking）。
  2. `P1` `self.expression.set_quiet_after_speech`（补齐表情工具第 5 个）。
  3. `P2` deferred MCP 结果 + 每轮配额（需先引入 UART 完成令牌等待与延迟返回机制）。
  4. `P2` `HandleExpressionCommandFromText`（表情微指令，需适配 2.6.0 的 sentence_start 文本链路）。
