#pragma once

#include <cstdint>
#include <string>

struct LoginRequest {
    std::string email;
    std::string password;
};

struct RegisterRequest {
    std::string email;
    std::string password;
    std::string name;
};

struct AuthResponse {
    bool success = false;
    int user_id = 0;
    std::string token;
    std::string message;
};

struct UserProfile {
    int id = 0;
    std::string email;
    std::string name;
    std::string created_at;
};
