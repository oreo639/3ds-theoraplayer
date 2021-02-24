#include <stdlib.h>
#include <stdbool.h>

#include <3ds.h>

#include "frame.h"

static inline u32 nearestPo2(u32 x)
{
    if (x <= 2)
        return x;

    return 1u << (32 - __builtin_clz(x - 1));
}

static inline size_t fmtGetBPP(GPU_TEXCOLOR fmt)
{
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

int frameInit(C2D_Image* image, THEORA_videoinfo* info) {
	if (!image || !info || y2rInit())
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

	image->tex = malloc(sizeof(C3D_Tex));
	C3D_TexInit(image->tex, nearestPo2(info->width), nearestPo2(info->height), GPU_RGB8);

	Tex3DS_SubTexture* subtex = malloc(sizeof(Tex3DS_SubTexture));

	subtex->width = info->width;
	subtex->height = info->height;
	subtex->left = 0.0f;
	subtex->top = 1.0f;
	subtex->right = (float)info->width/nearestPo2(info->width);
	subtex->bottom = 1.0-((float)info->height/nearestPo2(info->height));
	image->subtex = subtex;

	return 0;
}

void frameDelete(C2D_Image* image) {
	if (!image)
		return;

	Y2RU_StopConversion();

	if (image->tex) {
		C3D_TexDelete(image->tex);
		free(image->tex);
	}

	if (image->subtex)
		free((void*)image->subtex);

	y2rExit();
}

void frameWrite(C2D_Image* frame, THEORA_videoinfo* info, th_ycbcr_buffer ybr) {
	if (!frame || !info)
		return;

	bool is_busy = true;

	Y2RU_StopConversion();
	while (is_busy) {
		Y2RU_IsBusyConversion(&is_busy);
	}

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

	Y2RU_SetReceiving(frame->tex->data, info->width * info->height * fmtGetBPP(frame->tex->fmt), info->width * 8 * fmtGetBPP(frame->tex->fmt), (nearestPo2(info->width) - info->width) * 8 * fmtGetBPP(frame->tex->fmt));
	Y2RU_StartConversion();
}
