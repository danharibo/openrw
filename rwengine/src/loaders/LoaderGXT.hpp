#pragma once
#ifndef _LOADERGXT_HPP_
#define _LOADERGXT_HPP_
#include <data/forward.hpp>
#include <rw/forward.hpp>

class LoaderGXT {
public:
    void load(GameTexts& texts, FileHandle& file);
};

#endif
