#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "jxl_decode.h"

// Verified PNG-loader ABI and RVAs. Resolve each address from the loaded
// module base so Windows ASLR remains enabled.
typedef int (__stdcall *ImageInfoFn)(HLOCAL, int, SIZE_T, uint32_t*);
typedef int (__stdcall *ImageFn)(HLOCAL, int, SIZE_T, HLOCAL*, HLOCAL*);
static const uintptr_t kInfoRva = 0x1020;
static const uintptr_t kImageRva = 0x12c0;
static const uint8_t kInfoPrologue[] = {0x55,0x8b,0xec,0x6a,0xff};
static const uint8_t kImagePrologue[] = {0x55,0x8b,0xec,0x6a,0xff};

static HMODULE g_self = nullptr;
static INIT_ONCE g_original_once = INIT_ONCE_STATIC_INIT;
static HMODULE g_original_module = nullptr;
static BOOL CALLBACK LoadOriginal(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH]; DWORD n=GetModuleFileNameW(g_self,path,MAX_PATH);
  if(!n||n>=MAX_PATH)return TRUE;
  while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;
  if(!n||n+24>=MAX_PATH)return TRUE;
  lstrcpyW(path+n,L"LoaderPng.original.dll");
  g_original_module=LoadLibraryW(path);
  return TRUE;
}
static HMODULE OriginalModule() {
  InitOnceExecuteOnce(&g_original_once,LoadOriginal,nullptr,nullptr);
  return g_original_module;
}
static bool ValidPe(HMODULE m) {
  if (!m) return false; const uint8_t* b=(const uint8_t*)m;
  const IMAGE_DOS_HEADER* d=(const IMAGE_DOS_HEADER*)b;
  if (d->e_magic!=IMAGE_DOS_SIGNATURE || d->e_lfanew<=0 || d->e_lfanew>0x1000) return false;
  const IMAGE_NT_HEADERS32* n=(const IMAGE_NT_HEADERS32*)(b+d->e_lfanew);
  return n->Signature==IMAGE_NT_SIGNATURE && n->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
         n->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC && n->OptionalHeader.SizeOfImage>=0x3e000;
}
static ImageInfoFn OriginalInfo() {
  HMODULE m=OriginalModule(); if(!ValidPe(m)) return nullptr; uint8_t* p=(uint8_t*)m+kInfoRva;
  return memcmp(p,kInfoPrologue,sizeof(kInfoPrologue)) ? nullptr : (ImageInfoFn)p;
}
static ImageFn OriginalImage() {
  HMODULE m=OriginalModule(); if(!ValidPe(m)) return nullptr; uint8_t* p=(uint8_t*)m+kImageRva;
  return memcmp(p,kImagePrologue,sizeof(kImagePrologue)) ? nullptr : (ImageFn)p;
}

static bool ReadInput(HLOCAL h,int mode,SIZE_T size,std::vector<uint8_t>* out) {
  if(!h||!out||!size||size>512u*1024u*1024u)return false;
  try{out->resize((size_t)size);}catch(...){return false;}
  if(mode==1){void* p=LocalLock(h);if(!p)return false;memcpy(out->data(),p,(size_t)size);LocalUnlock(h);return true;}
  if(mode==0){LARGE_INTEGER zero={},before={};if(!SetFilePointerEx((HANDLE)h,zero,&before,FILE_CURRENT))return false;
    DWORD got=0;bool ok=ReadFile((HANDLE)h,out->data(),(DWORD)size,&got,nullptr)&&got==size;
    bool restored=SetFilePointerEx((HANDLE)h,before,nullptr,FILE_BEGIN)!=FALSE;return ok&&restored;}
  return false;
}
static bool IsJxlInput(HLOCAL h,int mode,SIZE_T size,std::vector<uint8_t>* input) {
  return ReadInput(h,mode,size,input) && IsJxlPayload(input->data(),input->size());
}

// Claim .png because converted archive entries retain their original names.
// Non-JXL PNG payloads are forwarded to the preserved original loader.
extern "C" __declspec(dllexport) const char* __stdcall GetExec(){return ".png";}
extern "C" __declspec(dllexport) int __stdcall GetImageInfo(HLOCAL h,int mode,SIZE_T size,uint32_t* info){
  std::vector<uint8_t> input; if(!IsJxlInput(h,mode,size,&input)){ImageInfoFn f=OriginalInfo();return f?f(h,mode,size,info):3;}
  if(!info)return 4;JxlDecodedImage image;if(!DecodeJxlToBgra(input.data(),input.size(),&image))return 3;
  info[0]=image.width;info[1]=image.height;info[2]=4;return 0;
}
extern "C" __declspec(dllexport) int __stdcall GetImage(HLOCAL h,int mode,SIZE_T size,HLOCAL* outInfo,HLOCAL* outPixels){
  std::vector<uint8_t> input;if(!IsJxlInput(h,mode,size,&input)){ImageFn f=OriginalImage();return f?f(h,mode,size,outInfo,outPixels):3;}
  if(!outInfo||!outPixels)return 4;*outInfo=nullptr;*outPixels=nullptr;JxlDecodedImage image;
  if(!DecodeJxlToBgra(input.data(),input.size(),&image))return 3;
  HLOCAL hi=LocalAlloc(2,12),hp=LocalAlloc(2,image.bgra.size());if(!hi||!hp){if(hi)LocalFree(hi);if(hp)LocalFree(hp);return 4;}
  uint32_t* meta=(uint32_t*)LocalLock(hi);uint8_t* dst=(uint8_t*)LocalLock(hp);
  if(!meta||!dst){if(meta)LocalUnlock(hi);if(dst)LocalUnlock(hp);LocalFree(hi);LocalFree(hp);return 4;}
  meta[0]=image.width;meta[1]=image.height;meta[2]=4;size_t stride=(size_t)image.width*4;
  for(uint32_t y=0;y<image.height;++y)memcpy(dst+(size_t)(image.height-1-y)*stride,image.bgra.data()+(size_t)y*stride,stride);
  LocalUnlock(hi);LocalUnlock(hp);*outInfo=hi;*outPixels=hp;return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)g_self=instance;return TRUE;}
