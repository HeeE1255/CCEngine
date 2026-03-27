#pragma once

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

// 무지성 new 치환 매크로
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif