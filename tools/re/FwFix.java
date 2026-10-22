import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.listing.*;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import java.math.BigInteger;
public class FwFix extends GhidraScript {
  void seed(long a) throws Exception {
    Address t = toAddr(a & ~1L);
    if (getFunctionContaining(t) != null) return;
    ArmDisassembleCommand c = new ArmDisassembleCommand(t, null, true);
    c.applyTo(currentProgram, monitor);
    createFunction(t, null);
  }
  public void run() throws Exception {
    Memory mem = currentProgram.getMemory();
    long size = mem.getMaxAddress().getOffset() + 1;
    for (int i = 1; i < 48; i++) {
      long v = mem.getInt(toAddr(i * 4)) & 0xffffffffL;
      if ((v & 1) == 1 && v < size) seed(v);
    }
    for (int pass = 0; pass < 3; pass++) {
      for (long a = 0xc0; a < size - 2; a += 2) {
        int hw = mem.getShort(toAddr(a)) & 0xffff;
        if ((hw & 0xff00) == 0xb500 && getInstructionAt(toAddr(a)) == null && getFunctionContaining(toAddr(a)) == null)
          seed(a | 1);
      }
      analyzeChanges(currentProgram);
    }
    println("functions: " + currentProgram.getFunctionManager().getFunctionCount());
  }
}
