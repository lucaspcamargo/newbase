#include <newbase/res/vorbis.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>


using namespace nb;


bool rvorbis::do_load()
{
    static constexpr float CACHE_THRESHOLD_SECS = 2.0f;

    log::info("[rloader_vorbis] loading: %x", id());

    // Read the file into a temporary buffer for probing. We never store these
    // raw bytes in the resource; they are discarded after this function returns.
    std::vector<char> raw;
    if(!rman().read_all_sync(id(), raw))
    {
        log::error("[rloader_vorbis] read failed: %x", id());
        return false;
    }

    // Open a decoder just to read stream metadata.
    int err;
    stb_vorbis *v = stb_vorbis_open_memory(
        reinterpret_cast<const unsigned char*>(raw.data()), static_cast<int>(raw.size()),
                                           &err, nullptr);
    if(!v)
    {
        log::error("[rloader_vorbis] decode open failed (err %d): %x", err, id());
        return false;
    }

    stb_vorbis_info info = stb_vorbis_get_info(v);
    int total = stb_vorbis_stream_length_in_samples(v);
    stb_vorbis_close(v);

    if(total < 0)
    {
        log::error("[rloader_vorbis] could not determine stream length: %x", id());
        return false;
    }

    spec = audio_spec{audio_format::S16,
        static_cast<uint8_t>(info.channels),
        static_cast<unsigned int>(info.sample_rate)};
        total_frames = static_cast<std::size_t>(total);

        float duration_secs = static_cast<float>(total) / static_cast<float>(info.sample_rate);

        if(duration_secs <= CACHE_THRESHOLD_SECS)
        {
            // Short sample: decode fully and cache the PCM.
            short *pcm = nullptr;
            int num_ch, freq;
            int n = stb_vorbis_decode_memory(
                reinterpret_cast<const unsigned char*>(raw.data()), static_cast<int>(raw.size()),
                                             &num_ch, &freq, &pcm);
            if(n > 0 && pcm)
            {
                const std::size_t size_bytes = sizeof(short) * static_cast<std::size_t>(n) * static_cast<std::size_t>(num_ch);
                frames.resize(size_bytes);
                memcpy(frames.data(), pcm, size_bytes);
                free(pcm);
                cached = true;
                log::info("[vorbis] cached: %x, %d frames, %.2fs, %d Hz, %d ch",
                          id(), n, duration_secs, freq, num_ch);
            }
            else
            {
                log::error("[vorbis] decode failed for short sample: %x", id());
                return false;
            }
        }
        else
        {
            // Long file: store only metadata; producer streams from storage.
            log::info("[vorbis] streaming: %x, %d frames, %.2fs, %d Hz, %d ch",
                      id(), total, duration_secs, info.sample_rate, info.channels);
        }

        valid = true;
        return true;
}
