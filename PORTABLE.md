# PuTTY 0.85 Portable (Windows x64)

수정된 PuTTY 0.85입니다. USB의 쓰기 가능한 폴더에 `putty.exe`를 복사하고
실행하세요. 설치나 별도 런타임 설치가 필요하지 않습니다.

- `putty.ini`: 첫 실행에 생성됩니다. 저장한 세션, 기본 설정, SSH 호스트 키,
  신뢰하는 호스트 인증기관 정보를 담습니다. 세션을 보존하려면 PuTTY 설정
  창에서 세션 이름을 입력하고 **Save**를 누르세요.
- `PUTTY.RND`: SSH 난수 시드입니다. SSH를 사용하면 같은 폴더에 저장됩니다.
- `putty.ini.lock`: 여러 PuTTY 프로세스가 설정을 덮어쓰지 않도록 하는 잠금
  파일입니다. 내용이 없는 파일이며 그대로 두면 됩니다.
- `putty.ini.tmp`: 저장 중에만 사용하는 임시 파일입니다.

현재 작업 디렉토리나 USB 드라이브 문자가 바뀌어도 실행파일 위치를 기준으로
저장합니다. USB를 옮길 때는 `putty.exe`, `putty.ini`, `PUTTY.RND`를 함께
옮기세요. 설정 파일은 평문이며 비밀번호를 저장하지 않습니다. 개인키나 로그의
사용자 지정 절대 경로는 USB 드라이브 문자에 따라 직접 수정해야 합니다.

이 실행파일은 PuTTY 설정을 위해 레지스트리를 읽거나 쓰지 않습니다. 기존
레지스트리 세션은 자동으로 가져오지 않습니다. Windows 점프 목록과 설치된
MIT Kerberos의 레지스트리 검색은 비활성화됩니다. Windows 자체와 시스템 DLL이
내부적으로 수행하는 레지스트리 접근까지 통제하는 것은 아닙니다.

폴더가 쓰기 불가능하거나 설정 파일이 손상되면 오류를 표시합니다. 다른 사용자
폴더나 레지스트리로 저장 위치를 변경하지 않습니다. `putty.exe -cleanup`은
실행파일 옆의 `putty.ini`와 `PUTTY.RND`를 삭제하므로 저장한 세션도 삭제됩니다.

## Windows에서 직접 빌드

Visual Studio 2022의 C++ 데스크톱 빌드 도구 및 CMake가 필요합니다.

```powershell
cmake -S . -B build -A x64 -DPUTTY_PORTABLE=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build --config Release --target putty test_portable_storage test_conf --parallel
.\build\Release\test_conf.exe
.\build\Release\test_portable_storage.exe
```

실행파일: `build/Release/putty.exe`.
저장 테스트는 테스트 실행파일 옆의 설정 파일을 삭제하므로 실제 사용자 설정과
분리된 빌드 디렉토리에서 실행하세요.

포터블 수정은 `windows/portable-file.c`, `windows/portable-storage.c`에 있으며
기존 공통 설정 직렬화와 암호화 코드를 재사용합니다. 이 배포물은 `putty.exe`만
포함합니다. Pageant 등 다른 도구의 포터블 동작을 보장하지 않습니다.
`-DPUTTY_PORTABLE=OFF`로 기존 레지스트리 저장 버전을 빌드할 수 있습니다.

GitHub Actions는 Windows에서 빌드·테스트한 후 레지스트리 API import와 런타임
DLL 의존성을 검사하고, ZIP 및 단독 EXE, SHA256을 릴리스에 게시합니다.
