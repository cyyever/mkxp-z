/*
** serial-util.h
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SERIALUTIL_H
#define SERIALUTIL_H

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>

namespace serial_detail {
static_assert(std::endian::native == std::endian::little ||
              std::endian::native == std::endian::big,
              "RGSS serialization requires uniform byte order");
static_assert(sizeof(double) == sizeof(uint64_t) && std::numeric_limits<double>::is_iec559,
              "RGSS serialization requires IEEE 754 binary64 doubles");

template<std::endian Order = std::endian::native, std::unsigned_integral T>
constexpr T littleEndian(T value) {
    if constexpr (Order == std::endian::big)
        return std::byteswap(value);
    else
        return value;
}
}

static inline int32_t readInt32(const char **dataP) {
    uint32_t bits;
    std::memcpy(&bits, *dataP, sizeof(bits));
    *dataP += sizeof(bits);
    return std::bit_cast<int32_t>(serial_detail::littleEndian(bits));
}

static inline double readDouble(const char **dataP) {
    uint64_t bits;
    std::memcpy(&bits, *dataP, sizeof(bits));
    *dataP += sizeof(bits);
    return std::bit_cast<double>(serial_detail::littleEndian(bits));
}

static inline void writeInt32(char **dataP, int32_t value) {
    const auto bits = serial_detail::littleEndian(std::bit_cast<uint32_t>(value));
    std::memcpy(*dataP, &bits, sizeof(bits));
    *dataP += sizeof(bits);
}

static inline void writeDouble(char **dataP, double value) {
    const auto bits = serial_detail::littleEndian(std::bit_cast<uint64_t>(value));
    std::memcpy(*dataP, &bits, sizeof(bits));
    *dataP += sizeof(bits);
}

#endif // SERIALUTIL_H
