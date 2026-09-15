#pragma once

#include <newbase/system.hpp>
#include <newbase/utility/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <memory>

namespace nb {

struct sensors_p;

// This maps to SDL_SensorType, although the count may disregard
//  additional sensor types from SDL 3 (like joycon accels and gyros)
enum class sensor_type : int32_t {
    INVALID = -1,
    UNKNOWN,
    ACCEL,
    GYRO,
    COUNT,
    PLATFORM_SPECIFIC = INT32_MAX
};

typedef uint32_t sensor_id_t;

static constexpr sensor_id_t SENSOR_ID_INVALID = 0;
static constexpr glm::vec4 SENSOR_NO_DATA {-99999.f, -99999.f, -99999.f, -99999.f};


class sensors : public system
{
public:
    sensors()  = default;
    ~sensors() override = default;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"sensors"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override;

    // API

    /**
     * Gets known sensor count
     * @return The amount of sensors known to the system
     */
    int count();

    /**
     * Tries to find a known sensor of a given type
     * @return The sensor id for the known sensor, or -1 if none was found
     */
    sensor_id_t id_find_by_type(sensor_type type);

    /**
     * Tries to open a sensor
     * @return Whether the sensor was successfully opened, or if it was already opened
     */
    bool open(sensor_id_t id);

    /**
     * Gets sensor open state
     * @return Whether the sensor is currently opened, or closed
     */
    bool is_open(sensor_id_t id);

    /**
     * Tries to close a sensor
     * @return Whether the sensor was successfully closed, or if it was already closed
     */
    bool close(sensor_id_t id);

    /**
     * Gets the current sensor data for the given ID into a glm::vec4
     * @return a vec4 filled with the latest sensor reading, zero-padded, or SENSOR_NO_DATA if sensor
     *         has no data yet, or does not exist
     */
    glm::vec4 get_data_vec4(sensor_id_t id);


    /**
     * @return How many data floats are returned .
     */
    static int type_get_data_len(sensor_type t);

    /**
     * @return A string representation of the sensor units, in SI.
     */
    static const char * type_get_unit_str(sensor_type t);

private:



    std::unique_ptr<sensors_p> _d;
};

} // namespace nb
