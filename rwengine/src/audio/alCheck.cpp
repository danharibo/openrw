#include "audio/alCheck.hpp"

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#include <iostream>

void checkALerror(const std::string& file, unsigned int line) {
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        std::cerr << "OpenAL error at " << file << ":" << line << ": ";

        switch (err) {
            case AL_INVALID_NAME:
                std::cerr << "Invalid name!";
                break;
            case AL_INVALID_VALUE:
                std::cerr << "Invalid value!";
                break;
            case AL_INVALID_OPERATION:
                std::cerr << "Invalid operation!";
                break;
            default:
                std::cerr << err;
        }

        std::cerr << std::endl;
    }
}
