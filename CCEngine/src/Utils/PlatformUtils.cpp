#include "Utils/PlatformUtils.h"

// 윈도우 API의 쓸데없는 매크로(min, max 등) 충돌 방지
#define NOMINMAX 
#include <windows.h>
#include <commdlg.h> // 파일 다이얼로그 헤더

namespace CCEngine
{
    std::string PlatformUtils::SaveFile(const char* filter, const char* initialDir)
    {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));

        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = NULL; // 현재는 콘솔창/메인창 핸들이 없으므로 NULL 처리
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;

        // OFN_OVERWRITEPROMPT: 덮어쓸 때 "진짜 덮어쓸래?" 경고창 띄워줌
        // OFN_NOCHANGEDIR: 창을 닫아도 엔진의 기본 실행 경로가 바뀌지 않게 고정!
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        // 기본 확장자 세팅 (사용자가 확장자 안 적으면 자동으로 붙여줌)
        ofn.lpstrDefExt = strchr(filter, '\0') + 1;

        if (initialDir != nullptr)
        {
            ofn.lpstrInitialDir = initialDir;
        }
            

        if (GetSaveFileNameA(&ofn) == TRUE)
        {
            return ofn.lpstrFile;
        }
            
        return std::string(); // 취소 누르면 빈 문자열 반환
    }

    std::string PlatformUtils::OpenFile(const char* filter, const char* initialDir)
    {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));

        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (initialDir != nullptr)
        {
            ofn.lpstrInitialDir = initialDir;
        }
            
        if (GetOpenFileNameA(&ofn) == TRUE)
        {
            return ofn.lpstrFile;
        }
            
        return std::string();
    }
}