#ifndef PETOI_FACE_DISPLAY_H
#define PETOI_FACE_DISPLAY_H

#include <memory>
#include <string>

#include "display/lcd_display.h"
#include "face_expression.h"

// 表情脸显示：在 SpiLcdDisplay 基础上隐藏系统 UI，替换为动画脸。
class PetoiFaceDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    ~PetoiFaceDisplay() override;

    void SetupUI() override;
    bool SetEmotion(const char* emotion) override;

    void SetExpressionState(bool recording, bool speaking) override;
    void SetExpressionVoiceActive(bool active) override;
    void SetExpressionLook(const std::string& direction) override;
    void TriggerExpressionBlink(int times) override;

private:
    void HideChrome();

    std::unique_ptr<FaceExpression> face_;
};

#endif  // PETOI_FACE_DISPLAY_H
