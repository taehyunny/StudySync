// ============================================================================
// config.h — JSON 기반 통합 설정 로더
// ============================================================================
// 목적:
//   프로젝트 루트의 config/config.json을 읽어 모든 설정값을 단일 지점에서
//   제공한다. 하드코딩된 IP/포트/경로/DB 접속정보를 한 곳으로 모은다.
//
// 사용:
//   Config::instance().load("../config/config.json");
//   std::string host = Config::instance().get_str("network.main_server_host");
//   int port        = Config::instance().get_int("network.main_server_ai_port");
//
// JSON 경로 표기:
//   점(.)으로 계층 구분. 예: "network.main_server_host"
//
// 환경변수 오버라이드:
//   get_str()은 설정값을 먼저 확인하고, 특정 환경변수가 있으면 우선 적용.
//   예: TRAIN_HOST가 설정되어 있으면 network.main_server_host보다 우선
// ============================================================================
#pragma once   // 헤더 중복 포함 방지 (같은 헤더가 여러 번 include되어도 1번만 처리)

#include <string>         // std::string (동적 문자열)
#include <vector>          // std::vector (동적 배열)
#include <unordered_map>   // std::unordered_map (해시 기반 key→value 맵, O(1) 조회)

namespace factory {        // 프로젝트 전역 네임스페이스 (다른 라이브러리와 이름 충돌 방지)

class Config {
public:
    /// 싱글톤 인스턴스
    /// 반환값은 참조(&)라서 "Config::instance().load(...)" 처럼 체이닝 가능.
    /// 전역에서 한 번 로드된 설정을 어디서나 공유.
    static Config& instance();

    /// JSON 파일 로드 (프로그램 시작 시 1회 호출)
    /// @return 성공 여부 (false면 파싱/파일 열기 실패)
    bool load(const std::string& path);

    /// 문자열 값 조회 (점 구분 키, 예: "network.main_server_host")
    /// default_val: 키가 없으면 이 값을 반환 (C++ 기본 인자)
    /// const 멤버함수: 이 함수는 객체 상태를 변경하지 않음을 보장 (쓰레드 안전성↑)
    std::string get_str(const std::string& key, const std::string& default_val = "") const;

    /// 정수 값 조회 (atoi 내부 사용, 숫자 파싱 실패 시 0)
    int         get_int(const std::string& key, int default_val = 0) const;

    /// 부동소수 값 조회 (atof 내부 사용)
    double      get_double(const std::string& key, double default_val = 0.0) const;

    /// 문자열 배열 조회 (예: "network.allowed_ip_prefixes")
    /// JSON ["10.", "192.168."] 같은 문자열 배열 반환
    std::vector<std::string> get_str_array(const std::string& key) const;

    /// 헬스체크 타겟 전용 구조체 조회
    /// 객체 배열 [{"name":..., "ip":..., "port":...}]을 타입 안전 구조체로 반환
    struct HealthTargetConfig {
        std::string name;     // 헬스체크 대상 이름 (예: "ai_inference_1")
        std::string ip;       // 대상 IP
        int         port;     // 대상 포트
    };
    std::vector<HealthTargetConfig> get_health_targets() const;

    /// 로드된 JSON 파일 경로 (디버깅/로그용)
    /// 반환값이 const std::string& → 복사 없이 참조만 반환 (성능)
    const std::string& source_path() const { return path_; }

private:
    /// 생성자를 private으로 → 외부에서 new Config() 금지
    /// = default: 컴파일러가 자동 생성 (기본 생성자 유지)
    /// 싱글톤이므로 오직 instance() 안의 static 변수로만 생성됨
    Config() = default;

    // ── flat 저장 구조 ──
    // JSON 중첩 구조를 "점으로 구분된 flat 키"로 펼쳐서 저장:
    //   JSON: { "network": { "port": 9000 } }
    //   values_: { "network.port": "9000" }
    // 이유: get_str("network.port") 한 번의 해시 조회(O(1))로 값 획득 → 빠름
    std::unordered_map<std::string, std::string>              values_;  // 단일 값
    std::unordered_map<std::string, std::vector<std::string>> arrays_;  // 문자열 배열
    std::string                                               path_;    // 원본 파일 경로

    // 최소 JSON 파서 (nested object + array of primitives + array of objects)
    // 지원: 중첩 객체, 문자열/숫자/bool/null 값, 문자열 배열, 객체 배열
    // 미지원: 중첩 배열, 유니코드 이스케이프(\uXXXX)
    bool parse(const std::string& json);
};

} // namespace factory
