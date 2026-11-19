#ifndef PETOI_ROBOT_CONTROLLER_H
#define PETOI_ROBOT_CONTROLLER_H

#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Petoi Bittle 机器人控制器：
// 通过 SecondUart（UART1）发送动作指令。MCP 工具只入队，专用任务负责发送，
// 避免阻塞 2.6.0 的 Application 主任务。
class PetoiRobotController {
public:
    PetoiRobotController();
    ~PetoiRobotController();

    // 非阻塞：把一条动作指令入队，由 RobotActionTask 发送。返回 false 表示队列已满。
    bool SendCommand(const std::string& command);

    // 本地语音→指令：解析用户转写文本，命中动作则入队发送（LLM/MCP 之外的本地兜底）。
    void HandleUserTranscript(const std::string& text);

private:
    static constexpr int kMaxCommandLength = 128;

    struct CommandMsg {
        char command[kMaxCommandLength];
    };

    static void RobotActionTask(void* arg);
    void RegisterMcpTools();

    QueueHandle_t command_queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
};

#endif  // PETOI_ROBOT_CONTROLLER_H
