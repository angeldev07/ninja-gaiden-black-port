// H5: decompila (y desensambla) las funciones que contienen las direcciones dadas.
// Uso: -process default.xbe -noanalysis -readOnly -postScript H5Decomp.java <salida.txt> <addr> [addr...]
//@category NGB
import java.io.*;
import java.nio.charset.StandardCharsets;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class H5Decomp extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[0]), StandardCharsets.UTF_8));
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		boolean asm = false;
		java.util.List<String> targets = new java.util.ArrayList<>();
		for (int i = 1; i < args.length; i++) {
			if (args[i].equals("--asm")) { targets.add("--asm"); continue; }
			if (args[i].startsWith("callers:")) {
				// Todas las funciones de .text que llaman a la dirección dada.
				Address t = toAddr(Long.decode(args[i].substring(8)));
				java.util.Set<Long> seen = new java.util.TreeSet<>();
				for (ghidra.program.model.symbol.Reference r : getReferencesTo(t)) {
					if (!r.getReferenceType().isCall()) continue;
					Function cf = getFunctionContaining(r.getFromAddress());
					if (cf != null && ".text".equals(currentProgram.getMemory().getBlock(cf.getEntryPoint()).getName())) {
						seen.add(cf.getEntryPoint().getOffset());
					}
				}
				for (Long l : seen) targets.add(String.format("0x%08X", l));
				continue;
			}
			if (args[i].startsWith("refsin:")) {
				// refsin:<bloque>:<símbolo o dirección>: funciones del bloque que referencian el destino.
				String[] p = args[i].split(":", 3);
				Address t;
				if (p[2].startsWith("0x")) {
					t = toAddr(Long.decode(p[2]));
				} else {
					t = getSymbols(p[2], null).isEmpty() ? null : getSymbols(p[2], null).get(0).getAddress();
				}
				if (t == null) { out.println("(símbolo no encontrado: " + p[2] + ")"); continue; }
				java.util.Set<Long> seen = new java.util.TreeSet<>();
				for (ghidra.program.model.symbol.Reference r : getReferencesTo(t)) {
					Function cf = getFunctionContaining(r.getFromAddress());
					if (cf != null && p[1].equals(currentProgram.getMemory().getBlock(cf.getEntryPoint()).getName())) {
						seen.add(cf.getEntryPoint().getOffset());
					}
				}
				out.println("// refsin " + p[1] + " -> " + p[2] + " @ " + t + ": " + seen.size() + " funciones");
				for (Long l : seen) targets.add(String.format("0x%08X", l));
				continue;
			}
			targets.add(args[i]);
		}
		for (String arg : targets) {
			if (arg.equals("--asm")) { asm = true; continue; }
			Address a = toAddr(Long.decode(arg));
			Function f = getFunctionContaining(a);
			out.println("==================== " + arg + " ====================");
			if (f == null) { out.println("(sin función)"); continue; }
			out.printf("// %s @ %s tamaño=%d%n", f.getName(true), f.getEntryPoint(), f.getBody().getNumAddresses());
			DecompileResults r = di.decompileFunction(f, 60, monitor);
			if (r.decompileCompleted()) {
				out.println(r.getDecompiledFunction().getC());
			} else {
				out.println("(fallo al decompilar: " + r.getErrorMessage() + ")");
			}
			if (asm) {
				InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
				while (ii.hasNext()) {
					Instruction ins = ii.next();
					out.printf("  %s  %s%n", ins.getAddress(), ins);
				}
			}
		}
		di.dispose();
		out.close();
	}
}
