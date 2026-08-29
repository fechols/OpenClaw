#include <fstream>
#include <memory>
#include <vector>
#include <stdint.h>

#include "libwap.h"
#include "IO.h"

#include <iostream>
using namespace std;

// Upper bound on the pixel count of a single PID. width/height come straight from the
// file, so their product must be range-checked before it is used to size an allocation.
// Far larger than any legitimate Claw asset, small enough to stay safely allocatable.
static const uint64_t WAP_PID_MAX_PIXELS = 16 * 1024 * 1024;

WapPid* WAP_PidLoadFromData(char* data, size_t size, WapPal* palette)
{
    uint32_t x, y;
    uint8_t byte;
    WapPid* wapPid = NULL;

    if ((data == NULL) || (size == 0))
    {
        return NULL;
    }

    /********************** PID HEADER/PROPERTIES **********************/

    wapPid = new WapPid;

    // Set default values;
    (*wapPid) = { 0 };

    // Own wapPid for the rest of the function so that every exit - including an exception
    // thrown out of InputStream - releases it exactly once. Ownership is only handed back
    // to the caller on the success path at the very bottom.
    std::unique_ptr<WapPid, void(*)(WapPid*)> wapPidGuard(wapPid, WAP_PidDestroy);

    // Only holds a palette when we built one from an embedded one; a caller-supplied
    // palette is never owned here.
    std::unique_ptr<WapPal, void(*)(WapPal*)> ownedPalette(NULL, WAP_PalDestroy);
    WapPal* imagePalette = NULL;

    x = 0;
    y = 0;
    // This is an extern "C" entry point, so no exception may escape it. The header read
    // below can throw on a truncated file, so it lives inside the try along with the
    // pixel decoding.
    try {
        InputStream pidFileStream(data, size);

        pidFileStream.read(wapPid->fileDesc,
            wapPid->flags,
            wapPid->width,
            wapPid->height,
            wapPid->offsetX,
            wapPid->offsetY,
            wapPid->unk0,
            wapPid->unk1);

        /********************** PID PALETTE **********************/

        // If image has embedded palette within it, extract it
        if (wapPid->flags & WAP_PID_FLAG_EMBEDDED_PALETTE)
        {
            // The palette is the last WAP_PALETTE_SIZE_BYTES of the file - guard the
            // subtraction, which would otherwise underflow on a short file.
            if (size < WAP_PALETTE_SIZE_BYTES)
            {
                return NULL;
            }

            size_t paletteOffset = size - WAP_PALETTE_SIZE_BYTES;
            char* paletteData = &(data[paletteOffset]);
            ownedPalette.reset(WAP_PalLoadFromData(paletteData, WAP_PALETTE_SIZE_BYTES));
            imagePalette = ownedPalette.get();
        }
        else
        {
            imagePalette = palette;
        }

        // Make sure we have loaded a palette
        if (imagePalette == NULL)
        {
            return NULL;
        }

        /********************** PID PIXELS **********************/

        // Compute in 64 bits: the uint32 product wraps for large dimensions, and the
        // decode loops below still bound on the original width/height, so a wrapped
        // count would let them write past the end of the allocation.
        uint64_t colorsCount = (uint64_t)wapPid->width * (uint64_t)wapPid->height;
        if (colorsCount > WAP_PID_MAX_PIXELS)
        {
            return NULL;
        }

        wapPid->colorsCount = (uint32_t)colorsCount;
        wapPid->colors = new WAP_ColorRGBA[wapPid->colorsCount];

        // PID is compressed, RLE
        if (wapPid->flags & WAP_PID_FLAG_COMPRESSION)
        {
            while (y < wapPid->height)
            {
                pidFileStream.read(byte);

                if (byte > 128)
                {
                    int32_t i = byte - 128;
                    while ((i > 0) && (y < wapPid->height))
                    {
                        wapPid->colors[y * wapPid->width + x] = WAP_ColorRGBA{ 0, 0, 0, 1 };
                        x++;
                        if (x == wapPid->width)
                        {
                            x = 0;
                            y++;
                        }
                        i--;
                    }
                }
                else
                {
                    int32_t i = byte;
                    while ((i > 0) && (y < wapPid->height))
                    {
                        pidFileStream.read(byte);

                        wapPid->colors[y * wapPid->width + x] = WAP_ColorRGBA{ imagePalette->colors[byte].r,
                            imagePalette->colors[byte].g,
                            imagePalette->colors[byte].b,
                            imagePalette->colors[byte].a };

                        x++;
                        if (x == wapPid->width)
                        {
                            x = 0;
                            y++;
                        }
                        i--;
                    }
                }
            }
        }
        else
        {
            while (y < wapPid->height)
            {
                int32_t i = 1;
                pidFileStream.read(byte);

                // PID related encoding probably, this means how many same pixels are following.
                // e.g. if byte = 220, then 220-192=28 same pixels are next to each other
                if (byte > 192)
                {
                    i = byte - 192;
                    pidFileStream.read(byte);
                }

                while ((i > 0) && (y < wapPid->height))
                {
                    wapPid->colors[y * wapPid->width + x] = WAP_ColorRGBA{ imagePalette->colors[byte].r,
                        imagePalette->colors[byte].g,
                        imagePalette->colors[byte].b,
                        imagePalette->colors[byte].a };

                    x++;
                    if (x == wapPid->width)
                    {
                        x = 0;
                        y++;
                    }
                    i--;
                }
            }
        }
    }
    catch (...)
    {
        // wapPidGuard / ownedPalette release everything built so far.
        return NULL;
    }

    // Success - hand ownership of the pid back to the caller. Any embedded palette we
    // built is still owned by ownedPalette and is released here.
    return wapPidGuard.release();
}

WapPid* WAP_PidLoadFromFile(const char* pidFilePath, WapPal* palette)
{
    std::ifstream pidFileStream(pidFilePath, std::ios::binary);
    if (!pidFileStream.is_open())
    {
        return NULL;
    }

    // Read whole file
    std::vector<char> pidFileContents((std::istreambuf_iterator<char>(pidFileStream)), std::istreambuf_iterator<char>());
    if (!pidFileStream.good())
    {
        return NULL;
    }

    return WAP_PidLoadFromData(pidFileContents.data(), pidFileContents.size(), palette);
}

WapPid* WAP_PidLoadFromRezFile(RezFile* rezFile, WapPal* palette)
{
    // Check input validity
    if (rezFile == NULL)
    {
        return NULL;
    }

    char* data = WAP_GetRezFileData(rezFile);
    if (data == NULL)
    {
        return NULL;
    }

    return WAP_PidLoadFromData(data, rezFile->size, palette);
}

WapPid* WAP_PidLoadFromRezArchive(RezArchive* rezArchive, const char* pidRezPath, WapPal* palette)
{
    // Check input validity
    if ((rezArchive == NULL) || (pidRezPath == NULL))
    {
        return NULL;
    }

    RezFile* pidRezFile = WAP_GetRezFileFromRezArchive(rezArchive, pidRezPath);
    if (pidRezFile == NULL)
    {
        return NULL;
    }

    return WAP_PidLoadFromRezFile(pidRezFile, palette);
}

void WAP_PidDestroy(WapPid* wapPid)
{
    if (wapPid == NULL)
    {
        return;
    }

    delete[] wapPid->colors;
    delete wapPid;
    wapPid = NULL;  
}