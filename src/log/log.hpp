//
// Created by loulfy on 01/12/2023.
//

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace ler::log
{
    using namespace spdlog;
    static void exit(const std::string& msg)
    {
        log::error(msg);
        throw std::runtime_error(msg);
    }
    static void setup(level::level_enum level)
    {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<sinks::stdout_color_sink_mt>());
        sinks.push_back(std::make_shared<sinks::basic_file_sink_mt>("logs.txt", true));
        auto logger = std::make_shared<spdlog::logger>("LER", begin(sinks), end(sinks));
        set_default_logger(logger);
        set_level(level);
        flush_on(level);
    }
}