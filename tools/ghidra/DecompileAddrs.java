// Decompile the functions containing the given addresses (hex script args, VAs or RVAs) and
// print them, one after another, so a headless run can dump a reading list:
//   analyzeHeadless ghidra_proj RE0 -process re0hd.fixed.exe -noanalysis \
//     -scriptPath tools/ghidra -postScript DecompileAddrs.java 0x573290 0x529310
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAddrs extends GhidraScript {
  @Override
  public void run() throws Exception {
    DecompInterface ifc = new DecompInterface();
    ifc.openProgram(currentProgram);
    for (String a : getScriptArgs()) {
      long v = Long.parseLong(a.replace("0x", "").replace("0X", ""), 16);
      // An RVA (below the image base) is taken relative to the image.
      long base = currentProgram.getImageBase().getOffset();
      if (v < base) v += base;
      Address addr = toAddr(v);
      Function f = getFunctionContaining(addr);
      if (f == null) {
        f = createFunction(addr, null);
        if (f == null) {
          println("=== " + a + ": no function ===");
          continue;
        }
      }
      DecompileResults r = ifc.decompileFunction(f, 90, monitor);
      println("=== " + a + " in " + f.getName() + " @ " + f.getEntryPoint() + " ===");
      if (r != null && r.getDecompiledFunction() != null) println(r.getDecompiledFunction().getC());
      else println("(decompilation failed)");
    }
    ifc.dispose();
  }
}
