#include "iniconfig.h"

#include <algorithm>
#include <string_view>

static std::string toLowerCase(std::string_view str)
{
	std::string lower(str);
	// INI names use the same locale-independent ASCII fold as asset paths.
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
	});
	return lower;
}
 
static std::string_view trim(std::string_view str, std::string_view chars = "\t\n\v\f\r ")
{
	const auto first = str.find_first_not_of(chars);
	return first == std::string_view::npos ? std::string_view{} :
	       str.substr(first, str.find_last_not_of(chars) - first + 1);
}

bool INIConfiguration::load (std::istream& is)
{
	if (!is.good())
	{
		return false;
	}

	std::string currSectionName;

	std::string line;
	while (std::getline(is, line))
	{
		if (line.empty())
			continue;

		if (line[0] == '[')
		{
			currSectionName = line.substr (1, line.find_last_of (']') - 1);
		}
		else if (line[0] != '#')
		{
			size_t equalsPos = line.find ('=');

			if (equalsPos != std::string::npos)
			{
				std::string key = line.substr (0, equalsPos);
				std::string val = line.substr (equalsPos + 1);

				addProperty (currSectionName, key , val);
			}
		}

	}

	if (is.bad())
	{
		return false;
	}

	return true;
}


std::string INIConfiguration::getStringProperty(const std::string& sname, const std::string& name, const std::string& def) const
{
    const auto section = m_SectionMap.find(toLowerCase(sname));
    if (section != m_SectionMap.end()) {
        const auto property = section->second.find(toLowerCase(name));
        if (property != section->second.end())
            return property->second;
    }
    return def;
}

void INIConfiguration::addProperty(const std::string& sname, const std::string& name, const std::string& val)
{
    m_SectionMap[toLowerCase(sname)][toLowerCase(trim(name))] = trim(val);
}
