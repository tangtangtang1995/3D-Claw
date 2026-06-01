// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "services/ai_generation/ai_3d_generation_client.h"
#include "common/http_status.h"

#include <3rd_party/httplib/httplib.h>
#include <3rd_party/json/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

#include <openssl/hmac.h>
#include <openssl/sha.h>

using json = nlohmann::json;

namespace claw_3dgen {
namespace {

constexpr const char* kTencent3DHost = "ai3d.tencentcloudapi.com";
constexpr const char* kTencent3DService = "ai3d";
constexpr const char* kTencent3DVersion = "2025-05-13";
constexpr const char* kTencent3DRegion = "ap-guangzhou";
constexpr const char* kJsonContentType = "application/json; charset=utf-8";
constexpr const char* kHunyuanStatusDone = "DONE";
constexpr const char* kHunyuanStatusFail = "FAIL";
constexpr int kSubmitReadTimeoutSec = 60;
constexpr int kQueryReadTimeoutSec = 30;
constexpr int kDownloadReadTimeoutSec = 120;

std::string trim_copy(const std::string& text) {
    const auto first = std::find_if_not(text.begin(), text.end(),
        [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(text.rbegin(), text.rend(),
        [](unsigned char c) { return std::isspace(c) != 0; }).base();
    return first < last ? std::string(first, last) : std::string();
}

std::string to_lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string sha256_hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()),
           data.size(), hash);
    char buf[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        std::snprintf(buf + i * 2, 3, "%02x", hash[i]);
    return std::string(buf);
}

std::string hmac_sha256_bin(const std::string& key, const std::string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.size(),
         result, &len);
    return std::string(reinterpret_cast<char*>(result), len);
}

std::string hmac_sha256_hex(const std::string& key, const std::string& data) {
    std::string bin = hmac_sha256_bin(key, data);
    char buf[EVP_MAX_MD_SIZE * 2 + 1];
    for (size_t i = 0; i < bin.size(); ++i)
        std::snprintf(buf + i * 2, 3, "%02x",
                      static_cast<unsigned char>(bin[i]));
    return std::string(buf);
}

std::string tc3_sign(const std::string& secret_id,
                     const std::string& secret_key,
                     const std::string& service,
                     const std::string& host,
                     const std::string& action,
                     const std::string& payload,
                     int64_t timestamp) {
    char date_buf[16];
    time_t ts = static_cast<time_t>(timestamp);
    struct tm gm_tm;
#ifdef _WIN32
    gmtime_s(&gm_tm, &ts);
#else
    gmtime_r(&ts, &gm_tm);
#endif
    std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
                  gm_tm.tm_year + 1900, gm_tm.tm_mon + 1, gm_tm.tm_mday);
    const std::string date = date_buf;
    const std::string ts_str = std::to_string(timestamp);
    const std::string canonical_headers =
        "content-type:" + to_lower_ascii(trim_copy(kJsonContentType)) + "\n"
        "host:" + to_lower_ascii(trim_copy(host)) + "\n"
        "x-tc-action:" + to_lower_ascii(trim_copy(action)) + "\n";
    const std::string signed_headers = "content-type;host;x-tc-action";
    const std::string canonical_request =
        "POST\n/\n\n" + canonical_headers + "\n" + signed_headers + "\n" +
        sha256_hex(payload);
    const std::string credential_scope =
        date + "/" + service + "/tc3_request";
    const std::string string_to_sign =
        "TC3-HMAC-SHA256\n" + ts_str + "\n" + credential_scope + "\n" +
        sha256_hex(canonical_request);

    const std::string k_date = hmac_sha256_bin("TC3" + secret_key, date);
    const std::string k_service = hmac_sha256_bin(k_date, service);
    const std::string k_signing = hmac_sha256_bin(k_service, "tc3_request");
    const std::string signature = hmac_sha256_hex(k_signing, string_to_sign);

    return "TC3-HMAC-SHA256 "
           "Credential=" + secret_id + "/" + credential_scope + ", "
           "SignedHeaders=" + signed_headers + ", "
           "Signature=" + signature;
}

httplib::Headers make_tc3_headers(const std::string& secret_id,
                                  const std::string& secret_key,
                                  const std::string& action,
                                  const std::string& payload,
                                  int64_t timestamp) {
    const std::string auth = tc3_sign(secret_id, secret_key,
        kTencent3DService, kTencent3DHost, action, payload, timestamp);
    return {
        {"Authorization", auth},
        {"Content-Type", kJsonContentType},
        {"Host", kTencent3DHost},
        {"X-TC-Action", action},
        {"X-TC-Version", kTencent3DVersion},
        {"X-TC-Timestamp", std::to_string(timestamp)},
        {"X-TC-Region", kTencent3DRegion}
    };
}

bool response_error(const json& response, std::string& error_out) {
    if (!response.is_object() || !response.contains("Response")) {
        error_out = "Missing Response object";
        return true;
    }
    const auto& resp = response["Response"];
    if (!resp.is_object()) {
        error_out = "Invalid Response object";
        return true;
    }
    if (!resp.contains("Error"))
        return false;

    const auto& err = resp["Error"];
    const std::string code = err.value("Code", "");
    const std::string message =
        err.value("Message", "Unknown Tencent Cloud error");
    error_out = code.empty() ? message : code + ": " + message;
    return true;
}

std::string base64_encode(const std::string& raw) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);
    for (size_t i = 0; i < raw.size(); i += 3) {
        const unsigned char a = static_cast<unsigned char>(raw[i]);
        const unsigned char b =
            i + 1 < raw.size() ? static_cast<unsigned char>(raw[i + 1]) : 0;
        const unsigned char c =
            i + 2 < raw.size() ? static_cast<unsigned char>(raw[i + 2]) : 0;
        out += table[a >> 2];
        out += table[((a & 3) << 4) | (b >> 4)];
        out += (i + 1 < raw.size()) ?
            table[((b & 15) << 2) | (c >> 6)] : '=';
        out += (i + 2 < raw.size()) ? table[c & 63] : '=';
    }
    return out;
}

bool make_submit_payload(const Hunyuan3DRequest& request,
                         std::string& payload_out,
                         std::string& error_out) {
    json body;
    if (request.input_mode == HunyuanInputMode::Text) {
        const std::string prompt = trim_copy(request.text_prompt);
        if (prompt.empty()) {
            error_out = "Prompt is empty";
            return false;
        }
        body["Prompt"] = prompt;
    } else {
        const std::string image_path = trim_copy(request.image_path);
        std::ifstream ifs(image_path, std::ios::binary);
        if (!ifs) {
            error_out = "Cannot open image: " + image_path;
            return false;
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        body["ImageBase64"] = base64_encode(oss.str());
    }

    const char* generate_types[] = {"Normal", "LowPoly", "Geometry"};
    const int generate_type = std::max(0, std::min(request.generate_type, 2));
    body["GenerateType"] = generate_types[generate_type];
    if (request.enable_pbr)
        body["EnablePBR"] = true;
    if (request.face_count > 0)
        body["FaceCount"] = request.face_count;

    payload_out = body.dump();
    return true;
}

bool parse_https_url(const std::string& url,
                     std::string& host_out,
                     std::string& path_out) {
    if (url.substr(0, 8) != "https://")
        return false;
    const auto slash = url.find('/', 8);
    host_out = slash != std::string::npos ?
        url.substr(8, slash - 8) : url.substr(8);
    path_out = slash != std::string::npos ? url.substr(slash) : "/";
    return !host_out.empty();
}

bool is_cancelled(const std::atomic<bool>* cancelled) {
    return cancelled && cancelled->load(std::memory_order_acquire);
}

} // namespace

bool Hunyuan3DClient::submit_job(const Hunyuan3DRequest& request,
                                 std::string& job_id_out,
                                 std::string& error_out,
                                 const std::atomic<bool>* cancelled) const {
    if (is_cancelled(cancelled)) {
        error_out = "Cancelled";
        return false;
    }

    const std::string secret_id = trim_copy(request.secret_id);
    const std::string secret_key = trim_copy(request.secret_key);
    if (secret_id.empty() || secret_key.empty()) {
        error_out = "SecretId/SecretKey is empty";
        return false;
    }

    std::string payload;
    if (!make_submit_payload(request, payload, error_out))
        return false;

    const std::string action = "SubmitHunyuanTo3DProJob";
    const int64_t ts = static_cast<int64_t>(std::time(nullptr));
    httplib::Client cli(std::string("https://") + kTencent3DHost);
    cli.set_read_timeout(kSubmitReadTimeoutSec);
    const auto headers =
        make_tc3_headers(secret_id, secret_key, action, payload, ts);
    auto progress = [cancelled](size_t, size_t) {
        return !is_cancelled(cancelled);
    };
    auto res = cli.Post("/", headers, payload, kJsonContentType, progress);
    if (is_cancelled(cancelled)) {
        error_out = "Cancelled";
        return false;
    }
    if (!res || res->status != claw3d::http_status::kOk) {
        error_out = res ? "HTTP " + std::to_string(res->status) :
            "Connection failed";
        return false;
    }

    auto j = json::parse(res->body, nullptr, false);
    if (j.is_discarded()) {
        error_out = "JSON parse error";
        return false;
    }
    if (response_error(j, error_out))
        return false;

    const auto& resp = j["Response"];
    if (!resp.contains("JobId") || !resp["JobId"].is_string()) {
        error_out = "Missing JobId";
        return false;
    }
    job_id_out = resp["JobId"].get<std::string>();
    return true;
}

bool Hunyuan3DClient::query_job(const Hunyuan3DRequest& request,
                                const std::string& job_id,
                                Hunyuan3DQueryResult& result_out,
                                std::string& error_out,
                                const std::atomic<bool>* cancelled) const {
    if (is_cancelled(cancelled)) {
        error_out = "Cancelled";
        return false;
    }

    const std::string secret_id = trim_copy(request.secret_id);
    const std::string secret_key = trim_copy(request.secret_key);
    if (secret_id.empty() || secret_key.empty()) {
        error_out = "SecretId/SecretKey is empty";
        return false;
    }

    json body;
    body["JobId"] = job_id;
    const std::string payload = body.dump();
    const std::string action = "QueryHunyuanTo3DProJob";
    const int64_t ts = static_cast<int64_t>(std::time(nullptr));
    httplib::Client cli(std::string("https://") + kTencent3DHost);
    cli.set_read_timeout(kQueryReadTimeoutSec);
    const auto headers =
        make_tc3_headers(secret_id, secret_key, action, payload, ts);
    auto progress = [cancelled](size_t, size_t) {
        return !is_cancelled(cancelled);
    };
    auto res = cli.Post("/", headers, payload, kJsonContentType, progress);
    if (is_cancelled(cancelled)) {
        error_out = "Cancelled";
        return false;
    }
    if (!res || res->status != claw3d::http_status::kOk) {
        error_out = res ? "HTTP " + std::to_string(res->status) :
            "Connection failed";
        return false;
    }

    auto j = json::parse(res->body, nullptr, false);
    if (j.is_discarded()) {
        error_out = "JSON parse error";
        return false;
    }
    if (response_error(j, error_out))
        return false;

    const auto& resp = j["Response"];
    if (!resp.contains("Status") || !resp["Status"].is_string()) {
        error_out = "Missing Status";
        return false;
    }

    const std::string status = resp["Status"].get<std::string>();
    result_out = Hunyuan3DQueryResult{};
    if (status == kHunyuanStatusDone) {
        result_out.state = HunyuanQueryState::Done;
        if (resp.contains("ResultFile3Ds") && resp["ResultFile3Ds"].is_array()) {
            for (const auto& file : resp["ResultFile3Ds"]) {
                if (file.contains("Url") && file["Url"].is_string())
                    result_out.result_urls.push_back(
                        file["Url"].get<std::string>());
            }
        }
        if (resp.contains("ResultCreditConsumed") &&
            resp["ResultCreditConsumed"].is_number()) {
            result_out.credits_consumed =
                static_cast<int>(resp["ResultCreditConsumed"].get<double>());
        }
    } else if (status == kHunyuanStatusFail) {
        result_out.state = HunyuanQueryState::Error;
        result_out.error_message =
            resp.value("ErrorMessage", "Unknown error");
    } else {
        result_out.state = HunyuanQueryState::Polling;
    }
    return true;
}

bool Hunyuan3DClient::download_url(const std::string& url,
                                   std::string& body_out,
                                   std::string& error_out,
                                   const std::atomic<bool>* cancelled) const {
    if (is_cancelled(cancelled)) {
        error_out = "Cancelled";
        return false;
    }

    std::string host;
    std::string path;
    if (!parse_https_url(url, host, path)) {
        error_out = "Invalid URL";
        return false;
    }

    httplib::Client cli("https://" + host);
    cli.set_read_timeout(kDownloadReadTimeoutSec);
    auto progress = [cancelled](size_t, size_t) {
        return !is_cancelled(cancelled);
    };
    auto res = cli.Get(path.c_str(), progress);
    if (is_cancelled(cancelled)) {
        error_out = "Cancelled";
        return false;
    }
    if (!res || res->status != claw3d::http_status::kOk) {
        error_out = res ? "Download HTTP " + std::to_string(res->status) :
            "Download failed";
        return false;
    }
    body_out = std::move(res->body);
    return true;
}

} // namespace claw_3dgen
