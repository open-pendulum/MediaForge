#include "transcoder.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#define NOMINMAX
#include <windows.h>

extern "C" {
#include <libavutil/opt.h>
}

// Helper to convert UTF-8 to Wide String
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to get Short Path (8.3) for FFmpeg compatibility on Windows
static std::string GetShortPath(const std::string& utf8Path) {
    std::wstring widePath = Utf8ToWide(utf8Path);
    long length = GetShortPathNameW(widePath.c_str(), NULL, 0);
    if (length == 0) return utf8Path; // Failed, return original

    std::vector<wchar_t> buffer(length);
    GetShortPathNameW(widePath.c_str(), &buffer[0], length);
    
    std::wstring shortWide(buffer.begin(), buffer.end() - 1); // Remove null terminator
    
    // Convert back to UTF-8 (which will be ASCII for short path)
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &shortWide[0], (int)shortWide.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &shortWide[0], (int)shortWide.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Smart bitrate calculation based on resolution and framerate
static int64_t calculateRecommendedBitrate(int width, int height, double fps) {
    // Calculate total pixels
    int64_t pixels = (int64_t)width * height;
    
    // Bitrate presets for common resolutions (in bps) for H.265 at 30fps
    struct ResolutionPreset {
        int64_t pixels;
        int64_t bitrate_30fps;
    };
    
    ResolutionPreset presets[] = {
        {1280 * 720,    2000000},   // 720p:  2 Mbps
        {1920 * 1080,   4000000},   // 1080p: 4 Mbps
        {2560 * 1440,   7500000},   // 1440p: 7.5 Mbps
        {3840 * 2160,   15000000},  // 4K:    15 Mbps
    };
    
    int64_t baseBitrate = 0;
    
    // Find the closest preset or interpolate
    if (pixels <= presets[0].pixels) {
        // Below 720p: scale proportionally
        baseBitrate = (int64_t)(presets[0].bitrate_30fps * ((double)pixels / presets[0].pixels));
    } else if (pixels >= presets[3].pixels) {
        // 4K or above: scale proportionally
        baseBitrate = (int64_t)(presets[3].bitrate_30fps * ((double)pixels / presets[3].pixels));
    } else {
        // Interpolate between presets
        for (size_t i = 0; i < 3; i++) {
            if (pixels >= presets[i].pixels && pixels <= presets[i + 1].pixels) {
                double ratio = (double)(pixels - presets[i].pixels) / 
                               (double)(presets[i + 1].pixels - presets[i].pixels);
                baseBitrate = (int64_t)(presets[i].bitrate_30fps + 
                             ratio * (presets[i + 1].bitrate_30fps - presets[i].bitrate_30fps));
                break;
            }
        }
    }
    
    // Adjust for framerate (assuming 30fps as base)
    double fpsRatio = std::max(0.5, std::min(2.5, fps / 30.0));
    baseBitrate = (int64_t)(baseBitrate * fpsRatio);
    
    std::cout << "Calculated recommended bitrate: " << baseBitrate / 1000 << " kbps "
              << "(" << width << "x" << height << " @ " << fps << " fps)" << std::endl;
    
    return baseBitrate;
}

Transcoder::Transcoder() {}

Transcoder::~Transcoder() {
    cleanup();
}

void Transcoder::setPauseCallback(std::function<bool()> cb) {
    pauseCallback = cb;
}

void Transcoder::cleanup() {
    if (videoStreamCtx.decCtx) avcodec_free_context(&videoStreamCtx.decCtx);
    if (videoStreamCtx.encCtx) avcodec_free_context(&videoStreamCtx.encCtx);
    if (audioStreamCtx.decCtx) avcodec_free_context(&audioStreamCtx.decCtx);
    if (audioStreamCtx.encCtx) avcodec_free_context(&audioStreamCtx.encCtx);
    if (inputFormatContext) avformat_close_input(&inputFormatContext);
    if (videoStreamCtx.swsCtx) sws_freeContext(videoStreamCtx.swsCtx);
    if (videoStreamCtx.encFrame) av_frame_free(&videoStreamCtx.encFrame);
    if (outputFormatContext) {
        if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFormatContext->pb);
        avformat_free_context(outputFormatContext);
    }
}

bool Transcoder::openInput(const std::string& inputPath) {
    // Use Short Path for Unicode support
    std::string pathForFFmpeg = GetShortPath(inputPath);
    std::cout << "Opening input: " << pathForFFmpeg << std::endl;

    if (avformat_open_input(&inputFormatContext, pathForFFmpeg.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file: " << inputPath << std::endl;
        return false;
    }

    if (avformat_find_stream_info(inputFormatContext, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        return false;
    }

    return true;
}

bool Transcoder::openOutput(const std::string& outputPath) {
    // For output, we need to create the file first to get the short path
    {
        std::wstring widePath = Utf8ToWide(outputPath);
        FILE* f = _wfopen(widePath.c_str(), L"wb");
        if (f) {
            fclose(f);
        } else {
            std::cerr << "Could not create output file: " << outputPath << std::endl;
            return false;
        }
    }
    
    std::string pathForFFmpeg = GetShortPath(outputPath);
    std::cout << "Opening output: " << pathForFFmpeg << std::endl;

    int ret = avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, pathForFFmpeg.c_str());
    if (!outputFormatContext) {
        std::cout << "Could not deduce output format from file extension, using MP4." << std::endl;
        avformat_alloc_output_context2(&outputFormatContext, nullptr, "mp4", pathForFFmpeg.c_str());
    }

    if (!outputFormatContext) {
        std::cerr << "Could not create output context" << std::endl;
        return false;
    }

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFormatContext->pb, pathForFFmpeg.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "Could not open output file" << std::endl;
            return false;
        }
    }

    return true;
}

bool Transcoder::tryOpenDecoder(const char* decoderName) {
    const AVCodec* decoder = avcodec_find_decoder_by_name(decoderName);
    if (!decoder) return false;

    AVCodecContext* tempCtx = avcodec_alloc_context3(decoder);
    if (!tempCtx) return false;

    avcodec_parameters_to_context(tempCtx, videoStreamCtx.inStream->codecpar);
    
    if (avcodec_open2(tempCtx, decoder, nullptr) < 0) {
        avcodec_free_context(&tempCtx);
        return false;
    }

    // Success, save decoder context
    if (videoStreamCtx.decCtx) avcodec_free_context(&videoStreamCtx.decCtx);
    videoStreamCtx.decCtx = tempCtx;
    std::cout << "Using decoder: " << decoderName << std::endl;
    return true;
}

bool Transcoder::tryOpenEncoder(const char* encoderName, AVDictionary** opts) {
    const AVCodec* encoder = avcodec_find_encoder_by_name(encoderName);
    if (!encoder) return false;

    AVCodecContext* tempCtx = avcodec_alloc_context3(encoder);
    if (!tempCtx) return false;

    // Configure encoder parameters
    tempCtx->height = videoStreamCtx.decCtx->height;
    tempCtx->width = videoStreamCtx.decCtx->width;
    tempCtx->sample_aspect_ratio = videoStreamCtx.decCtx->sample_aspect_ratio;
    tempCtx->pix_fmt = encoder->pix_fmts ? encoder->pix_fmts[0] : AV_PIX_FMT_YUV420P;
    
    // Get framerate for bitrate calculation
    double fps = 30.0;
    if (videoStreamCtx.decCtx->framerate.num > 0 && videoStreamCtx.decCtx->framerate.den > 0) {
        fps = av_q2d(videoStreamCtx.decCtx->framerate);
    } else if (videoStreamCtx.inStream->avg_frame_rate.num > 0 && videoStreamCtx.inStream->avg_frame_rate.den > 0) {
        fps = av_q2d(videoStreamCtx.inStream->avg_frame_rate);
    }
    
    // Get input bitrate for size control
    int64_t inputBitrate = 0;
    if (videoStreamCtx.decCtx->bit_rate > 0) {
        inputBitrate = videoStreamCtx.decCtx->bit_rate;
    } else if (videoStreamCtx.inStream->codecpar->bit_rate > 0) {
        inputBitrate = videoStreamCtx.inStream->codecpar->bit_rate;
    } else if (inputFormatContext->bit_rate > 0) {
        // Estimate video bitrate by subtracting audio
        inputBitrate = inputFormatContext->bit_rate - 128000; // Assume 128kbps audio
    }
    
    // Calculate recommended bitrate based on resolution and framerate
    int64_t recommendedBitrate = calculateRecommendedBitrate(
        videoStreamCtx.decCtx->width,
        videoStreamCtx.decCtx->height,
        fps
    );
    
    // Cap output bitrate to ensure file size doesn't exceed input
    // Use 70% of input bitrate (H.265 is more efficient than H.264)
    if (inputBitrate > 0) {
        tempCtx->bit_rate = std::min(recommendedBitrate, (int64_t)(inputBitrate * 0.7));
        std::cout << "Input bitrate: " << inputBitrate / 1000 << " kbps, "
                  << "Recommended: " << recommendedBitrate / 1000 << " kbps, "
                  << "Using: " << tempCtx->bit_rate / 1000 << " kbps" << std::endl;
    } else {
        tempCtx->bit_rate = recommendedBitrate;
        std::cout << "Input bitrate unknown, using recommended: " 
                  << tempCtx->bit_rate / 1000 << " kbps" << std::endl;
    }
    
    // Set framerate and timebase
    if (videoStreamCtx.decCtx->framerate.num > 0 && videoStreamCtx.decCtx->framerate.den > 0) {
        tempCtx->framerate = videoStreamCtx.decCtx->framerate;
        tempCtx->time_base = av_inv_q(videoStreamCtx.decCtx->framerate);
    } else if (videoStreamCtx.inStream->avg_frame_rate.num > 0 && videoStreamCtx.inStream->avg_frame_rate.den > 0) {
        tempCtx->framerate = videoStreamCtx.inStream->avg_frame_rate;
        tempCtx->time_base = av_inv_q(videoStreamCtx.inStream->avg_frame_rate);
    } else {
        tempCtx->time_base = videoStreamCtx.inStream->time_base;
    }
    
    // Set GOP size for better seeking (2-4 seconds of video)
    double gop_fps = (tempCtx->framerate.num > 0) ? av_q2d(tempCtx->framerate) : 30.0;
    tempCtx->gop_size = (int)(gop_fps * 2.0);  // 2 seconds
    std::cout << "Setting GOP size to " << tempCtx->gop_size << " frames" << std::endl;
    
    // Handle global headers
    if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        tempCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Try to open encoder
    if (avcodec_open2(tempCtx, encoder, opts) < 0) {
        avcodec_free_context(&tempCtx);
        return false;
    }

    // Success, save encoder context
    if (videoStreamCtx.encCtx) avcodec_free_context(&videoStreamCtx.encCtx);
    videoStreamCtx.encCtx = tempCtx;
    std::cout << "Using encoder: " << encoderName << std::endl;
    return true;
}

bool Transcoder::initVideoTranscoding(const std::string& encoderName, bool allowHardwareDecoders) {
    // Find video stream
    for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++) {
        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamCtx.streamIndex = i;
            videoStreamCtx.inStream = inputFormatContext->streams[i];
            break;
        }
    }

    if (videoStreamCtx.streamIndex == -1) {
        std::cerr << "No video stream found" << std::endl;
        return false;
    }

    // Try hardware decoders (by priority)
    AVCodecID codecId = videoStreamCtx.inStream->codecpar->codec_id;
    bool decoderOpened = false;
    
    // Simple decoder selection logic - could be improved to match encoder selection
    if (allowHardwareDecoders) {
        if (codecId == AV_CODEC_ID_H264) {
            if (tryOpenDecoder("h264_cuvid")) decoderOpened = true;
            else if (tryOpenDecoder("h264_qsv")) decoderOpened = true;
        } else if (codecId == AV_CODEC_ID_HEVC) {
            if (tryOpenDecoder("hevc_cuvid")) decoderOpened = true;
            else if (tryOpenDecoder("hevc_qsv")) decoderOpened = true;
        }
    }
    
    // Fallback to software decoder
    if (!decoderOpened) {
        const AVCodec* decoder = avcodec_find_decoder(codecId);
        if (!decoder) {
            std::cerr << "Failed to find decoder" << std::endl;
            return false;
        }
        videoStreamCtx.decCtx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(videoStreamCtx.decCtx, videoStreamCtx.inStream->codecpar);
        
        if (avcodec_open2(videoStreamCtx.decCtx, decoder, nullptr) < 0) {
            std::cerr << "Failed to open decoder" << std::endl;
            return false;
        }
        std::cout << "Using software decoder: " << decoder->name << std::endl;
    }

    // Create output stream
    videoStreamCtx.outStream = avformat_new_stream(outputFormatContext, nullptr);

    // Try hardware encoders
    bool encoderOpened = false;
    
    if (encoderName != "auto") {
        // Try specific encoder
        if (tryOpenEncoder(encoderName.c_str())) {
            encoderOpened = true;
        } else {
            std::cerr << "Failed to open requested encoder: " << encoderName << ". Falling back to auto." << std::endl;
        }
    }
    
    if (!encoderOpened) {
        // Auto selection
        // NVIDIA NVENC
        if (tryOpenEncoder("hevc_nvenc")) encoderOpened = true;
        // Intel Quick Sync
        else if (tryOpenEncoder("hevc_qsv")) encoderOpened = true;
        // AMD AMF
        else if (tryOpenEncoder("hevc_amf")) encoderOpened = true;
        // Software encoder
        else if (tryOpenEncoder("libx265")) encoderOpened = true;
    }
    
    if (!encoderOpened) {
        std::cerr << "Failed to find any available H.265 encoder" << std::endl;
        return false;
    }

    // Update output stream parameters
    avcodec_parameters_from_context(videoStreamCtx.outStream->codecpar, videoStreamCtx.encCtx);
    videoStreamCtx.outStream->time_base = videoStreamCtx.encCtx->time_base;

    // Initialize SwsContext if needed
    if (videoStreamCtx.decCtx->pix_fmt != videoStreamCtx.encCtx->pix_fmt) {
        videoStreamCtx.swsCtx = sws_getContext(
            videoStreamCtx.decCtx->width, videoStreamCtx.decCtx->height, videoStreamCtx.decCtx->pix_fmt,
            videoStreamCtx.encCtx->width, videoStreamCtx.encCtx->height, videoStreamCtx.encCtx->pix_fmt,
            SWS_BICUBIC, nullptr, nullptr, nullptr
        );
        
        if (!videoStreamCtx.swsCtx) {
            std::cerr << "Could not initialize sws context" << std::endl;
            return false;
        }
        
        videoStreamCtx.encFrame = av_frame_alloc();
        videoStreamCtx.encFrame->format = videoStreamCtx.encCtx->pix_fmt;
        videoStreamCtx.encFrame->width = videoStreamCtx.encCtx->width;
        videoStreamCtx.encFrame->height = videoStreamCtx.encCtx->height;
        if (av_frame_get_buffer(videoStreamCtx.encFrame, 32) < 0) {
            std::cerr << "Could not allocate converted frame buffer" << std::endl;
            return false;
        }
    }

    return true;
}

bool Transcoder::initAudioTranscoding() {
    // Find audio stream
    for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++) {
        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamCtx.streamIndex = i;
            audioStreamCtx.inStream = inputFormatContext->streams[i];
            break;
        }
    }

    if (audioStreamCtx.streamIndex == -1) {
        std::cout << "No audio stream found, output will be video only" << std::endl;
        return true;  // Not an error, some videos don't have audio
    }

    // Decoder
    const AVCodec* decoder = avcodec_find_decoder(audioStreamCtx.inStream->codecpar->codec_id);
    if (!decoder) {
        std::cerr << "Failed to find audio decoder" << std::endl;
        return false;
    }

    audioStreamCtx.decCtx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(audioStreamCtx.decCtx, audioStreamCtx.inStream->codecpar);
    
    if (avcodec_open2(audioStreamCtx.decCtx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open audio decoder" << std::endl;
        return false;
    }

    std::cout << "Using audio decoder: " << decoder->name << std::endl;

    // Create output stream
    audioStreamCtx.outStream = avformat_new_stream(outputFormatContext, nullptr);

    // Encoder - use AAC for broad compatibility
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!encoder) {
        std::cerr << "Failed to find AAC encoder" << std::endl;
        return false;
    }

    audioStreamCtx.encCtx = avcodec_alloc_context3(encoder);

    // Configure encoder - use FFmpeg 7.x API
    audioStreamCtx.encCtx->sample_rate = audioStreamCtx.decCtx->sample_rate;
    av_channel_layout_copy(&audioStreamCtx.encCtx->ch_layout, &audioStreamCtx.decCtx->ch_layout);
    audioStreamCtx.encCtx->sample_fmt = encoder->sample_fmts ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    audioStreamCtx.encCtx->bit_rate = 128000;  // 128 kbps
    audioStreamCtx.encCtx->time_base = {1, audioStreamCtx.encCtx->sample_rate};

    // Handle global headers
    if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        audioStreamCtx.encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(audioStreamCtx.encCtx, encoder, nullptr) < 0) {
        std::cerr << "Failed to open audio encoder" << std::endl;
        return false;
    }

    std::cout << "Using audio encoder: " << encoder->name << std::endl;

    // Update output stream parameters
    avcodec_parameters_from_context(audioStreamCtx.outStream->codecpar, audioStreamCtx.encCtx);
    audioStreamCtx.outStream->time_base = audioStreamCtx.encCtx->time_base;

    return true;
}

int Transcoder::encode(AVCodecContext *avctx, AVStream *stream, AVFrame *frame, AVFormatContext *fmt_ctx) {
    int ret = avcodec_send_frame(avctx, frame);
    if (ret < 0) return ret;

    while (ret >= 0) {
        AVPacket* pkt = av_packet_alloc();
        ret = avcodec_receive_packet(avctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            return 0;
        } else if (ret < 0) {
            av_packet_free(&pkt);
            return ret;
        }

        av_packet_rescale_ts(pkt, avctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;

        // Enforce monotonic DTS
        // Find the context for this stream to access lastDts
        StreamContext* ctx = nullptr;
        if (stream->index == videoStreamCtx.outStream->index) ctx = &videoStreamCtx;
        else if (audioStreamCtx.outStream && stream->index == audioStreamCtx.outStream->index) ctx = &audioStreamCtx;

        if (ctx) {
            if (pkt->dts != AV_NOPTS_VALUE) {
                if (ctx->lastDts != AV_NOPTS_VALUE && pkt->dts <= ctx->lastDts) {
                    // std::cout << "Fixing non-monotonic DTS: " << pkt->dts << " -> " << ctx->lastDts + 1 << std::endl;
                    pkt->dts = ctx->lastDts + 1;
                }
                if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts) {
                    pkt->pts = pkt->dts;
                }
                ctx->lastDts = pkt->dts;
            }
            if (pkt->pts != AV_NOPTS_VALUE) {
                 if (ctx->lastPts != AV_NOPTS_VALUE && pkt->pts <= ctx->lastPts) {
                     pkt->pts = ctx->lastPts + 1;
                 }
                 ctx->lastPts = pkt->pts;
            }
        }

        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        if (ret < 0) {
             char errbuf[AV_ERROR_MAX_STRING_SIZE];
             av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
             std::cerr << "Error writing frame: " << errbuf << " (PTS: " << pkt->pts << ", DTS: " << pkt->dts << ")" << std::endl;
        }
        av_packet_free(&pkt);
    }
    return ret;
}

bool Transcoder::run(const std::string& inputPath, const std::string& outputPath, const std::string& encoderName, bool allowHardwareDecoders) {
    if (!openInput(inputPath)) return false;
    if (!openOutput(outputPath)) return false;
    if (!initVideoTranscoding(encoderName, allowHardwareDecoders)) return false;
    if (!initAudioTranscoding()) return false;

    bool success = true;

    // Open output file
    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFormatContext->pb, outputFormatContext->url, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            return false;
        }
    }

    // Write header with faststart flag for better playback compatibility
    AVDictionary* formatOpts = nullptr;
    av_dict_set(&formatOpts, "movflags", "+faststart", 0);
    int ret = avformat_write_header(outputFormatContext, &formatOpts);
    av_dict_free(&formatOpts);
    
    if (ret < 0) {
        std::cerr << "Error writing output file header" << std::endl;
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    
    // Initialize frame counter
    videoStreamCtx.nextPts = 0;
    
    int64_t totalDuration = inputFormatContext->duration;
    
    while (av_read_frame(inputFormatContext, packet) >= 0) {
        // Check for pause
        if (pauseCallback) {
            while (pauseCallback()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        // Calculate and report progress
        if (onProgress && totalDuration > 0) {
            int64_t currentPts = packet->pts;
            AVRational timeBase = inputFormatContext->streams[packet->stream_index]->time_base;
            int64_t currentTime = av_rescale_q(currentPts, timeBase, AV_TIME_BASE_Q);
            float progress = (float)currentTime / (float)totalDuration;
            if (progress >= 0.0f && progress <= 1.0f) {
                onProgress(progress);
            }
        }
        if (packet->stream_index == videoStreamCtx.streamIndex) {
            int ret = avcodec_send_packet(videoStreamCtx.decCtx, packet);
            if (ret < 0) {
                std::cerr << "Error sending video packet for decoding" << std::endl;
                success = false;
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(videoStreamCtx.decCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    std::cerr << "Error during video decoding" << std::endl;
                    success = false;
                    goto end;
                }

                // Preserve original PTS to maintain correct playback speed
                // Only use counter if original PTS is invalid
                if (frame->pts == AV_NOPTS_VALUE) {
                    frame->pts = videoStreamCtx.nextPts;
                    videoStreamCtx.nextPts++;
                } else {
                    // Rescale PTS from input stream timebase to encoder timebase
                    frame->pts = av_rescale_q(frame->pts, 
                                            videoStreamCtx.inStream->time_base, 
                                            videoStreamCtx.encCtx->time_base);
                    // Update nextPts based on current rescaled pts for fallback
                    videoStreamCtx.nextPts = frame->pts + 1;
                }
                
                // Let encoder decide picture type
                frame->pict_type = AV_PICTURE_TYPE_NONE;
                
                AVFrame* frameToSend = frame;
                if (videoStreamCtx.swsCtx) {
                    // Convert frame
                    sws_scale(videoStreamCtx.swsCtx,
                        (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height,
                        videoStreamCtx.encFrame->data, videoStreamCtx.encFrame->linesize);
                    
                    videoStreamCtx.encFrame->pts = frame->pts;
                    videoStreamCtx.encFrame->pict_type = AV_PICTURE_TYPE_NONE;
                    frameToSend = videoStreamCtx.encFrame;
                }

                if (encode(videoStreamCtx.encCtx, videoStreamCtx.outStream, frameToSend, outputFormatContext) < 0) {
                    std::cerr << "Error during video encoding" << std::endl;
                    success = false;
                    goto end;
                }
            }
        } else if (audioStreamCtx.streamIndex != -1 && packet->stream_index == audioStreamCtx.streamIndex) {
            int ret = avcodec_send_packet(audioStreamCtx.decCtx, packet);
            if (ret < 0) {
                std::cerr << "Error sending audio packet for decoding" << std::endl;
                success = false;
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audioStreamCtx.decCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    std::cerr << "Error during audio decoding" << std::endl;
                    success = false;
                    goto end;
                }

                // Use incremental frame counter for audio PTS
                // Audio usually uses sample count, but we should also respect input timestamps if possible
                if (frame->pts != AV_NOPTS_VALUE) {
                     frame->pts = av_rescale_q(frame->pts, 
                                             audioStreamCtx.inStream->time_base, 
                                             audioStreamCtx.encCtx->time_base);
                } else {
                    frame->pts = audioStreamCtx.nextPts;
                }
                audioStreamCtx.nextPts = frame->pts + frame->nb_samples;
                
                if (encode(audioStreamCtx.encCtx, audioStreamCtx.outStream, frame, outputFormatContext) < 0) {
                    std::cerr << "Error during audio encoding" << std::endl;
                    success = false;
                    goto end;
                }
            }
        }
        av_packet_unref(packet);
    }

    // Flush encoders
    encode(videoStreamCtx.encCtx, videoStreamCtx.outStream, nullptr, outputFormatContext);
    if (audioStreamCtx.streamIndex != -1) {
        encode(audioStreamCtx.encCtx, audioStreamCtx.outStream, nullptr, outputFormatContext);
    }

    av_write_trailer(outputFormatContext);

end:
    av_packet_free(&packet);
    av_frame_free(&frame);

    return success;
}

bool Transcoder::isHevc(const std::string& inputPath) {
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, inputPath.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    bool foundHevc = false;
    unsigned int nbStreams = fmtCtx->nb_streams;
    for (unsigned int i = 0; i < nbStreams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (fmtCtx->streams[i]->codecpar->codec_id == AV_CODEC_ID_HEVC) {
                foundHevc = true;
            }
            break;
        }
    }

    avformat_close_input(&fmtCtx);
    return foundHevc;
}
