#include "util/iniconfig.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        INIConfiguration ini;
        std::istringstream input("\n# ignored=comment\r\n[GaMe]\r\n Title \t= 雨血：迷镇 \t\r\n"
                                 "Empty=\r\nX=\r\nMiXeD键=值É\r\nLast=终行");
        require(ini.load(input), "valid INI rejected");
        require(ini.getStringProperty("GAME", "title") == "雨血：迷镇", "CRLF/whitespace/ASCII fold failed");
        require(ini.getStringProperty("game", "last") == "终行", "unterminated final line dropped");
        require(ini.getStringProperty("game", "empty", "missing").empty(), "empty value lost");
        require(ini.getStringProperty("game", "x", "missing").empty(), "short empty assignment lost");
        require(ini.getStringProperty("game", "mixed键") == "值É", "UTF8 bytes changed");
        require(ini.getStringProperty("", "# ignored", "missing") == "missing", "comment parsed as property");
        require(ini.getStringProperty("missing", "key", "fallback") == "fallback", "missing lookup mutated defaults");
        std::istringstream single("A=last");
        INIConfiguration global;
        require(global.load(single) && global.getStringProperty("", "a") == "last", "single unterminated line lost");
        std::istringstream unicode("[章É]\n键A=原值");
        require(global.load(unicode), "UTF8 section rejected");
        require(global.getStringProperty("章É", "键a") == "原值", "UTF8 section or key changed");
        require(global.getStringProperty("章é", "键a", "missing") == "missing", "non-ASCII bytes case folded");
        std::istringstream blank("\n\r\n# comment");
        require(global.load(blank), "empty/comment lines rejected");
        std::istringstream empty;
        require(global.load(empty), "empty file rejected");
        std::istringstream failed;
        failed.setstate(std::ios::badbit);
        require(!global.load(failed), "bad input stream accepted");
        std::cout << "INI configuration tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
