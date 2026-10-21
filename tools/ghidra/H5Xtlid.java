// H5: vuelca la tabla .XTLID (pares id de librería / dirección) con el nombre de la base de ghidra-xbe.
// Uso: -process default.xbe -noanalysis -readOnly -postScript H5Xtlid.java <salida.csv>
//@category NGB
import java.io.*;
import java.nio.charset.StandardCharsets;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class H5Xtlid extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[0]), StandardCharsets.UTF_8));
		out.println("id,address,section,xtlid_namespace,xtlid_name,current_symbol,is_function_entry");
		MemoryBlock b = currentProgram.getMemory().getBlock(".XTLID");
		int n = 0, known = 0;
		for (Address a = b.getStart(); a.add(7).compareTo(b.getEnd()) <= 0; a = a.add(8)) {
			long id = getInt(a) & 0xFFFFFFFFL;
			long va = getInt(a.add(4)) & 0xFFFFFFFFL;
			if (id == 0) continue;
			n++;
			String[] nn = xbeloader.XbeXtlidDb.xtlids.get(id);
			if (nn != null) known++;
			Address t = toAddr(va);
			MemoryBlock tb = currentProgram.getMemory().getBlock(t);
			Symbol s = getSymbolAt(t);
			Function f = getFunctionAt(t);
			out.printf("0x%05X,0x%08X,%s,%s,%s,%s,%s%n", id, va, tb == null ? "?" : tb.getName(),
				nn == null ? "" : nn[0], nn == null ? "" : nn[1], s == null ? "" : s.getName(true), f != null);
		}
		out.close();
		println("H5Xtlid: entradas=" + n + " conocidas=" + known);
	}
}
