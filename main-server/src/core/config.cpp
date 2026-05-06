// ============================================================================
// config.cpp — config.json 로더 + 경량 JSON 파서 (의존성 0)
// ============================================================================
// 책임:
//   서버 부팅 시 config.json 을 한 번 로드하고, `network.main_server_host`
//   같은 점표기(dot path) 로 값을 조회할 수 있게 한다. Config::instance()
//   싱글톤을 통해 프로세스 전역에서 공유.
//
// 왜 자체 JSON 파서인가:
//   - 외부 라이브러리(nlohmann/json 등) 의존성 추가 회피 (빌드 단순화).
//   - config.json 스키마가 단순 평탄 객체 + 약간의 중첩뿐 → 완전한 파서 불필요.
//
// 지원 범위:
//   중첩 객체 O, 문자열/숫자/bool/null 값 O, 문자열 배열 O, 객체 배열 O
//   (health_check.targets 처럼 간단한 객체 배열까지 커버)
//
// 미지원:
//   중첩 배열([[1,2],[3,4]]), \u 유니코드 이스케이프, 과학표기법 숫자 등.
//   쓰일 일이 없으므로 의도적으로 제외.
//
// 조회 API:
//   get_str ("network.main_server_host", "default") — 점표기 경로로 문자열
//   get_int ("network.main_server_ai_port", 9000)   — 정수
//   get_bool, get_health_targets 등
// ============================================================================
#include "core/config.h"
#include "core/logger.h"

#include <cctype>    // std::isspace (공백/탭/개행 판정)
#include <cstdlib>   // std::atoi, std::atof (문자열 → 숫자 변환)
#include <fstream>   // std::ifstream (파일 읽기 스트림)
#include <sstream>   // std::ostringstream (문자열 버퍼)

namespace factory {

Config& Config::instance() {
    // Meyers Singleton 패턴:
    //   C++11부터 함수 내 static 변수는 스레드 안전하게 초기화됨 (표준 보장)
    //   첫 호출 시 inst 생성 → 이후 호출은 기존 인스턴스 참조 반환
    //   프로그램 종료 시 자동 소멸 (new/delete 불필요)
    static Config inst;
    return inst;
}

// ── JSON 토크나이저 ─────────────────────────────────────────────────────
namespace {  // 익명 네임스페이스: 이 파일 내부에서만 쓰이는 심볼 (링크 오류 방지)

// 파서 상태를 담는 구조체
// 입력 문자열(src)을 스캔하면서 현재 위치(pos)를 추적
struct Parser {
    const std::string& src;          // 원본 JSON 문자열 참조 (복사 안 함 → 성능)
    std::size_t pos = 0;             // 현재 파싱 위치 (인덱스)

    // explicit: 암시적 형변환 방지 (Parser p = "abc" 같은 실수 방지)
    explicit Parser(const std::string& s) : src(s) {}

    // 공백/탭/개행 건너뛰기
    void skip_ws() {
        // isspace는 char가 signed일 때 음수 → UB이므로 unsigned char로 캐스팅
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
            ++pos;
    }

    // 현재 문자를 "살펴보기"만 (pos는 공백 건너뛴 위치까지는 이동하지만 문자 소비 안 함)
    // 반환: 현재 문자, 끝이면 '\0'
    char peek() {
        skip_ws();
        return pos < src.size() ? src[pos] : '\0';  // 삼항 연산자: 조건 ? 참 : 거짓
    }

    // 기대 문자와 일치하면 소비하고 true 반환, 아니면 false (pos 유지)
    // "if (consume(','))" 같이 체이닝 사용
    bool consume(char c) {
        skip_ws();
        if (pos < src.size() && src[pos] == c) {
            ++pos;       // 문자 소비 (위치 전진)
            return true;
        }
        return false;
    }

    // 문자열 파싱: "..." → 내용 반환
    // 입력: pos가 여는 따옴표(")를 가리킴 (또는 공백)
    // 출력: 닫는 따옴표까지 소비하고 그 내용만 std::string으로 반환
    std::string parse_string() {
        skip_ws();
        // 여는 따옴표 확인
        if (pos >= src.size() || src[pos] != '"') return "";  // 시작이 "가 아니면 빈 문자열
        ++pos;  // 여는 따옴표 소비

        std::string out;  // 결과 버퍼

        // 닫는 따옴표(") 만날 때까지 반복
        while (pos < src.size() && src[pos] != '"') {
            // 이스케이프 처리: \" \\ \n \r \t 등
            if (src[pos] == '\\' && pos + 1 < src.size()) {
                char n = src[pos + 1];  // 백슬래시 다음 문자
                switch (n) {
                    case '"':  out += '"';  break;  // \" → "
                    case '\\': out += '\\'; break;  // \\ → \
                    case 'n':  out += '\n'; break;  // \n → 개행
                    case 'r':  out += '\r'; break;  // \r → CR
                    case 't':  out += '\t'; break;  // \t → 탭
                    default:   out += n;    break;  // 알 수 없는 이스케이프 → 그대로
                }
                pos += 2;  // 백슬래시 + 문자, 2개 소비
            } else {
                // 일반 문자 → 그대로 추가하고 1개 전진
                out += src[pos++];  // 후위 ++: 현재 값 사용 후 증가
            }
        }
        if (pos < src.size()) ++pos;  // 닫는 " 소비
        return out;
    }

    // 원시값(숫자/bool/null)을 문자열 그대로 추출
    // 값 경계는 쉼표, }, ], 공백 중 하나
    // 예: "true" → "true" / "42" → "42" / "null" → "null"
    // 나중에 get_int/get_double/ExtractBool 등에서 재파싱함
    std::string parse_primitive() {
        skip_ws();
        std::string out;
        while (pos < src.size()) {
            char c = src[pos];
            // 값 종료 문자 만나면 중단 (이 문자는 소비하지 않음 → 상위 파서가 처리)
            if (c == ',' || c == '}' || c == ']' ||
                std::isspace(static_cast<unsigned char>(c))) break;
            out += c;
            ++pos;
        }
        return out;
    }
};

} // anonymous

// ── JSON 파서 본체 ─────────────────────────────────────────────────────
// 알고리즘: 명시적 스택 기반 반복 파싱 (재귀 아님 → 스택 오버플로우 안전)
//
// 핵심 아이디어:
//   1. JSON의 중첩 객체를 "점 구분 경로"로 평탄화 (flatten)
//      예: { "net": { "port": 9000 } }  →  values_["net.port"] = "9000"
//   2. DFS(깊이 우선 탐색)처럼 객체에 들어가면 stack에 현재 prefix 푸시
//   3. 객체 닫힐 때 pop → 부모 컨텍스트로 복귀
bool Config::parse(const std::string& json) {
    Parser p(json);                     // 파서 상태 초기화 (pos=0)

    // 최상위는 반드시 객체여야 함 → '{' 소비 실패 시 에러
    if (!p.consume('{')) return false;

    // DFS로 중첩 객체 탐색용 스택 프레임
    struct StackFrame {
        std::string prefix;   // "network.database" 같은 현재까지의 점 구분 경로
        bool is_object;       // 현재 프레임이 객체인지 (미사용이지만 확장 여지)
    };
    std::vector<StackFrame> stack;
    // 최상위 프레임 푸시 (prefix 비어있음 = 루트 레벨)
    stack.push_back({"", true});

    // 현재 스택 맥락에서 키를 완전 경로로 변환하는 람다
    //   [&]: 바깥 스코프 변수들(stack)을 참조로 캡처
    //   예: stack.top.prefix = "network", key = "port"  →  "network.port"
    auto make_key = [&](const std::string& k) -> std::string {
        if (stack.back().prefix.empty()) return k;       // 루트면 키만
        return stack.back().prefix + "." + k;            // 중첩이면 prefix + "." + 키
    };

    // ── 메인 파싱 루프: 스택이 빌 때까지 반복 ──
    while (!stack.empty()) {
        p.skip_ws();                     // 공백 건너뛰기

        // ── 케이스 1: 객체 종료 '}' ──
        if (p.peek() == '}') {
            p.consume('}');               // '}' 소비
            stack.pop_back();             // 한 레벨 위로 (prefix 복원)
            // 부모 컨텍스트의 다음 항목 구분자 '쉼표' 소비 (있으면)
            if (!stack.empty()) p.consume(',');
            continue;                     // 다음 반복
        }

        // ── 케이스 2: 키-값 쌍 파싱 ──
        // 키 문자열 읽기 ("key":)
        std::string key = p.parse_string();
        if (key.empty()) return false;    // 빈 키 = 오류
        if (!p.consume(':')) return false;// 키-값 구분자 ':' 필수

        // 현재 스택 맥락 + 키 → flat 전체 경로
        std::string full_key = make_key(key);
        p.skip_ws();

        // ── 값 타입 분기 (4가지) ──
        if (p.peek() == '{') {
            // ── 값이 중첩 객체인 경우 ──
            p.consume('{');
            // 새 스택 프레임 푸시 → 다음 반복부터 full_key가 prefix로 사용됨
            stack.push_back({full_key, true});
        }
        else if (p.peek() == '[') {
            // ── 값이 배열인 경우 ──
            p.consume('[');
            p.skip_ws();

            // 배열 첫 요소로 타입 판별 (문자열 배열 vs 객체 배열)
            if (p.peek() == '"') {
                // ── 문자열 배열: ["10.", "192."] ──
                std::vector<std::string> arr;
                while (p.peek() != ']') {
                    arr.push_back(p.parse_string());      // 각 문자열 추가
                    if (!p.consume(',')) break;            // 다음 쉼표 없으면 종료
                }
                p.consume(']');
                // arrays_ 맵에 저장. std::move: 복사 없이 소유권 이전 (성능)
                arrays_[full_key] = std::move(arr);

            } else if (p.peek() == '{') {
                // ── 객체 배열: [{"name":"x", "port":80}, {"name":"y", "port":81}] ──
                // 각 객체를 "key.N.field" 형식으로 flatten:
                //   health_check.targets[0].name → values_["health_check.targets.0.name"]
                //   health_check.targets[1].name → values_["health_check.targets.1.name"]
                int idx = 0;                              // 배열 인덱스

                while (p.peek() == '{') {
                    p.consume('{');
                    // 이 객체의 prefix (예: "health_check.targets.0")
                    std::string obj_prefix = full_key + "." + std::to_string(idx);

                    // 객체 내부 필드 순회
                    while (p.peek() != '}') {
                        std::string subkey = p.parse_string();   // 필드 이름
                        if (!p.consume(':')) break;              // : 없으면 오류
                        std::string subval;

                        // 필드 값: 문자열이면 parse_string, 그 외(숫자/bool)는 primitive
                        if (p.peek() == '"') {
                            subval = p.parse_string();
                        } else {
                            subval = p.parse_primitive();
                        }
                        // 전체 경로로 저장 (예: "health_check.targets.0.name" = "ai_inference_1")
                        values_[obj_prefix + "." + subkey] = subval;

                        if (!p.consume(',')) break;     // 다음 필드 구분자
                    }
                    p.consume('}');                      // 객체 닫기
                    idx++;                               // ★ v0.15.5: 카운트 증가를 먼저
                    // v0.15.5 치명 버그 수정:
                    //   이전엔 consume(',') 뒤에서 idx++ → 마지막 객체(',' 없음)는 break 로
                    //   조기 탈출하면서 idx 가 증가 안 됨 → count 가 1 부족한 값으로 저장 →
                    //   get_health_targets() 가 마지막 target(ai_training) 을 무시하던 버그.
                    //   파싱된 values_ 데이터 자체는 있지만 count 가 2 라 접근 불가 상태였음.
                    if (!p.consume(',')) break;          // 다음 객체 없으면 종료
                }
                // 배열 길이를 "xxx.count" 키에 저장 → get_health_targets()가 사용
                values_[full_key + ".count"] = std::to_string(idx);
                p.consume(']');

            } else {
                // ── 빈 배열 또는 숫자 배열 — 현재 미사용 ──
                // 단순히 ']' 나올 때까지 건너뛰기
                while (p.pos < p.src.size() && p.src[p.pos] != ']') ++p.pos;
                p.consume(']');
            }
            // 배열 뒤 쉼표 (있을 수도 없을 수도)
            if (!p.consume(',')) {
                // 쉼표 없으면 부모 객체의 '}' 대기 (다음 반복에서 처리)
            }
        }
        else if (p.peek() == '"') {
            // ── 값이 문자열 ──
            values_[full_key] = p.parse_string();
            p.consume(',');                // 쉼표 (없어도 OK — 마지막 필드)
        }
        else {
            // ── 값이 원시값 (숫자/bool/null) ──
            values_[full_key] = p.parse_primitive();
            p.consume(',');
        }
    }

    return true;                          // 스택 비면 파싱 완료
}

// ── 공개 API ─────────────────────────────────────────────────────────

// 파일 열고 → 전체 내용 읽고 → 파서 실행 → 결과 저장
bool Config::load(const std::string& path) {
    // 파일 입력 스트림 생성. 실패 시 ifs가 false로 평가됨
    std::ifstream ifs(path);
    if (!ifs) {                              // 파일 못 열면
        log_err_main("config 파일 열기 실패 | %s", path.c_str());
        return false;
    }

    // 파일 전체를 한 번에 문자열로 읽기 (idiomatic C++ 방식)
    std::ostringstream ss;
    ss << ifs.rdbuf();                       // rdbuf(): 파일 스트림 버퍼 → ss로 복사
    std::string content = ss.str();          // 완성된 문자열 추출

    // 이전 로드된 데이터 초기화 (재로드 시나리오 대응)
    values_.clear();
    arrays_.clear();
    path_ = path;                            // 디버그용 경로 기록

    // 파싱 실행
    if (!parse(content)) {
        log_err_main("config JSON 파싱 실패 | %s", path.c_str());
        return false;
    }

    // 성공 로그 (%zu: size_t 전용 포맷 지정자, unsigned 큰 정수)
    log_main("config 로드 완료 | %s (%zu 값, %zu 배열)",
             path.c_str(), values_.size(), arrays_.size());
    return true;
}

// 문자열 값 조회 — 키 없으면 default_val 반환
std::string Config::get_str(const std::string& key, const std::string& default_val) const {
    // unordered_map::find: 키가 있으면 iterator, 없으면 end() 반환 (평균 O(1))
    auto it = values_.find(key);
    // 삼항 연산자: it이 end()가 아니면 값 반환, 아니면 기본값
    // it->second = 맵의 value (it->first = key)
    return (it != values_.end()) ? it->second : default_val;
}

// 정수 값 조회 — 내부적으로 문자열을 atoi로 변환
int Config::get_int(const std::string& key, int default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;       // 키 없음
    // std::atoi: "123" → 123. 실패 시 0 반환 (기본값 0이면 구분 불가 단점 있음)
    return std::atoi(it->second.c_str());
}

// 부동소수 값 조회 — atof 변환
double Config::get_double(const std::string& key, double default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    return std::atof(it->second.c_str());              // "3.14" → 3.14
}

// 문자열 배열 조회 — 없으면 빈 vector 반환
std::vector<std::string> Config::get_str_array(const std::string& key) const {
    auto it = arrays_.find(key);
    // std::vector<std::string>{} = 빈 벡터 리터럴 (C++11 이후)
    return (it != arrays_.end()) ? it->second : std::vector<std::string>{};
}

// 헬스체크 타겟 목록을 구조체 배열로 조회
// flat 키 패턴: health_check.targets.{N}.{field} 를 struct로 복원
std::vector<Config::HealthTargetConfig> Config::get_health_targets() const {
    std::vector<HealthTargetConfig> result;

    // 배열 크기 조회 (parse() 시 "xxx.count"로 저장됨)
    int count = get_int("health_check.targets.count");

    for (int i = 0; i < count; ++i) {
        HealthTargetConfig t;
        // base = "health_check.targets.0", "health_check.targets.1" ...
        std::string base = "health_check.targets." + std::to_string(i);

        // 각 필드를 점 경로로 조회
        t.name = get_str(base + ".name");     // "health_check.targets.0.name"
        t.ip   = get_str(base + ".ip");       // "health_check.targets.0.ip"
        t.port = get_int(base + ".port");     // "health_check.targets.0.port"
        result.push_back(t);                   // 결과 배열에 추가
    }
    return result;
}

} // namespace factory
