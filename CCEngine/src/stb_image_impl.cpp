// 이 매크로를 선언해야 stb_image의 코드가 컴파일됨
#define STB_IMAGE_IMPLEMENTATION

// new 매크로를 언디파인드하여 충돌을 방지
#include "Core/MemoryMacro.h" 
#undef new

#include "stb_image.h"