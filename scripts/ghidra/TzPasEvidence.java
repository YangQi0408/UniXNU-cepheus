// Offline, explicitly bounded TZ analysis. Only the private Ghidra DB is edited.
// Seed ranges are analyst-supplied hypotheses, not recovered source symbols.
// @category Cepheus
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import com.google.gson.*;
import java.nio.file.*;
import java.util.*;

public class TzPasEvidence extends GhidraScript {
    private String hex(byte[] bytes) {
        StringBuilder s = new StringBuilder();
        for (byte b : bytes) s.append(String.format("%02x", b & 255));
        return s.toString();
    }

    private List<Object> refs(Address address) {
        List<Object> list = new ArrayList<>();
        ReferenceIterator iter = currentProgram.getReferenceManager().getReferencesTo(address);
        while (iter.hasNext()) {
            Reference ref = iter.next();
            Map<String,Object> row = new LinkedHashMap<>();
            row.put("from", ref.getFromAddress().toString());
            row.put("to", ref.getToAddress().toString());
            row.put("type", ref.getReferenceType().toString());
            row.put("source", ref.getSource().toString());
            list.add(row);
        }
        return list;
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output followed by start:end:name seeds");
        Path output = Paths.get(args[0]);
        if (Files.exists(output)) throw new IllegalArgumentException("preserve earlier output");
        Map<String,Object> report = new LinkedHashMap<>();
        report.put("tool", "native Ghidra Headless / TzPasEvidence");
        report.put("program", currentProgram.getName());
        report.put("executable_sha256", currentProgram.getExecutableSHA256());
        report.put("language", currentProgram.getLanguageID().toString());
        report.put("limits", Arrays.asList("offline only; no secure execution",
            "analyst-supplied seed ranges and inferred names; no complete binary analysis",
            "references cover disassembled seeds only; absent references do not prove absence",
            "loaded static data is not live secure memory; decompiler types may be incorrect"));
        List<Object> memory = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            Map<String,Object> row = new LinkedHashMap<>();
            row.put("name", block.getName()); row.put("start", block.getStart().toString());
            row.put("size", block.getSize()); row.put("initialized", block.isInitialized());
            row.put("read", block.isRead()); row.put("write", block.isWrite());
            row.put("execute", block.isExecute()); memory.add(row);
        }
        report.put("memory", memory);
        List<Function> functions = new ArrayList<>();
        List<Object> records = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String[] seed = args[i].split(":", 3);
            if (seed.length != 3) throw new IllegalArgumentException("bad seed");
            Address start = toAddr(seed[0]), end = toAddr(seed[1]);
            if (end.subtract(start) < 0 || end.subtract(start) > 8191)
                throw new IllegalArgumentException("seed too large");
            AddressSet body = new AddressSet(start, end);
            Function overlap = currentProgram.getFunctionManager().getFunctionContaining(start);
            if (overlap != null && (!overlap.getEntryPoint().equals(start) || !overlap.getBody().equals(body)))
                throw new IllegalStateException("seed overlaps different function " + overlap);
            // Restricted disassembly follows branches within the declared body only.
            DisassembleCommand cmd = new DisassembleCommand(start, body, true);
            if (!cmd.applyTo(currentProgram, monitor)) throw new IllegalStateException(cmd.getStatusMsg());
            // Seeds here contain AArch64 instructions only. Decode unreachable switch
            // arms explicitly without inventing jump-table references or CFG edges.
            for (Address address = start; address.compareTo(end) <= 0; address = address.add(4)) {
                if (currentProgram.getListing().getInstructionAt(address) == null &&
                        !new DisassembleCommand(address, body, false).applyTo(currentProgram, monitor))
                    throw new IllegalStateException("undecodable seed instruction " + address);
            }
            Function f = overlap != null ? overlap :
                currentProgram.getFunctionManager().createFunction(seed[2], start, body, SourceType.USER_DEFINED);
            // Explicit AAPCS64 register-argument hypotheses, supported by raw instructions.
            // Do not let a callee's missing prototype hide the unchanged input w0.
            if (seed[2].equals("inferred_pas_lookup_verdict") || seed[2].equals("inferred_pas_lazy_lookup") ||
                    seed[2].equals("inferred_pas_find_descriptor")) {
                f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
                    SourceType.USER_DEFINED, new ParameterImpl("pas_id", UnsignedIntegerDataType.dataType, currentProgram));
                f.setReturnType(seed[2].equals("inferred_pas_find_descriptor") ?
                    new PointerDataType(VoidDataType.dataType) : UnsignedIntegerDataType.dataType, SourceType.USER_DEFINED);
            } else if (seed[2].equals("inferred_descriptor_id")) {
                f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
                    SourceType.USER_DEFINED, new ParameterImpl("descriptor", new PointerDataType(VoidDataType.dataType), currentProgram));
                f.setReturnType(UnsignedIntegerDataType.dataType, SourceType.USER_DEFINED);
            } else if (seed[2].equals("inferred_pas7_wrapper") || seed[2].equals("inferred_pas8_wrapper")) {
                f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED,
                    new ParameterImpl("pas_id", UnsignedIntegerDataType.dataType, currentProgram),
                    new ParameterImpl("results", new PointerDataType(UnsignedLongLongDataType.dataType), currentProgram));
                f.setReturnType(IntegerDataType.dataType, SourceType.USER_DEFINED);
            }
            functions.add(f);
        }
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
            for (Function f : functions) {
                Map<String,Object> row = new LinkedHashMap<>();
                row.put("entry", f.getEntryPoint().toString()); row.put("inferred_name", f.getName());
                row.put("analyst_body_start", f.getBody().getMinAddress().toString());
                row.put("analyst_body_end_inclusive", f.getBody().getMaxAddress().toString());
                row.put("prototype", f.getSignature().toString());
                row.put("prototype_source", f.getSignatureSource().toString());
                List<Object> instructions = new ArrayList<>();
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    Map<String,Object> inst = new LinkedHashMap<>();
                    inst.put("address", ins.getAddress().toString()); inst.put("bytes", hex(ins.getBytes()));
                    inst.put("text", ins.toString());
                    List<Object> outrefs = new ArrayList<>();
                    for (Reference ref : ins.getReferencesFrom()) {
                        Map<String,Object> r = new LinkedHashMap<>();
                        r.put("to", ref.getToAddress().toString()); r.put("type", ref.getReferenceType().toString());
                        outrefs.add(r);
                    }
                    inst.put("references_from", outrefs); instructions.add(inst);
                }
                row.put("instructions", instructions);
                row.put("references_to_entry", refs(f.getEntryPoint()));
                DecompileResults result = decompiler.decompileFunction(f, 30, monitor);
                row.put("decompile_completed", result.decompileCompleted());
                row.put("decompile_error", result.getErrorMessage());
                row.put("pseudocode", result.decompileCompleted() ? result.getDecompiledFunction().getC() : null);
                records.add(row);
            }
        } finally { decompiler.dispose(); }
        report.put("functions", records);
        List<Object> data = new ArrayList<>();
        String[] dataSeeds = {"1c1303f8:192", "1c11c1f0:4", "1c13edf8:8520", "1c165618:240",
            "1c147e28:16", "1c11d188:88", "14680000:32", "146801f8:64", "14680c68:8",
            "1c103078:16", "1c290168:24", "1c295c58:48", "1c29bdf8:240",
            "14690110:96", "14692810:8", "1c281000:110016"};
        for (String spec : dataSeeds) {
            String[] pair = spec.split(":"); Address address = toAddr(pair[0]);
            byte[] bytes = new byte[Integer.parseInt(pair[1])];
            currentProgram.getMemory().getBytes(address, bytes);
            Map<String,Object> row = new LinkedHashMap<>(); row.put("address", address.toString());
            row.put("bytes", hex(bytes)); row.put("references_to_start", refs(address)); data.add(row);
        }
        report.put("static_data", data);
        Files.writeString(output, new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n",
            StandardOpenOption.CREATE_NEW);
        println("TZ_PAS_EVIDENCE_WRITTEN " + output + " seeds=" + functions.size());
    }
}
