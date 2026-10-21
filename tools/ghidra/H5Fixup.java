// H5: convierte en función cada símbolo de XbSymbolDatabase que cae en código ejecutable
// y que el auto-análisis dejó como simple etiqueta. Después re-analiza los cambios.
// Uso (postScript tras el import): -postScript H5Fixup.java
//@category NGB
import java.util.*;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

public class H5Fixup extends GhidraScript {

	// Prefijos de librería que emite XbSymbolDatabaseCLI (nombre "LIB__Simbolo").
	static final Set<String> XBSDB_LIBS = new HashSet<>(Arrays.asList(
		"XAPILIB", "D3D8", "D3D8LTCG", "D3DX8", "DSOUND", "XACTENG", "XGRAPHC", "XGRAPHCL",
		"XNET", "XNETN", "XNETS", "XONLINE", "XONLINES", "XONLINLS", "JVS"));

	// Los nombres de variables globales no son funciones aunque estén en un bloque ejecutable.
	static boolean looksLikeData(String name) {
		return name.contains("g_") || name.endsWith("_OFFSET") || name.startsWith("D3DRS_")
			|| name.startsWith("D3DTSS_") || name.startsWith("IDirectSound") && name.contains("Vtbl");
	}

	@Override
	public void run() throws Exception {
		long t0 = System.currentTimeMillis();
		SymbolTable st = currentProgram.getSymbolTable();
		Listing listing = currentProgram.getListing();
		int disasm = 0, created = 0, already = 0, skippedData = 0, failed = 0;
		List<String> failures = new ArrayList<>();

		for (String lib : XBSDB_LIBS) {
			Namespace ns = st.getNamespace(lib, currentProgram.getGlobalNamespace());
			if (ns == null) {
				continue;
			}
			SymbolIterator it = st.getSymbols(ns);
			List<Symbol> syms = new ArrayList<>();
			while (it.hasNext()) {
				syms.add(it.next());
			}
			for (Symbol s : syms) {
				Address a = s.getAddress();
				MemoryBlock b = currentProgram.getMemory().getBlock(a);
				if (b == null || !b.isExecute()) {
					continue;
				}
				if (looksLikeData(s.getName())) {
					skippedData++;
					continue;
				}
				Function f = getFunctionAt(a);
				if (f != null) {
					already++;
					// Asegura que el nombre de la firma sea el principal.
					if (!s.isPrimary()) {
						s.setPrimary();
					}
					continue;
				}
				if (listing.getInstructionAt(a) == null) {
					if (listing.getDefinedDataContaining(a) != null) {
						clearListing(a);
					}
					DisassembleCommand dc = new DisassembleCommand(a, null, true);
					if (dc.applyTo(currentProgram, monitor)) {
						disasm++;
					}
				}
				CreateFunctionCmd cf = new CreateFunctionCmd(null, a, null, SourceType.ANALYSIS);
				if (cf.applyTo(currentProgram, monitor)) {
					created++;
					if (!s.isPrimary()) {
						s.setPrimary();
					}
				} else {
					failed++;
					failures.add(lib + "::" + s.getName() + " @ " + a + " -> " + cf.getStatusMsg());
				}
			}
		}
		analyzeChanges(currentProgram);
		long dt = System.currentTimeMillis() - t0;
		println(String.format("H5Fixup: ya_funcion=%d desensamblados=%d funciones_creadas=%d datos_omitidos=%d fallos=%d (%.1f s)",
			already, disasm, created, skippedData, failed, dt / 1000.0));
		for (String f : failures) {
			println("H5Fixup fallo: " + f);
		}
	}
}
