#include "tryplayer.h"
#include <stdio.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <argp.h>
#include <time.h>
#include <thread>
#include <mutex>

#if defined(__linux__)
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Widget.H>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/mathematics.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/parseutils.h>
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

AVFormatContext *pFormatCtx = nullptr;
AVCodec *pVideoCodec = nullptr;
AVStream *pVideoStream = nullptr;
int videoStream = -1;
int klvStream = -1;
AVCodecParameters *pvideoCodecparameters = nullptr;
AVCodecParameters *pklvCodecparameters = nullptr;
uint8_t *dst_data[4];
int src_linesize[4], dst_linesize[4];
double lastFrameTimeSeconds = 0;
time_t lastFrameWallclockTime = time(nullptr);
AVPacket *pPacket;
AVCodecContext *pCodecContext;
AVFrame *pFrame;
int response = 0;

int window_width;
int window_height;
char *outputWindow = nullptr;

//===================================================================


class VideoWidget : public Fl_Widget {
	void *data = nullptr;
	int widgetWidth, widgetHeight, lineWidth;
	std::mutex data_mutex;


public:
	VideoWidget(int x, int y, int w, int h, const char *label = nullptr) :
		Fl_Widget(x, y, w, h, label), data_mutex()  {
		widgetWidth = w;
		widgetHeight = h;
		lineWidth = w;
	}

	void draw() {
		if (data != nullptr) {
			std::lock_guard<std::mutex> guard(data_mutex);
			fl_draw_image((const unsigned char *)data, 0, 0, widgetWidth, widgetHeight, 3, lineWidth);
		}
	}

	void* getData() const {
		return data;
	}

	void setData(void *data, int lineWidth) {
		std::lock_guard<std::mutex> guard(data_mutex);
		this->data = realloc(this->data, lineWidth * widgetHeight * 3);
		memcpy(this->data, data, lineWidth * widgetHeight * 3);
		this->lineWidth = lineWidth;
		redraw();
	}
};

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

int videoThread(VideoWidget *window) {

    while (av_read_frame(pFormatCtx, pPacket) >= 0 && keepWorking) {

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

                double frameTimeSeconds = (double)pFrame->pts * (double)pVideoStream->time_base.num / (double)pVideoStream->time_base.den;

                time_t nextFrameWallclockTime = lastFrameWallclockTime + (time_t)floor((frameTimeSeconds - lastFrameTimeSeconds) * 1000);

                if (response >= 0) {
                    fprintf(stderr,
                            "Frame %dx%d %d (type=%c, size=%d bytes) pts %ld (%g) key_frame %d [DTS %d]\n",
                            pFrame->width,
                            pFrame->height,
                            pCodecContext->frame_number,
                            av_get_picture_type_char(pFrame->pict_type),
                            pFrame->pkt_size,
                            pFrame->pts,
                            frameTimeSeconds,
                            pFrame->key_frame,
                            pFrame->coded_picture_number
                    );

                    struct SwsContext *sws_ctx = sws_getContext(pFrame->width, pFrame->height,
                                                                pVideoStream->codec->pix_fmt,
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

                    window->setData(dst_data, dst_linesize[0]);

                    time_t currentTime = time(nullptr);

                    if (realTime && currentTime < nextFrameWallclockTime) {
                        time_t waitTime = nextFrameWallclockTime - currentTime;
                        struct timespec t = {waitTime / 1000, waitTime * 1000000};
                        nanosleep(&t, nullptr);
                    }

                    lastFrameWallclockTime = time(nullptr);
                    lastFrameTimeSeconds = frameTimeSeconds;
                    av_frame_unref(pFrame);
                    av_freep(&dst_data[0]);
                }
            }

        } else  if (pPacket->stream_index == klvStream) {
            fprintf(stderr, "Data packet\n");

            int lineCounter = 0;
            int amount;
            char buffer[123];
            uint8_t *byte = pPacket->buf->data;

            for (amount = pPacket->buf->size; amount > 0;  byte += 16, amount -= 16) {

                int amountThisTime = std::min(16, amount);
                hexData(byte, reinterpret_cast<uint8_t *>(buffer), amountThisTime);
                fprintf(stderr, "%s\n", buffer);
            }
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
}


// ========================================================

int main ( int argc, char *argv[] ) {
    av_register_all();
    avformat_network_init();
    
    struct argp args = {options, parse_opt, nullptr, nullptr, nullptr, nullptr, nullptr};
    argp_parse(&args, argc, argv, 0, 0, 0);
    
    fileName = source;

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
        AVStream *stream = pFormatCtx->streams[iStream];

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
            pVideoStream = stream;
            printf("\tCodec for stream %d: %s ID %d bit_rate %ld, width %d, height %d\n", 
			iStream, pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate,
							pvideoCodecparameters->width, pvideoCodecparameters->height);
        }
    }

    if (videoStream == -1) {
        fprintf(stderr, "Couldn't find a video stream\n");
        return -1;
    }

    pCodecContext = avcodec_alloc_context3(pVideoCodec);

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


    pFrame = av_frame_alloc();
    if (!pFrame) {
        fprintf(stderr, "failed to allocated memory for AVFrame\n");
        return -1;
    }

    pPacket = av_packet_alloc();
    if (!pPacket) {
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

    } else {
		printf("Open new window\n");
        window_width = pvideoCodecparameters->width;
        window_height = pvideoCodecparameters->height;

        Fl_Window *window = new Fl_Window(window_width, window_height);
        window->label((std::string("KLV Video player - ") + fileName).c_str());

        VideoWidget *widget =  new VideoWidget(0, 0, window_width, window_height);

        window->end();
        window->show(argc, argv);
        std::thread workThread = std::thread(videoThread, widget);

        return Fl::run();
    }


    return 0;
}


