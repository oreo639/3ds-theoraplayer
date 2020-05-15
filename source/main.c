#include <stdio.h>

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>

#include "video.h"
#include "frame.h"

C2D_Image frame;
C3D_RenderTarget* top;
THEORA_Context vidCtx;
Thread thread = NULL;
static size_t buffSize = 9 * 4096;
static ndspWaveBuf waveBuf[2];
int16_t* audioBuffer;

int run = true;

void audioInit(THEORA_audioinfo* ainfo) {
	ndspChnReset(0);
	ndspChnWaveBufClear(0);
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetInterp(0, ainfo->channels == 2 ? NDSP_INTERP_POLYPHASE : NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, ainfo->rate);
	ndspChnSetFormat(0, ainfo->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	audioBuffer = linearAlloc((buffSize * sizeof(int16_t)) * 2);

	memset(waveBuf, 0, sizeof(waveBuf));
	waveBuf[0].data_vaddr = audioBuffer;
	waveBuf[0].status = NDSP_WBUF_DONE;
	waveBuf[1].data_vaddr = audioBuffer + buffSize;
	waveBuf[1].status = NDSP_WBUF_DONE;
}

void audioClose(void) {
	linearFree(audioBuffer);
	ndspChnWaveBufClear(0);
}

void videoDecode_thread(void* nul) {
	THEORA_videoinfo* vinfo;
	THEORA_audioinfo* ainfo = THEORA_audinfo(&vidCtx);

	vinfo = THEORA_vidinfo(&vidCtx);

	if (THEORA_HasAudio(&vidCtx))
		audioInit(ainfo);

	if (THEORA_HasVideo(&vidCtx)) {
		printf("Ogg stream is Theora %dx%d %.02f fps\n", vinfo->width, vinfo->height, vinfo->fps);

		switch(vinfo->colorspace) {
			case TH_CS_UNSPECIFIED:
				// nothing to report
				break;;
			case TH_CS_ITU_REC_470M:
				printf("	encoder specified ITU Rec 470M (NTSC) color.\n");
				break;;
			case TH_CS_ITU_REC_470BG:
				printf("	encoder specified ITU Rec 470BG (PAL) color.\n");
				break;;
			default:
				printf("warning: encoder specified unknown colorspace (%d).\n",	vinfo->colorspace);
				break;;
		}
	}

	

	printf("First frame done.\n");

	while (run)
	{
		if (THEORA_eos(&vidCtx))
			break;

		bool newframe = THEORA_readvideo(&vidCtx);

		for (int cur_wvbuf = 0; cur_wvbuf < 2; cur_wvbuf++) {
			ndspWaveBuf *buf = &waveBuf[cur_wvbuf];

			if(buf->status == NDSP_WBUF_DONE)
			{
				size_t read = THEORA_readaudio(&vidCtx, buf->data_pcm16, buffSize);
				if(read <= 0)
				{
					return;
				}
				else if(read <= buffSize)
					buf->nsamples = read / ainfo->channels;

				//printf("Thing happened %d.\n", buf->nsamples);

				ndspChnWaveBufAdd(0, buf);
			}
			DSP_FlushDataCache(buf->data_pcm16, buffSize * sizeof(int16_t));
		}

		if (newframe)
		{
			th_ycbcr_buffer ybr;
			while (!THEORA_decodevideo(&vidCtx, ybr));
			frameWrite(&frame, vinfo, ybr);
		}
	}

	printf("frames: %d dropped: %d\n", vidCtx.frames, vidCtx.dropped);

	frameDelete(&frame);
	THEORA_Close(&vidCtx);
	thread = NULL;
	threadExit(0);
}

static void exitThread(void) {
	run = false;

	threadJoin(thread, U64_MAX);
	threadFree(thread);

	thread = NULL;
}

static void changeFile(char* filepath) {
	int ret;

	if (thread != NULL)
		exitThread();

	if ((ret = THEORA_Create(&vidCtx, filepath))) {
		printf("Video could not be opened.\n");
		return;
	}

	if (THEORA_HasVideo(&vidCtx)) {
		THEORA_videoinfo* vinfo = THEORA_vidinfo(&vidCtx);
		frameInit(&frame, vinfo);
	}

	printf("Theora Create sucessful.\n");
	run = true;

	s32 prio;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	thread = threadCreate(videoDecode_thread, NULL, 32 * 1024, prio-1, -2, false);
}

//---------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
//---------------------------------------------------------------------------------
	//char *filename = "sdmc:/test.ogv";
	char *filename = "sdmc:/videos/A_Digital_Media_Primer_For_Geeks-240p.ogv";

	romfsInit();
	ndspInit();
	y2rInit();
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	consoleInit(GFX_BOTTOM, NULL);

	osSetSpeedupEnable(true);

	// Create screens
	top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

	printf("%s.\n", th_version_string());

	changeFile(filename);

	while(aptMainLoop()){
		hidScanInput();
		u32 kDown = hidKeysDown();

		if (kDown & KEY_START)
			break;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			C2D_TargetClear(top, C2D_Color32(20, 29, 31, 255));
			C2D_SceneBegin(top);
			C2D_DrawImageAt(frame, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
		C3D_FrameEnd(0);
	}

	exitThread();

	osSetSpeedupEnable(false);

	gfxExit();
	y2rExit();
	ndspExit();
	romfsExit();
	return 0;
}
