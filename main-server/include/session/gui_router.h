// ============================================================================
// gui_router.h — GUI 클라이언트 요청 라우터
// ============================================================================
// 책임: protocol_no별로 GuiService 메서드를 호출하고 JSON 응답을 전송한다.
// TCP 수신/세션 관리와 분리되어 요청 처리 로직만 담당한다.
// ============================================================================
#pragma once

#include "session/gui_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace factory {

class GuiRouter {
public:
    explicit GuiRouter(GuiService& service);

    // protocol_no에 따라 적절한 핸들러 호출.
    // binary 는 JSON 뒤에 함께 수신된 바이너리(비어있을 수 있음).
    // v0.13.0: RETRAIN_UPLOAD(158) 처럼 바이너리를 동반하는 프로토콜 지원.
    void route(int client_fd, const std::string& remote_addr,
               const std::string& json_request,
               const std::vector<uint8_t>& binary);

private:
    void handle_login(int fd, const std::string& json);
    void handle_register(int fd, const std::string& json);
    void handle_logout(int fd, const std::string& json);
    void handle_inspect_history(int fd, const std::string& json);
    // handle_inspect_image: 이력 항목의 이미지 3장을 디스크에서 읽어 바이너리로 회신 (v0.10+)
    void handle_inspect_image(int fd, const std::string& json);
    void handle_stats(int fd, const std::string& json);
    void handle_model_list(int fd, const std::string& json);
    void handle_retrain(int fd, const std::string& json);
    // v0.13.0: 클라가 올린 학습용 이미지 1장 → 디스크 저장 + 학습서버로 중계
    void handle_retrain_upload(int fd, const std::string& json,
                               const std::vector<uint8_t>& binary);

    // v0.14.0: 검사 pause/resume 요청 → 각 추론서버에 INFERENCE_CONTROL_CMD 중계
    void handle_inspect_control(int fd, const std::string& json);

    // 유틸리티
    static std::string extract_str(const std::string& json, const std::string& key);
    static int extract_int(const std::string& json, const std::string& key);
    static bool send_json(int fd, const std::string& json_body);
    static std::string get_timestamp();
    static std::string escape_json(const std::string& s);

    GuiService& service_;
};

} // namespace factory
