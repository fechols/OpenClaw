#include <fstream>
#include <memory>
#include <stdint.h>
#include <string.h>
#include <string.h>
#include <exception>

#include "libwap.h"
#include "IO.h"
#include "Util.h"

WapAni* WAP_AniLoadFromDataImpl(char* data, size_t size)
{
    uint32_t i;
    WapAni* wapAni = NULL;

    if ((data == NULL) || (size == 0))
    {
        return NULL;
    }

    /********************** ANI HEADER/PROPERTIES **********************/

    wapAni = new WapAni;

    // Set default values;
    (*wapAni) = { 0 };

    // Own wapAni for the rest of the function: the reads below throw on malformed data,
    // and without this the imageSetPath / animationFrames / per-frame event strings built
    // so far all leaked.
    std::unique_ptr<WapAni, void(*)(WapAni*)> wapAniGuard(wapAni, WAP_AniDestroy);

    InputStream aniFileStream(data, size);
    // Read first 32 bytes
    aniFileStream.read(wapAni->signature,
        wapAni->unk0,
        wapAni->unk1,
        wapAni->animationFramesCount,
        wapAni->imageSetPathLength,
        wapAni->unk2,
        wapAni->unk3,
        wapAni->unk4);

    // Check if loaded file at leasr resembles ANI file format since there is no checksum.
    // Compute in 64 bits: animationFramesCount is an unvalidated uint32 from the file, so
    // "count * 20" wraps and lets an absurd frame count sail through this sanity check.
    const uint64_t minimumExpectedSize =
        (uint64_t)wapAni->animationFramesCount * 20ull + (uint64_t)wapAni->imageSetPathLength;
    if ((wapAni->animationFramesCount == 0) || (minimumExpectedSize > (uint64_t)size))
    {
        return NULL;
    }

    // Read imageSetPathLength is not including NULL terminating character
    wapAni->imageSetPath = ReadAndAllocateString(aniFileStream, wapAni->imageSetPathLength);

    /********************** ANI ANIMATION FRAMES **********************/

    // Value-initialize so that a throw part-way through the fill loop below still leaves
    // every frame's eventFilePath in a state the destructor can safely walk.
    wapAni->animationFrames = new AniAnimationFrame[wapAni->animationFramesCount]();

    // Load all animation frames, one by one
    for (i = 0; i < wapAni->animationFramesCount; i++)
    {
        AniAnimationFrame& animFrame = wapAni->animationFrames[i];

        // Set default values
        animFrame = { 0 };

        aniFileStream.read(animFrame.triggeredEventFlag,
            animFrame.unk0,
            animFrame.unk1,
            animFrame.unk2,
            animFrame.imageFileId,
            animFrame.duration,
            animFrame.unk3,
            animFrame.unk4,
            animFrame.unk5,
            animFrame.unk6,
            animFrame.unk7);
    
        if (animFrame.triggeredEventFlag == 2)
        {
            animFrame.eventFilePath = ReadNullTerminatedString(aniFileStream);
        }
    }

    // Success - ownership passes to the caller.
    return wapAniGuard.release();
}

WapAni* WAP_AniLoadFromData(char* data, size_t size)
{
    try
    {
        return WAP_AniLoadFromDataImpl(data, size);
    }
    catch (...)
    {
        return NULL;
    }
}

WapAni* WAP_AniLoadFromFile(char* aniFilePath)
{
    std::ifstream aniFileStream(aniFilePath, std::ios::binary);
    if (!aniFileStream.is_open())
    {
        return NULL;
    }

    // Read whole file
    std::vector<char> aniFileContents((std::istreambuf_iterator<char>(aniFileStream)), std::istreambuf_iterator<char>());
    if (!aniFileStream.good())
    {
        return NULL;
    }

    return WAP_AniLoadFromData(aniFileContents.data(), aniFileContents.size());
}

WapAni* WAP_AniLoadFromRezFile(RezFile* rezFile)
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

    return WAP_AniLoadFromData(data, rezFile->size);
}

WapAni* WAP_AniLoadFromRezArchive(RezArchive* rezArchive, const char* aniFilePath)
{
    // Check input validity
    if ((rezArchive == NULL) || (aniFilePath == NULL))
    {
        return NULL;
    }

    RezFile* aniRezFile = WAP_GetRezFileFromRezArchive(rezArchive, aniFilePath);
    if (aniRezFile == NULL)
    {
        return NULL;
    }

    return WAP_AniLoadFromRezFile(aniRezFile);
}

void WAP_AniDestroy(WapAni* wapAni)
{
    uint32_t i;

    // Check for validity
    if (wapAni == NULL)
    {
        return;
    }

    // Delete animation frame's event path strings.
    // animationFramesCount is set from the header before the frame array is allocated, and
    // an aborted parse can leave the count set with the array still NULL - so the array
    // itself has to be checked rather than trusting the count.
    if (wapAni->animationFrames != NULL)
    {
        for (i = 0; i < wapAni->animationFramesCount; i++)
        {
            delete[] wapAni->animationFrames[i].eventFilePath;
        }
    }

    delete[] wapAni->animationFrames;
    delete[] wapAni->imageSetPath;

    delete wapAni;
    wapAni = NULL;
}