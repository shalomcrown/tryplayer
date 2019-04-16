#include "tryplayer.h"
#include <stdio.h>
#include <string>
#include <iostream>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/mathematics.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/parseutils.h>
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_thread.h>
}

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame);

// Packages....
//
// sudo apt-get install libsdl2-dev libavcodec-dev libavdevice-dev libavfilter-dev \
libavformat-dev libavresample-dev libavutil-dev libswresample-dev libswscale-dev \
libpostproc-dev libass-dev libsdl-kitchensink-dev libsdl2-gfx-dev

int main ( int argc, char *argv[] ) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
    av_register_all();
    const char *fileName = "/home/shalomc/outputs/KLV/1_2019-04-15_20-05-21S.ts";

    AVFormatContext *pFormatCtx = nullptr;
    AVCodec *pVideoCodec = NULL;
    int videoStream = -1;
    int klvStream = -1;
    AVCodecParameters *pvideoCodecparameters = nullptr;
    AVCodecParameters *pklvCodecparameters = nullptr;
    uint8_t *dst_data[4];
    int src_linesize[4], dst_linesize[4];
    SDL_Window *win;

    if (avformat_open_input(&pFormatCtx, fileName, nullptr, nullptr) != 0) {

        fprintf(stderr, "Couldn't open file\n");
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        fprintf(stderr, "Couldn't find stream information\n");
        return -1;
    }

    av_dump_format(pFormatCtx, 0, fileName, 0);

    for(int iStream = 0; iStream < pFormatCtx->nb_streams; iStream++) {
        AVCodec *pLocalCodec = nullptr;
        AVCodecParameters *pLocalCodecParameters = pFormatCtx->streams[iStream]->codecpar;
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

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
            pVideoCodec = pLocalCodec;
            pvideoCodecparameters = pLocalCodecParameters;
            printf("\tCodec for stream %d: %s ID %d bit_rate %ld\n", iStream, pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
        }
    }

    if (videoStream == -1) {
        fprintf(stderr, "Couldn't find a video stream\n");
        return -1;
    }

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pVideoCodec);

    if (!pCodecContext) {
        fprintf(stderr, "failed to allocated memory for AVCodecContext\n");
        return -1;
    }

    if (avcodec_parameters_to_context(pCodecContext, pvideoCodecparameters) < 0) {
        fprintf(stderr, "failed to copy codec params to codec context\n");
        return -1;
    }

    if (avcodec_open2(pCodecContext, pVideoCodec, NULL) < 0) {
        fprintf(stderr, "failed to open codec through avcodec_open2\n");
        return -1;
    }


    AVFrame *pFrame = av_frame_alloc();
    if (!pFrame) {
        fprintf(stderr, "failed to allocated memory for AVFrame\n");
        return -1;
    }

    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket) {
        fprintf(stderr, "failed to allocated memory for AVPacket\n");
        return -1;
    }

    int response = 0;


    int window_width = pvideoCodecparameters->width;
    int window_height = pvideoCodecparameters->height;

    win = SDL_CreateWindow(fileName, 100, 100, window_width, window_height, SDL_WINDOW_SHOWN);

    if (win == nullptr) {
        fprintf(stderr, "Coudln't open window\n");
        SDL_Quit();
        return -1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (ren == nullptr){
        SDL_DestroyWindow(win);
        std::cout << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Texture *texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             window_width, window_height);

    if (! texture) {
        fprintf(stderr, "Failed to create texture\n");
        return -1;
    }

    SDL_SetRenderDrawColor(ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderPresent(ren);
    SDL_RenderClear(ren);

    while (av_read_frame(pFormatCtx, pPacket) >= 0) {

        // if it's the video stream
        if (pPacket->stream_index == videoStream) {
            if (avcodec_send_packet(pCodecContext, pPacket) < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder:\n %d", response);
                return -1;
            }

            for (int response = 0; response >= 0; ) {
                response = avcodec_receive_frame(pCodecContext, pFrame);

                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                } else if (response < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder: %d\n", response);
                    return -1;
                }

                if (response >= 0) {
                    fprintf(stderr,
                            "Frame %dx%d %d (type=%c, size=%d bytes) pts %ld key_frame %d [DTS %d]\n",
                            pFrame->width,
                            pFrame->height,
                            pCodecContext->frame_number,
                            av_get_picture_type_char(pFrame->pict_type),
                            pFrame->pkt_size,
                            pFrame->pts,
                            pFrame->key_frame,
                            pFrame->coded_picture_number
                    );


                    struct SwsContext *sws_ctx = sws_getContext(pFrame->width, pFrame->height,
                                                                AV_PIX_FMT_YUV420P,
                                                                window_width, window_height,
                                                                AV_PIX_FMT_YUV420P,
                                                                SWS_BILINEAR, NULL, NULL, NULL );

                    /* buffer is going to be written to rawvideo file, no alignment */
                    if (av_image_alloc(dst_data, dst_linesize, window_width, window_height, AV_PIX_FMT_YUV420P, 32) < 0) {
                        fprintf(stderr, "Could not allocate destination image\n");
                        return -1;
                    }

                    sws_scale(sws_ctx, pFrame->data,
                              pFrame->linesize, 0, pFrame->height, dst_data, dst_linesize);

                    int w, h;
                    SDL_QueryTexture(texture, NULL, NULL, &w, &h);

                    SDL_UpdateYUVTexture(texture, NULL,
                                         dst_data[0],
                                         dst_linesize[0],
                                         dst_data[1],
                                         dst_linesize[1],
                                         dst_data[2],
                                         dst_linesize[2]
                                        );

                    SDL_RenderCopy(ren, texture, NULL, NULL);
                    SDL_RenderPresent(ren);
                    SDL_UpdateWindowSurface(win);
                    SDL_RenderClear(ren);
                    av_frame_unref(pFrame);
                    av_freep(&dst_data[0]);
                    //sws_freeContext(sws_ctx);
                }
            }

        } else  if (pPacket->stream_index == klvStream) {
            fprintf(stderr, "Data packet\n");
        } else {
            fprintf(stderr, "Unknown packet\n");
        }

        av_packet_unref(pPacket);
    }


    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);
    SDL_Quit();
    return 0;
}

//==========================================================

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame,
                                                        struct SwsContext *sws_ctx, uint8_t *src_data) {
    int response = avcodec_send_packet(pCodecContext, pPacket);

    if (response < 0) {
        fprintf(stderr, "Error while sending a packet to the decoder:\n %d", response);
        return response;
    }

    while (response >= 0) {
        response = avcodec_receive_frame(pCodecContext, pFrame);

        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            fprintf(stderr, "Error while receiving a frame from the decoder: %d", response);
            return response;
        }

        if (response >= 0) {
            fprintf(stderr,
                "Frame %d (type=%c, size=%d bytes) pts %ld key_frame %d [DTS %d]\n",
                    pCodecContext->frame_number,
                    av_get_picture_type_char(pFrame->pict_type),
                    pFrame->pkt_size,
                    pFrame->pts,
                    pFrame->key_frame,
                    pFrame->coded_picture_number
            );

            //save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);
            av_frame_unref(pFrame);
        }
    }
    return 0;
}


