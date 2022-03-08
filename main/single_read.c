#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "driver/gpio.h"
#include "dht11.h"
#include "math.h"

#define SOIL 33
#define DHT 16
#define SOIL_ADC_CHANN ADC1_CHANNEL_5
#define DEVICEID 0
#define WEB_SERVER "example.com"
#define WEB_PORT "80"
#define WEB_PATH "/"

bool cali_enable;

char recv_buf[512];

static const char *TAG = "example";

typedef struct
{
    int deviceID;
    int soil;
    int temp;
    int humid;
} data_t;

data_t data = {
    .deviceID = DEVICEID,
    .soil = -1,
    .temp = -1,
    .humid = -1};

// ADC Calibration
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_VREF

static int adc_raw;

static esp_adc_cal_characteristics_t adc1_chars;
static esp_adc_cal_characteristics_t adc2_chars;

static bool adc_calibration_init(void)
{
    esp_err_t ret;
    bool cali_enable = false;

    ret = esp_adc_cal_check_efuse(ADC_EXAMPLE_CALI_SCHEME);
    if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        ESP_LOGW("Cal:", "Calibration scheme not supported, skip software calibration");
    }
    else if (ret == ESP_ERR_INVALID_VERSION)
    {
        ESP_LOGW("Cal:", "eFuse not burnt, skip software calibration");
    }
    else if (ret == ESP_OK)
    {
        cali_enable = true;
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
        esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc2_chars);
    }
    else
    {
        ESP_LOGE("Cal:", "Invalid arg");
    }

    return cali_enable;
}

char *callapi(char *routeApi)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s = 0, r = 0;


    int err = getaddrinfo("192.168.2.116", WEB_PORT, &hints, &res);

    if (err != 0 || res == NULL)
    {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return recv_buf;
    }

    /* Code to print the resolved IP.

       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0)
    {
        ESP_LOGE(TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return recv_buf;
    }
    ESP_LOGI(TAG, "... allocated socket");

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0)
    {
        ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return recv_buf;
    }

    ESP_LOGI(TAG, "... connected");
    freeaddrinfo(res);

    printf("%s", routeApi);
    if (write(s, routeApi, strlen(routeApi)) < 0)
    {
        ESP_LOGE(TAG, "... socket send failed");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return recv_buf;
    }
    ESP_LOGI(TAG, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                   sizeof(receiving_timeout)) < 0)
    {
        ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return recv_buf;
    }
    ESP_LOGI(TAG, "... set socket receiving timeout success");

    /* Read HTTP response */
    // do
    // {
    //     bzero(recv_buf, sizeof(recv_buf));
    //     r = read(s, recv_buf, sizeof(recv_buf) - 1);
    //     for (int i = 0; i < r; i++)
    //     {
    //         putchar(recv_buf[i]);
    //     }
    // } while (r > 0);

    bzero(recv_buf, sizeof(recv_buf));
    r = read(s, recv_buf, sizeof(recv_buf) - 1);

    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
    close(s);

    return recv_buf;
}

unsigned long int parseTime(){
    char* head = recv_buf;
    while(head - recv_buf < strlen(recv_buf) )    {
        if(*head == '\r')    {
        ++head;
            if(*head == '\n')    {
                ++head;
                if(*head == '\r')    {
                    ++head;
                    if(*head == '\n')    {
                        ++head;
                        printf("-a%s\n", head);
                        char *ptr;
                        return strtol(head, &ptr, 0);
                    }
                }
            }
        }
        ++head;
    }
    return 0;
}

void sendData(void *param)
{
    char req[128];

    while (1)
    {
        snprintf(req, 128,
                 "POST /api/device/%d/soil/%d/temp/%d/humid/%d HTTP/1.0\r\nHost: 192.168.2.116:80\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n", data.deviceID, data.soil, data.temp, data.humid);
        callapi(req);
        callapi("GET /api/time HTTP/1.0\r\nHost: 192.168.2.116:80\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n");
        printf("%s\n", recv_buf);
        
        printf("%ld\n", parseTime());

        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void sensor_read(void *param)
{
    uint32_t voltage = 0;
    while (1)
    {
        adc_raw = adc1_get_raw(SOIL_ADC_CHANN);
       // ESP_LOGI("ADC", "raw  data: %d", adc_raw);
        if (cali_enable)
        {
            voltage = esp_adc_cal_raw_to_voltage(adc_raw, &adc1_chars);
        //    ESP_LOGI("ADC", "cali data: %d mV", voltage);
            // data.soil = voltage;
            const static float b = 0.01f;
            data.soil = (int)roundf((float)data.soil + b * ((float)voltage - (float)data.soil));
            if (data.soil >= 1500)
            {
                gpio_set_level(27, 1);
            }
            else
            {
                gpio_set_level(27, 0);
            }
        }

        struct dht11_reading dht11 = DHT11_read();
        data.temp = dht11.temperature;
        data.humid = dht11.humidity;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    cali_enable = adc_calibration_init();

    gpio_config_t ioConfig = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ull << 27,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    gpio_config(&ioConfig);
    gpio_set_level(27, 0);

    // ADC1 config
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));

    ESP_ERROR_CHECK(adc1_config_channel_atten(SOIL_ADC_CHANN, ADC_ATTEN_DB_11)); // ist ein Makro //11db weil das ist max

    // WIFI connect
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    xTaskCreatePinnedToCore(&sendData, "sendData", 4096, NULL, 5, NULL, 1);

    xTaskCreate(&sensor_read, "readSoil", 4096, NULL, 10, NULL);

    DHT11_init(DHT);
}
