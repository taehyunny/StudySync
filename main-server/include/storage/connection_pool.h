// ============================================================================
// connection_pool.h — MariaDB 커넥션 풀
// ============================================================================
// 목적:
//   N개의 DB 커넥션을 미리 생성하여 풀로 관리한다.
//   DbManager, GuiTcpListener, DAO 등이 필요할 때 빌려쓰고 반납한다.
//
// 사용법:
//   auto conn = pool.acquire();   // 커넥션 획득 (없으면 대기)
//   mysql_query(conn, "...");     // SQL 실행
//   pool.release(conn);           // 반납
//
// 스레드 안전성:
//   mutex + condition_variable로 보호. 멀티스레드에서 안전.
// ============================================================================
#pragma once

#include <mariadb/mysql.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace factory {

class ConnectionPool {
public:
    /// @param pool_size 풀에 생성할 커넥션 수 (기본 4)
    ConnectionPool(const std::string& host,
                   const std::string& user,
                   const std::string& password,
                   const std::string& schema,
                   unsigned int port = 3306,
                   int pool_size = 4);
    ~ConnectionPool();

    /// 풀 초기화 — pool_size개의 커넥션을 생성한다
    bool init();

    /// 커넥션 획득 (풀이 비어있으면 대기)
    MYSQL* acquire();

    /// 커넥션 반납
    void release(MYSQL* conn);

    /// 모든 커넥션 닫기
    void shutdown();

private:
    MYSQL* create_connection();

    std::string  host_;
    std::string  user_;
    std::string  password_;
    std::string  schema_;
    unsigned int port_;
    int          pool_size_;

    std::queue<MYSQL*>       pool_;       // 사용 가능한 커넥션 큐
    std::vector<MYSQL*>      all_conns_;  // 생성된 모든 커넥션 (shutdown용)
    std::mutex               mutex_;
    std::condition_variable  cv_;
    bool                     is_shutdown_ = false;
};

/// RAII 커넥션 래퍼 — 스코프 벗어나면 자동 반납
class PooledConnection {
public:
    PooledConnection(ConnectionPool& pool) : pool_(pool), conn_(pool.acquire()) {}
    ~PooledConnection() { if (conn_) pool_.release(conn_); }

    MYSQL* get() const { return conn_; }
    operator MYSQL*() const { return conn_; }

    // 복사 금지
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

private:
    ConnectionPool& pool_;
    MYSQL* conn_;
};

} // namespace factory
