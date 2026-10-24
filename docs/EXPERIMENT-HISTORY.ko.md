# 실험과 실패 기록

[English](EXPERIMENT-HISTORY.md) · [개요](../README.ko.md) · [검증 기록](VALIDATION.md)

이 문서는 `0.8.0` 검증에 이르기까지의 실험 과정을 정리합니다. 성공한 결과뿐 아니라 실행 거부, 시간 초과, 원인을 찾지 못했던 진단도 기록했습니다. 각 시도에서 확인한 사실과 그에 따른 다음 조치를 함께 설명합니다.

전체 펌웨어 로그에는 시스템 식별자가 포함돼 있어 공개하지 않습니다. 주요 로그의 SHA-256은 아래에 남겼으며, 개인정보를 제거한 JSON 기록은 [results/experiment-history.json](../results/experiment-history.json)에 있습니다.

## 초기 조사

먼저 읽기 전용 진단으로 카드와 펌웨어를 확인했습니다. PCI 사전 검사, option ROM 캡처, 제한된 BAR0/PROM 읽기, 서로 일치하는 512 KiB PROM 캡처 2회, 반복 레지스터 조사, 낮은 주소 EFI DMA 서비스 검사, 플랫폼/VT-d 조사, Falcon 상태 조사 순서였습니다. 이 조사를 마치기 전에는 전체 언락을 시도하지 않았습니다.

기록은 실제 조사 순서로 배치했습니다. 진단용 버전을 별도로 만들었기 때문에 버전 번호가 오름차순으로 이어지지는 않습니다.

## 실험 기록

### 0.6.2 — 높은 주소 DMA 매핑 거부

펌웨어는 물리 주소 `0x86c000000`에 32 MiB를 할당했지만, `BusMasterRead64 Map()`이 반환한 장치 주소는 낮은 bounce 주소인 `0x84dee000`이었습니다. 동일 주소 매핑이 아니었으며 4 GiB보다 낮았습니다. 프로그램은 GPU 전송 없이 매핑을 해제하고 호스트 canary 값을 검사한 뒤, 페이지를 반환하고 `Unsupported`로 종료했습니다.

- **결과:** 안전한 쓰기 전 거부
- 로그 SHA-256: `af364bdda5b9f4bdbe20d7cc65679802039bb2ce7ac32be801fb284f9260afda`
- **해석:** 4 GiB 위 호스트 메모리 할당만으로 적합한 EFI 장치 주소를 입증할 수 없습니다. `Unsupported`는 이 펌웨어 서비스의 주소 배치를 뜻할 뿐, TU104가 그 물리 주소에 도달할 수 없다는 뜻은 아닙니다.
- **다음 조치:** 플랫폼/IOMMU 상태를 읽기 전용으로 조사하고, 엄격히 제한한 direct-physical 경로는 별도로 검증하기로 했습니다.

### 0.7.0 — 첫 FRTS 시도 중단

앱의 사전 검사는 모두 통과했고 높은 주소 페이지도 준비됐습니다. 사용자가 물리 키 `U`를 눌렀을 때 확인 단계는 `Success`를 출력했습니다. 그러나 실제 core는 `GUARDS` 단계까지만 진입한 뒤 `Security Policy Violation`을 반환했습니다. 첫 GPU 쓰기, GSP reset, 펌웨어 시작은 전혀 일어나지 않았습니다. 페이지가 반환됐으므로 운영체제로 돌아가는 것도 안전했습니다.

- **결과:** active 시도 실패, 변경 전에 안전하게 중단
- 로그 SHA-256: `21e387c6ee56206d7628ecf5cd98d1b86e50ecb3ee46f2ed0e2140433803295c`
- **화면 메시지의 의미:** `Physical one-shot confirmation status=Success`는 키 확인 성공을 뜻합니다. 최종 결과는 저장된 상태값과 마지막 실행 단계로 판단해야 합니다.
- **한계:** 최초 이미지는 실패 단계를 기록했지만 불일치한 레지스터와 값은 기록하지 않았습니다.
- **다음 조치:** 같은 active 이미지를 재시도하지 않고, 정확한 guard 경계 주위에 쓰기 불가능한 읽기 전용 진단을 만들기로 했습니다.

### 0.6.5 — 유휴 상태 검사

읽기 전용 조사에서 카드 고유 descriptor 15 dword, native FWSEC 10,108 dword, 나머지 고정값을 합친 10,197회 비교가 전부 일치했습니다. 정적 펌웨어와 프로필 손상은 배제했지만, 이 검사는 물리 확인/watchdog의 시간 경계 전에 실행돼 0.7.0 실패를 설명하지 못했습니다.

- **결과:** 읽기 전용 진단 성공, 원인 설명에는 불충분
- 로그 SHA-256: `97e3a0b67bdb989f6b13c105c3e2b9e874a5c5ec17fc1f709fcd35be8c5d3f27`
- **해석:** 다른 시점의 상태 기록만으로는 시간에 영향을 받는 guard의 유효성을 입증할 수 없습니다.
- **다음 조치:** 모든 write callback이 도달 불가능한 상태로 confirmation, revalidation, watchdog 순서를 정확히 replay하기로 했습니다.

### 0.6.6 — 읽기 전용 재현으로 원인 확인

0.7.0 순서를 재현하고 실제 core guard helper만 호출했습니다. 0부터 센 32번째 읽기에서 SEC2 `DMACTL` offset `0x0084010c`의 기대값은 `0x1`, 관측값은 `0x7`이었습니다. 차이 mask `0x6`은 읽기 전용 DMEM/IMEM scrubbing 상태 bit이고, 안정된 `REQUIRE_CTX` 비트는 그대로였습니다. GPU 쓰기, reset, 펌웨어 실행은 없었습니다.

- **결과:** 예상된 읽기 전용 guard 거부, 원인 식별
- 로그 SHA-256: `56add48627b1d12cf9edac694acea5f6803b690d7365a72c04fe8274ac4760dc`
- **해석:** 최초 guard가 문서화된 일시 상태 bit를 영구적인 정확한 값처럼 취급했습니다.
- **다음 조치:** bit를 무시한 채 진행하거나 `DMACTL`을 쓰지 않습니다. 문서화된 scrub bit가 없어지기를 제한된 시간 동안 읽기만 하며 기다리는 방식을 검증하기로 했습니다.

### 0.6.7 — scrub 대기 검증

독립적인 읽기 전용 부팅 2회 모두 처음에는 `DMACTL=0x7`, 1 ms 대기 한 번 뒤에는 `0x1`을 관측했습니다. 대기 뒤 변경하지 않은 실제 guard helper가 10,197회 읽기를 모두 승인했습니다. 두 실행 모두 페이지를 반환했고 쓰기 시도는 0회였습니다.

- **결과:** 읽기 전용 검증 2회 성공
- 1차 로그 SHA-256: `0761263ad389b95f5ca1f630b8a385bef38cff466a69f97b72ae6e5d9ae64b69`
- 2차 로그 SHA-256: `e1adab2c1826b33e82084f3cedb32182f8703d77db07af681d8ea1dd099fa0da`
- **해석:** 문서화된 dynamic mask만 기다리고, 그 뒤에는 안정값 전체가 정확히 일치해야 합니다. 시간 초과나 다른 bit 변화는 실패로 처리합니다.
- **다음 조치:** 그 대기를 새로운 일회성 active FRTS 이미지에 넣고, guard의 최종 기대값은 유지하기로 했습니다.

### 0.7.1 — FRTS 성공

검증한 scrub 대기를 적용하자 native signed FWSEC가 7개 단계를 모두 마치고, 높은 물리 주소 할당으로부터 정확한 휘발성 WPR2 경계 `LO=0x01ffee00`, `HI=0x01fffc00`을 만들었습니다.

- **결과:** active FRTS 성공, 아직 Tensor 언락은 아님
- 로그 SHA-256: `a06cb805e73e6df095ffffe0c24750661f102bf83897e5f0685a9d74f274b5c6`
- **해석:** 검증 대상에서 direct high-physical-address native FRTS 경로가 작동했습니다. 이 진단은 변경 뒤 안전하게 부팅을 이어갈 수 없으므로 완전한 전원 차단은 여전히 필수였습니다.
- **다음 조치:** 전체 경로와 합치기 전에 같은 부팅의 Ubuntu handoff를 독립적으로 검증하기로 했습니다.

### 0.7.2 — Ubuntu 부팅 전환 검증

UEFI 앱은 정확히 설치된 GRUB 이미지와 설정을 검증하고 미리 메모리에 읽었습니다. USB 로그를 닫은 다음 부팅 변수나 시스템 ESP를 쓰지 않고 GRUB을 시작했습니다. 7초 뒤 예상한 루트 파일시스템으로 Ubuntu가 올라왔습니다.

- **결과:** GPU 변경 없는 same-boot handoff 성공
- 로그 SHA-256: `9edb1796b4944119fc250a473da28e60854b837758c6803f1f04ac79e0ba0760`
- **다음 조치:** 정확히 완료되고 quiescent 상태인 언락 성공 뒤에만 메모리 반환과 부팅을 허용하는 lifecycle 규칙과 preloaded handoff를 합치기로 했습니다.

### 0.8.0 1차 — 확인 입력 시간 초과

정확한 asset 검사, GRUB preload, live PROM, 고정 32 MiB 할당, layout 준비를 포함한 변경 전 통합 과정은 통과했습니다. 그러나 30초 안에 `U` 입력이 오지 않았습니다. active core에 진입하지 않았고 GPU 쓰기도 없었습니다. 메모리 풀을 반환한 뒤 펌웨어가 Ubuntu를 부팅했습니다.

- **결과:** 안전 취소, 언락 시도에는 해당하지 않음
- 로그 SHA-256: `0475e6dadbcbbbfd0f925111a16cd1868ab73934cc566224214d31ee8bf5d8ad`
- **해석:** 시간 초과와 취소에 대한 자원 관리 규칙이 작동했습니다. 쓰기 이후의 실패와 구분해 기록해야 합니다.

### 0.8.0 2·3차 — 전체 언락 재현

완전한 전원 사이클로 분리한 두 시도 모두 `SS0=0x88888888`, `SS1=0x8`, Booter mailbox 0, stage 13, quiescent cleanup이라는 정확한 최종 상태에 도달했습니다. 그 뒤에만 32 MiB 페이지를 반환하고 미리 읽어 둔 GRUB을 시작했습니다. Linux는 수정하지 않은 드라이버를 로드했고, 관측된 Xid/AER 오류는 없었습니다.

- 2차 시도 성공 로그 SHA-256: `6be6fbc7b08b428334acf7a40cd50fe47ed2aade4e7dca3042ce8e86075f6ea7`
- 3차 시도 성공 로그 SHA-256: `9965b9586e817fd091bdc21b6e0a53adc4eef7590755bf7672ae24313ebb9896`
- 2차 시도의 Tensor 결과: 중앙값 `65.1408 TFLOPS`, `1021.9858 FLOP/SM-cycle`, 정확한 출력, 40/40 SM.
- 완전히 종료한 뒤 USB 없이 정상 부팅한 locked control은 `32.4437 TFLOPS`, `511.6813 FLOP/SM-cycle`이었습니다.
- 3차 시도에서도 `1021.9858 FLOP/SM-cycle`을 재현했고, 실행 시간 기준 TFLOPS는 2차 시도와 1% 미만 차이였습니다.

## 결과를 해석하는 기준

1. **첫 쓰기 전:** lifecycle이 명시적으로 허용할 때만 guard 거부, Escape, timeout 뒤에 소유한 메모리를 반환하고 끝낼 수 있습니다. 로그를 보존하고 원인을 진단하되 guard를 약하게 만들면 안 됩니다.
2. **쓰기 가능성이 생긴 뒤:** 정확하지 않거나 불확실한 상태에서는 backing allocation을 유지하고 정지합니다. 완전히 전원을 끄고, 같은 전원 상태에서 운영체제로 진행하거나 재시도하면 안 됩니다.
3. **성공:** 중간 EFI 호출에 표시된 `Success`만으로는 부족합니다. 정확한 최종 selector, mailbox, stage, quiescence, pool 반환, same-boot handoff, 드라이버 상태, 정확한 계산 결과, 독립적인 locked control이 모두 필요합니다.

0.7.0 실패 이후에는 중간 메시지와 최종 결과를 구분해 판단하는 원칙을 적용했습니다.

## 소스와 빌드의 보존 범위

다음 표는 각 단계의 소스와 빌드 가능 여부를 정리한 것입니다. 하드웨어 실행에는 해당 대상의 프로필, 로컬 USB 검사 조건, [안전 경계](SAFETY.md)가 필요합니다.

| 단계 | 현재 빌드 타깃 | 경계 | 현재 소스 트리 재현성 |
| --- | --- | --- | --- |
| 0.1.0 PCI 사전 검사 | `make` | PCI 설정 읽기 전용 | 가능 |
| 0.2.0 option ROM 캡처 | `make rom-capture` | 펌웨어 소유 ROM 버퍼 읽기 | 가능 |
| 0.3.0 4 KiB MMIO probe | `make mmio-probe` | 제한된 BAR0 읽기 | 가능 |
| 0.4.0 PROM 2회 캡처 | `make prom-capture` | 제한된 BAR0/PROM 읽기 | 가능 |
| 0.5.0 레지스터 조사 | `make register-probe` | 이름이 지정된 MMIO 읽기 | 가능 |
| 0.6.1 낮은 주소 DMA 서비스 | `make dma-map-probe` | EFI Map/Unmap, 장치 전송 없음 | 가능 |
| 0.6.2 높은 주소 DMA 서비스 | `make high-dma-map-probe` | EFI Map/Unmap, 장치 전송 없음 | 가능 |
| 0.6.3 플랫폼 조사 | `make platform-dma-probe` | CPU/chipset/UEFI 읽기 | 가능 |
| 0.6.4 Falcon 조사 | `make falcon-state-probe` | 이름이 지정된 MMIO 읽기 | 가능 |
| 0.6.5 idle guard 조사 | 당시의 과거 소스 | GPU 읽기 전용 | 현재 소스 트리에는 로그/해시만 보존 |
| 0.6.6 정확한 guard replay | 당시의 과거 소스 | GPU 읽기 전용 | 현재 소스 트리에는 로그/해시만 보존 |
| 0.6.7 scrub 대기 replay | `make fwsec-guard-probe` | GPU 읽기 전용 | 가능, 현재 guard 진단 |
| 0.7.0 최초 FRTS 시도 | 당시의 과거 소스 | active 가능, 쓰기 전에 중단 | 로그/해시만 보존, 의도적으로 재도입하지 않음 |
| 0.7.1 대기를 포함한 FRTS | `make fwsec-frts-probe` | GPU를 변경하는 일회성 실행 | 가능, 비공개 PROM fixture 필요 |
| 0.7.2 Ubuntu handoff | `make handoff-probe` | GPU 변경 없음 | 가능 |
| 0.8.0 전체 언락 | `make reproduce-field-build` | GPU를 변경하는 일회성 빌드만 수행 | 비공개 PROM fixture로 동일 바이트 빌드 |

공개 빌드 전에는 `make fetch-build-inputs`를 실행해야 합니다. 읽기 전용 단계는 `test-public`에만 의존합니다. FRTS guard/active와 전체 언락은 정확한 비공개 PROM을 사용하는 core test도 요구합니다. 0.6.5, 0.6.6, 0.7.0 소스는 이 작업 공간에 버전 관리가 생기기 전에 다음 버전으로 덮어써졌습니다. 정확한 로그와 결과의 해시는 남아 있지만, 해당 바이너리는 현재 소스 트리에서 재빌드할 수 없습니다. 문제가 확인된 0.7.0 경로는 빌드 선택지에서 제외했습니다.
