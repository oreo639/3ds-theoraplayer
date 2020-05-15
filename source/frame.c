#include <stdlib.h>

#include <3ds.h>

#include "frame.h"

Handle y2rEvent = 0;

static inline u32 Pow2(u32 x)
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
	Y2RU_ConversionParams convSettings;

	if (!image || !info)
		return 1;

	Y2RU_StopConversion();                          
	convSettings.alpha = 0xFF;
	convSettings.unused = 0;
	convSettings.rotation = ROTATION_NONE;
	convSettings.block_alignment = BLOCK_8_BY_8;
	convSettings.input_line_width = info->width;
	convSettings.input_lines = info->height;
	convSettings.standard_coefficient = COEFFICIENT_ITU_R_BT_601;
	if (convSettings.input_lines % 8) {
		convSettings.input_lines += 8 - (convSettings.input_lines % 8);
	}
	switch(info->fmt)
	{
		case TH_PF_420:
			printf(" 4:2:0 video\n");
			convSettings.input_format = INPUT_YUV420_INDIV_8;
			//y2y = convSettings.input_line_width * convSettings.input_lines * 1;
			//y2u = convSettings.input_line_width * convSettings.input_lines / 4 * 1;
			break;
		case TH_PF_422:
			printf(" 4:2:2 video\n");
			convSettings.input_format = INPUT_YUV422_INDIV_8;
			//y2y = convSettings.input_line_width * convSettings.input_lines * 1;
			//y2u = convSettings.input_line_width * convSettings.input_lines / 2 * 1;
			break;
		case TH_PF_444:
			puts("YUV444 is not supported by Y2R");
			break;
		case TH_PF_RSVD:
		default:
			printf(" video\n	(UNKNOWN Chroma sampling!)\n");
			return 2;
	}
	convSettings.output_format = OUTPUT_RGB_24;
	
	Y2RU_SetConversionParams(&convSettings);
	Y2RU_SetTransferEndInterrupt(1);
	Y2RU_GetTransferEndEvent(&y2rEvent);

	image->tex = malloc(sizeof(C3D_Tex));
	C3D_TexInit(image->tex, Pow2(info->width), Pow2(info->height), GPU_RGB8);

	Tex3DS_SubTexture* subtex = malloc(sizeof(Tex3DS_SubTexture));

	subtex->width = info->width;
	subtex->height = info->height;
	subtex->left = 0.0f;
	subtex->top = 1.0f;
	subtex->right = (float)info->width/Pow2(info->width);
	subtex->bottom = 1.0-((float)info->height/Pow2(info->height));
	image->subtex = subtex;

	return 0;
}

void frameDelete(C2D_Image* image) {
	if (!image)
		return;

	if (image->tex) {
		C3D_TexDelete(image->tex);
		free(image->tex);
	}

	if (image->subtex)
		free((void*)image->subtex);

	if(y2rEvent)
		svcCloseHandle(y2rEvent);
}

void frameWrite(C2D_Image* frame, THEORA_videoinfo* info, th_ycbcr_buffer ybr) {
	if (!frame || !info)

	//svcWaitSynchronization(y2rEvent, 1000 * 1000 * 10);
	if(svcWaitSynchronization(y2rEvent, 6e7)) puts("Y2R timed out");
	Y2RU_StopConversion();
	{
		Y2RU_SetSendingY(ybr[0].data, ybr[0].stride * ybr[0].height, ybr[0].width, ybr[0].stride - ybr[0].width);
		Y2RU_SetSendingU(ybr[1].data, ybr[1].stride * ybr[1].height, ybr[1].width, ybr[1].stride - ybr[1].width);
		Y2RU_SetSendingV(ybr[2].data, ybr[2].stride * ybr[2].height, ybr[2].width, ybr[2].stride - ybr[2].width);
	}

	Y2RU_SetReceiving((void *)frame->tex->data, info->width * info->height * fmtGetBPP(frame->tex->fmt),info->width * 8 * fmtGetBPP(frame->tex->fmt), (Pow2(info->width) - info->width) * 8 * fmtGetBPP(frame->tex->fmt));
	Y2RU_StartConversion();
}