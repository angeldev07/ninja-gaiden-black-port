// H5: signature coverage metrics (XbSymbolDatabase + FID), D3D8 list, assessment of the key
// APIs for a D3D8 HLE, scan of NV097 push buffer headers and symbol export.
// Uso: -process default.xbe -noanalysis -readOnly -postScript H5Report.java <reportsDir> <nv097_methods.txt>
//@category NGB
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class H5Report extends GhidraScript {

	static final Set<String> XBSDB_LIBS = new HashSet<>(Arrays.asList(
		"XAPILIB", "D3D8", "D3D8LTCG", "D3DX8", "DSOUND", "XACTENG", "XGRAPHC", "XGRAPHCL",
		"XNET", "XNETN", "XNETS", "XONLINE", "XONLINES", "XONLINLS", "JVS"));

	// Namespaces created by the ghidra-xbe .XTLID analyzer.
	static final Set<String> XTLID_LIBS = new HashSet<>(Arrays.asList(
		"D3D", "D3DX8", "DSOUND", "XAPILIB", "XGRAPHICS", "XACT", "DMUSIC", "XMV", "KEYBOARD", "XKBD"));

	// Address of the kernel thunk table (retail de-XOR) and number of imports, from xbe-default.md.
	static final long KTHUNK = 0x003E3AE0L;
	static final int KIMPORTS = 145;

	PrintWriter metrics;
	Map<Integer, String> nvMethods = new TreeMap<>();
	MemoryBlock text;

	String blockName(Address a) {
		MemoryBlock b = currentProgram.getMemory().getBlock(a);
		return b == null ? "?" : b.getName();
	}

	/** Library of a function: XbSymbolDatabase namespace, "FID" or "" if it has no name. */
	String libOf(Function f) {
		Namespace ns = f.getParentNamespace();
		while (ns != null && !ns.isGlobal()) {
			if (XBSDB_LIBS.contains(ns.getName())) {
				return ns.getName();
			}
			if (XTLID_LIBS.contains(ns.getName())) {
				return "XTLID:" + ns.getName();
			}
			ns = ns.getParentNamespace();
		}
		if (f.getSymbol().getSource() == SourceType.DEFAULT) {
			return "";
		}
		// Functions created at a case label: not library functions.
		if (f.getName().startsWith("caseD_") || f.getName().startsWith("switchD")) {
			return "";
		}
		BookmarkManager bm = currentProgram.getBookmarkManager();
		for (Bookmark b : bm.getBookmarks(f.getEntryPoint())) {
			if (b.getCategory() != null && b.getCategory().contains("Function ID")) {
				return "FID";
			}
		}
		if (f.getName().equals("entry")) {
			return "ENTRY";
		}
		return "OTHER:" + f.getSymbol().getSource();
	}

	/** Library family for grouping: D3D8 and D3D8LTCG are the same. */
	static String family(String lib) {
		if (lib.startsWith("D3D8") || lib.equals("XTLID:D3D")) return "D3D8";
		if (lib.equals("XAPILIB") || lib.equals("XTLID:XAPILIB")) return "XAPI";
		if (lib.startsWith("XONLINE")) return "XONLINE";
		if (lib.contains("XGRAPHIC")) return "XGRAPHICS";
		if (lib.contains("D3DX8")) return "D3DX8";
		if (lib.contains("DSOUND")) return "DSOUND";
		if (lib.equals("FID") || lib.startsWith("OTHER")) return "LIBC/LIBCPMT (FID)";
		return lib;
	}

	static class RefCount {
		int callText, jmpText, callD3D, callOther, dataText, dataOther;
		Set<Address> callerFuncsText = new TreeSet<>();
		int total() { return callText + jmpText + callD3D + callOther; }
	}

	RefCount countRefs(Address target) {
		RefCount rc = new RefCount();
		ReferenceManager rm = currentProgram.getReferenceManager();
		ReferenceIterator it = rm.getReferencesTo(target);
		while (it.hasNext()) {
			Reference r = it.next();
			Address from = r.getFromAddress();
			String blk = blockName(from);
			RefType t = r.getReferenceType();
			boolean inText = ".text".equals(blk);
			if (t.isCall()) {
				if (inText) {
					rc.callText++;
					Function cf = getFunctionContaining(from);
					if (cf != null) rc.callerFuncsText.add(cf.getEntryPoint());
				} else if ("D3D".equals(blk)) {
					rc.callD3D++;
				} else {
					rc.callOther++;
				}
			} else if (t.isJump()) {
				if (inText) {
					rc.jmpText++;
					Function cf = getFunctionContaining(from);
					if (cf != null) rc.callerFuncsText.add(cf.getEntryPoint());
				} else if ("D3D".equals(blk)) {
					rc.callD3D++;
				} else {
					rc.callOther++;
				}
			} else if (t.isData()) {
				if (inText) rc.dataText++; else rc.dataOther++;
			}
		}
		return rc;
	}

	static String hex(Address a) {
		return String.format("0x%08X", a.getOffset());
	}

	static String csv(String s) {
		if (s.contains(",") || s.contains("\"")) {
			return "\"" + s.replace("\"", "\"\"") + "\"";
		}
		return s;
	}

	PrintWriter open(File dir, String name) throws IOException {
		return new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, name)), StandardCharsets.UTF_8));
	}

	void m(String fmt, Object... args) {
		String s = String.format(fmt, args);
		metrics.println(s);
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		File dir = new File(args.length > 0 ? args[0]
			: System.getenv("USERPROFILE") + "\\ngb-data\\ghidra\\reports");
		dir.mkdirs();
		if (args.length > 1) {
			for (String line : Files.readAllLines(Paths.get(args[1]))) {
				String[] p = line.trim().split("\\s+");
				if (p.length == 2) nvMethods.put(Integer.decode(p[0]), p[1]);
			}
		}
		metrics = open(dir, "h5-metrics.txt");
		text = currentProgram.getMemory().getBlock(".text");
		FunctionManager fm = currentProgram.getFunctionManager();
		SymbolTable st = currentProgram.getSymbolTable();

		// ---------- 1. Blocks ----------
		m("# Memory blocks");
		m("%-10s %-10s %-10s %10s %s", "block", "start", "end", "size", "flags");
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			m("%-10s %s %s %10d %s%s%s", b.getName(), hex(b.getStart()), hex(b.getEnd()), b.getSize(),
				b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-");
		}

		// ---------- 2. Functions by section and library ----------
		Map<String, Map<String, Integer>> perBlockLib = new TreeMap<>();
		Map<String, Integer> perBlockTotal = new TreeMap<>();
		Map<String, Long> perBlockNamedBytes = new TreeMap<>();
		Map<String, Long> perBlockXbBytes = new TreeMap<>();
		Map<String, Long> perBlockAnyBytes = new TreeMap<>();
		Map<String, Integer> perLib = new TreeMap<>();
		int total = 0, named = 0;
		PrintWriter fcsv = open(dir, "h5-functions.csv");
		fcsv.println("address,name,library,section,size,source,calls_from_text,callers_in_text,calls_total");
		for (Function f : fm.getFunctions(true)) {
			if (f.isExternal()) continue;
			total++;
			String blk = blockName(f.getEntryPoint());
			String lib = libOf(f);
			long size = f.getBody().getNumAddresses();
			perBlockTotal.merge(blk, 1, Integer::sum);
			perBlockAnyBytes.merge(blk, size, Long::sum);
			if (!lib.isEmpty()) {
				named++;
				perLib.merge(lib, 1, Integer::sum);
				perBlockLib.computeIfAbsent(blk, k -> new TreeMap<>()).merge(lib, 1, Integer::sum);
				perBlockNamedBytes.merge(blk, size, Long::sum);
				if (XBSDB_LIBS.contains(lib) || lib.startsWith("XTLID:")) perBlockXbBytes.merge(blk, size, Long::sum);
			}
			RefCount rc = countRefs(f.getEntryPoint());
			fcsv.printf("%s,%s,%s,%s,%d,%s,%d,%d,%d%n", hex(f.getEntryPoint()), csv(f.getName()), lib, blk, size,
				f.getSymbol().getSource(), rc.callText + rc.jmpText, rc.callerFuncsText.size(), rc.total());
		}
		fcsv.close();
		m("");
		m("# Functions");
		m("total=%d named=%d unnamed=%d", total, named, total - named);
		m("");
		m("## Named functions by library (origin)");
		for (Map.Entry<String, Integer> e : perLib.entrySet()) m("%-14s %d", e.getKey(), e.getValue());
		m("");
		m("## Named functions by library family");
		Map<String, Integer> perFam = new TreeMap<>();
		for (Map.Entry<String, Integer> e : perLib.entrySet()) perFam.merge(family(e.getKey()), e.getValue(), Integer::sum);
		for (Map.Entry<String, Integer> e : perFam.entrySet()) m("%-22s %d", e.getKey(), e.getValue());
		m("");
		m("## Functions by section (total / bytes in functions / named by library)");
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			String n = b.getName();
			if (!perBlockTotal.containsKey(n)) continue;
			long nb = perBlockNamedBytes.getOrDefault(n, 0L);
			long xb = perBlockXbBytes.getOrDefault(n, 0L);
			long ab = perBlockAnyBytes.getOrDefault(n, 0L);
			m("%-10s total=%5d bytes_func=%8d (%.1f%%) bytes_named=%8d (%.2f%%) bytes_xbsdb=%8d (%.2f%%) by_lib=%s",
				n, perBlockTotal.get(n), ab, 100.0 * ab / b.getSize(), nb, 100.0 * nb / b.getSize(),
				xb, 100.0 * xb / b.getSize(), perBlockLib.getOrDefault(n, Map.of()));
		}

		// ---------- 3. XbSymbolDatabase symbols (functions and data) ----------
		m("");
		m("# XbSymbolDatabase symbols by library (function / data / outside memory)");
		PrintWriter sym = open(dir, "symbols.csv");
		sym.println("address,name,library,kind,section");
		List<Symbol> d3dSyms = new ArrayList<>();
		Set<String> seen = new HashSet<>();
		Set<Address> d3dAddrs = new HashSet<>();
		Set<String> allLibs = new TreeSet<>(XBSDB_LIBS);
		allLibs.addAll(XTLID_LIBS);
		for (String lib : allLibs) {
			Namespace ns = st.getNamespace(lib, currentProgram.getGlobalNamespace());
			if (ns == null) continue;
			int nf = 0, nd = 0, no = 0;
			SymbolIterator it = st.getSymbols(ns);
			while (it.hasNext()) {
				Symbol s = it.next();
				String key = s.getAddress() + s.getName().replaceAll("_[0-9].*$", "");
				if (!seen.add(key)) continue;
				boolean inMem = currentProgram.getMemory().contains(s.getAddress());
				boolean isFunc = getFunctionAt(s.getAddress()) != null;
				String kind = !inMem ? "offset" : (isFunc ? "function" : "data");
				if (!inMem) no++; else if (isFunc) nf++; else nd++;
				if (inMem) {
					sym.printf("%s,%s,%s,%s,%s%n", hex(s.getAddress()), csv(s.getName()), lib, kind, blockName(s.getAddress()));
				}
				if (lib.startsWith("D3D8") || lib.equals("D3D")) d3dSyms.add(s);
			}
			m("%-10s functions=%d data=%d offsets=%d", lib, nf, nd, no);
		}
		// Kernel imports (loader labels in the thunk table).
		Address kt = toAddr(KTHUNK);
		int nk = 0;
		for (int i = 0; i < KIMPORTS; i++) {
			Address a = kt.add(i * 4L);
			Symbol s = st.getPrimarySymbol(a);
			if (s != null && s.getSource() != SourceType.DEFAULT) {
				sym.printf("%s,%s,XBOXKRNL,kernel_thunk,%s%n", hex(a), csv(s.getName()), blockName(a));
				nk++;
			}
		}
		// Functions named by FID, and the entry point.
		int nfid = 0;
		for (Function f : fm.getFunctions(true)) {
			String lib = libOf(f);
			if (lib.equals("FID") || lib.equals("ENTRY")) {
				sym.printf("%s,%s,%s,function,%s%n", hex(f.getEntryPoint()), csv(f.getName(true)), lib, blockName(f.getEntryPoint()));
				if (lib.equals("FID")) nfid++;
			}
		}
		sym.close();
		m("kernel_thunks_named=%d fid_functions=%d", nk, nfid);

		// ---------- 4. D3D8 list ----------
		d3dSyms.sort(Comparator.comparing(Symbol::getAddress));
		PrintWriter d3 = open(dir, "h5-d3d8.csv");
		d3.println("address,name,library,kind,section,size,calls_from_text,jumps_from_text,callers_in_text,calls_from_d3d,calls_other,data_refs_text,data_refs_other");
		for (Symbol s : d3dSyms) {
			Address a = s.getAddress();
			if (!currentProgram.getMemory().contains(a)) continue;
			Function f = getFunctionAt(a);
			boolean isFunc = f != null;
			RefCount rc = countRefs(a);
			d3.printf("%s,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d%n", hex(a), csv(s.getName()), s.getParentNamespace().getName(),
				isFunc ? "function" : "data", blockName(a), isFunc ? f.getBody().getNumAddresses() : 0,
				rc.callText, rc.jmpText, rc.callerFuncsText.size(), rc.callD3D, rc.callOther, rc.dataText, rc.dataOther);
		}
		d3.close();

		// ---------- 5. Addresses of interest ----------
		m("");
		m("# Addresses of interest");
		for (long off : new long[] { 0x003D171BL, 0x003CE6CFL, 0x0032DA55L }) {
			Address a = toAddr(off);
			Function f = getFunctionContaining(a);
			Instruction ins = getInstructionContaining(a);
			m("%s block=%s instr=%s", hex(a), blockName(a), ins == null ? "(no instruction)" : (hex(ins.getAddress()) + " " + ins));
			if (ins != null) {
				Instruction p = ins;
				List<String> prev = new ArrayList<>();
				for (int k = 0; k < 6 && p != null; k++) {
					p = p.getPrevious();
					if (p != null) prev.add(0, hex(p.getAddress()) + " " + p);
				}
				for (String pv : prev) m("    %s", pv);
			}
			if (f == null) {
				m("  function: (none)");
				continue;
			}
			m("  function: %s @ %s (lib=%s, size=%d)", f.getName(true), hex(f.getEntryPoint()), libOf(f), f.getBody().getNumAddresses());
			// Chain of callers (3 levels).
			printCallers(f, 1, 3, new HashSet<>());
		}

		// ---------- 6. Key APIs ----------
		String[][] keys = {
			{ "CreateDevice", "^Direct3D_CreateDevice" },
			{ "Present", "^D3DDevice_Present" },
			{ "Swap", "^D3DDevice_Swap" },
			{ "Clear", "^D3DDevice_Clear" },
			{ "SetRenderState", "^D3DDevice_SetRenderState" },
			{ "SetTextureStageState", "^D3DDevice_SetTextureState|^D3DDevice_SetTextureStageState" },
			{ "SetTexture", "^D3DDevice_SetTexture($|_\\d)" },
			{ "SetVertexShader", "^D3DDevice_SetVertexShader($|_\\d)" },
			{ "SetVertexShaderConstant", "^D3DDevice_SetVertexShaderConstant" },
			{ "CreateVertexShader/Load/Select", "^D3DDevice_(Create|Load|Select)VertexShader" },
			{ "SetPixelShader", "^D3DDevice_SetPixelShader($|_\\d)" },
			{ "SetPixelShaderConstant", "^D3DDevice_SetPixelShaderConstant" },
			{ "CreatePixelShader", "^D3DDevice_CreatePixelShader" },
			{ "SetStreamSource", "^D3DDevice_SetStreamSource" },
			{ "SetIndices", "^D3DDevice_SetIndices" },
			{ "DrawVertices", "^D3DDevice_DrawVertices($|_\\d)" },
			{ "DrawIndexedVertices", "^D3DDevice_DrawIndexedVertices($|_\\d)" },
			{ "DrawVerticesUP", "^D3DDevice_DrawVerticesUP" },
			{ "DrawIndexedVerticesUP", "^D3DDevice_DrawIndexedVerticesUP" },
			{ "Begin/End (immediate)", "^D3DDevice_(Begin|End)($|_\\d)" },
			{ "BeginPush/EndPush", "^D3DDevice_(Begin|End)Push" },
			{ "MakeSpace/MakeRequestedSpace", "MakeSpace|MakeRequestedSpace" },
			{ "KickOff", "KickOff" },
			{ "RunPushBuffer", "RunPushBuffer" },
			{ "CreateTexture", "CreateTexture" },
			{ "Texture/Surface Lock", "LockRect|LockBox|_Lock($|_)" },
			{ "CreateVertexBuffer", "CreateVertexBuffer" },
			{ "CreateIndexBuffer", "CreateIndexBuffer" },
			{ "Resource Register/AddRef/Release", "^D3DResource_" },
			{ "SetRenderTarget", "^D3DDevice_SetRenderTarget" },
			{ "SetViewport/SetTransform", "^D3DDevice_Set(Viewport|Transform)" },
			{ "SetLight/LightEnable", "^D3DDevice_(SetLight|LightEnable)" },
			{ "Fences/BlockOn*", "Fence|BlockOn|BlockUntil" },
			{ "Init HW / frame buffers", "CMiniport_|FrameBuffers|Reset" },
		};
		PrintWriter kcsv = open(dir, "h5-key-apis.csv");
		kcsv.println("api,symbol,address,section,size,calls_from_text,jumps_from_text,callers_in_text,calls_from_d3d,calls_other,data_refs_text,ltcg_regcall");
		m("");
		m("# Key APIs (symbol, calls from .text, calling functions in .text, internal D3D calls)");
		for (String[] k : keys) {
			Pattern p = Pattern.compile(k[1]);
			boolean any = false;
			for (Symbol s : d3dSyms) {
				if (!p.matcher(s.getName()).find()) continue;
				Address a = s.getAddress();
				if (!currentProgram.getMemory().contains(a)) continue;
				Function f = getFunctionAt(a);
				if (f == null) continue;
				any = true;
				RefCount rc = countRefs(a);
				boolean reg = s.getName().contains("__LTCG");
				kcsv.printf("%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%s%n", csv(k[0]), csv(s.getName()), hex(a), blockName(a),
					f.getBody().getNumAddresses(), rc.callText, rc.jmpText, rc.callerFuncsText.size(), rc.callD3D,
					rc.callOther, rc.dataText, reg ? "yes" : "no");
				m("%-28s %-60s %s call_text=%d jmp_text=%d callers_text=%d d3d=%d other=%d data_text=%d",
					k[0], s.getName(), hex(a), rc.callText, rc.jmpText, rc.callerFuncsText.size(), rc.callD3D, rc.callOther, rc.dataText);
			}
			if (!any) {
				kcsv.printf("%s,(not identified),,,,,,,,,,%n", csv(k[0]));
				m("%-28s (not identified)", k[0]);
			}
		}
		kcsv.close();

		// ---------- 7. References to D3D globals from .text (sign of inlining) ----------
		m("");
		m("# D3D globals referenced from .text (direct reads/writes = inlined code)");
		List<Symbol> dataSyms = new ArrayList<>();
		for (Symbol s : d3dSyms) {
			if (!currentProgram.getMemory().contains(s.getAddress())) continue;
			if (s.getName().contains("g_")) dataSyms.add(s);
		}
		dataSyms.sort(Comparator.comparing(Symbol::getAddress));
		Set<String> doneG = new HashSet<>();
		for (int i = 0; i < dataSyms.size(); i++) {
			Symbol s = dataSyms.get(i);
			if (!doneG.add(s.getName() + s.getAddress())) continue;
			Address start = s.getAddress();
			Address end = start.add(0x3FF);
			for (int j = i + 1; j < dataSyms.size(); j++) {
				if (dataSyms.get(j).getAddress().compareTo(start) > 0) {
					Address nx = dataSyms.get(j).getAddress().subtract(1);
					if (nx.compareTo(end) < 0) end = nx;
					break;
				}
			}
			if (s.getName().equals("D3D_g_pDevice")) end = start.add(3);
			int rd = 0, wr = 0, other = 0;
			Set<Address> funcs = new HashSet<>();
			ReferenceManager rm = currentProgram.getReferenceManager();
			AddressIterator ai = rm.getReferenceDestinationIterator(new AddressSet(start, end), true);
			while (ai.hasNext()) {
				Address dst = ai.next();
				ReferenceIterator ri = rm.getReferencesTo(dst);
				while (ri.hasNext()) {
					Reference r = ri.next();
					if (!".text".equals(blockName(r.getFromAddress()))) { other++; continue; }
					if (r.getReferenceType().isWrite()) wr++; else rd++;
					Function cf = getFunctionContaining(r.getFromAddress());
					if (cf != null) funcs.add(cf.getEntryPoint());
				}
			}
			m("%-36s %s..%s text_reads=%d text_writes=%d text_functions=%d refs_outside_text=%d",
				s.getName(), hex(start), hex(end), rd, wr, funcs.size(), other);
		}

		// ---------- 8. Calls from .text to functions in the D3D section ----------
		MemoryBlock d3dBlk = currentProgram.getMemory().getBlock("D3D");
		PrintWriter dct = open(dir, "h5-d3d-called-from-text.csv");
		dct.println("address,name,library,size,calls_from_text,jumps_from_text,callers_in_text,data_refs_text");
		int surf = 0, surfNamed = 0, callSites = 0, callSitesNamed = 0;
		if (d3dBlk != null) {
			for (Function f : fm.getFunctions(new AddressSet(d3dBlk.getStart(), d3dBlk.getEnd()), true)) {
				RefCount rc = countRefs(f.getEntryPoint());
				if (rc.callText + rc.jmpText + rc.dataText == 0) continue;
				surf++;
				callSites += rc.callText + rc.jmpText;
				String lib = libOf(f);
				if (!lib.isEmpty()) { surfNamed++; callSitesNamed += rc.callText + rc.jmpText; }
				dct.printf("%s,%s,%s,%d,%d,%d,%d,%d%n", hex(f.getEntryPoint()), csv(f.getName()), lib,
					f.getBody().getNumAddresses(), rc.callText, rc.jmpText, rc.callerFuncsText.size(), rc.dataText);
			}
		}
		dct.close();
		m("");
		m("# D3D surface used by .text");
		m("D3D_functions_referenced_from_text=%d named=%d call_sites=%d (to named functions=%d)",
			surf, surfNamed, callSites, callSitesNamed);

		// ---------- 9. Scan of NV097 push buffer headers ----------
		scanPush(dir, fm);

		metrics.close();
		println("H5Report: reports in " + dir);
	}

	void printCallers(Function f, int depth, int max, Set<Function> seen) {
		if (depth > max || !seen.add(f)) return;
		Set<Function> callers = f.getCallingFunctions(monitor);
		String ind = "  ".repeat(depth + 1);
		if (callers.isEmpty()) {
			m("%s(no direct callers; refs=%d)", ind, currentProgram.getReferenceManager().getReferenceCountTo(f.getEntryPoint()));
		}
		int n = 0;
		for (Function c : callers) {
			if (n++ >= 6) { m("%s... (%d callers)", ind, callers.size()); break; }
			m("%s<- %s @ %s [%s, lib=%s]", ind, c.getName(true), hex(c.getEntryPoint()), blockName(c.getEntryPoint()), libOf(c));
			printCallers(c, depth + 1, max, seen);
		}
	}

	/** Finds "mov [mem], imm32" stores whose immediate is a valid NV097 header of a known method. */
	Map<String, Integer> noiseSub = new TreeMap<>();

	void scanPush(File dir, FunctionManager fm) throws IOException {
		Map<String, Map<String, Integer>> perBlockMethod = new TreeMap<>();
		Map<String, Integer> perBlock = new TreeMap<>();
		Map<Address, Map<String, Integer>> perFuncText = new HashMap<>();
		Listing listing = currentProgram.getListing();
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			if (!b.isExecute()) continue;
			InstructionIterator ii = listing.getInstructions(new AddressSet(b.getStart(), b.getEnd()), true);
			while (ii.hasNext()) {
				Instruction ins = ii.next();
				if (!ins.getMnemonicString().equals("MOV") || ins.getNumOperands() != 2) continue;
				int t0 = ins.getOperandType(0);
				if (OperandType.isRegister(t0) || OperandType.isScalar(t0)) continue;
				Scalar sc = ins.getScalar(1);
				if (sc == null) continue;
				long v = sc.getUnsignedValue() & 0xFFFFFFFFL;
				if ((v & 0xA0000003L) != 0) continue; // no JMP/CALL and aligned
				long count = (v >> 18) & 0x7FF;
				if (count == 0) continue;
				int method = (int) (v & 0x1FFC);
				String name = methodName(method);
				if (name == null) continue;
				int subch = (int) ((v >> 13) & 7);
				if (subch != 0) { noiseSub.merge(b.getName(), 1, Integer::sum); continue; }
				String key = String.format("0x%04X %s", method, name) + (subch != 0 ? " sub" + subch : "");
				perBlock.merge(b.getName(), 1, Integer::sum);
				perBlockMethod.computeIfAbsent(b.getName(), k -> new TreeMap<>()).merge(key, 1, Integer::sum);
				if (b == text) {
					Function f = getFunctionContaining(ins.getAddress());
					Address fa = f == null ? ins.getAddress() : f.getEntryPoint();
					perFuncText.computeIfAbsent(fa, k -> new TreeMap<>()).merge(name, 1, Integer::sum);
				}
			}
		}
		m("");
		m("# NV097 scan: 'mov [mem], header' stores (subchannel 0) of known methods, by section");
		m("discarded for subchannel!=0 (noise): %s", noiseSub);
		for (Map.Entry<String, Integer> e : perBlock.entrySet()) {
			m("%-10s %d stores; distinct methods=%d", e.getKey(), e.getValue(), perBlockMethod.get(e.getKey()).size());
		}
		m("");
		m("## Draw and flip methods by section");
		String[] interesting = { "SET_BEGIN_END", "DRAW_ARRAYS", "INLINE_ARRAY", "ARRAY_ELEMENT16", "ARRAY_ELEMENT32",
			"FLIP_STALL", "CLEAR_SURFACE", "SET_TEXTURE_OFFSET", "SET_VERTEX_DATA_ARRAY_FORMAT",
			"SET_VERTEX_DATA_ARRAY_OFFSET", "SET_TRANSFORM_PROGRAM", "SET_TRANSFORM_CONSTANT", "SET_SHADER_STAGE_PROGRAM" };
		for (String blk : perBlockMethod.keySet()) {
			StringBuilder sb = new StringBuilder();
			for (Map.Entry<String, Integer> e : perBlockMethod.get(blk).entrySet()) {
				for (String it : interesting) {
					if (e.getKey().contains(" " + it)) { sb.append(e.getKey()).append('=').append(e.getValue()).append("; "); break; }
				}
			}
			m("%-10s %s", blk, sb);
		}
		PrintWriter pw = open(dir, "h5-nv097-scan.csv");
		pw.println("section,method,stores");
		for (String blk : perBlockMethod.keySet()) {
			for (Map.Entry<String, Integer> e : perBlockMethod.get(blk).entrySet()) {
				pw.printf("%s,%s,%d%n", blk, csv(e.getKey()), e.getValue());
			}
		}
		pw.close();
		PrintWriter pf = open(dir, "h5-nv097-text-functions.csv");
		pf.println("function,name,stores,draw_stores,calls_makespace,methods");
		List<Address> fs = new ArrayList<>(perFuncText.keySet());
		Collections.sort(fs);
		int withDraw = 0;
		for (Address a : fs) {
			Map<String, Integer> mm = perFuncText.get(a);
			int tot = mm.values().stream().mapToInt(Integer::intValue).sum();
			int draw = 0;
			for (String d : new String[] { "SET_BEGIN_END", "DRAW_ARRAYS", "INLINE_ARRAY", "ARRAY_ELEMENT16", "ARRAY_ELEMENT32" }) {
				draw += mm.getOrDefault(d, 0);
			}
			if (draw > 0) withDraw++;
			Function f = getFunctionAt(a);
			int ms = 0;
			if (f != null) {
				for (Function c : f.getCalledFunctions(monitor)) {
					if (c.getName().contains("MakeSpace") || c.getName().contains("MakeRequestedSpace")) ms++;
				}
			}
			pf.printf("%s,%s,%d,%d,%d,%s%n", hex(a), csv(f == null ? "?" : f.getName()), tot, draw, ms, csv(mm.toString()));
		}
		pf.close();
		m("text_functions_with_NV097_stores=%d with_draw_methods=%d", fs.size(), withDraw);
	}

	String methodName(int m) {
		String n = nvMethods.get(m);
		if (n != null) return n;
		// Indexed ranges: textures (0x1B00 + 0x40*i), vertex arrays (+4*i), matrices and constants.
		if (m >= 0x1B00 && m < 0x1C00) { n = nvMethods.get(0x1B00 + (m - 0x1B00) % 0x40); if (n != null) return n; }
		Integer best = null;
		for (Integer k : nvMethods.keySet()) { if (k <= m) best = k; else break; }
		if (best == null) return null;
		String bn = nvMethods.get(best);
		// Only accept known array methods within a small window.
		if (m - best <= 0x3C && (bn.contains("ARRAY") || bn.contains("MATRIX") || bn.contains("CONSTANT")
			|| bn.contains("PROGRAM") || bn.contains("LIGHT") || bn.contains("COMBINER") || bn.contains("TEXGEN"))) {
			return bn;
		}
		return null;
	}
}
