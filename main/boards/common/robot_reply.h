#ifndef ROBOT_REPLY_H
#define ROBOT_REPLY_H

// 机器人回复抑制的文本谓词与清洗工具（移植自 xiaozhi0930）。
// header-only，供 application.cc 与机器人控制器共用。

#include <esp_log.h>
#include <initializer_list>
#include <string>

namespace robot_reply {

inline void TrimTtsWhitespace(std::string& text) {
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        text.clear();
        return;
    }
    const size_t end = text.find_last_not_of(" \t\r\n");
    text = text.substr(start, end - start + 1);
}

inline bool IsToolInvocationText(const std::string& text) {
    std::string trimmed = text;
    TrimTtsWhitespace(trimmed);
    return !trimmed.empty() && trimmed.front() == '%';
}

inline bool IsRobotToolInvocationText(const std::string& text) {
    return text.find("self.robot.send_command") != std::string::npos;
}

inline void StripMcpLeaksFromTtsText(std::string& text) {
    while (true) {
        const size_t pos = text.find("% self.robot.");
        if (pos == std::string::npos) {
            break;
        }

        size_t end_pos = text.find("...", pos);
        if (end_pos == std::string::npos) {
            end_pos = text.find('\n', pos);
            if (end_pos == std::string::npos) {
                end_pos = text.length();
            }
        } else {
            end_pos += 3;
        }

        ESP_LOGI("RobotReply", "Removed MCP code leak from TTS text: %.*s",
                 static_cast<int>(end_pos - pos), text.c_str() + pos);
        text.erase(pos, end_pos - pos);
    }

    std::string sanitized;
    size_t line_start = 0;
    while (line_start <= text.size()) {
        size_t line_end = text.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = text.size();
        }

        std::string line = text.substr(line_start, line_end - line_start);
        TrimTtsWhitespace(line);
        if (!line.empty() && !IsToolInvocationText(line)) {
            if (!sanitized.empty()) {
                sanitized += '\n';
            }
            sanitized += line;
        }

        if (line_end == text.size()) {
            break;
        }
        line_start = line_end + 1;
    }

    text = std::move(sanitized);
    TrimTtsWhitespace(text);
}

inline bool StripSpokenMarkdownFromTtsText(std::string& text) {
    bool changed = false;
    std::string sanitized;
    sanitized.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '*' || ch == '`') {
            changed = true;
            continue;
        }

        if (ch == '_' && i + 1 < text.size() && text[i + 1] == '_') {
            changed = true;
            ++i;
            continue;
        }

        sanitized.push_back(ch);
    }

    if (changed) {
        text = std::move(sanitized);
        TrimTtsWhitespace(text);
    }
    return changed;
}

inline bool ContainsAny(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* word : words) {
        if (text.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline std::string ToLowerAscii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

// 误导性回复：模型声称"我不能动/网络不稳定"等，而不是执行动作。
// 仅应在机器人回复窗口或本地序列激活时生效（词表较宽）。
inline bool IsMisleadingLocalCommandReply(const std::string& text) {
    const std::string lower_text = ToLowerAscii(text);
    return ContainsAny(lower_text, {
        "as an ai",
        "as a language model",
        "can't actually move",
        "cannot actually move",
        "can't move",
        "cannot move",
        "can't physically",
        "cannot physically",
        "can't control",
        "cannot control",
        "can't perform",
        "unable to move",
        "unable to perform",
        "i don't have the ability",
        "i do not have the ability",
        "movement tools",
        "tools aren't available",
        "tools are not available",
        "no tools available",
        "don't have access to",
        "do not have access to",
        "not available here",
        "no forward",
        "no backward",
        "didn't respond",
        "did not respond",
        "didn't work",
        "did not work",
        "network unstable",
        "network is unstable",
        "network is not stable",
        "network disconnected",
        "network is disconnected",
        "network isn't connected",
        "network is not connected",
        "can't connect to the network",
        "cannot connect to the network",
        "legs didn't cooperate",
        "legs did not cooperate",
        "body's still not cooperating",
        "body is still not cooperating",
        "body isn't cooperating",
        "body is not cooperating",
        "body didn't cooperate",
        "body did not cooperate",
        "my body's still not cooperating",
        "my body is still not cooperating",
        "my body isn't cooperating",
        "my body is not cooperating",
        "my body didn't cooperate",
        "my body did not cooperate",
        "still no luck",
        "motor connection seems to be down",
        "motor connection is down",
        "motor connection seems down",
        "motor connection down",
        "motors are having",
        "motors having",
        "motor is having",
        "motor having",
        "connection trouble",
        "connection problems",
        "motor trouble",
        "motor problems",
        "motor issue",
        "motor issues",
        "can't do the",
        "cannot do the",
        "try again later",
        "try it again later",
        "没反应",
        "没有反应",
        "无响应",
        "没响应",
        "站不起来",
        "动不了",
        "没法动",
        "没法执行",
        "不能动",
        "无法执行",
        "不能执行",
        "执行不了",
        "动作失败",
        "网络不稳定",
        "网络不太稳定",
        "网络连不上",
        "网络没连上",
        "网络连接不上",
        "网络断开",
        "等会儿再试",
        "恢复了我再试"
    });
}

// 工具调用后的"模型放弃了、要结束对话"的收尾语。
inline bool IsUnhelpfulToolFollowupReply(const std::string& text) {
    const std::string lower_text = ToLowerAscii(text);
    return ContainsAny(lower_text, {
        "things have gone quiet",
        "seems like things",
        "head off",
        "talk to you later"
    });
}

}  // namespace robot_reply

#endif  // ROBOT_REPLY_H
