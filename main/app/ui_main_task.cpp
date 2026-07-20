#include "ui_main_task.h"

#include "pet_state.h"
#include "pet_idle_events.h"
#include "pet_save.h"
#include "screen_power.h"
#include "ble_pet.h"
#include "bsp/bsp_qmi8658.h"
#include "esp_log.h"

namespace pet {

static const char *TAG = "PetMainTask";

PetMainTask &PetMainTask::instance() noexcept
{
    static PetMainTask s;
    return s;
}

void PetMainTask::task_trampoline(void *arg)
{
    static_cast<PetMainTask *>(arg)->task_loop();
    vTaskDelete(nullptr);
}

void PetMainTask::task_loop()
{
    bsp::QMI8658_Data imu_data;
    int notify_counter = 0;
    int save_counter = 0;
    pet::idle_events::init();

    while (true) {
        Pet::instance().update();
        pet::idle_events::tick(Pet::instance().get_state().age_ticks);

        if (Pet::instance().is_dirty()) {
            if (++save_counter >= 50) {
                pet::save::save_if_dirty(Pet::instance(), false);
                save_counter = 0;
            }
        } else {
            save_counter = 0;
        }

        if (bsp::QMI8658::instance().read(&imu_data) == ESP_OK) {
            // v0.7: a permissive wake-motion detector runs alongside
            // the strict play-shake detector. detect_wake_motion() is
            // called on every sample so the screen can wake up even
            // when the user only gives a small pickup jolt.
            bool woke = bsp::QMI8658::instance().detect_wake_motion(imu_data);
            if (woke) {
                pet::ScreenPower::instance().note_input();
            }
            if (bsp::QMI8658::instance().detect_shake(imu_data)) {
                ESP_LOGI(TAG, "Shake detected!");
                pet::ScreenPower::instance().note_input();
                if (!Pet::instance().is_sleeping()) {
                    Pet::instance().play();
                } else {
                    Pet::instance().wake_up();
                }
            }
        }

        if (++notify_counter >= 20) {
            pet_ble::notify_state();
            notify_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void PetMainTask::start() noexcept
{
    if (task_) return;
    BaseType_t rc = xTaskCreate(task_trampoline, "pet_task",
                                4096, this, 5, &task_);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "failed to create pet_task");
    } else {
        ESP_LOGI(TAG, "pet_task started");
    }
}

}  // namespace pet