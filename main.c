#include <stdio.h>
#include <stdlib.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <math.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <WS2812FX/WS2812FX.h>
#include <wifi_config.h>

#define LED_RGB_SCALE 255       // this is the scaling factor used for color conversion
#define LED_COUNT 300            // this is the number of WS2812B leds on the strip

// Global variables
float led_hue = 0;              // hue is scaled 0 to 360
float led_saturation = 100;      // saturation is scaled 0 to 100
float led_brightness = 33;     // brightness is scaled 0 to 100
bool led_on = false;            // on is boolean on or off
bool animation_on = false;            // on is boolean on or off
uint8_t current_mode_index = 0;
ws2812_pixel_t rgb = { { 0, 0, 0, 0 } };
int modes[] = {
    FX_MODE_COLOR_WIPE_RANDOM,
    FX_MODE_RANDOM_COLOR,
    FX_MODE_MULTI_DYNAMIC,
    FX_MODE_RAINBOW,
    FX_MODE_RAINBOW_CYCLE,
    FX_MODE_COMET,
    FX_MODE_SCAN,
    FX_MODE_DUAL_SCAN,
    FX_MODE_THEATER_CHASE,
    FX_MODE_SPARKLE,
    FX_MODE_MULTI_STROBE,
    FX_MODE_COLOR_SWEEP_RANDOM,
    FX_MODE_RUNNING_COLOR,
    FX_MODE_LARSON_SCANNER,
    FX_MODE_FIREWORKS,
    FX_MODE_FIREWORKS_RANDOM,
    FX_MODE_MERRY_CHRISTMAS,
    FX_MODE_DUAL_COLOR_WIPE_IN_OUT,
    FX_MODE_DUAL_COLOR_WIPE_IN_IN,
    FX_MODE_DUAL_COLOR_WIPE_OUT_OUT,
    FX_MODE_DUAL_COLOR_WIPE_OUT_IN,
    FX_MODE_HALLOWEEN
};

//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
static void hsi2rgb(float h, float s, float i, ws2812_pixel_t* rgb) {
    int r, g, b;

    while (h < 0) { h += 360.0F; };     // cycle h around to 0-360 degrees
    while (h >= 360) { h -= 360.0F; };
    h = 3.14159F*h / 180.0F;            // convert to radians.
    s /= 100.0F;                        // from percentage to ratio
    i /= 100.0F;                        // from percentage to ratio
    s = s > 0 ? (s < 1 ? s : 1) : 0;    // clamp s and i to interval [0,1]
    i = i > 0 ? (i < 1 ? i : 1) : 0;    // clamp s and i to interval [0,1]
    i = i * sqrt(i);                    // shape intensity to have finer granularity near 0

    if (h < 2.09439) {
        r = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        g = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        b = LED_RGB_SCALE * i / 3 * (1 - s);
    }
    else if (h < 4.188787) {
        h = h - 2.09439;
        g = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        b = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        r = LED_RGB_SCALE * i / 3 * (1 - s);
    }
    else {
        h = h - 4.188787;
        b = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        r = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        g = LED_RGB_SCALE * i / 3 * (1 - s);
    }

    rgb->red = (uint8_t) r;
    rgb->green = (uint8_t) g;
    rgb->blue = (uint8_t) b;
    rgb->white = (uint8_t) 0;           // white channel is not used
}

homekit_value_t led_on_get() {
    return HOMEKIT_BOOL(led_on);
}

void led_on_set(homekit_value_t value) {
    led_on = value.bool_value;

    if (led_on) {
        update_brightness();
    } else {
        WS2812FX_setBrightness(0);
    }
}

homekit_value_t animation_on_get() {
    return HOMEKIT_BOOL(animation_on);
}

void animation_on_set(homekit_value_t value) {
    animation_on = value.bool_value;

    if (animation_on) {
        current_mode_index++;

        if(current_mode_index >= sizeof(modes)) {
            current_mode_index = 0;
        }

        WS2812FX_setMode(modes[current_mode_index]);
    } else {
        WS2812FX_setMode(FX_MODE_STATIC);
    }
}

homekit_value_t led_brightness_get() {
    return HOMEKIT_INT(led_brightness);
}

void led_brightness_set(homekit_value_t value) {
    led_brightness = value.int_value;

    update_brightness();
}

homekit_value_t led_hue_get() {
    return HOMEKIT_FLOAT(led_hue);
}

void led_hue_set(homekit_value_t value) {
    led_hue = value.float_value;

    update_color();
}

homekit_value_t led_saturation_get() {
    return HOMEKIT_FLOAT(led_saturation);
}

void led_saturation_set(homekit_value_t value) {
    led_saturation = value.float_value;

    update_color();
}

void update_brightness() {
    WS2812FX_setBrightness((uint8_t) floor(led_brightness * 2.55));
}

void update_color() {
    hsi2rgb(led_hue, led_saturation, 100, &rgb);

    WS2812FX_setColor(rgb.red, rgb.green, rgb.blue);
}

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_lightbulb, .services = (homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "jonkofee LED strip"),
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "jonkofee"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "137A2BABF19D"),
            HOMEKIT_CHARACTERISTIC(MODEL, "WS2812"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1"),
            NULL
        }),
        HOMEKIT_SERVICE(LIGHTBULB, .primary = true, .characteristics = (homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "LED strip"),
            HOMEKIT_CHARACTERISTIC(
                ON, true,
            .getter = led_on_get,
            .setter = led_on_set
                ),
            HOMEKIT_CHARACTERISTIC(
                BRIGHTNESS, 100,
            .getter = led_brightness_get,
            .setter = led_brightness_set
                ),
            HOMEKIT_CHARACTERISTIC(
                HUE, 0,
            .getter = led_hue_get,
            .setter = led_hue_set
                ),
            HOMEKIT_CHARACTERISTIC(
                SATURATION, 0,
            .getter = led_saturation_get,
            .setter = led_saturation_set
                ),
            NULL
        }),
        HOMEKIT_SERVICE(LIGHTBULB, .primary = true, .characteristics = (homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Animation"),
            HOMEKIT_CHARACTERISTIC(
                ON, true,
            .getter = animation_on_get,
            .setter = animation_on_set
                ),
            NULL
        }),
        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};

void on_wifi_ready() {
    homekit_server_init(&config);
}

static void wifi_init() {
    wifi_config_init("jonkofee", NULL, on_wifi_ready);
}

void user_init(void) {
    wifi_init();
    WS2812FX_init(LED_COUNT);

    homekit_server_init(&config);
}
