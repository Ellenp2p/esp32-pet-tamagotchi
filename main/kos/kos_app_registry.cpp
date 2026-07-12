#include "kos_app_registry.h"
#include "kos.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

namespace kos {

static const char *TAG = "kos_registry";

namespace registry {
namespace internals {

// 静态表 + 锁外(单线程 OS)— 静态分配 16 个 slot 足够 App 数量。
static Entry s_table[16];
static int   s_count = 0;

void add_entry(const Entry &e)
{
    if (s_count < (int)(sizeof(s_table) / sizeof(s_table[0]))) {
        s_table[s_count++] = e;
    }
    // 超额就静默丢弃 —— 16 个 App 上限远高于实际。
}

StaticRegistrar::StaticRegistrar(const Entry &e)
{
    add_entry(e);
}

}  // namespace internals

void init()
{
    ESP_LOGI(TAG, "init: %d app(s) registered", registry::internals::s_count);
}

int count()
{
    return registry::internals::s_count;
}

App &app(int i)
{
    return *registry::internals::s_table[i].instance;
}

App *find(const char *id)
{
    int n = registry::internals::s_count;
    for (int i = 0; i < n; i++) {
        if (strcmp(registry::internals::s_table[i].id, id) == 0) {
            return registry::internals::s_table[i].instance;
        }
    }
    return nullptr;
}

static App *s_current = nullptr;

void launch(const char *id)
{
    App *next = find(id);
    if (!next) {
        ESP_LOGE(TAG, "launch: no such app '%s'", id);
        return;
    }
    if (s_current == next) return;

    if (s_current) s_current->on_pause();
    s_current = next;

    AppContext ctx;
    ctx.screen_root = lv_scr_act();
    ctx.now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ctx.request_launch = [](const char *app_id) {
        registry::launch(app_id);
    };
    s_current->on_start(ctx);

    kos::set_active_id(s_current->manifest().id);
    ESP_LOGI(TAG, "launched: %s", s_current->manifest().id);
}

App *current() { return s_current; }

}  // namespace registry

AutoRegisterForInstance::AutoRegisterForInstance(const char *id, App *instance)
{
    registry::internals::Entry e = {id, instance};
    registry::internals::add_entry(e);
}

}  // namespace kos
