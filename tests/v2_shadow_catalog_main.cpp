// velocita-v2-shadow-catalog: writes the execution catalog file.
#include "v2_catalog_test_support.hpp"
#include "v2_shadow_catalog_rows.hpp"

#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::string_view profile = argc == 3 ? std::string_view(argv[1]) : std::string_view();
    const bool btc_only = profile == "--truemarkets-cbbtc";
    const bool both = profile == "--truemarkets";
    if (argc != 2 && !btc_only && !both) {
        std::cerr << "usage: velocita-v2-shadow-catalog [--truemarkets-cbbtc | --truemarkets] OUTPUT_FILE\n";
        return 64;
    }
    const char* output = argv[argc - 1];
    const auto& catalog = both       ? shadow_catalog::truemarkets_two_row_catalog()
                          : btc_only ? shadow_catalog::truemarkets_catalog()
                                     : catalog_test::standard_catalog();
    std::vector<std::byte> bytes(velocita::v2::kMaxExecutionCatalogBytes);
    const auto encoded = catalog.encode(bytes);
    if (encoded.error != velocita::v2::ExecutionCatalogError::none) {
        std::cerr << "catalog did not encode\n";
        return 2;
    }
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(encoded.bytes_written));
    if (!out) {
        std::cerr << "cannot write " << output << "\n";
        return 2;
    }
    std::cout << "catalog.root = " << velocita::v2::to_hex(catalog.root()) << "\n";
    return 0;
}
