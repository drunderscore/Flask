#pragma once

#undef clamp
#include "flask.pb.h"

#include <mathlib/vector.h>

// In C++, we use pascal-case namespaces, but protobuf style guide says all lowercase. We follow the style in .proto
// files, but make it consistent in C++.
namespace Flask::Protocol
{
using namespace flask::protocol;

::Vector to_engine_vector(const Protocol::Vector& vector);
Protocol::Vector* from_engine_vector_to_allocated(const ::Vector& engine_vector);

::QAngle to_engine_angle(const Protocol::Vector& vector);
Protocol::Vector* from_engine_angle_to_allocated(const QAngle& angle);
}