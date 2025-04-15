#include <newbase/audio/producer/vorbis.h>
#include <cassert>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>


using namespace nb;

struct nb::audio_producer_vorbis_p
{
    const std::byte *buf {nullptr};
    size_t len {0};

    stb_vorbis *v {nullptr};
    stb_vorbis_info info {};
    size_t curr {0};

    audio_spec spec {};
};

audio_producer_vorbis::audio_producer_vorbis(const std::byte *buf, size_t len)
{
    _d = new audio_producer_vorbis_p();
    _d->buf = buf;
    _d->len = len;
    _d->v = nullptr;

    // try to init vorbis decoder from buffer
    int err;
    _d->v = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(buf), len, &err, nullptr);
    if(_d->v && err == VORBIS__no_error)
    {
        _d->info = stb_vorbis_get_info(_d->v);
        _d->spec = audio_spec {audio_format::S16, _d->info.channels, _d->info.sample_rate};
    }
}

audio_producer_vorbis::~audio_producer_vorbis()
{
    delete _d;
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

size_t audio_producer_vorbis::frames_pull(audio_buffer &buf, size_t max_frames)
{
    assert(buf.spec() == _d->spec);
    size_t try_frames = std::min(max_frames, frames_left());
    if(!try_frames)
        return 0;
    buf.data().resize(try_frames*buf.frame_stride());
    size_t produced_frames = stb_vorbis_get_samples_short_interleaved(_d->v, buf.channels(), 
        reinterpret_cast<short*>(buf.data().data()), try_frames*buf.channels());
    buf.data().resize(produced_frames*buf.frame_stride());
    _d->curr += produced_frames;
    return produced_frames;
}
