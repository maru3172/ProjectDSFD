# 회의 기록 (MeetingRecorder)

회의를 음성과 텍스트로 남기고, 음성을 읽기 쉬운 텍스트로 바꾼 뒤 요약하는 Windows 도구입니다.
어느 프로젝트에도 묶여 있지 않습니다. 이 폴더만 복사하면 다른 프로젝트나 다른 PC에서도 그대로 씁니다.

## 실행

```powershell
dotnet run --project .\MeetingRecorder.csproj
```

다른 PC로 옮길 실행 파일은 다음처럼 만듭니다.

```powershell
dotnet publish .\MeetingRecorder.csproj -c Release -r win-x64 --self-contained true -o .\publish
```

필요한 것: Windows 10/11, .NET 8 SDK(빌드할 때만).

## 기능

| 기능 | 설명 |
| --- | --- |
| 녹음 | 마이크와 PC에서 나는 소리(Discord, Zoom, 브라우저 통화 등)를 따로 녹음합니다. Discord에 따로 설정할 것은 없습니다. |
| 텍스트 기록 | 메모를 쓰고 Enter를 누르면 그 시각과 함께 쓴 그대로 저장됩니다. 녹음 없이 텍스트만 남기는 회의도 됩니다. |
| 중요 표시 | 회의 중 `중요 표시`를 누르거나 메모 앞에 `!`를 붙이면 그 순간이 중요 내용으로 저장되고, 그 앞 20초·뒤 10초에 오간 말이 요약 맨 위에 원문 그대로 실립니다. |
| 텍스트 파일 가져오기 | 복사해 둔 채팅이나 다른 곳에서 쓴 메모(.txt, .md)를 회의 기록에 붙입니다. |
| 음성 → 텍스트 | PC에서 Whisper로 음성을 인식합니다(인터넷·키 불필요, 처음 한 번만 모델을 받음). 말하는 사람과 시각이 붙은 대화문으로 정리하고 로컬 요약까지 바로 저장합니다. 소리 크기로 말소리가 없는 구간을 가려 그 구간에서 Whisper가 지어내는 문장("감사합니다" 등)을 버리고, 앞 문장을 다음 인식에 끌어오지 않아 반복·표류를 막습니다. 인식이 자신 없던 줄은 고치지 않고 "(인식 불확실)"로 표시합니다. |
| 로컬 요약 | 외부 전송 없이 `summary.md`를 만듭니다. 위쪽 "한눈에 보기"(중요 표시, 결정 사항, 할 일, 질문, 자주 나온 말)는 실제로 말하거나 적은 문장을 시각과 함께 그대로 옮긴 것이고, 아래쪽 "전체 내용"은 대화와 메모 전부입니다. 새로 지은 문장은 없습니다. |
| Claude 요약 | "한눈에 보기"만 Claude가 문장으로 씁니다(한 줄 요약, 핵심, 결정, 할 일, 남은 질문). 항목마다 근거 줄의 [시각]을 달게 하고 대화에 없는 내용은 쓰지 않게 합니다. 그 아래 중요 표시와 전체 내용은 원문 그대로 붙습니다. `ANTHROPIC_API_KEY` 환경 변수가 있어야 하며, 보내기 전에 한 번 묻습니다. |
| 내용 보기 | 회의를 고르면 창 오른쪽 아래에 요약(한눈에 보기 → 전체 내용), 대화문, 텍스트 기록 중 고른 것을 보여 줍니다. |

## 저장되는 것

회의마다 저장 위치(기본: 문서\MeetingRecorder) 아래에 `날짜_시각_제목` 폴더가 생깁니다.

| 파일 | 내용 |
| --- | --- |
| `mic.wav` | 내 마이크 |
| `system.wav` | PC 소리(상대방 목소리). PC가 조용한 동안은 무음으로 채워 두 파일의 시간이 맞습니다. |
| `notes.md` | 텍스트 기록 |
| `transcript.md` | 읽기 쉬운 대화문 |
| `transcript.raw.json` | 인식 원본(시각·말한 사람·문장) |
| `summary.md` | 한눈에 보기 + 전체 내용 |
| `meeting.json` | 회의 정보 |

설정과 음성 인식 모델은 `%AppData%\MeetingRecorder`에 있습니다. API 키는 어디에도 저장하지 않습니다.

## 설정 (`%AppData%\MeetingRecorder\settings.json`)

| 항목 | 기본값 | 설명 |
| --- | --- | --- |
| `StorageFolder` | 문서\MeetingRecorder | 회의 저장 위치 (화면에서도 바꿀 수 있음) |
| `WhisperModel` | `small` | `tiny`, `base`, `small`, `medium`, `large-v3-turbo`. 클수록 정확하고 느립니다. |
| `Language` | `ko` | 인식 언어 |
| `MicrophoneSpeaker` / `SystemAudioSpeaker` | 나 / 상대 | 대화문에 표시할 이름 |
| `RecordSystemAudio` | `true` | PC 소리 녹음 여부 |
| `ClaudeModel` | `claude-opus-5-5` | Claude 요약에 쓰는 모델 |

## 한계

- PC 소리는 한 줄로 녹음되어, 상대가 여러 명이면 모두 `상대`로 표시됩니다. 사람마다 나누려면 Discord 봇 녹음이 필요하며, Discord 음성 암호화 이후 봇 수신이 가능한지는 확인이 필요합니다.
- 음성 인식은 CPU에서도 되지만 회의 길이만큼 이상 걸릴 수 있습니다.
- 로컬 요약은 문장을 골라 옮기는 정리이고, 문장을 새로 쓰는 요약은 Claude 요약입니다. Claude 요약도 [시각]으로 원문을 찾아 확인할 수 있습니다.
- 음성 인식 자체가 틀린 단어를 낼 수는 있습니다. 이 도구는 그 단어를 짐작으로 고치지 않고, 녹음 파일(`mic.wav`, `system.wav`)은 손대지 않고 남깁니다.

## 검사

```powershell
dotnet build .\MeetingRecorder.csproj
.\bin\Debug\net8.0-windows\MeetingRecorder.exe --selftest
```

마이크·모델·네트워크 없이 대화문 정리, 무음 판정, 요약 순서(한눈에 보기 → 전체 내용), 중요 표시 원문 복사, 음성 변환, 저장을 검사합니다.
