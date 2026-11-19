#ifndef FACE_EXPRESSION_H
#define FACE_EXPRESSION_H

#include <esp_random.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "logo_face.h"

// 背景脸：内嵌的南瓜图片（240x280，黑色眼窝已由生成脚本画大），其上叠加可动白色眼珠。
struct FaceExpressionConfig {
    lv_color_t background_color = lv_color_black();

    // 左眼（白色眼珠）——眼窝由 gen_logo_face.py 画入背景，坐标按 240x280 屏幕换算
    int left_pupil_center_x = 72;
    int left_pupil_center_y = 107;
    int left_pupil_radius_x = 18;
    int left_pupil_radius_y = 18;
    lv_color_t left_highlight_color = lv_color_hex(0x3FA9FF);

    // 右眼（白色眼珠）
    int right_pupil_center_x = 172;
    int right_pupil_center_y = 107;
    int right_pupil_radius_x = 18;
    int right_pupil_radius_y = 18;
    lv_color_t right_highlight_color = lv_color_hex(0xFFFFFF);

    lv_color_t pupil_color = lv_color_hex(0xFFC107);  // 金黄色眼珠
    int highlight_radius = 14;
    int highlight_border_width = 0;  // 南瓜脸不使用高光环
    int eye_jitter_range = 20;       // 白眼球在黑色眼窝内最大偏移（像素）
    float focus_eye_scale = 0.85f;

    uint32_t animation_period_ms = 50;
};

// 单只眼睛的叠加对象：黑色瞳孔 + 高光环（高光为瞳孔子对象，随瞳孔移动）
struct EyeGroup {
    lv_obj_t* pupil = nullptr;
    lv_obj_t* highlight = nullptr;
};

class FaceExpression {
public:
    explicit FaceExpression(lv_obj_t* parent) : parent_(parent) { CreateObjects(); }

    ~FaceExpression() {
        if (timer_ != nullptr) {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }
        if (root_ != nullptr) {
            lv_obj_delete(root_);
            root_ = nullptr;
        }
    }

    static FaceExpressionConfig MakeDefaultConfig(int screen_width, int screen_height) {
        (void)screen_width;
        (void)screen_height;
        return FaceExpressionConfig();
    }

    void Show(const FaceExpressionConfig& config) {
        config_ = config;
        recording_ = false;
        speaking_ = false;
        voice_active_ = false;
        gaze_x_ = 0.0f;
        gaze_y_ = 0.0f;
        gaze_vx_ = 0.0f;
        gaze_vy_ = 0.0f;
        speaking_target_y_ = 0.0f;
        speaking_target_ticks_ = 0;
        idle_target_x_ = 0.0f;
        idle_target_y_ = 0.0f;
        idle_pause_ticks_ = 0;
        idle_need_new_target_ = true;
        manual_look_x_ = 0.0f;
        manual_look_y_ = 0.0f;
        manual_look_ticks_ = 0;
        eye_scale_ = 1.0f;
        target_eye_scale_ = 1.0f;
        blink_scale_ = 1.0f;
        blink_remaining_ = 0;
        blink_phase_ticks_ = 0;
        blink_closing_ = false;

        lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(root_, config_.background_color, 0);
        lv_obj_set_style_border_width(root_, 0, 0);
        lv_obj_set_style_radius(root_, 0, 0);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);

        ApplyColors();
        ApplyLayout();
        EnsureTimer();
    }

    void SetState(bool recording, bool speaking) {
        bool state_changed = (recording_ != recording) || (speaking_ != speaking);
        recording_ = recording;
        speaking_ = speaking;

        if (!recording_ && !speaking_) {
            voice_active_ = false;
        } else if (speaking_) {
            voice_active_ = false;
            speaking_target_ticks_ = 0;
        }
        if (state_changed) {
            ApplyLayout();
        }
    }

    void SetVoiceActive(bool active) {
        if (voice_active_ == active) {
            return;
        }
        voice_active_ = active;
    }

    void Look(const std::string& direction) {
        std::string normalized = direction;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char c) {
                           if (c >= 'A' && c <= 'Z') {
                               return static_cast<char>(c - 'A' + 'a');
                           }
                           return static_cast<char>(c);
                       });

        float target_x = 0.0f;
        float target_y = 0.0f;
        if (normalized == "up") {
            target_y = -0.95f;
        } else if (normalized == "down") {
            target_y = 0.95f;
        } else if (normalized == "left") {
            target_x = -0.95f;
        } else if (normalized == "right") {
            target_x = 0.95f;
        } else if (normalized == "up_left" || normalized == "left_up") {
            target_x = -0.72f;
            target_y = -0.72f;
        } else if (normalized == "up_right" || normalized == "right_up") {
            target_x = 0.72f;
            target_y = -0.72f;
        } else if (normalized == "down_left" || normalized == "left_down") {
            target_x = -0.72f;
            target_y = 0.72f;
        } else if (normalized == "down_right" || normalized == "right_down") {
            target_x = 0.72f;
            target_y = 0.72f;
        } else {
            target_x = 0.0f;
            target_y = 0.0f;
        }

        manual_look_x_ = target_x;
        manual_look_y_ = target_y;
        manual_look_ticks_ = 22;
        idle_pause_ticks_ = 0;
        idle_need_new_target_ = true;
    }

    void Blink(int times) {
        int blink_times = times;
        if (blink_times <= 0) {
            blink_times = RandomInRange(1, 2);
        }
        blink_times = std::max(1, std::min(2, blink_times));
        blink_remaining_ = blink_times;
        blink_closing_ = true;
        blink_phase_ticks_ = 4;
    }

private:
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* bg_image_ = nullptr;
    EyeGroup left_eye_;
    EyeGroup right_eye_;
    lv_timer_t* timer_ = nullptr;

    FaceExpressionConfig config_;
    bool recording_ = false;
    bool speaking_ = false;
    bool voice_active_ = false;
    float gaze_x_ = 0.0f;
    float gaze_y_ = 0.0f;
    float gaze_vx_ = 0.0f;
    float gaze_vy_ = 0.0f;
    float speaking_target_y_ = 0.0f;
    int speaking_target_ticks_ = 0;
    float idle_target_x_ = 0.0f;
    float idle_target_y_ = 0.0f;
    int idle_pause_ticks_ = 0;
    bool idle_need_new_target_ = true;
    float manual_look_x_ = 0.0f;
    float manual_look_y_ = 0.0f;
    int manual_look_ticks_ = 0;
    float eye_scale_ = 1.0f;
    float target_eye_scale_ = 1.0f;
    float blink_scale_ = 1.0f;
    int blink_remaining_ = 0;
    int blink_phase_ticks_ = 0;
    bool blink_closing_ = false;

    static int RandomInRange(int min_value, int max_value) {
        if (max_value <= min_value) {
            return min_value;
        }
        uint32_t value = esp_random();
        int span = max_value - min_value + 1;
        return min_value + static_cast<int>(value % static_cast<uint32_t>(span));
    }

    static float RandomFloat(float min_value, float max_value) {
        if (max_value <= min_value) {
            return min_value;
        }
        constexpr float inv_max = 1.0f / static_cast<float>(UINT32_MAX);
        float normalized = static_cast<float>(esp_random()) * inv_max;
        return min_value + normalized * (max_value - min_value);
    }

    static void TimerCallback(lv_timer_t* timer) {
        auto* self = static_cast<FaceExpression*>(lv_timer_get_user_data(timer));
        if (self != nullptr) {
            self->AnimateStep();
        }
    }

    void CreateObjects() {
        root_ = lv_obj_create(parent_);
        lv_obj_set_style_pad_all(root_, 0, 0);
        lv_obj_set_style_border_width(root_, 0, 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

        // 背景脸图片（全屏）
        bg_image_ = lv_image_create(root_);
        lv_image_set_src(bg_image_, &logo_face);
        lv_obj_set_pos(bg_image_, 0, 0);
        lv_obj_set_size(bg_image_, logo_face.header.w, logo_face.header.h);
        lv_obj_set_style_image_opa(bg_image_, LV_OPA_COVER, 0);

        left_eye_.pupil = lv_obj_create(root_);
        left_eye_.highlight = lv_obj_create(left_eye_.pupil);
        right_eye_.pupil = lv_obj_create(root_);
        right_eye_.highlight = lv_obj_create(right_eye_.pupil);

        for (lv_obj_t* obj : {bg_image_, left_eye_.pupil, left_eye_.highlight, right_eye_.pupil,
                              right_eye_.highlight}) {
            lv_obj_set_style_pad_all(obj, 0, 0);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    void EnsureTimer() {
        uint32_t period = std::max<uint32_t>(30, config_.animation_period_ms);
        if (timer_ == nullptr) {
            timer_ = lv_timer_create(TimerCallback, period, this);
            return;
        }
        lv_timer_set_period(timer_, period);
    }

    void ApplyColors() {
        ApplyEyeColors(left_eye_, config_.left_highlight_color);
        ApplyEyeColors(right_eye_, config_.right_highlight_color);
    }

    void ApplyEyeColors(EyeGroup& eye, lv_color_t highlight_color) {
        lv_obj_set_style_bg_color(eye.pupil, config_.pupil_color, 0);
        lv_obj_set_style_border_width(eye.pupil, 0, 0);
        lv_obj_set_style_radius(eye.pupil, LV_RADIUS_CIRCLE, 0);

        if (config_.highlight_border_width <= 0) {
            lv_obj_add_flag(eye.highlight, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_remove_flag(eye.highlight, LV_OBJ_FLAG_HIDDEN);

        // 高光用透明底 + 粗边框圆实现环形（左淡蓝「C」近似 / 右白圆环）
        lv_obj_set_style_bg_opa(eye.highlight, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(eye.highlight, config_.highlight_border_width, 0);
        lv_obj_set_style_border_color(eye.highlight, highlight_color, 0);
        lv_obj_set_style_radius(eye.highlight, LV_RADIUS_CIRCLE, 0);
    }

    void ApplyLayout() { ApplyEyeLayout(); }

    void ApplyEyeLayout() {
        ApplySingleEyeLayout(left_eye_, config_.left_pupil_center_x, config_.left_pupil_center_y,
                             config_.left_pupil_radius_x, config_.left_pupil_radius_y);
        ApplySingleEyeLayout(right_eye_, config_.right_pupil_center_x, config_.right_pupil_center_y,
                             config_.right_pupil_radius_x, config_.right_pupil_radius_y);
    }

    void ApplySingleEyeLayout(EyeGroup& eye, int center_x, int center_y, int radius_x,
                              int radius_y) {
        // 眨眼只压缩垂直方向，表现为眼缝闭合。
        int prx =
            std::max(1, static_cast<int>(std::lround(static_cast<float>(radius_x) * eye_scale_)));
        int pry = std::max(1, static_cast<int>(std::lround(static_cast<float>(radius_y) *
                                                           eye_scale_ * blink_scale_)));
        int max_offset = std::max(0, config_.eye_jitter_range);
        int ox = static_cast<int>(std::lround(gaze_x_ * static_cast<float>(max_offset)));
        int oy = static_cast<int>(std::lround(gaze_y_ * static_cast<float>(max_offset)));
        lv_obj_set_size(eye.pupil, prx * 2, pry * 2);
        lv_obj_set_pos(eye.pupil, center_x - prx + ox, center_y - pry + oy);

        if (config_.highlight_border_width <= 0) {
            return;
        }
        int hr =
            std::max(2, static_cast<int>(std::lround(static_cast<float>(config_.highlight_radius) *
                                                     eye_scale_ * blink_scale_)));
        int hd = hr * 2;
        lv_obj_set_size(eye.highlight, hd, hd);
        lv_obj_set_pos(eye.highlight, prx - hr, pry - hr);
    }

    void ClampGaze() {
        float radius_sq = gaze_x_ * gaze_x_ + gaze_y_ * gaze_y_;
        if (radius_sq <= 1.0f) {
            return;
        }
        float inv_len = 1.0f / std::sqrt(radius_sq);
        gaze_x_ *= inv_len;
        gaze_y_ *= inv_len;
    }

    void UpdateIdleGaze() {
        if (idle_need_new_target_) {
            constexpr float kTwoPi = 6.28318530718f;
            float angle = RandomFloat(0.0f, kTwoPi);
            float radius = RandomFloat(0.35f, 0.95f);
            idle_target_x_ = std::cos(angle) * radius;
            idle_target_y_ = std::sin(angle) * radius;
            idle_need_new_target_ = false;
        }

        if (idle_pause_ticks_ > 0) {
            idle_pause_ticks_--;
            gaze_vx_ = 0.0f;
            gaze_vy_ = 0.0f;
            gaze_x_ += (idle_target_x_ - gaze_x_) * 0.22f;
            gaze_y_ += (idle_target_y_ - gaze_y_) * 0.22f;
            if (idle_pause_ticks_ == 0) {
                idle_need_new_target_ = true;
            }
            return;
        }

        float dx = idle_target_x_ - gaze_x_;
        float dy = idle_target_y_ - gaze_y_;
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq < 0.0045f) {
            idle_pause_ticks_ = RandomInRange(3, 8);
            gaze_vx_ = 0.0f;
            gaze_vy_ = 0.0f;
            return;
        }

        gaze_vx_ += dx * 0.42f;
        gaze_vy_ += dy * 0.42f;
        gaze_vx_ = std::max(-0.28f, std::min(0.28f, gaze_vx_)) * 0.76f;
        gaze_vy_ = std::max(-0.28f, std::min(0.28f, gaze_vy_)) * 0.76f;
        gaze_x_ += gaze_vx_;
        gaze_y_ += gaze_vy_;
        ClampGaze();
    }

    void PullGazeToCenterFast(float gain) {
        gaze_vx_ = 0.0f;
        gaze_vy_ = 0.0f;
        gaze_x_ += (0.0f - gaze_x_) * gain;
        gaze_y_ += (0.0f - gaze_y_) * gain;
        if (std::fabs(gaze_x_) < 0.003f) {
            gaze_x_ = 0.0f;
        }
        if (std::fabs(gaze_y_) < 0.003f) {
            gaze_y_ = 0.0f;
        }
    }

    void UpdateManualLook() {
        float dx = manual_look_x_ - gaze_x_;
        float dy = manual_look_y_ - gaze_y_;
        gaze_vx_ += dx * 0.55f;
        gaze_vy_ += dy * 0.55f;
        gaze_vx_ = std::max(-0.32f, std::min(0.32f, gaze_vx_)) * 0.75f;
        gaze_vy_ = std::max(-0.32f, std::min(0.32f, gaze_vy_)) * 0.75f;
        gaze_x_ += gaze_vx_;
        gaze_y_ += gaze_vy_;
        ClampGaze();
        if (manual_look_ticks_ > 0) {
            manual_look_ticks_--;
        }
    }

    void UpdateBlinkAnimation() {
        if (blink_remaining_ <= 0) {
            blink_scale_ += (1.0f - blink_scale_) * 0.25f;
            if (std::fabs(1.0f - blink_scale_) < 0.002f) {
                blink_scale_ = 1.0f;
            }
            return;
        }

        if (blink_closing_) {
            blink_scale_ += (0.12f - blink_scale_) * 0.46f;
            if (--blink_phase_ticks_ <= 0) {
                blink_closing_ = false;
                blink_phase_ticks_ = 4;
            }
        } else {
            blink_scale_ += (1.0f - blink_scale_) * 0.42f;
            if (--blink_phase_ticks_ <= 0) {
                blink_remaining_--;
                if (blink_remaining_ > 0) {
                    blink_closing_ = true;
                    blink_phase_ticks_ = RandomInRange(3, 4);
                } else {
                    blink_scale_ = 1.0f;
                }
            }
        }
    }

    void UpdateSpeakingGaze() {
        if (speaking_target_ticks_ <= 0) {
            speaking_target_y_ = RandomFloat(-0.35f, 0.35f);
            speaking_target_ticks_ = RandomInRange(4, 9);
        }
        speaking_target_ticks_--;

        gaze_vx_ += (0.0f - gaze_x_) * 0.20f;
        gaze_vx_ *= 0.72f;
        gaze_vy_ += (speaking_target_y_ - gaze_y_) * 0.17f;
        gaze_vy_ *= 0.78f;

        gaze_x_ += gaze_vx_;
        gaze_y_ += gaze_vy_;
        gaze_x_ = std::max(-0.20f, std::min(0.20f, gaze_x_));
        gaze_y_ = std::max(-0.45f, std::min(0.45f, gaze_y_));
    }

    void AnimateStep() {
        if (root_ == nullptr || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }

        float prev_gaze_x = gaze_x_;
        float prev_gaze_y = gaze_y_;
        float prev_scale = eye_scale_;
        float prev_blink_scale = blink_scale_;

        if (manual_look_ticks_ > 0) {
            target_eye_scale_ = 1.0f;
            UpdateManualLook();
        } else if (speaking_) {
            target_eye_scale_ = 1.0f;
            UpdateSpeakingGaze();
        } else if (recording_ && voice_active_) {
            target_eye_scale_ = std::max(0.2f, config_.focus_eye_scale);
            PullGazeToCenterFast(0.72f);
        } else if (recording_) {
            target_eye_scale_ = 1.0f;
            PullGazeToCenterFast(0.44f);
        } else {
            target_eye_scale_ = 1.0f;
            UpdateIdleGaze();
        }

        eye_scale_ += (target_eye_scale_ - eye_scale_) * 0.20f;
        if (std::fabs(eye_scale_ - target_eye_scale_) < 0.003f) {
            eye_scale_ = target_eye_scale_;
        }
        UpdateBlinkAnimation();

        bool eye_changed = std::fabs(prev_gaze_x - gaze_x_) >= 0.001f ||
                           std::fabs(prev_gaze_y - gaze_y_) >= 0.001f ||
                           std::fabs(prev_scale - eye_scale_) >= 0.001f ||
                           std::fabs(prev_blink_scale - blink_scale_) >= 0.001f;
        if (eye_changed) {
            ApplyEyeLayout();
        }
    }
};

#endif  // FACE_EXPRESSION_H
