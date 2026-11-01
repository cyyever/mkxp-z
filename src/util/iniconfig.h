#ifndef INICONFIG_H
#define INICONFIG_H

#include <istream>
#include <string>
#include <map>
#include <string>

class INIConfiguration
{
public:
	bool load (std::istream& inStream);

	std::string getStringProperty(const std::string& sname, const std::string& name, const std::string& def = "") const;

protected:
	void addProperty (const std::string& sname, const std::string& name, const std::string& val);

private:
	std::map<std::string, std::map<std::string, std::string>> m_SectionMap;
};

#endif // INICONFIG_H
