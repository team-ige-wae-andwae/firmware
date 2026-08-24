/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Get Start Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_csi_gain_ctrl.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "rom/ets_sys.h"

#include "csi_control_protocol.h"

#define CONFIG_LESS_INTERFERENCE_CHANNEL 11
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61 ||                 \
    (CONFIG_IDF_TARGET_ESP32C6 &&                                              \
     ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0))
#define CONFIG_WIFI_BAND_MODE WIFI_BAND_MODE_2G_ONLY
#define CONFIG_WIFI_2G_BANDWIDTHS WIFI_BW40
#define CONFIG_WIFI_5G_BANDWIDTHS WIFI_BW40
#define CONFIG_WIFI_2G_PROTOCOL WIFI_PROTOCOL_11N
#define CONFIG_WIFI_5G_PROTOCOL WIFI_PROTOCOL_11N
#else
#define CONFIG_WIFI_BANDWIDTH WIFI_BW40
#endif

#define CONFIG_ESP_NOW_PHYMODE WIFI_PHY_MODE_HT40
#define CONFIG_ESP_NOW_RATE WIFI_PHY_RATE_MCS0_LGI
#define CONFIG_FORCE_GAIN 0

#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
#define CSI_FORCE_LLTF 0
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 ||                  \
    CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 ||                  \
    CONFIG_IDF_TARGET_ESP32C61
#define CONFIG_GAIN_CONTROL 1
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#define ESP_IF_WIFI_STA ESP_MAC_WIFI_STA
#endif

static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x1a, 0x00, 0x00,
                                              0x00, 0x00, 0x00};
static const uint8_t ESPNOW_BROADCAST_MAC[] = {0xff, 0xff, 0xff,
                                               0xff, 0xff, 0xff};
static const char *TAG = "csi_recv";
static QueueHandle_t s_status_queue;
static SemaphoreHandle_t s_console_mutex;

#define RX_STATUS_QUEUE_DEPTH 8
#define RX_UART_LINE_MAX 128
#define RX_CONTROL_SEND_RETRIES 3

static void wifi_init() {
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_netif_init());
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

#if CONFIG_IDF_TARGET_ESP32C5
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_band_mode(CONFIG_WIFI_BAND_MODE);
  wifi_protocols_t protocols = {.ghz_2g = CONFIG_WIFI_2G_PROTOCOL,
                                .ghz_5g = CONFIG_WIFI_5G_PROTOCOL};
  ESP_ERROR_CHECK(esp_wifi_set_protocols(ESP_IF_WIFI_STA, &protocols));
  wifi_bandwidths_t bandwidth = {.ghz_2g = CONFIG_WIFI_2G_BANDWIDTHS,
                                 .ghz_5g = CONFIG_WIFI_5G_BANDWIDTHS};
  ESP_ERROR_CHECK(esp_wifi_set_bandwidths(ESP_IF_WIFI_STA, &bandwidth));
#elif (CONFIG_IDF_TARGET_ESP32C6 &&                                            \
       ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)) ||                     \
    CONFIG_IDF_TARGET_ESP32C61
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_band_mode(CONFIG_WIFI_BAND_MODE);
  wifi_protocols_t protocols = {
      .ghz_2g = CONFIG_WIFI_2G_PROTOCOL,
  };
  ESP_ERROR_CHECK(esp_wifi_set_protocols(ESP_IF_WIFI_STA, &protocols));
  wifi_bandwidths_t bandwidth = {
      .ghz_2g = CONFIG_WIFI_2G_BANDWIDTHS,
  };
  ESP_ERROR_CHECK(esp_wifi_set_bandwidths(ESP_IF_WIFI_STA, &bandwidth));
#else
  ESP_ERROR_CHECK(
      esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, CONFIG_WIFI_BANDWIDTH));
  ESP_ERROR_CHECK(esp_wifi_start());
#endif

  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#if CONFIG_IDF_TARGET_ESP32C5
  if ((CONFIG_WIFI_BAND_MODE == WIFI_BAND_MODE_2G_ONLY &&
       CONFIG_WIFI_2G_BANDWIDTHS == WIFI_BW20) ||
      (CONFIG_WIFI_BAND_MODE == WIFI_BAND_MODE_5G_ONLY &&
       CONFIG_WIFI_5G_BANDWIDTHS == WIFI_BW20)) {
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL,
                                         WIFI_SECOND_CHAN_NONE));
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL,
                                         WIFI_SECOND_CHAN_BELOW));
  }
#elif (CONFIG_IDF_TARGET_ESP32C6 &&                                            \
       ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)) ||                     \
    CONFIG_IDF_TARGET_ESP32C61
  if (CONFIG_WIFI_BAND_MODE == WIFI_BAND_MODE_2G_ONLY &&
      CONFIG_WIFI_2G_BANDWIDTHS == WIFI_BW20) {
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL,
                                         WIFI_SECOND_CHAN_NONE));
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL,
                                         WIFI_SECOND_CHAN_BELOW));
  }
#else
  if (CONFIG_WIFI_BANDWIDTH == WIFI_BW20) {
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL,
                                         WIFI_SECOND_CHAN_NONE));
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL,
                                         WIFI_SECOND_CHAN_BELOW));
  }
#endif

  /* Keep the receiver's factory MAC. The fixed 1a:00:... MAC belongs only to
   * the CSI sender; using it on both devices prevents reliable bidirectional
   * ESP-NOW control. */
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int data_len) {
  if (recv_info == NULL ||
      memcmp(recv_info->src_addr, CONFIG_CSI_SEND_MAC, 6) != 0) {
    return;
  }
#else
static void espnow_recv_cb(const uint8_t *src_addr, const uint8_t *data,
                           int data_len) {
  if (src_addr == NULL || memcmp(src_addr, CONFIG_CSI_SEND_MAC, 6) != 0) {
    return;
  }
#endif
  if (data == NULL || data_len != sizeof(csi_ctrl_packet_t)) {
    return;
  }

  csi_ctrl_packet_t response;
  memcpy(&response, data, sizeof(response));
  if (!csi_ctrl_packet_is_valid(&response, data_len) ||
      response.type != CSI_CTRL_MSG_STATUS) {
    return;
  }

  if (xQueueSend(s_status_queue, &response, 0) != pdTRUE) {
    ESP_LOGW(TAG, "status queue full, request=%" PRIu32,
             response.request_id);
  }
}

static void wifi_esp_now_init(esp_now_peer_info_t peer) {
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)"pmk1234567890123"));
  esp_now_rate_config_t rate_config = {
      .phymode = CONFIG_ESP_NOW_PHYMODE,
      .rate = CONFIG_ESP_NOW_RATE, //  WIFI_PHY_RATE_MCS0_LGI,
      .ersu = false,
      .dcm = false};
  ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  ESP_ERROR_CHECK(esp_now_set_peer_rate_config(peer.peer_addr, &rate_config));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
}

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info) {
  if (!info || !info->buf) {
    ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
    return;
  }

  if (memcmp(info->mac, CONFIG_CSI_SEND_MAC, 6)) {
    return;
  }

  /* A TX status response also produces CSI because it comes from the same
   * radio. Do not feed that control frame into the sensing dataset. ESP-NOW
   * application bytes begin at payload offset 15 in this example. */
  if (info->payload != NULL && info->rx_ctrl.sig_len >= 19) {
    uint32_t payload_magic = 0;
    memcpy(&payload_magic, info->payload + 15, sizeof(payload_magic));
    if (payload_magic == CSI_CTRL_MAGIC) {
      return;
    }
  }

  if (xSemaphoreTake(s_console_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;
  static int s_count = 0;
  float compensate_gain = 1.0f;

  uint8_t agc_gain = 0;
  int8_t fft_gain = 0;

  // Extract per-packet RX gain values.
  esp_csi_gain_ctrl_get_rx_gain(rx_ctrl, &agc_gain, &fft_gain);

#if CONFIG_GAIN_CONTROL
  static uint8_t agc_gain_baseline = 0;
  static int8_t fft_gain_baseline = 0;

  if (s_count < 100) {
    esp_csi_gain_ctrl_record_rx_gain(agc_gain, fft_gain);
  } else if (s_count == 100) {
    esp_csi_gain_ctrl_get_rx_gain_baseline(&agc_gain_baseline,
                                           &fft_gain_baseline);
#if CONFIG_FORCE_GAIN
    esp_csi_gain_ctrl_set_rx_force_gain(agc_gain_baseline, fft_gain_baseline);
    ESP_LOGD(TAG, "fft_force %d, agc_force %d", fft_gain_baseline,
             agc_gain_baseline);
#endif
  }

  esp_csi_gain_ctrl_get_gain_compensation(&compensate_gain, agc_gain, fft_gain);
#endif

  int compensate_gain_x1000000 = (int)(compensate_gain * 1000000.0f + 0.5f);

  // Console/debug log. Printed sparsely so the HTML viewer does not freeze.
  // CSV still contains gain values for every CSI_DATA row.
  if ((s_count % 100) == 0) {
    printf("GAIN,%d,%u,%d,%d\n", rx_ctrl->rssi, agc_gain, fft_gain,
           compensate_gain_x1000000);
#if CONFIG_GAIN_CONTROL
    ESP_LOGI(TAG, "compensate_gain_x1000000 %d, agc_gain %u, fft_gain %d",
             compensate_gain_x1000000, agc_gain, fft_gain);
#endif
  }

  uint32_t rx_id = *(uint32_t *)(info->payload + 15);
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 ||                  \
    CONFIG_IDF_TARGET_ESP32C61
  if (!s_count) {
    ESP_LOGI(TAG, "================ CSI RECV ================");
    ets_printf("type,seq,mac,rssi,rate,noise_floor,fft_gain,agc_gain,"
               "compensate_gain_x1000000,channel,local_timestamp,sig_len,rx_format,len,"
               "first_word,data\n");
  }

  ets_printf("CSI_DATA,%d," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
             rx_id, MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate,
             rx_ctrl->noise_floor, fft_gain, agc_gain,
             compensate_gain_x1000000, rx_ctrl->channel, rx_ctrl->timestamp,
             rx_ctrl->sig_len, rx_ctrl->cur_bb_format);
#else
  if (!s_count) {
    ESP_LOGI(TAG, "================ CSI RECV ================");
    ets_printf("type,id,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_"
               "sounding,aggregation,stbc,fec_coding,sgi,noise_floor,fft_gain,"
               "agc_gain,compensate_gain_x1000000,ampdu_cnt,channel,secondary_channel,"
               "local_timestamp,ant,sig_len,rx_format,len,first_word,data\n");
  }

  ets_printf("CSI_DATA,%d," MACSTR
             ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
             rx_id, MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate,
             rx_ctrl->sig_mode, rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing,
             rx_ctrl->not_sounding, rx_ctrl->aggregation, rx_ctrl->stbc,
             rx_ctrl->fec_coding, rx_ctrl->sgi, rx_ctrl->noise_floor, fft_gain,
             agc_gain, compensate_gain_x1000000, rx_ctrl->ampdu_cnt,
             rx_ctrl->channel, rx_ctrl->secondary_channel, rx_ctrl->timestamp,
             rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->sig_mode);

#endif
#if (CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61) && CSI_FORCE_LLTF
  int16_t csi =
      ((int16_t)(((((uint16_t)info->buf[1]) << 8) | info->buf[0]) << 4) >> 4);
  ets_printf(",%d,%d,\"[%d", (info->len - 2) / 2, info->first_word_invalid,
             (int16_t)(compensate_gain * csi));
  for (int i = 2; i < (info->len - 2); i += 2) {
    csi =
        ((int16_t)(((((uint16_t)info->buf[i + 1]) << 8) | info->buf[i]) << 4) >>
         4);
    ets_printf(",%d", (int16_t)(compensate_gain * csi));
  }
#else
  ets_printf(",%d,%d,\"[%d", info->len, info->first_word_invalid,
             (int16_t)(info->buf[0]));
  for (int i = 1; i < info->len; i++) {
    ets_printf(",%d", (int16_t)(info->buf[i]));
  }
#endif
  ets_printf("]\"\n");
  s_count++;
  xSemaphoreGive(s_console_mutex);
}

static void wifi_csi_init() {
  ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

  /**< default config */
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
  wifi_csi_config_t csi_config = {.enable = true,
                                  .acquire_csi_legacy = false,
                                  .acquire_csi_force_lltf = CSI_FORCE_LLTF,
                                  .acquire_csi_ht20 = true,
                                  .acquire_csi_ht40 = true,
                                  .acquire_csi_vht = false,
                                  .acquire_csi_su = false,
                                  .acquire_csi_mu = false,
                                  .acquire_csi_dcm = false,
                                  .acquire_csi_beamformed = false,
                                  .acquire_csi_he_stbc_mode = 2,
                                  .val_scale_cfg = 0,
                                  .dump_ack_en = false,
                                  .reserved = false};
#elif CONFIG_IDF_TARGET_ESP32C6
  wifi_csi_config_t csi_config = {.enable = true,
                                  .acquire_csi_legacy = false,
                                  .acquire_csi_ht20 = true,
                                  .acquire_csi_ht40 = true,
                                  .acquire_csi_su = true,
                                  .acquire_csi_mu = true,
                                  .acquire_csi_dcm = true,
                                  .acquire_csi_beamformed = true,
                                  .acquire_csi_he_stbc = 2,
                                  .val_scale_cfg = false,
                                  .dump_ack_en = false,
                                  .reserved = false};
#else
  wifi_csi_config_t csi_config = {
      .lltf_en = true,
      .htltf_en = true,
      .stbc_htltf2_en = true,
      .ltf_merge_en = true,
      .channel_filter_en = true,
      .manu_scale = false,
      .shift = false,
  };
#endif
  ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
  ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void console_print_status(const char *prefix,
                                 const csi_ctrl_packet_t *packet) {
  if (xSemaphoreTake(s_console_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  printf("%s,%" PRIu32 ",%" PRId32 ",%u,%d\n", prefix,
         packet->request_id, packet->status, packet->rate_hz,
         packet->tx_power_qdbm);
  fflush(stdout);
  xSemaphoreGive(s_console_mutex);
}

static void console_print_error(uint32_t request_id, esp_err_t status) {
  csi_ctrl_packet_t response = {
      .request_id = request_id,
      .rate_hz = 0,
      .tx_power_qdbm = 0,
      .status = status,
  };
  console_print_status("CSI_CTRL_ACK", &response);
}

static esp_err_t send_control_request(csi_ctrl_message_type_t type,
                                      uint32_t request_id, uint16_t rate_hz,
                                      int16_t tx_power_qdbm) {
  if (type == CSI_CTRL_MSG_SET &&
      !csi_ctrl_settings_are_valid(rate_hz, tx_power_qdbm)) {
    return ESP_ERR_INVALID_ARG;
  }

  csi_ctrl_packet_t request = {
      .magic = CSI_CTRL_MAGIC,
      .version = CSI_CTRL_VERSION,
      .type = type,
      .size = sizeof(request),
      .request_id = request_id,
      .rate_hz = rate_hz,
      .tx_power_qdbm = tx_power_qdbm,
      .status = ESP_OK,
  };
  esp_err_t err = ESP_FAIL;
  for (int attempt = 0; attempt < RX_CONTROL_SEND_RETRIES; ++attempt) {
    err = esp_now_send(ESPNOW_BROADCAST_MAC, (const uint8_t *)&request,
                       sizeof(request));
    if (err == ESP_OK) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return err;
}

static void status_output_task(void *arg) {
  (void)arg;
  csi_ctrl_packet_t response;

  while (true) {
    if (xQueueReceive(s_status_queue, &response, portMAX_DELAY) == pdTRUE) {
      console_print_status("CSI_CTRL_ACK", &response);
    }
  }
}

static void uart_command_task(void *arg) {
  (void)arg;
  char line[RX_UART_LINE_MAX];
  setvbuf(stdin, NULL, _IONBF, 0);

  while (true) {
    if (fgets(line, sizeof(line), stdin) == NULL) {
      clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    line[strcspn(line, "\r\n")] = '\0';
    uint32_t request_id = 0;
    unsigned int rate_hz = 0;
    int tx_power_qdbm = 0;
    int consumed = 0;
    csi_ctrl_message_type_t type = 0;

    int matched = sscanf(line,
                         "CSI_CTRL,SET,%" SCNu32 ",%u,%d%n",
                         &request_id, &rate_hz, &tx_power_qdbm, &consumed);
    if (matched == 3 && consumed > 0 && line[consumed] == '\0' &&
        rate_hz <= UINT16_MAX && tx_power_qdbm >= INT16_MIN &&
        tx_power_qdbm <= INT16_MAX) {
      type = CSI_CTRL_MSG_SET;
    } else {
      consumed = 0;
      matched = sscanf(line, "CSI_CTRL,GET,%" SCNu32 "%n", &request_id,
                       &consumed);
      if (matched == 1 && consumed > 0 && line[consumed] == '\0') {
        type = CSI_CTRL_MSG_GET;
      } else {
        consumed = 0;
        matched = sscanf(line, "CSI_CTRL,RESET,%" SCNu32 "%n", &request_id,
                         &consumed);
        if (matched == 1 && consumed > 0 && line[consumed] == '\0') {
          type = CSI_CTRL_MSG_RESET;
        }
      }
    }

    if (type == 0) {
      console_print_error(request_id, ESP_ERR_INVALID_ARG);
      continue;
    }

    esp_err_t err = send_control_request(type, request_id, (uint16_t)rate_hz,
                                         (int16_t)tx_power_qdbm);
    if (err != ESP_OK) {
      console_print_error(request_id, err);
      continue;
    }

    csi_ctrl_packet_t sent = {
        .request_id = request_id,
        .rate_hz = (uint16_t)rate_hz,
        .tx_power_qdbm = (int16_t)tx_power_qdbm,
        .status = ESP_OK,
    };
    console_print_status("CSI_CTRL_SENT", &sent);
  }
}

void app_main(void) {
  /**
   * @brief Initialize NVS
   */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  s_status_queue =
      xQueueCreate(RX_STATUS_QUEUE_DEPTH, sizeof(csi_ctrl_packet_t));
  ESP_ERROR_CHECK(s_status_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
  s_console_mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(s_console_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);

  /**
   * @brief Initialize Wi-Fi
   */
  wifi_init();

  /**
   * @brief Initialize ESP-NOW
   *        ESP-NOW protocol see:
   * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html
   */

  esp_now_peer_info_t peer = {
      .channel = CONFIG_LESS_INTERFERENCE_CHANNEL,
      .ifidx = WIFI_IF_STA,
      .encrypt = false,
      .peer_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
  };

  wifi_esp_now_init(peer);

  wifi_csi_init();

  BaseType_t status_task_created =
      xTaskCreate(status_output_task, "ctrl_status", 3072, NULL, 5, NULL);
  ESP_ERROR_CHECK(status_task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  BaseType_t uart_task_created =
      xTaskCreate(uart_command_task, "ctrl_uart", 4096, NULL, 5, NULL);
  ESP_ERROR_CHECK(uart_task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

  uint8_t receiver_mac[6];
  ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, receiver_mac));
  if (xSemaphoreTake(s_console_mutex, portMAX_DELAY) == pdTRUE) {
    printf("CSI_CTRL_READY,%d,%d,%d,%d,%d," MACSTR "\n", CSI_CTRL_VERSION,
           CSI_CTRL_MIN_RATE_HZ, CSI_CTRL_MAX_RATE_HZ,
           CSI_CTRL_MIN_TX_POWER_QDBM, CSI_CTRL_MAX_TX_POWER_QDBM,
           MAC2STR(receiver_mac));
    fflush(stdout);
    xSemaphoreGive(s_console_mutex);
  }
}
