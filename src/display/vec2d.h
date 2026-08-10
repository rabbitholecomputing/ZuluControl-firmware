/**
 * Copyright (c) 2025 Guy Taylor
 *
 * ZuluSCSI™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

// Ported verbatim (minus the unused std::cout print() helper and its
// <iostream>/ZuluSCSI_log.h includes, which don't apply to this bare
// pico-sdk project) from ZuluSCSI-firmware's lib/ZuluSCSI_UI_RP2MCU/Vec2D.h,
// used only by the lightspeed screen saver's particle vectors.

#pragma once

#include <cmath>

namespace zuluide::display {

struct Vec2D
{
    float x;
    float y;

    Vec2D(float _x = 0.0f, float _y = 0.0f) : x(_x), y(_y) {}
    Vec2D(const Vec2D &p1, const Vec2D &p2) : x(p2.x - p1.x), y(p2.y - p1.y) {}
    Vec2D(float x1, float y1, float x2, float y2) : x(x2 - x1), y(y2 - y1) {}

    void set(float _x, float _y)
    {
        x = _x;
        y = _y;
    }

    void set(const Vec2D &other)
    {
        x = other.x;
        y = other.y;
    }

    Vec2D operator+(const Vec2D &other) const { return Vec2D(x + other.x, y + other.y); }
    Vec2D operator-(const Vec2D &other) const { return Vec2D(x - other.x, y - other.y); }
    Vec2D operator*(float scalar) const { return Vec2D(x * scalar, y * scalar); }
    Vec2D operator/(float scalar) const { return Vec2D(x / scalar, y / scalar); }

    Vec2D &operator+=(const Vec2D &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vec2D &operator-=(const Vec2D &other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vec2D &operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vec2D &operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    float magnitude() const { return std::sqrt(x * x + y * y); }

    Vec2D normalized() const
    {
        float mag = magnitude();
        return (mag > 0) ? *this / mag : Vec2D(0, 0);
    }

    float dot(const Vec2D &other) const { return x * other.x + y * other.y; }
    float cross(const Vec2D &other) const { return x * other.y - y * other.x; }
};

}  // namespace zuluide::display
