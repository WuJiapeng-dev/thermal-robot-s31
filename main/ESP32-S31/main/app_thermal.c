#include "app_thermal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

#define TAG "thermal"
#define USB_SCAN_PERIOD_MS 500U
#define USB_OUT_TIMEOUT_MS 500U
#define CALIBRATION_CAPACITY_BYTES (2U * 1024U * 1024U)
#define CALIBRATION_CHUNK_BYTES 65472U /* 64 KiB rounded down to a 64-byte multiple. */
#define THERMAL_CMD_GET_PARAMETER_2   0x6BB6B66CU
#define THERMAL_CMD_GET_CALI_INFO     0x6BB6B66FU
#define THERMAL_CMD_GET_CALI_FILE     0x6BB6B670U

typedef struct {
    bool ready;
    uint32_t frame_id;
    uint32_t fpa_raw;
    uint64_t timestamp_ms;
} thermal_slot_t;

static uint8_t *s_assembly_buffer;
static uint8_t *s_frame_buffers[2];
static thermal_slot_t s_slots[2];
static size_t s_write_index;
static size_t s_assemble_offset;
static QueueHandle_t s_frame_queue;
static SemaphoreHandle_t s_frame_mutex;

static usb_host_client_handle_t s_client;
static usb_device_handle_t s_device;
static uint8_t s_interface;
static uint16_t s_vid;
static uint16_t s_pid;
static uint8_t s_endpoint_count;
static uint16_t s_image_mps = 64;
static usb_transfer_t *s_cmd_out;
static usb_transfer_t *s_cmd_in;
static usb_transfer_t *s_image_in;
static usb_transfer_t *s_cal_in;
static SemaphoreHandle_t s_cmd_out_done;
static SemaphoreHandle_t s_cmd_ack_done;
static SemaphoreHandle_t s_cal_done;
static SemaphoreHandle_t s_cmd_mutex;
static volatile bool s_image_in_flight;
static volatile bool s_device_gone;
static volatile bool s_stop_image_requested;
static bool s_usb_installed;
static bool s_initialized;
static bool s_streaming;
static bool s_calibration_downloaded;
static uint8_t *s_calibration;
static size_t s_calibration_length;
static uint32_t s_received_frames;
static uint32_t s_dropped_frames;
static uint32_t s_failed_frames;
static uint32_t s_consecutive_failures;

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 16) & 0xFFU);
    data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void command_out_callback(usb_transfer_t *transfer)
{
    (void)transfer;
    xSemaphoreGive(s_cmd_out_done);
}

static void command_ack_callback(usb_transfer_t *transfer)
{
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "command ACK transfer status=%d", (int)transfer->status);
    }
    xSemaphoreGive(s_cmd_ack_done);
}

static void calibration_callback(usb_transfer_t *transfer)
{
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "calibration transfer status=%d", (int)transfer->status);
    }
    xSemaphoreGive(s_cal_done);
}

static uint8_t *find_head_magic(uint8_t *buffer, size_t length)
{
    if (length < 4) {
        return NULL;
    }
    for (size_t i = 0; i + 4 <= length; ++i) {
        if (read_le32(&buffer[i]) == THERMAL_FRAME_MAGIC_HEAD) {
            return &buffer[i];
        }
    }
    return NULL;
}

static void deliver_frame(const uint8_t *frame)
{
    const uint32_t head_frame_id = read_le32(&frame[4]);
    const uint32_t tail_frame_id = read_le32(&frame[THERMAL_HEADER_BYTES + THERMAL_PIXEL_BYTES + 4]);
    const uint32_t fpa_raw = read_le32(&frame[THERMAL_HEADER_BYTES + THERMAL_PIXEL_BYTES + 8]);
    if (head_frame_id != tail_frame_id) {
        ++s_failed_frames;
        return;
    }

    uint8_t *pixels = s_frame_buffers[s_write_index];
    memcpy(pixels, &frame[THERMAL_HEADER_BYTES], THERMAL_PIXEL_BYTES);

    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
    s_slots[s_write_index].frame_id = head_frame_id;
    s_slots[s_write_index].fpa_raw = fpa_raw;
    s_slots[s_write_index].timestamp_ms = esp_timer_get_time() / 1000ULL;
    s_slots[s_write_index].ready = true;
    size_t ready_index = s_write_index;
    s_write_index ^= 1U;
    xSemaphoreGive(s_frame_mutex);

    uint8_t queue_value = (uint8_t)ready_index;
    xQueueOverwrite(s_frame_queue, &queue_value);
    ++s_received_frames;
    s_consecutive_failures = 0;
}

static void parse_image_bytes(const uint8_t *data, size_t length)
{
    uint8_t *buffer = s_assembly_buffer;
    if (length > THERMAL_FRAME_BYTES - s_assemble_offset) {
        size_t accepted = THERMAL_FRAME_BYTES - s_assemble_offset;
        memcpy(&buffer[s_assemble_offset], data, accepted);
        s_assemble_offset = THERMAL_FRAME_BYTES;
        data += accepted;
        length -= accepted;
    } else {
        memcpy(&buffer[s_assemble_offset], data, length);
        s_assemble_offset += length;
        length = 0;
    }

    for (;;) {
        uint8_t *magic = find_head_magic(buffer, s_assemble_offset);
        if (!magic) {
            /* Retain only a possible split magic suffix; discard the rest. */
            if (s_assemble_offset > 3) {
                memmove(buffer, &buffer[s_assemble_offset - 3], 3);
                s_assemble_offset = 3;
            }
            break;
        }
        size_t discard = (size_t)(magic - buffer);
        if (discard) {
            memmove(buffer, magic, s_assemble_offset - discard);
            s_assemble_offset -= discard;
        }
        if (s_assemble_offset < THERMAL_FRAME_BYTES) {
            break;
        }

        if (read_le32(&buffer[THERMAL_HEADER_BYTES + THERMAL_PIXEL_BYTES]) != THERMAL_FRAME_MAGIC_TAIL) {
            ++s_dropped_frames;
            memmove(buffer, &buffer[1], s_assemble_offset - 1);
            --s_assemble_offset;
            continue;
        }

        deliver_frame(buffer);
        size_t remaining = s_assemble_offset - THERMAL_FRAME_BYTES;
        memmove(buffer, &buffer[THERMAL_FRAME_BYTES], remaining);
        s_assemble_offset = remaining;
    }

    if (length > 0) {
        ESP_LOGW(TAG, "image parser discarded %u trailing bytes", (unsigned)length);
    }
}

static void image_callback(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED &&
        transfer->actual_num_bytes > 0) {
        parse_image_bytes(transfer->data_buffer, (size_t)transfer->actual_num_bytes);
    } else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "image transfer status=%d bytes=%d", (int)transfer->status,
                 transfer->actual_num_bytes);
    }

    s_image_in_flight = false;
    if (s_streaming && !s_stop_image_requested && s_device) {
        transfer->num_bytes = (int)THERMAL_USB_CHUNK_BYTES;
        transfer->actual_num_bytes = 0;
        esp_err_t ret = usb_host_transfer_submit(transfer);
        if (ret == ESP_OK) {
            s_image_in_flight = true;
        } else {
            ESP_LOGW(TAG, "image resubmit failed: %s", esp_err_to_name(ret));
        }
    }
}

static void usb_client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGI(TAG, "new USB device address=%u", (unsigned)event_msg->new_dev.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (event_msg->dev_gone.dev_hdl == s_device) {
            s_device_gone = true;
        }
        break;
    default:
        break;
    }
}

static void usb_lib_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

static esp_err_t command_transaction(uint32_t command, uint32_t parameter,
                                     bool send_parameter, uint8_t *ack, size_t ack_capacity,
                                     size_t *ack_length)
{
    if (!s_device || !s_cmd_out || !s_cmd_in) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);

    size_t command_bytes = send_parameter ? 8U : 4U;
    memset(s_cmd_out->data_buffer, 0, s_cmd_out->data_buffer_size);
    write_le32(s_cmd_out->data_buffer, command);
    if (send_parameter) {
        write_le32(&s_cmd_out->data_buffer[4], parameter);
    }
    s_cmd_out->num_bytes = (int)command_bytes;
    s_cmd_out->actual_num_bytes = 0;
    s_cmd_out->device_handle = s_device;
    s_cmd_out->bEndpointAddress = THERMAL_EP_CMD_OUT;
    s_cmd_out->callback = command_out_callback;
    s_cmd_out->context = NULL;

    xSemaphoreTake(s_cmd_out_done, 0);
    esp_err_t ret = usb_host_transfer_submit(s_cmd_out);
    if (ret == ESP_OK &&
        xSemaphoreTake(s_cmd_out_done, pdMS_TO_TICKS(USB_OUT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "command 0x%08lX write timeout", (unsigned long)command);
        ret = ESP_ERR_TIMEOUT;
    }
    if (ret != ESP_OK) {
        xSemaphoreGive(s_cmd_mutex);
        return ret;
    }

    memset(s_cmd_in->data_buffer, 0, THERMAL_ACK_MAX_BYTES);
    s_cmd_in->num_bytes = (int)THERMAL_ACK_MAX_BYTES;
    s_cmd_in->actual_num_bytes = 0;
    s_cmd_in->device_handle = s_device;
    s_cmd_in->bEndpointAddress = THERMAL_EP_ACK_IN;
    s_cmd_in->callback = command_ack_callback;
    s_cmd_in->context = NULL;

    xSemaphoreTake(s_cmd_ack_done, 0);
    ret = usb_host_transfer_submit(s_cmd_in);
    if (ret == ESP_OK &&
        xSemaphoreTake(s_cmd_ack_done, pdMS_TO_TICKS(THERMAL_USB_CMD_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "command 0x%08lX ACK timeout", (unsigned long)command);
        ret = ESP_ERR_TIMEOUT;
    }
    if (ret == ESP_OK && s_cmd_in->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "command 0x%08lX ACK status=%d", (unsigned long)command,
                 (int)s_cmd_in->status);
        ret = ESP_ERR_INVALID_RESPONSE;
    }
    if (ret == ESP_OK) {
        size_t length = (size_t)s_cmd_in->actual_num_bytes;
        if (length > ack_capacity) {
            length = ack_capacity;
        }
        if (ack && length) {
            memcpy(ack, s_cmd_in->data_buffer, length);
        }
        if (ack_length) {
            *ack_length = length;
        }
        ESP_LOGD(TAG, "command 0x%08lX ACK %u bytes", (unsigned long)command,
                 (unsigned)length);
    }
    xSemaphoreGive(s_cmd_mutex);
    return ret;
}

static esp_err_t start_image_stream(void)
{
    uint8_t ack[16];
    esp_err_t ret = command_transaction(THERMAL_CMD_START_IMAGE, 0, false,
                                        ack, sizeof(ack), NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "StartTransferImg failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_streaming = true;
    s_stop_image_requested = false;
    if (!s_image_in_flight) {
        s_image_in->num_bytes = (int)THERMAL_USB_CHUNK_BYTES;
        s_image_in->actual_num_bytes = 0;
        s_image_in->device_handle = s_device;
        s_image_in->bEndpointAddress = THERMAL_EP_IMAGE_IN;
        s_image_in->callback = image_callback;
        s_image_in->context = NULL;
        ret = usb_host_transfer_submit(s_image_in);
        if (ret != ESP_OK) {
            s_streaming = false;
            ESP_LOGE(TAG, "image stream submit failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_image_in_flight = true;
    }
    ESP_LOGI(TAG, "image stream started");
    return ESP_OK;
}

static esp_err_t stop_image_stream(void)
{
    if (!s_streaming) {
        return ESP_OK;
    }
    s_stop_image_requested = true;
    uint8_t ack[16];
    esp_err_t ret = command_transaction(THERMAL_CMD_STOP_IMAGE, 0, false,
                                        ack, sizeof(ack), NULL);
    s_streaming = false;
    for (int i = 0; i < 20 && s_image_in_flight; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_image_in_flight) {
        ESP_LOGW(TAG, "image transfer still in flight after stop request");
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "image stream stopped");
    } else {
        ESP_LOGW(TAG, "StopTransferImg failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t close_device(void)
{
    if (!s_device) {
        return ESP_OK;
    }
    esp_err_t stop_ret = stop_image_stream();
    (void)stop_ret;

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(500);
    while (s_image_in_flight && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_image_in_flight) {
        ESP_LOGW(TAG, "closing device with an image transfer still pending");
    }

    esp_err_t ret = usb_host_interface_release(s_client, s_device, s_interface);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "interface release failed: %s", esp_err_to_name(ret));
    }
    ret = usb_host_device_close(s_client, s_device);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "device close failed: %s", esp_err_to_name(ret));
    }
    s_device = NULL;
    s_streaming = false;
    s_image_in_flight = false;
    s_assemble_offset = 0;
    ESP_LOGI(TAG, "MAG160Core disconnected");
    return ret;
}

static esp_err_t open_thermal_device(void)
{
    uint8_t addresses[8] = {0};
    int count = 0;
    esp_err_t list_ret = usb_host_device_addr_list_fill(sizeof(addresses), addresses, &count);
    if (list_ret != ESP_OK) {
        return list_ret;
    }
    if (count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    for (int i = 0; i < count; ++i) {
        usb_device_handle_t device = NULL;
        if (usb_host_device_open(s_client, addresses[i], &device) != ESP_OK) {
            continue;
        }
        const usb_device_desc_t *device_desc = NULL;
        if (usb_host_get_device_descriptor(device, &device_desc) != ESP_OK || !device_desc) {
            usb_host_device_close(s_client, device);
            continue;
        }
        if (device_desc->idVendor != THERMAL_USB_VID ||
            (device_desc->idProduct != THERMAL_USB_PID_1 &&
             device_desc->idProduct != THERMAL_USB_PID_2)) {
            usb_host_device_close(s_client, device);
            continue;
        }

        const usb_config_desc_t *config_desc = NULL;
        esp_err_t ret = usb_host_get_active_config_descriptor(device, &config_desc);
        if (ret != ESP_OK || !config_desc) {
            ESP_LOGE(TAG, "active config read failed: %s", esp_err_to_name(ret));
            usb_host_device_close(s_client, device);
            return ret;
        }

        bool found = false;
        uint8_t interface_number = 0;
        const usb_ep_desc_t *cmd_ep = NULL;
        const usb_ep_desc_t *ack_ep = NULL;
        const usb_ep_desc_t *image_ep = NULL;
        for (uint8_t intf = 0; intf < config_desc->bNumInterfaces && !found; ++intf) {
            const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf, 0, NULL);
            if (!intf_desc) {
                continue;
            }
            cmd_ep = NULL;
            ack_ep = NULL;
            image_ep = NULL;
            for (uint8_t ep = 0; ep < intf_desc->bNumEndpoints; ++ep) {
                const usb_ep_desc_t *ep_desc = usb_parse_endpoint_descriptor_by_index(
                    intf_desc, ep, config_desc->wTotalLength, NULL);
                if (!ep_desc) {
                    continue;
                }
                if (ep_desc->bEndpointAddress == THERMAL_EP_CMD_OUT) {
                    cmd_ep = ep_desc;
                } else if (ep_desc->bEndpointAddress == THERMAL_EP_ACK_IN) {
                    ack_ep = ep_desc;
                } else if (ep_desc->bEndpointAddress == THERMAL_EP_IMAGE_IN) {
                    image_ep = ep_desc;
                }
            }
            if (cmd_ep && ack_ep && image_ep) {
                found = true;
                interface_number = intf_desc->bInterfaceNumber;
                s_endpoint_count = intf_desc->bNumEndpoints;
                s_image_mps = image_ep->wMaxPacketSize;
            }
        }
        if (!found) {
            ESP_LOGW(TAG, "VID/PID matched but required bulk endpoints were not found");
            usb_host_device_close(s_client, device);
            continue;
        }

        ret = usb_host_interface_claim(s_client, device, interface_number, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "interface claim failed: %s", esp_err_to_name(ret));
            usb_host_device_close(s_client, device);
            return ret;
        }

        s_device = device;
        s_interface = interface_number;
        s_vid = device_desc->idVendor;
        s_pid = device_desc->idProduct;
        s_device_gone = false;
        ESP_LOGI(TAG, "MAG160Core connected: VID=0x%04X PID=0x%04X EPs=%u image MPS=%u",
                 s_vid, s_pid, s_endpoint_count, s_image_mps);

        uint8_t ack[64];
        size_t ack_length = 0;
        ret = command_transaction(THERMAL_CMD_GET_PARAMETER_1, 0, true,
                                  ack, sizeof(ack), &ack_length);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GetParameter1 failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "GetParameter1 ACK: %u bytes", (unsigned)ack_length);
        }
        ret = command_transaction(THERMAL_CMD_GET_PARAMETER_2, 0, true,
                                  ack, sizeof(ack), &ack_length);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GetParameter2 failed: %s", esp_err_to_name(ret));
        }
        ret = command_transaction(THERMAL_CMD_GET_CALI_INFO, 0, true,
                                  ack, sizeof(ack), &ack_length);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GetCaliInfo failed: %s", esp_err_to_name(ret));
        } else if (ack_length >= 8) {
            ESP_LOGI(TAG, "calibration size reported by device: %lu bytes",
                     (unsigned long)read_le32(&ack[4]));
        }

        ret = start_image_stream();
        if (ret != ESP_OK) {
            close_device();
            return ret;
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static void thermal_event_task(void *arg)
{
    (void)arg;
    esp_err_t ret = usb_host_client_register(&(usb_host_client_config_t){
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .flags = {.notify_dev_removed = false},
        .async = {.client_event_callback = usb_client_event_callback, .callback_arg = NULL},
    }, &s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB client register failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_scan = 0;
    for (;;) {
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(10));
        if (s_device_gone) {
            ++s_consecutive_failures;
            ESP_LOGW(TAG, "device gone (failure %lu)", (unsigned long)s_consecutive_failures);
            close_device();
            vTaskDelay(pdMS_TO_TICKS(THERMAL_RETRY_WAIT_MS));
            last_scan = xTaskGetTickCount();
            continue;
        }
        if (!s_device && (xTaskGetTickCount() - last_scan) >= pdMS_TO_TICKS(USB_SCAN_PERIOD_MS)) {
            last_scan = xTaskGetTickCount();
            ret = open_thermal_device();
            if (ret == ESP_OK) {
                s_consecutive_failures = 0;
            } else if (ret != ESP_ERR_NOT_FOUND) {
                ++s_consecutive_failures;
                ESP_LOGW(TAG, "open attempt failed: %s", esp_err_to_name(ret));
                if (s_consecutive_failures >= THERMAL_MAX_CONSECUTIVE_FAILS) {
                    ESP_LOGE(TAG, "too many USB failures; retrying after %u ms",
                             (unsigned)THERMAL_RETRY_WAIT_MS);
                    vTaskDelay(pdMS_TO_TICKS(THERMAL_RETRY_WAIT_MS));
                }
            }
        }
    }
}

static esp_err_t alloc_buffers(void)
{
    s_assembly_buffer = heap_caps_malloc(THERMAL_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_frame_buffers[0] = heap_caps_malloc(THERMAL_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_frame_buffers[1] = heap_caps_malloc(THERMAL_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_assembly_buffer || !s_frame_buffers[0] || !s_frame_buffers[1]) {
        ESP_LOGE(TAG, "PSRAM frame allocation failed");
        return ESP_ERR_NO_MEM;
    }
    s_calibration = heap_caps_malloc(CALIBRATION_CAPACITY_BYTES,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_calibration) {
        ESP_LOGE(TAG, "PSRAM calibration allocation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_thermal_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = alloc_buffers();
    if (ret != ESP_OK) {
        return ret;
    }

    s_frame_queue = xQueueCreate(1, sizeof(uint8_t));
    s_frame_mutex = xSemaphoreCreateMutex();
    s_cmd_mutex = xSemaphoreCreateMutex();
    s_cmd_out_done = xSemaphoreCreateBinary();
    s_cmd_ack_done = xSemaphoreCreateBinary();
    s_cal_done = xSemaphoreCreateBinary();
    if (!s_frame_queue || !s_frame_mutex || !s_cmd_mutex || !s_cmd_out_done ||
        !s_cmd_ack_done || !s_cal_done) {
        return ESP_ERR_NO_MEM;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = 0,
        .fifo_settings_custom = {0},
        .peripheral_map = 0,
        .fsls_only = false,
    };
    ret = usb_host_install(&host_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "USB host install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_usb_installed = true;

    BaseType_t task_ret = xTaskCreate(usb_lib_task, "usb_lib", TASK_STACK_USB_LIB, NULL,
                                      TASK_PRIORITY_USB_LIB, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "USB library task create failed");
        return ESP_ERR_NO_MEM;
    }
    task_ret = xTaskCreate(thermal_event_task, "thermal_usb", TASK_STACK_USB_CLIENT, NULL,
                           TASK_PRIORITY_USB_CLIENT, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "thermal USB task create failed");
        return ESP_ERR_NO_MEM;
    }

    ret = usb_host_transfer_alloc(8, 0, &s_cmd_out);
    if (ret == ESP_OK) {
        ret = usb_host_transfer_alloc(THERMAL_ACK_MAX_BYTES, 0, &s_cmd_in);
    }
    if (ret == ESP_OK) {
        ret = usb_host_transfer_alloc(THERMAL_USB_CHUNK_BYTES, 0, &s_image_in);
    }
    if (ret == ESP_OK) {
        ret = usb_host_transfer_alloc(CALIBRATION_CHUNK_BYTES, 0, &s_cal_in);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB transfer allocation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "MAG160Core USB host initialized");
    return ESP_OK;
}

static esp_err_t copy_slot(size_t index, thermal_frame_t *frame)
{
    if (!s_slots[index].ready || !frame || !frame->pixels) {
        return ESP_ERR_INVALID_STATE;
    }
    frame->width = THERMAL_WIDTH;
    frame->height = THERMAL_HEIGHT;
    frame->frame_id = s_slots[index].frame_id;
    frame->fpa_raw = s_slots[index].fpa_raw;
    frame->timestamp_ms = s_slots[index].timestamp_ms;
    memcpy(frame->pixels, s_frame_buffers[index], THERMAL_PIXEL_BYTES);
    return ESP_OK;
}

esp_err_t thermal_get_frame(thermal_frame_t *frame, uint32_t timeout_ms)
{
    if (!s_initialized || !frame || !frame->pixels) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t index = 0;
    if (xQueueReceive(s_frame_queue, &index, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
    esp_err_t ret = copy_slot(index, frame);
    xSemaphoreGive(s_frame_mutex);
    return ret;
}

esp_err_t app_thermal_get_latest(thermal_frame_t *frame)
{
    if (!s_initialized || !frame || !frame->pixels) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
    size_t index = s_write_index == 0 ? 1U : 0U;
    esp_err_t ret = s_slots[index].ready ? copy_slot(index, frame) : ESP_ERR_TIMEOUT;
    xSemaphoreGive(s_frame_mutex);
    return ret;
}

float raw_to_celsius(uint16_t raw)
{
    (void)raw;
    static bool warned;
    if (!warned) {
        warned = true;
        ESP_LOGW(TAG, "raw_to_celsius is uncalibrated and returns NAN; do not report raw counts as Celsius");
    }
    return NAN;
}

esp_err_t app_thermal_set_shutter(bool open)
{
    uint8_t ack[16];
    return command_transaction(THERMAL_CMD_SET_SHUTTER_STATE, open ? 1U : 0U, true,
                               ack, sizeof(ack), NULL);
}

esp_err_t app_thermal_download_calibration(void)
{
    if (!s_device || !s_calibration) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = stop_image_stream();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "stream stop before calibration download failed; continuing");
    }

    uint8_t ack[64];
    size_t ack_length = 0;
    ret = command_transaction(THERMAL_CMD_GET_CALI_INFO, 0, true,
                              ack, sizeof(ack), &ack_length);
    if (ret != ESP_OK || ack_length < 8) {
        ESP_LOGE(TAG, "GetCaliInfo failed before download");
        goto restart;
    }
    const size_t expected = (size_t)read_le32(&ack[4]);
    if (expected == 0 || expected > CALIBRATION_CAPACITY_BYTES) {
        ESP_LOGE(TAG, "invalid calibration size %u", (unsigned)expected);
        ret = ESP_ERR_INVALID_SIZE;
        goto restart;
    }

    ret = command_transaction(THERMAL_CMD_GET_CALI_FILE, 0, false,
                              ack, sizeof(ack), &ack_length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GetCaliFile command failed");
        goto restart;
    }

    size_t offset = 0;
    while (offset < expected) {
        size_t request = expected - offset;
        if (request > CALIBRATION_CHUNK_BYTES) {
            request = CALIBRATION_CHUNK_BYTES;
        }
        s_cal_in->num_bytes = (int)request;
        s_cal_in->actual_num_bytes = 0;
        s_cal_in->device_handle = s_device;
        s_cal_in->bEndpointAddress = THERMAL_EP_CALIB_IN;
        s_cal_in->callback = calibration_callback;
        s_cal_in->context = NULL;
        xSemaphoreTake(s_cal_done, 0);
        ret = usb_host_transfer_submit(s_cal_in);
        if (ret == ESP_OK &&
            xSemaphoreTake(s_cal_done, pdMS_TO_TICKS(60000)) != pdTRUE) {
            ESP_LOGE(TAG, "calibration transfer timeout at %u/%u", (unsigned)offset,
                     (unsigned)expected);
            ret = ESP_ERR_TIMEOUT;
        }
        if (ret != ESP_OK || s_cal_in->status != USB_TRANSFER_STATUS_COMPLETED ||
            s_cal_in->actual_num_bytes <= 0) {
            ESP_LOGE(TAG, "calibration transfer failed at %u/%u status=%d bytes=%d",
                     (unsigned)offset, (unsigned)expected, (int)s_cal_in->status,
                     s_cal_in->actual_num_bytes);
            ret = ret == ESP_OK ? ESP_ERR_INVALID_RESPONSE : ret;
            goto restart;
        }
        memcpy(&s_calibration[offset], s_cal_in->data_buffer,
               (size_t)s_cal_in->actual_num_bytes);
        offset += (size_t)s_cal_in->actual_num_bytes;
        ESP_LOGI(TAG, "calibration download %u/%u bytes", (unsigned)offset,
                 (unsigned)expected);
    }
    if (offset != expected) {
        ESP_LOGE(TAG, "calibration length mismatch: got %u expected %u",
                 (unsigned)offset, (unsigned)expected);
        ret = ESP_ERR_INVALID_SIZE;
        goto restart;
    }

    s_calibration_length = expected;
    s_calibration_downloaded = true;
    ESP_LOGI(TAG, "calibration stored in PSRAM: %u bytes", (unsigned)expected);
    ret = ESP_OK;

restart:
    {
        esp_err_t restart_ret = start_image_stream();
        if (restart_ret != ESP_OK && ret == ESP_OK) {
            ret = restart_ret;
        }
    }
    return ret;
}

const uint8_t *app_thermal_calibration_data(size_t *length)
{
    if (length) {
        *length = s_calibration_downloaded ? s_calibration_length : 0;
    }
    return s_calibration_downloaded ? s_calibration : NULL;
}

esp_err_t app_thermal_usb_probe(void)
{
    ESP_LOGI(TAG, "probe: connected=%u VID=0x%04X PID=0x%04X interface=%u endpoints=%u",
             s_device != NULL, s_vid, s_pid, s_interface, s_endpoint_count);
    if (!s_device) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t app_thermal_self_test(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t *pixels = heap_caps_malloc(THERMAL_PIXEL_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        return ESP_ERR_NO_MEM;
    }
    thermal_frame_t frame = {
        .pixels = pixels,
        .width = THERMAL_WIDTH,
        .height = THERMAL_HEIGHT,
    };

    uint32_t ids[10] = {0};
    uint8_t count = 0;
    uint32_t first_ms = 0;
    uint32_t last_ms = 0;
    uint32_t start_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (count < 10U && ((uint32_t)(esp_timer_get_time() / 1000ULL) - start_ms) < 12000U) {
        if (app_thermal_get_latest(&frame) == ESP_OK &&
            (count == 0 || frame.frame_id != ids[count - 1])) {
            if (count == 0) {
                first_ms = (uint32_t)frame.timestamp_ms;
            }
            ids[count++] = frame.frame_id;
            last_ms = (uint32_t)frame.timestamp_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    heap_caps_free(pixels);

    if (count < 10U) {
        ESP_LOGE(TAG, "self test timeout: only %u unique frames", count);
        return ESP_ERR_TIMEOUT;
    }
    uint32_t continuity_errors = 0;
    for (uint8_t i = 1; i < count; ++i) {
        if (ids[i] != ids[i - 1] + 1U && ids[i] != ids[i - 1]) {
            ++continuity_errors;
        }
    }
    uint16_t center = 0;
    pixels = heap_caps_malloc(THERMAL_PIXEL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels) {
        frame.pixels = pixels;
        if (app_thermal_get_latest(&frame) == ESP_OK) {
            center = frame.pixels[(THERMAL_HEIGHT / 2U) * THERMAL_WIDTH + (THERMAL_WIDTH / 2U)];
        }
        heap_caps_free(pixels);
    }
    float fps = last_ms > first_ms ? (9.0f * 1000.0f) / (float)(last_ms - first_ms) : 0.0f;
    ESP_LOGI(TAG, "self test: %u frames, %.2f fps, center_raw=%u, continuity_errors=%lu",
             count, fps, center, (unsigned long)continuity_errors);
    return continuity_errors == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

bool app_thermal_connected(void)
{
    return s_device != NULL;
}

uint32_t app_thermal_received_frames(void)
{
    return s_received_frames;
}

uint32_t app_thermal_dropped_frames(void)
{
    return s_dropped_frames;
}

uint32_t app_thermal_failed_frames(void)
{
    return s_failed_frames;
}
