#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavcodec/avcodec.h>
#include <errno.h>

int main(int argc, char* argv[])
{
    AVFormatContext* fmt_ctx  = NULL;
    AVOutputFormat*  ofmt     = NULL;
    AVFormatContext* ofmt_ctx = NULL;
    int              ret;
    AVPacket         pkt;
    int              i;

    double start_seconds = atoi(argv[3]);
    double end_seconds = atoi(argv[4]);

    if (argc < 3)
    {
        fprintf(stderr, "usage:command infile outfile start end\n");
        return -1;
    }

    if (!argv[1] || !argv[2])
    {
        fprintf(stderr, "argument cannot be null\n");
        return -1;
    }

    char* src_file = argv[1];
    char* dst_file = argv[2];
    FILE* dst_fd   = NULL;

    av_log_set_level(AV_LOG_DEBUG);

    // open input file and get format context
    if ((ret = avformat_open_input(&fmt_ctx, src_file, NULL, NULL)) < 0)
    {
        /* av_strerror() */
        fprintf(stderr, "open input file error %s %d\n", src_file, errno);
        return -1;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    {
        fprintf(stderr, "open input file error %s %d\n", src_file, errno);
        return -1;
    }

    av_dump_format(fmt_ctx, 0, src_file, 0);

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, dst_file);

    ofmt = ofmt_ctx->oformat;
    for (i = 0; i < fmt_ctx->nb_streams; i++)
    {
        AVStream* in_stream  = fmt_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream)
        {
            fprintf(stderr, "failed allocating output stream\n");
            goto error;
        }
        avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    }
    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, dst_file, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            fprintf(stderr, "Could not open output file '%s'", dst_file);
            goto error;
        }
    }

    // write header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto error;
    }

    // goto the frame
    ret = av_seek_frame(fmt_ctx, -1, start_seconds * AV_TIME_BASE, AVSEEK_FLAG_ANY);

    int64_t* dts_start_from = (int64_t*)malloc(sizeof(int64_t) * fmt_ctx->nb_streams);
    memset(dts_start_from, 0, sizeof(int64_t) * fmt_ctx->nb_streams);

    int64_t* pts_start_from = (int64_t*)malloc(sizeof(int64_t) * fmt_ctx->nb_streams);
    memset(pts_start_from, 0, sizeof(int64_t) * fmt_ctx->nb_streams);

    pkt.data = NULL;
    pkt.size = 0;

    while (1)
    {
        AVStream *in_stream, *out_stream;

        // read packet
        ret = av_read_frame(fmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream  = fmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        // beyond the end time, break
        if (av_q2d(in_stream->time_base) * pkt.pts > end_seconds)
        {
            av_packet_unref(&pkt);
            break;
        }

        if (dts_start_from[pkt.stream_index] == 0)
        {
            dts_start_from[pkt.stream_index] = pkt.dts;
        }

        if (pts_start_from[pkt.stream_index] == 0)
        {
            pts_start_from[pkt.stream_index] = pkt.pts;
        }

        // time base change
        pkt.pts = av_rescale_q_rnd(pkt.pts - pts_start_from[pkt.stream_index], in_stream->time_base,
            out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts - dts_start_from[pkt.stream_index], in_stream->time_base,
            out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

        if (pkt.pts < 0)
            pkt.pts = 0;
        if (pkt.dts < 0)
            pkt.dts = 0;

        pkt.duration = (int64_t)av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos      = -1;

        if (pkt.pts < pkt.dts)
            continue;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            fprintf(stderr, "Error write packet\n");
            break;
        }

        av_packet_unref(&pkt);
    }
    free(pts_start_from);
    free(dts_start_from);

    av_write_trailer(ofmt_ctx);

error:
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_log(NULL, AV_LOG_INFO, "start free\n");
    avformat_close_input(&fmt_ctx);

    return 0;
}
