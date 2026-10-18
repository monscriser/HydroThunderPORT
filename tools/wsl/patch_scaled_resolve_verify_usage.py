"""Fix transfer usage required by the existing scaled resolve verifier.

The verifier snapshots and restores the scaled resolve destination with
vkCmdCopyBuffer. Both sparse and non-sparse allocations must allow transfers.
Does not add copies or change rendering when verification is disabled.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1])/'src/graphics/vulkan/texture_cache.cpp'
s=p.read_text()
a='buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;'
b='''// The direct-resolve proof snapshots/restores this buffer using copies.
  buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;'''
assert s.count(a)==1;s=s.replace(a,b)
a='vulkan_device, scaled_resolve_buffer_size_, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,'
b='''vulkan_device, scaled_resolve_buffer_size_,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,'''
assert s.count(a)==1;s=s.replace(a,b)
p.write_text(s)
print('Scaled resolve buffer: transfer source/destination usage fixed for exact proof.')
