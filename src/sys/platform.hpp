//
// Created by loulfy on 09/11/2024.
//

#pragma once

#if defined(_WIN32)
#define PLATFORM_WIN 1
#elif defined(__linux__)
#define PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
#define PLATFORM_MACOS 1
#endif