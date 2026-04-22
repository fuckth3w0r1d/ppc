#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>

class logger {
private:
    // 用于表示“没有额外 data 参数”
    struct Empty {};

    // ANSI 颜色代码
    static constexpr const char* COLOR_RESET  = "\033[0m";
    static constexpr const char* COLOR_RED    = "\033[31m";
    static constexpr const char* COLOR_GREEN  = "\033[32m";
    static constexpr const char* COLOR_YELLOW = "\033[33m";
    static constexpr const char* COLOR_BLUE   = "\033[34m";

    static std::string getColoredLevelStr(size_t level)
    {
        if(level == 0) return std::string(COLOR_BLUE) + "[DEBUG]" + COLOR_RESET;
        if(level == 1) return std::string(COLOR_GREEN) + "[INFO]" + COLOR_RESET;
        if(level == 2) return std::string(COLOR_YELLOW) + "[WARN]" + COLOR_RESET;
        if(level == 3) return std::string(COLOR_RED) + "[ERROR]" + COLOR_RESET;
        return "";
    }

    static std::string getLevelStr(size_t level)
    {
        if(level == 0) return "[DEBUG]";
        if(level == 1) return "[INFO]";
        if(level == 2) return "[WARN]";
        if(level == 3) return "[ERROR]";
        return "";
    }

    template<typename T>
    static void log(std::ostream& os, const size_t level, const std::string& tip, const T& data)
    {
        std::lock_guard<std::mutex> lock(getMutex());

        std::string coloredlevelstr = getColoredLevelStr(level);
        // 如果你后面要输出到文件，可以再用这个无颜色版本
        // std::string levelstr = getLevelStr(level);

        os << coloredlevelstr << " " << tip;
        print(os, data);
        os << std::endl;
    }

    template<typename T>
    static void print(std::ostream& os, const T& value)
    {
        os << value;
    }

    // 针对 Empty 特判：什么都不输出
    static void print(std::ostream& os, const Empty&)
    {
        (void)os;
    }

    static std::mutex& getMutex()
    {
        static std::mutex mtx;
        return mtx;
    }

public:
    template<typename T = Empty>
    static void debug(const std::string& tip, const T& data = T())
    {
        log(std::cerr, 0, tip, data);
    }

    template<typename T = Empty>
    static void info(const std::string& tip, const T& data = T())
    {
        log(std::cout, 1, tip, data);
    }

    template<typename T = Empty>
    static void warn(const std::string& tip, const T& data = T())
    {
        log(std::cerr, 2, tip, data);
    }

    template<typename T = Empty>
    static void error(const std::string& tip, const T& data = T())
    {
        log(std::cerr, 3, tip, data);
    }
};
