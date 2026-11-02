/*
** sfnt.h
**
** This file is part of mkxp.
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

#ifndef SFNT_H
#define SFNT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

/* Minimal read-only access to the tables of an SFNT (TrueType / OpenType /
 * collection) font, enough for name lookup and Windows-style size selection.
 * SDL_ttf renders the font; this only reads table data from the same bytes.
 * All reads are bounds checked; malformed data reads as absent. */
namespace sfnt {

using Bytes = std::span<const std::uint8_t>;

inline std::optional<std::uint16_t> u16(Bytes b, std::size_t at)
{
	if (at > b.size() || b.size() - at < 2)
		return std::nullopt;
	return std::uint16_t(b[at] << 8 | b[at + 1]);
}

inline std::optional<std::int16_t> s16(Bytes b, std::size_t at)
{
	auto v = u16(b, at);
	if (!v)
		return std::nullopt;
	return std::int16_t(*v);
}

inline std::optional<std::uint32_t> u32(Bytes b, std::size_t at)
{
	auto hi = u16(b, at), lo = u16(b, at + 2);
	if (!hi || !lo)
		return std::nullopt;
	return std::uint32_t(*hi) << 16 | *lo;
}

constexpr std::uint32_t tag(const char (&t)[5])
{
	return std::uint32_t(std::uint8_t(t[0])) << 24 | std::uint32_t(std::uint8_t(t[1])) << 16 |
	       std::uint32_t(std::uint8_t(t[2])) << 8 | std::uint32_t(std::uint8_t(t[3]));
}

/* One face of an SFNT file. For a collection ('ttcf') this is the first face,
 * matching SDL_ttf / FreeType face index 0. */
class Font
{
public:
	Font() = default;

	explicit Font(Bytes file) : file(file)
	{
		std::size_t offset = 0;
		if (u32(file, 0) == tag("ttcf"))
		{
			auto first = u32(file, 12);
			auto count = u32(file, 8);
			if (!first || !count || *count == 0)
				return;
			offset = *first;
		}
		auto version = u32(file, offset);
		auto numTables = u16(file, offset + 4);
		if (!version || !numTables)
			return;
		if (*version != 0x00010000 && *version != tag("OTTO") && *version != tag("true"))
			return;
		dir = offset + 12;
		tableCount = *numTables;
		if (file.size() < dir || (file.size() - dir) / 16 < tableCount)
			tableCount = 0, dir = 0;
	}

	bool valid() const { return tableCount != 0; }

	/* Table contents, or empty if absent or out of bounds */
	Bytes table(std::uint32_t wanted) const
	{
		for (std::size_t i = 0; i < tableCount; ++i)
		{
			const std::size_t rec = dir + i * 16;
			if (u32(file, rec) != wanted)
				continue;
			auto off = u32(file, rec + 8), len = u32(file, rec + 12);
			if (!off || !len || *off > file.size() || file.size() - *off < *len)
				return {};
			return file.subspan(*off, *len);
		}
		return {};
	}

	/* Outline fonts; bitmap-only SFNTs are not scalable */
	bool scalable() const
	{
		return !table(tag("glyf")).empty() || !table(tag("CFF ")).empty() ||
		       !table(tag("CFF2")).empty();
	}

	std::optional<std::uint16_t> unitsPerEm() const
	{
		auto v = u16(table(tag("head")), 18);
		if (!v || *v == 0)
			return std::nullopt;
		return v;
	}

	struct NameRecord
	{
		std::uint16_t platformId, encodingId, languageId, nameId;
		std::string_view text;
	};

	/* Calls f(NameRecord) for every well-formed record of the 'name' table */
	template<class F>
	void forEachName(F &&f) const
	{
		const Bytes name = table(tag("name"));
		auto count = u16(name, 2), strings = u16(name, 4);
		if (!count || !strings)
			return;
		for (std::size_t i = 0; i < *count; ++i)
		{
			const std::size_t rec = 6 + i * 12;
			auto platform = u16(name, rec), encoding = u16(name, rec + 2),
			     language = u16(name, rec + 4), id = u16(name, rec + 6),
			     length = u16(name, rec + 8), offset = u16(name, rec + 10);
			if (!platform || !encoding || !language || !id || !length || !offset)
				return;
			const std::size_t start = std::size_t(*strings) + *offset;
			if (start > name.size() || name.size() - start < *length)
				continue;
			f(NameRecord{*platform, *encoding, *language, *id,
			             std::string_view(reinterpret_cast<const char *>(name.data() + start),
			                              *length)});
		}
	}

private:
	Bytes file;
	std::size_t dir = 0;
	std::size_t tableCount = 0;
};

/* FreeType's FT_MulDiv: a * b / c, rounded to nearest */
inline long mulDiv(long a, long b, long c)
{
	if (c == 0)
		return 0;
	const long long p = static_cast<long long>(a) * b;
	const long long half = (c < 0 ? -c : c) / 2;
	const long long r = (p < 0) != (c < 0) ? (p - half) / c : (p + half) / c;
	return static_cast<long>(r);
}

} // namespace sfnt

#endif // SFNT_H
