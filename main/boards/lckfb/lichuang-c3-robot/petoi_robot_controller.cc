#include "petoi_robot_controller.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <esp_log.h>

#include "application.h"
#include "mcp_server.h"
#include "robot_model.h"
#include "second_uart.h"

#define TAG "PetoiRobotController"

#if ROBOT_MODEL_QUADDLE
// Quaddle：技能名与下游 progmem 表一致；技能码带 k 前缀。
#define MCP_ROBOT_SEND_COMMAND_DESC \
    "机器狗物理动作指令(非屏幕表情)。参数仅含代码，勿含中文。\n" \
    "Physical robot dog motion tool. Call once for one requested action; argument is command code only. After success, never retry and speak immediately.\n" \
    "【铁律】机器狗身体动作必调此器！勿用expression! 技能发串须带k前缀(如kwkF、ksit)。\n" \
    "Gaits: kwkF forward, kbkF backward, ktrF run, kvtL turn left, kvtR turn right. Basic: khi greet, ksit sit, kup stand, kstr stretch.\n" \
    "【步态】kwkF前进 kbkF后退 ktrF跑 kvtL左转 kvtR右转\n" \
    "  参≤200为步数(默认3)，>200为毫秒。转向参数=度数(默90)。\n" \
    "【基本】khi打招呼 ksit坐 kup起 kstr伸懒腰\n" \
    "【技能】kbf后翻 kff前翻 kslide滑行 kpinWheel大风车\n" \
    "【互动】khsk握手 kscrh抓 kpee\n" \
    "【关节】m<号> <角> 头0左手2右手3 负=前正=后\n" \
    "【范例】坐下→ksit 跑→ktrF 3 举左手→m2 -30"
#else
#define MCP_ROBOT_SEND_COMMAND_DESC \
    "Control the robot dog body (not screen). Argument is command code only; call once per action. " \
    "Gaits: kwkF fwd, kbk back, kvtL/kvtR turn(deg), ktrF run. " \
    "Basic: kup stand, d rest, ksit sit, kstr stretch, kpu push-up, kpd play-dead, khi greet, knd nod, kwh shake-head. " \
    "Head/joints: m0 head-turn, m8/m9 arms (angle, negative=forward). " \
    "Examples: sit->ksit, run 3->ktrF 3, turn left 90->kvtL 90."

#endif

PetoiRobotController::PetoiRobotController() {
    command_queue_ = xQueueCreate(16, sizeof(CommandMsg));
    RegisterMcpTools();
}

PetoiRobotController::~PetoiRobotController() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
    }
    if (command_queue_ != nullptr) {
        vQueueDelete(command_queue_);
    }
}

void PetoiRobotController::RobotActionTask(void* arg) {
    auto* controller = static_cast<PetoiRobotController*>(arg);
    CommandMsg msg;
    while (true) {
        if (xQueueReceive(controller->command_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            // 保证以 NUL 结尾
            msg.command[kMaxCommandLength - 1] = '\0';
            const std::string command(msg.command);
            if (!command.empty()) {
                // 支持逗号分隔的多动作序列（本地语法会产生 "kwkF 3,kvtL 90"）
                size_t start = 0;
                while (start <= command.size()) {
                    const size_t comma = command.find(',', start);
                    const size_t end = (comma == std::string::npos) ? command.size() : comma;
                    std::string sub = command.substr(start, end - start);
                    const size_t first = sub.find_first_not_of(" \t");
                    const size_t last = sub.find_last_not_of(" \t");
                    if (first != std::string::npos) {
                        sub = sub.substr(first, last - first + 1);
                        SecondUart::GetInstance().SendString(sub);
                    }
                    if (comma == std::string::npos) {
                        break;
                    }
                    start = comma + 1;
                }
            }
        }
    }
}

bool PetoiRobotController::SendCommand(const std::string& command) {
    if (command_queue_ == nullptr) {
        return false;
    }
    if (task_handle_ == nullptr) {
        xTaskCreate(RobotActionTask, "robot_action", 3072, this, 2, &task_handle_);
    }

    CommandMsg msg;
    const size_t len = std::min(command.size(), static_cast<size_t>(kMaxCommandLength - 1));
    std::memcpy(msg.command, command.data(), len);
    msg.command[len] = '\0';

    return xQueueSend(command_queue_, &msg, 0) == pdTRUE;
}

namespace {

bool ContainsAny(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* word : words) {
        if (text.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string ToLowerAscii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

int ExtractFirstNumber(const std::string& text, int default_value) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] >= '0' && text[i] <= '9') {
            int value = 0;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                value = value * 10 + (text[i] - '0');
                ++i;
            }
            return value;
        }
    }

    struct WordNumber {
        const char* text;
        int value;
    };
    static constexpr WordNumber numbers[] = {
        {"ten", 10}, {"nine", 9}, {"eight", 8}, {"seven", 7}, {"six", 6},
        {"five", 5}, {"four", 4}, {"three", 3}, {"two", 2}, {"one", 1},
        {"十", 10}, {"九", 9}, {"八", 8}, {"七", 7}, {"六", 6},
        {"五", 5}, {"四", 4}, {"三", 3}, {"两", 2}, {"二", 2}, {"一", 1}
    };
    const std::string lower_text = ToLowerAscii(text);
    for (const auto& number : numbers) {
        if (lower_text.find(number.text) != std::string::npos) {
            return number.value;
        }
    }
    return default_value;
}

int ExtractActionNumber(const std::string& text, size_t pos, int default_value) {
    if (pos >= text.size()) {
        return default_value;
    }

    std::string action_clause = text.substr(pos);
    const std::string lower_clause = ToLowerAscii(action_clause);
    size_t clause_end = std::string::npos;
    const auto keep_earliest_boundary = [&clause_end](size_t boundary) {
        if (boundary != std::string::npos &&
            (clause_end == std::string::npos || boundary < clause_end)) {
            clause_end = boundary;
        }
    };

    for (const char* boundary : {
             "然后", "接着", "随后", "之后", "并且", "最后",
             "再向", "再往", "再左", "再右", "再打", "再做", "再站",
             "再坐", "再伸", "再点", "再摇", "再退", "再前", "再后",
             "再起", "再趴",
             "，", "。", "；", "、", ",", ".", ";"
         }) {
        keep_earliest_boundary(action_clause.find(boundary));
    }
    for (const char* boundary : {
             " and then ", " then ", " followed by ", " after that ",
             " next ", " and "
         }) {
        keep_earliest_boundary(lower_clause.find(boundary));
    }
    if (clause_end != std::string::npos) {
        action_clause.resize(clause_end);
    }

    return ExtractFirstNumber(action_clause, default_value);
}

size_t FindFirstPhrasePos(const std::string& text, std::initializer_list<const char*> phrases) {
    size_t best = std::string::npos;
    for (const char* phrase : phrases) {
        const size_t pos = text.find(phrase);
        if (pos != std::string::npos && pos < best) {
            best = pos;
        }
    }
    return best;
}

void AddRobotActionMatch(std::vector<std::pair<size_t, std::string>>& matches,
                         size_t pos, const std::string& command) {
    if (pos == std::string::npos || command.empty()) {
        return;
    }
    matches.emplace_back(pos, command);
}

bool BuildRobotCommandFromUserText(const std::string& text, std::string& command) {
    command.clear();
    const std::string lower_text = ToLowerAscii(text);

    if (ContainsAny(text, {"天气", "故事", "笑话", "介绍", "你会", "什么", "怎么样", "在吗"}) ||
        ContainsAny(lower_text, {"weather", "story", "joke", "introduce", "what can you", "how are you"})) {
        return false;
    }

    std::vector<std::pair<size_t, std::string>> matches;
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"再见", "拜拜", "休息", "趴下", "待机"}),
                        "d");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"goodbye", "bye", "rest", "lie down", "lay down"}),
                        "d");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"打招呼", "打个招呼", "打一个招呼", "招手", "你好"}),
                        "khi");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"say hi", "wave", "greet"}),
                        "khi");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"坐下", "坐下来", "坐好"}),
                        "ksit");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"sit down", "sit"}),
                        "ksit");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"站起来", "起立", "起来"}),
                        "kup");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"stand up", "stand", "sand up"}),
                        "kup");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"前空翻", "前空", "起空翻", "前滚翻"}),
                        "kff");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"front flip", "forward flip"}),
                        "kff");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"后空翻", "后空", "后孔", "后滚翻"}),
                        "kbf");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"back flip", "backflip"}),
                        "kbf");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {
                            "俯卧撑", "俯卧撑动作", "卧撑"
                        }),
                        "kpu");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"push up", "push-up", "pushup"}),
                        "kpu");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"装死", "假死", "装死动作", "装饰", "装实", "装饰动作", "做个装饰"}),
                        "kpd");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"play dead", "pretend dead"}),
                        "kpd");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"大风车", "风车"}),
                        "kpinWheel");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"pinwheel"}),
                        "kpinWheel");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"握手"}),
                        "khsk");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"shake hand", "handshake"}),
                        "khsk");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {
                            "伸懒腰", "伸个懒腰", "伸一下懒腰", "伸一伸懒腰", "伸伸懒腰"
                        }),
                        "kstr");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"stretch"}),
                        "kstr");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"点头"}),
                        "knd");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"nod"}),
                        "knd");
    AddRobotActionMatch(matches, FindFirstPhrasePos(text, {"摇头", "歪头"}),
                        "kwh");
    AddRobotActionMatch(matches, FindFirstPhrasePos(lower_text, {"shake head"}),
                        "kwh");

    size_t pos = FindFirstPhrasePos(text, {"向左转", "左转"});
    if (pos == std::string::npos) {
        pos = FindFirstPhrasePos(lower_text, {"turn left", "left turn", "rotate left", "to the left"});
    }
    AddRobotActionMatch(matches, pos, "kvtL " + std::to_string(ExtractActionNumber(text, pos, 90)));

    pos = FindFirstPhrasePos(text, {"向右转", "右转"});
    if (pos == std::string::npos) {
        pos = FindFirstPhrasePos(lower_text, {"turn right", "right turn", "rotate right", "to the right"});
    }
    AddRobotActionMatch(matches, pos, "kvtR " + std::to_string(ExtractActionNumber(text, pos, 90)));

    pos = FindFirstPhrasePos(text, {"向前走", "往前走", "下前走", "下去走", "向前", "往前", "前走", "前进", "走五步", "走几步"});
    if (pos == std::string::npos) {
        pos = FindFirstPhrasePos(lower_text, {
            "go forward", "move forward", "walk forward", "step forward",
            "steps forward", "step ahead", "go ahead", "move ahead", "walk ahead"
        });
    }
    AddRobotActionMatch(matches, pos, "kwkF " + std::to_string(ExtractActionNumber(text, pos, 5)));

    pos = FindFirstPhrasePos(text, {"向后退", "往后退", "后退", "退后"});
    if (pos == std::string::npos) {
        pos = FindFirstPhrasePos(lower_text, {
            "go back", "move back", "walk back", "step back", "go backward",
            "move backward", "walk backward", "step backward", "steps backward", "backward"
        });
    }
    AddRobotActionMatch(matches, pos, "kbk " + std::to_string(ExtractActionNumber(text, pos, 5)));

    if (matches.empty()) {
        return false;
    }

    std::sort(matches.begin(), matches.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.first < rhs.first;
              });
    for (const auto& match : matches) {
        if (!command.empty()) {
            command += ",";
        }
        command += match.second;
    }

    ESP_LOGW(TAG, "Local robot command fallback: %s -> %s", text.c_str(), command.c_str());
    printf("[LOCAL] robot command from STT: %s\n", command.c_str());
    return true;
}

}  // namespace

void PetoiRobotController::HandleUserTranscript(const std::string& text) {
    // 新一轮对话：清空 UART 去重标记，允许重复指令再次发送
    SecondUart::GetInstance().BeginConversationTurn();

    std::string command;
    if (BuildRobotCommandFromUserText(text, command)) {
        // 移动类单动作（无逗号且 kwkF/kbk/kbkF/kvtL/kvtR 前缀）静音整段回复
        const bool movement_only =
            command.find(',') == std::string::npos &&
            (command.rfind("kwkF", 0) == 0 || command.rfind("kbkF", 0) == 0 ||
             command.rfind("kbk", 0) == 0 || command.rfind("kvtL", 0) == 0 ||
             command.rfind("kvtR", 0) == 0);
        Application::GetInstance().NotifyRobotCommandDispatched(command, movement_only);
        SendCommand(command);
    }
}

void PetoiRobotController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.robot.send_command",
        MCP_ROBOT_SEND_COMMAND_DESC,
        PropertyList({
            Property("text", kPropertyTypeString).SetMaxLength(128),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            const std::string text = properties["text"].value<std::string>();
            if (text.empty()) {
                return std::string("empty command");
            }
            // 同一轮里本地 STT 兜底已发过同一条命令时，依靠 UART 去重抑制云端 MCP 的重复投递。
            if (!SendCommand(text)) {
                return std::string("command queue is full; try again");
            }
            Application::GetInstance().NotifyRobotCommandDispatched(text);
            ESP_LOGI(TAG, "MCP tool executed: send_command(\"%s\")", text.c_str());
            return std::string("ok; action sent; do not retry");
        });
}
