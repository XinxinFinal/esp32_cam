#pragma once

#include <stdbool.h>

/* 在 app_main 早期调用一次：NVS 计数 +1，判断是否触发「连续快速重启」。
 * 返回 true 表示应清空 WiFi 凭据并进入配网模式。 */
bool boot_counter_check_and_reset_needed(void);

/* 启动稳定期定时器：设备连续运行超过 settle 秒后把计数归零。
 * 应在设备正常起来后调用（app_main 末尾即可，幂等）。 */
void boot_counter_start_settle(void);
