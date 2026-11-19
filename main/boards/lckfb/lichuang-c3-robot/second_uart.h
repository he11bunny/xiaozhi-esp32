#ifndef SECOND_UART_H
#define SECOND_UART_H

#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <mutex>

// 第二串口配置 - ESP32C3 只支持 UART0 和 UART1
// 使用 UART1，映射到 U0 默认引脚 GPIO21/20，释放 GPIO18/19 用于 USB 通信
#define SECOND_UART_NUM         UART_NUM_1
#define SECOND_UART_TX_PIN      21       // U0_TXD (原 GPIO19 改为 GPIO21，避免占用 USB D+)
#define SECOND_UART_RX_PIN      20       // U0_RXD (原 GPIO18 改为 GPIO20，避免占用 USB D-)

// 机器人控制串口：BiBoard V1 的 Grove UART2（OpenCat Serial2，
// moduleManager.h: read_serial()）。115200，每帧 = 动作串 + '\n'。
// 狗端解析：首字符作为 token，其余作为参数，例如 "kup" -> k + "up" -> kup。
// 注意：语音模块口是 Serial1 @9600（voice.h: read_voice()），与本口不同。
#define SECOND_UART_BAUD_RATE   115200
#define ROBOT_UART_FRAME_PREFIX ""

#define SECOND_UART_BUF_SIZE    1024

// 临时诊断：打印 UART1 RX 收到的字节（验证 C3 RX20 <- 狗 UART2 TX GPIO10）
#define ROBOT_UART_RX_DEBUG 1

class SecondUart {
private:
    static SecondUart* instance_;
    bool initialized_;
    std::string last_command_;
    int64_t last_command_time_us_ = 0;
    std::mutex send_mutex_;

    SecondUart() : initialized_(false) {}

    static void LogUartTxPayload(const char* data, size_t length) {
        printf("[UART] >> (%u bytes) ", static_cast<unsigned>(length));
        for (size_t i = 0; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(data[i]);
            if (c >= 32 && c < 127 && c != '\\') {
                putchar(static_cast<char>(c));
            } else if (c == '\n') {
                fputs("\\n", stdout);
            } else if (c == '\r') {
                fputs("\\r", stdout);
            } else if (c == '\t') {
                fputs("\\t", stdout);
            } else {
                printf("\\x%02X", c);
            }
        }
        putchar('\n');
    }

#if ROBOT_UART_RX_DEBUG
    static void RxDebugTask(void* arg) {
        (void)arg;
        uint8_t buf[256];
        while (true) {
            int n = uart_read_bytes(SECOND_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(200));
            if (n > 0) {
                printf("[ROBOT UART RX] (%d bytes): ", n);
                for (int i = 0; i < n; ++i) {
                    const unsigned char c = buf[i];
                    if (c >= 32 && c < 127 && c != '\\') {
                        putchar(static_cast<char>(c));
                    } else if (c == '\n') {
                        fputs("\\n", stdout);
                    } else if (c == '\r') {
                        fputs("\\r", stdout);
                    } else {
                        printf("\\x%02X", c);
                    }
                }
                putchar('\n');
            }
        }
    }
#endif

public:
    static SecondUart& GetInstance() {
        if (instance_ == nullptr) {
            instance_ = new SecondUart();
        }
        return *instance_;
    }

    // 初始化第二串口
    esp_err_t Initialize() {
        if (initialized_) {
            return ESP_OK;
        }

        // 先卸载可能已存在的 UART 驱动
        uart_driver_delete(SECOND_UART_NUM);

        uart_config_t uart_config = {
            .baud_rate = SECOND_UART_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t ret = uart_param_config(SECOND_UART_NUM, &uart_config);
        if (ret != ESP_OK) {
            ESP_LOGE("SecondUart", "Failed to configure UART1: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = uart_driver_install(SECOND_UART_NUM, SECOND_UART_BUF_SIZE, SECOND_UART_BUF_SIZE, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGE("SecondUart", "Failed to install UART1 driver: %s", esp_err_to_name(ret));
            uart_driver_delete(SECOND_UART_NUM);
            return ret;
        }

        ret = uart_set_pin(SECOND_UART_NUM, SECOND_UART_TX_PIN, SECOND_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (ret != ESP_OK) {
            ESP_LOGE("SecondUart", "Failed to set UART1 pins: %s", esp_err_to_name(ret));
            uart_driver_delete(SECOND_UART_NUM);
            return ret;
        }

        uart_flush(SECOND_UART_NUM);

#if ROBOT_UART_RX_DEBUG
        xTaskCreate(RxDebugTask, "robot_uart_rx", 3072, this, 2, nullptr);
#endif

        initialized_ = true;
        ESP_LOGI("SecondUart", "Robot control UART1 initialized on pins TX:%d(U0_TXD), RX:%d(U0_RXD) @%d - GPIO18/19 freed for USB", SECOND_UART_TX_PIN, SECOND_UART_RX_PIN, SECOND_UART_BAUD_RATE);
        return ESP_OK;
    }

    // 向第二串口发送原始数据（带相同命令去重）
    void SendData(const char* data, size_t length) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (!initialized_) {
            ESP_LOGW("SecondUart", "UART not initialized, attempting to initialize...");
            if (Initialize() != ESP_OK) {
                ESP_LOGE("SecondUart", "Failed to initialize UART");
                return;
            }
        }

        // BeginConversationTurn() 会清空去重标记，使新一轮里相同的命令仍然有效；
        // 在同一轮里则抑制云端重试导致的重复发送。
        const int64_t now_us = esp_timer_get_time();
        if (last_command_.size() == length &&
            memcmp(last_command_.data(), data, length) == 0 &&
            last_command_time_us_ > 0) {
            ESP_LOGI("SecondUart", "Skip duplicate command: %.*s", (int)length, data);
            return;
        }

        LogUartTxPayload(data, length);

        int written = uart_write_bytes(SECOND_UART_NUM, data, length);
        if (written != length) {
            ESP_LOGW("SecondUart", "Only wrote %d bytes out of %d", written, length);
        } else {
            ESP_LOGI("SecondUart", "Sent robot command: %.*s (written %d bytes)", (int)length, data, written);
        }

        last_command_.assign(data, length);
        last_command_time_us_ = now_us;
    }

    // 向第二串口发送字符串（UART2 普通命令帧：动作串 + 换行）
    void SendString(const std::string& str) {
        std::string frame = ROBOT_UART_FRAME_PREFIX + str + "\n";
        SendData(frame.c_str(), frame.length());
    }

    // 新一轮对话开始：允许重复命令再次发送（去重仅针对同一轮内的云端/本地重复投递）
    void BeginConversationTurn() {
        std::lock_guard<std::mutex> lock(send_mutex_);
        last_command_.clear();
        last_command_time_us_ = 0;
    }

    void FlushInput() {
        if (!initialized_) {
            ESP_LOGW("SecondUart", "UART not initialized, attempting to initialize...");
            if (Initialize() != ESP_OK) {
                ESP_LOGE("SecondUart", "Failed to initialize UART");
                return;
            }
        }
        uart_flush_input(SECOND_UART_NUM);
    }

    // 阻塞等待机器人回传的完成令牌（单字符），可被 cancel 原子标志中断
    bool WaitForToken(char token, uint32_t timeout_ms,
                      const std::atomic<bool>* cancel = nullptr) {
        if (!initialized_) {
            ESP_LOGW("SecondUart", "UART not initialized, attempting to initialize...");
            if (Initialize() != ESP_OK) {
                ESP_LOGE("SecondUart", "Failed to initialize UART");
                return false;
            }
        }

        if (token == '\0') {
            return false;
        }

        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
        uint8_t byte = 0;
        while (static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) {
            if (cancel != nullptr && cancel->load()) {
                ESP_LOGW("SecondUart", "Completion-token wait interrupted by a newer command");
                return false;
            }
            const TickType_t remaining = deadline - xTaskGetTickCount();
            const TickType_t poll_ticks = pdMS_TO_TICKS(100);
            const int len = uart_read_bytes(
                SECOND_UART_NUM, &byte, 1, std::min(remaining, poll_ticks));
            if (len > 0) {
                const char ch = static_cast<char>(byte);
                printf("[UART] << %c\n", (ch >= 32 && ch < 127) ? ch : '.');
                if (ch == token) {
                    ESP_LOGI("SecondUart", "Received command token: %c", token);
                    return true;
                }
            }
        }

        ESP_LOGW("SecondUart", "No completion token '%c' within %u ms", token,
                 static_cast<unsigned>(timeout_ms));
        return false;
    }

    // 反初始化（清理资源）
    void Deinitialize() {
        if (initialized_) {
            uart_driver_delete(SECOND_UART_NUM);
            initialized_ = false;
            ESP_LOGI("SecondUart", "Second UART deinitialized");
        }
    }

    ~SecondUart() {
        Deinitialize();
    }
};

#endif // SECOND_UART_H
