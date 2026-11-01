/*
** keybindings.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
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

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <type_traits>
#include <utility>

#include "keybindings.h"

#include "config.h"
#include "paths.h"

struct KbBindingData
{
	SDL_Scancode source;
	Input::ButtonCode target;

	void add(BDescVec &d) const
	{
		SourceDesc src;
		src.type = Key;
		src.d.scan = source;

		BindingDesc desc;
		desc.src = src;
		desc.target = target;

		d.push_back(desc);
	}
};

struct CtrlBindingData
{
    SDL_GamepadButton source;
    Input::ButtonCode target;
    
    void add(BDescVec &d) const
    {
        SourceDesc src;
        src.type = CButton;
        src.d.cb = source;
        
        BindingDesc desc;
        desc.src = src;
        desc.target = target;
        
        d.push_back(desc);
    }
};

/* Common */
static const KbBindingData defaultKbBindings[] =
{
	{ SDL_SCANCODE_LEFT,   Input::Left  },
	{ SDL_SCANCODE_RIGHT,  Input::Right },
	{ SDL_SCANCODE_UP,     Input::Up    },
	{ SDL_SCANCODE_DOWN,   Input::Down  },
    
	{ SDL_SCANCODE_SPACE,  Input::C     },
	{ SDL_SCANCODE_RETURN, Input::C     },
	{ SDL_SCANCODE_ESCAPE, Input::B     },
	{ SDL_SCANCODE_KP_0,   Input::B     },
	{ SDL_SCANCODE_LSHIFT, Input::A     },
	{ SDL_SCANCODE_X,      Input::B     },
	{ SDL_SCANCODE_D,      Input::Z     },
	{ SDL_SCANCODE_Q,      Input::L     },
	{ SDL_SCANCODE_W,      Input::R     },
	{ SDL_SCANCODE_A,      Input::X     },
	{ SDL_SCANCODE_S,      Input::Y     }
};

/* RGSS1 */
static const KbBindingData defaultKbBindings1[] =
{
	{ SDL_SCANCODE_Z,      Input::A     },
	{ SDL_SCANCODE_C,      Input::C     },
};

/* RGSS2 and higher */
static const KbBindingData defaultKbBindings2[] =
{
	{ SDL_SCANCODE_Z,      Input::C     }
};


static const CtrlBindingData defaultCtrlBindings[] =
{
	{ SDL_GAMEPAD_BUTTON_WEST, Input::A  },
	{ SDL_GAMEPAD_BUTTON_EAST, Input::B  },
	{ SDL_GAMEPAD_BUTTON_SOUTH, Input::C },
	{ SDL_GAMEPAD_BUTTON_NORTH, Input::X  },
	{ SDL_GAMEPAD_BUTTON_LEFT_STICK, Input::Y  },
	{ SDL_GAMEPAD_BUTTON_RIGHT_STICK, Input::Z },
	{ SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Input::L  },
	{ SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Input::R  },
    
    { SDL_GAMEPAD_BUTTON_DPAD_UP, Input::Up },
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN, Input::Down },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT, Input::Left },
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Input::Right }
};


static void addAxisBinding(BDescVec &d, SDL_GamepadAxis axis, AxisDir dir, Input::ButtonCode target)
{
	SourceDesc src;
	src.type = CAxis;
	src.d.ca.axis = axis;
	src.d.ca.dir = dir;

	BindingDesc desc;
	desc.src = src;
	desc.target = target;

	d.push_back(desc);
}

BDescVec genDefaultBindings(const Config &conf)
{
	BDescVec d;

	for (const auto &binding : defaultKbBindings)
		binding.add(d);

	if (conf.rgssVersion == 1)
		for (const auto &binding : defaultKbBindings1)
			binding.add(d);
	else
		for (const auto &binding : defaultKbBindings2)
			binding.add(d);

	for (const auto &binding : defaultCtrlBindings)
		binding.add(d);

	addAxisBinding(d, SDL_GAMEPAD_AXIS_LEFTX, Negative, Input::Left );
	addAxisBinding(d, SDL_GAMEPAD_AXIS_LEFTX, Positive, Input::Right);
	addAxisBinding(d, SDL_GAMEPAD_AXIS_LEFTY, Negative, Input::Up   );
	addAxisBinding(d, SDL_GAMEPAD_AXIS_LEFTY, Positive, Input::Down );

	return d;
}

static constexpr uint32_t FormatVersion = 3;

struct Header
{
	uint32_t formVer;
	uint32_t rgssVer;
	uint32_t count;
};

static_assert(std::is_trivially_copyable_v<Header> && std::is_trivially_copyable_v<BindingDesc>);

static std::filesystem::path buildPath(const std::string &dir, uint32_t rgssVersion)
{
    return paths::fromUtf8(dir) / std::format("keybindings.mkxp{}", rgssVersion);
}

static bool writeBindings(const BDescVec &bindings, const std::string &dir,
                          uint32_t rgssVersion)
{
    if (dir.empty() || !std::in_range<uint32_t>(bindings.size()))
        return false;

    std::ofstream file(buildPath(dir, rgssVersion), std::ios::binary);
    const Header header{FormatVersion, rgssVersion, static_cast<uint32_t>(bindings.size())};
    if (!file.write(reinterpret_cast<const char *>(&header), sizeof(header)))
        return false;
    const auto bytes = std::as_bytes(std::span(bindings));
    if (!bytes.empty() && !file.write(reinterpret_cast<const char *>(bytes.data()),
                                     static_cast<std::streamsize>(bytes.size())))
        return false;
    file.close();
    return bool(file);
}

void storeBindings(const BDescVec &d, const Config &conf)
{
    writeBindings(d, conf.customDataPath, conf.rgssVersion);
}

static bool verifyDesc(const BindingDesc &desc)
{
	const Input::ButtonCode codes[] =
	{
	    Input::None,
	    Input::Down, Input::Left, Input::Right, Input::Up,
	    Input::A, Input::B, Input::C,
	    Input::X, Input::Y, Input::Z,
	    Input::L, Input::R,
	    Input::Shift, Input::Ctrl, Input::Alt,
	    Input::F5, Input::F6, Input::F7, Input::F8, Input::F9
	};

    if (std::ranges::find(codes, desc.target) == std::end(codes))
        return false;

    const auto &src = desc.src;

	switch (src.type)
	{
	case Invalid:
		return true;
	case Key:
		return src.d.scan < SDL_SCANCODE_COUNT;
            
    case CButton:
        return true;

	case CAxis:
		return src.d.ca.dir == Negative || src.d.ca.dir == Positive;
	default:
		return false;
	}
}

static bool readBindings(BDescVec &out, const std::string &dir,
                         uint32_t rgssVersion)
{
    if (dir.empty())
        return false;

    std::ifstream file(buildPath(dir, rgssVersion), std::ios::binary);
    Header header{};
    if (!file.read(reinterpret_cast<char *>(&header), sizeof(header)) ||
        header.formVer != FormatVersion || header.rgssVer != rgssVersion || header.count > 1024)
        return false;

    BDescVec bindings(header.count);
    const auto bytes = std::as_writable_bytes(std::span(bindings));
    if ((!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()),
                                     static_cast<std::streamsize>(bytes.size()))) ||
        !std::ranges::all_of(bindings, verifyDesc))
        return false;
    out = std::move(bindings);
    return true;
}

BDescVec loadBindings(const Config &conf)
{
	BDescVec d;

	if (readBindings(d, conf.customDataPath, conf.rgssVersion))
		return d;

	return genDefaultBindings(conf);
}
