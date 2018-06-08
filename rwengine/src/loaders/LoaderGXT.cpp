#include "loaders/LoaderGXT.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <platform/FileHandle.hpp>

#include "data/GameTexts.hpp"
#include "rw/bit_cast.cpp"

void LoaderGXT::load(GameTexts &texts, const FileHandle &file) {
    auto data = file->data;

    data += 4;  // TKEY

    std::uint32_t blocksize = bit_cast<std::uint32_t>(*data);

    data += 4;

    auto tdata = data + blocksize + 8;

    for (size_t t = 0; t < blocksize / 12; ++t) {
        size_t offset = bit_cast<std::uint32_t>(data + (t * 12 + 0));
        std::string id(data + (t * 12 + 4));

        GameStringChar *stringSrc =
            reinterpret_cast<GameStringChar *>(tdata + offset);
        GameString string(stringSrc);
        texts.addText(id, std::move(string));
    }
}
