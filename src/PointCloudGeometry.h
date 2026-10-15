#pragma once

#include "MappingTypes.h"
#include <algorithm>
#include <array>
#include <cmath>

struct CloudBounds {
    QVector3D low;
    QVector3D high;
    bool valid = false;
};

// Use coordinate percentiles so occasional distant returns do not flatten the
// useful height palette or make the camera fit mostly empty space. Small scans
// retain their exact extent, matching the interactive camera's fit behavior.
inline CloudBounds cloudBounds(const QVector<MapPoint>& points, double trimFraction = 0.01)
{
    std::array<QVector<float>, 3> coordinates;
    for (auto& axis : coordinates) axis.reserve(points.size());
    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) continue;
        coordinates[0].append(point.x);
        coordinates[1].append(point.y);
        coordinates[2].append(point.z);
    }

    CloudBounds bounds;
    if (coordinates[0].isEmpty()) return bounds;
    if (!std::isfinite(trimFraction)) trimFraction = 0.01;
    trimFraction = std::clamp(trimFraction, 0.0, 0.49);
    const int count = coordinates[0].size();
    const int trim = count >= 1000 ? int(count * trimFraction) : 0;
    for (int axis = 0; axis < 3; ++axis) {
        auto& values = coordinates[axis];
        const auto lower = values.begin() + trim;
        const auto upper = values.begin() + (count - 1 - trim);
        std::nth_element(values.begin(), lower, values.end());
        bounds.low[axis] = *lower;
        // The lower order statistic is already fixed; partition only the tail.
        if (upper != lower) std::nth_element(lower + 1, upper, values.end());
        bounds.high[axis] = *upper;
    }
    bounds.valid = true;
    return bounds;
}
