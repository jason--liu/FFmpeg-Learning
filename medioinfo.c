#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <errno.h>

int main(int argc, char* argv[])
{
    AVFormatContext* fmt_ctx = NULL;
    int              ret;

    if (argc < 2)
    {
        perror("too few arguments\n");
        return -1;
    }

    // this api is deprecated
    /* av_register_all(); */

    if ((ret = avformat_open_input(&fmt_ctx, argv[1], NULL, NULL)) < 0)
    {
        /* av_strerror() */
        fprintf(stderr, "open input file error %s %d\n", argv[1], errno);
        return -1;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    {
        fprintf(stderr, "open input file error %s %d\n", argv[1], errno);
        return -1;
    }

    av_dump_format(fmt_ctx, 0, argv[1], 0);

    avformat_close_input(&fmt_ctx);

    return 0;
}
