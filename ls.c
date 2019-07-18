#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/log.h>

int main(int argc, char* argv[])
{
    int ret;

    AVIODirContext* ctx   = NULL;
    AVIODirEntry*   entry = NULL;

    if (argc < 2)
    {
        av_log(NULL, AV_LOG_ERROR, "wrong arguments\n");
        return -1;
    }

    if ((ret = avio_open_dir(&ctx, argv[1], NULL)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "cannot open directory %s\n", av_err2str(ret));
        goto error;
    }
    for (;;)
    {
        if ((ret = avio_read_dir(ctx, &entry)) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "cannot open directory %s\n", av_err2str(ret));
            goto error;
        }
        if (!entry) // read complete
            break;
        av_log(NULL, AV_LOG_INFO, "%s %ld\n", entry->name, entry->size);
        avio_free_directory_entry(&entry);
    }

    av_log_set_level(AV_LOG_DEBUG);

error:
    avio_close_dir(&ctx);

    return 0;
}
