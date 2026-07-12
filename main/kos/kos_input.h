#pragma once

namespace kos {
namespace input {

// 初始化输入子系统(读 IMU + 触摸)。在 boot() 之前调用。
void init();

// poll:每 100ms tick 时调用,由 KOS task 调用。
void poll();

// shake 自上次 reset 以来是否被检测到?
bool shake_detected();

// 清除 shake 标志。
void clear_shake();

}  // namespace input
}  // namespace kos
