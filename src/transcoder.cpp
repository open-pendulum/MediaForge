#include "transcoder.h"
#include <iostream>

extern "C" {
#include <libavutil/opt.h>
}

Transcoder::Transcoder() {}

Transcoder::~Transcoder() {
    cleanup();
}

void Transcoder::cleanup() {
    if (videoStreamCtx.decCtx) avcodec_free_context(&videoStreamCtx.decCtx);
    if (videoStreamCtx.encCtx) avcodec_free_context(&videoStreamCtx.encCtx);
    if (audioStreamCtx.decCtx) avcodec_free_context(&audioStreamCtx.decCtx);
    if (audioStreamCtx.encCtx) avcodec_free_context(&audioStreamCtx.encCtx);
    if (inputFormatContext) avformat_close_input(&inputFormatContext);
    if (outputFormatContext) {
        if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFormatContext->pb);
        avformat_free_context(outputFormatContext);
    }
}

bool Transcoder::openInput(const std::string& inputPath) {
    if (avformat_open_input(&inputFormatContext, inputPath.c_str(), nullptr, nullptr) < 0) {
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
    avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, outputPath.c_str());
    if (!outputFormatContext) {
        std::cerr << "Could not create output context" << std::endl;
        return false;
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
    
    // Set framerate and timebase
    if (videoStreamCtx.decCtx->framerate.num > 0 && videoStreamCtx.decCtx->framerate.den > 0) {
        tempCtx->framerate = videoStreamCtx.decCtx->framerate;
        tempCtx->time_base = av_inv_q(videoStreamCtx.decCtx->framerate);
    } else {
        tempCtx->time_base = videoStreamCtx.inStream->time_base;
    }
    
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

bool Transcoder::initVideoTranscoding() {
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
    
    if (codecId == AV_CODEC_ID_H264) {
        // H.264 hardware decoders
        if (tryOpenDecoder("h264_cuvid")) decoderOpened = true;      // NVIDIA
        else if (tryOpenDecoder("h264_qsv")) decoderOpened = true;   // Intel
    } else if (codecId == AV_CODEC_ID_HEVC) {
        // H.265 hardware decoders
        if (tryOpenDecoder("hevc_cuvid")) decoderOpened = true;      // NVIDIA
        else if (tryOpenDecoder("hevc_qsv")) decoderOpened = true;   // Intel
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

    // Try hardware encoders (by priority)
    bool encoderOpened = false;
    
    // NVIDIA NVENC
    if (tryOpenEncoder("hevc_nvenc")) encoderOpened = true;
    // Intel Quick Sync
    else if (tryOpenEncoder("hevc_qsv")) encoderOpened = true;
    // AMD AMF
    else if (tryOpenEncoder("hevc_amf")) encoderOpened = true;
    // Software encoder
    else if (tryOpenEncoder("libx265")) encoderOpened = true;
    
    if (!encoderOpened) {
        std::cerr << "Failed to find any available H.265 encoder" << std::endl;
        return false;
    }

    // Update output stream parameters
    avcodec_parameters_from_context(videoStreamCtx.outStream->codecpar, videoStreamCtx.encCtx);
    videoStreamCtx.outStream->time_base = videoStreamCtx.encCtx->time_base;

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

        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_free(&pkt);
    }
    return ret;
}

bool Transcoder::run(const std::string& inputPath, const std::string& outputPath) {
    if (!openInput(inputPath)) return false;
    if (!openOutput(outputPath)) return false;
    if (!initVideoTranscoding()) return false;
    if (!initAudioTranscoding()) return false;

    // Open output file
    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFormatContext->pb, outputFormatContext->url, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            return false;
        }
    }

    // Write header
    if (avformat_write_header(outputFormatContext, nullptr) < 0) {
        std::cerr << "Error writing output file header" << std::endl;
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    
    // Initialize frame counter
    videoStreamCtx.nextPts = 0;
    
    int64_t totalDuration = inputFormatContext->duration;
    
    while (av_read_frame(inputFormatContext, packet) >= 0) {
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
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(videoStreamCtx.decCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    std::cerr << "Error during video decoding" << std::endl;
                    goto end;
                }

                // FIX: Use incremental frame counter to generate correct PTS
                frame->pts = videoStreamCtx.nextPts++;
                
                if (encode(videoStreamCtx.encCtx, videoStreamCtx.outStream, frame, outputFormatContext) < 0) {
                    std::cerr << "Error during video encoding" << std::endl;
                    goto end;
                }
            }
        } else if (audioStreamCtx.streamIndex != -1 && packet->stream_index == audioStreamCtx.streamIndex) {
            int ret = avcodec_send_packet(audioStreamCtx.decCtx, packet);
            if (ret < 0) {
                std::cerr << "Error sending audio packet for decoding" << std::endl;
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audioStreamCtx.decCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    std::cerr << "Error during audio decoding" << std::endl;
                    goto end;
                }

                // Use incremental frame counter for audio PTS
                frame->pts = audioStreamCtx.nextPts;
                audioStreamCtx.nextPts += frame->nb_samples;
                
                if (encode(audioStreamCtx.encCtx, audioStreamCtx.outStream, frame, outputFormatContext) < 0) {
                    std::cerr << "Error during audio encoding" << std::endl;
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
    return true;
}
