#pragma once
#include "vzmcore/Components.h"

#include <any>

#ifdef _WIN32
#define PI_EXPORT __declspec(dllexport)
#else
#define PI_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PI_EXPORT bool ImportDicom(std::unordered_map<std::string, std::any>& io);