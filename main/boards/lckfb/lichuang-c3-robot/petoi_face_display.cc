#include "petoi_face_display.h"

#include <esp_log.h>

#include "face_expression.h"

#define TAG "PetoiFaceDisplay"

PetoiFaceDisplay::~PetoiFaceDisplay() {
    // 在基类删除 LVGL 对象前先释放脸组件
    DisplayLockGuard lock(this);
    face_.reset();
}

void PetoiFaceDisplay::HideChrome() {
    // 隐藏系统 UI 的各个顶层对象（成员来自 LcdDisplay/LvglDisplay）
    if (top_bar_) lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
    if (status_bar_) lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    if (bottom_bar_) lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    if (emoji_box_) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_) lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    if (side_bar_) lv_obj_add_flag(side_bar_, LV_OBJ_FLAG_HIDDEN);
}

void PetoiFaceDisplay::SetupUI() {
    if (IsSetupUICalled() || face_) {
        return;
    }
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    HideChrome();
    SetHideSubtitle(true);

    face_ = std::make_unique<FaceExpression>(lv_screen_active());
    auto config = FaceExpression::MakeDefaultConfig(width_, height_);
    face_->Show(config);
    ESP_LOGI(TAG, "Expression face enabled (%dx%d)", width_, height_);
}

bool PetoiFaceDisplay::SetEmotion(const char* emotion) {
    if (!face_) {
        return SpiLcdDisplay::SetEmotion(emotion);
    }

    DisplayLockGuard lock(this);
    const std::string e = emotion != nullptr ? emotion : "";
    if (e == "happy" || e == "laughing" || e == "funny" || e == "loving" ||
        e == "relaxed" || e == "delicious") {
        face_->Look("center");
        face_->Blink(2);
    } else if (e == "confident" || e == "cool") {
        face_->Look("up");
        face_->Blink(1);
    } else if (e == "sad" || e == "crying") {
        face_->Look("down");
    } else if (e == "angry") {
        face_->Look("center");
        face_->Blink(2);
    } else if (e == "surprised" || e == "shocked") {
        face_->Look("center");
        face_->Blink(1);
    } else if (e == "thinking" || e == "confused") {
        face_->Look("up_left");
    } else if (e == "winking") {
        face_->Blink(1);
    } else if (e == "sleepy") {
        face_->Look("center");
        face_->Blink(1);
    } else if (e == "embarrassed" || e == "silly" || e == "kissy") {
        face_->Look("down_left");
        face_->Blink(1);
    } else {
        // neutral / idle / robot_2 / warning / 未知：回到默认
        face_->Look("center");
        face_->Blink(1);
    }
    return true;
}

void PetoiFaceDisplay::SetExpressionState(bool recording, bool speaking) {
    if (!face_) return;
    DisplayLockGuard lock(this);
    face_->SetState(recording, speaking);
}

void PetoiFaceDisplay::SetExpressionVoiceActive(bool active) {
    if (!face_) return;
    face_->SetVoiceActive(active);
}

void PetoiFaceDisplay::SetExpressionLook(const std::string& direction) {
    if (!face_) return;
    DisplayLockGuard lock(this);
    face_->Look(direction);
}

void PetoiFaceDisplay::TriggerExpressionBlink(int times) {
    if (!face_) return;
    DisplayLockGuard lock(this);
    face_->Blink(times);
}
