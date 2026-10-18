#pragma once
//
// Guest-function tracing hooks — DEVELOPMENT ONLY, not part of a play build.
//
// Include this from main.cpp by setting HT_TRACE_GUEST_ENTRY to 1. It is off by
// default because every hook here logs synchronously on the guest thread; with
// all of them enabled a single boot produced ~26k log lines and visibly hurt
// frame pacing, audio continuity and input latency.
//
// ---------------------------------------------------------------------------
// How to hook a guest function (this is the part worth remembering)
// ---------------------------------------------------------------------------
// Codegen emits every guest function as a WEAK alias to __imp__<name>
// (DECLARE_REX_FUNC / DEFINE_REX_FUNC in hydrothunder_init.h). Defining a strong
// <name> here overrides the alias, so the interception applies to the *direct*
// C++ calls the generated code makes, and to indirect ones.
//
// Patching PPCFuncMappings instead only catches calls that go through the
// dispatcher. An earlier attempt did exactly that, saw `xstart` and nothing
// else, and produced a confidently wrong conclusion ("the game never reaches
// main"). It does reach main; the wrapper simply was not bound. Use
// REX_HOOK_RAW.
//
// ---------------------------------------------------------------------------
// What these groups were chasing
// ---------------------------------------------------------------------------
// The boot bug is FIXED (setjmp at 0x826A4760 was truncated to 5 instructions;
// see config/hydrothunder_overrides.toml and investigation-map.md). These hooks
// are kept because they are the tooling that found it, and the same technique —
// hook a layer, read the return values, descend — is how the next one gets
// found. They are grouped in the order the investigation actually went.
//
// Never hook __imp__sub_826A1E70 (memcpy) or __imp__sub_826A2E60 (memset):
// far too hot, they will drown the log.
//

#include <cstdint>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>

#include "hydrothunder_init.h"

// --- helpers ---------------------------------------------------------------

// Trace entry and exit with r3, for following control flow.
#define HT_TRACE_CALL(sub)                                            \
  REX_HOOK_RAW(sub) {                                                 \
    REXLOG_INFO("[ht]   -> " #sub " enter (r3={:08X})", ctx.r3.u32);  \
    __imp__##sub(ctx, base);                                          \
    REXLOG_INFO("[ht]   <- " #sub " return (r3={:08X})", ctx.r3.u32); \
  }

// Trace only the return value, for finding which callee produces an error code.
#define HT_TRACE_RET(sub)                                                    \
  REX_HOOK_RAW(sub) {                                                        \
    const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32;                         \
    __imp__##sub(ctx, base);                                                 \
    REXLOG_ERROR("[ht]     " #sub "(r3={:08X} r4={:08X}) -> {:08X}", a3, a4, \
                 ctx.r3.u32);                                                \
  }

// Read a NUL-terminated guest string. Returns a marker rather than risking a
// fault or dumping binary into the log.
inline std::string HtGuestStr(uint8_t* base, uint32_t addr, size_t max = 96) {
  if (!addr) return "<null>";
  std::string out;
  for (size_t i = 0; i < max; ++i) {
    const char c = static_cast<char>(base[addr + i]);
    if (c == '\0') return out.empty() ? std::string("<empty>") : out;
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
      return "<binary>";
    }
    out.push_back(c);
  }
  return out + "...";
}

// --- group 1: guest entry --------------------------------------------------
//
// xstart (0x823EC9F8) is the CRT entry. Its tail is:
//
//     bl 0x821201d0     <- main(argc, argv, envp)
//     mr r30,r3         <- keep main's return value
//     bl 0x826a38c8     <- CRT teardown
//     bl 0x82793914     <- DbgPrint of the return value
//     bl 0x82793684     <- XamLoaderTerminateTitle
//
// so sub_821201D0 is main. These separate "the game asked to quit" from "the
// thread died inside main", which the log alone cannot distinguish.

REX_HOOK_RAW(xstart) {
  REXLOG_INFO("[ht] xstart entered");
  __imp__xstart(ctx, base);
  REXLOG_INFO("[ht] xstart RETURNED - CRT teardown done, guest thread will exit");
}

REX_HOOK_RAW(sub_821201D0) {
  REXLOG_INFO("[ht] main entered: argc={} argv={:08X}", ctx.r3.u32, ctx.r4.u32);
  __imp__sub_821201D0(ctx, base);
  REXLOG_INFO("[ht] main RETURNED, exit code = {} (0x{:08X})", ctx.r3.s32, ctx.r3.u32);
}

// xstart's pre-main sequence, in call order. The heavy lifting before main is
// the C++ global constructors, which is where this game does its graphics and
// input initialisation.
HT_TRACE_CALL(sub_823EFC68)
HT_TRACE_CALL(sub_823EFAB8)
HT_TRACE_CALL(sub_823EC810)
HT_TRACE_CALL(sub_826A3BD8)
HT_TRACE_CALL(sub_823EFA40)
HT_TRACE_CALL(sub_823EF960)
HT_TRACE_CALL(sub_823EC638)

// --- group 2: thread termination -------------------------------------------
//
// ExTerminateThread_entry (xboxkrnl_threading.cpp:170) calls XThread::Exit,
// which unwinds the host thread via pthread_exit and logs nothing. These are the
// only two guest functions in the image that call it. Both turned out to be CRT
// thread-start trampolines (they fire with lr=0 on worker threads), not error
// paths — but that had to be measured to be ruled out.
REX_HOOK_RAW(sub_823F5708) {
  REXLOG_ERROR("[ht] !! sub_823F5708 entered (calls ExTerminateThread) lr={:08X} r3={:08X}",
               static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
  __imp__sub_823F5708(ctx, base);
  REXLOG_ERROR("[ht] !! sub_823F5708 returned");
}

REX_HOOK_RAW(sub_826B4158) {
  REXLOG_ERROR("[ht] !! sub_826B4158 entered (calls ExTerminateThread) lr={:08X} r3={:08X}",
               static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
  __imp__sub_826B4158(ctx, base);
  REXLOG_ERROR("[ht] !! sub_826B4158 returned");
}

// --- group 3: the null-pointer crash ---------------------------------------
//
//   sub_8212E940:  r3=2044; bl sub_822F6280   <- allocate
//                  beq if null                <- allocation succeeded
//                  r4=r26; bl sub_821704F0    <- copy-construct(this, src)
//
//   sub_821704F0:  memcpy(this, src, 1888)
//                  r11 = *(this+0)            <- copied from *(src+0)
//                  r10 = *(r11+0)             <- FAULTED, r11 == 0
//
// Offset 0 is NOT a vtable: sub_8216FC68's first instruction is `stw r4,0(r3)`,
// so it is an ordinary pointer field whose value came from sub_821A4270.

REX_HOOK_RAW(sub_821704F0) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t src = ctx.r4.u32;
  const uint32_t src_field0 =
      src ? *reinterpret_cast<const uint32_t*>(base + src) : 0xDEADBEEF;
  REXLOG_ERROR("[ht] sub_821704F0(this={:08X}, src={:08X}) *(src+0)={:08X}{}", self, src,
               __builtin_bswap32(src_field0),
               src_field0 == 0 ? "   <-- NULL SOURCE" : "");
  __imp__sub_821704F0(ctx, base);
  REXLOG_ERROR("[ht] sub_821704F0 returned {:08X}", ctx.r3.u32);
}

// ctx.lr on entry is the guest return address, which identifies which of the 16
// call sites is live.
REX_HOOK_RAW(sub_8212E940) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t src = ctx.r4.u32;
  const uint32_t f0 = src ? __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + src)) : 0;
  REXLOG_ERROR("[ht] sub_8212E940(this={:08X}, src={:08X}) *(src+0)={:08X} lr={:08X}{}", self, src,
               f0, static_cast<uint32_t>(ctx.lr), f0 == 0 ? "   <-- ALREADY NULL ON ENTRY" : "");
  __imp__sub_8212E940(ctx, base);
}

// --- group 4: the runtime shader cache -------------------------------------
//
// sub_821A4270 is the shader cache. Its two failing lookups pass pointers into
// .rdata holding HLSL source, with kind=0 / kind=1 selecting vertex or pixel.
// It returns NULL either on an early bail (sub_821A4578 reports failure) or from
// a later "not found" path — measure which, do not infer.

REX_HOOK_RAW(sub_821A4270) {
  const uint32_t kind = ctx.r3.u32;
  const uint32_t a1 = ctx.r4.u32;
  const uint32_t a2 = ctx.r5.u32;
  const std::string name = HtGuestStr(base, a1);
  __imp__sub_821A4270(ctx, base);
  const uint32_t ret = ctx.r3.u32;
  REXLOG_ERROR("[ht] sub_821A4270(kind={}, arg1={:08X} \"{}\", arg2={:08X}) -> {:08X}{}", kind, a1,
               name, a2, ret, ret == 0 ? "   <-- LOOKUP FAILED" : "");
}

REX_HOOK_RAW(sub_821A4578) {
  const uint32_t src = ctx.r3.u32;
  __imp__sub_821A4578(ctx, base);
  REXLOG_ERROR("[ht] sub_821A4578(src={:08X}) -> {:08X} (low byte {:02X} => {})", src, ctx.r3.u32,
               ctx.r3.u32 & 0xFF, (ctx.r3.u32 & 0xFF) ? "SUCCESS" : "FAILURE");
}

// The HLSL compiler is statically linked into the game, so this is recompiled
// guest code — there is no HLE stub to blame. sub_82400440's r8 is an error
// out-parameter: the game's own compiler will say why a shader failed.
REX_HOOK_RAW(sub_82400440) {
  const uint32_t src = ctx.r3.u32;
  const uint32_t len = ctx.r4.u32;
  const uint32_t err_pp = ctx.r8.u32;

  __imp__sub_82400440(ctx, base);

  const uint32_t ret = ctx.r3.u32;
  uint32_t err = 0;
  if (err_pp) {
    err = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + err_pp));
  }
  REXLOG_ERROR("[ht] shader compile: src={:08X} len={} -> ret={:08X} errobj={:08X}", src, len, ret,
               err);
  if (err) {
    REXLOG_ERROR("[ht]   err@blob : {}", HtGuestStr(base, err, 400));
    const uint32_t inner = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + err));
    if (inner) {
      REXLOG_ERROR("[ht]   err@*blob: {}", HtGuestStr(base, inner, 400));
    }
  }
}

// Shader-object creation. A non-zero return here is what nulled the shader:
//   bl sub_824004F0 ; mr r31,r3 ; cmpwi cr6,r31,0 ; bne -> `li r3,0` (return NULL)
REX_HOOK_RAW(sub_824004F0) {
  const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a7 = ctx.r7.u32, a8 = ctx.r8.u32;
  const uint32_t outp = ctx.r10.u32;
  __imp__sub_824004F0(ctx, base);
  const uint32_t ret = ctx.r3.u32;
  uint32_t outv = 0;
  if (outp) outv = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + outp));
  REXLOG_ERROR("[ht] sub_824004F0(r3={:08X} r4={:08X} r7={:08X} r8={:08X}) -> {:08X}  out={:08X}{}",
               a3, a4, a7, a8, ret, outv, ret ? "   <-- ERROR, shader discarded" : "");
}

// --- group 5: descending to the E_UNEXPECTED origin ------------------------
//
// 0x8000FFFF appears neither as an immediate pair nor as a literal anywhere in
// the image, so it had to be found by descending layer by layer:
//
//   sub_824004F0 -> sub_823FFC48 -> sub_823FFBD0 -> sub_823FF708
//                -> sub_823FF3E8 -> sub_82437390 -> sub_82436430
//                -> sub_824E4FC0 -> sub_824E4A80   <- born here
//
// sub_824E4FC0 is called with D3D shader version tokens: 0xFFFE0300 = vs_3_0,
// 0xFFFF0300 = ps_3_0.

HT_TRACE_RET(sub_823FF708)
HT_TRACE_RET(sub_82205518)
HT_TRACE_RET(sub_823ECBD8)
HT_TRACE_RET(sub_823EECA0)
HT_TRACE_RET(sub_823FF3E8)
HT_TRACE_RET(sub_82400810)
HT_TRACE_RET(sub_824E19B8)

// sub_823FF3E8's callees.
HT_TRACE_RET(sub_823FF0B8)
HT_TRACE_RET(sub_823FF160)
HT_TRACE_RET(sub_82415C50)
HT_TRACE_RET(sub_82415D08)
HT_TRACE_RET(sub_82416DE0)
HT_TRACE_RET(sub_824198C8)
HT_TRACE_RET(sub_82421A60)
HT_TRACE_RET(sub_824239B8)
HT_TRACE_RET(sub_82437390)
HT_TRACE_RET(sub_824D7FA8)
HT_TRACE_RET(sub_824DA020)
HT_TRACE_RET(sub_824DCAE0)
HT_TRACE_RET(sub_824DCB28)
HT_TRACE_RET(sub_824DD570)
HT_TRACE_RET(sub_824DD6F0)
HT_TRACE_RET(sub_824DD6F8)
HT_TRACE_RET(sub_8265EDB0)

// sub_82436430's direct callees. sub_826A42A0 / sub_826A4408 are CRT-range
// helpers and left alone.
HT_TRACE_RET(sub_82130E88)
HT_TRACE_RET(sub_823F02B8)
HT_TRACE_RET(sub_823F0350)
HT_TRACE_RET(sub_823FF268)
HT_TRACE_RET(sub_823FFEB8)
HT_TRACE_RET(sub_8241A310)
HT_TRACE_RET(sub_8241C6D8)
HT_TRACE_RET(sub_824219E0)
HT_TRACE_RET(sub_82421A20)
HT_TRACE_RET(sub_824233B8)
HT_TRACE_RET(sub_82423508)
HT_TRACE_RET(sub_82423660)
HT_TRACE_RET(sub_82423A30)
HT_TRACE_RET(sub_82423E58)
HT_TRACE_RET(sub_82423EE8)
HT_TRACE_RET(sub_82433470)
HT_TRACE_RET(sub_824341D8)
HT_TRACE_RET(sub_824362B8)
HT_TRACE_RET(sub_82444570)
HT_TRACE_RET(sub_824445B8)
HT_TRACE_RET(sub_824748F0)
HT_TRACE_RET(sub_82477A50)
HT_TRACE_RET(sub_82478220)
HT_TRACE_RET(sub_82493390)
HT_TRACE_RET(sub_824A6B08)
HT_TRACE_RET(sub_824A6C98)
HT_TRACE_RET(sub_824AAB98)
HT_TRACE_RET(sub_824BA468)
HT_TRACE_RET(sub_824BDE78)
HT_TRACE_RET(sub_824E4FC0)

// sub_824E4FC0's callees.
HT_TRACE_RET(sub_824E3DD0)
HT_TRACE_RET(sub_824E3E10)
HT_TRACE_RET(sub_824E4B60)
HT_TRACE_RET(sub_824E4BF0)
HT_TRACE_RET(sub_824E4CD0)
HT_TRACE_RET(sub_826A4760)

// Innermost source:
//
//   obj    = *(arg+4)
//   flags  = *(obj+40)
//   if ((flags & 0x10000000) == 0) goto status;   <- rlwinm. r11,r11,0,3,3
//   if (*(arg+0) == *(obj+52)) *(obj+2480) = 0x8000FFFF;
//   status:
//   hr = *(obj+2480);
//   if (hr >= 0) hr = 0x8000FFFF;                 <- fallback: no real error
//   return hr;
//
// E_UNEXPECTED is a "nothing was actually recorded" fallback, not a diagnosis.
// The object arrived entirely zeroed, which is by design — sub_824E3E10
// allocates 2496 bytes and memsets them. The real fault was upstream: the
// setjmp at 0x826A4760 returned the jmp_buf pointer instead of 0, so the caller
// took its longjmp-recovery path. The rlwinm. translation here was checked by
// hand and is CORRECT — do not re-suspect it.
REX_HOOK_RAW(sub_824E4A80) {
  const uint32_t arg = ctx.r3.u32;
  auto rd = [&](uint32_t a) -> uint32_t {
    return a ? __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + a)) : 0u;
  };
  const uint32_t obj = rd(arg + 4);
  const uint32_t flags = obj ? rd(obj + 40) : 0;
  const uint32_t status = obj ? rd(obj + 2480) : 0;
  const uint32_t cur = rd(arg + 0);
  const uint32_t lim = obj ? rd(obj + 52) : 0;
  REXLOG_ERROR("[ht] sub_824E4A80: arg={:08X} obj={:08X} flags={:08X} bit28={} "
               "status={:08X} *(arg+0)={:08X} *(obj+52)={:08X}",
               arg, obj, flags, (flags & 0x10000000) ? 1 : 0, status, cur, lim);
  __imp__sub_824E4A80(ctx, base);
  REXLOG_ERROR("[ht] sub_824E4A80 -> {:08X}", ctx.r3.u32);
}
