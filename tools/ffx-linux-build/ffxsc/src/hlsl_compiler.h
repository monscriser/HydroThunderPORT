// Linux port stub: DXC/FXC backends are not available; only -compiler=glslang is supported.
#pragma once
#include "compiler.h"
#include <stdexcept>
struct HLSLCompiler {
  enum Backend { DXC, GDK_SCARLETT_X64, GDK_XBOXONE_X64, FXC };
};
inline ICompiler* NoHLSL(HLSLCompiler::Backend, const std::string&, const std::string&, const std::string&,
                         const std::string&, const std::string&, bool, bool) {
  throw std::runtime_error("HLSL backends (dxc/fxc) are not built in the Linux port; use -compiler=glslang");
}
