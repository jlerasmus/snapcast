/***
        This file is part of snapcast
        Copyright (C) 2015  Hannes Ellinger

        This program is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program.  If not, see <http://www.gnu.org/licenses/>.
***/

#include "opus_decoder.hpp"
#include "common/aixlog.hpp"
#include "common/snap_exception.hpp"
#include "common/str_compat.hpp"

namespace decoder
{

#define ID_OPUS 0x4F505553

/// int: Number of samples per channel in the input signal.
/// This must be an Opus frame size for the encoder's sampling rate. For example, at 48 kHz the
/// permitted values are 120, 240, 480, 960, 1920, and 2880.
/// Passing in a duration of less than 10 ms (480 samples at 48 kHz) will prevent the encoder from using the LPC or hybrid modes.
static constexpr int const_max_frame_size = 2880;


OpusDecoder::OpusDecoder() : Decoder(), dec_(nullptr)
{
    pcm_.resize(120);
}


OpusDecoder::~OpusDecoder()
{
    if (dec_ != nullptr)
        opus_decoder_destroy(dec_);
}


bool OpusDecoder::decode(msg::PcmChunk* chunk)
{
    int frame_size = 0;

    while ((frame_size = opus_decode(dec_, (unsigned char*)chunk->payload, chunk->payloadSize, pcm_.data(), pcm_.size() / sample_format_.channels(), 0)) ==
           OPUS_BUFFER_TOO_SMALL)
    {
        if (pcm_.size() < const_max_frame_size * sample_format_.channels())
        {
            pcm_.resize(pcm_.size() * 2);
            LOG(INFO) << "OPUS encoding buffer too small, resizing to " << pcm_.size() / sample_format_.channels() << " samples per channel\n";
        }
        else
            break;
    }

    if (frame_size < 0)
    {
        LOG(ERROR) << "Failed to decode chunk: " << opus_strerror(frame_size) << ", IN size:  " << chunk->payloadSize << ", OUT size: " << pcm_.size() << '\n';
        return false;
    }
    else
    {
        LOG(DEBUG) << "Decoded chunk: size " << chunk->payloadSize << " bytes, decoded " << frame_size << " samples" << '\n';

        // copy encoded data to chunk
        chunk->payloadSize = frame_size * sample_format_.channels() * sizeof(opus_int16);
        chunk->payload = (char*)realloc(chunk->payload, chunk->payloadSize);
        memcpy(chunk->payload, (char*)pcm_.data(), chunk->payloadSize);
        return true;
    }
}


SampleFormat OpusDecoder::setHeader(msg::CodecHeader* chunk)
{
    // decode the opus pseudo header
    if (chunk->payloadSize < 12)
        throw SnapException("OPUS header too small");

    // decode the "opus id" magic number, this is our constant part that must match
    uint32_t id_opus;
    memcpy(&id_opus, chunk->payload, sizeof(id_opus));
    if (SWAP_32(id_opus) != ID_OPUS)
        throw SnapException("Not an Opus pseudo header");

    // decode the sampleformat
    uint32_t rate;
    memcpy(&rate, chunk->payload + 4, sizeof(id_opus));
    uint16_t bits;
    memcpy(&bits, chunk->payload + 8, sizeof(bits));
    uint16_t channels;
    memcpy(&channels, chunk->payload + 10, sizeof(channels));

    sample_format_.setFormat(SWAP_32(rate), SWAP_16(bits), SWAP_16(channels));
    LOG(DEBUG) << "Opus sampleformat: " << sample_format_.getFormat() << "\n";

    // create the decoder
    int error;
    dec_ = opus_decoder_create(sample_format_.rate(), sample_format_.channels(), &error);
    if (error != 0)
        throw SnapException("Failed to initialize Opus decoder: " + std::string(opus_strerror(error)));

    return sample_format_;
}

} // namespace decoder
