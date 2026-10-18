// Minimal sampling profiler for one process (Windows x64): suspends each selected thread,
// reads RIP and the top of the stack, resumes it. Return-address candidates = stack qwords
// that point into a loaded module. Used by sample_threads.ps1 (measurement only).
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class HtSampler {
  [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int id);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] static extern int SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern int ResumeThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int id);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, int size, out IntPtr read);

  // Returns, per sample: RIP followed by up to `depth` return-address candidates (0 = none).
  public static List<long[]> Sample(int pid, int tid, int samples, int sleepMs, int depth,
                                    long[] modBase, long[] modEnd) {
    var result = new List<long[]>();
    IntPtr th = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);  // suspend|get ctx|query
    IntPtr ph = OpenProcess(0x0010 | 0x0400, false, pid);          // vm read|query
    if (th == IntPtr.Zero || ph == IntPtr.Zero) return result;
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    byte[] stack = new byte[8 * 512];
    for (int i = 0; i < samples; ++i) {
      for (int k = 0; k < 1232; k += 8) Marshal.WriteInt64(ctx, k, 0);
      Marshal.WriteInt32(ctx, 0x30, 0x100001);  // CONTEXT_CONTROL (AMD64)
      if (SuspendThread(th) != -1) {
        if (GetThreadContext(th, ctx)) {
          long rip = Marshal.ReadInt64(ctx, 0xF8);
          long rsp = Marshal.ReadInt64(ctx, 0x98);
          long[] s = new long[depth + 1];
          s[0] = rip;
          IntPtr read;
          if (ReadProcessMemory(ph, new IntPtr(rsp), stack, stack.Length, out read)) {
            int n = 1;
            for (int q = 0; q + 8 <= stack.Length && n <= depth; q += 8) {
              long v = BitConverter.ToInt64(stack, q);
              for (int m = 0; m < modBase.Length; ++m) {
                if (v > modBase[m] && v < modEnd[m]) { s[n++] = v; break; }
              }
            }
          }
          result.Add(s);
        }
        ResumeThread(th);
      }
      System.Threading.Thread.Sleep(sleepMs);
    }
    Marshal.FreeHGlobal(raw);
    CloseHandle(th);
    CloseHandle(ph);
    return result;
  }
}
