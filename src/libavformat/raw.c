/*
 * RAW muxer and demuxer
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2005 Alex Beregszaszi
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/crc.h"
#include "libavcodec/ac3_parser.h"
#include "libavcodec/bitstream.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "raw.h"

#ifdef CONFIG_MUXERS
/* simple formats */
static int flac_write_header(struct AVFormatContext *s)
{
    static const uint8_t header[8] = {
        0x66, 0x4C, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22
    };
    uint8_t *streaminfo = s->streams[0]->codec->extradata;
    int len = s->streams[0]->codec->extradata_size;
    if(streaminfo != NULL && len > 0) {
        put_buffer(s->pb, header, 8);
        put_buffer(s->pb, streaminfo, len);
    }
    return 0;
}


static int roq_write_header(struct AVFormatContext *s)
{
    static const uint8_t header[] = {
        0x84, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x1E, 0x00
    };

    put_buffer(s->pb, header, 8);
    put_flush_packet(s->pb);

    return 0;
}

static int null_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    return 0;
}

static int raw_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}
#endif //CONFIG_MUXERS

/* raw input */
static int raw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    int id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec->codec_type = CODEC_TYPE_VIDEO;
        } else {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
        }
        st->codec->codec_id = id;

        switch(st->codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            st->codec->sample_rate = ap->sample_rate;
            st->codec->channels = ap->channels;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            break;
        case CODEC_TYPE_VIDEO:
            if(ap->time_base.num)
                av_set_pts_info(st, 64, ap->time_base.num, ap->time_base.den);
            else
                av_set_pts_info(st, 64, 1, 25);
            st->codec->width = ap->width;
            st->codec->height = ap->height;
            st->codec->pix_fmt = ap->pix_fmt;
            if(st->codec->pix_fmt == PIX_FMT_NONE)
                st->codec->pix_fmt= PIX_FMT_YUV420P;
            break;
        default:
            return -1;
        }
    return 0;
}

#define RAW_PACKET_SIZE 1024

static int raw_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size, bps;
    //    AVStream *st = s->streams[0];

    size= RAW_PACKET_SIZE;

    ret= av_get_packet(s->pb, pkt, size);

    pkt->stream_index = 0;
    if (ret <= 0) {
        return AVERROR(EIO);
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;

    bps= av_get_bits_per_sample(s->streams[0]->codec->codec_id);
    assert(bps); // if false there IS a bug elsewhere (NOT in this function)
    pkt->dts=
    pkt->pts= pkt->pos*8 / (bps * s->streams[0]->codec->channels);

    return ret;
}

static int raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(EIO);

    pkt->pos= url_ftell(s->pb);
    pkt->stream_index = 0;
    ret = get_partial_buffer(s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR(EIO);
    }
    pkt->size = ret;
    return ret;
}

static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    ret= av_get_packet(s->pb, pkt, packet_size);
    pkt->pts=
    pkt->dts= pkt->pos / packet_size;

    pkt->stream_index = 0;
    if (ret != packet_size) {
        return AVERROR(EIO);
    } else {
        return 0;
    }
}

// http://www.artificis.hu/files/texts/ingenient.txt
static int ingenient_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size, w, h, unk1, unk2;

    if (get_le32(s->pb) != MKTAG('M', 'J', 'P', 'G'))
        return AVERROR(EIO); // FIXME

    size = get_le32(s->pb);

    w = get_le16(s->pb);
    h = get_le16(s->pb);

    url_fskip(s->pb, 8); // zero + size (padded?)
    url_fskip(s->pb, 2);
    unk1 = get_le16(s->pb);
    unk2 = get_le16(s->pb);
    url_fskip(s->pb, 22); // ascii timestamp

    av_log(NULL, AV_LOG_DEBUG, "Ingenient packet: size=%d, width=%d, height=%d, unk1=%d unk2=%d\n",
        size, w, h, unk1, unk2);

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(EIO);

    pkt->pos = url_ftell(s->pb);
    pkt->stream_index = 0;
    ret = get_buffer(s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR(EIO);
    }
    pkt->size = ret;
    return ret;
}

int pcm_read_seek(AVFormatContext *s,
                  int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;
    int block_align, byte_rate;
    int64_t pos;

    st = s->streams[0];

    block_align = st->codec->block_align ? st->codec->block_align :
        (av_get_bits_per_sample(st->codec->codec_id) * st->codec->channels) >> 3;
    byte_rate = st->codec->bit_rate ? st->codec->bit_rate >> 3 :
        block_align * st->codec->sample_rate;

    if (block_align <= 0 || byte_rate <= 0)
        return -1;

    /* compute the position by aligning it to block_align */
    pos = av_rescale_rnd(timestamp * byte_rate,
                         st->time_base.num,
                         st->time_base.den * (int64_t)block_align,
                         (flags & AVSEEK_FLAG_BACKWARD) ? AV_ROUND_DOWN : AV_ROUND_UP);
    pos *= block_align;

    /* recompute exact position */
    st->cur_dts = av_rescale(pos, st->time_base.den, byte_rate * (int64_t)st->time_base.num);
    url_fseek(s->pb, pos + s->data_offset, SEEK_SET);
    return 0;
}

static int audio_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

/* mpeg1/h263 input */
static int video_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;

    /* for mjpeg, specify frame rate */
    /* for mpeg4 specify it too (most mpeg4 streams do not have the fixed_vop_rate set ...)*/
    if (ap->time_base.num) {
        av_set_pts_info(st, 64, ap->time_base.num, ap->time_base.den);
    } else if ( st->codec->codec_id == CODEC_ID_MJPEG ||
                st->codec->codec_id == CODEC_ID_MPEG4 ||
                st->codec->codec_id == CODEC_ID_DIRAC ||
                st->codec->codec_id == CODEC_ID_H264) {
        av_set_pts_info(st, 64, 1, 25);
    }

    return 0;
}

#define SEQ_START_CODE          0x000001b3
#define GOP_START_CODE          0x000001b8
#define PICTURE_START_CODE      0x00000100
#define SLICE_START_CODE        0x00000101
#define PACK_START_CODE         0x000001ba
#define VIDEO_ID                0x000001e0
#define AUDIO_ID                0x000001c0

static int mpegvideo_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int pic=0, seq=0, slice=0, pspack=0, pes=0;
    int i;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            switch(code){
            case     SEQ_START_CODE:   seq++; break;
            case PICTURE_START_CODE:   pic++; break;
            case   SLICE_START_CODE: slice++; break;
            case    PACK_START_CODE: pspack++; break;
            }
            if     ((code & 0x1f0) == VIDEO_ID)   pes++;
            else if((code & 0x1e0) == AUDIO_ID)   pes++;
        }
    }
    if(seq && seq*9<=pic*10 && pic*9<=slice*10 && !pspack && !pes)
        return AVPROBE_SCORE_MAX/2+1; // +1 for .mpg
    return 0;
}

#define VISUAL_OBJECT_START_CODE       0x000001b5
#define VOP_START_CODE                 0x000001b6

static int mpeg4video_probe(AVProbeData *probe_packet)
{
    uint32_t temp_buffer= -1;
    int VO=0, VOL=0, VOP = 0, VISO = 0, res=0;
    int i;

    for(i=0; i<probe_packet->buf_size; i++){
        temp_buffer = (temp_buffer<<8) + probe_packet->buf[i];
        if ((temp_buffer & 0xffffff00) != 0x100)
            continue;

        if (temp_buffer == VOP_START_CODE)                         VOP++;
        else if (temp_buffer == VISUAL_OBJECT_START_CODE)          VISO++;
        else if (temp_buffer < 0x120)                              VO++;
        else if (temp_buffer < 0x130)                              VOL++;
        else if (   !(0x1AF < temp_buffer && temp_buffer < 0x1B7)
                 && !(0x1B9 < temp_buffer && temp_buffer < 0x1C4)) res++;
    }

    if ( VOP >= VISO && VOP >= VOL && VO >= VOL && VOL > 0 && res==0)
        return AVPROBE_SCORE_MAX/2;
    return 0;
}

static int h264_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int sps=0, pps=0, idr=0, res=0;
    int i;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            int ref_idc= (code>>5)&3;
            int type   = code & 0x1F;
            static const int8_t ref_zero[32]={
                2, 0, 0, 0, 0,-1, 1,-1,
               -1, 1, 1, 1, 1,-1, 2, 2,
                2, 2, 2, 0, 2, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2
            };

            if(code & 0x80) //forbidden bit
                return 0;

            if(ref_zero[type] == 1 && ref_idc)
                return 0;
            if(ref_zero[type] ==-1 && !ref_idc)
                return 0;
            if(ref_zero[type] == 2)
                res++;

            switch(type){
            case     5:   idr++; break;
            case     7:
                if(p->buf[i+2]&0x0F)
                    return 0;
                sps++;
                break;
            case     8:   pps++; break;
            }
        }
    }
    if(sps && pps && idr && res<(sps+pps+idr))
        return AVPROBE_SCORE_MAX/2+1; // +1 for .mpg
    return 0;
}

static int h263_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    d = p->buf;
    code = (d[0] << 14) | (d[1] << 6) | (d[2] >> 2);
    if (code == 0x20) {
        return 50;
    }
    return 0;
}

static int h261_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    d = p->buf;
    code = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
    if (code == 0x10) {
        return 50;
    }
    return 0;
}

#define DCA_MARKER_14B_BE 0x1FFFE800
#define DCA_MARKER_14B_LE 0xFF1F00E8
#define DCA_MARKER_RAW_BE 0x7FFE8001
#define DCA_MARKER_RAW_LE 0xFE7F0180
static int dts_probe(AVProbeData *p)
{
    const uint8_t *buf, *bufp;
    uint32_t state = -1;

    buf = p->buf;

    for(; buf < (p->buf+p->buf_size)-2; buf+=2) {
        bufp = buf;
        state = (state << 16) | bytestream_get_be16(&bufp);

        /* Regular bitstream */
        if (state == DCA_MARKER_RAW_BE || state == DCA_MARKER_RAW_LE)
            return AVPROBE_SCORE_MAX/2+1;

        /* 14 bits big endian bitstream */
        if (state == DCA_MARKER_14B_BE)
            if ((bytestream_get_be16(&bufp) & 0xFFF0) == 0x07F0)
                return AVPROBE_SCORE_MAX/2+1;

        /* 14 bits little endian bitstream */
        if (state == DCA_MARKER_14B_LE)
            if ((bytestream_get_be16(&bufp) & 0xF0FF) == 0xF007)
                return AVPROBE_SCORE_MAX/2+1;
    }

    return 0;
}

static int dirac_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('B', 'B', 'C', 'D'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int ac3_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0, frames;
    uint8_t *buf, *buf2, *end;
    AC3HeaderInfo hdr;
    GetBitContext gbc;

    max_frames = 0;
    buf = p->buf;
    end = buf + p->buf_size;

    for(; buf < end; buf++) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            init_get_bits(&gbc, buf2, 54);
            if(ff_ac3_parse_header(&gbc, &hdr) < 0)
                break;
            if(buf2 + hdr.frame_size > end ||
               av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0, buf2 + 2, hdr.frame_size - 2))
                break;
            buf2 += hdr.frame_size;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == p->buf)
            first_frames = frames;
    }
    if   (first_frames>=3) return AVPROBE_SCORE_MAX * 3 / 4;
    else if(max_frames>=3) return AVPROBE_SCORE_MAX / 2;
    else if(max_frames>=1) return 1;
    else                   return 0;
}

static int flac_probe(AVProbeData *p)
{
    if(memcmp(p->buf, "fLaC", 4)) return 0;
    else                          return AVPROBE_SCORE_MAX / 2;
}


/* Note: Do not forget to add new entries to the Makefile as well. */

AVInputFormat aac_demuxer = {
    "aac",
    NULL_IF_CONFIG_SMALL("ADTS AAC"),
    0,
    NULL,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "aac",
    CODEC_ID_AAC,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "aac",
    .value = CODEC_ID_AAC,
#endif
};

#ifdef CONFIG_AC3_DEMUXER
AVInputFormat ac3_demuxer = {
    "ac3",
    NULL_IF_CONFIG_SMALL("raw AC-3"),
    0,
    ac3_probe,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "ac3",
    CODEC_ID_AC3,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "ac3",
    .value = CODEC_ID_AC3,
#endif
};
#endif

#ifdef CONFIG_MUXERS
AVOutputFormat ac3_muxer = {
    "ac3",
    NULL_IF_CONFIG_SMALL("raw AC-3"),
    "audio/x-ac3",
    "ac3",
    0,
    CODEC_ID_AC3,
    CODEC_ID_NONE,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat dirac_demuxer = {
    "dirac",
    NULL_IF_CONFIG_SMALL("raw Dirac"),
    0,
    dirac_probe,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    0,
    CODEC_ID_DIRAC,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .value = CODEC_ID_DIRAC,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat dirac_muxer = {
    "dirac",
    NULL_IF_CONFIG_SMALL("raw Dirac"),
    NULL,
    "drc",
    0,
    CODEC_ID_NONE,
    CODEC_ID_DIRAC,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif

AVInputFormat dts_demuxer = {
    "dts",
    NULL_IF_CONFIG_SMALL("raw DTS"),
    0,
    dts_probe,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "dts",
    CODEC_ID_DTS,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "dts",
    .value = CODEC_ID_DTS,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat dts_muxer = {
    "dts",
    NULL_IF_CONFIG_SMALL("raw DTS"),
    "audio/x-dca",
    "dts",
    0,
    CODEC_ID_DTS,
    CODEC_ID_NONE,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif

AVInputFormat flac_demuxer = {
    "flac",
    NULL_IF_CONFIG_SMALL("raw FLAC"),
    0,
    flac_probe,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "flac",
    CODEC_ID_FLAC,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "flac",
    .value = CODEC_ID_FLAC,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat flac_muxer = {
    "flac",
    NULL_IF_CONFIG_SMALL("raw FLAC"),
    "audio/x-flac",
    "flac",
    0,
    CODEC_ID_FLAC,
    CODEC_ID_NONE,
    flac_write_header,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat gsm_demuxer = {
    "gsm",
    NULL_IF_CONFIG_SMALL("GSM"),
    0,
    NULL,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "gsm",
    CODEC_ID_GSM,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "gsm",
    .value = CODEC_ID_GSM,
#endif
};

AVInputFormat h261_demuxer = {
    "h261",
    NULL_IF_CONFIG_SMALL("raw H.261"),
    0,
    h261_probe,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "h261",
    CODEC_ID_H261,
#else
    AVFMT_GENERIC_INDEX,
    "h261",
    CODEC_ID_H261,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat h261_muxer = {
    "h261",
    NULL_IF_CONFIG_SMALL("raw H.261"),
    "video/x-h261",
    "h261",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H261,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat h263_demuxer = {
    "h263",
    NULL_IF_CONFIG_SMALL("raw H.263"),
    0,
    h263_probe,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    0,
    CODEC_ID_H263,
#else
    .flags= AVFMT_GENERIC_INDEX,
//    .extensions = "h263", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H263,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat h263_muxer = {
    "h263",
    NULL_IF_CONFIG_SMALL("raw H.263"),
    "video/x-h263",
    "h263",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H263,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat h264_demuxer = {
    "h264",
    NULL_IF_CONFIG_SMALL("raw H.264 video format"),
    0,
    h264_probe,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "h26l,h264,264", //FIXME remove after writing mpeg4_probe
    CODEC_ID_H264,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "h26l,h264,264", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H264,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat h264_muxer = {
    "h264",
    NULL_IF_CONFIG_SMALL("raw H.264 video format"),
    NULL,
    "h264",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H264,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat ingenient_demuxer = {
    "ingenient",
    NULL_IF_CONFIG_SMALL("Ingenient MJPEG"),
    0,
    NULL,
    video_read_header,
    ingenient_read_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "cgi", // FIXME
    CODEC_ID_MJPEG,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "cgi", // FIXME
    .value = CODEC_ID_MJPEG,
#endif
};

AVInputFormat m4v_demuxer = {
    "m4v",
    NULL_IF_CONFIG_SMALL("raw MPEG-4 video format"),
    0,
    mpeg4video_probe, /** probing for mpeg4 data */
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "m4v", //FIXME remove after writing mpeg4_probe
    CODEC_ID_MPEG4,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "m4v", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_MPEG4,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat m4v_muxer = {
    "m4v",
    NULL_IF_CONFIG_SMALL("raw MPEG-4 video format"),
    NULL,
    "m4v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG4,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat mjpeg_demuxer = {
    "mjpeg",
    NULL_IF_CONFIG_SMALL("MJPEG video"),
    0,
    NULL,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "mjpg,mjpeg",
    CODEC_ID_MJPEG,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mjpg,mjpeg",
    .value = CODEC_ID_MJPEG,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat mjpeg_muxer = {
    "mjpeg",
    NULL_IF_CONFIG_SMALL("MJPEG video"),
    "video/x-mjpeg",
    "mjpg,mjpeg",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat mlp_demuxer = {
    "mlp",
    NULL_IF_CONFIG_SMALL("raw MLP"),
    0,
    NULL,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "mlp",
    CODEC_ID_MLP,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mlp",
    .value = CODEC_ID_MLP,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat mpeg1video_muxer = {
    "mpeg1video",
    NULL_IF_CONFIG_SMALL("MPEG video"),
    "video/x-mpeg",
    "mpg,mpeg,m1v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG1VIDEO,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

#ifdef CONFIG_MUXERS
AVOutputFormat mpeg2video_muxer = {
    "mpeg2video",
    NULL_IF_CONFIG_SMALL("MPEG-2 video"),
    NULL,
    "m2v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG2VIDEO,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat mpegvideo_demuxer = {
    "mpegvideo",
    NULL_IF_CONFIG_SMALL("MPEG video"),
    0,
    mpegvideo_probe,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    0,
    CODEC_ID_MPEG1VIDEO,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .value = CODEC_ID_MPEG1VIDEO,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat null_muxer = {
    "null",
    NULL_IF_CONFIG_SMALL("null video format"),
    NULL,
    NULL,
    0,
#ifdef WORDS_BIGENDIAN
    CODEC_ID_PCM_S16BE,
#else
    CODEC_ID_PCM_S16LE,
#endif
    CODEC_ID_RAWVIDEO,
    NULL,
    null_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

AVInputFormat rawvideo_demuxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    0,
    NULL,
    raw_read_header,
    rawvideo_read_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "yuv,cif,qcif,rgb",
    CODEC_ID_RAWVIDEO,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "yuv,cif,qcif,rgb",
    .value = CODEC_ID_RAWVIDEO,
#endif
};

#ifdef CONFIG_MUXERS
AVOutputFormat rawvideo_muxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    NULL,
    "yuv,rgb",
    0,
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    NULL,
    raw_write_packet,
#ifdef __CW32__
    0,
    AVFMT_NOTIMESTAMPS,
#else
    .flags= AVFMT_NOTIMESTAMPS,
#endif
};
#endif //CONFIG_MUXERS

#ifdef CONFIG_ROQ_MUXER
AVOutputFormat roq_muxer =
{
    "RoQ",
    NULL_IF_CONFIG_SMALL("id RoQ format"),
    NULL,
    "roq",
    0,
    CODEC_ID_ROQ_DPCM,
    CODEC_ID_ROQ,
    roq_write_header,
    raw_write_packet,
};
#endif //CONFIG_ROQ_MUXER

AVInputFormat shorten_demuxer = {
    "shn",
    NULL_IF_CONFIG_SMALL("raw Shorten"),
    0,
    NULL,
    audio_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    AVFMT_GENERIC_INDEX,
    "shn",
    CODEC_ID_SHORTEN,
#else
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "shn",
    .value = CODEC_ID_SHORTEN,
#endif
};

AVInputFormat vc1_demuxer = {
    "vc1",
    NULL_IF_CONFIG_SMALL("raw VC-1"),
    0,
    NULL /* vc1_probe */,
    video_read_header,
    raw_read_partial_packet,
#ifdef __CW32__
    0,
    0,
    0,
    0,
    "vc1",
    CODEC_ID_VC1,
#else
    .extensions = "vc1",
    .value = CODEC_ID_VC1,
#endif
};

/* pcm formats */

#ifdef __CW32__
#define PCMINPUTDEF(name, long_name, ext, codec) \
AVInputFormat pcm_ ## name ## _demuxer = {\
    #name,\
    NULL_IF_CONFIG_SMALL(long_name),\
    0,\
    NULL,\
    raw_read_header,\
    raw_read_packet,\
    NULL,\
    pcm_read_seek,\
    0,\
    AVFMT_GENERIC_INDEX,\
    ext,\
    codec,\
};
#else
#define PCMINPUTDEF(name, long_name, ext, codec) \
AVInputFormat pcm_ ## name ## _demuxer = {\
    #name,\
    NULL_IF_CONFIG_SMALL(long_name),\
    0,\
    NULL,\
    raw_read_header,\
    raw_read_packet,\
    NULL,\
    pcm_read_seek,\
    .flags= AVFMT_GENERIC_INDEX,\
    .extensions = ext,\
    .value = codec,\
};
#endif

#ifdef __CW32__
#define PCMOUTPUTDEF(name, long_name, ext, codec) \
AVOutputFormat pcm_ ## name ## _muxer = {\
    #name,\
    NULL_IF_CONFIG_SMALL(long_name),\
    NULL,\
    ext,\
    0,\
    codec,\
    CODEC_ID_NONE,\
    NULL,\
    raw_write_packet,\
    0,\
    AVFMT_NOTIMESTAMPS,\
};
#else
#define PCMOUTPUTDEF(name, long_name, ext, codec) \
AVOutputFormat pcm_ ## name ## _muxer = {\
    #name,\
    NULL_IF_CONFIG_SMALL(long_name),\
    NULL,\
    ext,\
    0,\
    codec,\
    CODEC_ID_NONE,\
    NULL,\
    raw_write_packet,\
    .flags= AVFMT_NOTIMESTAMPS,\
};
#endif


#if !defined(CONFIG_MUXERS) && defined(CONFIG_DEMUXERS)
#define PCMDEF(name, long_name, ext, codec) \
        PCMINPUTDEF(name, long_name, ext, codec)
#elif defined(CONFIG_MUXERS) && !defined(CONFIG_DEMUXERS)
#define PCMDEF(name, long_name, ext, codec) \
        PCMOUTPUTDEF(name, long_name, ext, codec)
#elif defined(CONFIG_MUXERS) && defined(CONFIG_DEMUXERS)
#define PCMDEF(name, long_name, ext, codec) \
        PCMINPUTDEF(name, long_name, ext, codec)\
        PCMOUTPUTDEF(name, long_name, ext, codec)
#else
#define PCMDEF(name, long_name, ext, codec)
#endif

#ifdef WORDS_BIGENDIAN
#define BE_DEF(s) s
#define LE_DEF(s) NULL
#else
#define BE_DEF(s) NULL
#define LE_DEF(s) s
#endif


PCMDEF(s16be, "PCM signed 16 bit big-endian format",
       BE_DEF("sw"), CODEC_ID_PCM_S16BE)

PCMDEF(s16le, "PCM signed 16 bit little-endian format",
       LE_DEF("sw"), CODEC_ID_PCM_S16LE)

PCMDEF(s8, "PCM signed 8 bit format",
       "sb", CODEC_ID_PCM_S8)

PCMDEF(u16be, "PCM unsigned 16 bit big-endian format",
       BE_DEF("uw"), CODEC_ID_PCM_U16BE)

PCMDEF(u16le, "PCM unsigned 16 bit little-endian format",
       LE_DEF("uw"), CODEC_ID_PCM_U16LE)

PCMDEF(u8, "PCM unsigned 8 bit format",
       "ub", CODEC_ID_PCM_U8)

PCMDEF(alaw, "PCM A-law format",
       "al", CODEC_ID_PCM_ALAW)

PCMDEF(mulaw, "PCM mu-law format",
       "ul", CODEC_ID_PCM_MULAW)
