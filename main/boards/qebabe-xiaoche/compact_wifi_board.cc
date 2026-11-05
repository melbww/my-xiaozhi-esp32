#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "led/circular_strip.h"
#include "mcp_server.h"
#include "system_reset.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#define TAG "QebabeXiaocheBoard"

// The vendor firmware exposes a lamp on GPIO18, but this hardware revision has
// no separate lamp: the 7-LED WS2812 strip is the only light. Wrap the strip so
// the lamp and RGB tools can override the normal status colors.
class QebabeStatusLed : public Led {
public:
    QebabeStatusLed(gpio_num_t gpio, uint16_t max_leds) : strip_(gpio, max_leds) {}

    void OnStateChanged() override {
        if (!override_active_) {
            strip_.OnStateChanged();
        }
    }

    void SetSolidColor(uint8_t red, uint8_t green, uint8_t blue) {
        override_active_ = true;
        strip_.SetAllColor(StripColor{red, green, blue});
    }

    void RestoreDefault() {
        override_active_ = false;
        strip_.OnStateChanged();
    }

private:
    CircularStrip strip_;
    bool override_active_ = false;
};

class QebabeXiaocheBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    esp_timer_handle_t motor_stop_timer_ = nullptr;
    bool lamp_on_ = false;

    static constexpr int kPwmResolutionBits = 10;
    static constexpr int kPwmMaxDuty = (1 << kPwmResolutionBits) - 1;
    static constexpr int kPwmFrequencyHz = 20000;

    static constexpr ledc_timer_t kMotorTimer = LEDC_TIMER_1;
    static constexpr ledc_channel_t kMotorLeftForward = LEDC_CHANNEL_0;
    static constexpr ledc_channel_t kMotorLeftReverse = LEDC_CHANNEL_1;
    static constexpr ledc_channel_t kMotorRightForward = LEDC_CHANNEL_2;
    static constexpr ledc_channel_t kMotorRightReverse = LEDC_CHANNEL_3;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags =
                {
                    .dc_low_on_data = 0,
                    .disable_control_phase = 0,
                },
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                   DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeMotorPwm() {
        ledc_timer_config_t timer_config = {};
        timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_config.duty_resolution = LEDC_TIMER_10_BIT;
        timer_config.timer_num = kMotorTimer;
        timer_config.freq_hz = kPwmFrequencyHz;
        timer_config.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

        const gpio_num_t motor_pins[4] = {
            MOTOR_IN1_GPIO,
            MOTOR_IN2_GPIO,
            MOTOR_IN3_GPIO,
            MOTOR_IN4_GPIO,
        };

        for (int i = 0; i < 4; ++i) {
            ledc_channel_config_t channel_config = {};
            channel_config.gpio_num = motor_pins[i];
            channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
            channel_config.channel = static_cast<ledc_channel_t>(LEDC_CHANNEL_0 + i);
            channel_config.timer_sel = kMotorTimer;
            channel_config.duty = 0;
            channel_config.hpoint = 0;
            ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
        }
    }

    static void SetMotorDuty(ledc_channel_t channel, int percent) {
        int magnitude = std::abs(percent);
        if (magnitude > 0) {
            magnitude = MOTOR_MIN_DRIVE_PERCENT + magnitude * (100 - MOTOR_MIN_DRIVE_PERCENT) / 100;
        }
        uint32_t duty = static_cast<uint32_t>(magnitude * kPwmMaxDuty / 100);
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
    }

    static void SetMotorSide(ledc_channel_t forward, ledc_channel_t reverse, int percent) {
        if (percent > 0) {
            SetMotorDuty(forward, percent);
            SetMotorDuty(reverse, 0);
        } else if (percent < 0) {
            SetMotorDuty(forward, 0);
            SetMotorDuty(reverse, -percent);
        } else {
            SetMotorDuty(forward, 0);
            SetMotorDuty(reverse, 0);
        }
    }

    void SetMotorSpeeds(int left, int right) {
        left = std::clamp(left, -100, 100);
        right = std::clamp(right, -100, 100);
        SetMotorSide(kMotorLeftForward, kMotorLeftReverse, left);
        SetMotorSide(kMotorRightForward, kMotorRightReverse, right);
    }

    void StopMotors() {
        if (motor_stop_timer_ != nullptr) {
            esp_timer_stop(motor_stop_timer_);
        }
        SetMotorSpeeds(0, 0);
    }

    void Move(int left, int right, int duration_ms) {
        SetMotorSpeeds(left, right);
        if (motor_stop_timer_ == nullptr) {
            return;
        }
        esp_timer_stop(motor_stop_timer_);
        if (duration_ms > 0) {
            ESP_ERROR_CHECK(esp_timer_start_once(motor_stop_timer_,
                                                 static_cast<uint64_t>(duration_ms) * 1000ULL));
        }
    }

    static void OnMotorStopTimer(void* arg) {
        static_cast<QebabeXiaocheBoard*>(arg)->SetMotorSpeeds(0, 0);
    }

    void InitializeMotorTimer() {
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = OnMotorStopTimer;
        timer_args.arg = this;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "motor_stop";
        timer_args.skip_unhandled_events = true;
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &motor_stop_timer_));
    }

    void InitializeMotorTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.motor.drive",
            "Drive the robot. steer: -100 (left) to 100 (right); throttle: -100 (backward) "
            "to 100 (forward); duration_ms: 0 = continuous, >0 = auto-stop.",
            PropertyList({
                Property("steer", kPropertyTypeInteger, 0, -100, 100),
                Property("throttle", kPropertyTypeInteger, 0, -100, 100),
                Property("duration_ms", kPropertyTypeInteger, 0, 0, 15000),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int steer = properties["steer"].value<int>();
                int throttle = properties["throttle"].value<int>();
                int duration_ms = properties["duration_ms"].value<int>();
                Move(throttle + steer, throttle - steer, duration_ms);
                return true;
            });

        mcp_server.AddTool("self.motor.stop", "Stop both motors immediately.", PropertyList(),
                           [this](const PropertyList&) -> ReturnValue {
                               StopMotors();
                               return true;
                           });

        auto add_move_tool = [this, &mcp_server](const char* name, const char* description,
                                                 int default_speed, int min_speed, int max_speed,
                                                 int default_duration_ms, bool reverse, bool spin) {
            mcp_server.AddTool(
                name, description,
                PropertyList({
                    Property("speed_percent", kPropertyTypeInteger, default_speed, min_speed,
                             max_speed),
                    Property("duration_ms", kPropertyTypeInteger, default_duration_ms, 0, 15000),
                }),
                [this, reverse, spin](const PropertyList& properties) -> ReturnValue {
                    int speed = properties["speed_percent"].value<int>();
                    int duration_ms = properties["duration_ms"].value<int>();
                    if (spin) {
                        Move(reverse ? speed : -speed, reverse ? -speed : speed, duration_ms);
                    } else if (reverse) {
                        Move(-speed, -speed, duration_ms);
                    } else {
                        Move(speed, speed, duration_ms);
                    }
                    return true;
                });
        };

        add_move_tool("self.motor.move_forward",
                      "Move the robot forward at the given speed for the specified duration. "
                      "Fall detection is not available on this board.",
                      100, 0, 100, 5000, false, false);
        add_move_tool("self.motor.move_backward",
                      "Move the robot backward at the given speed for the specified duration.", 100,
                      0, 100, 5000, true, false);
        add_move_tool("self.motor.spin_around",
                      "Spin the robot around at the given speed for the specified duration.", 100,
                      0, 100, 1000, false, true);
        add_move_tool("self.motor.quick_forward",
                      "Quick forward movement. Speed 60-100; fall detection is not available on "
                      "this board.",
                      100, 60, 100, 1000, false, false);
        add_move_tool("self.motor.quick_backward", "Quick backward movement. Speed 0-100.", 100, 0,
                      100, 1000, true, false);
        add_move_tool("self.motor.wiggle", "Quick wiggle/turn movement.", 100, 0, 100, 300, false,
                      true);

        mcp_server.AddTool("self.motor.dance", "Perform a quick dance movement (forward briefly).",
                           PropertyList({
                               Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                           }),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int speed = properties["speed_percent"].value<int>();
                               Move(speed, speed, 800);
                               return true;
                           });

        mcp_server.AddTool(
            "self.motor.forward",
            "Drive forward. speed: 0-100; duration_ms: 0 = continuous, >0 = auto-stop.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 1000, 0, 15000),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed"].value<int>();
                int duration_ms = properties["duration_ms"].value<int>();
                Move(speed, speed, duration_ms);
                return true;
            });

        mcp_server.AddTool(
            "self.motor.backward",
            "Drive backward. speed: 0-100; duration_ms: 0 = continuous, >0 = auto-stop.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 1000, 0, 15000),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed"].value<int>();
                int duration_ms = properties["duration_ms"].value<int>();
                Move(-speed, -speed, duration_ms);
                return true;
            });

        mcp_server.AddTool("self.motor.turn_left",
                           "Turn left in place. speed_percent: 0-100; duration_ms default 600.",
                           PropertyList({
                               Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                               Property("duration_ms", kPropertyTypeInteger, 600, 0, 15000),
                           }),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int speed = properties["speed_percent"].value<int>();
                               int duration_ms = properties["duration_ms"].value<int>();
                               Move(-speed, speed, duration_ms);
                               return true;
                           });

        mcp_server.AddTool("self.motor.turn_right",
                           "Turn right in place. speed_percent: 0-100; duration_ms default 600.",
                           PropertyList({
                               Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                               Property("duration_ms", kPropertyTypeInteger, 600, 0, 15000),
                           }),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int speed = properties["speed_percent"].value<int>();
                               int duration_ms = properties["duration_ms"].value<int>();
                               Move(speed, -speed, duration_ms);
                               return true;
                           });

        mcp_server.AddTool(
            "self.car.act",
            "Perform a preset action: forward, backward, turn_left, turn_right, spin, stop. "
            "speed: 0-100; duration_ms: 0 = continuous, >0 = auto-stop.",
            PropertyList({
                Property("action", kPropertyTypeString, std::string("forward")).SetMaxLength(32),
                Property("speed", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 1000, 0, 15000),
            }),
            [this](const PropertyList& properties) -> ToolResult {
                const std::string action = properties["action"].value<std::string>();
                int speed = properties["speed"].value<int>();
                int duration_ms = properties["duration_ms"].value<int>();

                if (action == "forward") {
                    Move(speed, speed, duration_ms);
                } else if (action == "backward") {
                    Move(-speed, -speed, duration_ms);
                } else if (action == "turn_left" || action == "spin") {
                    Move(-speed, speed, duration_ms);
                } else if (action == "turn_right") {
                    Move(speed, -speed, duration_ms);
                } else if (action == "stop") {
                    StopMotors();
                } else {
                    return std::unexpected(std::string("unknown action: ") + action);
                }
                return ReturnValue(true);
            });
    }

    QebabeStatusLed& GetStatusLed() {
        static QebabeStatusLed led(BUILTIN_LED_GPIO, BUILTIN_LED_COUNT);
        return led;
    }

    void InitializeLampGpio() {
        gpio_config_t config = {};
        config.pin_bit_mask = 1ULL << LAMP_GPIO;
        config.mode = GPIO_MODE_OUTPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&config));
        ESP_ERROR_CHECK(gpio_set_level(LAMP_GPIO, 0));
    }

    void InitializeLampTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.lamp.get_state",
                           "Get the power state of the lamp (the 7-LED WS2812 strip).",
                           PropertyList(), [this](const PropertyList&) -> ReturnValue {
                               return lamp_on_ ? "{\"power\": true}" : "{\"power\": false}";
                           });

        mcp_server.AddTool("self.lamp.turn_on", "Turn on the lamp: all 7 WS2812 LEDs solid white.",
                           PropertyList(), [this](const PropertyList&) -> ReturnValue {
                               lamp_on_ = true;
                               gpio_set_level(LAMP_GPIO, 1);
                               GetStatusLed().SetSolidColor(160, 160, 160);
                               return true;
                           });

        mcp_server.AddTool("self.lamp.turn_off", "Turn off the lamp/7-LED strip.", PropertyList(),
                           [this](const PropertyList&) -> ReturnValue {
                               lamp_on_ = false;
                               gpio_set_level(LAMP_GPIO, 0);
                               GetStatusLed().SetSolidColor(0, 0, 0);
                               return true;
                           });

        mcp_server.AddTool("self.rgb_led.set_color",
                           "Set the 7-LED strip to a solid color. r, g and b are 0-255.",
                           PropertyList({
                               Property("r", kPropertyTypeInteger, 255, 0, 255),
                               Property("g", kPropertyTypeInteger, 255, 0, 255),
                               Property("b", kPropertyTypeInteger, 255, 0, 255),
                           }),
                           [this](const PropertyList& properties) -> ReturnValue {
                               int red = properties["r"].value<int>();
                               int green = properties["g"].value<int>();
                               int blue = properties["b"].value<int>();
                               lamp_on_ = (red | green | blue) != 0;
                               GetStatusLed().SetSolidColor(red, green, blue);
                               return true;
                           });

        mcp_server.AddTool("self.rgb_led.turn_off", "Turn off the 7-LED strip.", PropertyList(),
                           [this](const PropertyList&) -> ReturnValue {
                               lamp_on_ = false;
                               GetStatusLed().SetSolidColor(0, 0, 0);
                               return true;
                           });

        mcp_server.AddTool("self.rgb_led.restore_default_idle",
                           "Restore the normal status colors on the 7-LED strip.", PropertyList(),
                           [this](const PropertyList&) -> ReturnValue {
                               lamp_on_ = false;
                               GetStatusLed().RestoreDefault();
                               return true;
                           });
    }

    void InitializeTools() {
        InitializeLampGpio();
        InitializeLampTools();
        InitializeMotorTools();
    }

public:
    QebabeXiaocheBoard()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeMotorTimer();
        InitializeMotorPwm();
        StopMotors();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override { return &GetStatusLed(); }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
                                               AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(QebabeXiaocheBoard);
