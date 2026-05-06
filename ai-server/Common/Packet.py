"""Packet.py — TCP 패킷 빌드/파싱 유틸리티

이 파일은 AI 추론 서버가 운용 서버(메인 서버)로 보내는 TCP 패킷을
만들고(build) 해석하는(parse) 기능을 제공한다.

역할 경계:
  PacketBuilder.build_packet  : 송신 전용 (AiServer → MainServer)
  PacketBuilder.parse_json_only: JSON 프레임만 추출 (ACK 응답 파싱용)
  실제 TCP 송수신은 TcpClient 가 담당하며, 이 모듈은 바이트 직렬화만 책임.

MainServer 의 대응 모듈 (참고):
  - 수신: MainServer/src/core/tcp_listener.cpp (동일 프레이밍 규칙)
  - 송신: MainServer/src/handler/ack_sender.cpp, gui_notifier.cpp

패킷 구조 (와이어 포맷):
  [4바이트 헤더: JSON 크기(big-endian)] + [JSON 본문] + [이미지1] + [이미지2] + ...

기본 버전 (단일 이미지):
  헤더: 00 00 00 7B (123바이트)
  JSON: {"protocol_no": 1000, "image_size": 50000, ...}
  이미지: (JPEG 바이너리 50000바이트)

확장 버전 (여러 이미지, NG 결과용):
  헤더: 00 00 00 A5 (165바이트)
  JSON: {
    "protocol_no": 1000,
    "image_size": 50000,          # 원본 이미지
    "heatmap_size": 30000,        # 히트맵 합성 이미지
    "pred_mask_size": 15000,      # 마스크 합성 이미지
    ...
  }
  원본: (JPEG 50000바이트)
  히트맵: (PNG 30000바이트)
  마스크: (PNG 15000바이트)

수신 측은 JSON 안의 _size 필드들을 보고 순서대로 이미지를 읽는다.
크기가 0이면 해당 이미지는 없는 것이다.
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

import json     # JSON 인코딩/디코딩 라이브러리
import struct   # 바이너리 데이터 패킹/언패킹 (4바이트 헤더 만들 때 사용)
from typing import Optional  # Optional: 값이 None일 수 있음을 표현

from Common.Protocol import PROTOCOL_VERSION  # 프로토콜 버전 문자열 ("1.0")


class PacketBuilder:
    """검사 결과 dict를 메인 서버로 전송할 바이트 패킷으로 변환하는 클래스.

    사용 예 (단일 이미지):
        packet = PacketBuilder.build_packet(
            protocol_no=1000,
            body_dict={"station_id": 1, "result": "NG", "score": 0.87},
            inspection_id="station1-20260416-000001",
            image_bytes=jpeg_bytes,
        )

    사용 예 (시각화 포함 — NG 시):
        packet = PacketBuilder.build_packet(
            protocol_no=1000,
            body_dict={...},
            inspection_id="...",
            image_bytes=original_jpeg,
            heatmap_bytes=heatmap_png,       # 원본+히트맵 합성
            pred_mask_bytes=pred_mask_png,   # 원본+마스크 합성
        )
    """

    @staticmethod  # 인스턴스 없이 클래스에서 바로 호출 가능한 메서드
    def build_packet(protocol_no: int,
                     body_dict: dict,
                     inspection_id: Optional[str] = None,
                     request_id: Optional[str] = None,
                     image_bytes: Optional[bytes] = None,
                     heatmap_bytes: Optional[bytes] = None,
                     pred_mask_bytes: Optional[bytes] = None) -> bytes:
        """패킷을 빌드한다. 공통 헤더 필드를 자동으로 추가한다.

        용도:
          추론서버가 메인서버로 보내는 패킷을 생성한다.
          NG 결과 전송 시에는 원본 이미지 + 시각화 2장(히트맵, 마스크)을
          함께 전송하여 클라이언트가 3분할 표시를 할 수 있게 한다.

        Args:
            protocol_no: 메시지 번호 (예: 1000=STATION1_NG)
            body_dict: 메시지별 본문 필드 (station_id, result, score 등)
            inspection_id: 검사 ID (NG 결과 전송 시 필수, OK카운트 등에서는 생략)
            request_id: 요청/응답 매칭용 ID (있으면 넣고, 없으면 생략)
            image_bytes: NG 원본 이미지 JPEG 바이트 (정상이면 None)
            heatmap_bytes: 원본+히트맵 합성 PNG 바이트 (NG 시각화용, 선택)
            pred_mask_bytes: 원본+Pred Mask 합성 PNG 바이트 (NG 시각화용, 선택)

        Returns:
            전송할 바이트열 = [4바이트 헤더] + [JSON] + [원본] + [히트맵] + [마스크]
            (없는 이미지는 생략되며, JSON에 size=0으로 표시됨)
        """
        # 본문 dict를 복사해서 공통 필드를 추가한다 (원본 변경 방지)
        payload = dict(body_dict)

        # 공통 필드 자동 주입
        payload["protocol_no"]      = int(protocol_no)       # 메시지 번호 (정수)
        payload["protocol_version"] = PROTOCOL_VERSION        # 프로토콜 버전 ("1.0")

        # 검사 ID — NG 결과 계열에서는 필수이므로 값이 있으면 추가
        if inspection_id is not None:
            payload["inspection_id"] = inspection_id

        # 요청/응답 매칭 ID — 있으면 추가
        if request_id is not None:
            payload["request_id"] = request_id

        # 이미지 크기 — 수신 측에서 이 값을 보고 이미지를 추가로 읽을지 결정
        # 각 이미지 크기를 JSON에 명시하여 수신 측에서 순서대로 읽게 한다.
        payload["image_size"]     = len(image_bytes) if image_bytes else 0
        payload["heatmap_size"]   = len(heatmap_bytes) if heatmap_bytes else 0
        payload["pred_mask_size"] = len(pred_mask_bytes) if pred_mask_bytes else 0

        # JSON을 UTF-8 바이트로 인코딩 (한국어 등을 그대로 보존하기 위해 ensure_ascii=False)
        json_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")

        # 헤더: JSON의 바이트 크기를 4바이트 big-endian 정수로 패킹
        # ">I" = big-endian unsigned int (4바이트)
        # 예: JSON이 123바이트면 → b'\x00\x00\x00\x7b'
        header = struct.pack(">I", len(json_bytes))

        # 최종 패킷 조립: 헤더 + JSON + 원본 이미지 + 히트맵 + 마스크 (순서 고정)
        # bytearray로 빈 바이트열 시작 (list-like한 바이트 append 가능)
        packet = bytearray(header)
        packet.extend(json_bytes)

        # 각 이미지가 있으면 이어붙이기 (순서 중요: 수신 측도 이 순서로 읽어야 함)
        if image_bytes:
            packet.extend(image_bytes)
        if heatmap_bytes:
            packet.extend(heatmap_bytes)
        if pred_mask_bytes:
            packet.extend(pred_mask_bytes)

        # bytearray → bytes 변환 후 반환 (TCP 소켓이 bytes를 받으므로)
        return bytes(packet)

    @staticmethod
    def parse_json_only(raw_json_bytes: bytes) -> dict:
        """이미지가 없는 응답(ACK 등)의 JSON 부분만 파싱한다.

        Args:
            raw_json_bytes: JSON 부분의 바이트열
        Returns:
            파싱된 dict (예: {"protocol_no": 1001, "ack": true, ...})
        """
        return json.loads(raw_json_bytes.decode("utf-8"))
