# 우편함과 쿠폰 코드 (설계, 2026-11-11)

## 정한 것

| 항목 | 결정 |
| --- | --- |
| 보상 | 크레딧, 뽑기권, 꾸미기 아이템(사건 기록 상점 목록의 테두리·아이콘 등). 한 우편에 함께 담을 수 있다. |
| 우편 기한 | 기본 30일(보낼 때 바꿀 수 있고 "기한 없음"도 가능). 기한이 지나면 받지 않은 보상과 함께 사라진다. |
| 예약 지급 | 운영 도우미의 예약 지급은 예약 시각에 크레딧을 바로 넣지 않고 우편으로 보낸다. 중복 예약 방지는 그대로. 회수: 아직 받지 않은 우편은 지우고, 이미 받은 사람은 잔액이 충분할 때만 한 번에 빼며 한 사람이라도 모자라면 아무것도 회수하지 않는다. |
| 쿠폰 | 공용 코드(예: DFSD2026, 계정당 1번, 전체 사용 횟수·기간 제한)와 1회용 코드 묶음(예: 100개를 만들어 하나씩 나눠 줌, 코드 하나는 한 번만). 받은 보상은 우편함으로 간다. |

## 플레이어

- 로비 아래쪽 휴대폰 버튼 옆 **우편함** 버튼. 안 받은 우편이 있으면 빨간 점.
- 우편 목록(최신순): 제목, 남은 기한, 보상. 하나씩 받기, 모두 받기, 받은 우편 지우기.
- 우편함 위쪽 **쿠폰 입력** 칸. 띄어쓰기·하이픈·대소문자는 무시한다. 결과: "우편함으로 보냈습니다", "이미 사용한 쿠폰입니다", "없는 쿠폰이거나 기간이 지났습니다".
- 잘못된 코드를 1시간에 10번 넘게 넣으면 잠시 막는다(코드 맞히기 방지).
- 이미 가진 꾸미기 아이템이 우편에 있으면 그 아이템은 건너뛴다(나머지 보상은 받는다).

## 운영 도우미

서버 운영 › 콘텐츠·소통 › 사건 기록·보상 › **우편·쿠폰**.
- 우편 보내기: 받는 사람(전체 또는 계정 목록), 제목, 내용, 보상, 기한. 미리 보기(받는 사람 수) 후 보내기. 보낸 묶음 목록과 받은 수, 회수(받지 않은 우편 지우기).
- 쿠폰: 공용 코드 또는 1회용 묶음 만들기(보상·기간·사용 횟수), 목록과 사용 수, 중지, 1회용 코드 내보내기(CSV).
- 보상을 주는 기능이라 소유자 키만 쓸 수 있다(예약 지급과 같음).

## 구현 (2026-11-11)

| 부분 | 파일 |
| --- | --- |
| 우편 서버 | `Tools/ProjectProject01Backend/Game/Mailbox.cs` (`mail_batches`, `mail_messages`; 받기는 크레딧 `case_accounts`, 뽑기권 `case_draw_wallet`, 아이템 `case_inventory`) |
| 쿠폰 서버 | `Tools/ProjectProject01Backend/Game/Coupons.cs` (`coupon_campaigns`, `coupon_codes`, `coupon_redemptions`, `coupon_failures`) |
| 모듈 등록 | `Game/GameModules.cs`의 `MailCouponModule` |
| 예약 지급 | `Platform/LiveOperations.cs` `Apply`: 우편 발송(`reward:{id}`), 회수는 받은 사람 잔액 확인 후 한 번에 처리 |
| 게임 화면 | `Source/.../ProjectMailboxWidget.h/.cpp`, 로비 막대 우편 버튼(`ProjectCaseRecordWidgets.cpp`), 아이콘 `EIcon::Mail` |
| 운영 도우미 | `Tools/ProjectProject01BuildServerHelper/MailCouponAdministrationForm.cs` (사건 기록·보상 묶음의 "우편·쿠폰" 버튼) |
| 검사 | 서버 `Tools/ProjectProject01FeatureChecks/MailCouponChecks.cs`, 게임 `ProjectProject01.Mailbox.Render` |

API: 플레이어 `GET /api/mail`, `POST /api/mail/{id}/claim`, `POST /api/mail/claim-all`, `POST /api/mail/{id}/delete`, `POST /api/coupons/redeem`.
운영 `GET|POST /api/admin/operations/mail`, `POST .../mail/preview`, `POST .../mail/{id}/recall`, `GET|POST /api/admin/operations/coupons`, `GET .../coupons/{id}/codes`, `POST .../coupons/{id}/disable`.
같은 요청 번호(requestId)로 두 번 보내면 한 번만 보낸다. 공용 코드는 중지만 되고 다시 켤 수 없다.
