#include <newbase/audio/producer/vorbis.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include <cassert>
#include <cstring>
#include <algorithm>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

using namespace nb;

// Streaming tuning constants.
// CHUNK: bytes requested per read_partial_sync call.
// LOW_WATERMARK: refill when fewer than this many bytes remain in the sliding buffer.
static constexpr std::size_t STREAM_CHUNK         = 65536;
static constexpr std::size_t STREAM_LOW_WATERMARK = 8192;

struct nb::audio_producer_vorbis_p
{
    std::shared_ptr<nb::rvorbis> res;

    // --- Cached path (res->cached == true) ---
    // No extra state needed; we index directly into res->frames.

    // --- Streaming path (res->cached == false) ---
    // Sliding read buffer: [buf_start .. buf.size()-1] is unconsumed data.
    std::vector<char> buf {};
    std::size_t buf_start  {0};
    std::size_t file_offset{0};   // next byte to request from storage
    std::size_t file_size  {0};   // 0 = unknown, refill until read returns short

    stb_vorbis      *v    {nullptr};
    stb_vorbis_info  info {};

    // Interleaved S16 overflow from the last decoded vorbis frame that didn't
    // fit in the previous frames_pull call. Drained before decoding more.
    std::vector<short> overflow {};
    std::size_t overflow_start {0};

    // Set when the sliding buffer is empty after a refill attempt — the file is
    // exhausted. Cleared by reinit(). frames_left() returns 0 when this is set.
    bool stream_eof {false};

    // --- Common ---
    std::size_t curr {0};
    audio_spec  spec {};

    // ---- helpers ----

    // Move remaining data to the front and read up to STREAM_CHUNK more bytes.
    void compact_and_refill()
    {
        std::size_t avail = buf.size() - buf_start;
        if(buf_start > 0)
        {
            if(avail > 0)
                memmove(buf.data(), buf.data() + buf_start, avail);
            buf.resize(avail);
            buf_start = 0;
        }

        bool at_eof = (file_size > 0 && file_offset >= file_size);
        if(at_eof)
            return;

        std::size_t want = STREAM_CHUNK;
        if(file_size > 0)
            want = std::min(want, file_size - file_offset);

        std::vector<char> chunk;
        if(rman().read_partial_sync(res->id(), file_offset, want, chunk) && !chunk.empty())
        {
            buf.insert(buf.end(), chunk.begin(), chunk.end());
            file_offset += chunk.size();
        }
    }

    // Open the pushdata decoder. Feeds data in STREAM_CHUNK increments until
    // stb_vorbis either opens successfully or fails non-recoverably.
    bool open_pushdata()
    {
        while(true)
        {
            if(buf.size() - buf_start < STREAM_LOW_WATERMARK)
                compact_and_refill();

            std::size_t avail = buf.size() - buf_start;
            if(avail == 0)
                return false;

            int consumed = 0;
            int err      = 0;
            v = stb_vorbis_open_pushdata(
                reinterpret_cast<const unsigned char*>(buf.data() + buf_start),
                static_cast<int>(avail),
                &consumed, &err, nullptr);

            buf_start += static_cast<std::size_t>(consumed);

            if(v)
            {
                info = stb_vorbis_get_info(v);
                return true;
            }
            if(err != VORBIS_need_more_data)
            {
                log::error("[audio] vorbis: pushdata open error %d", err);
                return false;
            }
            // Need more data — loop.
        }
    }

    // Close and reinitialise the pushdata decoder from the beginning of the file.
    bool reinit()
    {
        if(v) { stb_vorbis_close(v); v = nullptr; }
        buf.clear();
        buf_start      = 0;
        file_offset    = 0;
        curr           = 0;
        overflow.clear();
        overflow_start = 0;
        stream_eof     = false;
        return open_pushdata();
    }
};

// ---------------------------------------------------------------------------

audio_producer_vorbis::audio_producer_vorbis(std::shared_ptr<rvorbis> res)
{
    _d = new audio_producer_vorbis_p();
    _d->res = res;

    if(!res || !res->valid)
    {
        log::error("[audio] vorbis: invalid resource");
        return;
    }

    _d->spec = res->spec;

    if(res->cached)
    {
        log::info("[audio] vorbis: cached producer: S16, %d chs, %d Hz, %zu frames",
                  _d->spec.channels, _d->spec.frequency, res->total_frames);
        return;
    }

    // Streaming: look up the file size from the asset handle (may be unknown).
    auto &handles = rman().handles();
    auto it = handles.find(res->id());
    if(it != handles.end() && it->second.size != std::string::npos)
        _d->file_size = it->second.size;

    if(!_d->open_pushdata())
        log::error("[audio] vorbis: streaming init failed for %x", res->id());
    else
        log::info("[audio] vorbis: streaming producer: S16, %d chs, %d Hz",
                  _d->spec.channels, _d->spec.frequency);
}

audio_producer_vorbis::~audio_producer_vorbis()
{
    if(_d->v)
        stb_vorbis_close(_d->v);
    delete _d;
}

bool audio_producer_vorbis::is_valid() const
{
    if(!_d->res || !_d->res->valid) return false;
    if(_d->res->cached)             return true;
    return _d->v != nullptr;
}

bool audio_producer_vorbis::is_seekable()  { return is_valid(); }
bool audio_producer_vorbis::is_complete()  { return is_valid(); }
bool audio_producer_vorbis::is_resetable() { return is_valid(); }

audio_spec audio_producer_vorbis::spec() { return _d->spec; }

bool audio_producer_vorbis::seek(size_t frame_index)
{
    if(!is_valid()) return false;

    if(_d->res->cached)
    {
        if(frame_index > _d->res->total_frames) return false;
        _d->curr = frame_index;
        return true;
    }

    // Streaming: only full rewind is efficient; arbitrary forward seek done by
    // reinitialising and decoding-discarding up to frame_index.
    if(!_d->reinit()) return false;
    if(frame_index == 0) return true;

    // Decode-and-discard to reach frame_index.
    int ch = _d->spec.channels;
    std::vector<short> discard(STREAM_CHUNK / sizeof(short));
    std::size_t remaining = frame_index;
    while(remaining > 0)
    {
        std::size_t batch = std::min(remaining, discard.size() / static_cast<std::size_t>(ch));
        // Pull via frames_pull would recurse; drive pushdata directly.
        if(_d->buf.size() - _d->buf_start < STREAM_LOW_WATERMARK)
            _d->compact_and_refill();

        std::size_t avail = _d->buf.size() - _d->buf_start;
        if(!avail) break;

        int n_channels;
        float **output;
        int n_samples;
        int consumed = stb_vorbis_decode_frame_pushdata(
            _d->v,
            reinterpret_cast<const unsigned char*>(_d->buf.data() + _d->buf_start),
            static_cast<int>(avail),
            &n_channels, &output, &n_samples);

        _d->buf_start += static_cast<std::size_t>(consumed);

        if(n_samples > 0)
        {
            std::size_t take = std::min(static_cast<std::size_t>(n_samples), remaining);
            _d->curr += take;
            remaining -= take;
        }
        else if(consumed == 0)
        {
            _d->compact_and_refill();
            if(_d->buf.size() - _d->buf_start == 0) break;
        }
    }

    return remaining == 0;
}

bool audio_producer_vorbis::reset()
{
    return seek(0);
}

size_t audio_producer_vorbis::frames_left()
{
    if(!is_valid()) return 0;
    if(!_d->res->cached && _d->stream_eof) return 0;
    std::size_t total = _d->res->total_frames;
    return _d->curr < total ? total - _d->curr : 0;
}

size_t audio_producer_vorbis::curr_frame() const  { return _d->curr; }
size_t audio_producer_vorbis::total_frames() const { return _d->res ? _d->res->total_frames : 0; }

size_t audio_producer_vorbis::frames_pull(audio_buffer::span dst, size_t max_frames)
{
    assert(dst.buffer_ref().spec() == _d->spec);

    size_t left       = frames_left();
    size_t try_frames = std::min(max_frames, left);
    if(!try_frames) return 0;

    int ch = _d->spec.channels;

    // --- Cached path ---
    if(_d->res->cached)
    {
        const short *src = reinterpret_cast<const short*>(_d->res->frames.data())
                           + _d->curr * ch;
        memcpy(dst.begin(), src, try_frames * static_cast<size_t>(ch) * sizeof(short));
        _d->curr += try_frames;
        return try_frames;
    }

    // --- Streaming path (pushdata) ---
    short *out_ptr = reinterpret_cast<short*>(dst.begin());
    size_t produced = 0;

    auto f2s = [](float f) -> short {
        if(f >  1.0f) f =  1.0f;
        if(f < -1.0f) f = -1.0f;
        return static_cast<short>(f * 32767.0f);
    };

    // 1. Drain overflow from the previous decode before requesting more data.
    if(!_d->overflow.empty())
    {
        std::size_t ovfl_samples = _d->overflow.size() - _d->overflow_start;
        std::size_t ovfl_frames  = ovfl_samples / static_cast<std::size_t>(ch);
        std::size_t take         = std::min(ovfl_frames, try_frames);
        memcpy(out_ptr, _d->overflow.data() + _d->overflow_start,
               take * static_cast<std::size_t>(ch) * sizeof(short));
        out_ptr             += take * static_cast<std::size_t>(ch);
        produced            += take;
        _d->curr            += take;
        _d->overflow_start  += take * static_cast<std::size_t>(ch);
        if(_d->overflow_start >= _d->overflow.size())
        {
            _d->overflow.clear();
            _d->overflow_start = 0;
        }
    }

    // 2. Decode frames until the destination is full.
    while(produced < try_frames)
    {
        if(_d->buf.size() - _d->buf_start < STREAM_LOW_WATERMARK)
            _d->compact_and_refill();

        std::size_t avail = _d->buf.size() - _d->buf_start;
        if(!avail) { _d->stream_eof = true; break; }

        int n_channels;
        float **output;
        int n_samples;
        int consumed = stb_vorbis_decode_frame_pushdata(
            _d->v,
            reinterpret_cast<const unsigned char*>(_d->buf.data() + _d->buf_start),
            static_cast<int>(avail),
            &n_channels, &output, &n_samples);

        _d->buf_start += static_cast<std::size_t>(consumed);

        if(n_samples > 0)
        {
            std::size_t need = try_frames - produced;
            std::size_t take = std::min(static_cast<std::size_t>(n_samples), need);

            // Copy the samples that fit.
            for(std::size_t s = 0; s < take; s++)
                for(int c = 0; c < ch; c++)
                    *out_ptr++ = f2s(output[c][s]);

            produced += take;
            _d->curr += take;

            // Buffer any leftover samples so they are not lost.
            std::size_t leftover = static_cast<std::size_t>(n_samples) - take;
            if(leftover > 0)
            {
                _d->overflow.resize(leftover * static_cast<std::size_t>(ch));
                _d->overflow_start = 0;
                for(std::size_t s = 0; s < leftover; s++)
                    for(int c = 0; c < ch; c++)
                        _d->overflow[s * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] =
                            f2s(output[c][take + s]);
            }
        }
        else if(consumed == 0)
        {
            // No progress — try a larger refill before giving up.
            _d->compact_and_refill();
            if(_d->buf.size() - _d->buf_start == 0) break;
        }
    }

    return produced;
}
