#pragma once
#include <jxl/decode.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <new>

struct JxlDecodedImage { uint32_t width = 0, height = 0; std::vector<uint8_t> bgra; };

static inline bool IsJxlPayload(const uint8_t* p, size_t n) {
  static const uint8_t c[] = {0,0,0,0x0c,'J','X','L',' ',0x0d,0x0a,0x87,0x0a};
  return p && ((n >= 2 && p[0] == 0xff && p[1] == 0x0a) || (n >= sizeof(c) && !memcmp(p,c,sizeof(c))));
}

static inline bool DecodeJxlToBgra(const uint8_t* input, size_t input_size, JxlDecodedImage* out) {
  if (!input || !input_size || !out) return false; *out = JxlDecodedImage();
  JxlDecoder* d = JxlDecoderCreate(nullptr); if (!d) return false;
  JxlPixelFormat fmt = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0}; bool ok=false, full=false;
  JxlDecoderSubscribeEvents(d, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE);
  JxlDecoderSetInput(d,input,input_size); JxlDecoderCloseInput(d);
  try { for (;;) {
    JxlDecoderStatus s=JxlDecoderProcessInput(d);
    if (s==JXL_DEC_ERROR || s==JXL_DEC_NEED_MORE_INPUT) break;
    if (s==JXL_DEC_BASIC_INFO) { JxlBasicInfo i{};
      if (JxlDecoderGetBasicInfo(d,&i)!=JXL_DEC_SUCCESS || !i.xsize || !i.ysize || i.have_animation ||
          i.orientation!=1 || i.num_extra_channels>1 || (i.num_extra_channels==1 && !i.alpha_bits) ||
          (uint64_t)i.xsize*i.ysize > SIZE_MAX/4) break;
      out->width=i.xsize; out->height=i.ysize; out->bgra.resize((size_t)i.xsize*i.ysize*4);
    } else if (s==JXL_DEC_NEED_IMAGE_OUT_BUFFER) { size_t n=0;
      if (out->bgra.empty() || JxlDecoderImageOutBufferSize(d,&fmt,&n)!=JXL_DEC_SUCCESS || n!=out->bgra.size() ||
          JxlDecoderSetImageOutBuffer(d,&fmt,out->bgra.data(),n)!=JXL_DEC_SUCCESS) break;
    } else if (s==JXL_DEC_FULL_IMAGE) full=true;
    else if (s==JXL_DEC_SUCCESS) { if (!full) break;
      for(size_t i=0;i<out->bgra.size();i+=4){uint8_t r=out->bgra[i];out->bgra[i]=out->bgra[i+2];out->bgra[i+2]=r;} ok=true; break;
    }
  }} catch(const std::bad_alloc&) { ok=false; }
  JxlDecoderDestroy(d); if(!ok) *out=JxlDecodedImage(); return ok;
}
