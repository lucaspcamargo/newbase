#include <newbase/sys/sensors/sensors.hpp>
#include <newbase/log.hpp>
#include <newbase/ui/manager.hpp>
#include <newbase/engine.hpp>

// rtti
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>
#include <entt/entt.hpp>
#include <entt/meta/factory.hpp>

#include <SDL3/SDL_sensor.h>
#include <entt/locator/locator.hpp>
#include <imgui.h>
#include <unordered_map>

#ifdef ANDROID__TODO_NOT_YET
// we can use the sensor types from android in sensors of type PLATFORM_SPECIFIC
// we can store the platform-specific type of sensor in an opaque int
#include <android/sensor.h>
#endif

using namespace nb;
using entt::operator""_hs;

static sensor_type convert_sdl_sensor_type(SDL_SensorType t);

struct sensor_handle
{
    SDL_SensorID id {0};
    sensor_type type {SDL_SENSOR_UNKNOWN};
    SDL_Sensor *dev {nullptr};
    glm::vec4 data {SENSOR_NO_DATA};
    int val_len {1}; // how many values to read into data
};

struct nb::sensors_p
{
    std::unordered_map<SDL_SensorID, sensor_handle> handles;
};

SDL_InitFlags sensors::sdl_subsystems(ryml::ConstNodeRef)
{
    return SDL_INIT_SENSOR;
}

bool sensors::init(ryml::ConstNodeRef /*cfg*/) {
    log::info("[sensors] init");
    _d = std::make_unique<nb::sensors_p>();

    int count{0};
    SDL_SensorID *ids = SDL_GetSensors(&count);

    if (!ids) {
        log::warn("[sensors] failed to get sensor list!");
        return true;
    }

    if (count == 0)
        log::info("[sensors] no sensors found");

    for (int i = 0; i < count; i++) {
        const SDL_SensorType st_sdl = SDL_GetSensorTypeForID(ids[i]);
        const auto st = convert_sdl_sensor_type(st_sdl);
        log::info("[sensors] found sensor, id=%08x, type=%llu", ids[i], static_cast<uint64_t>(st));
        _d->handles.emplace(ids[i], sensor_handle{
                ids[i],
                st,
                nullptr,
                {SENSOR_NO_DATA},
                sensors::type_get_data_len(st)
        });
    }

    SDL_free(ids);

    // Tool Window
    typedef entt::locator<ui_manager *> uim_loc;
    static constexpr const char *tw_name = "Sensor Debug";
    if (uim_loc::has_value()) {
        ui_manager *uim = uim_loc::value();
        uim->register_tool_window(tw_name, [this](bool *p_open) {
            if (ImGui::Begin(tw_name, p_open)) {
                static constexpr ImGuiTableFlags flags =
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
                if (ImGui::BeginTable("SensorTable", 4, flags))
                {
                    ImGui::TableSetupColumn("ID", 0, 20);
                    ImGui::TableSetupColumn("Type", 0, 20);
                    ImGui::TableSetupColumn("Open", 0, 40);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const auto& [id, hnd] : _d->handles)
                    {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%u", id);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%u", hnd.type);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text(hnd.dev != nullptr? "open" : "");

                        ImGui::TableSetColumnIndex(3);
                        const float* ptr = glm::value_ptr(hnd.data);
                        if (hnd.data == SENSOR_NO_DATA)
                            ImGui::Text("(none)");
                        else
                            ImGui::Text("[%.2f, %.2f, %.2f, %.2f] %s",
                            ptr[0], ptr[1], ptr[2], ptr[3], sensors::type_get_unit_str(hnd.type));

                    }
                    ImGui::EndTable();

                    if(ImGui::Button("Open all sensors"))
                    {
                        for (const auto& [id, hnd] : _d->handles) {
                            if(!is_open(id) && hnd.type != sensor_type::UNKNOWN &&
                            hnd.type != sensor_type::INVALID && hnd.type != sensor_type::COUNT)
                                open(id);
                        }
                    }
                    ImGui::SameLine();
                    if(ImGui::Button("Close all sensors"))
                    {
                        for (const auto& [id, hnd] : _d->handles) {
                            if(is_open(id))
                                close(id);
                        }
                    }
                }
            }
            ImGui::End();
        });
    }
    engine::instance().debug_action_register(tw_name, []() {
        if(!uim_loc::has_value())
            return;
        if (auto *m = uim_loc::value())
            m->toggle_tool_window(tw_name);
    }, 6);

    return true; 
}

bool sensors::step(step_phase phase)
{
    if (phase == step_phase::PRE_UPDATE)
    {
        // try to get new data for every sensor
        for(auto &[id, hnd]: _d->handles)
        {
            if( hnd.dev )
            {
                // is open
                bool ret = SDL_GetSensorData(hnd.dev, glm::value_ptr(hnd.data), hnd.val_len);
            }
        }
    }

    return true;
}

bool sensors::event(SDL_Event*)
{
    // ... and nothing on the other
    return true;
}

int sensors::count()
{
    return static_cast<int>(_d->handles.size());
}

sensor_id_t sensors::id_find_by_type(sensor_type t)
{
    auto it = std::find_if(_d->handles.begin(), _d->handles.end(),
   [t](const auto &pair){
            return pair.second.type == t;
    });

    if(it == _d->handles.end())
        return SENSOR_ID_INVALID;

    return it->first;
}

bool sensors::open(sensor_id_t id)
{
    auto it = _d->handles.find(id);
    if (it == _d->handles.end())
        return false;
    if (it->second.dev)
        return true; // already open
    it->second.dev = SDL_OpenSensor(id);
    return it->second.dev != nullptr;
}

bool sensors::is_open(sensor_id_t id)
{
    auto it = _d->handles.find(id);
    if (it == _d->handles.end())
        return false;
    return _d->handles[id].dev != nullptr;
}

bool sensors::close(sensor_id_t id)
{
    auto it = _d->handles.find(id);
    if (it == _d->handles.end())
        return false;
    if (!it->second.dev)
        return true; // already closed

    // no failure state for closing it seems
    SDL_CloseSensor(it->second.dev);
    it->second.dev = nullptr;
    it->second.data = SENSOR_NO_DATA;
    return true;
}

glm::vec4 sensors::get_data_vec4(sensor_id_t id)
{
    auto it = _d->handles.find(id);
    if (it == _d->handles.end())
        return SENSOR_NO_DATA;
    return it->second.data;
}


// STATIC HELPERS

int sensors::type_get_data_len(sensor_type t)
{
    switch(t)
    {
        case sensor_type::ACCEL: [[fallthrough]];
        case sensor_type::GYRO:
            return 3;
        default:
            return 0;
    }
}

const char *sensors::type_get_unit_str(sensor_type t)
{
    switch(t)
    {
        case sensor_type::ACCEL:
            return "m/s^2";
        case sensor_type::GYRO:
            return "rad/s";
        default:
            return "";
    }
}

static sensor_type convert_sdl_sensor_type(SDL_SensorType t)
{
    const auto as_num = static_cast<int32_t>(t);
    const auto our_max = static_cast<int32_t>(sensor_type::COUNT);

    if (as_num >= our_max)
        return sensor_type::UNKNOWN;

    return static_cast<sensor_type>(t);
}


// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_sensors()
{
    // TODO enums
    // this also goes for other places but because of my API design it is important

    entt::meta_factory<nb::sensors>{}
        .type("sensors"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "sensors",
            .type_class = rtti::TYPE_CLASS_SYSTEM
        })
        .base<nb::system>()
        .func<&nb::sensors::count>("count"_hs)
        .custom<rtti::func_info>(rtti::func_info{"count"})
        .func<&nb::sensors::id_find_by_type>("id_find_by_type"_hs)
        .custom<rtti::func_info>(rtti::func_info{"id_find_by_type"})
        .func<&nb::sensors::open>("open"_hs)
        .custom<rtti::func_info>(rtti::func_info{"open"})
        .func<&nb::sensors::is_open>("is_open"_hs)
        .custom<rtti::func_info>(rtti::func_info{"is_open"})
        .func<&nb::sensors::close>("close"_hs)
        .custom<rtti::func_info>(rtti::func_info{"close"})
        .func<&nb::sensors::get_data_vec4>("get_data_vec4"_hs)
        .custom<rtti::func_info>(rtti::func_info{"get_data_vec4"});
    entt::meta_factory<std::shared_ptr<nb::sensors>>{rtti::ctx_systems()}
        .type("sensors_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::sensors>>()
        .conv<std::shared_ptr<nb::system>>();
}
