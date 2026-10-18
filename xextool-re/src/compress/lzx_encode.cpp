// lzx_encode.cpp - faithful transliteration of xextool's MS-LZX compressor.
//
// MODEL: the original keeps a 0x4350-byte context struct plus several heap
// buffers, storing raw 32-bit pointers inside the struct and doing biased
// pointer arithmetic. To remain byte-exact yet portable to 64-bit we place the
// context and every buffer in ONE arena and represent each "pointer" as a
// 32-bit signed byte-offset into it. A dereference `*(T*)(slot + idx)` becomes
// `*(T*)(A + (int32_t)slot + idx)`, which reproduces the original's flat 32-bit
// address arithmetic exactly (differences/biases are identical).
//
// Field access mirrors the decompiler: `in_EAX[N]` (int index) == byte N*4.
// The context base is `A` (arena.data()); functions that the decompiler showed
// using in_EAX / unaff_ESI for the context take `A` explicitly.
//
// Derived from the original's encoder cluster (FUN_004203c0..FUN_00424dd0 in a
// Ghidra decompile of XexTool v6.3). Verified byte-exact by compressing a 36 MB
// Xbox 360 basefile and diffing against the original tool's `-c c` output.
#include "lzx_encode.h"
#include <cstring>
#include <stdexcept>

namespace xex::compress {
namespace {

// ---- static .rdata tables (verbatim from xextool_unpacked.exe) -------------
// extra_bits[slot] @ VA 0x4554cc
static const uint8_t kExtraBits[52] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,
    13,13,14,14,15,15,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,0};
// position_base[slot] @ VA 0x455338
static const uint32_t kPosBase[51] = {
    0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024,1536,
    2048,3072,4096,6144,8192,12288,16384,24576,32768,49152,65536,98304,131072,
    196608,262144,393216,524288,786432,1048576,1572864,2097152,3145728,4194304,
    6291456,8388608,12582912,16777216,25165824,33554432};
// footer_mask[slot] = (1<<extra_bits)-1 @ VA 0x455400
static const uint32_t kFooterMask[51] = {
    0,0,0,0,1,1,3,3,7,7,0xf,0xf,0x1f,0x1f,0x3f,0x3f,0x7f,0x7f,0xff,0xff,
    0x1ff,0x1ff,0x3ff,0x3ff,0x7ff,0x7ff,0xfff,0xfff,0x1fff,0x1fff,0x3fff,0x3fff,
    0x7fff,0x7fff,0xffff,0xffff,0x1ffff,0x1ffff,0x3ffff,0x3ffff,0x7ffff,0x7ffff,
    0xfffff,0xfffff,0x1fffff,0x1fffff,0x3fffff,0x3fffff,0x7fffff,0x7fffff,0x1ffffff};
// pretree length-delta map DAT_004552e9, centred: kDelta[16 + prevLen - curLen]
// (negative deltas wrap to 17+delta). Indexable for delta in [-16, +16].
static const uint8_t kDelta[33] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
// block-split divergence tables: kSplitMag[x]=bit-length of x @0x4551d8,
// kSplitWeight[m]=m^2 @0x455190. metric = Σ|kSplitWeight[mag(h1)]-kSplitWeight[mag(h2)]|
static const uint8_t kSplitMag[256] = {
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8};
static const uint32_t kSplitWeight[18] = {
    0,1,4,9,16,25,36,49,64,81,100,121,144,169,196,225,256,0};
static inline uint32_t split_mag(uint32_t f){ return f < 0x100 ? kSplitMag[f] : (uint32_t)kSplitMag[f >> 8] + 8; }

// ---- arena field accessors -------------------------------------------------
static inline int32_t&  i32(uint8_t* A, size_t o){ return *reinterpret_cast<int32_t*>(A+o); }
static inline uint32_t& u32(uint8_t* A, size_t o){ return *reinterpret_cast<uint32_t*>(A+o); }
static inline int16_t&  i16(uint8_t* A, size_t o){ return *reinterpret_cast<int16_t*>(A+o); }
static inline uint16_t& u16(uint8_t* A, size_t o){ return *reinterpret_cast<uint16_t*>(A+o); }
static inline uint8_t&  u8 (uint8_t* A, size_t o){ return *(A+o); }
static inline int8_t&   i8 (uint8_t* A, size_t o){ return *reinterpret_cast<int8_t*>(A+o); }
// int-index view: in_EAX[n] == byte n*4
static inline int32_t&  E (uint8_t* A, size_t n){ return *reinterpret_cast<int32_t*>(A+n*4); }
// dereference a stored offset-"pointer" at byte slot `o`, plus byte index `idx`
static inline size_t P(uint8_t* A, size_t o, int64_t idx=0){
    return static_cast<size_t>(static_cast<int64_t>(i32(A,o)) + idx);
}

// Arena byte offsets of the heap sub-buffers (filled by setup()).
struct Layout {
    uint32_t window, btL, btR, lenbuf, offbuf, flagbuf, nodes, outbuf;
    uint32_t ptFreq, ptLen, ptCode;   // pretree Huffman scratch (our addition)
    uint32_t total;
};

// Bit/flag buffer offsets used throughout, named for clarity.
constexpr size_t OFF_BITBUF   = 0x14;   // 32-bit accumulator (MSB-first)
constexpr size_t OFF_BITFREE  = 0x18;   // free-bit count (init 0x20, flush <0x11)
constexpr size_t OFF_OUTERR   = 0x1c;   // overflow flag
constexpr size_t OFF_OUTBASE  = 0x85c;  // == E[0x217]
constexpr size_t OFF_OUTPTR   = 0x860;  // == E[0x218]
constexpr size_t OFF_OUTEND   = 0x864;
constexpr size_t OFF_OUTCNT   = 0x868;  // == E[0x21a]

} // namespace

// ---------------------------------------------------------------------------
// Canonical Huffman builder (FUN_00424e80 family). Operates on the context
// arena `a`. Scratch fields: freq array ptr @0x9a0, node-order ptr @0x9a4,
// lengths-out ptr @0x9a8, heap[1..] @0x9ac (1-indexed), heap size @0x252c,
// depth @0x252e, num-symbols @0x2528, bl_count[0..16] @0x2504, tree children
// @0xf28 (left @+0, right @+2 per node).
namespace {
void huff_sift(uint8_t* a, int16_t node);        // FUN_00424d00
void huff_countdepth(uint8_t* a, int16_t node);  // FUN_00424b40
void huff_lengths(uint8_t* a, int16_t rootNode); // FUN_00424ba0

// FUN_00424d00: sift node down the min-heap (keyed on freq, unsigned 16-bit).
void huff_sift(uint8_t* a, int16_t param_1) {
    uint32_t fq = (uint32_t)i32(a, 0x9a0);
    int16_t sVar1 = i16(a, 0x9ac + param_1*2);
    int sVar2 = param_1*2;
    int sVar4 = i16(a, 0x252c);
    if (sVar4 < sVar2) { i16(a, 0x9ac + param_1*2) = sVar1; return; }
    do {
        int sVar3 = sVar2;
        if (sVar2 < sVar4 &&
            u16(a, fq + (uint16_t)i16(a, 0x9ae + sVar2*2)*2) <
            u16(a, fq + (uint16_t)i16(a, 0x9ac + sVar2*2)*2))
            sVar3 = sVar2 + 1;
        sVar4 = i16(a, 0x9ac + sVar3*2);                       // child value
        if (u16(a, fq + (uint16_t)sVar1*2) <= u16(a, fq + (uint16_t)(int16_t)sVar4*2)) break;
        i16(a, 0x9ac + param_1*2) = (int16_t)sVar4;
        sVar4 = i16(a, 0x252c);
        sVar2 = sVar3*2;
        param_1 = (int16_t)sVar3;
    } while (sVar2 <= sVar4);
    i16(a, 0x9ac + param_1*2) = sVar1;
}

// FUN_00424b40: recursively tally leaf depths into bl_count[].
void huff_countdepth(uint8_t* a, int16_t node) {
    int iVar2 = node;
    if (iVar2 < i32(a, 0x2528)) {
        int d = (int8_t)u8(a, 0x252e);
        if ((int8_t)u8(a, 0x252e) > 0xf) d = 0x10;
        i16(a, 0x2504 + d*2) += 1;
        return;
    }
    u8(a, 0x252e) += 1;
    huff_countdepth(a, i16(a, 0xf28 + iVar2*4));
    huff_countdepth(a, i16(a, 0xf2a + iVar2*4));
    u8(a, 0x252e) -= 1;
}

// FUN_00424ba0: derive code lengths from the tree, 16-bit-limited, written to
// the lengths-out buffer in node-order.
void huff_lengths(uint8_t* a, int16_t rootNode) {
    for (uint32_t o = 0x2504; o <= 0x2520; o += 4) i32(a,o) = 0;
    i16(a, 0x2524) = 0;
    int iVar5 = rootNode;
    if (iVar5 < i32(a, 0x2528)) {
        int d = (int8_t)u8(a, 0x252e);
        if ((int8_t)u8(a, 0x252e) > 0xf) d = 0x10;
        i16(a, 0x2504 + d*2) += 1;
    } else {
        u8(a, 0x252e) += 1;
        huff_countdepth(a, i16(a, 0xf28 + iVar5*4));
        huff_countdepth(a, i16(a, 0xf2a + iVar5*4));
        u8(a, 0x252e) -= 1;
    }
    uint8_t bVar3 = 0; int psVar7 = 0x2524; int8_t cVar2 = 0x10; int16_t sVar8 = 0; int psVar6 = psVar7;
    do {
        uint8_t bVar1 = bVar3 & 0x1f;
        cVar2--; bVar3++;
        sVar8 = (int16_t)(sVar8 + ((int)i16(a, psVar6) << bVar1));
        psVar6 -= 2;
    } while (cVar2 != 0);
    while (true) {
        if (sVar8 == 0) {
            cVar2 = 0x10;
            do {
                sVar8 = i16(a, psVar7);
                while ((sVar8 = (int16_t)(sVar8 - 1)) >= 0) {
                    uint32_t lenBase = (uint32_t)i32(a, 0x9a8);
                    uint16_t sym = u16(a, (uint32_t)i32(a, 0x9a4));
                    u8(a, lenBase + sym) = (uint8_t)cVar2;
                    i32(a, 0x9a4) += 2;
                }
                cVar2--; psVar7 -= 2;
            } while (cVar2 != 0);
            return;
        }
        i16(a, psVar7) -= 1;
        uint32_t uVar4 = 0xf;
        do {
            if (i16(a, 0x2504 + uVar4*2) != 0) {
                i16(a, 0x2504 + uVar4*2) -= 1;
                i16(a, 0x2506 + uVar4*2) += 2;
                break;
            }
            bVar3 = (uint8_t)((char)uVar4 - 1);
            uVar4 = bVar3;
        } while (bVar3 != 0);
        sVar8 = (int16_t)(sVar8 - 1);
    }
}

// FUN_00424f80: build the Huffman tree via repeated min-extraction.
void huff_treebuild(uint8_t* a, int16_t count, uint32_t freqOff, uint32_t codeOff) {
    for (int16_t s = (int16_t)(i16(a, 0x252c) >> 1); s > 0; s--) huff_sift(a, s);
    i32(a, 0x9a4) = (int32_t)codeOff;
    int16_t param_1 = count, sVar5 = count;
    do {
        sVar5 = param_1;
        uint16_t uVar2 = u16(a, 0x9ae);                       // min
        if ((int16_t)uVar2 < i32(a, 0x2528)) {
            u16(a, (uint32_t)i32(a, 0x9a4)) = uVar2; i32(a, 0x9a4) += 2;
        }
        uint16_t uVar1 = (uint16_t)i16(a, 0x9ac + i16(a, 0x252c)*2);
        i16(a, 0x252c) -= 1;
        u16(a, 0x9ae) = uVar1;
        huff_sift(a, 1);
        int16_t sVar3 = i16(a, 0x9ae);                        // 2nd min
        if (sVar3 < i32(a, 0x2528)) {
            u16(a, (uint32_t)i32(a, 0x9a4)) = (uint16_t)sVar3; i32(a, 0x9a4) += 2;
        }
        int iVar6 = sVar5;
        u16(a, freqOff + (uint16_t)sVar5*2) =
            (uint16_t)(u16(a, freqOff + (uint16_t)sVar3*2) + u16(a, freqOff + uVar2*2));
        i16(a, 0x9ae) = sVar5;
        huff_sift(a, 1);
        u16(a, 0xf28 + iVar6*4) = uVar2;
        i16(a, 0xf2a + iVar6*4) = sVar3;
        param_1 = (int16_t)(sVar5 + 1);
    } while (1 < i16(a, 0x252c));
    i32(a, 0x9a4) = (int32_t)codeOff;
    huff_lengths(a, sVar5);
}

// FUN_00424dd0: assign canonical codes from the per-length histogram.
void huff_codes(uint8_t* a, int count, uint32_t codeOff, uint32_t lenOff) {
    int16_t nc[18]; nc[0] = 0; nc[1] = 0;
    int iVar4 = 1, p = 0x2508;
    do {
        int16_t sVar1 = (int16_t)((nc[iVar4] + i16(a, p-2)) * 2);
        int16_t sVar3 = (int16_t)((i16(a, p) + sVar1) * 2);
        nc[iVar4+1] = sVar1;
        int16_t s1 = i16(a, p+2);
        nc[iVar4+2] = sVar3;
        int16_t s2 = i16(a, p+4);
        s1 = (int16_t)((s1 + sVar3) * 2);
        nc[iVar4+3] = s1;
        nc[iVar4+4] = (int16_t)((s2 + s1) * 2);
        iVar4 += 4; p += 8;
    } while (iVar4 < 0x11);
    for (int i = 0; i < count; i++) {
        uint8_t len = u8(a, lenOff + i);
        i16(a, codeOff + i*2) = nc[len];
        nc[len] = (int16_t)(nc[len] + 1);
    }
}

// FUN_00424e80: entry — collect nonzero-freq symbols, build tree + codes.
void huff_build(uint8_t* a, int count, uint32_t freqOff, uint32_t lenOff,
                uint32_t codeOff, int doCodes) {
    while (true) {
        i32(a, 0x9a8) = (int32_t)lenOff;
        int16_t sVar2 = 0; int iVar1 = 0;
        i32(a, 0x2528) = count;
        i32(a, 0x9a0) = (int32_t)freqOff;
        u8(a, 0x252e) = 0;
        i16(a, 0x252c) = 0;
        i16(a, 0x9ae) = 0;
        if (count < 1) break;
        do {
            u8(a, lenOff + iVar1) = 0;
            if (u16(a, freqOff + iVar1*2) != 0) {
                i16(a, 0x252c) += 1;
                i16(a, 0x9ac + i16(a, 0x252c)*2) = sVar2;
            }
            sVar2++; iVar1 = sVar2;
        } while (iVar1 < count);
        if (1 < i16(a, 0x252c)) {
            huff_treebuild(a, (int16_t)count, freqOff, codeOff);
            if (doCodes != 0) huff_codes(a, count, codeOff, lenOff);
            return;
        }
        if (i16(a, 0x252c) == 0) break;
        if (i16(a, 0x9ae) == 0) u16(a, freqOff + 2) = 1;
        else u16(a, freqOff + 0) = 1;
    }
    u16(a, codeOff + (uint16_t)i16(a, 0x9ae)*2) = 0;
}
} // namespace

// ---------------------------------------------------------------------------
// Binary-tree match finder (FUN_00421e60 insert / FUN_00421bb0 search /
// FUN_00421fa0 slide-delete). [0]=window base (biased), [2]=BT head pos,
// [3]=left-child array (biased), [4]=right-child array (biased). Window byte
// at position k = arena[[0]+k]; child[k] = arena[[3 or 4]+k*4].
namespace {
static inline size_t btL_off(uint8_t* a, int64_t k){ return (size_t)((int64_t)(int32_t)E(a,3) + k*4); }
static inline size_t btR_off(uint8_t* a, int64_t k){ return (size_t)((int64_t)(int32_t)E(a,4) + k*4); }
static inline int32_t& BTL(uint8_t* a, int64_t k){ return i32(a, btL_off(a,k)); }
static inline int32_t& BTR(uint8_t* a, int64_t k){ return i32(a, btR_off(a,k)); }
static inline uint8_t  WIN(uint8_t* a, int64_t k){ return u8(a, (size_t)((int64_t)(int32_t)E(a,0) + k)); }

// FUN_00421e60: insert position param_1 into the BT (threshold param_2).
void bt_insert(uint8_t* a, int param_1, int param_2) {
    int iVar6 = E(a,2);
    E(a,2) = param_1;
    if (iVar6 <= param_2) { BTR(a,param_1) = 0; BTL(a,param_1) = 0; return; }
    size_t local_14 = btL_off(a, param_1);
    int iVar5 = 0;
    size_t local_10 = btR_off(a, param_1);
    int local_8 = 0, local_c = 0;
    while (true) {
        int iVar1 = iVar5 + iVar6;
        int iVar2 = (int)WIN(a, iVar1) - (int)WIN(a, (int64_t)iVar5 + param_1);
        int iVar3 = iVar5;
        if (iVar2 == 0) {
            int iVar4 = (iVar1 - iVar6) + param_1;
            do {
                iVar3++; iVar4++;
                if (0x31 < iVar3) break;
                iVar2 = (int)WIN(a, (int64_t)(iVar1 - iVar5) + iVar3) - (int)WIN(a, iVar4);
            } while (iVar2 == 0);
        }
        if (iVar2 < 0) {
            if (local_c < iVar3) {
                if (0x31 < iVar3) { i32(a,local_14)=BTL(a,iVar6); i32(a,local_10)=BTR(a,iVar6); return; }
                iVar5 = local_8; local_c = iVar3;
                if (iVar3 <= local_8) iVar5 = iVar3;
            }
            i32(a, local_10) = iVar6;
            local_10 = btL_off(a, iVar6);
            iVar6 = i32(a, local_10);
        } else {
            if (local_8 < iVar3) {
                if (0x31 < iVar3) { i32(a,local_14)=BTL(a,iVar6); i32(a,local_10)=BTR(a,iVar6); return; }
                iVar5 = iVar3; local_8 = iVar3;
                if (local_c <= iVar3) iVar5 = local_c;
            }
            i32(a, local_14) = iVar6;
            local_14 = btR_off(a, iVar6);
            iVar6 = i32(a, local_14);
        }
        if (iVar6 <= param_2) { i32(a,local_14)=0; i32(a,local_10)=0; return; }
    }
}

// FUN_00421bb0: search the BT for matches at param_1; fill in_EAX[len+0x14]
// with the offset-code per length; return the best (frame-capped) length.
int bt_search(uint8_t* a, uint32_t param_1) {
    uint32_t local_1c = (uint32_t)E(a,2);
    uint32_t uVar1 = (param_1 - (uint32_t)E(a,1)) + 4;
    E(a,2) = (int32_t)param_1;
    if (local_1c <= uVar1) { BTR(a,param_1)=0; BTL(a,param_1)=0; return 0; }
    size_t local_18 = btL_off(a, param_1);
    int iVar2 = 0, iVar6 = 0;
    size_t local_14 = btR_off(a, param_1);
    int local_10 = 0, local_c = 0;
    bool toC9F = false;
    while (true) {
        int iVar9 = (int)local_1c + iVar6;
        int iVar5 = (int)WIN(a, iVar9) - (int)WIN(a, (int64_t)param_1 + iVar6);
        int iVar3 = iVar6;
        bool toC7C = false;
        if (iVar5 == 0) {
            int iVar8 = (iVar9 - (int)local_1c) + (int)param_1;
            do {
                iVar3++; iVar8++;
                if (0x100 < iVar3) { toC7C = true; break; }
                iVar5 = (int)WIN(a, (int64_t)(iVar9 - iVar6) + iVar3) - (int)WIN(a, iVar8);
            } while (iVar5 == 0);
        }
        if (!toC7C && iVar5 < 0) {
            if (local_c < iVar3) {
                if (iVar2 < iVar3) toC7C = true;
                else { iVar6 = local_10; local_c = iVar3; if (iVar3 <= local_10) iVar6 = iVar3; }
            }
            if (!toC7C) {
                i32(a, local_14) = (int32_t)local_1c;
                local_14 = btL_off(a, local_1c);
                local_1c = (uint32_t)i32(a, local_14);
            }
        } else if (!toC7C) {  // iVar5 >= 0
            if (local_10 < iVar3) {
                if (iVar2 < iVar3) {
                    size_t piVar4 = (size_t)(iVar2 + 0x14)*4;
                    do { iVar2++; piVar4 += 4; i32(a,piVar4) = (int32_t)((param_1 - local_1c) + 2); }
                    while (iVar2 < iVar3);
                    if (0x31 < iVar3) {
                        i32(a, local_18) = BTL(a, local_1c);
                        i32(a, local_14) = BTR(a, local_1c);
                        toC9F = true;
                    }
                }
                if (!toC9F) { iVar6 = local_c; local_10 = iVar3; if (iVar3 < local_c) iVar6 = iVar3; }
            }
            if (!toC9F) {
                i32(a, local_18) = (int32_t)local_1c;
                local_18 = btR_off(a, local_1c);
                local_1c = (uint32_t)BTR(a, local_1c);
            }
        }
        if (toC7C) {  // LAB_00421c7c: extend the match-length table, then maybe c9f
            size_t piVar4 = (size_t)(iVar2 + 0x14)*4;
            do { iVar2++; piVar4 += 4; i32(a,piVar4) = (int32_t)((param_1 - local_1c) + 2); }
            while (iVar2 < iVar3);
            if (0x31 < iVar3) {
                i32(a, local_18) = BTL(a, local_1c);
                i32(a, local_14) = BTR(a, local_1c);
                toC9F = true;
            } else {
                // fell through the original c7c into the iVar5<0 tail handling
                iVar6 = local_10; local_c = iVar3; if (iVar3 <= local_10) iVar6 = iVar3;
                i32(a, local_14) = (int32_t)local_1c;
                local_14 = btL_off(a, local_1c);
                local_1c = (uint32_t)i32(a, local_14);
            }
        }
        if (toC9F) break;
        if (!(uVar1 < local_1c)) { i32(a,local_18)=0; i32(a,local_14)=0; break; }
    }
    // LAB_00421d69: repeat-offset (R0/R1/R2) extension
    if (iVar2 < 2) return 0;
    iVar6 = 0;
    if (0 < iVar2) {
        int64_t b = (int64_t)param_1 - E(a,0x11);
        while (WIN(a, (int64_t)param_1 + iVar6) == WIN(a, b + iVar6)) { iVar6++; if (!(iVar6 < iVar2)) break; }
    }
    if (1 < iVar6) {
        size_t piVar4 = (size_t)(iVar6 + 0x14)*4;
        int iVar3 = iVar6;
        do { i32(a,piVar4)=0; iVar3--; piVar4-=4; } while (1 < iVar3);
        if (0x32 < iVar6) goto e2e;
    }
    {
        int iVar3 = 0;
        if (0 < iVar2) {
            int64_t b = (int64_t)param_1 - E(a,0x12);
            while (WIN(a,(int64_t)param_1+iVar3) == WIN(a,b+iVar3)) { iVar3++; if (!(iVar3<iVar2)) break; }
        }
        if (iVar6 < iVar3) {
            size_t piVar4 = (size_t)(iVar6 + 0x14)*4;
            do { iVar6++; piVar4+=4; i32(a,piVar4)=1; } while (iVar6 < iVar3);
        }
        iVar3 = 0;
        if (0 < iVar2) {
            int64_t b = (int64_t)param_1 - E(a,0x13);
            while (WIN(a,(int64_t)param_1+iVar3) == WIN(a,b+iVar3)) { iVar3++; if (!(iVar3<iVar2)) break; }
        }
        if (iVar6 < iVar3) {
            size_t piVar4 = (size_t)(iVar6 + 0x14)*4;
            do { iVar6++; piVar4+=4; i32(a,piVar4)=2; } while (iVar6 < iVar3);
        }
    }
e2e:
    iVar6 = 0x7fff - (int)(param_1 & 0x7fff);
    if (iVar2 <= iVar6) return iVar2;
    if (iVar6 < 2) return 0;
    return iVar6;
}

// FUN_00421fa0: remove position in_EAX from the BT when it falls below thresh.
void bt_slide_delete(uint8_t* a, uint32_t pos, uint32_t thresh) {
    size_t puVar5 = 8;                      // &[2]
    if ((uint32_t)E(a,2) != pos) return;
    if ((uint32_t)E(a,2) <= thresh) { E(a,2)=0; BTR(a,pos)=0; BTL(a,pos)=0; return; }
    uint32_t uVar4 = (uint32_t)BTL(a,pos); if (uVar4 <= thresh) { BTL(a,pos)=0; uVar4=0; }
    uint32_t uVar3 = (uint32_t)BTR(a,pos); if (uVar3 <= thresh) { BTR(a,pos)=0; uVar3=0; }
    while (true) {
        for (; uVar4 <= uVar3; uVar3 = (uint32_t)BTL(a, uVar3)) {
            if (uVar3 <= thresh) uVar3 = 0;
            i32(a, puVar5) = (int32_t)uVar3;
            if (uVar3 == 0) return;
            puVar5 = btL_off(a, uVar3);
        }
        if (uVar4 <= thresh) uVar4 = 0;
        i32(a, puVar5) = (int32_t)uVar4;
        if (uVar4 == 0) break;
        puVar5 = btR_off(a, uVar4);
        uVar4 = (uint32_t)i32(a, puVar5);
    }
}
} // namespace

static inline uint32_t off_slot(uint8_t* a, uint32_t off);  // defined below

// The encoder instance: owns the arena and the assembled output stream.
struct LzxEnc {
    std::vector<uint8_t> arena;
    std::vector<uint8_t> out;          // assembled compressed stream (callback sink)
    Layout L{};
    uint8_t* A() { return arena.data(); }

    // ---- output callback (replaces the 0x434c / 0x10d3 function pointer) -----
    // LAB_0041dc70: writes the segment as one XEX chunk = [2-byte BE len][data].
    void emit(uint32_t srcOff, int n) {
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)(n & 0xff));
        if (n > 0) out.insert(out.end(), arena.begin()+srcOff, arena.begin()+srcOff+n);
    }

    // ---- FUN_00422e90: flush compressed output buffer, reset bit state ------
    void flush_block() {
        uint8_t* a = A();
        if (i32(a, OFF_OUTCNT) != 0) {
            if (i8(a, OFF_BITFREE) < 0x20)            // pad partial 16-bit word
                write_bits(0, (int8_t)(i8(a, OFF_BITFREE) - 0x10));
            int len = i32(a, OFF_OUTPTR) - i32(a, OFF_OUTBASE);
            if (len > 0) emit((uint32_t)i32(a, OFF_OUTBASE), len);
        }
        i32(a, OFF_OUTCNT) = 0;
        i32(a, OFF_OUTPTR) = i32(a, OFF_OUTBASE);
        u8 (a, OFF_BITFREE) = 0x20;
        i32(a, OFF_BITBUF)  = 0;
    }

    // ---- FUN_00422040: write `nbits` (param_1) bits of `val` (this) ---------
    void write_bits(uint32_t val, int8_t nbits) {
        uint8_t* a = A();
        int8_t c = (int8_t)(i8(a, OFF_BITFREE) - nbits);
        u32(a, OFF_BITBUF) |= val << ((i8(a, OFF_BITFREE) - nbits) & 0x1f);
        i8(a, OFF_BITFREE) = c;
        while (c < 0x11) {
            if (u32(a, OFF_OUTEND) <= u32(a, OFF_OUTPTR)) {  // FUN_00422040: wrap
                i32(a, OFF_OUTERR) = 1;
                i32(a, OFF_OUTPTR) = i32(a, OFF_OUTBASE);
            }
            u8(a, P(a, OFF_OUTPTR)) = u8(a, 0x16);
            i32(a, OFF_OUTPTR) += 1;
            u8(a, P(a, OFF_OUTPTR)) = u8(a, 0x17);
            i32(a, OFF_OUTPTR) += 1;
            i32(a, OFF_BITBUF) <<= 0x10;
            i8(a, OFF_BITFREE) += 0x10;
            c = i8(a, OFF_BITFREE);
        }
    }

    // ---- FUN_00423a70: build offset->position-slot table at byte 0x45c ------
    void build_slot_table() {
        uint8_t* a = A();
        u8(a,0x45c)=0; u8(a,0x45d)=1; u8(a,0x45e)=2; u8(a,0x45f)=3;
        int size = 2, pos = 4; uint8_t val = 4;
        do {
            std::memset(a+0x45c+pos, val, size); pos += size;
            uint8_t cur = val; val = (uint8_t)(val + 1);
            std::memset(a+0x45c+pos, val, size); pos += size;
            val = (uint8_t)(cur + 2);
            size *= 2;
        } while (pos < 0x400);
    }

    // ---- FUN_00420ec0: per-run state + default tree lengths ----------------
    void init_run() {
        uint8_t* a = A();
        int32_t win = E(a,1);
        E(a,0)      = E(a,0x10cc) - win;        // window working base (biased)
        E(a,3)      = E(a,0x10cd) - win*4;      // BT left  (biased)
        E(a,0x116)  = win;                      // cur pos
        E(a,0x21b)  = win;
        E(a,4)      = E(a,0x10ce) - win*4;      // BT right (biased)
        E(a,2)      = 0;
        E(a,0x11)=1; E(a,0x12)=1; E(a,0x13)=1;  // R0/R1/R2 (parse)
        E(a,0xe)=1;  E(a,0xf)=1;  E(a,0x10)=1;  // R0/R1/R2 (emit)
        u8(a,0x97d) = 1;                        // block-pending flag
        E(a,0x260) = 1;
        u8(a,0x18) = 0x20;                      // bit free-count
        E(a,5)=0; E(a,7)=0;
        std::memset(a+0x3960, 0, (size_t)E(a,0x21c)*8 + 0x100);  // [0xe58]
        std::memset(a+0x41f4, 0, 0xf9);                          // [0x107d]
        std::memset(a+0x2540, 8, 0x100);        // main-tree lengths: literals=8
        std::memset(a+0x2640, 9, (size_t)E(a,0x21c)*8);          // slot syms=9
        std::memset(a+0x27fd, 6, 0xf9);         // length-tree lengths=6
        i32(a,0x431e) = 0x03030303;             // aligned-tree lengths=3
        i32(a,0x4322) = 0x03030303;
        uint32_t uv = u8(a,0x55c) + 0x12;
        if (uv < (uint32_t)E(a,0x21c)) {
            uint32_t off = 0x2640 + uv*8;
            do { u8(a,off)=100; uv++; off+=8; } while (uv < (uint32_t)E(a,0x21c));
        }
        E(a,0x264)=E(a,0x116); E(a,0x263)=E(a,0x116);
        E(a,0x21a)=0; E(a,0x261)=1;
        std::memset(a + P(a,0x34), 0, 0x2000);  // flag buffer
        E(a,9)=0; E(a,10)=0; u8(a,0x87c)=0;
        E(a,0xe)=1; E(a,0xf)=1; E(a,0x10)=1;
        E(a,0x267)=0; E(a,0x10cf)=0;
        std::memset(a+0x28f8, 0, 0xaf0);        // main/len frequency tables
        std::memset(a+0x3c1e, 0, 0x3e4);
        for (uint32_t o = 0x42ee; o <= 0x430a; o += 4) i32(a,o)=0;  // aligned freq
        std::memset(a + P(a,0x4330), 0, (size_t)E(a,0x262) + 0x1101 + E(a,1)); // window
    }

    // ---- FUN_00420190 + FUN_004210a0 + FUN_004220c0: allocate + init -------
    void setup(uint32_t window, uint32_t e8size) {
        if (window & 0x7fff) window &= 0xffff8000u;
        if ((int)window < 0x8000) window = 0x8000;
        uint32_t W = window, C = window;
        // number of position slots (FUN_004210a0)
        int32_t slots = 4; { uint32_t acc = 4;
            do { uint8_t eb = kExtraBits[slots]; slots++; acc += (1u << eb); } while (acc < W); }
        // arena layout
        uint32_t off = 0x4350;
        auto place = [&](uint32_t sz){ off=(off+15)&~15u; uint32_t p=off; off+=sz; return p; };
        L.window  = place(C + 0x1101 + W);
        L.btL     = place((C+W)*4 + 0x4404);
        L.btR     = place((C+W)*4 + 0x4404);
        L.lenbuf  = place(0x10000);
        L.offbuf  = place(0x20000);
        L.flagbuf = place(0x2000);
        L.nodes   = place(0x18150);
        L.outbuf  = place(0x9800);
        L.ptFreq  = place(0x60);   // 20 syms + internal nodes (shorts)
        L.ptLen   = place(0x20);   // 20 pretree code lengths
        L.ptCode  = place(0x40);   // 20 pretree codes (shorts)
        L.total   = (off+15)&~15u;
        arena.assign(L.total, 0);
        uint8_t* a = A();
        E(a,1)      = W;
        E(a,0x262)  = C;
        E(a,0x21c)  = slots;
        E(a,0x21d)  = (int32_t)e8size;
        E(a,0x10cc) = L.window;  E(a,0x10cd) = L.btL;  E(a,0x10ce) = L.btR;
        E(a,0xb)    = L.offbuf;  E(a,0xc)   = L.lenbuf; E(a,0xd)   = L.flagbuf;
        E(a,0x94f)  = L.nodes;
        E(a,0x217)  = L.outbuf;  E(a,0x218) = L.outbuf; E(a,0x219) = L.outbuf + 0x97c0;
        build_slot_table();
        for (uint32_t v = 0; v < 0x100; ++v) {            // popcount table @0x87d
            uint8_t c = 0; for (uint32_t x = v; x; x >>= 1) c += (x & 1);
            u8(a, 0x87d + v) = c;
        }
        init_run();
    }

    // ---- methods defined out-of-line below --------------------------------
    void parse(int param_1);              // FUN_00420460 (optimal parse)
    void emit_partial();                  // FUN_004203c0
    void flush_pending(uint32_t pos);     // FUN_00420d70
    int  block_boundary(uint32_t* pos);   // FUN_00420dc0
    void build_emit_block();              // FUN_00421480
    void emit_one_block(uint32_t mainLen, uint32_t offCount);  // FUN_00421320
    int  count_tokens(uint32_t offIdx, uint32_t from, uint32_t to);  // FUN_004233e0
    void build_codes(int doMain);         // FUN_00423170
    void fill_default_lengths();          // FUN_004231e0
    int  decide_aligned(uint32_t offCount);                    // FUN_00423370
    void count_aligned(uint32_t offCount);                     // FUN_00423330
    uint32_t cost_uncompressed();         // FUN_00422450
    void update_repeats(uint32_t offCount);                    // FUN_00422290
    void store_uncompressed(uint32_t outPos, uint32_t len);    // FUN_00422300
    void emit_tokens_verbatim(uint32_t mainLen);               // FUN_00422640
    void emit_tokens_aligned(uint32_t mainLen);                // FUN_004229d0
    void pretree_encode(uint32_t srcLen, uint32_t count, uint32_t prevLen);  // FUN_00422f10
    void emit_aligned_tree();             // FUN_004232e0
    void emit_pretrees();                 // FUN_00423280
    void find_split(uint32_t* outTok, uint32_t tokCount, uint32_t offCount, uint32_t* outOff); // FUN_00423830
    uint32_t eval_split(uint32_t a0, uint32_t a1, uint32_t h0, uint32_t h1);  // FUN_00423500
    void e8_translate(int len, uint32_t dstOff);               // FUN_00422180
    int  feed_window(uint32_t destOff);   // FUN_00422100
    void feed_chunk();                    // FUN_00420350 (per-chunk driver)
    void chunk_flush();                   // FUN_00422e90
    void out_chunk(uint32_t srcOff, int complen, int declen);  // output callback [0x10d3]
    std::vector<uint8_t> output;          // assembled compressed chunks
    std::vector<uint8_t> input;           // source basefile ([0x994]=index, [0x998]=remaining)
};

// ===== emit glue layer =====================================================
// FUN_004233e0: accumulate token frequencies for range [from,to); offIdx is
// the offset-buffer index at `from`. Returns decoded byte count of the range.
int LzxEnc::count_tokens(uint32_t offIdx, uint32_t param_1, uint32_t param_3) {
    uint8_t* a = A();
    int iVar5 = 0;
    uint32_t pos = param_1 & 7;
    uint8_t bVar4 = (uint8_t)((1u << pos) | (1u >> (8 - pos)));
    if (param_1 < param_3) {
        int iVar1 = (int)offIdx * 4;
        do {
            if ((u8(a, (param_1 >> 3) + (uint32_t)i32(a,0x34)) & bVar4) == 0) {
                uint32_t lit = u8(a, (uint32_t)i32(a,0x30) + param_1);
                i16(a, 0x28f8 + lit*2) += 1;
                iVar5 += 1;
            } else {
                uint32_t uVar2 = (uint32_t)i32(a, (uint32_t)i32(a,0x2c) + iVar1);
                uint32_t lenHdr = u8(a, param_1 + (uint32_t)i32(a,0x30));
                uint32_t slot = off_slot(a, uVar2);
                if (lenHdr < 7) {
                    i16(a, 0x2af8 + (lenHdr + slot*8)*2) += 1;
                } else {
                    i16(a, slot*0x10 + 0x2b06) += 1;
                    i16(a, 0x3c10 + lenHdr*2) += 1;
                }
                iVar5 = (int)lenHdr + 2 + iVar5;
                iVar1 += 4;
            }
            param_1 += 1;
            bVar4 = (uint8_t)((bVar4 << 1) | ((bVar4 & 0x80) ? 1 : 0));
        } while (param_1 < param_3);
    }
    return iVar5;
}

// FUN_00423170: build the three Huffman trees from accumulated frequencies.
void LzxEnc::build_codes(int doMain) {
    uint8_t* a = A();
    int n = E(a,0x21c) * 8 + 0x100;
    huff_build(a, n,    0x28f8, 0x2540, 0x33e8, doMain);
    huff_build(a, 0xf9, 0x3c1e, 0x27fd, 0x4002, doMain);
    huff_build(a, 8,    0x42ee, 0x431e, 0x430e, 1);
}

// FUN_004231e0: fill in default code lengths for absent symbols.
void LzxEnc::fill_default_lengths() {
    uint8_t* a = A();
    uint32_t uVar1 = 0;
    do { if (i8(a, 0x2540 + uVar1) == 0) u8(a, 0x2540 + uVar1) = 0xb; uVar1++; } while (uVar1 < 0x100);
    uint32_t mainN = (uint32_t)(E(a,0x21c) * 8 + 0x100);
    if (uVar1 < mainN) {
        do { if (i8(a, 0x2540 + uVar1) == 0) u8(a, 0x2540 + uVar1) = 0xc; uVar1++; } while (uVar1 < mainN);
    }
    uint32_t off = 0x27fd; int iVar3 = 0xf9;
    do { if (i8(a, off) == 0) u8(a, off) = 8; off++; iVar3--; } while (iVar3 != 0);
    uVar1 = u8(a, 0x55c) + 0x12;
    if (uVar1 < (uint32_t)E(a,0x21c)) {
        uint32_t p = 0x2640 + uVar1*8;
        do { u8(a, p) = 100; uVar1++; p += 8; } while (uVar1 < (uint32_t)E(a,0x21c));
    }
}

// FUN_004203c0: refresh the adaptive cost model from accumulated tokens.
void LzxEnc::emit_partial() {
    uint8_t* a = A();
    uint32_t uVar1 = (uint32_t)E(a,9);
    if (uVar1 != 0) {
        if (E(a,0x260) == 0) {
            count_tokens((uint32_t)i32(a,0x2538), (uint32_t)i32(a,0x2534), uVar1);
        } else {
            std::memset(a + 0x28f8, 0, (size_t)(E(a,0x21c) + 0x20) * 0x10);
            std::memset(a + 0x3c1e, 0, 0x1f2);
            count_tokens(0, 0, uVar1);
            E(a,0x260) = 0;
        }
        build_codes(0);
        fill_default_lengths();
        i32(a,0x2534) = E(a,9);
        i32(a,0x2538) = E(a,10);
    }
}

// FUN_00420d70: emit one block from the pending token buffer.
void LzxEnc::flush_pending(uint32_t pos) {
    uint8_t* a = A();
    u8(a,0x97d) = 0;
    E(a,0x260) = 1;
    build_emit_block();
    E(a,0x21b) = (int32_t)pos;
    if ((uint32_t)E(a,9) < 0x1000) E(a,0x94c) = 0x1000;
    else E(a,0x94c) = E(a,9) + 0x1000;
}

// FUN_00420dc0: decide an adaptive block boundary; reset stats and rewind.
int LzxEnc::block_boundary(uint32_t* param_1) {
    uint8_t* a = A();
    uint32_t uVar1 = (uint32_t)E(a,1);
    int iVar2 = E(a,0x21b);
    int iVar3 = (int)*param_1;
    u8(a,0x97d) = 0;
    if (iVar2 - uVar1 < uVar1) uVar1 = (uint32_t)(iVar2 - (int)uVar1);
    if ((int)((int32_t)E(a,0) - E(a,0x10cc) + iVar3) < (int)((iVar3 - iVar2) + (int)uVar1)) return 0;
    uint32_t local_4;
    find_split(&local_4, (uint32_t)E(a,9), (uint32_t)E(a,10), nullptr);
    std::memset(a + 0x28f8, 0, (size_t)(E(a,0x21c) + 0x20) * 0x10);
    std::memset(a + 0x3c1e, 0, 0x1f2);
    count_tokens(0, 0, local_4);
    build_codes(0);
    fill_default_lengths();
    E(a,2) = 0;
    std::memset(a + P(a,0x34), 0, 0x2000);
    *param_1 = (uint32_t)iVar2;
    E(a,0x21a) = 0; E(a,9) = 0; E(a,10) = 0;
    E(a,0x11) = 1; E(a,0x12) = 1; E(a,0x13) = 1;
    E(a,0xe) = 1; E(a,0xf) = 1; E(a,0x10) = 1;
    E(a,0x260) = 1;
    E(a,0x94c) = (int32_t)local_4;
    return 1;
}

// FUN_00421480: split the pending buffer, emit the first block, keep the rest.
void LzxEnc::build_emit_block() {
    uint8_t* a = A();
    u8(a,0x97d) = 0;
    uint32_t local_4, local_8;
    find_split(&local_4, (uint32_t)E(a,9), (uint32_t)E(a,10), &local_8);
    emit_one_block(local_4, local_8);
    if (local_4 == (uint32_t)E(a,9)) {
        std::memset(a + P(a,0x34), 0, 0x2000);
        E(a,9) = 0; E(a,10) = 0;
        fill_default_lengths();
        return;
    }
    std::memmove(a + P(a,0x34), a + (P(a,0x34) + (local_4 >> 3)),
                 (((uint32_t)E(a,9) >> 3) - (local_4 >> 3)) + 1);
    uint32_t uVar1 = ((uint32_t)E(a,9) - local_4) >> 3;
    std::memset(a + (uVar1 + 1 + P(a,0x34)), 0, 0x1fff - uVar1);
    std::memmove(a + P(a,0x30), a + (P(a,0x30) + local_4), (uint32_t)E(a,9) - local_4);
    std::memmove(a + P(a,0x2c), a + (P(a,0x2c) + (size_t)local_8 * 4),
                 (size_t)((uint32_t)E(a,10) - local_8) * 4);
    E(a,9) = (int32_t)((uint32_t)E(a,9) - local_4);
    E(a,10) = (int32_t)((uint32_t)E(a,10) - local_8);
    fill_default_lengths();
}

// FUN_00421320: emit a single LZX block (choose verbatim/aligned/uncompressed).
void LzxEnc::emit_one_block(uint32_t param_1, uint32_t param_2) {
    uint8_t* a = A();
    std::memset(a + 0x28f8, 0, (size_t)(E(a,0x21c) + 0x20) * 0x10);
    std::memset(a + 0x3c1e, 0, 0x1f2);
    uint32_t uVar1 = (uint32_t)count_tokens(0, 0, param_1);
    uint32_t local_4 = (uint32_t)decide_aligned(param_2);
    build_codes(1);
    uint32_t uVar2 = cost_uncompressed();
    if (uVar1 <= uVar2 && (uint32_t)i32(a,0x98c) <= (uint32_t)i32(a,0x990)) local_4 = 3;
    write_bits(local_4 & 0xff, 3);
    write_bits((uVar1 >> 0x10) & 0xff, 8);
    write_bits((uVar1 >> 8) & 0xff, 8);
    write_bits(uVar1 & 0xff, 8);
    if (local_4 == 1) {
        emit_pretrees();
        emit_tokens_verbatim(param_1);
        update_repeats(param_2);
        i32(a,0x990) += (int32_t)uVar1;
        return;
    }
    if (local_4 == 2) {
        emit_aligned_tree();
        emit_pretrees();
        emit_tokens_aligned(param_1);
        update_repeats(param_2);
        i32(a,0x990) += (int32_t)uVar1;
        return;
    }
    if (local_4 == 3) {
        update_repeats(param_2);
        store_uncompressed((uint32_t)i32(a,0x990), uVar1);
    }
    i32(a,0x990) += (int32_t)uVar1;
}

// position-slot for an offset (matches the in-arena 0x45c table logic).
static inline uint32_t off_slot(uint8_t* a, uint32_t off) {
    if (off < 0x400) return u8(a, 0x45c + off);
    if (off < 0x80000) return (uint8_t)(u8(a, 0x45c + (off >> 9)) + 0x12);
    return (uint8_t)((off >> 0x11) + 0x22);
}

// pretree symbol for the length-delta between prev and current tree at index i.
static inline uint8_t delta_sym(uint8_t* a, uint32_t prevOff, uint32_t curOff, uint32_t i) {
    return kDelta[16 + (int)u8(a, prevOff + i) - (int)u8(a, curOff + i)];
}

// ===== block-type / repeat-offset / pretree subsystem ======================
// FUN_00423330: tally aligned-tree (low-3-bit) frequencies over the offsets.
void LzxEnc::count_aligned(uint32_t offCount) {
    uint8_t* a = A();
    uint32_t off = P(a,0x2c);
    uint32_t n = offCount;
    if (n != 0) {
        do {
            uint32_t uVar2 = (uint32_t)i32(a, off);
            off += 4;
            if (0xf < uVar2) i16(a, 0x42ee + (uVar2 & 7)*2) += 1;
            n--;
        } while (n != 0);
    }
}

// FUN_00423370: choose verbatim(1) vs aligned(2) block type.
int LzxEnc::decide_aligned(uint32_t param_3) {
    uint8_t* a = A();
    i16(a,0x42ee)=0; i16(a,0x42f0)=0;
    i32(a,0x42f2)=0; i32(a,0x42f6)=0; i32(a,0x42fa)=0;
    uint32_t uVar6=0, uVar4=0;
    count_aligned(param_3);
    uint32_t off=0x42ee; int iVar3=8, iVar2=0;
    do {
        iVar2 = iVar3;
        uint32_t uVar1 = u16(a, off);
        if (uVar4 < uVar1) uVar4 = uVar1;
        uVar6 += uVar1;
        off += 2; iVar3 = iVar2 - 1;
    } while (iVar3 != 0);
    if (uVar4 <= uVar6/5 || (iVar2 = iVar2 + 1, param_3 < 100)) iVar2 = 1;
    return iVar2;
}

// FUN_00422f10: pretree-encode one tree's code lengths (RLE + delta vs prev).
void LzxEnc::pretree_encode(uint32_t srcLen, uint32_t count, uint32_t prevLen) {
    uint8_t* a = A();
    const uint32_t fF = L.ptFreq, fL = L.ptLen, fC = L.ptCode;
    std::memset(a + fF, 0, 0x60);
    uint8_t saved = u8(a, srcLen + count);
    u8(a, srcLen + count) = 0x7b;
    int iVar3 = 0;
    uint8_t bVar2;
    if (0 < (int)count) {
        do {
            char cVar1 = i8(a, srcLen + iVar3);
            int iVar7 = 0;
            uint32_t pc = srcLen + iVar3 + 1;
            if (i8(a, pc) == cVar1) {
                do { pc++; iVar7++; } while (i8(a, pc) == cVar1);
                if (iVar7 < 4) goto P1_single;
                if (cVar1 == 0) {
                    if (iVar7 < 0x34) {
                        if (iVar7 < 0x14) { i16(a, fF + 17*2) += 1; iVar3 = iVar3 - 1 + iVar7; goto P1_next; }
                    } else { iVar7 = 0x33; }
                    i16(a, fF + 18*2) += 1; iVar3 = iVar3 - 1 + iVar7;
                } else {
                    if (5 < iVar7) iVar7 = 5;
                    bVar2 = delta_sym(a, prevLen, srcLen, iVar3);
                    i16(a, fF + bVar2*2) += 1;
                    i16(a, fF + 19*2) += 1;
                    iVar3 = iVar3 - 1 + iVar7;
                }
            } else {
P1_single:
                bVar2 = delta_sym(a, prevLen, srcLen, iVar3);
                i16(a, fF + bVar2*2) += 1;
            }
P1_next:
            iVar3++;
        } while (iVar3 < (int)count);
    }
    huff_build(a, 0x14, fF, fL, fC, 1);
    for (int i = 0; i < 0x14; i++) write_bits(u8(a, fL + i), 4);
    iVar3 = 0;
    if (0 < (int)count) {
        do {
            uint8_t b = u8(a, srcLen + iVar3);
            uint32_t pb = srcLen + iVar3 + 1;
            int iVar7 = 0;
            char local_b6;
            if (u8(a, pb) == b) {
                do { pb++; iVar7++; } while (u8(a, pb) == b);
                if (iVar7 < 4) goto P2_single;
                if (b == 0) {
                    if (iVar7 < 0x34) {
                        if (iVar7 < 0x14) { local_b6 = 0x11; goto P2_emit; }
                    } else { iVar7 = 0x33; }
                    local_b6 = 0x12;
                } else {
                    if (5 < iVar7) iVar7 = 5;
                    local_b6 = 0x13;
                }
            } else {
P2_single:
                local_b6 = (char)delta_sym(a, prevLen, srcLen, iVar3);
            }
P2_emit:
            write_bits(u16(a, fC + (uint8_t)local_b6*2), u8(a, fL + (uint8_t)local_b6));
            if (local_b6 == 0x11) {
                write_bits(iVar7 - 4, 4); iVar3 = iVar3 - 1 + iVar7;
            } else if (local_b6 == 0x12) {
                write_bits(iVar7 - 0x14, 5); iVar3 = iVar3 - 1 + iVar7;
            } else if (local_b6 == 0x13) {
                write_bits(iVar7 - 4, 1);
                uint8_t dm = delta_sym(a, prevLen, srcLen, iVar3);
                write_bits(u16(a, fC + dm*2), u8(a, fL + dm));
                iVar3 = iVar3 - 1 + iVar7;
            }
            iVar3++;
        } while (iVar3 < (int)count);
    }
    u8(a, srcLen + count) = saved;
    std::memcpy(a + prevLen, a + srcLen, count);
}

// FUN_00423280: pretree-encode the main tree (split lit/match) + length tree.
void LzxEnc::emit_pretrees() {
    uint8_t* a = A();
    pretree_encode(0x2540, 0x100, 0x3960);
    pretree_encode(0x2640, (uint32_t)E(a,0x21c) * 8, 0x3a60);
    pretree_encode(0x27fd, 0xf9, 0x41f4);
}

// FUN_004232e0: build + emit the aligned-offset tree (8 syms, 3 bits each).
void LzxEnc::emit_aligned_tree() {
    uint8_t* a = A();
    huff_build(a, 8, 0x42ee, 0x431e, 0x430e, 1);
    for (int i = 0; i < 8; i++) write_bits(u8(a, 0x431e + i), 3);
}

// Output sink (xextool's [0x10d3] callback). Each 32KB-decoded chunk is stored
// as a 2-byte big-endian compressed length followed by that many bytes.
void LzxEnc::out_chunk(uint32_t srcOff, int complen, int declen) {
    uint8_t* a = A();
    (void)declen;
    output.push_back((uint8_t)((uint32_t)complen >> 8));
    output.push_back((uint8_t)complen);
    output.insert(output.end(), a + srcOff, a + srcOff + complen);
}

// FUN_00420350: per-chunk driver — feed window, optionally E8-translate, parse.
void LzxEnc::feed_chunk() {
    uint8_t* a = A();
    int iVar1 = E(a,0x10cc);
    int iVar2 = E(a,0x116);
    int iVar3 = E(a,0);
    uint32_t dest = (uint32_t)(iVar1 + (iVar2 - iVar1) + iVar3);   // = frame_base + pos
    int sVar4 = feed_window(dest);
    if (-1 < sVar4) {
        if (E(a,0x21d) != 0 && (uint32_t)E(a,0x10cf) < 0x8000)
            e8_translate(sVar4, dest);
        E(a,0x10cf) += 1;
        if (0 < sVar4) parse(sVar4);
    }
}

// FUN_00422100: copy the next ≤0x8000 input bytes into the window at destOff.
int LzxEnc::feed_window(uint32_t destOff) {
    uint8_t* a = A();
    uint32_t sz = (uint32_t)i32(a,0x998);
    if (0x7fff < (int)sz) {
        std::memcpy(a + destOff, input.data() + (uint32_t)i32(a,0x994), 0x8000);
        i32(a,0x998) -= 0x8000;
        i32(a,0x994) += 0x8000;
        return 0x8000;
    }
    if ((int)sz < 1) return 0;
    std::memcpy(a + destOff, input.data() + (uint32_t)i32(a,0x994), sz);
    i32(a,0x994) += i32(a,0x998);
    i32(a,0x998) = 0;
    return (int)sz;
}

// FUN_00422180: Intel-E8 call translation at compress time (operand → relative).
void LzxEnc::e8_translate(int param_2, uint32_t param_3) {
    uint8_t* a = A();
    if (6 < param_2) {
        uint32_t local_c = (uint32_t)i32(a, param_3 + param_2 - 6);
        uint16_t local_8 = u16(a, param_3 + param_2 - 2);
        for (int k = 0; k < 6; k++) u8(a, param_3 + param_2 - 6 + k) = 0xe8;
        uint32_t local_10 = (uint32_t)i32(a,0x99c) - 10 + param_2;
        uint32_t pcVar5 = param_3;
        while (true) {
            char cVar1 = i8(a, pcVar5);
            while (cVar1 != (char)0xe8) { i32(a,0x99c) += 1; pcVar5++; cVar1 = i8(a, pcVar5); }
            uint32_t uVar2 = (uint32_t)i32(a,0x99c);
            if (local_10 <= uVar2) break;
            int iVar4 = i32(a, pcVar5 + 1);
            uint32_t uVar6 = uVar2 + (uint32_t)iVar4;
            uint32_t uVar3;
            if ((-1 < (int)uVar6) && (uVar3 = (uint32_t)i32(a,0x874), uVar6 < uVar2 + uVar3)) {
                if (uVar3 <= uVar6) uVar6 = (uint32_t)iVar4 - uVar3;
                u8(a, pcVar5+2) = (uint8_t)(uVar6 >> 8);
                u8(a, pcVar5+1) = (uint8_t)uVar6;
                u8(a, pcVar5+3) = (uint8_t)(uVar6 >> 0x10);
                u8(a, pcVar5+4) = (uint8_t)(uVar6 >> 0x18);
            }
            pcVar5 += 5; i32(a,0x99c) += 5;
        }
        i32(a, param_3 + param_2 - 6) = (int32_t)local_c;
        u16(a, param_3 + param_2 - 2) = local_8;
        i32(a,0x99c) = (int32_t)(local_10 + 10);
    } else {
        i32(a,0x99c) += param_2;
    }
}

// FUN_00423500: histogram divergence between two 0x400-token windows.
uint32_t LzxEnc::eval_split(uint32_t param_2, uint32_t param_3, uint32_t param_4, uint32_t param_5) {
    uint8_t* a = A();
    int iVar2 = E(a,0x21c) + 0x20;
    uint32_t local_c88 = (uint32_t)iVar2 * 8;
    if (799 < local_c88) return 0;
    int16_t h1[800] = {0}, h2[800] = {0};
    uint32_t c1 = param_2, c2 = param_3;
    int o1 = (int)param_4 * 4, o2 = (int)param_5 * 4;
    uint8_t m1 = (uint8_t)(1u << (param_2 & 7));
    uint8_t m2 = (uint8_t)(1u << (param_3 & 7));
    int n = 0x400;
    do {
        uint32_t s1;
        if ((u8(a, (c1 >> 3) + (uint32_t)i32(a,0x34)) & m1) == 0) s1 = u8(a, (uint32_t)i32(a,0x30) + c1);
        else {
            uint8_t b = u8(a, (uint32_t)i32(a,0x30) + c1);
            uint32_t off = (uint32_t)i32(a, o1 + (uint32_t)i32(a,0x2c));
            uint32_t slot = off_slot(a, off);
            s1 = (b < 7) ? (b + 0x100 + slot*8) : (slot*8 + 0x107);
            o1 += 4;
        }
        h1[s1]++;
        c1++; m1 = (uint8_t)((m1 << 1) | ((m1 & 0x80) ? 1 : 0));
        uint32_t s2;
        if ((u8(a, (c2 >> 3) + (uint32_t)i32(a,0x34)) & m2) == 0) s2 = u8(a, (uint32_t)i32(a,0x30) + c2);
        else {
            uint8_t b = u8(a, (uint32_t)i32(a,0x30) + c2);
            uint32_t off = (uint32_t)i32(a, (uint32_t)i32(a,0x2c) + o2);
            uint32_t slot = off_slot(a, off);
            s2 = (b < 7) ? (b + 0x100 + slot*8) : (slot*8 + 0x107);
            o2 += 4;
        }
        h2[s2]++;
        c2++; m2 = (uint8_t)((m2 << 1) | ((m2 & 0x80) ? 1 : 0));
        n--;
    } while (n != 0);
    uint32_t edi = 0;
    for (uint32_t s = 0; s < local_c88; s++) {
        int v = (int)kSplitWeight[split_mag((uint16_t)h1[s])] - (int)kSplitWeight[split_mag((uint16_t)h2[s])];
        edi += (uint32_t)(v < 0 ? -v : v);
    }
    return edi;
}

// FUN_00423830: find an entropy-based block split point in the token stream.
void LzxEnc::find_split(uint32_t* outTok, uint32_t param_2, uint32_t param_3, uint32_t* outOff) {
    uint8_t* a = A();
    *outTok = param_2;
    if (outOff != nullptr) *outOff = param_3;
    uint8_t local_829;
    if (param_2 < 0x1800 || (local_829 = u8(a,0x87c), 3 < local_829)) return;
    uint16_t local_814[1032];
    uint16_t uVar6 = 0; uint32_t uVar3 = 0;
    if (param_2 >> 3 != 0) {
        uint32_t flag = (uint32_t)i32(a,0x34);
        int j = 0;
        do {
            if ((uVar3 & 7) == 0) local_814[j++] = uVar6;
            uint8_t fb = u8(a, flag + uVar3);
            uVar3++;
            uVar6 = (uint16_t)(uVar6 + u8(a, 0x87d + fb));
        } while (uVar3 < (param_2 >> 3));
    }
    uint32_t local_824 = param_2 - 0x1000;
    if (0x800 < local_824) {
        uVar3 = 0xc00;
        uint32_t local_818 = 0;
        do {
            uint32_t uVar4 = eval_split(uVar3-0x400, uVar3, local_814[(uVar3-0x400)>>6], local_814[uVar3>>6]);
            if (0x578 < uVar4 &&
                (uVar4 = eval_split(uVar3-0x800, uVar3+0x400, local_814[(uVar3-0x800)>>6], local_814[(uVar3+0x400)>>6]), 0x578 < uVar4) &&
                (uVar4 = eval_split(uVar3-0xc00, uVar3+0x800, local_814[(uVar3-0xc00)>>6], local_814[(uVar3+0x800)>>6]), 0x578 < uVar4)) {
                uint32_t uVar4b = uVar3 - 0x200;
                uint32_t uVar7 = 0;
                if (uVar4b < uVar3 + 0x600) {
                    uint32_t uVar8 = uVar3 - 0x600;
                    do {
                        uint32_t uVar5 = eval_split(uVar8, uVar4b, local_814[uVar8>>6], local_814[uVar4b>>6]);
                        if (uVar7 < uVar5) { uVar7 = uVar5; local_818 = uVar4b; }
                        uVar4b += 0x40; uVar8 += 0x40;
                    } while (uVar4b < uVar3 + 0x600);
                    if (0x6a3 < uVar7 && 0xfff < local_818) {
                        u8(a,0x87c) = (uint8_t)(local_829 + 1);
                        *outTok = local_818;
                        if (outOff != nullptr) *outOff = local_814[local_818>>6];
                        break;
                    }
                }
            }
            bool cont = uVar3 < local_824;
            uVar3 += 0x400;
            if (!cont) break;
        } while (true);
    }
}

// FUN_00422e90: pad + ship the current 32KB chunk through the output sink.
void LzxEnc::chunk_flush() {
    uint8_t* a = A();
    if (i32(a,0x868) != 0) {
        if ((int8_t)i8(a,0x18) < 0x20)
            write_bits(0, (int8_t)((char)i8(a,0x18) - 0x10));
        int iVar1 = i32(a,0x860) - i32(a,0x85c);
        if (0 < iVar1) out_chunk(P(a,0x85c), iVar1, i32(a,0x868));
    }
    i32(a,0x868) = 0;
    i32(a,0x860) = i32(a,0x85c);
    u8(a,0x18) = 0x20;
    i32(a,0x14) = 0;
}

// FUN_00422290: replay repeat-offset (R0/R1/R2 @0x38) state across the block.
void LzxEnc::update_repeats(uint32_t offCount) {
    uint8_t* a = A();
    int unaff_EDI = (int)offCount;
    int iVar3 = unaff_EDI - 1;
    uint8_t bVar4 = 0;
    if (-1 < iVar3) {
        uint32_t puVar5 = (uint32_t)i32(a,0x2c) + (uint32_t)iVar3*4;
        do {
            if ((uint32_t)i32(a, puVar5) < 3) bVar4 = 0;
            else { bVar4++; if (2 < bVar4) goto done; }
            iVar3--; puVar5 -= 4;
        } while (-1 < iVar3);
        if (2 < bVar4) goto done;
    }
    iVar3 = 0;
done:
    for (; iVar3 < unaff_EDI; iVar3++) {
        uint32_t uVar1 = (uint32_t)i32(a, (uint32_t)i32(a,0x2c) + (uint32_t)iVar3*4);
        if (uVar1 != 0) {
            if (uVar1 < 3) {
                int t = i32(a, 0x38 + uVar1*4);
                i32(a, 0x38 + uVar1*4) = i32(a, 0x38);
                i32(a, 0x38) = t;
            } else {
                i32(a, 0x40) = i32(a, 0x3c);
                i32(a, 0x3c) = i32(a, 0x38);
                i32(a, 0x38) = (int32_t)(uVar1 - 2);
            }
        }
    }
}

// FUN_00422300: emit an uncompressed block (aligned, R0/R1/R2 header, raw bytes).
void LzxEnc::store_uncompressed(uint32_t param_1, uint32_t param_2) {
    uint8_t* a = A();
    write_bits(0, (int8_t)((char)i8(a,0x18) - 0x10));
    uint32_t piVar3 = 0x38;
    int iVar4 = 3;
    do {
        int iVar1 = i32(a, piVar3);
        u8(a, P(a,0x860)) = (uint8_t)iVar1;          i32(a,0x860) += 1;
        u8(a, P(a,0x860)) = (uint8_t)(iVar1 >> 8);   i32(a,0x860) += 1;
        u8(a, P(a,0x860)) = (uint8_t)(iVar1 >> 0x10); i32(a,0x860) += 1;
        u8(a, P(a,0x860)) = (uint8_t)(iVar1 >> 0x18); i32(a,0x860) += 1;
        piVar3 += 4; iVar4--;
    } while (iVar4 != 0);
    uint32_t uVar2 = param_2 & 1;
    if (param_2 != 0) {
        do {
            u8(a, P(a,0x860)) = WIN(a, param_1); i32(a,0x860) += 1;
            i32(a,0x868) += 1;
            param_1++; param_2--;
            if (i32(a,0x868) == 0x8000) {
                if ((int8_t)i8(a,0x18) < 0x20) write_bits(0, (int8_t)((char)i8(a,0x18) - 0x10));
                int iVar4b = i32(a,0x860) - i32(a,0x85c);
                if (0 < iVar4b) out_chunk(P(a,0x85c), iVar4b, i32(a,0x868));
                i32(a,0x868) = 0;
                i32(a,0x860) = i32(a,0x85c);
                u8(a,0x18) = 0x20;
                i32(a,0x14) = 0;
                u8(a,0x87c) = 0;
            }
        } while (param_2 != 0);
    }
    if (uVar2 != 0) { u8(a, P(a,0x860)) = 0; i32(a,0x860) += 1; }
    i32(a,0x14) = 0;
    u8(a,0x18) = 0x20;
}

// FUN_00422450: estimate compressed cost (bytes) of the block under cur trees.
uint32_t LzxEnc::cost_uncompressed() {
    uint8_t* a = A();
    int iVar6 = 0, iVar5 = 0x4b0, local_8 = 0, local_c = 0;
    uint32_t pb = 0x2541, pw = 0x28fa;
    int local_4 = 0x40;
    do {
        iVar5  += (int)u16(a, pw-2) * u8(a, pb-1);
        iVar6  += (int)u16(a, pw)   * u8(a, pb);
        local_c += (int)u16(a, pw+2) * u8(a, pb+1);
        local_8 += (int)u16(a, pw+4) * u8(a, pb+2);
        pw += 8; pb += 4; local_4--;
    } while (local_4 != 0);
    iVar5 = iVar5 + local_8 + local_c + iVar6;
    uint32_t uVar3 = 0; uint8_t local_d = 0;
    if (E(a,0x21c) != 0) {
        do {
            int iVar7 = (int)uVar3 * 8;
            int ib = (int)uVar3 * 8 + 0x100;
            uint32_t uVar4 = kExtraBits[uVar3];
            local_d++; uVar3 = local_d;
            iVar5 = (u8(a, 0x2542 + ib) + uVar4) * (int)u16(a, 0x28fc + ib*2) + iVar5
                  + (u8(a, 0x2641 + iVar7) + uVar4) * (int)u16(a, 0x28fa + ib*2)
                  + (u8(a, 0x2540 + ib) + uVar4) * (int)u16(a, 0x28f8 + ib*2)
                  + (u8(a, 0x2544 + ib) + uVar4) * (int)u16(a, 0x2900 + ib*2)
                  + (u8(a, 0x2545 + ib) + uVar4) * (int)u16(a, 0x2902 + ib*2)
                  + (u8(a, 0x2546 + ib) + uVar4) * (int)u16(a, 0x2904 + ib*2)
                  + (u8(a, 0x2547 + ib) + uVar4) * (int)u16(a, 0x2906 + ib*2)
                  + (u8(a, 0x2543 + ib) + uVar4) * (int)u16(a, 0x28fe + ib*2);
        } while (uVar3 < (uint32_t)E(a,0x21c));
    }
    int iVar7 = 0; iVar6 = 0;
    uint32_t pb2 = 0x27fe, pw2 = 0x3c20;
    local_4 = 0x53;
    do {
        iVar5 += (int)u16(a, pw2-2) * u8(a, pb2-1);
        iVar6 += (int)u8(a, pb2)    * u16(a, pw2);
        iVar7 += (int)u16(a, pw2+2) * u8(a, pb2+1);
        pw2 += 6; pb2 += 3; local_4--;
    } while (local_4 != 0);
    return (uint32_t)(iVar5 + iVar7 + iVar6 + 7) >> 3;
}

// FUN_00422640: emit the token bitstream for a verbatim block.
void LzxEnc::emit_tokens_verbatim(uint32_t param_1) {
    uint8_t* a = A();
    uint32_t uVar7 = 0;
    if (param_1 != 0) {
        int local_8 = 0;
        do {
            if ((u8(a, (uVar7 >> 3) + (uint32_t)i32(a,0x34)) & (uint8_t)(1 << (uVar7 & 7))) == 0) {
                uint32_t uVar5 = u8(a, uVar7 + (uint32_t)i32(a,0x30));
                write_bits(u16(a, 0x33e8 + uVar5*2), u8(a, 0x2540 + uVar5));
                i32(a,0x868) += 1;
            } else {
                uint8_t bVar2 = u8(a, uVar7 + (uint32_t)i32(a,0x30));
                uint32_t uVar5 = (uint32_t)i32(a, local_8 + (uint32_t)i32(a,0x2c));
                local_8 += 4;
                uint32_t uVar6 = off_slot(a, uVar5);
                if (bVar2 < 7) {
                    write_bits(u16(a, 0x35e8 + ((uint32_t)bVar2 + uVar6*8)*2),
                               u8(a, (uint32_t)bVar2 + 0x2640 + uVar6*8));
                } else {
                    write_bits(u16(a, uVar6*0x10 + 0x35f6), u8(a, 0x2647 + uVar6*8));
                    write_bits(u16(a, 0x3ff4 + (uint32_t)bVar2*2), u8(a, (uint32_t)bVar2 + 0x27f6));
                }
                if (kExtraBits[uVar6] != 0)
                    write_bits(kFooterMask[uVar6] & uVar5, kExtraBits[uVar6]);
                i32(a,0x868) += (int)bVar2 + 2;
            }
            uVar7 += 1;
            if (i32(a,0x868) == 0x8000) { chunk_flush(); u8(a,0x87c) = 0; }
        } while (uVar7 < param_1);
    }
}

// FUN_004229d0: emit the token bitstream for an aligned-offset block.
void LzxEnc::emit_tokens_aligned(uint32_t param_1) {
    uint8_t* a = A();
    uint32_t local_10 = 0;
    if (param_1 != 0) {
        int local_4 = 0;
        do {
            if ((u8(a, (local_10 >> 3) + (uint32_t)i32(a,0x34)) & (uint8_t)(1 << (local_10 & 7))) == 0) {
                uint32_t uVar5 = u8(a, (uint32_t)i32(a,0x30) + local_10);
                write_bits(u16(a, 0x33e8 + uVar5*2), u8(a, 0x2540 + uVar5));
                i32(a,0x868) += 1;
            } else {
                uint8_t bVar3 = u8(a, (uint32_t)i32(a,0x30) + local_10);
                uint32_t uVar5 = (uint32_t)i32(a, local_4 + (uint32_t)i32(a,0x2c));
                local_4 += 4;
                uint32_t uVar6 = off_slot(a, uVar5);
                if (bVar3 < 7) {
                    write_bits(u16(a, 0x35e8 + ((uint32_t)bVar3 + uVar6*8)*2),
                               u8(a, (uint32_t)bVar3 + 0x2640 + uVar6*8));
                } else {
                    write_bits(u16(a, uVar6*0x10 + 0x35f6), u8(a, 0x2647 + uVar6*8));
                    write_bits(u16(a, 0x3ff4 + (uint32_t)bVar3*2), u8(a, (uint32_t)bVar3 + 0x27f6));
                }
                uint32_t local_8 = bVar3;
                uint8_t eb = kExtraBits[uVar6];
                if (eb < 3) {
                    if (eb != 0) write_bits(kFooterMask[uVar6] & uVar5, eb);
                } else {
                    if (3 < eb) write_bits(((1u << (eb - 3)) - 1) & (uVar5 >> 3), eb - 3);
                    uint32_t al = uVar5 & 7;
                    write_bits(u16(a, 0x430e + al*2), u8(a, 0x431e + al));
                }
                i32(a,0x868) += (int)local_8 + 2;
            }
            local_10 += 1;
            if (i32(a,0x868) == 0x8000) { chunk_flush(); u8(a,0x87c) = 0; }
        } while (local_10 < param_1);
    }
}

// ===== FUN_00420460: cost-optimal forward parse =============================
void LzxEnc::parse(int param_1) {
    uint8_t* a = A();
    const int64_t NB = (int32_t)E(a, 0x94f);            // parse-node array base
    uint32_t uVar2, uVar5, uVar7, uVar8u, uVar10;
    int64_t  uVar8, iVar6, iVar9, iVar4, local_2c, local_14, puVar3, pbVar11;
    uint32_t local_4, local_8, local_c, local_10, local_1c, local_20, local_24, local_28, local_30;
    int      local_18;
    uint8_t  bVar1;

    uVar10 = (uint32_t)E(a, 0x116);
    local_c = (uint32_t)(param_1 + uVar10);
    local_30 = uVar10;
    if (E(a, 0x261) == 0) {
        local_24 = uVar10 - 0x32;
        for (int i = 0x32; i != 0; i--) {
            bt_insert(a, local_24, (uVar10 - (uint32_t)E(a,1)) + 4);
            local_24++;
        }
    } else {
        E(a, 0x261) = 0;
        E(a, 0x94c) = 10000;
        if (E(a, 0x21d) == 0) write_bits(0, 1);
        else { write_bits(1, 1); write_bits(u16(a,0x876), 0x10); write_bits(u16(a,0x874), 0x10); }
    }

LAB_520:
    while (true) {
        local_24 = uVar10 + 0x8000;
LAB_530:
        if (local_c <= uVar10) break;                  // -> tail
        uVar8u = local_24 & 0xffff8000;
        if (local_c < (local_24 & 0xffff8000)) uVar8u = local_c;
        local_20 = uVar8u;
        uVar2 = (uint32_t)bt_search(a, uVar10);
        if (((int)uVar2 < 2) ||
            (uVar8u < uVar2 + uVar10 && (uVar2 = uVar8u - uVar10, (int)uVar2 < 2))) {
            u8(a, (uint32_t)E(a,0xc) + E(a,9)) = WIN(a, uVar10);
            E(a,9) += 1;
            local_24++; uVar10++; local_30 = uVar10;
            if (0xfff7 < (uint32_t)E(a,9)) flush_pending(uVar10);
            goto LAB_530;
        }
        if (0x31 < (int)uVar2) {
            local_4 = (uint32_t)E(a, uVar2 + 0x14);
            if (local_4 == 3 && 0x10 < (int)uVar2) {
                bt_insert(a, uVar10 + 1, (uVar10 - (uint32_t)E(a,1)) + 5);
            } else {
                for (local_1c = 1; local_1c < uVar2; local_1c++)
                    bt_insert(a, local_1c + uVar10, (local_1c - (uint32_t)E(a,1)) + 4 + uVar10);
            }
            pbVar11 = ((int64_t)((uint32_t)E(a,9) >> 3)) + (int32_t)E(a,0xd);
            uVar10 += uVar2;
            u8(a, (size_t)pbVar11) |= (uint8_t)(1 << ((uint8_t)E(a,9) & 7));
            i8(a, (uint32_t)E(a,0xc) + E(a,9)) = (int8_t)((char)uVar2 - 2);
            E(a,9) += 1;
            i32(a, (uint32_t)E(a,0xb) + (uint32_t)E(a,10)*4) = (int32_t)local_4;
            E(a,10) += 1;
            if (local_4 < 3) {
                if (local_4 != 0) {
                    int t = E(a,0x11); E(a,0x11) = E(a, local_4 + 0x11); E(a, local_4 + 0x11) = t;
                }
            } else {
                E(a,0x13) = E(a,0x12); E(a,0x12) = E(a,0x11); E(a,0x11) = (int32_t)(local_4 - 2);
            }
            local_30 = uVar10;
            if (0xfff7 < (uint32_t)E(a,9) || 0x7ff7 < (uint32_t)E(a,10)) flush_pending(uVar10);
            goto LAB_520;
        }
        // ---- optimal parse over a short match (2..0x31) --------------------
        local_28 = uVar2 + uVar10;
        local_8 = uVar10 + 0xefd;
        i32(a, NB + 0x2c) = (int32_t)u8(a, 0x2540 + (uint32_t)WIN(a, uVar10));
        i32(a, NB + 0x1c) = (int32_t)uVar10;
        uVar7 = 2;
        if (1 < uVar2) {
            iVar6 = 0x30;
            local_2c = 0x16 * 4;                        // &in_EAX[0x16] as byte off
            do {
                uVar8u = (uint32_t)i32(a, local_2c);
                bVar1 = (uint8_t)off_slot(a, uVar8u);
                if (uVar7 < 9)
                    i32(a, iVar6 + 0x14 + NB) =
                        (int32_t)(u8(a, uVar7 + (uint32_t)bVar1*8 + 0x263e) + kExtraBits[bVar1]);
                else
                    i32(a, iVar6 + 0x14 + NB) =
                        (int32_t)(u8(a, (uint32_t)bVar1*8 + 0x2647) + u8(a, uVar7 + 0x27f4) + kExtraBits[bVar1]);
                i32(a, iVar6 + 4 + NB) = (int32_t)uVar10;
                i32(a, iVar6 + NB) = i32(a, local_2c);
                uVar7++; iVar6 += 0x18; local_2c += 4;
            } while (uVar7 <= uVar2);
        }
        i32(a, NB + 0x14) = 0;
        i32(a, NB + 8)  = E(a,0x11);
        i32(a, NB + 0xc) = E(a,0x12);
        i32(a, NB + 0x10) = E(a,0x13);
        local_2c = NB - (int64_t)uVar10 * 0x18;
        local_18 = uVar10 - 1;
        local_1c = uVar8u - uVar10;
        uVar8 = (int64_t)uVar10 * 0x18 + 4 + local_2c;
        local_10 = uVar10;
        while (true) {
            iVar6 = i32(a, uVar8 + 0x18);
            local_1c = local_1c - 1;
            iVar9 = uVar8 + 0x18;
            local_18 = local_18 + 1;
            uVar2 = uVar10 + 1;
            if (iVar6 != local_18) {
                uVar7 = (uint32_t)i32(a, uVar8 + 0x14);
                if (uVar7 < 3) {
                    if (uVar7 == 0) {
                        iVar4 = local_2c + iVar6 * 0x18;
                        E(a,0x11) = i32(a, local_2c + 8 + iVar6 * 0x18);
                        E(a,0x12) = i32(a, iVar4 + 0xc);
                        iVar6 = i32(a, iVar4 + 0x10);
                    } else {
                        iVar6 = local_2c + iVar6 * 0x18;
                        if (uVar7 == 1) {
                            E(a,0x11) = i32(a, iVar6 + 0xc);
                            E(a,0x12) = i32(a, iVar6 + 8);
                            iVar6 = i32(a, iVar6 + 0x10);
                        } else {
                            E(a,0x11) = i32(a, iVar6 + 0x10);
                            E(a,0x12) = i32(a, iVar6 + 0xc);
                            iVar6 = i32(a, iVar6 + 8);
                        }
                    }
                } else {
                    E(a,0x11) = (int32_t)(uVar7 - 2);
                    iVar6 = local_2c + iVar6 * 0x18;
                    E(a,0x12) = i32(a, iVar6 + 8);
                    iVar6 = i32(a, iVar6 + 0xc);
                }
                E(a,0x13) = (int32_t)iVar6;
            }
            i32(a, uVar8 + 0x1c) = E(a,0x11);
            i32(a, uVar8 + 0x20) = E(a,0x12);
            i32(a, uVar8 + 0x24) = E(a,0x13);
            local_24 = (uint32_t)iVar9;
            if (local_28 == uVar2) goto LAB_9e0;
            local_30 = (uint32_t)bt_search(a, uVar2);
            if ((local_30 + uVar2 <= local_20) || (local_30 = local_1c, 1 < (int)local_1c)) break;
            local_30 = 0;
LAB_7f5:
            if (local_8 <= local_30 + uVar2) goto LAB_93c;
            if (((2 < (int)local_30) || (local_30 == 2 && (uint32_t)E(a,0x16) < 0x800)) &&
                local_28 < local_30 + uVar2) {
                uVar10 = (local_30 - local_10) + uVar2;
                if (0xefb < uVar10) uVar10 = 0xefc;
                int64_t ii6 = (int64_t)(local_28 - local_10) + 1;
                if (ii6 <= (int64_t)uVar10) {
                    int64_t iVar4b = ii6 * 0x18;
                    int64_t cnt = ((int64_t)uVar10 - ii6) + 1;
                    do { i32(a, NB + 0x14 + iVar4b) = (int32_t)0xffffffff; iVar4b += 0x18; cnt--; } while (cnt != 0);
                }
                local_28 = local_30 + uVar2;
            }
            local_4 = (uint32_t)i32(a, uVar8 + 0x28);
            uVar10 = (uint32_t)u8(a, 0x2540 + (uint32_t)WIN(a, uVar2)) + local_4;
            if (uVar10 < (uint32_t)i32(a, uVar8 + 0x40)) {
                i32(a, uVar8 + 0x40) = (int32_t)uVar10;
                i32(a, uVar8 + 0x30) = (int32_t)uVar2;
            }
            uVar7 = 2; uVar8 = iVar9; uVar10 = uVar2;
            if (1 < local_30) {
                local_14 = 0x16 * 4;
                puVar3 = local_24 + 0x40;
                do {
                    uVar8u = (uint32_t)i32(a, local_14);
                    bVar1 = (uint8_t)off_slot(a, uVar8u);
                    if (uVar7 < 9) uVar5 = u8(a, uVar7 + (uint32_t)bVar1*8 + 0x263e);
                    else uVar5 = u8(a, (uint32_t)bVar1*8 + 0x2647) + u8(a, uVar7 + 0x27f4);
                    uVar5 = uVar5 + kExtraBits[bVar1] + local_4;
                    if (uVar5 < (uint32_t)i32(a, puVar3)) {
                        i32(a, puVar3) = (int32_t)uVar5;
                        i32(a, puVar3 - 16) = (int32_t)uVar2;
                        i32(a, puVar3 - 20) = i32(a, local_14);
                    }
                    uVar7++; local_14 += 4; puVar3 += 24;
                } while (uVar7 <= local_30);
            }
        }
        if ((int)local_30 < 0x33) goto LAB_7f5;
LAB_93c:
        local_8 = (uint32_t)E(a, local_30 + 0x14);
        local_4 = local_30 + uVar2;
        puVar3 = local_2c + (int64_t)local_4 * 0x18;
        i32(a, puVar3) = (int32_t)local_8;
        i32(a, puVar3 + 4) = (int32_t)uVar2;
        if (local_8 == 3 && 0x10 < (int)local_30) {
            bt_insert(a, uVar10 + 2, (uVar2 - (uint32_t)E(a,1)) + 5);
        } else {
            for (uVar10 = 1; uVar10 < local_30; uVar10++)
                bt_insert(a, uVar10 + uVar2, (uVar10 - (uint32_t)E(a,1)) + 4 + uVar2);
        }
        uVar2 = local_4;
        if (local_8 < 3) {
            if (local_8 != 0) { int t=E(a,0x11); E(a,0x11)=E(a,local_8+0x11); E(a,local_8+0x11)=t; }
        } else { E(a,0x13)=E(a,0x12); E(a,0x12)=E(a,0x11); E(a,0x11)=(int32_t)(local_8-2); }
LAB_9e0:
        puVar3 = local_2c;
        local_1c = 0;
        uVar10 = (uint32_t)i32(a, local_2c + 4 + (int64_t)uVar2 * 0x18);
        do {
            local_30 = uVar10;
            uVar10 = (uint32_t)i32(a, (int64_t)local_30 * 0x18 + 4 + local_2c);
            local_1c = local_1c + 1;
            i32(a, (int64_t)local_30 * 0x18 + 4 + local_2c) = (int32_t)uVar2;
            uVar2 = local_30;
        } while (local_30 != local_10);
        while (0xfff7 < (uint32_t)E(a,9) + local_1c || 0x7ff7 < (uint32_t)E(a,10) + local_1c)
            flush_pending(local_30);
        do {
            puVar3 = puVar3 + 4 + (int64_t)local_30 * 0x18;
            if (local_30 + 1 < (uint32_t)i32(a, puVar3)) {
                pbVar11 = ((int64_t)((uint32_t)E(a,9) >> 3)) + (int32_t)E(a,0xd);
                u8(a,(size_t)pbVar11) |= (uint8_t)(1 << ((uint8_t)E(a,9) & 7));
                i8(a, (uint32_t)E(a,9) + (uint32_t)E(a,0xc)) =
                    (int8_t)(((char)i32(a,puVar3) - (char)local_30) - 2);
                E(a,9) += 1;
                i32(a, (uint32_t)E(a,0xb) + (uint32_t)E(a,10)*4) =
                    i32(a, local_2c + (int64_t)i32(a,puVar3) * 0x18);
                E(a,10) += 1;
                local_30 = (uint32_t)i32(a, puVar3);
            } else {
                u8(a, (uint32_t)E(a,9) + (uint32_t)E(a,0xc)) = WIN(a, local_30);
                E(a,9) += 1;
                local_30 = local_30 + 1;
            }
            local_1c = local_1c - 1;
            puVar3 = local_2c;
        } while (local_1c != 0);
        if ((uint32_t)E(a,0x94c) <= (uint32_t)E(a,9)) { emit_partial(); E(a,0x94c) += 0x1000; }
        uVar10 = local_30;
        if (u8(a,0x97d) != 0 &&
            (0xfdff < (uint32_t)E(a,9) || 0x7dff < (uint32_t)E(a,10))) {
            int r = block_boundary(&local_30); uVar10 = local_30;
            if (r == 0) { flush_pending(local_30); uVar10 = local_30; }
        }
    }
    // ---- tail -------------------------------------------------------------
    E(a,0x263) = (int32_t)(uVar10 - (uint32_t)E(a,1));
    if (0x7fff < param_1) {
        uint32_t ebp = uVar10;
        uint32_t thresh = (uVar10 - (uint32_t)E(a,1)) + 0x36;
        ebp--;
        for (int k = 0x32; k != 0; k--) { bt_slide_delete(a, ebp, thresh); ebp--; }
        uVar10 = local_30;
        if ((uint32_t)(E(a,0x262) + E(a,1)) <=
            (uint32_t)((int32_t)E(a,0) - E(a,0x10cc) + (int32_t)local_30)) {
            // (*in_EAX - in_EAX[0x10cc]) + local_30
            if (u8(a,0x97d) != 0) {
                int r = block_boundary(&local_30); uVar10 = local_30;
                if (r != 0) goto LAB_520;
            }
            uVar10 = local_30;
            std::memmove(a + P(a,0x4330), a + (uint32_t)(E(a,0x262) + (int32_t)E(a,0x10cc)), E(a,1));
            std::memmove(a + P(a,0x4334), a + (uint32_t)((int32_t)E(a,0x10cd) + E(a,0x262)*4), (size_t)E(a,1)*4);
            std::memmove(a + P(a,0x4338), a + (uint32_t)((int32_t)E(a,0x10ce) + E(a,0x262)*4), (size_t)E(a,1)*4);
            E(a,0x263) = (int32_t)(uVar10 - (uint32_t)E(a,1));
            int32_t iv = E(a,0x262);
            E(a,0) -= iv;
            E(a,3) += iv * -4;
            E(a,4) += iv * -4;
        }
        E(a,0x116) = (int32_t)uVar10;
        return;
    }
    if (u8(a,0x97d) == 0) { E(a,0x116) = (int32_t)uVar10; return; }
    {
        int r = block_boundary(&local_30); uVar10 = local_30;
        if (r == 0) { E(a,0x116) = (int32_t)local_30; return; }
    }
    goto LAB_520;
}

// Top-level: compress `in` into a continuous LZX bitstream (FUN_0041dcc0 loop
// + FUN_00414850 finalize). The SHA-1 block-wrapping is applied by the caller.
std::vector<uint8_t> lzx_compress(const uint8_t* in, size_t in_size,
                                  uint32_t window_size, uint32_t e8_filesize) {
    LzxEnc enc;
    enc.setup(window_size, e8_filesize);
    enc.init_run();
    uint8_t* a = enc.A();
    enc.input.assign(in, in + in_size);
    i32(a, 0x994) = 0;
    i32(a, 0x998) = (int32_t)in_size;
    while (i32(a, 0x998) > 0) {                 // FUN_0041dcc0 per-chunk loop
        enc.feed_chunk();
        if (i32(a, 0x1c) == 0) { enc.build_codes(0); enc.fill_default_lengths(); }
    }
    while (i32(a, 0x24) != 0) enc.build_emit_block();   // FUN_00414850 finalize
    enc.chunk_flush();
    return std::move(enc.output);
}

} // namespace xex::compress
