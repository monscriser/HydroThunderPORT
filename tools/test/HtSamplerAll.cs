// Samples ALL threads of a process every few ms (measurement only, used by hitch_sample.ps1):
// for each round, suspends each thread, reads RIP + stack return candidates, resumes it.
// Output lines: "<ms> <tid> <rip> <ret1> <ret2> ..." (absolute addresses, hex).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

public static class HtSamplerAll {
  [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int id);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] static extern int SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern int ResumeThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int id);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, int size, out IntPtr read);

  public static List<string> Run(int pid, int durationMs, int intervalMs, int depth, long[] modBase, long[] modEnd) {
    var lines = new List<string>();
    var proc = Process.GetProcessById(pid);
    IntPtr ph = OpenProcess(0x0010 | 0x0400, false, pid);
    var handles = new Dictionary<int, IntPtr>();
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    byte[] stack = new byte[8 * 1024];
    var sw = Stopwatch.StartNew();
    while (sw.ElapsedMilliseconds < durationMs && !proc.HasExited) {
      proc.Refresh();
      foreach (ProcessThread t in proc.Threads) {
        IntPtr th;
        if (!handles.TryGetValue(t.Id, out th)) { th = OpenThread(0x0002 | 0x0008 | 0x0040, false, t.Id); handles[t.Id] = th; }
        if (th == IntPtr.Zero) continue;
        for (int k = 0; k < 1232; k += 8) Marshal.WriteInt64(ctx, k, 0);
        Marshal.WriteInt32(ctx, 0x30, 0x100001);
        if (SuspendThread(th) == -1) continue;
        var sb = new StringBuilder();
        if (GetThreadContext(th, ctx)) {
          long rip = Marshal.ReadInt64(ctx, 0xF8), rsp = Marshal.ReadInt64(ctx, 0x98);
          sb.Append(sw.ElapsedMilliseconds).Append(' ').Append(t.Id).Append(' ').Append(rip.ToString("X"));
          IntPtr read; int n = 0;
          if (ReadProcessMemory(ph, new IntPtr(rsp), stack, stack.Length, out read)) {
            for (int q = 0; q + 8 <= stack.Length && n < depth; q += 8) {
              long v = BitConverter.ToInt64(stack, q);
              for (int m = 0; m < modBase.Length; ++m) if (v > modBase[m] && v < modEnd[m]) { sb.Append(' ').Append(v.ToString("X")); n++; break; }
            }
          }
        }
        ResumeThread(th);
        if (sb.Length > 0) lines.Add(sb.ToString());
      }
      System.Threading.Thread.Sleep(intervalMs);
    }
    foreach (var h in handles.Values) if (h != IntPtr.Zero) CloseHandle(h);
    Marshal.FreeHGlobal(raw);
    CloseHandle(ph);
    return lines;
  }
}
