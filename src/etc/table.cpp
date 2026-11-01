/*
** table.cpp
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

#include "table.h"

#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <algorithm>

#include "serial-util.h"
#include "exception.h"


namespace {
constexpr size_t headerSize = 20;
constexpr size_t maxElements = (std::numeric_limits<int>::max() - headerSize) / sizeof(int16_t);

size_t checkedCount(int x, int y, int z, Exception::Type error) {
    auto invalid = [error]() -> size_t {
        throw Exception(error, error == Exception::RGSSError ? "Marshal: Table: bad file format" :
                        "Table dimensions are negative or too large");
    };
    if (x < 0 || y < 0 || z < 0) return invalid();
    if (x == 0 || y == 0 || z == 0) return 0;
    size_t count = 1;
    for (int dimension : {x, y, z}) {
        const auto extent = static_cast<size_t>(dimension);
        if (count > maxElements / extent) return invalid();
        count *= extent;
    }
    return count;
}
}

/* Init normally */
Table::Table(int x, int y /*= 1*/, int z /*= 1*/)
    : xs(x), ys(y), zs(z),
      data(checkedCount(x, y, z, Exception::ArgumentError))
{}

Table::Table(const Table &other)
    : xs(other.xs), ys(other.ys), zs(other.zs),
      data(other.data)
{}

int16_t Table::get(int x, int y, int z) const
{
	return data[xs*ys*z + xs*y + x];
}

void Table::set(int16_t value, int x, int y, int z)
{
	if (x < 0 || x >= xs
	||  y < 0 || y >= ys
	||  z < 0 || z >= zs)
	{
		return;
	}

	data[xs*ys*z + xs*y + x] = value;

	modified();
}

void Table::resize(int x, int y, int z)
{
	if (x == xs && y == ys && z == zs)
		return;

    std::vector<int16_t> newData(checkedCount(x, y, z, Exception::ArgumentError));
    const auto columns = static_cast<size_t>(std::min(x, xs));
    const int rows = std::min(y, ys), layers = std::min(z, zs);
    if (columns != 0 && rows != 0 && layers != 0) {
        const std::span<const int16_t> oldCells(data);
        std::span<int16_t> newCells(newData);
        for (int layer = 0; layer < layers; ++layer) {
            for (int row = 0; row < rows; ++row) {
                const size_t oldOffset = (static_cast<size_t>(layer) * ys + row) * xs;
                const size_t newOffset = (static_cast<size_t>(layer) * y + row) * x;
                const auto oldRow = oldCells.subspan(oldOffset, columns);
                const auto newRow = newCells.subspan(newOffset, columns);
                std::copy(oldRow.begin(), oldRow.end(), newRow.begin());
            }
        }
    }

	data.swap(newData);

	xs = x;
	ys = y;
	zs = z;

	return;
}

void Table::resize(int x, int y)
{
	resize(x, y, zs);
}

void Table::resize(int x)
{
	resize(x, ys, zs);
}

/* Serializable */
int Table::serialSize() const {
    return static_cast<int>(headerSize + data.size() * sizeof(int16_t));
}

void Table::serialize(char *buffer) const {
    if (!buffer) throw Exception(Exception::ArgumentError, "Missing Table serialization buffer");
    serialize(std::span<char>(buffer, static_cast<size_t>(serialSize())));
}

void Table::serialize(std::span<char> buffer) const {
    if (buffer.size() != static_cast<size_t>(serialSize()))
        throw Exception(Exception::ArgumentError, "Incorrect Table serialization buffer size");
    int dim = 1;
    if (ys > 1) dim = 2;
    if (zs > 1) dim = 3;
    char *header = buffer.first<headerSize>().data();
    writeInt32(&header, dim);
    writeInt32(&header, xs);
    writeInt32(&header, ys);
    writeInt32(&header, zs);
    writeInt32(&header, static_cast<int32_t>(data.size()));
    const auto payload = buffer.subspan<headerSize>();
    if constexpr (std::endian::native == std::endian::little) {
        if (!payload.empty()) std::memcpy(payload.data(), data.data(), payload.size());
    } else {
        for (size_t i = 0; i < data.size(); ++i) {
            const auto bits = serial_detail::littleEndian(std::bit_cast<uint16_t>(data[i]));
            std::memcpy(payload.data() + i * sizeof(bits), &bits, sizeof(bits));
        }
    }
}

Table *Table::deserialize(const char *data, int len) {
    if (!data || len < 0)
        throw Exception(Exception::RGSSError, "Marshal: Table: bad file format");
    return deserialize(std::span<const char>(data, static_cast<size_t>(len)));
}

Table *Table::deserialize(std::span<const char> bytes) {
    if (bytes.size() < headerSize)
        throw Exception(Exception::RGSSError, "Marshal: Table: bad file format");
    const char *header = bytes.first<headerSize>().data();
    readInt32(&header); // RGSS dimension hint remains informational.
    const int x = readInt32(&header), y = readInt32(&header), z = readInt32(&header);
    const int size = readInt32(&header);
    const auto count = checkedCount(x, y, z, Exception::RGSSError);
    const auto payload = bytes.subspan<headerSize>();
    if (size < 0 || static_cast<size_t>(size) != count || payload.size() != count * sizeof(int16_t))
        throw Exception(Exception::RGSSError, "Marshal: Table: bad file format");
    auto table = std::make_unique<Table>(x, y, z);
    if constexpr (std::endian::native == std::endian::little) {
        if (!payload.empty()) std::memcpy(table->data.data(), payload.data(), payload.size());
    } else {
        for (size_t i = 0; i < count; ++i) {
            uint16_t bits;
            std::memcpy(&bits, payload.data() + i * sizeof(bits), sizeof(bits));
            table->data[i] = std::bit_cast<int16_t>(serial_detail::littleEndian(bits));
        }
    }
    return table.release();
}
