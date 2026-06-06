#include <unistd.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdlib>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <editline/readline.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <cstring>
#include <variant>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <sys/user.h>

// =============================================================================
// cbg.cpp - interactive command-line debugger (REPL over libcbg::Process)
// =============================================================================

namespace
{
    // -------------------------------------------------------------------------
    // String / CLI utilities
    // -------------------------------------------------------------------------

    bool is_prefix(std::string_view str, std::string_view of)
    {
        if (str.size() > of.size())
            return false;
        return std::equal(str.begin(), str.end(), of.begin());
    }

    void print_help(const std::vector<std::string> &args)
    {
        if (args.size() == 1)
        {
            std::cerr << R"(Available commands:
continue (c)            Resume the process (to next bp or signal)
step (s, si)            Single-step one instruction
break (b) <addr>        Set software breakpoint at hex address (e.g. b 0x1040)
delete (d) <id>         Delete breakpoint by id
info breakpoints        List breakpoints
info registers   Show registers
register read [name|all]
register write <name> <value> (0x... ok for 128-bit vN)
help register           Help for register subcommands
)";
        }
        else if (is_prefix(args[1], "register"))
        {
            std::cerr << R"(register read [name|all]     e.g. register read x0, w19, v3.4s[2], pc
register write <name> <val>  value can be decimal, 0x... (up to 32 hex digits/128-bit for full vN), or float for s/d views
regs                         shorthand for 'register read all'
regs <name>                  shorthand for 'register read <name>'
info registers
info breakpoints             list active breakpoints with ids
b / break <addr>             set SW bp (addr hex); returns id
d / delete <id>              remove bp by id
s / step / si                single step one instr (respects bps)
c / continue                 resume + show stop reason (stops at bps)
)";
        }
        else
        {
            std::cerr << "No help available for this command\n";
        }
    }

    std::vector<std::string> split(std::string_view str, char delimiter)
    {
        std::vector<std::string> res{};
        std::stringstream ss{std::string(str)};
        std::string item;

        while (std::getline(ss, item, delimiter))
        {
            res.push_back(item);
        }
        return res;
    }

    // -------------------------------------------------------------------------
    // Process launch / attach
    // -------------------------------------------------------------------------

    std::unique_ptr<cbg::Process> attach(int argc, const char **argv)
    {
        pid_t pid = 0;
        if (argc == 3 and argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            return cbg::Process::attach(pid);
        }
        else
        {
            const char *program_path = argv[1];
            return cbg::Process::launch(program_path);
        }
    }

    // Forward declarations for functions defined later in this anonymous namespace
    std::string stop_reason_str(cbg::stop_reason r);
    void handle_register_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args);
    void handle_register_read(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args);
    void handle_register_write(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args);
    void handle_break_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args);
    void handle_delete_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args);
    void handle_info_breakpoints(std::unique_ptr<cbg::Process> &process);
    void print_breakpoint_hit_if_any(std::unique_ptr<cbg::Process> &process);

    // -------------------------------------------------------------------------
    // Command dispatcher
    // -------------------------------------------------------------------------

    void handle_command(std::unique_ptr<cbg::Process> &process, std::string_view line)
    {
        auto args = split(line, ' ');
        if (args.empty())
            return;
        auto command = args[0];

        if (is_prefix(command, "continue"))
        {
            process->resume();
            auto reason = process->wait_on_signal();
            std::cout << stop_reason_str(reason) << "\n";
            print_breakpoint_hit_if_any(process);
            // On natural exit the Process already cleared pid_ and printed a message.
        }
        else if (is_prefix(command, "step"))
        {
            auto reason = process->step_instruction();
            std::cout << stop_reason_str(reason) << "\n";
            print_breakpoint_hit_if_any(process);
        }
        else if (is_prefix(command, "breakpoints"))
        {
            handle_break_command(process, args);
        }
        else if (is_prefix(command, "delete"))
        {
            handle_delete_command(process, args);
        }
        else if (is_prefix(command, "info") && args.size() > 1 && is_prefix(args[1], "breakpoints"))
        {
            handle_info_breakpoints(process);
        }
        else if (is_prefix(command, "register"))
        {
            handle_register_command(process, args);
        }
        else if (is_prefix(command, "registers") ||
                 (is_prefix(command, "info") && args.size() > 1 && is_prefix(args[1], "registers")))
        {
            // Convenience: "regs" or "info registers" / "info reg"
            std::vector<std::string> normalized{"register", "read", "all"};
            if (is_prefix(command, "registers") && args.size() > 1)
            {
                // allow "regs x0" etc.
                normalized = {"register", "read", args[1]};
            }
            handle_register_command(process, normalized);
        }
        else if (is_prefix(command, "help"))
        {
            print_help(args);
        }
        else
        {
            std::cerr << "Unknown command\n";
        }
    }

    // -------------------------------------------------------------------------
    // Stop reason + hex formatting helpers
    // -------------------------------------------------------------------------

    std::string stop_reason_str(cbg::stop_reason r)
    {
        std::stringstream ss;
        switch (r.reason)
        {
        case cbg::process_state::EXITED:
            ss << "Exited with status " << static_cast<int>(r.info);
            break;
        case cbg::process_state::TERMINATED:
            ss << "Terminated by signal " << (sigabbrev_np(r.info) ? sigabbrev_np(r.info) : "???");
            break;
        case cbg::process_state::STOPPED:
            ss << "Stopped by signal " << (sigabbrev_np(r.info) ? sigabbrev_np(r.info) : "???");
            break;
        default:
            ss << "Unknown stop state";
            break;
        }
        return ss.str();
    }

    std::string format_hex(uint64_t v, int width = 16)
    {
        std::ostringstream oss;
        oss << "0x" << std::hex << std::setw(width) << std::setfill('0') << v;
        return oss.str();
    }

    // -------------------------------------------------------------------------
    // Register / subregister pretty printers + block dumps
    // -------------------------------------------------------------------------

    void print_register_value(const std::string &name, const cbg::RegisterView &rv)
    {
        std::cout << std::left << std::setw(12) << (name + " =");
        if (rv.size == 16)
        {
            // Vec128 (v regs): print as two 64-bit words (low, high)
            __uint128_t val = rv.get<__uint128_t>();
            uint64_t lo = (uint64_t)val;
            uint64_t hi = (uint64_t)(val >> 64);
            std::cout << format_hex(lo) << " " << format_hex(hi) << "\n";
        }
        else if (rv.size == 4 && (rv.format == cbg::RegisterFormat::U32 || rv.format == cbg::RegisterFormat::F32))
        {
            uint32_t v = rv.get<uint32_t>();
            std::cout << format_hex(v, 8) << "\n";
        }
        else
        {
            uint64_t v = rv.get<uint64_t>();
            std::cout << format_hex(v) << "\n";
        }
    }

    void print_subregister_value(const std::string &name, cbg::SubregisterView &sv)
    {
        std::cout << std::left << std::setw(16) << (name + " =");
        auto fmt = sv.format;

        if (fmt == cbg::RegisterFormat::F32)
        {
            float f = sv.read_f32();
            uint32_t bits = sv.read_u32();
            std::cout << format_hex(bits, 8) << "   (float: " << f << ")\n";
        }
        else if (fmt == cbg::RegisterFormat::F64)
        {
            double d = sv.read_f64();
            uint64_t bits = sv.read_u64();
            std::cout << format_hex(bits) << "   (double: " << d << ")\n";
        }
        else
        {
            uint64_t v = sv.read_u64();
            int w = (sv.bit_size <= 32) ? 8 : 16;
            std::cout << format_hex(v, w) << "\n";
        }
    }

    void print_gpr_block(cbg::Registers &regs)
    {
        // Resolve descriptors once (first call), then use O(1) fast path for every "regs" dump.
        static std::vector<std::pair<const char *, cbg::RegisterDescriptor>> gpr_descs;
        if (gpr_descs.empty())
        {
            const char *names[] = {
                "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
                "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
                "x24", "x25", "x26", "x27", "x28", "x29", "x30",
                "sp", "pc", "pstate"
            };
            for (auto *n : names)
                gpr_descs.emplace_back(n, regs.lookup(n));
        }

        for (auto &[name, d] : gpr_descs)
        {
            try
            {
                auto &rv = regs.get_register(d);
                print_register_value(name, rv);
            }
            catch (...) {}
        }
    }

    void print_fp_summary(cbg::Registers &regs)
    {
        // fpsr / fpcr + v0..v3 — descriptors resolved once.
        static std::vector<std::pair<const char *, cbg::RegisterDescriptor>> fp_descs;
        if (fp_descs.empty())
        {
            const char *names[] = {"fpsr", "fpcr", "v0", "v1", "v2", "v3"};
            for (auto *n : names)
                fp_descs.emplace_back(n, regs.lookup(n));
        }

        // fpsr/fpcr (always full registers)
        try
        {
            auto &fpsr = regs.get_register(fp_descs[0].second);
            auto &fpcr = regs.get_register(fp_descs[1].second);
            std::cout << "fpsr = " << format_hex(fpsr.get<uint32_t>(), 8)
                      << "    fpcr = " << format_hex(fpcr.get<uint32_t>(), 8) << "\n";
        }
        catch (...) { /* ignore if missing */ }

        // First 4 vector regs (compact view)
        for (int i = 0; i < 4; ++i)
        {
            try
            {
                auto &v = regs.get_register(fp_descs[2 + i].second);
                __uint128_t val = v.get<__uint128_t>();
                uint64_t lo = (uint64_t)val;
                uint64_t hi = (uint64_t)(val >> 64);
                std::cout << "v" << i << " = " << format_hex(lo) << " " << format_hex(hi) << "\n";
            }
            catch (...) {}
        }
        std::cout << "(use 'register read vN' or 'register read vN.4s[k]' for full SIMD views)\n";
    }

    void print_all_registers(cbg::Registers &regs)
    {
        std::cout << "=== General Purpose Registers ===\n";
        print_gpr_block(regs);
        std::cout << "\n=== Floating Point / SIMD (summary) ===\n";
        print_fp_summary(regs);
    }

    // -------------------------------------------------------------------------
    // Value parsers (int / float) for register write
    // -------------------------------------------------------------------------

    uint64_t parse_integer(const std::string &s)
    {
        if (s.empty())
            cbg::Error::send("Empty value for register write");

        char *end = nullptr;
        uint64_t val = std::strtoull(s.c_str(), &end, 0);  // base 0 = auto 0x/dec/oct
        if (end == s.c_str() || *end != '\0')
            cbg::Error::send("Invalid integer value: " + s);
        return val;
    }

    double parse_floating(const std::string &s)
    {
        if (s.empty())
            cbg::Error::send("Empty value for FP register write");
        char *end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0')
            cbg::Error::send("Invalid floating-point value: " + s);
        return d;
    }

    uint64_t parse_addr(const std::string &s)
    {
        if (s.empty())
            cbg::Error::send("Empty address for breakpoint");
        char *end = nullptr;
        uint64_t v = std::strtoull(s.c_str(), &end, 0);
        if (end == s.c_str() || *end != '\0')
            cbg::Error::send("Invalid breakpoint address: " + s);
        return v;
    }

    // -------------------------------------------------------------------------
    // Register command handlers (read / write + dispatch)
    // -------------------------------------------------------------------------

    void handle_register_read(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args)
    {
        auto &regs = process->get_registers();

        if (args.size() == 2 || (args.size() == 3 && is_prefix(args[2], "all")))
        {
            print_all_registers(regs);
            return;
        }
        if (args.size() < 3)
        {
            std::cerr << "Usage: register read <name> | register read all\n";
            return;
        }
        std::string name = args[2];

        try
        {
            auto h = regs.resolve(name);

            if (auto val = regs.read(h))
            {
                // Use SubregisterDescriptor (from unified resolve) + get_subregister (consistent
                // with get_register for full) for sub view. (Avoids legacy make_subview_by_name.)
                // Full registers (incl. 128-bit vN) use descriptor view for print.
                if (auto* sd = std::get_if<cbg::SubregisterDescriptor>(&h))
                {
                    auto sv = regs.get_subregister(*sd);
                    print_subregister_value(name, sv);
                }
                else
                {
                    auto d = regs.lookup(name);
                    const auto &rv = regs.get_register(d);
                    print_register_value(name, rv);
                }
            }
            else
            {
                std::cerr << "Failed to read register '" << name << "'\n";
            }
        }
        catch (const cbg::Error &e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    void handle_register_write(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args)
    {
        if (args.size() < 4)
        {
            std::cerr << "Usage: register write <name> <value>\n";
            return;
        }
        auto &regs = process->get_registers();
        std::string name = args[2];
        std::string valstr = args[3];

        // === New experimental unified path ===
        auto h = regs.resolve(name);
        auto fmt = regs.get_format(h);
        auto bit_size = regs.get_bit_size(h);

        auto opt_raw = cbg::Registers::parse_register_value(valstr, fmt, bit_size);
        if (!opt_raw)
        {
            std::cerr << "Invalid value '" << valstr << "' for register/subregister '" << name << "'\n";
            return;
        }
        __uint128_t raw = *opt_raw;

        try
        {
            auto h = regs.resolve(name);
            regs.write(h, raw);
            process->write_back_registers();

            std::cout << "Wrote " << name << " <- " << valstr << "\n";

            // Re-display using the nicest view (sub for FP interpretation, otherwise full).
            // Use get_subregister (consistent with get_register for fulls).
            if (auto* sd = std::get_if<cbg::SubregisterDescriptor>(&h))
            {
                auto sv = regs.get_subregister(*sd);
                print_subregister_value(name, sv);
            }
            else
            {
                try
                {
                    auto d = regs.lookup(name);
                    const auto &rv = regs.get_register(d);
                    print_register_value(name, rv);
                }
                catch (...) {}
            }
        }
        catch (const cbg::Error &e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    void handle_register_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args)
    {
        if (args.size() < 2)
        {
            std::cerr << "register subcommand required (read / write). Try 'help register'\n";
            return;
        }
        std::string sub = args[1];

        if (is_prefix(sub, "read"))
        {
            handle_register_read(process, args);
        }
        else if (is_prefix(sub, "write"))
        {
            handle_register_write(process, args);
        }
        else
        {
            std::cerr << "Unknown register subcommand '" << sub << "'. Use read or write.\n";
        }
    }

    // -------------------------------------------------------------------------
    // Breakpoint command support (handle_break / delete / info + hit reporting)
    // (parse_addr lives with the other value parsers above)
    // -------------------------------------------------------------------------

    void handle_break_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args)
    {
        if (args.size() < 2)
        {
            std::cerr << "Usage: break <addr>   (or b 0x...)\n";
            return;
        }
        std::string addrstr = args[1];
        if (args.size() >= 3)
            addrstr = args[2]; // allow "break 0x.." or "b 0x.."
        try
        {
            uint64_t addr = parse_addr(addrstr);
            int id = process->add_breakpoint(addr);
            std::cout << "Breakpoint " << id << " at 0x" << std::hex << addr << std::dec << "\n";
        }
        catch (const cbg::Error &e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    void handle_delete_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args)
    {
        if (args.size() < 2)
        {
            std::cerr << "Usage: delete <id>   (or d <id>)\n";
            return;
        }
        try
        {
            int id = std::stoi(args.size() >= 3 ? args[2] : args[1]);
            if (process->remove_breakpoint(id))
                std::cout << "Deleted breakpoint " << id << "\n";
            else
                std::cerr << "No breakpoint with id " << id << "\n";
        }
        catch (...)
        {
            std::cerr << "Invalid breakpoint id\n";
        }
    }

    void handle_info_breakpoints(std::unique_ptr<cbg::Process> &process)
    {
        auto bps = process->get_breakpoints();
        if (bps.empty())
        {
            std::cout << "No breakpoints.\n";
            return;
        }
        std::cout << "Num     Addr\n";
        for (auto [id, addr] : bps)
        {
            std::cout << std::setw(3) << id << "   0x" << std::hex << addr << std::dec << "\n";
        }
    }

    void print_breakpoint_hit_if_any(std::unique_ptr<cbg::Process> &process)
    {
        try
        {
            auto &regs = process->get_registers();
            auto d = regs.lookup("pc");
            uint64_t pc = regs.get_register(d).get<uint64_t>();
            auto bps = process->get_breakpoints();
            for (auto [id, addr] : bps)
            {
                if (addr == pc)
                {
                    std::cout << "(hit breakpoint " << id << ")\n";
                    break;
                }
            }
        }
        catch (...) {}
    }
}

// =============================================================================
// Top-level REPL, logging, and entry point (outside anon ns)
// =============================================================================

void main_loop(std::unique_ptr<cbg::Process> &process)
{
    char *line = nullptr;
    while ((line = readline("cbg> ")) != nullptr)
    {
        std::string line_str;
        if (line == std::string_view(""))
        {
            free(line);
            if (history_length > 0)
                line_str = history_get(history_length - 1)->line;
        }
        else
        {
            line_str = line;
            add_history(line);
            free(line);
        }
        if (!line_str.empty())
        {
            try
            {
                handle_command(process, line_str);
            }
            catch (const cbg::Error &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Logging setup + main
// -----------------------------------------------------------------------------

void log_setup()
{
    try
    {
        auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/debug_logs.log", true);
        spdlog::set_default_logger(std::move(file_logger));
        spdlog::set_level(spdlog::level::debug);
        spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");
        spdlog::flush_on(spdlog::level::debug);
        // spdlog::flush_every(std::chrono::seconds(5));
        spdlog::debug("Begin logging");
    }
    catch (const spdlog::spdlog_ex &e)
    {
        std::cerr << "Log init failed because: " << e.what() << '\n';
    }
}

int main(int argc, const char **argv)
{
    log_setup();
    if (argc == 1)
    {
        std::cerr << "Usage: " << argv[0] << " -p <pid> | --self\n";
        return -1;
    }

    try
    {
        auto process = attach(argc, argv);
        main_loop(process);
    }
    catch (const cbg::Error &e)
    {
        std::cout << e.what() << "\n";
    }
}