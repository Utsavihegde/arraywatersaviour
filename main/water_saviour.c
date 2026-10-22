#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define TANK_DEPTH_CM    200
#define MIN_DISTANCE_CM  20
#define LOW_PERCENT      20
#define FULL_PERCENT     90
#define TOTAL_READINGS   10
#define MAX_TIMEOUTS     3
#define MIN_VALID_FOR_RELAY_ON  2

typedef struct {
    int number;
    int trig;
    int echo;
    int float_pin;
    int relay;
} tank_t;

tank_t tank1 = {1, 5, 18, 4, 23};
tank_t tank2 = {2, 16, 17, 19, 27};

float get_distance(tank_t *tank)
{
    gpio_set_level(tank->trig, 1);
    esp_rom_delay_us(10);
    gpio_set_level(tank->trig, 0);

    int64_t wait_start = esp_timer_get_time();
    while (gpio_get_level(tank->echo) == 0)
        if (esp_timer_get_time() - wait_start > 20000) return -1;

    int64_t pulse_start = esp_timer_get_time();
    while (gpio_get_level(tank->echo) == 1)
        if (esp_timer_get_time() - pulse_start > 20000) return -1;

    return (esp_timer_get_time() - pulse_start) * 0.0343 / 2.0;
}

// returns 1 if the reading is good, 0 if not
int is_valid(float distance)
{
    if (distance > MIN_DISTANCE_CM && distance <= TANK_DEPTH_CM)
        return 1;
    return 0;
}

void tank_task(void *arg)
{
    tank_t *tank = (tank_t *)arg;

    gpio_reset_pin(tank->trig);       gpio_set_direction(tank->trig, GPIO_MODE_OUTPUT);
    gpio_reset_pin(tank->echo);       gpio_set_direction(tank->echo, GPIO_MODE_INPUT);
    gpio_reset_pin(tank->float_pin);  gpio_set_direction(tank->float_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(tank->float_pin, GPIO_PULLUP_ONLY);
    gpio_reset_pin(tank->relay);      gpio_set_direction(tank->relay, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(tank->relay, 0);

    if (tank->number == 2)
        vTaskDelay(1500 / portTICK_PERIOD_MS);

    printf("T%d started\n", tank->number);

    int malfunction = 0;

    while (1)
    {
        float total_distance = 0;
        int valid_count = 0;
        int timeouts_in_a_row = 0;
        malfunction = 0;

        printf("T%d ===== Taking %d readings =====\n", tank->number, TOTAL_READINGS);

        for (int reading = 1; reading <= TOTAL_READINGS; reading++)
        {
            float distance = get_distance(tank);

            if (is_valid(distance))
            {
                total_distance += distance;
                valid_count++;
                timeouts_in_a_row = 0;
                printf("T%d Reading %d/%d | Distance: %.1f cm | Water: %.1f%%\n",tank->number, reading, TOTAL_READINGS, distance, (TANK_DEPTH_CM - distance) / TANK_DEPTH_CM * 100.0);
            }
            else
            {
                timeouts_in_a_row++;
                printf("T%d Reading %d/%d | Not valid | Timeouts: %d\n",tank->number, reading, TOTAL_READINGS, timeouts_in_a_row);
            }

            // 3 timeouts in a row: relay OFF, raise malfunction, exit the loop
            if (timeouts_in_a_row >= MAX_TIMEOUTS)
            {
                malfunction = 1;
                gpio_set_level(tank->relay, 0);
                break;
            }

            // sump dry: relay OFF
            if (gpio_get_level(tank->float_pin) == 1)
                gpio_set_level(tank->relay, 0);

            vTaskDelay(3000 / portTICK_PERIOD_MS);
        }

        if (malfunction)
        {
            printf("T%d MALFUNCTION: %d timeouts in a row | RELAY: OFF | restarting cycle\n", tank->number, MAX_TIMEOUTS);
        }
        else if (valid_count == 0)
        {
            gpio_set_level(tank->relay, 0);
            printf("T%d No valid readings | RELAY: OFF\n", tank->number);
        }
        else
        {
            float average_distance = total_distance;
            if (valid_count > 1)
                average_distance = total_distance / valid_count;

            float water_percent = (TANK_DEPTH_CM - average_distance) / TANK_DEPTH_CM * 100.0;
            int relay_on = gpio_get_level(tank->relay);

            if (gpio_get_level(tank->float_pin) == 1 || water_percent >= FULL_PERCENT)
                relay_on = 0;
            else if (water_percent < LOW_PERCENT && valid_count >= MIN_VALID_FOR_RELAY_ON)
                relay_on = 1;

            gpio_set_level(tank->relay, relay_on);
            printf("T%d Water: %.1f%% | Valid: %d | RELAY: %s\n",tank->number, water_percent, valid_count, relay_on ? "ON" : "OFF");
        }

        vTaskDelay(10000 / portTICK_PERIOD_MS);   
    }
}

void app_main(void)
{
    printf("System starting\n");
    xTaskCreate(tank_task, "tank1", 4096, &tank1, 5, NULL);
    xTaskCreate(tank_task, "tank2", 4096, &tank2, 5, NULL);
}