#include <stdio.h>
#include<unistd.h>

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>

#include "video.h"
#include "frame.h"
#include "explorer.h"

#define WAVEBUFCOUNT 3
#define	MAX_LIST     28

C2D_Image frame;
C3D_RenderTarget* top;
THEORA_Context vidCtx;
Thread vthread = NULL;
Thread athread = NULL;
static size_t buffSize = 8 * 4096;
static ndspWaveBuf waveBuf[WAVEBUFCOUNT];
int16_t* audioBuffer;
LightEvent soundEvent;
_LOCK_T oggMutex;
int ready = 0;

int isplaying = false;

void audioInit(THEORA_audioinfo* ainfo) {
	ndspChnReset(0);
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetInterp(0, ainfo->channels == 2 ? NDSP_INTERP_POLYPHASE : NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, ainfo->rate);
	ndspChnSetFormat(0, ainfo->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	audioBuffer = linearAlloc((buffSize * sizeof(int16_t)) * 2);

	memset(waveBuf, 0, sizeof(waveBuf));
	for (unsigned i = 0; i < WAVEBUFCOUNT; ++i)
	{
		waveBuf[i].data_vaddr = &audioBuffer[i * buffSize];
		waveBuf[i].nsamples = buffSize;
		waveBuf[i].status = NDSP_WBUF_DONE;
	}
}

void audioClose(void) {
	if (audioBuffer) linearFree(audioBuffer);
	ndspChnWaveBufClear(0);
}

void videoDecode_thread(void* nul) {
	THEORA_videoinfo* vinfo = THEORA_vidinfo(&vidCtx);
	THEORA_audioinfo* ainfo = THEORA_audinfo(&vidCtx);

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

	while (isplaying)
	{
		if (THEORA_eos(&vidCtx))
			break;

		if (THEORA_HasVideo(&vidCtx)) {
			//__lock_acquire(oggMutex);
			bool newframe = THEORA_readvideo(&vidCtx);
			//__lock_release(oggMutex);

			if (THEORA_HasAudio(&vidCtx)) {
				for (int cur_wvbuf = 0; cur_wvbuf < WAVEBUFCOUNT; cur_wvbuf++) {
					ndspWaveBuf *buf = &waveBuf[cur_wvbuf];

					if(buf->status == NDSP_WBUF_DONE) {
						//__lock_acquire(oggMutex);
						size_t read = THEORA_readaudio(&vidCtx, buf->data_pcm16, buffSize);
						//__lock_release(oggMutex);
						if(read <= 0)
						{
							return;
						}
						else if(read <= buffSize)
							buf->nsamples = read / ainfo->channels;

						ndspChnWaveBufAdd(0, buf);
					}
					DSP_FlushDataCache(buf->data_pcm16, buffSize * sizeof(int16_t));
				}
			}

			if (newframe) {
				th_ycbcr_buffer ybr;
				while (!THEORA_decodevideo(&vidCtx, ybr));
				frameWrite(&frame, vinfo, ybr);
			}
		}
	}

	printf("frames: %d dropped: %d\n", vidCtx.frames, vidCtx.dropped);

	frameDelete(&frame);
	THEORA_Close(&vidCtx);
	audioClose();

	threadExit(0);
}

void audioCallback(void *const arg_)
{
	(void)arg_;

	if (!isplaying)
		return;

	LightEvent_Signal(&soundEvent);
}

/*void audioDecode_thread(void* nul) {
	THEORA_audioinfo* ainfo = THEORA_audinfo(&vidCtx);

	if (THEORA_HasAudio(&vidCtx))
		audioInit(ainfo);

	while (isplaying) {
		if (THEORA_HasAudio(&vidCtx)) {
			for (int cur_wvbuf = 0; cur_wvbuf < WAVEBUFCOUNT; cur_wvbuf++) {
				ndspWaveBuf *buf = &waveBuf[cur_wvbuf];

				if(buf->status == NDSP_WBUF_DONE) {
					__lock_acquire(oggMutex);
					size_t read = THEORA_readaudio(&vidCtx, buf->data_pcm16, buffSize);
					__lock_release(oggMutex);
					if(read <= 0)
					{
						return;
					}
					else if(read <= buffSize)
						buf->nsamples = read / ainfo->channels;

					ndspChnWaveBufAdd(0, buf);
				}
				DSP_FlushDataCache(buf->data_pcm16, buffSize * sizeof(int16_t));
			}
			LightEvent_Wait(&soundEvent);
		}
	}

	//audioClose();

	threadExit(0);
}*/

static void exitThread(void) {
	isplaying = false;

	threadJoin(vthread, U64_MAX);
	threadFree(vthread);

	LightEvent_Signal(&soundEvent);
	threadJoin(athread, U64_MAX);
	threadFree(athread);

	vthread = NULL;
	athread = NULL;
}

static void changeFile(char* filepath) {
	int ret;

	if (vthread != NULL || athread != NULL)
		exitThread();

	if ((ret = THEORA_Create(&vidCtx, filepath))) {
		printf("Video could not be opened.\n");
		return;
	}

	if (!THEORA_HasVideo(&vidCtx) && !THEORA_HasAudio(&vidCtx)) {
		printf("No audio or video stream could be found.\n");
		return;
	}

	if (THEORA_HasVideo(&vidCtx)) {
		THEORA_videoinfo* vinfo = THEORA_vidinfo(&vidCtx);
		frameInit(&frame, vinfo);
	}

	printf("Theora Create sucessful.\n");
	isplaying = true;

	s32 prio;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	//athread = threadCreate(audioDecode_thread, NULL, 32 * 1024, prio-1, -1, false);
	vthread = threadCreate(videoDecode_thread, NULL, 32 * 1024, prio-1, -1, false);
}

//---------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
//---------------------------------------------------------------------------------
	dirList_t dirList = {0};
	int fileMax;
	int cursor = 0;
	int from = 0;

	romfsInit();
	ndspInit();
	y2rInit();
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	consoleInit(GFX_BOTTOM, NULL);
	hidSetRepeatParameters(25, 5);

	osSetSpeedupEnable(true);

	chdir("sdmc:/");
	chdir("videos");

	// Create screens
	top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

	//printf("%s.\n", th_version_string());

	fileMax = getDir(&dirList);
	printDir(from, MAX_LIST, 0, dirList);

	ndspSetCallback(audioCallback, NULL);

	while(aptMainLoop()){
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kDownRepeat = hidKeysDownRepeat();

		if (kDown & KEY_START)
			break;

		if (!isplaying) {
			if((kDown & KEY_UP || (kDownRepeat & KEY_UP)) && cursor > 0)
			{
				cursor--;

				/* 26 is the maximum number of entries that can be printed */
				if(fileMax - cursor > 26 && from != 0)
					from--;

				printDir(from, MAX_LIST, cursor, dirList);
			}

			if((kDown & KEY_DOWN || (kDownRepeat & KEY_DOWN)) && cursor < fileMax)
			{
				cursor++;

				if(cursor >= MAX_LIST && fileMax - cursor >= 0 &&
						from < fileMax - MAX_LIST)
					from++;

				printDir(from, MAX_LIST, cursor, dirList);
			}

			/*
			 * Pressing B goes up a folder, as well as pressing A or R when ".."
			 * is selected.
			 */
			if((kDown & KEY_B) || ((kDown & KEY_A) && (from == 0 && cursor == 0)))
			{
				chdir("..");
				consoleClear();
				fileMax = getDir(&dirList);

				cursor = 0;
				from = 0;

				printDir(from, MAX_LIST, cursor, dirList);

				continue;
			}

			if(kDown & KEY_A) {
				if(dirList.dirNum >= cursor) {
					chdir(dirList.directories[cursor - 1]);
					consoleClear();
					cursor = 0;
					from = 0;
					fileMax = getDir(&dirList);

					printDir(from, MAX_LIST, cursor, dirList);
					continue;
				}

				if(dirList.dirNum < cursor) {
					changeFile(dirList.files[cursor - dirList.dirNum - 1]);
					continue;
				}
			}
		} else {
			if(kDown & KEY_B) {
				exitThread();
				printDir(from, MAX_LIST, cursor, dirList);
				continue;
			}
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			C2D_TargetClear(top, C2D_Color32(20, 29, 31, 255));
			C2D_SceneBegin(top);
			if (isplaying && THEORA_HasVideo(&vidCtx))
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
