#include "Flask.h"

namespace Flask::Protocol
{
::Vector to_engine_vector(const Protocol::Vector& vector) { return {vector.x(), vector.y(), vector.z()}; }
Protocol::Vector* from_engine_vector_to_allocated(const ::Vector& engine_vector)
{
    auto vector = new Protocol::Vector;

    vector->set_x(engine_vector.x);
    vector->set_y(engine_vector.y);
    vector->set_z(engine_vector.z);

    return vector;
}

::QAngle to_engine_angle(const Protocol::Vector& vector) { return {vector.x(), vector.y(), vector.z()}; }
Protocol::Vector* from_engine_angle_to_allocated(const QAngle& angle)
{
    auto vector = new Protocol::Vector;

    vector->set_x(angle.x);
    vector->set_y(angle.y);
    vector->set_z(angle.z);

    return vector;
}
}