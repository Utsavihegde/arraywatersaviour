#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define DEPTH 100.0     // change to your tank depth in cm
#define LOW   50
#define FULL  90

int tank1[5] = {1, 5, 18, 4, 23};     // id, trig, echo, float, relay
int tank2[5] = {2, 16, 17, 19, 27};

volatile float avg[2] = {100, 100};   // latest average water %, 100 = relay stays OFF until first average
volatile int timeouts[2] = {0, 0};    // consecutive failed readings

float get_distance(int trig, int echo)
{
    gpio_set_level(trig, 1);
    esp_rom_delay_us(10);
    gpio_set_level(trig, 0);

    int64_t t = esp_timer_get_time();
    while (gpio_get_level(echo) == 0)
        if (esp_timer_get_time() - t > 20000) return -1;

    int64_t start = esp_timer_get_time();
    while (gpio_get_level(echo) == 1)
        if (esp_timer_get_time() - start > 20000) return -1;

    return (esp_timer_get_time() - start) * 0.0343 / 2.0;
}

// highest priority: decides the relay
void logic_task(void *arg)
{
    int *p = (int *)arg;
    int id = p[0], fl = p[3], relay = p[4];

    vTaskDelay(pdMS_TO_TICKS(1000));   // wait for tank_task to set up the pins

    while (1)
    {
        int on = gpio_get_level(relay);

        if (gpio_get_level(fl) == 1 || timeouts[id - 1] >= 3 || avg[id - 1] >= FULL)
            on = 0;
        else if (avg[id - 1] < LOW && gpio_get_level(fl) == 0)
            on = 1;

        gpio_set_level(relay, on);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// lower priority: only measures
void tank_task(void *arg)
{
    int *p = (int *)arg;
    int id = p[0], trig = p[1], echo = p[2], fl = p[3], relay = p[4];

    gpio_reset_pin(trig);  gpio_set_direction(trig, GPIO_MODE_OUTPUT);
    gpio_reset_pin(echo);  gpio_set_direction(echo, GPIO_MODE_INPUT);
    gpio_reset_pin(fl);    gpio_set_direction(fl, GPIO_MODE_INPUT);
    gpio_set_pull_mode(fl, GPIO_PULLUP_ONLY);
    gpio_reset_pin(relay); gpio_set_direction(relay, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(relay, 0);

    if (id == 2) vTaskDelay(pdMS_TO_TICKS(1500));
    printf("started, taking values...\n"); 
    while (1)
    {
        printf(" ===== Taking 10 readings =====\n");
        float sum = 0;
        int valid = 0;
      //  int gap = gpio_get_level(relay) ? 2000 : 3000;  

        for (int i = 0; i < 10; i++)
{
    float d = get_distance(trig, echo);
    if (d > 20 && d <= DEPTH)
    {
        sum += d;
        valid++;
        timeouts[id - 1] = 0;
        printf("T%d Reading %d/10 | Distance: %.1f cm | Water: %.1f%%\n",
               id, i + 1, d, (DEPTH - d) / DEPTH * 100.0);
    }
    else
    {
        timeouts[id - 1]++;
        printf("T%d Reading %d/10 | Not valid\n", id, i + 1);
        if (timeouts[id - 1] >= 3)
    printf("T%d WARNING: %d timeouts in a row | RELAY forced OFF\n", id, timeouts[id - 1]);
    }

    vTaskDelay(7000 / portTICK_PERIOD_MS);
}

        if (valid == 0)
        {
            avg[id - 1] = 100;     // no average, relay stays OFF
            printf("T%d No valid reading\n", id);
            continue;
        }

        avg[id - 1] = (DEPTH - sum / valid) / DEPTH * 100.0;
        printf("T%d Water: %.1f%% | Valid: %d | Timeouts: %d | Sump: %d | RELAY: %s\n",
               id, avg[id - 1], valid, timeouts[id - 1], !gpio_get_level(fl), gpio_get_level(relay) ? "ON" : "OFF");
               vTaskDelay(10000 / portTICK_PERIOD_MS);  
    }
}

void app_main(void)
{
    printf("System starting.\n");
    xTaskCreate(tank_task,  "tank1",  4096, tank1, 4, NULL);
    xTaskCreate(tank_task,  "tank2",  4096, tank2, 4, NULL);
    xTaskCreate(logic_task, "logic1", 2048, tank1, 6, NULL);
    xTaskCreate(logic_task, "logic2", 2048, tank2, 6, NULL);
}