#include <newbase/audio/producer/vorbis.hpp>
#include <newbase/log.hpp>
#include <cassert>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>


using namespace nb;

struct nb::audio_producer_vorbis_p
{
    std::shared_ptr<nb::rvorbis> res; // keeps the data alive

    stb_vorbis *v {nullptr};
    stb_vorbis_info info {};
    size_t curr {0};

    audio_spec spec {};
};

audio_producer_vorbis::audio_producer_vorbis(std::shared_ptr<rvorbis> res)
{
    _d = new audio_producer_vorbis_p();
    _d->res = std::move(res);
    _d->v = nullptr;

    const std::byte* buf = reinterpret_cast<const std::byte*>(_d->res->data.data());
    size_t len = _d->res->data.size();

    // try to init vorbis decoder from buffer
    int err;
    _d->v = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(buf), len, &err, nullptr);
    if(_d->v && err == VORBIS__no_error)
    {
        _d->info = stb_vorbis_get_info(_d->v);
        _d->spec = audio_spec {audio_format::S16, _d->info.channels, _d->info.sample_rate};
        log::info("[audio] vorbis: producer init: S16, %d chs, %d Hz", _d->spec.channels, _d->spec.frequency);
    }
    else
    {
        if(_d->v)
        {
            stb_vorbis_close(_d->v);
            _d->v = nullptr;
        }
        
        log::error("[audio] vorbis: cannot open: error %d", err);
    }
}

audio_producer_vorbis::~audio_producer_vorbis()
{
    if(_d->v)
    {
        stb_vorbis_close(_d->v);
    }
    delete _d;
}

bool audio_producer_vorbis::is_valid() const
{
    return !(_d->spec == audio_spec{});
}

bool audio_producer_vorbis::is_seekable()
{
    if(!_d->v)
        return false;
    return true;
}

bool audio_producer_vorbis::is_complete()
{
    if(!_d->v)
        return false;
    return true;
}

bool audio_producer_vorbis::is_resetable()
{
    if(!_d->v)
        return false;
    return true;
}

audio_spec audio_producer_vorbis::spec()
{
    return _d->spec;
}


bool audio_producer_vorbis::seek(size_t frame_index)
{
    if(!_d->v)
        return false;
    int err = stb_vorbis_seek_frame(_d->v, static_cast<unsigned int>(frame_index));
    return err == VORBIS__no_error;
}

bool audio_producer_vorbis::reset()
{
    return seek(0);
}

size_t audio_producer_vorbis::frames_left()
{
    if(!_d->v)
        return 0;
    return static_cast<size_t>(stb_vorbis_stream_length_in_samples(_d->v)) - _d->curr;
}

size_t audio_producer_vorbis::frames_pull(audio_buffer::span dst, size_t max_frames)
{
    assert(dst.buffer_ref().spec() == _d->spec);
    size_t left = frames_left();
    size_t try_frames = std::min(max_frames, left);
    log::info("[vorbis] pull: max=%zu left=%zu try=%zu curr=%zu",
              max_frames, left, try_frames, _d->curr);
    if(!try_frames)
    {
        log::info("[vorbis] pull: nothing to produce");
        return 0;
    }

    size_t produced_frames = stb_vorbis_get_samples_short_interleaved(_d->v, dst.buffer_ref().channels(),
        reinterpret_cast<short*>(dst.begin()), try_frames*dst.buffer_ref().channels());
    _d->curr += produced_frames;
    log::info("[vorbis] pull: produced=%zu", produced_frames);
    return produced_frames;
}
