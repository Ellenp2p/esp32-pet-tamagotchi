#include "kos_input.h"
#include "bsp/bsp_qmi8658.h"
#include "esp_log.h"

namespace kos {
namespace input {

static const char *TAG = "kos_input";
static bool s_shake = false;

void init()
{
    s_shake = false;
}

void poll()
{
    bsp::QMI8658_Data d;
    if (bsp::QMI8658::instance().read(&d) == ESP_OK) {
        if (bsp::QMI8658::instance().detect_shake(d)) {
            ESP_LOGI(TAG, "Shake detected");
            s_shake = true;
        }
    }
}

bool shake_detected() { return s_shake; }
void clear_shake() { s_shake = false; }

}  // namespace input
}  // namespace kos
