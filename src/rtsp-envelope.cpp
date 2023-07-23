//
// Created by shalomc on 20/07/23.
//

#include "tryplayer.h"
#include <stdio.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <argp.h>
#include <time.h>
#include <thread>
#include <mutex>
#include <stdlib.h>
#include <spawn.h>
#include <csignal>
#include <error.h>


#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Widget.H>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/timestamp.h>
}


extern void hexData(uint8_t *data, uint8_t *output, int length);

const char *argp_program_version = "tryplayer 1.0";
const char *argp_program_bug_address = "<shalomcrown@gmail.com>";

char errorBuffer[AV_ERROR_MAX_STRING_SIZE];

struct SwsContext *sws_ctx = nullptr;

bool realTime = false;
char *source = nullptr;
char *dest = nullptr;
const char *fileName = source;
volatile bool keepWorking = true;

AVFormatContext *pInputFormatCtx = nullptr;
AVCodec *pInputVideoCodec = nullptr;
AVStream *pInputVideoStream = nullptr;
int videoStream = -1;
int klvStream = -1;
AVCodecParameters *pInputVideoCodecparameters = nullptr;
AVCodecParameters *pklvCodecparameters = nullptr;
uint8_t *dst_data[4];
int src_linesize[4], dst_linesize[4];
double lastFrameTimeSeconds = 0;
time_t lastFrameWallclockTime = time(nullptr);
AVPacket *pInputPacket;
AVCodecContext *pInputCodecContext;
AVFrame *pInputFrame = nullptr;
AVFrame *pOutputFrame = nullptr;
int response = 0;


int framerate = 30;

AVCodecContext *pOutputCodecContext;
AVCodec *pOutputVideoCodec = nullptr;
AVFormatContext *pOutputFormatCtx = nullptr;
AVStream *pOutputVideoStream = nullptr;
AVCodecParameters *pOutputVideoCodecParameters = nullptr;
AVPacket *pOutputPacket;

bool transcode = true;
pid_t ffmpegGeneratorPid = 0;
pid_t ffPlayPid = 0;


int window_width = 1280;
int window_height = 960;
char *outputWindow = nullptr;

// =====================================================

const struct argp_option options[] = {
        {"real-time", 'r', 0, 0, "Run video file at natural speed", 0},
        {"window-name", 'n', "WINDOW_NAME", 0, "Existing window name to play in", 0},
        {0}
};

// =====================================================

static error_t parse_opt (int key, char *arg, struct argp_state *state) {
    switch(key) {
        case 'r':
            realTime = true;
            break;

        case 'n':
            outputWindow = arg;
            break;

        case ARGP_KEY_ARG:
            if (source == nullptr) {
                source = strdup(arg);
                printf("Play source %s\n", source);
            } else {
                dest = strdup(arg);
                printf("Destination %s\n", dest);
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;

        case ARGP_KEY_END:
            if (state->arg_num <  1) {
                argp_usage (state);
            }
            break;
    }

    return 0;
}
// ========================================================================

void printError(const char *formatString, int errnum, ...) {
    va_list args;
    va_start(args, errnum);

    av_make_error_string(errorBuffer, AV_ERROR_MAX_STRING_SIZE, errnum);
    vfprintf(stderr, formatString, args);
    fprintf(stderr, " %d %s\n", errnum, errorBuffer);
}

// ========================================================================

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag){
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
// ========================================================================

void signalHandler(int signum) {
    kill(ffmpegGeneratorPid, SIGKILL);
}

// ========================================================================

int dealWithTranscodedInpuPacket() {
    while (av_read_frame(pInputFormatCtx, pInputPacket) >= 0 && keepWorking) {

        // if it's the video stream
        if (pInputPacket->stream_index == videoStream) {
            if (avcodec_send_packet(pInputCodecContext, pInputPacket) < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder:\n %d", response);
                return -1;
            }

            for (int response = 0; response >= 0;) {
                response = avcodec_receive_frame(pInputCodecContext, pInputFrame);

                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                } else if (response < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder: %d\n", response);
                    return -1;
                }

                double frameTimeSeconds = (double) pInputFrame->pts * (double) pInputVideoStream->time_base.num /
                                          (double) pInputVideoStream->time_base.den;

                time_t nextFrameWallclockTime =
                        lastFrameWallclockTime + (time_t) floor((frameTimeSeconds - lastFrameTimeSeconds) * 1000);

                if (response >= 0) {
                    fprintf(stderr,
                            "Frame %dx%d %d (type=%c, size=%d bytes) pts %ld (%g) key_frame %d [DTS %d]\n",
                            pInputFrame->width,
                            pInputFrame->height,
                            pInputCodecContext->frame_number,
                            av_get_picture_type_char(pInputFrame->pict_type),
                            pInputFrame->pkt_size,
                            pInputFrame->pts,
                            frameTimeSeconds,
                            pInputFrame->key_frame,
                            pInputFrame->coded_picture_number
                    );

                    // ========================================================================

                    if (pOutputFormatCtx == nullptr) {
                        printf("Try to open output context\n");
                        if ((response = avformat_alloc_output_context2(&pOutputFormatCtx, nullptr, "rtsp", (const char *)dest)) < 0) {
                            printError("Couldn't open output context", response);
                            return -1;
                        }

                        pOutputVideoCodec = avcodec_find_encoder_by_name("libx265");

                        if (pOutputVideoCodec == nullptr) {
                            fprintf(stderr, "Couldn't open video codec\n");
                            return -1;
                        }

                        pOutputVideoStream = avformat_new_stream(pOutputFormatCtx, pOutputVideoCodec);

                        if (pOutputVideoStream == nullptr) {
                            fprintf(stderr, "Couldn't open video stream\n");
                            return -1;
                        }

                        pOutputCodecContext = avcodec_alloc_context3(pOutputVideoCodec);

                        if (pOutputCodecContext == nullptr) {
                            fprintf(stderr, "Couldn't allocate video codec context\n");
                            return -1;
                        }

                        pOutputCodecContext->bit_rate = 2500000;
                        pOutputCodecContext->time_base = av_make_q(1, (int)framerate);
                        pOutputCodecContext->framerate= av_make_q(1, (int)framerate);
                        pOutputCodecContext->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;
                        pOutputCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
                        pOutputCodecContext->width = pInputFrame->width;
                        pOutputCodecContext->height = pInputFrame->height;
                        pOutputCodecContext->max_b_frames = 0;

                        av_opt_set(pOutputCodecContext->priv_data, "preset", "ultrafast", 0);

                        if ((response = avcodec_open2(pOutputCodecContext, pOutputVideoCodec, nullptr)) < 0) {
                            printError("Couldn't open output context", response);
                            return -1;
                        }

                        if ((response = avcodec_parameters_from_context(pOutputVideoStream->codecpar, pOutputCodecContext)) < 0) {
                            printError("Couldn't copy codec paramters", response);
                            return -1;
                        }

                        if ((response = avformat_write_header(pOutputFormatCtx, (AVDictionary**)nullptr)) < 0) {
                            printError("Couldn't  write header to output", response);
                            return -1;
                        }

                        pOutputFrame->width = pInputFrame->width;
                        pOutputFrame->height = pInputFrame->height;
                        pOutputFrame->format = pOutputCodecContext->pix_fmt;

                        if ((response = av_image_alloc(pOutputFrame->data, pOutputFrame->linesize, pInputFrame->width, pInputFrame->height, pOutputCodecContext->pix_fmt, 32)) < 0) {
                            printError("Couldn't  allocate output frame", response);
                            return -1;
                        }
                    }

                    // ========================================================================

                    if (pOutputCodecContext->pix_fmt != pInputCodecContext->pix_fmt) {
                        sws_ctx = sws_getCachedContext(sws_ctx,
                                                       pInputFrame->width, pInputFrame->height,
                                                       pInputCodecContext->pix_fmt,
                                                       pOutputFrame->width, pOutputFrame->height,
                                                       pOutputCodecContext->pix_fmt,
                                                       SWS_BILINEAR, NULL, NULL, NULL);

                        if (sws_ctx == nullptr) {
                            fprintf(stderr, "Couldn't get SWS context");
                            return -1;
                        }

                        response = sws_scale(sws_ctx, pInputFrame->data, pInputFrame->linesize, 0, pInputFrame->height,
                                             pOutputFrame->data, pOutputFrame->linesize);

                        if (response < 0) {
                            printError("Couldn't convert pixel format", response);
                            return -1;
                        }

                        pOutputFrame->pts = pInputFrame->pts;

                    } else {
                        av_frame_copy(pOutputFrame, pInputFrame);
                    }

                    if ((response = avcodec_send_frame(pOutputCodecContext, pOutputFrame)) < 0) {
                        printError("Couldn't send frame", response);
                        return -1;
                    }

                    while (true) {
                        response = avcodec_receive_packet(pOutputCodecContext, pOutputPacket);

                        if (response == AVERROR(EAGAIN)) {
                            break;
                        }

                        if (response < 0) {
                            printError("Couldn't get packet", response);
                            return -1;
                        }

//                        pOutputPacket->dts = pOutputFrame->pts; //pInputPacket->dts;
                        pOutputPacket->stream_index = pInputVideoStream->index;

                        if ((response = av_write_frame(pOutputFormatCtx, pOutputPacket)) < 0) {
                            printError("Couldn't write frame", response);
                            return -1;
                        }
                    }

                    // ========================================================================

                    av_frame_unref(pInputFrame);
                }
            }

        } else if (pInputPacket->stream_index == klvStream) {
            fprintf(stderr, "Data packet\n");

            int lineCounter = 0;
            int amount;
            char buffer[123];
            uint8_t *byte = pInputPacket->buf->data;

            for (amount = pInputPacket->buf->size; amount > 0; byte += 16, amount -= 16) {

                int amountThisTime = std::min(16, amount);
                hexData(byte, reinterpret_cast<uint8_t *>(buffer), amountThisTime);
                fprintf(stderr, "%s\n", buffer);
            }
        } else {
            fprintf(stderr, "Unknown packet\n");
        }

        av_packet_unref(pInputPacket);
    }
}

// ========================================================

int main ( int argc, char *argv[], char **envp) {
    signal(SIGTERM, signalHandler);

    avformat_network_init();
    avdevice_register_all();

    struct argp args = {options, parse_opt, nullptr, nullptr, nullptr, nullptr, nullptr};
    argp_parse(&args, argc, argv, 0, 0, 0);

    fileName = source;

    if (strcmp(source, "testcard") == 0) {
        printf("Start source generator\n");
        fileName = "udp://localhost:3333";

        char *const ffmpegArgs[] = {"-fflags=nobuffer", "-re", "-f", "lavfi", "-i", "testsrc=size=1280x720:rate=15",
                                    "-vcodec", "libx264", "-x264-params", "keyint=20:scenecut=0", "-b:v", "500k", "-f", "mpegts", "udp://localhost:3333",
                                    nullptr};

        if ((response = posix_spawnp(&ffmpegGeneratorPid, "ffmpeg", nullptr, nullptr, ffmpegArgs, envp)) != 0) {
            error(-1, response, "Failed to start source video");
        }
    }

    if ((response = avformat_open_input(&pInputFormatCtx, fileName, nullptr, nullptr)) != 0) {
        printError("Couldn't open file\n", response);
        return -1;
    }

    if ((response - avformat_find_stream_info(pInputFormatCtx, nullptr)) < 0) {
        printError("Couldn't find stream information\n", response);
        return -1;
    }

    av_dump_format(pInputFormatCtx, 0, fileName, 0);

    for(int iStream = 0; iStream < pInputFormatCtx->nb_streams; iStream++) {
        AVCodec *pLocalCodec = nullptr;
        AVCodecParameters *pLocalCodecParameters = pInputFormatCtx->streams[iStream]->codecpar;
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
        AVStream *stream = pInputFormatCtx->streams[iStream];

        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_DATA) {
            klvStream = iStream;
            pklvCodecparameters = pLocalCodecParameters;
            printf("\tKLV stream found at %d\n", iStream);
        }

        if (pLocalCodec == nullptr) {
            fprintf(stderr, "No codec for stream %d\n", iStream);
            continue;
        }

        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = iStream;
            pInputVideoCodec = pLocalCodec;
            pInputVideoCodecparameters = pLocalCodecParameters;
            pInputVideoStream = stream;
            printf("\tCodec for stream %d: %s ID %d bit_rate %ld, width %d, height %d\n",
                   iStream, pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate,
                   pInputVideoCodecparameters->width, pInputVideoCodecparameters->height);
        }
    }

    if (videoStream == -1) {
        fprintf(stderr, "Couldn't find a video stream\n");
        return -1;
    }

    pInputCodecContext = avcodec_alloc_context3(pInputVideoCodec);

    if (!pInputCodecContext) {
        fprintf(stderr, "failed to allocated memory for AVCodecContext\n");
        return -1;
    }

    if (avcodec_parameters_to_context(pInputCodecContext, pInputVideoCodecparameters) < 0) {
        fprintf(stderr, "failed to copy codec params to codec context\n");
        return -1;
    }

    if (avcodec_open2(pInputCodecContext, pInputVideoCodec, NULL) < 0) {
        fprintf(stderr, "failed to open codec through avcodec_open2\n");
        return -1;
    }

    pInputFrame = av_frame_alloc();
    if (!pInputFrame) {
        fprintf(stderr, "failed to allocated memory for AVFrame\n");
        return -1;
    }

    pInputPacket = av_packet_alloc();
    if (pInputPacket == nullptr) {
        fprintf(stderr, "failed to allocated memory for input AVPacket\n");
        return -1;
    }

    pOutputPacket = av_packet_alloc();
    if (pInputPacket == nullptr) {
        fprintf(stderr, "failed to allocated memory for output AVPacket\n");
        return -1;
    }


    response = 0;
    av_dump_format(pInputFormatCtx, 0, fileName, 0);

    pOutputFrame = av_frame_alloc();

    const char* inputCodecName = pInputCodecContext->codec->name;
    const char* names[] = {"libx265", "hevc", "libx264", "h264", "h265"};

    for (const char* name: names) {
        if (strcmp(name, inputCodecName) == 0) {
            transcode = false;
            break;
        }
    }

    if (transcode == false) {
        if (pOutputFormatCtx == nullptr) {
            printf("Try to open output context (not transcoding)\n");
            if ((response = avformat_alloc_output_context2(&pOutputFormatCtx, nullptr, "rtsp", (const char *) dest)) <
                0) {
                printError("Couldn't open output context", response);
                return -1;
            }

            av_opt_set(pOutputFormatCtx, "genpts", "1", 0);

            pOutputVideoStream = avformat_new_stream(pOutputFormatCtx, nullptr);

            if (pOutputVideoStream == nullptr) {
                fprintf(stderr, "Couldn't open video stream\n");
                return -1;
            }

            avcodec_parameters_copy(pOutputVideoStream->codecpar, pInputVideoStream->codecpar);
            pOutputVideoStream->codecpar->codec_tag = 0;

            if (pOutputVideoStream->codecpar->width == 0) {
                pOutputVideoStream->codecpar->width = pInputVideoStream->codecpar->width;
                pOutputVideoStream->codecpar->height = pInputVideoStream->codecpar->height;
            }

            pOutputVideoStream->time_base = pInputVideoStream->time_base;
            pOutputVideoStream->avg_frame_rate = pInputVideoStream->avg_frame_rate;

            pOutputVideoStream->id = pInputVideoStream->id;
            pOutputFormatCtx->oformat->video_codec = pInputVideoStream->codecpar->codec_id;

            if ((response = avformat_write_header(pOutputFormatCtx, (AVDictionary **) nullptr)) < 0) {
                printError("Couldn't  write header to output", response);
                return -1;
            }
        }

        while (av_read_frame(pInputFormatCtx, pInputPacket) >= 0 && keepWorking) {
            log_packet(pInputFormatCtx, pInputPacket, "in");
            if ((response = av_write_frame(pOutputFormatCtx, pInputPacket)) < 0) {
                printError("Couldn't write frame", response);
                return -1;
            }
        }

    } else {
            response = dealWithTranscodedInpuPacket();

        if (response != 0) {
            return response;
        }
    }


//    if (outputWindow != nullptr) {
//        printf("Using output window\n");
//        long windowHandle = getWindowId(outputWindow);
//
//        if (windowHandle == -1) {
//            fprintf(stderr, "Couldn't find window with name %s\n", outputWindow);
//            return -1;
//        }
//
//    } else {
//        printf("Open new window\n");
//        window_width = pvideoCodecparameters->width;
//        window_height = pvideoCodecparameters->height;
//
//        Fl::visual(FL_RGB);
//        Fl::lock();
//        Fl_Window *window = new Fl_Window(window_width, window_height);
//        window->label((std::string("KLV Video player - ") + fileName).c_str());
//
//        VideoWidget *widget =  new VideoWidget(0, 0, window_width, window_height);
//
//        window->end();
//        window->show(argc, argv);
//        std::thread workThread = std::thread(videoThread, widget);
//
//        return Fl::run();
//    }


    return 0;
}
