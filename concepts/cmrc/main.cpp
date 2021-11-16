// Check out the CMakeLists.txt in this directory
// before looking at the implementation below

// We are not using iostreams for reading cmrc files,
// but only for printing messages to the user via std::cout
#include <iostream>

// The first step to use cmrc in your program is to include
// the cmrc.hpp library
#include <cmrc/cmrc.hpp>

// Then declare which resources you want to access from this file.
// We've created only one asset library, so we are just using that one.
CMRC_DECLARE(assets);

int main() {
    // The entry function into the world of CMRC is the
    // cmrc::YOUR_CHOSEN_NAMESPACE::get_filesystem which
    // returns an object through which you can access all
    // your embedded assets.
    auto fs = cmrc::assets::get_filesystem();

    std::cout << "Listing known files and directories\n";

    // The CMRC library allows for simple directory iteration.
    // The iterate_directory function takes a directory you want to
    // list all the entries for, and returns a range of all entries
    // in that directory.
    for (auto entry : fs.iterate_directory("/")) {
        // You can check whether an entry is a directory or a file
        // and you can access its filename.
        if (entry.is_directory()) {
            std::cout << entry.filename() << " is a directory\n";
        } else if (entry.is_file()) {
            std::cout << entry.filename() << " is a file\n";
        }
    }

    std::cout << "\n\n\n";

    // If you know the path and the name of your file, you can just
    // use fs.open. We will open main.cpp -- this source file.
    auto file = fs.open("/main.cpp");

    // Internally, all the files are just arrays of bytes, and
    // each file has a begin and end iterator which allows us to
    // create a std::string_view over that file.
    std::string_view contents{ file.begin(), file.end() };

    // And we can print it out.
    std::cout << "-- This is main.cpp --\n"
              << contents << '\n';
}
