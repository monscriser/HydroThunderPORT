// Linux replacement for the Windows-only pch.hpp (glslang path only).
#pragma once
#include "winshim.h"
#include <algorithm>
#include <assert.h>
#include <deque>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <functional>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdio.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace fs = std::filesystem;
#include <process.hpp>
namespace tpl = TinyProcessLib;
