#!/bin/bash
# Where the presenter's RCAS/CAS shaders live and how they are built.
cd /root/ht/sdk
ls src/ui/shaders | head -40
echo "== vulkan_spirv"
ls src/ui/shaders/vulkan_spirv | grep -iE 'rcas|cas|easu' | head
echo "== who includes rcas spirv"
grep -rln 'rcas' src/ui --include=*.cpp --include=*.h | head
grep -rn 'rcas' src/ui/vulkan/vulkan_presenter.cpp | head -8
echo "== any shader compile step in cmake"
grep -rln 'glslang\|spirv-opt\|\.xesl\|xesl' cmake CMakeLists.txt src/ui/CMakeLists.txt 2>/dev/null | head
