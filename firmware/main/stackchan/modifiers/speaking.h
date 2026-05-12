/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
#include <hal/hal.h>
#include <cstdint>

namespace stackchan {

class SpeakingModifier : public Modifier {
public:
    /**
     * @param destroyAfterMs 持续说话时间（0 为永久，直到手动移除）
     * @param mouthIntervalMs 嘴巴开合频率（默认 180ms）
     * @param enableMotion 是否在说话时伴随头部微动
     * @param silentGapMs 说话结束后静默 gap 时间（默认 500ms），设为 0 禁用
     */
    SpeakingModifier(uint32_t destroyAfterMs = 0, uint32_t mouthIntervalMs = 180, bool enableMotion = true, uint32_t silentGapMs = 500)
        : _mouth_interval_ms(mouthIntervalMs), _silent_gap_duration_ms(silentGapMs), _enable_motion(enableMotion)
    {
        uint32_t now = GetHAL().millis();

        // 销毁计时
        if (destroyAfterMs > 0) {
            // Add slight randomization to speech duration (±150ms)
            int32_t randomOffset = Random::getInstance().getInt(-150, 150);
            int32_t adjustedDuration = static_cast<int32_t>(destroyAfterMs) + randomOffset;
            if (adjustedDuration < 500) adjustedDuration = 500;
            _destroy_at   = now + static_cast<uint32_t>(adjustedDuration);
            _has_lifetime = true;
        }

        // 嘴巴计时
        _next_mouth_tick = now + _mouth_interval_ms;

        // 动作计时
        if (_enable_motion) {
            _next_motion_tick = now + Random::getInstance().getInt(1000, 2000);
        }

        _need_get_prev_angles = true;
        _is_in_silent_gap = false;
        _silent_gap_start = 0;
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        uint32_t now = GetHAL().millis();

        // 检查静默 gap 逻辑
        if (_is_in_silent_gap) {
            if (now - _silent_gap_start >= _silent_gap_duration_ms) {
                requestDestroy();
            }
            return;
        }

        // 检查销毁逻辑
        if (_has_lifetime && now >= _destroy_at) {
            // 进入静默 gap 阶段
            if (_silent_gap_duration_ms > 0) {
                stackchan.avatar().setSpeech("");
                stackchan.avatar().mouth().setWeight(0);
                _is_in_silent_gap = true;
                _silent_gap_start = now;
                // Add slight randomization to gap duration (±100ms)
                int32_t randomOffset = Random::getInstance().getInt(-100, 100);
                int32_t adjustedGap = static_cast<int32_t>(_silent_gap_duration_ms) + randomOffset;
                if (adjustedGap < 200) adjustedGap = 200;
                _silent_gap_duration_ms = static_cast<uint32_t>(adjustedGap);
            } else {
                stackchan.avatar().mouth().setWeight(0);
                requestDestroy();
            }
            return;
        }

        // 嘴巴开合动画
        if (now >= _next_mouth_tick) {
            _next_mouth_tick = now + _mouth_interval_ms;
            animate_mouth(stackchan.avatar());
        }

        // 身体微动动作
        if (_enable_motion && now >= _next_motion_tick) {
            // 随机下一个动作的时间 (1.5s ~ 2.5s)
            _next_motion_tick = now + Random::getInstance().getInt(1500, 2500);
            perform_subtle_speaking_motion(stackchan);
        }
    }

private:
    void animate_mouth(avatar::Avatar& avatar)
    {
        _is_mouth_open = !_is_mouth_open;
        auto& random   = Random::getInstance();

        int weight = _is_mouth_open ? random.getInt(_open_min_weight, _open_max_weight)
                                    : random.getInt(_close_min_weight, _close_max_weight);

        avatar.mouth().setWeight(weight);
    }

    void perform_subtle_speaking_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        if (motion.isMoving()) {
            return;
        }

        uitk::Vector2i current_actual_angles = motion.getCurrentAngles();

        if (_need_get_prev_angles) {
            _prev_angles          = current_actual_angles;
            _need_get_prev_angles = false;
        } else {
            // If there is a large external movement
            // sync the baseline angles to preventsnapping back to old position
            const int32_t threshold = 300;
            int32_t diff_x          = std::abs(current_actual_angles.x - _prev_angles.x);
            int32_t diff_y          = std::abs(current_actual_angles.y - _prev_angles.y);

            if (diff_x > threshold || diff_y > threshold) {
                _prev_angles = current_actual_angles;
            }
        }

        int32_t target_yaw   = _prev_angles.x;
        int32_t target_pitch = _prev_angles.y;

        int action = Random::getInstance().getInt(0, 10);
        int speed  = Random::getInstance().getInt(100, 200);  // 说话时的动作都很慢

        if (action < 5) {
            // 动作 A：轻微点头 (Nod)
            target_pitch += Random::getInstance().getInt(-20, 50);
        } else {
            // 动作 B：轻微摆头 (Yaw drift)
            target_yaw += Random::getInstance().getInt(-40, 40);
            target_pitch += Random::getInstance().getInt(-20, 20);
        }

        motion.moveWithSpeed(target_yaw, target_pitch, speed);
    }

    // 配置常量
    const int _open_min_weight  = 40;
    const int _open_max_weight  = 80;
    const int _close_min_weight = 0;
    const int _close_max_weight = 20;

    // 计时状态
    uint32_t _destroy_at       = 0;
    uint32_t _next_mouth_tick  = 0;
    uint32_t _next_motion_tick = 0;
    uint32_t _mouth_interval_ms;
    uint32_t _silent_gap_duration_ms;
    uint32_t _silent_gap_start;

    bool _has_lifetime         = false;
    bool _enable_motion        = false;
    bool _is_mouth_open        = false;
    bool _need_get_prev_angles = true;
    bool _is_in_silent_gap     = false;

    uitk::Vector2i _prev_angles;
};

}  // namespace stackchan
