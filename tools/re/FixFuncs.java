import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
public class FixFuncs extends GhidraScript {
  public void run() throws Exception {
    Memory mem = currentProgram.getMemory();
    MemoryBlock text = mem.getBlock(".text");
    int total = 0;
    for (int pass = 0; pass < 4; pass++) {
      int made = 0;
      for (MemoryBlock b : mem.getBlocks()) {
        if (b.getName().equals(".text") || !b.isInitialized()) continue;
        Address a = b.getStart();
        while (a.compareTo(b.getEnd().subtract(3)) < 0) {
          long v = mem.getInt(a) & 0xffffffffL;
          Address t = toAddr(v);
          if (text.contains(t) && (v & 0xf) == 0) {
            if (getFunctionAt(t) == null && getFunctionContaining(t) == null) {
              if (getInstructionAt(t) == null) {
                new DisassembleCommand(t, null, true).applyTo(currentProgram, monitor);
              }
              if (createFunction(t, null) != null) made++;
            }
          }
          a = a.add(4);
        }
      }
      // also scan text for undefined 16-aligned code after ret/nop padding
      Address a = text.getStart();
      while (a.compareTo(text.getEnd()) < 0) {
        if (getFunctionContaining(a) == null && getInstructionAt(a) == null && (a.getOffset() & 0xf) == 0) {
          int bt = mem.getByte(a) & 0xff;
          if (bt == 0x55 || bt == 0x83 || bt == 0x53 || bt == 0x56 || bt == 0x57 || bt == 0x8b || bt == 0x0f || bt == 0xb8 || bt == 0x31 || bt == 0x8d || bt == 0xe9 || bt == 0xc3 || bt == 0x89 || bt == 0x80 || bt == 0x66 || bt == 0xa1 || bt == 0xc7 || bt == 0x81) {
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            if (createFunction(a, null) != null) made++;
          }
        }
        a = a.add(16);
      }
      analyzeChanges(currentProgram);
      total += made;
      println("pass " + pass + " made " + made);
      if (made == 0) break;
    }
    println("total made " + total);
  }
}
