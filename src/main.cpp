#include "tryplayer.h"
#include <stdio.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <argp.h>
#include <time.h>
#if defined(__linux__)
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
#endif

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

// Packages....
//
// sudo apt-get install libsdl2-dev libavcodec-dev libavdevice-dev libavfilter-dev \
libavformat-dev libavresample-dev libavutil-dev libswresample-dev libswscale-dev \
libpostproc-dev libass-dev libsdl-kitchensink-dev libsdl2-gfx-dev


// ffmpeg -fflags nobuffer -f v4l2 -video_size 640x480  -i /dev/video0 udp://localhost:1234


// Example file: http://samples.ffmpeg.org/MPEG2/mpegts-klv/Day%20Flight.mpg

const char *argp_program_version = "tryplayer 1.0";
const char *argp_program_bug_address = "<shalomcrown@gmail.com>";

bool realTime = false;
char *source = nullptr;
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
SDL_Window *win;
double lastFrameTimeSeconds = 0;
time_t lastFrameWallclockTime = time(nullptr);
AVPacket *pInputPacket;
AVCodecContext *pInputCodecContext;
AVFrame *pInputFrame;
int response = 0;

int window_width;
int window_height;
SDL_Renderer *ren;
SDL_Texture *texture;
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
            source = arg;
	    printf("Play source %s\n", source);
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

// =====================================================

uint8_t nybbleToHex(uint8_t nybble) {
    return nybble > 9 ? nybble + 0x37 : nybble + 0x30;
}

void hexData(uint8_t *data, uint8_t *output, int length)  {
    int x = 0;
    for (int y = 0; y < length; ++y, ++x)  {
        output[x] = nybbleToHex(data[y] >> 4 & 0xF);
        output[++x] = nybbleToHex(data[y] & 0xF);
        output[++x] = 0x20;
    }

    output[x] = 0;
}

// -----------------------------------------------------------------

#if defined(__linux__)
    long getWindowId(const char *name) {
        unsigned long nWIndowIds;
        int form;
        unsigned long remain;
        Display *disp = XOpenDisplay(NULL);
        Window *list;
        Atom prop = XInternAtom(disp, "_NET_CLIENT_LIST", False);
        Atom type;
        long windowHandle = -1;

        if (!disp) {
            fprintf(stderr, "no display!\n");
            return -1;
        }

        if (XGetWindowProperty(disp, XDefaultRootWindow(disp), prop, 0, 1024, False, XA_WINDOW,
                                                        &type, &form, &nWIndowIds, &remain, (unsigned char **)&list) != Success) {
            perror("winlist() -- GetWinProp");
            return -1;
            }

        for (int i = 0; i < (int)nWIndowIds; i++) {
            prop = XInternAtom(disp,"WM_NAME",False), type;
            unsigned char *windowName;
            unsigned long len;

            if (XGetWindowProperty(disp, list[i], prop, 0, 1024, False, XA_STRING,
                                                    &type, &form, &len, &remain, &windowName) == Success) {

                if (windowName != nullptr) {
                    if (strcmp((const char *)windowName, name) == 0) {
                        XFree(windowName);
                        windowHandle = list[i];
                        break;
                    } else {
                        XFree(windowName);
                    }
                }
            }
        }

        XFree(list);
        XCloseDisplay(disp);
        return windowHandle;
    }
#else
    long getWindowId(const char *name) {
        return -1;
    }
#endif

// -----------------------------------------------------------------

int sdlVideoThread(void *) {

    while (av_read_frame(pInputFormatCtx, pInputPacket) >= 0 && keepWorking) {

        // if it's the video stream
        if (pInputPacket->stream_index == videoStream) {
            if (avcodec_send_packet(pInputCodecContext, pInputPacket) < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder:\n %d", response);
                return -1;
            }

            for (int response = 0; response >= 0; ) {
                response = avcodec_receive_frame(pInputCodecContext, pInputFrame);

                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                } else if (response < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder: %d\n", response);
                    return -1;
                }

                double frameTimeSeconds = (double)pInputFrame->pts * (double)pInputVideoStream->time_base.num / (double)pInputVideoStream->time_base.den;

                time_t nextFrameWallclockTime = lastFrameWallclockTime + (time_t)floor((frameTimeSeconds - lastFrameTimeSeconds) * 1000);

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

                    struct SwsContext *sws_ctx = sws_getContext(pInputFrame->width, pInputFrame->height,
                                                                pInputVideoStream->codec->pix_fmt,
                                                                window_width, window_height,
                                                                AV_PIX_FMT_YUV420P,
                                                                SWS_BILINEAR, NULL, NULL, NULL );

                    /* buffer is going to be written to rawvideo file, no alignment */
                    if (av_image_alloc(dst_data, dst_linesize, window_width, window_height, AV_PIX_FMT_YUV420P, 32) < 0) {
                        fprintf(stderr, "Could not allocate destination image\n");
                        return -1;
                    }

                    sws_scale(sws_ctx, pInputFrame->data,
                              pInputFrame->linesize, 0, pInputFrame->height, dst_data, dst_linesize);

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

                    time_t currentTime = time(nullptr);

                    if (realTime && currentTime < nextFrameWallclockTime) {
                        time_t waitTime = nextFrameWallclockTime - currentTime;
                        struct timespec t = {waitTime / 1000, waitTime * 1000000};
                        nanosleep(&t, nullptr);
                    }

                    lastFrameWallclockTime = time(nullptr);
                    lastFrameTimeSeconds = frameTimeSeconds;
                    SDL_RenderPresent(ren);
                    SDL_UpdateWindowSurface(win);
                    SDL_RenderClear(ren);
                    av_frame_unref(pInputFrame);
                    av_freep(&dst_data[0]);
                    //sws_freeContext(sws_ctx);
                }
            }

        } else  if (pInputPacket->stream_index == klvStream) {
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


    avformat_close_input(&pInputFormatCtx);
    avformat_free_context(pInputFormatCtx);
    av_packet_free(&pInputPacket);
    av_frame_free(&pInputFrame);
    avcodec_free_context(&pInputCodecContext);
    SDL_Quit();
}


// ========================================================

int main ( int argc, char *argv[] ) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
    av_register_all();
    avformat_network_init();

    struct argp args = {options, parse_opt, nullptr, nullptr, nullptr, nullptr, nullptr};
    argp_parse(&args, argc, argv, 0, 0, 0);

    fileName = source;

    if (avformat_open_input(&pInputFormatCtx, fileName, nullptr, nullptr) != 0) {

        fprintf(stderr, "Couldn't open file\n");
        return -1;
    }

    if (avformat_find_stream_info(pInputFormatCtx, nullptr) < 0) {
        fprintf(stderr, "Couldn't find stream information\n");
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
    if (!pInputPacket) {
        fprintf(stderr, "failed to allocated memory for AVPacket\n");
        return -1;
    }

    response = 0;

    if (outputWindow != nullptr) {
        printf("Using output window\n");
    	long windowHandle = getWindowId(outputWindow);

    	if (windowHandle == -1) {
            fprintf(stderr, "Couldn't find window with name %s\n", outputWindow);
            return -1;
    	}

    	win = SDL_CreateWindowFrom((const void *)windowHandle);

    	if (win != nullptr) {
    		SDL_GetWindowSize(win, &window_width, &window_height);
    	}

    } else {
		printf("Open new window\n");
        window_width = pInputVideoCodecparameters->width;
        window_height = pInputVideoCodecparameters->height;

        win = SDL_CreateWindow(fileName, 100, 100, window_width, window_height, SDL_WINDOW_SHOWN);
    }

    if (win == nullptr) {
        fprintf(stderr, "Coudln't open window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (ren == nullptr){
        SDL_DestroyWindow(win);
        std::cout << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             window_width, window_height);

    if (! texture) {
        fprintf(stderr, "Failed to create texture\n");
        return -1;
    }

    SDL_SetRenderDrawColor(ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderPresent(ren);
    SDL_RenderClear(ren);

    SDL_Thread *thread = SDL_CreateThread(sdlVideoThread, "Video thread", nullptr);

    SDL_Event event;

    while (SDL_QuitRequested() == false) {
        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_WINDOWEVENT) {
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:
                        keepWorking = false;
                        SDL_Quit();
                        exit(0);
                        break;
                }
            }
        }
    }

    SDL_Quit();
    return 0;
}
