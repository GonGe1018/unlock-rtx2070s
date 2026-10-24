# RTX 2070 SUPER Tensor 언락

[English](README.md)

Gigabyte RTX 2070 SUPER(TU104, SM75)에서 Tensor 연산 제한을 해제한 실험을 정리한 프로젝트입니다. 소스 코드와 벤치마크 결과, 실패를 포함한 검증 과정을 공개합니다.

UEFI 앱이 운영체제 시작 전에 GPU 상태를 설정하면, 기존 NVIDIA 드라이버가 이를 이어받습니다. 드라이버나 VBIOS를 수정하지 않으며, 전원을 완전히 끄면 원래 상태로 돌아갑니다. 현재까지 검증한 대상은 아래에 명시한 카드와 시스템 한 대입니다.

## 성능 측정 결과

FP16 입력과 FP32 누산을 사용하는 두 가지 벤치마크로 언락 전후를 비교했습니다. Tensor MMA에서는 명령어 처리량을, cuBLAS에서는 행렬곱 처리량을 측정했습니다.

### Tensor MMA — 약 2배

아래 표는 같은 카드, 드라이버, 실행 파일로 상태별 7회씩 측정한 중앙값입니다. 모든 기록에서 장치가 보고한 클록 속성값은 1905 MHz였습니다.

| 상태 | TFLOPS | FLOP/SM-cycle |
| --- | ---: | ---: |
| 잠금 상태 — 일반 콜드 부팅 | 32.4437 | 511.6813 |
| 언락 — 독립 전원 사이클 1 | 65.1408 | 1021.9858 |
| 언락 — 독립 전원 사이클 2 | 64.5049 | 1021.9858 |

첫 번째 언락에서 TFLOPS는 **2.0078배**, SM 사이클당 처리량은 **1.9973배**로 증가했습니다. 전원을 완전히 끈 뒤 다시 언락한 두 번째 실험에서도 `1021.9858 FLOP/SM-cycle`을 재현했습니다. 두 언락 실험의 TFLOPS 차이는 1% 미만이었습니다.

0이 아닌 입력으로 SM75 `mma.sync.aligned.m16n8k8` 연산을 실행했으며, 총 21개 샘플에서 계산 오차는 모두 0이었습니다. 매 샘플에서 40개 SM 전체를 사용했고, 측정 후 NVIDIA Xid나 PCIe AER 오류는 관측되지 않았습니다.

### cuBLAS GEMM — 1.63–1.65배

같은 25,928바이트 실행 파일과 cuBLAS 12.0.2를 사용한 결과입니다.

| 정방 행렬 크기 N | 잠금 상태 TFLOPS | 언락 TFLOPS | 향상 배율 |
| ---: | ---: | ---: | ---: |
| 4096 | 39.094819 | 63.773163 | 1.631243배 |
| 8192 | 39.832639 | 65.676676 | 1.648816배 |

각 크기에서 2초간 워밍업한 뒤 GEMM 30회씩 총 7묶음을 CUDA event로 측정했습니다. 표는 묶음별 처리량의 중앙값이며, 크기별로 확인한 출력 네 지점의 오차는 0이었습니다.

측정 결과는 위 두 작업에 한정됩니다. 실제 응용 프로그램의 성능 향상, 다양한 입력에 대한 정확성, 장시간 안정성은 아직 검증하지 않았습니다. 자세한 측정 조건과 원시 데이터는 [검증 기록](docs/VALIDATION.md)에서 확인할 수 있습니다.

## 검증 환경

| 항목 | 구성 |
| --- | --- |
| GPU | NVIDIA GeForce RTX 2070 SUPER, TU104, 40 SM |
| PCI ID | `10de:1e84`, subsystem `1458:3ff5` |
| PCI 위치 | `0000:01:00.0` |
| VBIOS | `90.04.76.00.D5` |
| 메인보드 | MSI MPG Z390 GAMING EDGE AC (`MS-7B17`, BIOS `A.70`) |
| 운영체제 | Ubuntu 24.04, 커널 `7.0.0-38-generic` |
| NVIDIA 드라이버 | 수정하지 않은 `610.57.04` |
| 실행 이미지 | `0.8.0-TU104-VOLATILE-UNLOCK-CANDIDATE` |

검증에 사용한 EFI의 SHA-256입니다.

```text
053a587ec2127ce1471068eb1a370d97ae2295c56046d5a6d13d3251a54c535e
```

실행 조건은 이 시스템의 펌웨어, 메모리 배치, Ubuntu GRUB 이미지까지 포함합니다. 같은 GPU 모델이라도 다른 구성의 호환성은 확인하지 않았습니다. 빌드와 실행에 앞서 [안전 문서](docs/SAFETY.md)를 확인해 주세요.

## 언락 원리

운영체제가 시작되기 전에 GPU의 휘발성 펌웨어·레지스터 상태를 설정하고, 기존 드라이버가 해당 상태에서 초기화되도록 합니다. 검증된 최종 선택자 값은 `SS0=0x88888888`, `SS1=0x00000008`입니다. 이 상태에서 FP16 입력/FP32 누산 Tensor 처리량이 SM 사이클당 약 512 FLOP에서 1,022 FLOP로 증가했습니다.

호스트는 입력 메모리를 준비하고, 카드 내부 Falcon 펌웨어가 상태를 전환합니다. 호스트가 WPR2나 기능 선택자를 직접 쓰지는 않습니다. SM 개수도 달라지지 않으며, 선택자가 나타내는 내부 정책 전체를 규명한 것은 아닙니다.

<details>
<summary>펌웨어 전환 과정 자세히 보기</summary>

1. UEFI wrapper가 정확한 PCI ID와 subsystem, live PROM, option ROM, 메인보드/펌웨어 상태, memory map, 초기 GSP/SEC2 상태를 검증합니다. GPU 쓰기가 가능해지기 전에 정확한 Ubuntu GRUB 이미지도 검증해 메모리에 미리 읽습니다.
2. Live PROM에서 카드 고유의 signed native FWSEC 이미지를 가져오고, 정확한 32 MiB high-physical-memory pool 하나를 할당합니다. 이 pool에는 native FWSEC staging 영역과 해시로 고정한 production TU10x Booter, signature, V67 payload, boot container, metadata, radix, data 영역이 들어갑니다. Layout은 NVIDIA가 공개한 TU102 WPR metadata 구조를 따르며 실행 전에 전체 경계를 검사합니다.
3. 사용자가 일회성 물리 키 `U`를 누르면, 문서화된 SEC2 IMEM/DMEM scrub 상태 bit만 제한된 시간 동안 기다린 뒤 안정 상태 전체를 다시 검사합니다. 이것이 실패한 `0.7.0` 시도에서 얻은 수정점입니다. 다른 bit가 변하거나 시간이 초과되면 그대로 거부합니다.
4. 카드의 signed native FWSEC가 GSP에서 FRTS 명령을 한 번 실행해 정확한 임시 WPR2 경계를 만들어야 합니다. 이어 production Booter를 SEC2에 적재하고, 준비된 V67/container layout을 가리키는 metadata와 함께 정확히 한 번 실행합니다. 자동 재시도는 없습니다.
5. Booter mailbox가 0이고, selector가 정확히 `SS0=0x88888888`, `SS1=0x00000008`이며, 예상한 PLM과 복원된 WPR2 상태가 일치하고, 두 Falcon이 정지한 채 state machine이 quiescent stage 13에 도달해야만 성공으로 인정합니다.
6. 이 조건을 모두 확인한 뒤에만 32 MiB pool을 반환하고, 미리 읽어 둔 GRUB을 시작합니다. 상태 전환과 Linux 사이에 펌웨어 reset이 없으므로 수정하지 않은 NVIDIA 드라이버가 휘발성 상태를 이어받습니다. 완전히 전원을 껐다 켜면 원래 상태로 돌아갑니다.

첫 GPU 쓰기 이후 결과가 불확실하면 backing memory를 유지한 채 UEFI에서 정지합니다. 이때 warm reboot나 운영체제 부팅을 계속하면 안 되며, 물리적으로 전원을 완전히 꺼야 합니다.

</details>

## 문서와 재현

공개 결과를 다시 계산하거나 검증 과정을 살펴보려면 아래 문서를 참고해 주세요.

| 내용 | 문서 |
| --- | --- |
| 결과 재계산과 재현 범위 | [보고서 재현](docs/REPRODUCING.md) |
| 측정 방법·원시 데이터·해시 | [검증 기록](docs/VALIDATION.md) |
| 실패 원인과 후속 조치 | [실험 기록](docs/EXPERIMENT-HISTORY.ko.md) |
| 의존성과 빌드 방법 | [빌드 안내](docs/BUILD.md) |
| 실행 조건과 중단 기준 | [안전 문서](docs/SAFETY.md) |
| 다른 시스템에서 필요한 검증 | [포팅 조건](docs/PORTING.md) |
| 출처와 제3자 라이선스 | [NOTICE](NOTICE.md) |

다음 명령은 GPU에 접근하지 않고 공개 입력, 호스트 테스트, 측정 결과를 검증합니다.

```sh
make reproduce-public
```

공개 입력은 NVIDIA `610.57.04`와 `xrip/cmp50hx-unlock`의 특정 리비전에 고정돼 있습니다. 전체 대상 테스트에는 두 번의 일치하는 캡처로 검증한 512 KiB PROM을 비공개 테스트 입력으로 제공해야 합니다.

`install-unlock-active` 타깃은 제공하지 않습니다. 공개 결과 검증, 동일한 EFI 바이트 재빌드, 실제 하드웨어 측정은 각각 필요한 조건이 다릅니다. 중간 메시지의 `Success`만으로 언락을 판정하지 않으며, 최종 펌웨어 상태와 드라이버 동작, 계산 결과, 독립적인 잠금 상태 측정을 함께 확인합니다. 자세한 구분은 재현 문서에 정리했습니다.

## 저장소 구성

```text
.
├── README.md / README.ko.md   프로젝트 소개와 측정 결과
├── LICENSE                   자체 기여분의 MIT 라이선스
├── NOTICE.md                 출처와 제3자 라이선스
├── LICENSES/                 제3자 라이선스 원문
├── docs/                     검증·재현·실험 문서
├── include/                  프로필, 메모리 배치, 상태 머신
├── src/                      UEFI 앱과 단계별 진단
├── tests/                    호스트 테스트와 실패 주입 검사
├── scripts/                  감사 스크립트와 외부 입력 다운로드
├── tools/                    분석 도구와 벤치마크 소스
└── results/                  공개 측정 데이터와 해시
```

PROM 원본, 전체 시스템 로그, 외부 레퍼런스 저장소, 미리 빌드한 EFI는 공개 대상에서 제외합니다.

## 라이선스

이 프로젝트의 자체 기여분은 [MIT 라이선스](LICENSE)로 공개합니다. 제3자 코드, 펌웨어와 기타 외부 입력에는 각각의 기존 라이선스가 적용됩니다. 출처와 적용 범위는 [NOTICE](NOTICE.md)를 참고해 주세요.
