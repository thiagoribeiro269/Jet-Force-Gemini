// JFG-specific metadata/dispatch adapter using the unmodified N64Recomp library.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>
#include <fmt/format.h>
#include "rabbitizer.hpp"
#include "config.h"
#include "recompiler/context.h"
#include "recompiler/generator.h"

class JfgGenerator final : public N64Recomp::Generator {
    const N64Recomp::Context& context_;
    std::ostream& out_;
    N64Recomp::CGenerator delegate_;
    bool live_relocated_calls_;

    void call_target(size_t index) const {
        const auto& function = context_.functions.at(index);
        const auto& section = context_.sections.at(function.section_index);
        out_ << fmt::format("jfg_poc_call({}, 0x{:X}u, rdram, ctx);\n",
                            function.section_index, function.vram - section.ram_addr);
    }
public:
    JfgGenerator(const N64Recomp::Context& context, std::ostream& out, bool live_relocated_calls)
        : context_(context), out_(out), delegate_(out), live_relocated_calls_(live_relocated_calls) {}
    void process_binary_op(const N64Recomp::BinaryOp& op, const N64Recomp::InstructionContext& ctx) const override { delegate_.process_binary_op(op, ctx); }
    void process_unary_op(const N64Recomp::UnaryOp& op, const N64Recomp::InstructionContext& ctx) const override { delegate_.process_unary_op(op, ctx); }
    void process_store_op(const N64Recomp::StoreOp& op, const N64Recomp::InstructionContext& ctx) const override { delegate_.process_store_op(op, ctx); }
    void emit_function_start(const std::string& name, size_t index) const override {
        delegate_.emit_function_start(name, index);
        out_ << fmt::format("    jfg_poc_require_section({});\n", context_.functions.at(index).section_index);
    }
    void emit_function_end() const override { delegate_.emit_function_end(); }
    void emit_function_call(const N64Recomp::Context&, size_t index) const override { call_target(index); }
    void emit_function_call_lookup(uint32_t address) const override {
        auto it = context_.functions_by_vram.find(address);
        if (it == context_.functions_by_vram.end() || it->second.size() != 1) {
            throw std::runtime_error("Call target is outside the verified function set");
        }
        call_target(it->second.front());
    }
    void emit_function_call_by_register(int) const override { out_ << "jfg_poc_call_indirect(rdram, ctx);\n"; }
    void emit_function_call_reference_symbol(const N64Recomp::Context& context, uint16_t section, size_t symbol, uint32_t) const override {
        if (live_relocated_calls_) {
            // runLink may point an unresolved overlay call at TrapDanglingJump.
            // Read its real patched JAL rather than bypassing the game's loader.
            out_ << "jfg_poc_call_jal(rdram, ctx);\n";
            return;
        }
        const auto& reference = context.get_reference_symbol(section, symbol);
        auto function = context_.functions_by_name.find(reference.name);
        if (function == context_.functions_by_name.end()) throw std::runtime_error("Unlisted reference dependency");
        call_target(function->second);
    }
    void emit_named_function_call(const std::string&) const override { throw std::runtime_error("Unlisted static function dependency"); }
    void emit_goto(const std::string& target) const override { delegate_.emit_goto(target); }
    void emit_label(const std::string& label) const override { delegate_.emit_label(label); }
    void emit_jtbl_addend_declaration(const N64Recomp::JumpTable& table, int reg) const override { delegate_.emit_jtbl_addend_declaration(table, reg); }
    void emit_branch_condition(const N64Recomp::ConditionalBranchOp& op, const N64Recomp::InstructionContext& ctx) const override { delegate_.emit_branch_condition(op, ctx); }
    void emit_branch_close() const override { delegate_.emit_branch_close(); }
    void emit_switch(const N64Recomp::Context& context, const N64Recomp::JumpTable& table, int reg) const override { delegate_.emit_switch(context, table, reg); }
    void emit_case(int index, const std::string& label) const override { delegate_.emit_case(index, label); }
    void emit_switch_error(uint32_t address, uint32_t table) const override { delegate_.emit_switch_error(address, table); }
    void emit_switch_close() const override { delegate_.emit_switch_close(); }
    void emit_return(const N64Recomp::Context& context, size_t index) const override { delegate_.emit_return(context, index); }
    void emit_check_fr(int reg) const override { delegate_.emit_check_fr(reg); }
    void emit_check_nan(int reg, bool is_double) const override { delegate_.emit_check_nan(reg, is_double); }
    void emit_cop0_status_read(int reg) const override { delegate_.emit_cop0_status_read(reg); }
    void emit_cop0_status_write(int reg) const override { delegate_.emit_cop0_status_write(reg); }
    void emit_cop1_cs_read(int reg) const override { delegate_.emit_cop1_cs_read(reg); }
    void emit_cop1_cs_write(int reg) const override { delegate_.emit_cop1_cs_write(reg); }
    void emit_muldiv(N64Recomp::InstrId id, int first, int second) const override { delegate_.emit_muldiv(id, first, second); }
    void emit_syscall(uint32_t address) const override { delegate_.emit_syscall(address); }
    void emit_do_break(uint32_t address) const override { delegate_.emit_do_break(address); }
    void emit_pause_self() const override { delegate_.emit_pause_self(); }
    void emit_trigger_event(uint32_t index) const override { delegate_.emit_trigger_event(index); }
    void emit_comment(const std::string& text) const override { delegate_.emit_comment(text); }
};

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: jfg_recomp_tool recomp.toml");
        N64Recomp::Config config(argv[1]);
        if (!config.good() || config.symbols_file_path.empty() || config.rom_file_path.empty()) {
            throw std::runtime_error("Expected valid symbol and ROM inputs");
        }
        // Match the official CLI's decoder setup: code generation consumes
        // actual MIPS instructions rather than these disassembler aliases.
        RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
        RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
        RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
        RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
        RabbitizerConfig_Cfg.pseudos.pseudoBal = false;
        std::ifstream input(config.rom_file_path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read the local ROM");
        std::vector<uint8_t> rom(std::istreambuf_iterator<char>(input), {});
        N64Recomp::Context context{};
        if (!N64Recomp::Context::from_symbol_file(config.symbols_file_path, std::move(rom), context, true)) {
            throw std::runtime_error("Cannot parse recompilation metadata");
        }
        context.trace_mode = false;
        const auto metadata = toml::parse_file(config.symbols_file_path.string());
        const auto* sections = metadata["section"].as_array();
        if (!sections || sections->size() != context.sections.size()) throw std::runtime_error("Section count mismatch");
        size_t cross_relocations = 0, call_sites = 0, indirect_sites = 0;
        std::unordered_set<size_t> excluded;
        for (size_t i = 0; i < sections->size(); i++) {
            auto& section = context.sections[i];
            const auto* function_rows = (*sections)[i].as_table()->get_as<toml::array>("functions");
            for (const auto& row : *function_rows) {
                const auto* table = row.as_table();
                if ((*table)["host_import"].value_or(false) || (*table)["deferred"].value_or(false)) {
                    auto name = (*table)["name"].value<std::string>();
                    if (!name || !context.functions_by_name.contains(*name)) throw std::runtime_error("Unknown platform import");
                    excluded.insert(context.functions_by_name.at(*name));
                }
            }
            // Main stays fixed by the loader; the flag also makes references to
            // its data/BSS explicit relocations in the N64Recomp context.
            section.relocatable = true;
            const auto* relocs = (*sections)[i].as_table()->get_as<toml::array>("relocs");
            if (!relocs || relocs->size() != section.relocs.size()) throw std::runtime_error("Relocation count mismatch");
            for (size_t j = 0; j < relocs->size(); j++) {
                const auto* row = (*relocs)[j].as_table();
                auto target = (*row)["target_section"].value<uint32_t>();
                auto address = (*row)["target_vram"].value<uint32_t>();
                if (!target || !address || *target >= context.sections.size()) throw std::runtime_error("Invalid target section");
                auto& relocation = section.relocs[j];
                relocation.target_section = static_cast<uint16_t>(*target);
                relocation.target_section_offset = *address - context.sections[*target].ram_addr;
                cross_relocations += *target != i;
            }
        }
        if (const auto* patches = metadata["runtime_patch"].as_array()) {
            for (const auto& row : *patches) {
                const auto* patch = row.as_table();
                const auto address = (*patch)["vram"].value<uint32_t>();
                const auto before = (*patch)["before"].value<uint32_t>();
                const auto after = (*patch)["after"].value<uint32_t>();
                if (!address || !before || !after) throw std::runtime_error("Invalid runtime preparation patch");
                size_t patched = 0;
                for (size_t i = 0; i < context.functions.size(); i++) {
                    auto& function = context.functions[i];
                    if (*address < function.vram || *address >= function.vram + function.words.size() * 4) continue;
                    if (excluded.contains(i)) throw std::runtime_error("Runtime patch targets an external import");
                    auto& word = function.words.at((*address - function.vram) / 4);
                    if (byteswap(word) != *before) throw std::runtime_error("Runtime patch input word differs");
                    word = byteswap(*after);
                    function.function_hooks[-1] += fmt::format(
                        "jfg_poc_require_runtime_patch(rdram, 0x{:X}u, 0x{:X}u);", *address, *after);
                    patched++;
                }
                if (patched != 1) throw std::runtime_error("Runtime patch did not match exactly one compiled function");
            }
        }
        // JFG stores external JAL instructions with a zero target. Use the
        // library's reference-symbol facility to bind the selected callee;
        // do not guess a target from those unrelocated instruction bits.
        if (!context.import_reference_context(context)) throw std::runtime_error("Cannot register reference symbols");
        context.skip_validating_reference_symbols = false;
        for (auto& section : context.sections) {
            for (auto& relocation : section.relocs) {
                if (relocation.type != N64Recomp::RelocType::R_MIPS_26) continue;
                const auto address = context.sections[relocation.target_section].ram_addr + relocation.target_section_offset;
                auto matches = context.functions_by_vram.find(address);
                if (matches == context.functions_by_vram.end() || matches->second.size() != 1) throw std::runtime_error("Ambiguous JAL dependency");
                const auto& function = context.functions[matches->second.front()];
                N64Recomp::SymbolReference reference;
                if (function.section_index != relocation.target_section || !context.find_reference_symbol(function.name, reference)) {
                    throw std::runtime_error("Cannot bind JAL dependency to its section");
                }
                relocation.reference_symbol = true;
                relocation.symbol_index = static_cast<uint32_t>(reference.symbol_index);
                relocation.target_section = reference.section_index;
            }
        }
        for (size_t function_index = 0; function_index < context.functions.size(); function_index++) {
            if (excluded.contains(function_index)) continue;
            auto& function = context.functions[function_index];
            const auto& section = context.sections[function.section_index];
            for (size_t i = 0; i < function.words.size(); i++) {
                const auto word = byteswap(function.words[i]);
                if ((word >> 26) == 3) {
                    function.function_hooks[static_cast<int32_t>(i)] += fmt::format(
                        "ctx->r31 = (gpr)(int32_t)(section_addresses[{}] + 0x{:X}u);",
                        function.section_index, function.vram - section.ram_addr + i * 4 + 8);
                    call_sites++;
                }
                if ((word >> 26) == 0 && ((word & 63) == 9 || ((word & 63) == 8 && ((word >> 21) & 31) != 31))) {
                    const auto source = (word >> 21) & 31;
                    function.function_hooks[static_cast<int32_t>(i)] += fmt::format("jfg_poc_prepare_indirect(ctx->r{});", source);
                    if ((word & 63) == 9) {
                        if (((word >> 11) & 31) != 31) throw std::runtime_error("Unsupported JALR return register");
                        function.function_hooks[static_cast<int32_t>(i)] += fmt::format(
                            "ctx->r31 = (gpr)(int32_t)(section_addresses[{}] + 0x{:X}u);",
                            function.section_index, function.vram - section.ram_addr + i * 4 + 8);
                    }
                    indirect_sites++;
                }
                if ((word >> 26) == 1 && ((word >> 16) & 31) >= 16 && ((word >> 16) & 31) <= 19) {
                    throw std::runtime_error("Conditional link branches need their own return-register proof");
                }
            }
            for (const auto& relocation : section.relocs) {
                if (relocation.address < function.vram || relocation.address >= function.vram + function.words.size() * 4) continue;
                if (relocation.target_section != function.section_index && relocation.type != N64Recomp::RelocType::R_MIPS_26) {
                    function.function_hooks[static_cast<int32_t>((relocation.address - function.vram) / 4)] +=
                        fmt::format("jfg_poc_require_section({});", relocation.target_section);
                }
            }
        }
        std::filesystem::create_directories(config.output_func_path);
        std::ofstream output(config.output_func_path / "funcs_0.c");
        if (!output) throw std::runtime_error("Cannot create generated C output");
        output << "#include \"runtime.h\"\n";
        std::vector<std::vector<uint32_t>> static_functions(context.sections.size());
        JfgGenerator generator(context, output, metadata["live_relocated_calls"].value_or(false));
        for (size_t i = 0; i < context.functions.size(); i++) {
            if (excluded.contains(i)) continue;
            if (!N64Recomp::recompile_function_custom(generator, context, i, output, static_functions, false)) {
                throw std::runtime_error("Recompilation failed: " + context.functions[i].name);
            }
        }
        for (const auto& functions : static_functions) {
            if (!functions.empty()) throw std::runtime_error("Unexpected unlisted static functions");
        }
        std::cout << "Recompiled " << context.functions.size() - excluded.size() << " functions, " << excluded.size()
                  << " explicit external/deferred entries, " << cross_relocations << " cross-section relocations, "
                  << call_sites << " direct and " << indirect_sites << " indirect call/jump sites.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
