import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpAll extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    PrintWriter pw = new PrintWriter(new FileWriter(out));
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
    while (it.hasNext()) {
      Function f = it.next();
      if (f.isThunk() || f.isExternal()) continue;
      DecompileResults r = d.decompileFunction(f, 60, monitor);
      pw.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
      if (r.decompileCompleted()) pw.println(r.getDecompiledFunction().getC());
    }
    pw.close();
  }
}
