#include "ble_pet.h"

#include "app/pet_state.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/util/util.h"

extern "C" void ble_store_config_init(void);

// BLE protocol notes (custom 16-bit UUIDs: svc 0x1234, state chr 0x1235, cmd chr 0x1236).
//   * State characteristic payload: 5 bytes
//       [0] fullness    (uint8, 0=starving .. 100=full)   <-- v0.2.0 SEMANTICS INVERTED
//       [1] happiness
//       [2] energy
//       [3] health
//       [4] sleeping    (uint8, 0=awake, 1=sleeping)
//   * Pre-v0.2.0 byte [0] was "hunger" (high = bad); it is now "fullness" (high = good).
//     External clients reading [0] must invert if they expect the old meaning.
//   * A future protocol_version byte will be prepended once we have a second client.

namespace pet_ble {

static const char *TAG = "ble_pet";

/* Custom 16-bit UUIDs for the pet service */
static const ble_uuid16_t pet_svc_uuid   = BLE_UUID16_INIT(0x1234);
static const ble_uuid16_t state_chr_uuid = BLE_UUID16_INIT(0x1235);
static const ble_uuid16_t cmd_chr_uuid   = BLE_UUID16_INIT(0x1236);

static uint16_t state_chr_handle = 0;
static uint16_t cmd_chr_handle   = 0;

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool notify_subscribed = false;

static int pet_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg);
static int pet_gap_event(struct ble_gap_event *event, void *arg);

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &pet_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &state_chr_uuid.u,
                .access_cb = pet_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &state_chr_handle,
            },
            {
                .uuid = &cmd_chr_uuid.u,
                .access_cb = pet_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &cmd_chr_handle,
            },
            { 0 }, /* No more characteristics */
        },
    },
    { 0 }, /* No more services */
};

static void update_state_mbuf(struct os_mbuf *om)
{
    pet::State s = pet::Pet::instance().get_state();
    uint8_t buf[5] = {
        static_cast<uint8_t>(s.fullness),
        static_cast<uint8_t>(s.happiness),
        static_cast<uint8_t>(s.energy),
        static_cast<uint8_t>(s.health),
        pet::Pet::instance().is_sleeping() ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0)
    };
    os_mbuf_append(om, buf, sizeof(buf));
}

static int pet_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (attr_handle == state_chr_handle) {
            update_state_mbuf(ctxt->om);
            return 0;
        }
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (attr_handle == cmd_chr_handle) {
            uint8_t cmd = 0;
            rc = ble_hs_mbuf_to_flat(ctxt->om, &cmd, sizeof(cmd), nullptr);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            switch (cmd) {
            case 'F':
            case 'f':
                pet::Pet::instance().feed();
                ESP_LOGI(TAG, "BLE command: feed");
                break;
            case 'P':
            case 'p':
                pet::Pet::instance().play();
                ESP_LOGI(TAG, "BLE command: play");
                break;
            case 'S':
            case 's':
                pet::Pet::instance().sleep();
                ESP_LOGI(TAG, "BLE command: sleep");
                break;
            case 'W':
            case 'w':
                pet::Pet::instance().wake_up();
                ESP_LOGI(TAG, "BLE command: wake");
                break;
            case 'C':
            case 'c':
                pet::Pet::instance().pet();
                ESP_LOGI(TAG, "BLE command: pet");
                break;
            default:
                ESP_LOGW(TAG, "Unknown BLE command: 0x%02x", cmd);
                break;
            }
            return 0;
        }
        break;

    default:
        break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void pet_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service %s handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registered characteristic %s handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.val_handle);
        break;
    default:
        break;
    }
}

static void pet_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = reinterpret_cast<uint8_t *>(const_cast<char *>(name));
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = &pet_svc_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER,
                           &adv_params, pet_gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
    }
}

static int pet_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
        } else {
            pet_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect reason=%d", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_subscribed = false;
        pet_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertise complete reason=%d", event->adv_complete.reason);
        pet_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == state_chr_handle) {
            notify_subscribed = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Notify subscription changed: %d", notify_subscribed);
        }
        return 0;

    default:
        return 0;
    }
}

static void pet_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error ensuring identity address; rc=%d", rc);
        return;
    }

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, nullptr);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0]);
    }

    pet_advertise();
}

static void pet_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t init()
{
    int rc;

    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE port: %d", rc);
        return rc;
    }

    ble_hs_cfg.reset_cb = pet_on_reset;
    ble_hs_cfg.sync_cb = pet_on_sync;
    ble_hs_cfg.gatts_register_cb = pet_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;

    rc = ble_svc_gap_device_name_set("ESP-Pet");
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
        return ESP_FAIL;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return rc;
    }

    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE peripheral initialized");
    return ESP_OK;
}

void notify_state()
{
    if (!notify_subscribed || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct os_mbuf *om = os_msys_get_pkthdr(5, 0);
    if (om == nullptr) {
        return;
    }
    update_state_mbuf(om);

    int rc = ble_gatts_notify_custom(conn_handle, state_chr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send notification; rc=%d", rc);
    }
}

} // namespace pet_ble
