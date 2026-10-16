#include "Commands.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char* kUsage =
    "usage: metamorph convert <world> --to anvil|linear [-o <out>]\n"
    "       metamorph upgrade <world> [-o <out>]\n"
    "The input is never modified; the result goes to <world>.converted unless -o says otherwise.\n";

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2 || (args[0] != "convert" && args[0] != "upgrade")) {
        std::cerr << kUsage;
        return 2;
    }
    const fs::path world{args[1]};
    fs::path output = fs::path{args[1]}.string() + ".converted";
    std::string format;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "-o" && i + 1 < args.size()) {
            output = args[++i];
        } else if (args[i] == "--to" && i + 1 < args.size()) {
            format = args[++i];
        } else {
            std::cerr << kUsage;
            return 2;
        }
    }
    if (!fs::is_directory(world)) {
        std::cerr << world.string() << " is not a folder\n";
        return 2;
    }
    if (fs::exists(output) && !fs::is_empty(output)) {
        std::cerr << output.string() << " already exists and is not empty\n";
        return 2;
    }
    if (args[0] == "upgrade") {
        return basalt::metamorph::cli::upgrade(world, output);
    }
    if (format != "anvil" && format != "linear") {
        std::cerr << kUsage;
        return 2;
    }
    return basalt::metamorph::cli::convert(
        world, format == "anvil" ? basalt::metamorph::cli::Format::Anvil : basalt::metamorph::cli::Format::Linear,
        output);
}
