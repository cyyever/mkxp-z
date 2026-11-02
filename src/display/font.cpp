/*
** font.cpp
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

#include "font.h"

#include "sharedstate.h"
#include "filesystem.h"
#include "exception.h"
#include <map>
#include "util.h"
#include "config.h"
#include "encoding.h"
#include "util/font-encoding.h"

#include "debugwriter.h"

#include <string>
#include <utility>
#include <algorithm>
#include <cctype>
#include <array>
#include <unordered_map>

#include <SDL3_ttf/SDL_ttf.h>
#include <memory>
#include <optional>
#include <tuple>

#include "sfnt.h"

#ifndef MKXPZ_CJK_FONT
#include "liberation.ttf.xxd"
#else
#include "wqymicrohei.ttf.xxd"
#endif


#ifndef MKXPZ_CJK_FONT
#define BUNDLED_FONT liberation
#else
#define BUNDLED_FONT wqymicrohei
#endif

#define BUNDLED_FONT_D(f) mkxp_assets_## f ##_ttf
#define BUNDLED_FONT_L(f) mkxp_assets_## f ##_ttf_len

// Go fuck yourself CPP
#define BNDL_F_D(f) BUNDLED_FONT_D(f)
#define BNDL_F_L(f) BUNDLED_FONT_L(f)

// SDL_ttf's font handle is opaque. Read the SFNT tables needed for name
// lookup and Windows-style size selection directly from the same bytes.
namespace {
struct FontMetrics {
    std::unique_ptr<void, decltype(&SDL_free)> data{nullptr, SDL_free};
    size_t size = 0;
    sfnt::Font sfnt; // invalid for non-SFNT fonts SDL_ttf can still open
};
} // namespace

static std::unique_ptr<FontMetrics> loadFontMetrics(SDL_IOStream *stream) {
    auto metrics = std::make_unique<FontMetrics>();
    const Sint64 offset = SDL_TellIO(stream);
    if (offset < 0)
        return nullptr;
    size_t size = 0;
    metrics->data.reset(SDL_LoadFile_IO(stream, &size, false));
    const auto status = SDL_GetIOStatus(stream);
    const bool restored = SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset;
    if (!metrics->data || !restored ||
        (status != SDL_IO_STATUS_READY && status != SDL_IO_STATUS_EOF))
        return nullptr;
    metrics->size = size;
    metrics->sfnt = sfnt::Font({static_cast<const std::uint8_t *>(metrics->data.get()), size});
    return metrics;
}

static SDL_IOStream *openBundledFont()
{
    return SDL_IOFromConstMem(BNDL_F_D(BUNDLED_FONT), BNDL_F_L(BUNDLED_FONT));
}


/* <name, size> */
typedef std::pair<std::string, int> FontSizeKey;
/* <name, ppem> */
typedef std::pair<std::string, int> FontPPEMKey;

namespace {
struct FontSet
{
	/* 'Regular' style */
	std::string regular;

	/* Any other styles (used in case no 'Regular' exists) */
	std::string other;

	/* 'Regular' style obtained via SFNT */
	std::string sfnt_regular;

	/* Any other styles obtained via SFNT */
	std::string sfnt_other;

	const std::string *operator->() const noexcept
	{
		if (!sfnt_regular.empty())
			return &sfnt_regular;
		if (!sfnt_other.empty())
			return &sfnt_other;
		if (!regular.empty())
			return &regular;
		return &other;
	}
};
} // namespace

struct SharedFontStatePrivate
{
	/* Maps: font family name, To: substituted family name,
	 * as specified via configuration file / arguments */
	std::map<std::string, std::string> subs;

	/* Maps: font family name, To: set of physical
	 * font filenames located in "Fonts/" */
	std::map<std::string, FontSet> sets;

	/* Pool of font size to ppem values */
	std::map<FontSizeKey, int> size_to_ppem;

	/* Pool of already opened fonts; once opened, they are reused
	 * and never closed until the termination of the program */
	std::map<FontPPEMKey, TTF_Font*> ppem_to_font;
    
    /* Internal default font family that is used anytime an
     * empty/invalid family is requested */
    std::string defaultFamily;

	float fontScale;
	bool fontKerning;
	int fontHinting;
};

SharedFontState::SharedFontState(const Config &conf)
    : p(std::make_unique<SharedFontStatePrivate>())
{

	/* Parse font substitutions */
	for (size_t i = 0; i < conf.fontSubs.size(); ++i)
	{
		const std::string &raw = conf.fontSubs[i];
		size_t sepPos = raw.find_first_of('>');

		if (sepPos == std::string::npos)
			continue;

		std::string from = raw.substr(0, sepPos);
		std::string to   = raw.substr(sepPos+1);

		p->subs.insert_or_assign(from, to);
	}
	
	p->fontScale = conf.fontScale;
	if (p->fontScale < 0.1f)
	{
		p->fontScale = 1.0f;
	}
	p->fontKerning = conf.fontKerning;
	p->fontHinting = conf.fontHinting;
}

SharedFontState::~SharedFontState()
{
	for (const auto &[key, font] : p->ppem_to_font)
		if (font)
			TTF_CloseFont(font);

}

static std::string decodeSfntName(const sfnt::Font::NameRecord &aname)
{
    constexpr std::uint16_t appleUnicode = 0, microsoft = 3, msUnicodeBmp = 1, msUcs4 = 10;
    const std::string_view text = aname.text;
    try {
        if ((aname.platformId == microsoft && aname.encodingId == msUnicodeBmp)
            || aname.platformId == appleUnicode)
            return FontEncoding::decodeName(text);
        if (aname.platformId == microsoft && aname.encodingId == msUcs4)
            return FontEncoding::decodeName(text, true);
        return Encoding::convertString(text);
    } catch (const Exception &) {
        return {}; // Ignore malformed names; other records still identify the font.
    }
}

void SharedFontState::initFontSetCB(SDL_IOStream *ops,
                                    const std::string &filename)
{
	auto metrics = loadFontMetrics(ops);
    if (!metrics)
        return;
    // SDL_ttf reports the same family/style names FreeType derives for a face.
    TTF_Font *ttf = TTF_OpenFontIO(SDL_IOFromConstMem(metrics->data.get(), metrics->size), true, 1.0f);
    if (!ttf)
        return;
    const char *familyName = TTF_GetFontFamilyName(ttf);
    const char *styleName = TTF_GetFontStyleName(ttf);
    std::string family = familyName ? familyName : "";
    std::string style = styleName ? styleName : "";
    TTF_CloseFont(ttf);

	std::transform(family.begin(), family.end(), family.begin(),
		[](unsigned char c){ return std::tolower(c); });

	FontSet &set = p->sets[family];

	if (style == "Regular" && set.regular.empty())
		set.regular = filename;
	else if (style != "Regular" && set.other.empty())
		set.other = filename;


	if (metrics->sfnt.valid())
	{
		constexpr std::uint16_t nameFamily = 1, nameSubfamily = 2;
		std::unordered_map<uint32_t, std::pair<std::string, std::string>> name_map;

		metrics->sfnt.forEachName([&](const sfnt::Font::NameRecord &aname)
		{
			uint32_t key = aname.platformId;
			key <<= 16;
			key |= aname.languageId;
			if (aname.nameId == nameFamily)
				name_map[key].first = decodeSfntName(aname);
			else if (aname.nameId == nameSubfamily)
				name_map[key].second = decodeSfntName(aname);
		});

		for (const auto &entry : name_map)
		{
			const std::string &sfnt_family_raw = entry.second.first;
			const std::string &sfnt_style = entry.second.second;
			if (sfnt_family_raw.empty())
				continue;

			std::string sfnt_family(sfnt_family_raw);

			std::transform(sfnt_family.begin(), sfnt_family.end(), sfnt_family.begin(),
				[](unsigned char c){ return std::tolower(c); });

			FontSet &set = p->sets[sfnt_family];

			if (sfnt_style == "Regular" && set.sfnt_regular.empty())
				set.sfnt_regular = filename;
			else if (sfnt_style != "Regular" && set.sfnt_other.empty())
				set.sfnt_other = filename;
		}
	}

}

// https://github.com/wine-mirror/wine/blob/dc34fef45d491516fa8eaee45b2ae40faa7b0bfe/dlls/win32u/freetype.c

/* The following code was derived from Wine to emulate
 * Windows's font size selection behavior. */
 
/* We're not currently using yMax and yMin for anything,
 * but it could be useful later. */
namespace {
typedef struct {
	const sfnt::Font *sfnt;
	int ppem;
	short yMax;
	short yMin;
} Font_Container;
} // namespace

static int load_VDMX(Font_Container *font, int height)
{
	const sfnt::Bytes vdmx = font->sfnt->table(sfnt::tag("VDMX"));
	/* Header: version, numRecs, numRatios; then 4-byte ratio records and
	 * one 16-bit group offset per ratio. */
	const auto numRatios = sfnt::u16(vdmx, 4);
	if (!numRatios) /* no vdmx table present, use linear scaling */
		return 0;

	/* FIXME: need the real device aspect ratio */
	const uint8_t devXRatio = 1, devYRatio = 1;

	std::optional<size_t> offset;
	for (size_t i = 0; i < *numRatios; ++i) {
		const size_t ratio = 6 + i * 4;
		if (ratio + 4 > vdmx.size())
			return 0;
		const uint8_t bCharSet = vdmx[ratio], xRatio = vdmx[ratio + 1],
		              yStartRatio = vdmx[ratio + 2], yEndRatio = vdmx[ratio + 3];

		if (!bCharSet)
			continue;

		if((xRatio == 0 && yStartRatio == 0 && yEndRatio == 0) ||
		   (devXRatio == xRatio && devYRatio >= yStartRatio && devYRatio <= yEndRatio))
		{
			const auto groupOffset = sfnt::u16(vdmx, 6 + size_t(*numRatios) * 4 + i * 2);
			if (!groupOffset)
				return 0;
			offset = *groupOffset;
			break;
		}
	}

	if (!offset) return 0;

	/* Group: recs, startsz, endsz; then 6-byte (yPelHeight, yMax, yMin) records */
	const auto recs = sfnt::u16(vdmx, *offset);
	const size_t base = *offset + 4;
	if (!recs || base > vdmx.size() || (vdmx.size() - base) / 6 < *recs)
		return 0;
	const auto entry = [&](int i) {
		const size_t at = base + size_t(i) * 6;
		return std::tuple(int(*sfnt::u16(vdmx, at)), *sfnt::s16(vdmx, at + 2), *sfnt::s16(vdmx, at + 4));
	};

	int ppem = 0;
	for (int i = 0; i < *recs; i++) {
		const auto [pelHeight, yMax, yMin] = entry(i);
		ppem = pelHeight;

		if(yMax + -yMin == height) {
			font->yMax = yMax;
			font->yMin = yMin;
			break;
		}
		if(yMax + -yMin > height) {
			if(--i < 0)
				return 0; /* failed */
			const auto [smallerHeight, smallerMax, smallerMin] = entry(i);
			font->yMax = smallerMax;
			font->yMin = smallerMin;
			ppem = smallerHeight;
			break;
		}
	}
	if(!font->yMax) /* ppem not found for height */
		ppem = 0;

	return ppem;
}

/* Some fonts have large usWinDescent values, as a result of storing signed short
   in unsigned field. That's probably caused by sTypoDescent vs usWinDescent confusion in
   some font generation tools. */
static inline uint16_t get_fixed_windescent(uint16_t windescent)
{
    return abs((int16_t)windescent);
}

static int calc_ppem_for_height(Font_Container *font, int height)
{
	const sfnt::Bytes os2 = font->sfnt->table(sfnt::tag("OS/2"));
	const sfnt::Bytes hhea = font->sfnt->table(sfnt::tag("hhea"));
	const long unitsPerEm = font->sfnt->unitsPerEm().value_or(0);

	int ppem;
	const int MAX_PPEM = (1 << 16) - 1;

	if(height == 0)
		height = 16;

	/* Calc. height of EM square:
	 *
	 * For +ve lfHeight we have
	 * lfHeight = (winAscent + winDescent) * ppem / units_per_em
	 * Re-arranging gives:
	 * ppem = units_per_em * lfheight / (winAscent + winDescent)
	 *
	 * For -ve lfHeight we have
	 * |lfHeight| = ppem
	 * [i.e. |lfHeight| = (winAscent + winDescent - il) * ppem / units_per_em
	 * with il = winAscent + winDescent - units_per_em]
	 *
	 */

	if(height > 0) {
		const uint16_t winAscent = sfnt::u16(os2, 74).value_or(0);
		const uint16_t windescent = get_fixed_windescent(sfnt::u16(os2, 76).value_or(0));
		int units;

		if(winAscent + windescent == 0)
		{
			const int16_t ascender = sfnt::s16(hhea, 4).value_or(0);
			const int16_t descender = sfnt::s16(hhea, 6).value_or(0);
			font->yMax = ascender;
			font->yMin = descender;
			units = ascender - descender;
		} else {
			font->yMax = winAscent;
			font->yMin = -windescent;
			units = winAscent + windescent;
		}
		if (units <= 0 || unitsPerEm <= 0) /* no usable vertical metrics */
			return height;
		ppem = (int)sfnt::mulDiv(unitsPerEm, height, units);

		/* If rounding ends up getting a font exceeding height, choose a smaller ppem */
		if(ppem > 1 && sfnt::mulDiv(units, ppem, unitsPerEm) > height)
			--ppem;

		if(ppem > MAX_PPEM) {
			//WARN("Ignoring too large height %d, ppem %d\n", height, ppem);
			ppem = 1;
		}
	}
	else if(height >= -MAX_PPEM)
		ppem = -height;
	else {
		//WARN("Ignoring too large height %d\n", height);
		ppem = 1;
	}

	return ppem;
}
/* /wine */

MKXPZ_TTF_FONT *SharedFontState::getFont(std::string family,
                                    int size, float hiresMult)
{
	std::transform(family.begin(), family.end(), family.begin(),
		[](unsigned char c){ return std::tolower(c); });

	if (family.empty())
		family = p->defaultFamily;

	/* Check for substitutions */
	if (p->subs.contains(family))
		family = p->subs[family];

	/* Find out if the font asset exists */
	const FontSet &req = p->sets[family];

	if (req->empty())
	{
		/* Doesn't exist; use built-in font */
		family = "";
	}

	FontSizeKey key(family, size);

	TTF_Font *font;
	int &ppem = p->size_to_ppem[key];
	int ppemMult;
	
	if (ppem != 0)
	{
		ppemMult = std::max<int>(ppem * hiresMult, 1);
		font = p->ppem_to_font[FontPPEMKey(family, ppemMult)];
		if (font)
			return font;
	}
	
	/* Not in pool; open new handle */
	SDL_IOStream *ops;

	if (family.empty())
	{
		/* Built-in font */
		ops = openBundledFont();
	}
	else
	{
		/* Use 'other' path as alternative in case
		 * we have no 'regular' styled font asset */
		const char *path = req->c_str();

		try{
			ops = shState->fileSystem().openReadRaw(path);
		} catch (const Exception &e) {
			p->size_to_ppem.erase(key);
			throw e;
		}
	}

	/* Try to compute the size the same way Windows does. */
    auto metrics = loadFontMetrics(ops);
    if (!metrics) {
        SDL_CloseIO(ops);
        p->size_to_ppem.erase(key);
        throw Exception(Exception::SDLError, "Unable to read font metrics");
    }
    font = TTF_OpenFontIO(ops, true, 1.0f);

	if (font)
	{
		if (metrics->sfnt.valid() && metrics->sfnt.scalable())
		{
			if (ppem == 0)
			{
				Font_Container c = {};
				c.sfnt = &metrics->sfnt;
				c.ppem = load_VDMX(&c, size);
				if (!c.ppem)
					c.ppem = calc_ppem_for_height( &c, size );

				ppem = std::max<int>(c.ppem * p->fontScale, 1);
				ppemMult = std::max<int>(ppem * hiresMult, 1);
			}
			if (!TTF_SetFontSize(font, ppemMult))
			{
				TTF_CloseFont(font);
				font = 0;
			}
		} else {
			/* Someone must have renamed a non-scalable font file to ttf or otf.
			 * Wine has a scaling setup for these, but I'll just fall back to
			 * the mkxp method for now. */
			if (ppem == 0)
			{
				ppem = std::max<int>(size * p->fontScale, 5);
				ppemMult = std::max<int>(ppem * hiresMult, 1);
			}
			if (!TTF_SetFontSize(font, ppemMult))
			{
				TTF_CloseFont(font);
				font = 0;
			}
		}
		if (font)
		{
			/* RGSS doesn't use font hinting */
			TTF_SetFontHinting(font, static_cast<TTF_HintingFlags>(p->fontHinting));
		}
	}
	
	if (!font)
	{
		p->size_to_ppem.erase(key);
		throw Exception(Exception::SDLError, "%s", SDL_GetError());
	}
	
	p->ppem_to_font[FontPPEMKey(family, std::max<int>(ppem * hiresMult, 1))] = font;
	
	if (!p->fontKerning)
		TTF_SetFontKerning(font, 0);
	
	return font;
}

bool SharedFontState::fontPresent(std::string family) const
{
	std::transform(family.begin(), family.end(), family.begin(),
		[](unsigned char c){ return std::tolower(c); });

	/* Check for substitutions */
	if (p->subs.contains(family))
		family = p->subs[family];

	const FontSet &set = p->sets[family];

	return !set->empty();
}

MKXPZ_TTF_FONT *SharedFontState::openBundled(int size)
{
	SDL_IOStream *ops = openBundledFont();

	return TTF_OpenFontIO(ops, true, size);
}

void SharedFontState::setDefaultFontFamily(const std::string &family) {
    p->defaultFamily = family;
}

static bool pickExistingFontName(const std::vector<std::string> &names,
                          std::string &out,
                          const SharedFontState &sfs)
{
	/* Note: In RMXP, a names array with no existing entry
	 * results in no text being drawn at all (same for "" and []);
	 * we can't replicate this in mkxp due to the default substitute. */

	for (size_t i = 0; i < names.size(); ++i)
	{
		if (sfs.fontPresent(names[i]))
		{
			if (out == names[i])
				return false;
			out = names[i];
			return true;
		}
		else
		{
			if (i == 0)
			{
				Debug() << "Primary font not found:" << names[i];
			}
			else
			{
				Debug() << "Fallback font not found:" << names[i];
			}
		}
	}

	if (out[0] == '\0')
		return false;
	out = "";
	return true;
}


struct FontPrivate
{
	std::string name;
	int size;
	float hiresMult;
	bool bold;
	bool italic;
	Color *color;

	Color colorTmp;

	static std::string defaultName;
	static int defaultSize;
	static bool defaultBold;
	static bool defaultItalic;
	static Color *defaultColor;

	static Color defaultColorTmp;

	static std::vector<std::string> initialDefaultNames;

	/* The actual font is opened as late as possible
	 * (when it is queried by a Bitmap), prior it is
	 * set to null */
	TTF_Font *sdlFont;
    
    bool isSolid;

	FontPrivate(int size)
	    : size(size),
	      hiresMult(1.0f),
	      bold(defaultBold),
	      italic(defaultItalic),
	      color(&colorTmp),
	      colorTmp(*defaultColor),
	      sdlFont(0),
          isSolid(false)
	{}

	FontPrivate(const FontPrivate &other)
	    : name(other.name),
	      size(other.size),
	      hiresMult(1.0f),
	      bold(other.bold),
	      italic(other.italic),
	      color(&colorTmp),
	      colorTmp(*other.color),
	      sdlFont(other.sdlFont),
          isSolid(false)
	{}

	void operator=(const FontPrivate &o)
	{
		if (size != o.size || name != o.name)
			sdlFont = 0;
		if (hiresMult == o.hiresMult && sdlFont == 0)
			sdlFont = o.sdlFont;

		 name     =  o.name;
		 size     =  o.size;
		 bold     =  o.bold;
		 italic   =  o.italic;
		*color    = *o.color;

        isSolid = o.isSolid;
	}
};

std::string FontPrivate::defaultName     = "Arial";
int         FontPrivate::defaultSize     = 22;
bool        FontPrivate::defaultBold     = false;
bool        FontPrivate::defaultItalic   = false;
Color      *FontPrivate::defaultColor    = &FontPrivate::defaultColorTmp;

Color FontPrivate::defaultColorTmp(255, 255, 255, 255);

std::vector<std::string> FontPrivate::initialDefaultNames;

bool Font::isSolid() const {
    return p->isSolid;
}

bool Font::doesExist(const char *name)
{
	if (!name)
		return false;

	return shState->fontState().fontPresent(name);
}

Font::Font(const std::vector<std::string> *names,
           int size)
    : p(std::make_unique<FontPrivate>(size ? size : FontPrivate::defaultSize))
{

	if (names)
		setName(*names);
	else
		p->name = FontPrivate::defaultName;
}

Font::Font(const Font &other)
    : p(std::make_unique<FontPrivate>(*other.p))
{}

Font::~Font() = default;

const Font &Font::operator=(const Font &o)
{
	*p = *o.p;

	return o;
}

void Font::setName(const std::vector<std::string> &names)
{
	if (pickExistingFontName(names, p->name, shState->fontState()))
		p->sdlFont = 0;
	p->isSolid = strcmp(p->name.c_str(), "") && shState->config().fontIsSolid(p->name.c_str());
}

void Font::setSize(int value, bool checkIllegal)
{
	if (p->size == value)
		return;

	/* Catch illegal values (according to RMXP) */
	if (value < 6 || value > 96) {
		if (checkIllegal) {
			throw Exception(Exception::ArgumentError, "%s", "bad value for size");
		}
	}

	p->size = value;
	p->sdlFont = 0;
}

void Font::setHiresMult(float value)
{
	if (p->hiresMult == value)
		return;

	p->hiresMult = value;
	p->sdlFont = 0;
}

static void guardDisposed() {}

DEF_ATTR_RD_SIMPLE(Font, Size, int, p->size)

DEF_ATTR_SIMPLE(Font, Bold,     bool,    p->bold)
DEF_ATTR_SIMPLE(Font, Italic,   bool,    p->italic)
DEF_ATTR_SIMPLE(Font, Color,    Color&, *p->color)

DEF_ATTR_SIMPLE_STATIC(Font, DefaultSize,     int,     FontPrivate::defaultSize)
DEF_ATTR_SIMPLE_STATIC(Font, DefaultBold,     bool,    FontPrivate::defaultBold)
DEF_ATTR_SIMPLE_STATIC(Font, DefaultItalic,   bool,    FontPrivate::defaultItalic)
DEF_ATTR_SIMPLE_STATIC(Font, DefaultColor,    Color&, *FontPrivate::defaultColor)

void Font::setDefaultName(const std::vector<std::string> &names,
                          const SharedFontState &sfs)
{
	pickExistingFontName(names, FontPrivate::defaultName, sfs);
}

const std::vector<std::string> &Font::getInitialDefaultNames()
{
	return FontPrivate::initialDefaultNames;
}

void Font::initDynAttribs()
{
	p->color = new Color(p->colorTmp);
}

void Font::initDefaultDynAttribs()
{
	FontPrivate::defaultColor = new Color(FontPrivate::defaultColorTmp);
}

void Font::initDefaults(const SharedFontState &sfs)
{
	std::vector<std::string> &names = FontPrivate::initialDefaultNames;

	// FIXME: Japanese version has "MS PGothic" instead
	names.push_back("Arial");

	setDefaultName(names, sfs);
}

MKXPZ_TTF_FONT *Font::getSdlFont()
{
	MKXPZ_TTF_FONT **font = &p->sdlFont;

	if (!*font)
		*font = shState->fontState().getFont(p->name.c_str(),
		                                     p->size, p->hiresMult);

	int style = TTF_STYLE_NORMAL;

	if (p->bold)
		style |= TTF_STYLE_BOLD;

	if (p->italic)
		style |= TTF_STYLE_ITALIC;

	TTF_SetFontStyle(*font, static_cast<TTF_FontStyleFlags>(style));

	return *font;
}
