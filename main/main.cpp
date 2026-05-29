#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include "NimBLEDevice.h"

// Managed Components
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io_additions.h" // This contains the 3-wire SPI types
#include "Jet.hpp"
#include "ObjLoader.h"
#include <vector>
#include <math.h>
#include "driver/twai.h"
#include <map>
#include "esp_log.h"

#include <nvs_flash.h>
#include <nvs.h>

#include "CANBus_Driver.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

using namespace Renderer;

#define CAMERA_VERT_DEFAULT -240
#define CAMERA_BACK_DEFAULT 400
#define CAMERA_HORZ_DEFAULT 0

int cameraRotation = 0;
int cameraPitch = 0;
int cameraFOV = 60;
int cameraRoll = 0;

int current_arrow_bearing = 0;
int new_arrow_bearing = 0;

Scene *scene;
Camera *camera;
DirectionalLight *dirLight;
AmbientLight *ambLight;
Material *greyMaterial;
Material *greenMaterial;
Material *blueMaterial;
Material *arrowmtl;
Object *arrow;
//Object *arrow_outline;
Object *cube;
Object *plane;
Object *sphere;
uint16_t color[480 * 480];

// Declare the pointers globally, but don't initialize them yet
static void *buf1 = NULL;
static void *buf2 = NULL;

static const char *TAG = "UGAUGE";

void (*can_message_handler)(twai_message_t *message) = NULL;

// Hardware Pins
#define GPIO_BACKLIGHT    (gpio_num_t)6
#define LCD_MOSI_PIN      1
#define LCD_CLK_PIN       2

#define PIN_RESET      (1 << 0) // P1
#define PIN_POWER      (1 << 1) // P2
#define PIN_CS         (1 << 2) // P3
#define PIN_LED        (1 << 3) // P4 - Verified for LED

#define Low    0
#define High   1

#define CAN_TX_GPIO     (gpio_num_t)5
#define CAN_RX_GPIO     (gpio_num_t)4

#define CANBUS_SPEED    500000   // 500kbps

spi_device_handle_t SPI_handle = NULL;
esp_io_expander_handle_t io_expander = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;

// CONTROL VARIABLE INIT
bool receiving_data              = false; // has the first data been received
volatile bool data_ready        = false; // new incoming data
bool status_led = false;

uint16_t *framebuffer = NULL;
extern "C" const char arrow_obj_data[];
//extern "C" const char arrow_outline_obj_data[];
extern "C" const char arrow_mtl_data[];
#define SCREEN_WIDTH     480
#define SCREEN_HEIGHT    480
#define BUFFER_FACTOR   6

int Xstart = 0;
int Ystart = 0;
int Xend   = SCREEN_WIDTH;
int Yend   = SCREEN_HEIGHT;  

void setup(void);
void Drivers_Init(void);

// NVS helpers
static void save_camera_settings();
static void load_camera_settings();

// The exact init sequence from Display_ST7701.cpp
static const st7701_lcd_init_cmd_t ugb_init_code[] = {
    // --- Page 1 (Gamma) ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x07, 0x02}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}, 16, 0},
    
    // --- Page 2 (Power Management) ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x6D}, 1, 0},
    {0xB1, (uint8_t[]){0x37}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x43}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                   0x00,0x00,0x00,0x00,0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
    // --- RESTORE MISSING PANEL CONTROL BLOCKS ---
    {0xE5, (uint8_t[]){0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},

    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},

    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},

    {0xE8, (uint8_t[]){0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},

    {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    
    // --- The Critical Power Sequences (from CPP) ---
    {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},

    // --- Page 3 ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},

    // --- Page 0 (Finalize) ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x11, NULL, 0, 480}, // Sleep Out
    {0x29, NULL, 0, 120}, // Display On
};

static void send_angles() {
    twai_message_t msg = { 0 };

    /* Use 11-bit standard identifier (modify if you need extended IDs). */
    msg.identifier = (uint32_t)0x453;
    msg.extd = 0; /* standard frame */
    msg.rtr = 0;  /* data frame */
    msg.data_length_code = 4;
    msg.data[0] = cameraRotation + 30;
    msg.data[1] = cameraPitch + 50;
    msg.data[2] = cameraFOV;
    msg.data[3] = cameraRoll + 15;
    
    /* Try a short timeout for transmit; adjust as needed. */
    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (err == ESP_OK) {
        printf("CAN tx OK: ID=0x%03x DLC=%u", (unsigned)msg.identifier, (unsigned)msg.data_length_code);
        if (msg.data_length_code > 0) {
            printf(" Data:");
            for (int i = 0; i < (int)msg.data_length_code; ++i) printf(" %02x", msg.data[i]);
        }
        printf("\n");
    } else {
        printf("CAN tx FAILED: ID=0x%03x DLC=%u err=%s (0x%x)\n", (unsigned)msg.identifier, (unsigned)msg.data_length_code, esp_err_to_name(err), (unsigned)err);
    }
}

// Custom SPI Transfer specifically for UGB/ST7701
void ST7701_SPI_Send(uint8_t addr, uint8_t is_data) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = 0;
    t.cmd = static_cast<uint16_t>(is_data ? 1 : 0);
    t.addr = addr;
    t.length = 0;
    spi_device_transmit(SPI_handle, &t);
}

float easeInOutQuad(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x; // linear progress
}

// Bearing animator: ease, shortest-angle delta, and global instance
// shortest signed delta in degrees (-180..180]
static float shortestDeltaDeg(float fromDeg, float toDeg) {
    float d = fmodf((toDeg - fromDeg) + 540.0f, 360.0f) - 180.0f;
    return d;
}

struct BearingAnimator {
    float current = 0.0f;    // current displayed bearing (deg)
    float start = 0.0f;      // animation start value (deg)
    float delta = 0.0f;      // shortest delta (deg)
    float startTime = 0.0f;  // seconds
    float duration = 0.5f;   // seconds
    bool active = false;

    void setTarget(float newBearingDeg, float nowSeconds) {
        start = current;
        float target = fmodf(newBearingDeg, 360.0f);
        delta = shortestDeltaDeg(start, target);
        startTime = nowSeconds;
        duration = 0.5f;
        active = true;
    }

    void update(float nowSeconds) {
        if (!active) return;
        float t = (nowSeconds - startTime) / duration;
        if (t >= 1.0f) {
            current = fmodf(start + delta + 360.0f, 360.0f);
            active = false;
            return;
        }
        float e = easeInOutQuad(t);
        current = fmodf(start + delta * e + 360.0f, 360.0f);
    }
};

BearingAnimator arrowAnimator;

// Backlight fade controller
struct BacklightController {
    int maxDuty = 511; // 9-bit
    float current = 0.0f;
    float start = 0.0f;
    float target = 0.0f;
    float startTime = 0.0f;
    float duration = 1.0f;
    bool active = false;

    void setTarget(float tDuty, float nowSeconds, float dur) {
        start = current;
        target = tDuty;
        startTime = nowSeconds;
        duration = dur;
        active = true;
    }

    void update(float nowSeconds) {
        if (!active) return;
        float t = (nowSeconds - startTime) / duration;
        if (t >= 1.0f) {
            current = target;
            active = false;
        } else if (t <= 0.0f) {
            current = start;
        } else {
            current = start + (target - start) * t;
        }
        int duty = (int)roundf(current);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
};

static BacklightController backlightController;
static float last_ble_time = -10000.0f;
// brightness settings (percent, scaled 5..80)
static int day_brightness = 80;
static int night_brightness = 30;
static bool night_mode = false;

static int scale_in_to_10_80(int in) {
    if (in < 10) in = 10;
    if (in > 100) in = 100;
    // map [10,100] -> [5,80]
    // (out - 5) / (80 - 5) = (in - 10) / (100 - 10)
    // out = 5 + (in - 10) * 75 / 90
    return 5 + (in - 10) * 75 / 90;
}

static void save_brightness_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &h);
    if (err != ESP_OK) return;
    nvs_set_i32(h, "day_brightness", day_brightness);
    nvs_set_i32(h, "night_brightness", night_brightness);
    nvs_commit(h);
    nvs_close(h);
}

static void load_brightness_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &h);
    if (err != ESP_OK) return;
    int32_t v = 0;
    if (nvs_get_i32(h, "day_brightness", &v) == ESP_OK) day_brightness = v;
    if (nvs_get_i32(h, "night_brightness", &v) == ESP_OK) night_brightness = v;
    nvs_close(h);
}

class GaugeCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    std::string val = pChar->getValue();
    if (val.size() == 0) return;
    
    const char *cstr = val.c_str();
    float f = static_cast<float>(atof(cstr));
    //Serial.printf("BLE received string: '%s' -> %f\n", cstr, f);

    int bearing = static_cast<int>(f);
    float now = (float)esp_timer_get_time() / 1000000.0f;
    arrowAnimator.setTarget((float)bearing, now);
    // start backlight fade-in to max over 2s
    last_ble_time = now;
    // determine current target percent based on mode
    {
        int percent = night_mode ? night_brightness : day_brightness;
        float dutyTarget = (float)backlightController.maxDuty * ((float)percent / 100.0f);
        backlightController.setTarget(dutyTarget, now, 1.0f);
    }

    printf("Animating arrow from %d to %d and fading backlight in\n", current_arrow_bearing, bearing);
  }
};

void ble_init() {
  NimBLEDevice::init("NFSU2_Arrow");

  NimBLEServer *pServer = NimBLEDevice::createServer();

  // Create service and characteristic with the requested UUIDs
  NimBLEService *pService = pServer->createService("0000ff00-0000-1000-8000-00805f9b34fb");

  NimBLECharacteristic *pChar = pService->createCharacteristic(
    "0000ff01-0000-1000-8000-00805f9b34fb",
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  );

  pChar->setCallbacks(new GaugeCharacteristicCallbacks());
  pChar->setValue("");

  pService->start();

  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID("0000ff00-0000-1000-8000-00805f9b34fb");
  pAdv->enableScanResponse(true);
  pAdv->start();

  printf("BLE advertising started\n");
}

void Drivers_Init(void) {
    size_t buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT * 2) / BUFFER_FACTOR;
    buf1 = heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    buf2 = heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE("LCD", "Failed to allocate LVGL buffers in PSRAM");
        return;
    }

    // --- 1. BACKLIGHT & I2C EXPANDER SETUP ---
    gpio_set_direction(GPIO_BACKLIGHT, GPIO_MODE_OUTPUT);
    // configure LEDC PWM for backlight (start OFF)
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.duty_resolution = LEDC_TIMER_9_BIT; // increased resolution (512 steps)
    ledc_timer.freq_hz = 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_chan = {};
    ledc_chan.gpio_num = (int)GPIO_BACKLIGHT;
    ledc_chan.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_chan.channel = LEDC_CHANNEL_0;
    ledc_chan.timer_sel = LEDC_TIMER_0;
    ledc_chan.duty = 0;
    ledc_chan.hpoint = 0;
    ledc_channel_config(&ledc_chan);

    backlightController.current = 0.0f;
    backlightController.start = 0.0f;
    backlightController.target = 0.0f;

    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t i2c_bus_cfg = {};
    i2c_bus_cfg.i2c_port = 0;
    i2c_bus_cfg.scl_io_num = (gpio_num_t)7;
    i2c_bus_cfg.sda_io_num = (gpio_num_t)15;
    i2c_bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_cfg.glitch_ignore_cnt = 7;
    i2c_bus_cfg.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));

    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(i2c_bus, 0x20, &io_expander));

    uint32_t output_mask = PIN_RESET | PIN_POWER | PIN_CS | PIN_LED;
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, output_mask, IO_EXPANDER_OUTPUT));

    // --- 2. HARDWARE RESET SEQUENCE ---
    ESP_LOGI(TAG, "Hardware Resetting LCD...");
    esp_io_expander_set_level(io_expander, PIN_POWER, High);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_io_expander_set_level(io_expander, PIN_RESET, Low);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_io_expander_set_level(io_expander, PIN_RESET, High);
    vTaskDelay(pdMS_TO_TICKS(150));

    // --- 3. SPI INTERFACE SETUP
    esp_lcd_panel_io_3wire_spi_config_t io_config = {};
    io_config.line_config.scl_io_type = IO_TYPE_GPIO;
    io_config.line_config.scl_gpio_num = 2;
    io_config.line_config.sda_io_type = IO_TYPE_GPIO;
    io_config.line_config.sda_gpio_num = 1;
    io_config.line_config.cs_io_type = IO_TYPE_EXPANDER;
    io_config.line_config.cs_expander_pin = IO_EXPANDER_PIN_NUM_2;
    io_config.line_config.io_expander = io_expander;
    io_config.expect_clk_speed = 400000;
    io_config.spi_mode = 0;
    io_config.lcd_cmd_bytes = 1;
    io_config.lcd_param_bytes = 1;
    io_config.flags.use_dc_bit = 1;
    io_config.flags.dc_zero_on_data = 0;
    io_config.flags.lsb_first = 0;
    io_config.flags.cs_high_active = 0;
    io_config.flags.del_keep_cs_inactive = 1;

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_config, &io_handle));

    // --- 4. RGB PANEL TIMING ---
    esp_lcd_rgb_panel_config_t rgb_cfg = {};
    rgb_cfg.clk_src = LCD_CLK_SRC_XTAL;
    rgb_cfg.timings.pclk_hz = 16 * 1000 * 1000;
    rgb_cfg.timings.h_res = 480;
    rgb_cfg.timings.v_res = 480;
    rgb_cfg.timings.hsync_pulse_width = 8;
    rgb_cfg.timings.hsync_back_porch = 10;
    rgb_cfg.timings.hsync_front_porch = 50;
    rgb_cfg.timings.vsync_pulse_width = 3;
    rgb_cfg.timings.vsync_back_porch = 8;
    rgb_cfg.timings.vsync_front_porch = 8;
    rgb_cfg.timings.flags.hsync_idle_low = 0;
    rgb_cfg.timings.flags.vsync_idle_low = 0;
    rgb_cfg.timings.flags.de_idle_high = 0;
    rgb_cfg.timings.flags.pclk_active_neg = false;
    rgb_cfg.timings.flags.pclk_idle_high = 0;
    rgb_cfg.data_width = 16;
    rgb_cfg.bits_per_pixel = 16;
    rgb_cfg.num_fbs = 2;
    // match working project: bounce buffer = 10 * panel height
    rgb_cfg.bounce_buffer_size_px = 15 * SCREEN_HEIGHT;
    rgb_cfg.psram_trans_align = 64;
    rgb_cfg.de_gpio_num = 40;
    rgb_cfg.pclk_gpio_num = 41;
    rgb_cfg.hsync_gpio_num = 38;
    rgb_cfg.vsync_gpio_num = 39;
    // display enable pin (not used on this board)
    rgb_cfg.disp_gpio_num = -1;
    int data_pins[] = {42, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 17, 18, 8};
    for (int i = 0; i < (int)(sizeof(data_pins)/sizeof(data_pins[0])); ++i) {
        rgb_cfg.data_gpio_nums[i] = data_pins[i];
    }
    rgb_cfg.flags.disp_active_low = 0;
    rgb_cfg.flags.refresh_on_demand = 0;
    rgb_cfg.flags.fb_in_psram = 1;
    rgb_cfg.flags.double_fb = 1;
    rgb_cfg.flags.no_fb = 0;
    rgb_cfg.flags.bb_invalidate_cache = 0;

    st7701_vendor_config_t vendor_cfg = {};
    vendor_cfg.rgb_config = &rgb_cfg;
    vendor_cfg.init_cmds = ugb_init_code;
    vendor_cfg.init_cmds_size = sizeof(ugb_init_code) / sizeof(st7701_lcd_init_cmd_t);

    esp_lcd_panel_dev_config_t panel_dev_cfg = {};
    panel_dev_cfg.reset_gpio_num = -1;
    panel_dev_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_dev_cfg.bits_per_pixel = 16;
    panel_dev_cfg.vendor_config = &vendor_cfg;

    // --- 5. INITIALIZATION ---
    //esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_LOGI(TAG, "Creating ST7701 Panel...");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io_handle, &panel_dev_cfg, &panel_handle));

    ESP_LOGI(TAG, "Running Init Sequence...");
    esp_io_expander_set_level(io_expander, PIN_CS, Low);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    esp_io_expander_set_level(io_expander, PIN_CS, High);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

void Tilt_Camera(uint8_t tilt_direction) {
    ESP_LOGI(TAG, "Tilt Direction: %d", tilt_direction);
    switch (tilt_direction) {
    case 0: // Up
        cameraPitch += 1;
        break;
    case 1: // Down
        cameraPitch -= 1;
        break;
    case 2: // Left
        cameraRotation -= 1;
        break;
    case 3: // Right    
        cameraRotation += 1;
        break;
    case 4: // FOV Increase
        cameraFOV += 2;
        break;
    case 5: // FOV Decrease
        cameraFOV -= 2;
        break;
    case 6: // Roll left (nudge)
        cameraRoll -= 1;
        break;
    case 7: // Roll right (nudge)
        cameraRoll += 1;
        break;
    default:
        break;
    }

    int newHorz = CAMERA_HORZ_DEFAULT + (cameraRotation * 10);
    int newVert = CAMERA_VERT_DEFAULT + (cameraPitch * 10);
    int newFOV = cameraFOV;
    int newRoll = cameraRoll;

    ESP_LOGI(TAG, "Camera Rotation: %d, Camera Pitch: %d, Camera FOV: %d, Camera Roll: %d", newHorz, newVert, newFOV, newRoll);

    camera->setPosition(CAMERA_BACK_DEFAULT, newVert, newHorz);
    camera->setFOV(newFOV, SCREEN_WIDTH);
    scene->setCamera(camera);
    // apply roll immediately when adjusted
    camera->lookAt(Vector3(0,20,0));
    camera->rotateLocal(0, 0, (float)newRoll);
    // persist updated camera settings
    save_camera_settings();
    // wake screen / keep it on when camera is adjusted via CAN
    {
        float now = (float)esp_timer_get_time() / 1000000.0f;
        last_ble_time = now;
        int percent = night_mode ? night_brightness : day_brightness;
        float dutyTarget = (float)backlightController.maxDuty * ((float)percent / 100.0f);
        backlightController.setTarget(dutyTarget, now, 1.0f);
    }
}

// handle incoming CAN messages
void custom_can_message_handler(twai_message_t *message) {
    ESP_LOGI(TAG, "Received CAN message: ID=0x%lX DLC=%lu Data=%d", (unsigned long)(message->identifier), (unsigned long)(message->data_length_code), message->data[0]);
    switch (message->identifier) {
        case 0x454: // Camera Tilt
            Tilt_Camera(message->data[0]);
            data_ready = true;
            break;
        case 0x412: // update daytime brightness (incoming 10..100)
        {
            if (message->data_length_code > 0) {
                int in = message->data[0];
                int scaled = scale_in_to_10_80(in);
                day_brightness = scaled;
                save_brightness_settings();
                float now = (float)esp_timer_get_time() / 1000000.0f;
                if (!night_mode) {
                    float duty = (float)backlightController.maxDuty * ((float)day_brightness / 100.0f);
                    backlightController.setTarget(duty, now, 1.0f);
                    last_ble_time = now; // keep screen on for 10s so user can see new brightness
                }
            }
            break;
        }
        case 0x416: // update nighttime brightness
        {
            if (message->data_length_code > 0) {
                int in = message->data[0];
                int scaled = scale_in_to_10_80(in);
                night_brightness = scaled;
                save_brightness_settings();
                float now = (float)esp_timer_get_time() / 1000000.0f;
                if (night_mode) {
                    float duty = (float)backlightController.maxDuty * ((float)night_brightness / 100.0f);
                    backlightController.setTarget(duty, now, 1.0f);
                    last_ble_time = now; // keep screen on for 10s so user can see new brightness
                }
            }
            break;
        }
        case 0x401:
        {
            if (message->data_length_code > 1) {
                bool useNight = (message->data[1] & 0x01) != 0;
                night_mode = useNight;
                float now = (float)esp_timer_get_time() / 1000000.0f;
                int percent = night_mode ? night_brightness : day_brightness;
                float duty = (float)backlightController.maxDuty * ((float)percent / 100.0f);
                backlightController.setTarget(duty, now, 1.0f);
                last_ble_time = now; // keep screen on for 10s when mode changes
            }
            break;
        }
    default:
          break; 
  }
}

extern "C" void app_main(void) {
    setup();
    
    while (1) {
        float now = (float)esp_timer_get_time() / 1000000.0f;
        arrowAnimator.update(now);
        arrow->setRotation(0, arrowAnimator.current, 0);
        //arrow_outline->setRotation(0, arrowAnimator.current, 0);
        // backlight update
        backlightController.update(now);
        // auto fade-out after 10s of no BLE
        if ((now - last_ble_time) > 10.0f && backlightController.target > 1.0f) {
            backlightController.setTarget(0.0f, now, 1.0f);
        }
        // render
        scene->render();
        esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend, Yend, framebuffer);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup(void)  {
    ESP_LOGI(TAG, "begin");
    Drivers_Init();
    // Initialize NVS and load saved camera settings before using them
    esp_err_t _nvs_ret = nvs_flash_init();
    if (_nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || _nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        _nvs_ret = nvs_flash_init();
    }
    if (_nvs_ret == ESP_OK) {
        load_camera_settings();
        load_brightness_settings();
        // start with backlight OFF until BLE/CAN activity
        backlightController.current = 0.0f;
        backlightController.target = 0.0f;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        last_ble_time = -10000.0f;
    } else {
        ESP_LOGW(TAG, "NVS init failed: %s", esp_err_to_name(_nvs_ret));
    }
    
    can_message_handler = custom_can_message_handler;

    canbus_init();
    start_can_tasks();

    ESP_LOGI(TAG, "LED4 set OFF");
    esp_io_expander_set_level(io_expander, PIN_LED, Low);
    status_led = false;

    // Initialize framebuffer and screen dimensions
    // allocate framebuffer in DMA-capable memory to avoid DMA/cache corruption
    size_t fb_size = (size_t)SCREEN_WIDTH * (size_t)SCREEN_HEIGHT * sizeof(uint16_t);
    framebuffer = (uint16_t*)heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!framebuffer) {
        ESP_LOGW(TAG, "Failed to allocate aligned DMA framebuffer, falling back to heap_caps_malloc");
        framebuffer = (uint16_t*)heap_caps_malloc(fb_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    if (!framebuffer) {
        ESP_LOGW(TAG, "Failed to allocate DMA framebuffer, falling back to malloc");
        framebuffer = (uint16_t*)malloc(fb_size);
    }
    memset(framebuffer, 0, fb_size);
    // Create scene
    scene = new Scene(framebuffer, nullptr, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create and set camera
    camera = new Camera();
    camera->setPosition(CAMERA_BACK_DEFAULT, CAMERA_VERT_DEFAULT + (cameraPitch * 10), CAMERA_HORZ_DEFAULT + (cameraRotation * 10));
    camera->setFOV(cameraFOV, SCREEN_WIDTH);
    scene->setCamera(camera);

    // Create and set lights
    // Use a slightly warm directional light and a cool low ambient to tint
    // different faces of the grey material differently depending on orientation.
    dirLight = new DirectionalLight(Vector3(-60, -10, 0), {255, 255, 255}, 100);
    //ambLight = new AmbientLight({40, 40, 40});
    scene->setDirectionalLight(dirLight);
    //scene->setAmbientLight(ambLight);

    // Create materials
    // Mid-grey base colour (RGB565). Use a non-emissive material with moderate
    // diffuse reflectance so the directional light tints faces differently.
    greyMaterial = new Material(0x7BEF, nullptr, nullptr, false, 255, 200, 32);
    greenMaterial = new Material(0x07E0); // Green
    blueMaterial = new Material(0x001F);  // Blue
    arrowmtl = new Material(); // Red - default for faces without a material
    arrowmtl->shadingMode = ShadingMode::FLAT;

    // Create objects
    //cube = Primitives::createCube(200, 200, 200, greyMaterial);
    //scene->addObject(cube);

    std::vector<Material*> materialLibrary;

    Loader::LoadMtlData(arrow_mtl_data, &materialLibrary, nullptr);

    // Debug: list loaded materials and their properties
    printf("Loaded %zu materials:\n", materialLibrary.size());
    for (auto mat : materialLibrary) {
        printf("  name=%s emissive=%d color=0x%04x diffuse=%u specular=%u alpha=%u\n",
               mat->name ? mat->name : "(null)", mat->emissive, mat->color, mat->diffuse, mat->specular, mat->alpha);
    }

    // Pass `greyMaterial` as the default so faces without an explicit
    // `usemtl` still get a sensible material.
    arrow = Loader::LoadFromObjData(arrow_obj_data, arrowmtl, &materialLibrary, 1.0f);
    if (arrow) {
        arrow->setPosition(0, 0, 0);
        scene->addObject(arrow);
        arrow->setRotation(0, current_arrow_bearing, 0);
        arrowAnimator.current = (float)current_arrow_bearing;
    }

    // arrow_outline = Loader::LoadFromObjData(arrow_outline_obj_data, arrowmtl, &materialLibrary, 1.0f);
    // if (arrow_outline) {
    //     printf("Setting arrow outline culling mode to CULL_BACKFACES\n");
    //     arrow_outline->zBias = -1; // ensure outline renders on top of arrow
    //     arrow_outline->setPosition(0, 0, 0);
    //     scene->addObject(arrow_outline);
    //     arrow_outline->setRotation(0, current_arrow_bearing, 0);
    // }

    camera->lookAt(Vector3(0,20,0));
    camera->rotateLocal(0, 0, (float)cameraRoll);

    esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend, Yend, framebuffer);
    // Set object transformations

    send_angles();

    // Initialize BLE
    ble_init();
}

// -- NVS implementation ----------------------------------------------------
static void save_camera_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_i8(h, "cam_rot", (int8_t)cameraRotation);
    nvs_set_i8(h, "cam_pitch", (int8_t)cameraPitch);
    nvs_set_i8(h, "cam_fov", (int8_t)cameraFOV);
    nvs_set_i8(h, "cam_roll", (int8_t)cameraRoll);
    nvs_commit(h);
    nvs_close(h);
}

static void load_camera_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS open (readonly) failed: %s", esp_err_to_name(err));
        return;
    }
    int8_t val = 0;
    if (nvs_get_i8(h, "cam_rot", &val) == ESP_OK) cameraRotation = val;
    if (nvs_get_i8(h, "cam_pitch", &val) == ESP_OK) cameraPitch = val;
    if (nvs_get_i8(h, "cam_fov", &val) == ESP_OK) cameraFOV = val;
    if (nvs_get_i8(h, "cam_roll", &val) == ESP_OK) cameraRoll = val;
    nvs_close(h);
}

