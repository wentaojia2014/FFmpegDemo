#include <QDebug>
#include <QFile>
#include <QDir>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
}

struct _Args {
	const char *inputFileName;
	const char *output_Audio_FileName;
	const char *output_Video_FileName;
}Args;
QDebug operator<< (QDebug dbg, const _Args & args)
{
	QDebugStateSaver saver(dbg);
	dbg.noquote() << args.inputFileName << args.output_Audio_FileName << args.output_Video_FileName;
	return dbg;
}
void parseArgs(int argc, char * argv[])
{
	if (argc < 1 || argv == nullptr) {
		qDebug() << "parse args failed";
		return ;
	}
	Args.inputFileName = argv[1];
	Args.output_Audio_FileName = argv[2];
	Args.output_Video_FileName = argv[3];
}
QFile videoFile, audioFile;
struct _FFmpeg {
	AVFormatContext *fmtCtx;
	AVCodec *codec;
	AVCodecContext *codecCtx;
	AVStream	*stream;
	_FFmpeg():fmtCtx(0),
		codec(0),
		codecCtx(0),
		stream(0)
	{
	}
} FFmpeg;

struct _Video : public _FFmpeg {
	uint8_t *dest_data[4];
	int dest_linesize[4];
	int dest_bufsize;
	enum AVPixelFormat pix_format;
	int frameWidth, frameHeight;

} video;
struct _Audio :public _FFmpeg {

}audio;
AVFrame *frame;
AVPacket packet;

static int open_codec_context(AVMediaType type)
{
	AVDictionary *opts = NULL;
	int index = av_find_best_stream(FFmpeg.fmtCtx, type, -1, -1, NULL, 0);
	if (index < 0) {
		qDebug() << "find best stream failed";
		return index;
	} else {
		FFmpeg.stream = FFmpeg.fmtCtx->streams[index];
		FFmpeg.codecCtx = FFmpeg.stream->codec;
		FFmpeg.codec = avcodec_find_decoder(FFmpeg.codecCtx->codec_id);
		if (!FFmpeg.codec) {
			qDebug() << "find codec failed";
			return -1;
		}

		//		/* Init the decoders, with or without reference counting */
		av_dict_set(&opts, "refcounted_frames", "0", 0);
		if (avcodec_open2(FFmpeg.codecCtx, FFmpeg.codec, &opts) < 0) {
			qDebug() << "open codec failed";
			return -2;
		}
	}
	return 0;
}

int decode_packet(int & gotFrame, bool cached)
{
	int ret = 0;
	int decoded = packet.size;
	static int video_frame_count = 0;
	static int audio_frame_count = 0;
	gotFrame = 0;
	if (packet.stream_index == video.stream->index) {
		ret = avcodec_decode_video2(video.codecCtx, frame, &gotFrame, &packet);
		if (ret < 0) {
			qDebug() << "decode video frame failed";
			return -1;
		}

		if (gotFrame) {
			if (video.codecCtx->width != frame->width || video.codecCtx->height != frame->height || video.pix_format != frame->format){
				qDebug() << "Error, width or height mot match";
				return -1;
			}

			qDebug() << "video frame "<< (cached  ? QString("(cached)") : QString("")) <<video_frame_count << "coded_n: " << frame->coded_picture_number << "pts:" << frame->pts;
			video_frame_count++;
			av_image_copy(video.dest_data, video.dest_linesize, (const uint8_t **)frame->data, frame->linesize, video.pix_format, frame->width, frame->height);
			videoFile.write((const char *)video.dest_data[0], video.dest_bufsize);
		}
	} else if (packet.stream_index == audio.stream->index){
		ret = avcodec_decode_audio4(audio.codecCtx, frame, &gotFrame, &packet);
		if (ret < 0) {
			qDebug() << "audio decode failed";
			return -1;
		}

		decoded = FFMIN(ret, packet.size);

		if (gotFrame) {
			qDebug() << "audio frame " << (cached  ? QString("(cached)") : QString("")) <<audio_frame_count << "b_samples" << frame->nb_samples << "pts:" << frame->pts;
			audio_frame_count++;
			size_t linesize = frame->nb_samples * av_get_bytes_per_sample((AVSampleFormat)frame->format);
			audioFile.write((const char *)frame->extended_data[0], linesize);
		}
	}
	return FFMIN(ret, packet.size);
}
int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt)
{
	int  i;
	struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt;
		const char *fmt_be, *fmt_le;
	}
	sample_fmt_entry[] = {
	{AV_SAMPLE_FMT_U8, "u8", "u8"},
	{AV_SAMPLE_FMT_S16, "s16be", "s16le"},
	{AV_SAMPLE_FMT_S32, "s32be", "s32le"},
	{AV_SAMPLE_FMT_FLT, "f32be", "f32le"},
	{AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
};
	*fmt = NULL;
	for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entry); ++i) {
		struct sample_fmt_entry *entry = &sample_fmt_entry[i];
		if (sample_fmt == entry->sample_fmt)
		{
			*fmt = AV_NE(entry->fmt_be, entry->fmt_le);
			return 0;
		}
	}
	qDebug() << "sample format" << av_get_sample_fmt_name(sample_fmt) << "is not support as output format";
	return -1;
}
void outError(int num)
{
	QByteArray error;
	error.resize(1024);
	av_strerror(num, error.data(), error.size());
	qDebug() << "ffmpeg error:" << QString(error);
}
int main(int argc, char *argv[])
{
	qSetMessagePattern("[log %{file} %{line} %{function}] %{message}");
	//[1]args
    parseArgs(argc, argv);
    qDebug () << Args;
//	[1]

	//[2] init
	av_register_all();
	avcodec_register_all();
	avformat_network_init();
	//[2]
	int ret = 0;
	//[3] open input
    qDebug() << QDir::currentPath();
	ret = avformat_open_input(&FFmpeg.fmtCtx, Args.inputFileName, NULL, NULL);
	if (ret < 0) {
		qDebug() << "open input file error";
		outError(ret);
		return -1;
	}
	if (avformat_find_stream_info(FFmpeg.fmtCtx, NULL) < 0) {
		qDebug() <<"finfd stream failed";
		return -1;
	}
	//[3]
	//[4] init audio video resource
	if (open_codec_context(AVMEDIA_TYPE_VIDEO) >= 0) {
		video.codecCtx = FFmpeg.codecCtx;
		video.stream = FFmpeg.stream;
		video.codec  = FFmpeg.codec;

		video.frameWidth = video.codecCtx->width;
		video.frameHeight= video.codecCtx->height;
		video.pix_format = video.codecCtx->pix_fmt;

		video.dest_bufsize = av_image_alloc(video.dest_data, video.dest_linesize, video.frameWidth, video.frameHeight, video.pix_format, 1);
		if (video.dest_bufsize < 0) {
			qDebug() << "alloc image buf failed";
			return -2;
		}
		videoFile.setFileName(Args.output_Video_FileName);
		if (!videoFile.open(QFile::ReadWrite)) {
			qDebug() << "open video output file failed";
			return -3;
		}
	}
	if (open_codec_context(AVMEDIA_TYPE_AUDIO) >=0) {
		audio.codecCtx = FFmpeg.codecCtx;
		audio.stream = FFmpeg.stream;
		audio.codec  = FFmpeg.codec;

		audioFile.setFileName(Args.output_Audio_FileName);
		if (!audioFile.open(QFile::ReadWrite)) {
			qDebug() << "open audio output file failed";
			return -3;
		}
	}
	if (video.stream) {
		qDebug() << "Demuxing video from " << Args.inputFileName << "into "<< Args.output_Video_FileName;
	}
	if (audio.stream) {
		qDebug() << "Demuxing audio from " << Args.inputFileName << "into " << Args.output_Audio_FileName;
	}
	av_dump_format(FFmpeg.fmtCtx, 0, Args.inputFileName, 0);
	//[4]
	if (!video.stream && !audio.stream) {
		qDebug() << "Could not find audio or video stream.";
		return -1;
	}
	//[5] read audio video
	frame = av_frame_alloc();
	if (frame == nullptr) {
		qDebug() << "alloc frame failed";
		return -1;
	}

	av_init_packet(&packet);
	packet.data = nullptr;
	packet.size = 0;

	int frameNum = 0;
	int gotFrame = 0;
	while(av_read_frame(FFmpeg.fmtCtx, &packet) >= 0) {
//		if (frameNum++ > MAX_NUM_TO_DECODE) {
//			break;
//		}
		do {
			ret = decode_packet(gotFrame, false);
			if (ret < 0) {
				break;
			}
			packet.data += ret;
			packet.size -= ret;

		} while( packet.size > 0);
	}

	packet.data = nullptr;
	packet.size = 0;
	do {
		ret = decode_packet(gotFrame, true);
	} while(gotFrame);
	//[5]

	//[6] make play command file

	//Video
	{
		QString playVideo = QString("ffplay -f rawvideo -pix_fmt %1 -video_size %2x%3 %4")
				.arg(QString(av_get_pix_fmt_name(video.codecCtx->pix_fmt)))
				.arg(video.codecCtx->width)
				.arg(video.codecCtx->height)
				.arg(Args.output_Video_FileName);
#ifdef Q_OS_WIN
        QFile file("ffplayVideo.bat");
#else
        QFile file("ffplayVideo.sh");
#endif
		if (file.open(QFile::ReadWrite)) {
			QTextStream out(&file);
			out << playVideo;
			file.close();
		}
	}

	//Audio
	{
		enum AVSampleFormat sfmt = audio.codecCtx->sample_fmt;
		int n_channels			 = audio.codecCtx->channels;
		const char *fmt;
		if (av_sample_fmt_is_planar(sfmt)) {
			const char *packed = av_get_sample_fmt_name(sfmt);
			qDebug() << "the sample foprmat the decoder produced is planar"
					 << (packed ? packed : "?")
					 << ". This example will output the first channel only";
			sfmt = av_get_packed_sample_fmt(sfmt);
			n_channels = 1;
		}
		if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
			goto ret;


		QString playAudio = QString("ffplay -f %1 -ac %2 -ar %3 %4")
				.arg(QString(fmt))
				.arg(n_channels)
				.arg(audio.codecCtx->sample_rate)
				.arg(Args.output_Audio_FileName);
#ifdef Q_OS_WIN
        QFile file("ffplayAudio.bat");
#else
        QFile file("ffplayAudio.sh");
#endif
		if (file.open(QFile::ReadWrite)) {
			QTextStream out(&file);
			out << playAudio;
			file.close();
		}
	}
	//[6]

	//[7]release
ret:
	avcodec_close(video.codecCtx);
	avcodec_close(audio.codecCtx);
	avformat_close_input(&FFmpeg.fmtCtx);
	av_frame_free(&frame);
	av_free(video.dest_data[0]);
	videoFile.close();
	audioFile.close();
	//[7]

	return 0;
}
