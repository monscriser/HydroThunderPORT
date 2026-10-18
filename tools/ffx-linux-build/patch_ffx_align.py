import re,sys
p=sys.argv[1]
s=open(p).read()
if 'HT_FFX_ALIGN_PTR' in s:
    print('already patched', p); sys.exit(0)
# 1. slack in scratch size
old="""    return FFX_ALIGN_UP(sizeof(BackendContext_VK) + extensionPropArraySize + gpuJobDescArraySize + resourceViewArraySize + stagingRingBufferArraySize +
                            pipelineArraySize + resourceArraySize + contextArraySize,
                        sizeof(uint64_t));"""
new="""    // HT: room to align every sub-array to 64 bytes (see HT_FFX_ALIGN_PTR).
    return FFX_ALIGN_UP(sizeof(BackendContext_VK) + extensionPropArraySize + gpuJobDescArraySize + resourceViewArraySize + stagingRingBufferArraySize +
                            pipelineArraySize + resourceArraySize + contextArraySize + 64 * 8,
                        sizeof(uint64_t));"""
assert s.count(old)==1; s=s.replace(old,new)
# 2. align each mapped array
macro="#define HT_FFX_ALIGN_PTR(p) (p) = (uint8_t*)(((uintptr_t)(p) + 63) & ~(uintptr_t)63)\n"
s=s.replace("FFX_API size_t ffxGetScratchMemorySizeVK(", macro+"FFX_API size_t ffxGetScratchMemorySizeVK(",1)
names=["pGpuJobs","pResourceViews","pStagingRingBuffer","pPipelineLayouts","pResources","pEffectContexts","extensionProperties"]
for n in names:
    pat=re.compile(r"(\n(\s*)backendContext->"+n+r" = \([^)]*\)\s*\(?pMem\)?;)")
    m=pat.search(s); assert m, n
    s=s[:m.start()]+"\n"+m.group(2)+"HT_FFX_ALIGN_PTR(pMem);"+s[m.start():]
open(p,'w').write(s); print('patched',p)
