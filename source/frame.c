#include <stdlib.h>
#include <stdbool.h>

#include <3ds.h>

#include "frame.h"

Handle y2rEvent;

static inline unsigned nearestPo2(unsigned x) {
	return x <= 1 ? 1 : (1u << (32 - __builtin_clz(x - 1)));
}

static inline size_t fmtGetBPP(GPU_TEXCOLOR fmt) {
	switch (fmt)
	{
		case GPU_RGBA8:
			return 4;
		case GPU_RGB8:
			return 3;
		default:
			return 0;
	}
}

int frameInit(TH3DS_Frame* vframe, THEORA_videoinfo* info) {
	if (!vframe || !info || y2rInit())
		return 1;

	switch(info->colorspace) {
		case TH_CS_UNSPECIFIED:
			// nothing to report
			break;
		case TH_CS_ITU_REC_470M:
			printf("	encoder specified ITU Rec 470M (NTSC) color.\n");
			break;
		case TH_CS_ITU_REC_470BG:
			printf("	encoder specified ITU Rec 470BG (PAL) color.\n");
			break;
		default:
			printf("warning: encoder specified unknown colorspace (%d).\n",	info->colorspace);
			break;
	}

	switch(info->fmt)
	{
		case TH_PF_420:
			printf(" 4:2:0 video\n");
			break;
		case TH_PF_422:
			printf(" 4:2:2 video\n");
			break;
		case TH_PF_444:
			puts("YUV444 is not supported by Y2R");
			return 2;
		case TH_PF_RSVD:
		default:
			printf(" video\n	(UNKNOWN Chroma sampling!)\n");
			return 2;
	}

	for (int i = 0; i < 2; i++) {
		C3D_Tex* curtex = &vframe->buff[i];
		C3D_TexInit(curtex, nearestPo2(info->width), nearestPo2(info->height), GPU_RGB8);
		C3D_TexSetFilter(curtex, GPU_LINEAR, GPU_LINEAR);
		memset(curtex->data, 0, curtex->size);
	}

	Tex3DS_SubTexture* subtex = malloc(sizeof(Tex3DS_SubTexture));

	subtex->width = info->width;
	subtex->height = info->height;
	subtex->left = 0.0f;
	subtex->top = 1.0f;
	subtex->right = (float)info->width/nearestPo2(info->width);
	subtex->bottom = 1.0-((float)info->height/nearestPo2(info->height));

	vframe->curbuf = false;
	vframe->img.tex = &vframe->buff[vframe->curbuf];
	vframe->img.subtex = subtex;

	return 0;
}

void frameDelete(TH3DS_Frame* vframe) {
	if (!vframe)
		return;

	Y2RU_StopConversion();

	if (vframe->buff[0].data) {
		C3D_TexDelete(&vframe->buff[0]);
		C3D_TexDelete(&vframe->buff[1]);
		//free(image->tex);
	}

	if (vframe->img.subtex)
		free((void*)vframe->img.subtex);

	y2rExit();
}

void frameWrite(TH3DS_Frame* vframe, THEORA_videoinfo* info, th_ycbcr_buffer ybr) {
	bool is_busy = true;
	bool drawbuf = !vframe->curbuf;
	C3D_Tex* wframe = &vframe->buff[drawbuf];

	if (!vframe || !info)
		return;

	if (!ybr[0].data || !ybr[1].data || !ybr[2].data) {
		printf("Error: ybr data is null\n");
		return;
	}

	Y2RU_StopConversion();

	while (is_busy)
		Y2RU_IsBusyConversion(&is_busy);

	switch(info->fmt)
	{
		case TH_PF_420:
			Y2RU_SetInputFormat(INPUT_YUV420_INDIV_8);
			break;
		case TH_PF_422:
			Y2RU_SetInputFormat(INPUT_YUV422_INDIV_8);
			break;
		default:
			break;
	}

	Y2RU_SetOutputFormat(OUTPUT_RGB_24);
	Y2RU_SetRotation(ROTATION_NONE);
	Y2RU_SetBlockAlignment(BLOCK_8_BY_8);
	Y2RU_SetTransferEndInterrupt(true);
	Y2RU_SetInputLineWidth(info->width);
	Y2RU_SetInputLines(info->height);
	Y2RU_SetStandardCoefficient(COEFFICIENT_ITU_R_BT_601_SCALING);
	Y2RU_SetAlpha(0xFF);

	//Y2RU_SetSendingY(ybr[0].data, ybr[0].stride * ybr[0].height, ybr[0].width, ybr[0].stride - ybr[0].width);
	//Y2RU_SetSendingU(ybr[1].data, ybr[1].stride * ybr[1].height, ybr[1].width, ybr[1].stride - ybr[1].width);
	//Y2RU_SetSendingV(ybr[2].data, ybr[2].stride * ybr[2].height, ybr[2].width, ybr[2].stride - ybr[2].width);

	Y2RU_SetSendingY(ybr[0].data, info->width * info->height, info->width, ybr[0].stride - info->width);
	Y2RU_SetSendingU(ybr[1].data, (info->width/2) * (info->height/2), info->width/2, ybr[1].stride - (info->width >> 1));
	Y2RU_SetSendingV(ybr[2].data, (info->width/2) * (info->height/2), info->width/2, ybr[2].stride - (info->width >> 1));

	Y2RU_SetReceiving(wframe->data, info->width * info->height * fmtGetBPP(wframe->fmt), info->width * 8 * fmtGetBPP(wframe->fmt), (nearestPo2(info->width) - info->width) * 8 * fmtGetBPP(wframe->fmt));
	Y2RU_StartConversion();

	// Wait untill we are ready to present the frame
	Y2RU_GetTransferEndEvent(&y2rEvent);
	if(svcWaitSynchronization(y2rEvent, 6e7)) puts("Y2R timed out"); // DEBUG
	//svcWaitSynchronization(y2rEvent, -1);
	//svcWaitSynchronization(y2rEvent, 6e7);
	vframe->curbuf = drawbuf;
	vframe->img.tex = wframe;
}

bool frameDrawAtCentered(TH3DS_Frame* vframe, float x, float y, float depth, float scaleX, float scaleY) {
	C2D_Image img = vframe->img;
	C2D_DrawParams params =
	{
		{ x, y, scaleX*img.subtex->width, scaleY*img.subtex->height },
		{ (scaleX*img.subtex->width)/2.0f, (scaleY*img.subtex->height)/2.0f },
		depth, 0.0f
	};
	return C2D_DrawImage(img, &params, NULL);
}
