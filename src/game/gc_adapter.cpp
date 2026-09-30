#include "gc_adapter.h"

#if ZELDA64_GC_ADAPTER

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <libusb.h>

#ifdef _WIN32
#include "SDL.h"
#else
#include "SDL2/SDL.h"
#endif

namespace {
    constexpr uint16_t adapter_vendor_id = 0x057E;
    constexpr uint16_t adapter_product_id = 0x0337;
    constexpr size_t num_ports = 4;

    // Adapter protocol.
    constexpr uint8_t cmd_start_polling = 0x13;
    constexpr uint8_t cmd_rumble = 0x11;
    constexpr uint8_t report_header = 0x21;
    constexpr size_t report_size = 37;
    constexpr size_t port_data_size = 9;

    // Controller type in the upper nibble of each port's status byte.
    constexpr uint8_t port_type_wired = 1;
    constexpr uint8_t port_type_wireless = 2;

    // Buttons in the first and second button bytes of each port.
    constexpr uint8_t btn_a = 0x01;
    constexpr uint8_t btn_b = 0x02;
    constexpr uint8_t btn_x = 0x04;
    constexpr uint8_t btn_y = 0x08;
    constexpr uint8_t btn_dpad_left = 0x10;
    constexpr uint8_t btn_dpad_right = 0x20;
    constexpr uint8_t btn_dpad_down = 0x40;
    constexpr uint8_t btn_dpad_up = 0x80;
    constexpr uint8_t btn_start = 0x01;
    constexpr uint8_t btn_z = 0x02;
    constexpr uint8_t btn_r_click = 0x04;
    constexpr uint8_t btn_l_click = 0x08;

    // Distance from the center that's treated as a full tilt for each stick. The C-stick's gate is smaller.
    constexpr float main_stick_range = 100.0f;
    constexpr float c_stick_range = 85.0f;
    // Distance past the resting position that's treated as a fully pulled analog trigger.
    constexpr float trigger_range = 190.0f;

    constexpr auto reconnect_interval = std::chrono::seconds(1);

    struct PortData {
        uint8_t status = 0;
        uint8_t buttons1 = 0;
        uint8_t buttons2 = 0;
        uint8_t stick_x = 128;
        uint8_t stick_y = 128;
        uint8_t c_x = 128;
        uint8_t c_y = 128;
        uint8_t trigger_l = 0;
        uint8_t trigger_r = 0;

        bool connected() const {
            uint8_t type = status >> 4;
            return type == port_type_wired || type == port_type_wireless;
        }
    };

    // Data shared between the adapter thread and the SDL event thread.
    std::mutex ports_mutex;
    std::array<PortData, num_ports> latest_ports{};
    // Rumble requested for each port by SDL, sent to the adapter by the adapter thread.
    std::array<std::atomic_bool, num_ports> rumble_requested{};

    std::thread adapter_thread;
    std::mutex stop_mutex;
    std::condition_variable stop_cv;
    bool stop_requested = false;

    // State of the virtual controllers, only accessed from the SDL event thread.
    struct VirtualController {
        SDL_JoystickID instance_id = -1;
        SDL_Joystick* joystick = nullptr;
        // Resting positions, read when the controller is connected.
        PortData origin{};
    };
    std::array<VirtualController, num_ports> virtual_controllers{};

    bool wait_for_stop(std::chrono::milliseconds duration) {
        std::unique_lock lock{ stop_mutex };
        return stop_cv.wait_for(lock, duration, [] { return stop_requested; });
    }

    bool should_stop() {
        std::lock_guard lock{ stop_mutex };
        return stop_requested;
    }

    void clear_ports() {
        std::lock_guard lock{ ports_mutex };
        latest_ports = {};
    }

    class AdapterConnection {
    public:
        AdapterConnection(libusb_context* ctx) : ctx(ctx) {}
        ~AdapterConnection() { close(); }

        bool open() {
            handle = libusb_open_device_with_vid_pid(ctx, adapter_vendor_id, adapter_product_id);
            if (handle == nullptr) {
                return false;
            }

            // Detach any kernel driver (Linux) so the interface can be claimed.
            libusb_set_auto_detach_kernel_driver(handle, 1);

            int ret = libusb_claim_interface(handle, 0);
            if (ret != LIBUSB_SUCCESS) {
                // Only log this once per failure streak, as it happens repeatedly while another program (e.g. Dolphin)
                // is using the adapter or if the WinUSB driver isn't installed.
                if (!claim_error_logged) {
                    fprintf(stderr, "[GC Adapter] Found the adapter but couldn't claim it: %s\n", libusb_error_name(ret));
                    claim_error_logged = true;
                }
                libusb_close(handle);
                handle = nullptr;
                return false;
            }
            claim_error_logged = false;
            interface_claimed = true;

            if (!find_endpoints()) {
                fprintf(stderr, "[GC Adapter] Couldn't find the adapter's endpoints\n");
                close();
                return false;
            }

            int transferred = 0;
            uint8_t start = cmd_start_polling;
            ret = libusb_interrupt_transfer(handle, endpoint_out, &start, 1, &transferred, 100);
            if (ret != LIBUSB_SUCCESS) {
                fprintf(stderr, "[GC Adapter] Couldn't start polling: %s\n", libusb_error_name(ret));
                close();
                return false;
            }

            sent_rumble = {};
            printf("[GC Adapter] Connected\n");
            return true;
        }

        void close() {
            if (handle == nullptr) {
                return;
            }
            // Make sure no controllers are left rumbling.
            send_rumble({});
            if (interface_claimed) {
                libusb_release_interface(handle, 0);
                interface_claimed = false;
            }
            libusb_close(handle);
            handle = nullptr;
            printf("[GC Adapter] Disconnected\n");
        }

        // Reads a report and updates the shared port data. Returns false if the adapter was lost.
        bool read() {
            uint8_t report[report_size];
            int transferred = 0;
            int ret = libusb_interrupt_transfer(handle, endpoint_in, report, sizeof(report), &transferred, 50);

            if (ret == LIBUSB_ERROR_TIMEOUT) {
                return true;
            }
            if (ret != LIBUSB_SUCCESS) {
                fprintf(stderr, "[GC Adapter] Read failed: %s\n", libusb_error_name(ret));
                return false;
            }
            if (transferred != report_size || report[0] != report_header) {
                return true;
            }

            std::lock_guard lock{ ports_mutex };
            for (size_t port = 0; port < num_ports; port++) {
                const uint8_t* data = &report[1 + port * port_data_size];
                latest_ports[port] = PortData{
                    .status = data[0],
                    .buttons1 = data[1],
                    .buttons2 = data[2],
                    .stick_x = data[3],
                    .stick_y = data[4],
                    .c_x = data[5],
                    .c_y = data[6],
                    .trigger_l = data[7],
                    .trigger_r = data[8],
                };
            }
            return true;
        }

        // Sends the requested rumble state if it changed. Returns false if the adapter was lost.
        bool update_rumble() {
            std::array<bool, num_ports> requested;
            for (size_t port = 0; port < num_ports; port++) {
                requested[port] = rumble_requested[port].load();
            }
            if (requested == sent_rumble) {
                return true;
            }
            return send_rumble(requested);
        }

    private:
        bool find_endpoints() {
            libusb_config_descriptor* config = nullptr;
            if (libusb_get_config_descriptor(libusb_get_device(handle), 0, &config) != LIBUSB_SUCCESS) {
                return false;
            }
            const libusb_interface_descriptor& iface = config->interface[0].altsetting[0];
            for (int i = 0; i < iface.bNumEndpoints; i++) {
                uint8_t address = iface.endpoint[i].bEndpointAddress;
                if (address & LIBUSB_ENDPOINT_IN) {
                    endpoint_in = address;
                }
                else {
                    endpoint_out = address;
                }
            }
            libusb_free_config_descriptor(config);
            return endpoint_in != 0 && endpoint_out != 0;
        }

        bool send_rumble(const std::array<bool, num_ports>& rumble) {
            uint8_t cmd[1 + num_ports] = { cmd_rumble };
            for (size_t port = 0; port < num_ports; port++) {
                cmd[1 + port] = rumble[port] ? 1 : 0;
            }
            int transferred = 0;
            int ret = libusb_interrupt_transfer(handle, endpoint_out, cmd, sizeof(cmd), &transferred, 16);
            if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_TIMEOUT) {
                return false;
            }
            sent_rumble = rumble;
            return true;
        }

        libusb_context* ctx;
        libusb_device_handle* handle = nullptr;
        bool interface_claimed = false;
        bool claim_error_logged = false;
        uint8_t endpoint_in = 0;
        uint8_t endpoint_out = 0;
        std::array<bool, num_ports> sent_rumble{};
    };

    void adapter_thread_func() {
        libusb_context* ctx = nullptr;
        int ret = libusb_init(&ctx);
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "[GC Adapter] Failed to initialize libusb: %s\n", libusb_error_name(ret));
            return;
        }

        {
            AdapterConnection connection{ ctx };
            while (!should_stop()) {
                // Keep trying to connect until the adapter is plugged in and available.
                if (!connection.open()) {
                    if (wait_for_stop(reconnect_interval)) {
                        break;
                    }
                    continue;
                }

                // Read reports until the adapter is disconnected or the thread is stopped.
                while (!should_stop()) {
                    if (!connection.read() || !connection.update_rumble()) {
                        break;
                    }
                }

                connection.close();
                clear_ports();
            }
        }

        libusb_exit(ctx);
    }

    int SDLCALL virtual_rumble(void* userdata, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble) {
        // The adapter only supports turning each controller's rumble motor on or off.
        size_t port = reinterpret_cast<size_t>(userdata);
        rumble_requested[port].store(low_frequency_rumble != 0 || high_frequency_rumble != 0);
        return 0;
    }

    void attach_controller(size_t port, const PortData& data) {
        static char names[num_ports][32];
        snprintf(names[port], sizeof(names[port]), "GameCube Controller (Port %zu)", port + 1);

        SDL_VirtualJoystickDesc desc{};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes = SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        desc.vendor_id = adapter_vendor_id;
        desc.product_id = adapter_product_id;
        desc.name = names[port];
        desc.userdata = reinterpret_cast<void*>(port);
        desc.Rumble = virtual_rumble;

        int device_index = SDL_JoystickAttachVirtualEx(&desc);
        if (device_index < 0) {
            fprintf(stderr, "[GC Adapter] Failed to add a controller for port %zu: %s\n", port + 1, SDL_GetError());
            return;
        }

        SDL_Joystick* joystick = SDL_JoystickOpen(device_index);
        if (joystick == nullptr) {
            fprintf(stderr, "[GC Adapter] Failed to open the controller for port %zu: %s\n", port + 1, SDL_GetError());
            SDL_JoystickDetachVirtual(device_index);
            return;
        }

        VirtualController& controller = virtual_controllers[port];
        controller.joystick = joystick;
        controller.instance_id = SDL_JoystickInstanceID(joystick);
        // The controller reports its resting stick and trigger positions when it's connected, so calibrate from them.
        controller.origin = data;
        printf("[GC Adapter] Controller connected to port %zu\n", port + 1);
    }

    void detach_controller(size_t port) {
        VirtualController& controller = virtual_controllers[port];
        if (controller.joystick == nullptr) {
            return;
        }

        SDL_JoystickClose(controller.joystick);
        // Virtual joysticks are detached by device index, so find the one for this controller.
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_JoystickGetDeviceInstanceID(i) == controller.instance_id) {
                SDL_JoystickDetachVirtual(i);
                break;
            }
        }
        controller = {};
        rumble_requested[port].store(false);
        printf("[GC Adapter] Controller disconnected from port %zu\n", port + 1);
    }

    Sint16 stick_axis(uint8_t value, uint8_t origin, float range) {
        float normalized = (static_cast<int>(value) - static_cast<int>(origin)) / range;
        return static_cast<Sint16>(SDL_clamp(normalized, -1.0f, 1.0f) * 32767.0f);
    }

    Sint16 trigger_axis(uint8_t value, uint8_t origin) {
        // SDL maps a game controller trigger's full axis range to its released and fully pulled positions.
        float normalized = (static_cast<int>(value) - static_cast<int>(origin)) / trigger_range;
        normalized = SDL_clamp(normalized, 0.0f, 1.0f);
        return static_cast<Sint16>(-32768.0f + normalized * 65535.0f);
    }

    void update_controller(const VirtualController& controller, const PortData& data) {
        SDL_Joystick* joystick = controller.joystick;
        const PortData& origin = controller.origin;

        // SDL's Y axes point down, while the controller's point up.
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, stick_axis(data.stick_x, origin.stick_x, main_stick_range));
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTY, stick_axis(origin.stick_y, data.stick_y, main_stick_range));
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTX, stick_axis(data.c_x, origin.c_x, c_stick_range));
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTY, stick_axis(origin.c_y, data.c_y, c_stick_range));
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, trigger_axis(data.trigger_l, origin.trigger_l));
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, trigger_axis(data.trigger_r, origin.trigger_r));

        auto set_button = [joystick](SDL_GameControllerButton button, bool pressed) {
            SDL_JoystickSetVirtualButton(joystick, button, pressed ? SDL_PRESSED : SDL_RELEASED);
        };
        // Face buttons are mapped by position, with the GameCube's large A button as the bottom face button.
        set_button(SDL_CONTROLLER_BUTTON_A, data.buttons1 & btn_a);
        set_button(SDL_CONTROLLER_BUTTON_X, data.buttons1 & btn_b);
        set_button(SDL_CONTROLLER_BUTTON_B, data.buttons1 & btn_x);
        set_button(SDL_CONTROLLER_BUTTON_Y, data.buttons1 & btn_y);
        set_button(SDL_CONTROLLER_BUTTON_START, data.buttons2 & btn_start);
        // Z is in the position of N64's L button on the default bindings.
        set_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, data.buttons2 & btn_z);
        set_button(SDL_CONTROLLER_BUTTON_DPAD_UP, data.buttons1 & btn_dpad_up);
        set_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, data.buttons1 & btn_dpad_down);
        set_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, data.buttons1 & btn_dpad_left);
        set_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, data.buttons1 & btn_dpad_right);
        // The digital clicks at the end of the analog triggers' travel.
        set_button(SDL_CONTROLLER_BUTTON_PADDLE1, data.buttons2 & btn_l_click);
        set_button(SDL_CONTROLLER_BUTTON_PADDLE2, data.buttons2 & btn_r_click);
    }
}

void recomp::gc_adapter::start() {
    {
        std::lock_guard lock{ stop_mutex };
        stop_requested = false;
    }
    adapter_thread = std::thread{ adapter_thread_func };
}

void recomp::gc_adapter::stop() {
    {
        std::lock_guard lock{ stop_mutex };
        stop_requested = true;
    }
    stop_cv.notify_all();
    if (adapter_thread.joinable()) {
        adapter_thread.join();
    }
}

void recomp::gc_adapter::update() {
    std::array<PortData, num_ports> ports;
    {
        std::lock_guard lock{ ports_mutex };
        ports = latest_ports;
    }

    for (size_t port = 0; port < num_ports; port++) {
        const PortData& data = ports[port];
        VirtualController& controller = virtual_controllers[port];

        if (data.connected() && controller.joystick == nullptr) {
            attach_controller(port, data);
        }
        else if (!data.connected() && controller.joystick != nullptr) {
            detach_controller(port);
        }

        if (controller.joystick != nullptr) {
            update_controller(controller, data);
        }
    }
}

#else // ZELDA64_GC_ADAPTER

// GameCube adapter support is disabled (libusb wasn't available when building).
void recomp::gc_adapter::start() {}
void recomp::gc_adapter::stop() {}
void recomp::gc_adapter::update() {}

#endif // ZELDA64_GC_ADAPTER
