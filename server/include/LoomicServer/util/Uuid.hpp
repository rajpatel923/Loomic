#pragma once

#include <string>

namespace Loomic {

/// Generate a UUID v4 string (e.g. "550e8400-e29b-41d4-a716-446655440000").
/// Uses a thread-local mt19937_64 seeded from std::random_device.
/// No external dependencies — <random> only.
std::string generate_uuid_v4();

} // namespace Loomic
