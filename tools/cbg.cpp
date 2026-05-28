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
#include <string.h>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <sys/user.h>

namespace
{
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
continue (c)            Resume the process
regs / info registers   Show registers
register read [name|all]
register write <name> <value>
help register           Help for register subcommands
)";
        }
        else if (is_prefix(args[1], "register"))
        {
            std::cerr << R"(register read [name|all]     e.g. register read x0, w19, v3.4s[2], pc
register write <name> <val>  value can be decimal, 0x..., or float for s/d views
regs                         shorthand for 'register read all'
regs <name>                  shorthand for 'register read <name>'
info registers
c / continue                 resume + show stop reason
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
    bool is_vector_lane(std::string_view name);
    bool looks_like_float_literal(const std::string &s);

    void handle_command(std::unique_ptr<cbg::Process> &process, std::string_view line)
    {
        auto args = split(line, ' ');
        if (args.empty())
            return;
        auto command = args[0];

        if (is_prefix(command, "continue") || command == "c")
        {
            process->resume();
            auto reason = process->wait_on_signal();
            std::cout << stop_reason_str(reason) << "\n";
            // On natural exit the Process already cleared pid_ and printed a message.
        }
        else if (is_prefix(command, "register") || is_prefix(command, "reg"))
        {
            handle_register_command(process, args);
        }
        else if (is_prefix(command, "regs") ||
                 (is_prefix(command, "info") && args.size() > 1 && is_prefix(args[1], "reg")))
        {
            // Convenience: "regs" or "info registers" / "info reg"
            std::vector<std::string> normalized{"register", "read", "all"};
            if (is_prefix(command, "regs") && args.size() > 1)
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

    // --- New register + stop-reason support for wired CLI ---

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

            if (is_vector_lane(name))
            {
                // Raw bit view for lanes is what users expect when debugging vectors
                std::cout << format_hex(bits, 8) << "\n";
            }
            else
            {
                std::cout << format_hex(bits, 8) << "   (float: " << f << ")\n";
            }
        }
        else if (fmt == cbg::RegisterFormat::F64)
        {
            uint64_t bits = sv.read_u64();
            double d = sv.read_f64();

            if (is_vector_lane(name))
            {
                // Primary value = the raw bits the user wrote / cares about
                std::cout << format_hex(bits) << "\n";
            }
            else
            {
                std::cout << format_hex(bits) << "   (double: " << d << ")\n";
            }
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
        // One register per line using the standard formatter (clean + correct for all sizes)
        const char *gprs[] = {
            "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
            "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
            "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
            "x24", "x25", "x26", "x27", "x28", "x29", "x30",
            "sp", "pc", "pstate"};
        for (auto *name : gprs)
        {
            try
            {
                auto &rv = regs.get_register(name);
                print_register_value(name, rv);
            }
            catch (...)
            {
            }
        }
    }

    void print_fp_summary(cbg::Registers &regs)
    {
        // fpsr / fpcr + a few example vector regs (user can query any vN individually)
        try
        {
            auto &fpsr = regs.get_register("fpsr");
            auto &fpcr = regs.get_register("fpcr");
            std::cout << "fpsr = " << format_hex(fpsr.get<uint32_t>(), 8)
                      << "    fpcr = " << format_hex(fpcr.get<uint32_t>(), 8) << "\n";
        }
        catch (...)
        { /* ignore if missing */
        }

        // Show first 4 vector regs as compact 2x u64 for quick visibility
        for (int i = 0; i < 4; ++i)
        {
            try
            {
                auto &v = regs.get_register("v" + std::to_string(i));
                __uint128_t val = v.get<__uint128_t>();
                uint64_t lo = (uint64_t)val;
                uint64_t hi = (uint64_t)(val >> 64);
                std::cout << "v" << i << " = " << format_hex(lo) << " " << format_hex(hi) << "\n";
            }
            catch (...)
            {
            }
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

    uint64_t parse_integer(const std::string &s)
    {
        if (s.empty())
            cbg::Error::send("Empty value for register write");

        char *end = nullptr;
        uint64_t val = std::strtoull(s.c_str(), &end, 0); // base 0 = auto 0x/dec/oct
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

    bool is_vector_lane(std::string_view name)
    {
        // Explicit lane views (vN.4s[k], vN.2d[k]) are almost always used for raw bit manipulation
        return name.find(".2d[") != std::string_view::npos ||
               name.find(".4s[") != std::string_view::npos;
    }

    bool looks_like_float_literal(const std::string &s)
    {
        if (s.empty())
            return false;

        // Strong signal that the user wants raw bits
        if (s.find("0x") == 0 || s.find("0X") == 0)
            return false;

        // Looks like a floating point literal
        if (s.find('.') != std::string::npos)
            return true;
        if (s.find('e') != std::string::npos || s.find('E') != std::string::npos)
            return true;

        return false;
    }

    void handle_register_command(std::unique_ptr<cbg::Process> &process, const std::vector<std::string> &args)
    {
        if (args.size() < 2)
        {
            std::cerr << "register subcommand required (read / write). Try 'help register'\n";
            return;
        }
        auto &regs = process->get_registers();
        std::string sub = args[1];

        if (is_prefix(sub, "read"))
        {
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

            // Prefer subregister view first (wN, sN, dN, vN.4s[k], vN.2d[k])
            if (auto sub = regs.make_subview_by_name(name))
            {
                print_subregister_value(name, *sub);
                return;
            }
            // Fall back to full register view (xN, vN, pc, fpsr, brk_*, etc.)
            try
            {
                const auto &rv = regs.get_register(name);
                print_register_value(name, rv);
            }
            catch (const cbg::Error &e)
            {
                std::cerr << e.what() << "\n";
            }
        }
        else if (is_prefix(sub, "write"))
        {
            if (args.size() < 4)
            {
                std::cerr << "Usage: register write <name> <value>\n";
                return;
            }
            std::string name = args[2];
            std::string valstr = args[3];

            bool wrote = false;

            // Try subregister path first (supports wN / scalar FP / lanes with correct policies)
            if (auto sub = regs.make_subview_by_name(name))
            {
                auto fmt = sub->format;

                // Decide based on the value the user typed (standard debugger behavior),
                // not the register name. This allows both of the following to work:
                //   register write v20.2d[1] 0xdeadbeefcafebabe
                //   register write v20.2d[1] 3.141592653589793
                bool treat_as_float = (fmt == cbg::RegisterFormat::F32 || fmt == cbg::RegisterFormat::F64)
                                      && looks_like_float_literal(valstr);

                if (treat_as_float)
                {
                    double d = parse_floating(valstr);
                    if (fmt == cbg::RegisterFormat::F32)
                        sub->write_f32(static_cast<float>(d));
                    else
                        sub->write_f64(d);
                }
                else
                {
                    uint64_t v = parse_integer(valstr);
                    sub->write_u64(v);   // Safe for 32-bit and 64-bit subviews
                }
                process->write_back_registers();
                wrote = true;
            }
            else
            {
                // Regular named register
                try
                {
                    uint64_t v = parse_integer(valstr);
                    regs.set_register(name, v);
                    process->write_back_registers();
                    wrote = true;
                }
                catch (const cbg::Error &e)
                {
                    std::cerr << e.what() << "\n";
                    return;
                }
            }

            if (wrote)
            {
                std::cout << "Wrote " << name << " <- " << valstr << "\n";
                // Re-read to show the (possibly policy-transformed) result
                if (auto sub = regs.make_subview_by_name(name))
                {
                    print_subregister_value(name, *sub);
                }
                else
                {
                    try
                    {
                        const auto &rv = regs.get_register(name);
                        print_register_value(name, rv);
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        else
        {
            std::cerr << "Unknown register subcommand '" << sub << "'. Use read or write.\n";
        }
    }
}

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