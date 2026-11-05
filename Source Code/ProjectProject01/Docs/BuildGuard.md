# 빌드 중 소스 편집 방지

## 왜 필요한가

UBT(언리얼 빌드 도구)는 파일 내용이 아니라 수정 시각으로 재컴파일 여부를 정한다. 컴파일러가 헤더나 소스를 이미 읽은 뒤 같은 파일을 저장하면, 옛 내용으로 만든 `.obj`가 파일보다 늦은 시각을 갖게 된다. 그 뒤 빌드는 이 `.obj`를 최신으로 보고 건너뛴다.

2026-11-04에 실제로 이 일이 일어났다. `MultiplayTestPlayerController.h`가 빌드 도중(19:08:35) 저장되었고, 19:09에 끝난 `ProjectInstantReplay.cpp.obj`와 `ProjectOpenObserver.cpp.obj`는 `AuthenticatedMatchId`를 오프셋 0xc48로 읽었다. 컨트롤러 본체(`MultiplayTestPlayerController.cpp.obj`, 20:09)는 같은 필드를 0xc80에 둔다. 튜토리얼에서 마네킹이 생존자를 잡는 순간 `UProjectInstantReplay::Tick`이 깨진 문자열을 복사하다가 접근 위반으로 종료되었다. 모듈의 `.obj`를 모두 지우고 다시 빌드해 해결했고, 다시 빌드한 DLL에서 두 파일 모두 0xc80을 쓰는 것을 확인했다.

이런 문제는 컴파일 오류나 경고 없이 실행 중 크래시로만 나타난다.

## 적용한 장치

1. Claude Code 훅: `Tools/BuildGuard/BlockSourceEditsDuringBuild.ps1`
   - 사용자 설정(`%USERPROFILE%\.claude\settings.json`)의 `PreToolUse` 훅으로 등록했다. Claude 세션이 `ProjectProject01/Source` 아래의 `.h/.hpp/.inl/.cpp/.c/.cs`를 수정하기 직전에 `UnrealBuildTool`, `cl`, `link`, `UbaHost`, `UbaCli`, `clang-cl`, `lld-link` 실행 여부를 확인한다. 실행 중이면 편집을 거부하고 이유를 알린다.
   - 문서, 설정, 다른 폴더의 파일은 막지 않는다. 스크립트 자체가 실패하면 편집을 막지 않는다.
   - 사용자가 에디터나 IDE에서 직접 저장하는 것은 막지 못한다. 이 경우는 2번이 잡는다.
2. 빌드 스크립트: `Tools/BuildGuard/SafeBuild.ps1`
   - 에디터, Live Coding, UBT가 실행 중이면 빌드를 시작하지 않는다.
   - 빌드가 끝나면 빌드 시작 이후 수정된 소스를 찾는다. 있으면 해당 파일에 의존하는 `.obj`(`*.dep.json` 기준)를 지우고 한 번 더 빌드한다. 다시 빌드하는 동안에도 소스가 바뀌면 경고하고 종료 코드 4를 낸다.
   - 사용: `pwsh -File Tools\BuildGuard\SafeBuild.ps1` (기본 `ProjectDFSDEditor Win64 Development`, 로그는 `Saved/Logs/SafeBuild.log`)
3. 운영 규칙
   - 빌드가 도는 동안 소스 파일을 저장하지 않는다. 저장했다면 2번 스크립트로 빌드하거나, 그 파일을 포함하는 `.obj`를 지운 뒤 다시 빌드한다.

## 확인 결과

- 훅: 빌드가 없을 때 소스 편집 통과(종료 코드 0). 이름이 `cl.exe`인 테스트 프로세스가 실행 중일 때 소스 편집 차단(종료 코드 2), 문서 편집 통과. 실제 Claude 세션의 Edit에서도 차단 메시지를 확인했다.
- 빌드 스크립트: 2026-11-05 테마·로비 작업 빌드 6회에 사용, 모두 "빌드 중 바뀐 소스 없음"으로 정상 종료.

## 다른 세션·Codex·다른 사람에게도 적용 (2026-11-05)

처음 장치는 이 PC에서 사용자 계정으로 도는 Claude 세션만 확실히 막았다. 다음을 추가해 누가, 어떤 도구로 빌드하든 같은 검사를 거치게 했다.

4. 빌드 단계 검사: `Tools/BuildGuard/BuildStepGuard.ps1`
   - 세 대상(`ProjectDFSD`, `ProjectDFSDEditor`, `ProjectDFSDServer`)의 `Target.cs`에 `PreBuildSteps`/`PostBuildSteps`로 연결했다. Visual Studio, Rider, 에디터의 컴파일, Claude, Codex, SafeBuild 등 UBT를 거치는 모든 빌드에서 돈다.
   - 빌드 시작 시각을 `Intermediate/BuildGuard/<대상>.start`에 적어 두고, 빌드가 끝나면 그 사이에 저장된 소스를 찾는다. 그 소스를 읽은 `.obj`가 있으면 지우고 `BuildGuard: error: ...`로 이번 빌드만 실패시킨다. 다시 빌드하면 지운 파일이 다시 컴파일되어 정상이 된다.
   - 바뀐 파일을 읽은 `.obj`가 하나도 없으면 안내만 하고 빌드는 성공시킨다.
   - 안전장치: 검사 스크립트 안에서 문제가 생기면 `BuildGuard: warning: check skipped ...`를 출력하고 `Saved/Logs/BuildGuard.log`에 남긴 뒤 빌드는 성공시킨다. 이 경고가 보이면 빌드 결과는 믿어도 되고, 검사 스크립트만 고치면 된다. 스크립트 파일이 없으면(저장소를 일부만 받은 경우) 단계 자체를 조용히 건너뛴다. Windows가 아닌 빌드 호스트에서는 단계를 넣지 않는다.
   - Windows PowerShell 5.1로 실행하므로 PowerShell 7이 없는 PC에서도 동작한다. Live Coding 패치는 이 단계를 거치지 않을 수 있다.
5. 저장소 Claude 훅: 저장소 루트 `.claude/settings.json`. git으로 저장소 위치를 찾아 같은 편집 차단 스크립트를 실행하므로, 이 저장소에서 Claude Code를 쓰는 다른 사람에게도 적용된다. 스크립트가 없으면 아무것도 막지 않는다. 이 PC에서는 사용자 설정의 훅과 함께 두 번 실행될 수 있는데, 둘 다 확인만 하므로 문제는 없다.
6. 규칙 파일: 저장소 루트 `AGENTS.md`(Codex가 읽음)와 `CLAUDE.md`(`AGENTS.md`를 불러옴).

적용 범위 요약:

| 경우 | 편집 차단 | 빌드 단계 검사 |
|---|---|---|
| 이 저장소의 Claude 세션(누구든) | 막음 | 동작 |
| Codex | 규칙 파일로 안내만 | 동작 |
| IDE/에디터에서 사람이 직접 | 막지 않음 | 동작 |
| Live Coding | 막지 않음 | 동작하지 않을 수 있음 |

`Target.cs`, `Tools/BuildGuard`, `.claude/settings.json`, `AGENTS.md`, `CLAUDE.md`는 커밋해야 다른 PC에 적용된다.

빌드 단계 검사 확인(2026-11-05):

- 빌드 도중(컴파일러 실행 중) `ProjectRoguePick.cpp`의 수정 시각을 바꾸자, 빌드 후 단계가 `BuildGuard: error: ...`를 출력하고 해당 `.obj` 1개를 지운 뒤 빌드를 실패시켰다(`Result: Failed`). 이어서 보통 빌드를 하자 그 파일만 다시 컴파일되어 성공했다. `.obj`는 소스에서 만들어지는 중간 파일이라 지워도 잃는 것이 없고, 다음 빌드가 현재 소스로 다시 만든다.
- 매 실행의 판단은 `Saved/Logs/BuildGuard.log`에 한 줄씩 남는다.
- `Target.cs`가 바뀐 뒤 첫 빌드(이 변경을 처음 받은 PC의 첫 빌드 포함)에서는 UBT가 빌드 전 단계를 건너뛰어 시작 기록이 없다. 이때 빌드 후 검사는 빌드를 실행 중인 UnrealBuildTool 프로세스의 시작 시각을 대신 쓴다(`no start record; using UnrealBuildTool start ...`). 빌드 도중 시작 기록을 지우고 소스를 저장한 시험에서 이 방식으로 감지·실패·`.obj` 삭제가 되었고, 다음 빌드는 그 파일만 다시 컴파일해 성공했다.
- 그대로 두는 빈틈(의도적): Live Coding 패치는 이 검사를 거치지 않을 수 있다. 사람이 IDE나 에디터에서 저장하는 것 자체는 막지 않는다. 다만 그 저장이 빌드 도중이었다면 빌드 후 검사가 잡는다. 읽기 전용 속성으로 막는 방법은, 빌드가 중단되면 파일이 잠긴 채 남을 수 있어 쓰지 않는다.
