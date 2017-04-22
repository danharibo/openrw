#include <boost/test/unit_test.hpp>
#include <platform/FileIndex.hpp>
#include "test_globals.hpp"

BOOST_AUTO_TEST_SUITE(FileIndexTests)

#if RW_TEST_WITH_DATA
BOOST_AUTO_TEST_CASE(test_directory_paths) {
    FileIndex index;

    index.indexGameDirectory(Global::getGamePath());

    {
        std::string upperpath{"DATA/CULLZONE.DAT"};
        auto truepath = index.findFilePath(upperpath);
        BOOST_ASSERT(!truepath.empty());
        BOOST_CHECK(upperpath != truepath);
        fs::path expected{Global::getGamePath()};
        expected /= "data/CULLZONE.DAT";
        BOOST_CHECK_EQUAL(truepath.native(), expected.native());
    }
    {
        std::string upperpath{"DATA/MAPS/COMNBTM/COMNBTM.IPL"};
        auto truepath = index.findFilePath(upperpath);
        BOOST_ASSERT(!truepath.empty());
        BOOST_CHECK(upperpath != truepath);
        fs::path expected{Global::getGamePath()};
        expected /= "data/maps/comnbtm/comNbtm.ipl";
        BOOST_CHECK_EQUAL(truepath.native(), expected.native());
    }
}

BOOST_AUTO_TEST_CASE(test_file) {
    FileIndex index;

    index.indexTree(Global::getGamePath() + "/data");

    auto handle = index.openFile("cullzone.dat");
    BOOST_CHECK(handle != nullptr);
}

BOOST_AUTO_TEST_CASE(test_file_archive) {
    FileIndex index;

    index.indexArchive(Global::getGamePath() + "/models/gta3.img");

    auto handle = index.openFile("landstal.dff");
    BOOST_CHECK(handle != nullptr);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
