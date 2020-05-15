#pragma once

#include <citro2d.h>

#include "video.h"

extern Handle y2rEvent;

int frameInit(C2D_Image* image, THEORA_videoinfo* info);
void frameDelete(C2D_Image* image);
void frameWrite(C2D_Image* frame, THEORA_videoinfo* info, th_ycbcr_buffer ybr);
