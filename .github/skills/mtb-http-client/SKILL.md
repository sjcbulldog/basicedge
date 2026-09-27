---
name: mtb-http-client
description: Add HTTP GET client capability to a ModusToolbox project using cy_secure_sockets. Covers plain HTTP and HTTPS requests, DNS resolution, chunked receive, JSON response parsing with coreJSON, and periodic polling patterns. Use for REST API integration, weather data, cloud services.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# HTTP Client Pattern for ModusToolbox

> **Prerequisite:** WiFi must be connected first. See /wifi-mqtt for WiFi STA setup.

## 1. Required Libraries

Add to `deps/` (or `proj_cm33_ns/deps/` for PSOC Edge):

```
# secure-sockets.mtb (provides cy_socket_* API)
https://github.com/Infineon/secure-sockets#latest-v3.X#$$ASSET_REPO$$/secure-sockets/latest-v3.X
```

For JSON parsing, also add coreJSON (part of AWS IoT SDK, header-only):
```
# core-json — no .mtb needed; copy core_json.h + core_json.c into your source/
```

WiFi prerequisite libraries (if not already present): see /wifi-mqtt.

Run `make getlibs` after adding `.mtb` files.

---

## 2. HTTP GET — Complete Pattern (Plain HTTP, Port 80)

```c
#include "cy_secure_sockets.h"
#include <stdio.h>
#include <string.h>

#define HTTP_PORT            (80U)
#define HTTP_BUF_SIZE        (4096U)    /* Response buffer — size for your payload */
#define SOCKET_TIMEOUT_MS    (10000U)
#define RECV_CHUNK_SIZE      (1460U)    /* TCP MSS — optimal recv chunk */

static char response_buf[HTTP_BUF_SIZE];

/**
 * @brief Perform an HTTP GET and return the response body.
 *
 * @param hostname  Server hostname (e.g., "api.openweathermap.org")
 * @param path      URL path (e.g., "/data/2.5/forecast?zip=78741,US&appid=KEY")
 * @param body_out  Pointer set to start of HTTP body in response_buf
 * @return          Body length, or 0 on failure
 */
size_t http_get(const char *hostname, const char *path, char **body_out)
{
    cy_rslt_t result;
    cy_socket_t socket_handle;
    *body_out = NULL;

    /* 1. DNS resolve */
    cy_socket_ip_address_t ip_addr;
    result = cy_socket_gethostbyname(hostname, CY_SOCKET_IP_VER_V4, &ip_addr);
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTP] DNS failed: 0x%08lx\r\n", (unsigned long)result);
        return 0;
    }

    /* 2. Create TCP socket */
    result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET,
                               CY_SOCKET_TYPE_STREAM,
                               CY_SOCKET_IPPROTO_TCP,
                               &socket_handle);
    if (result != CY_RSLT_SUCCESS) return 0;

    /* 3. Set timeouts */
    uint32_t timeout = SOCKET_TIMEOUT_MS;
    cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_SOCKET,
                          CY_SOCKET_SO_RCVTIMEO, &timeout, sizeof(timeout));
    cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_SOCKET,
                          CY_SOCKET_SO_SNDTIMEO, &timeout, sizeof(timeout));

    /* 4. Connect */
    cy_socket_sockaddr_t addr = {
        .ip_address = ip_addr,
        .port = HTTP_PORT
    };
    result = cy_socket_connect(socket_handle, &addr, sizeof(addr));
    if (result != CY_RSLT_SUCCESS) {
        cy_socket_delete(socket_handle);
        return 0;
    }

    /* 5. Build and send HTTP request */
    char request[384];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "Accept: application/json\r\n"
        "\r\n",
        path, hostname);

    uint32_t bytes_sent = 0;
    result = cy_socket_send(socket_handle, request, (uint32_t)req_len,
                             CY_SOCKET_FLAGS_NONE, &bytes_sent);
    if (result != CY_RSLT_SUCCESS || bytes_sent != (uint32_t)req_len) {
        cy_socket_disconnect(socket_handle, SOCKET_TIMEOUT_MS);
        cy_socket_delete(socket_handle);
        return 0;
    }

    /* 6. Receive response in chunks */
    size_t total = 0;
    while (total < (HTTP_BUF_SIZE - 1)) {
        uint32_t chunk = HTTP_BUF_SIZE - 1 - (uint32_t)total;
        if (chunk > RECV_CHUNK_SIZE) chunk = RECV_CHUNK_SIZE;

        uint32_t received = 0;
        result = cy_socket_recv(socket_handle, response_buf + total,
                                 chunk, CY_SOCKET_FLAGS_NONE, &received);
        if (received > 0) total += received;
        if (result != CY_RSLT_SUCCESS) break;  /* Server closed or timeout */
    }
    response_buf[total] = '\0';

    /* 7. Cleanup socket */
    cy_socket_disconnect(socket_handle, SOCKET_TIMEOUT_MS);
    cy_socket_delete(socket_handle);

    if (total == 0) return 0;

    /* 8. Find HTTP body (after \r\n\r\n header delimiter) */
    char *body = strstr(response_buf, "\r\n\r\n");
    if (!body) return 0;
    body += 4;

    *body_out = body;
    return total - (size_t)(body - response_buf);
}
```

---

## 3. JSON Parsing with coreJSON

```c
#include "core_json.h"

/* Extract a float value from a JSON response */
float json_get_float(char *json, size_t len, const char *query)
{
    char *value = NULL;
    size_t value_len = 0;
    if (JSON_Search(json, len, query, strlen(query),
                    &value, &value_len) == JSONSuccess && value_len > 0) {
        char save = value[value_len];
        value[value_len] = '\0';
        float result = (float)atof(value);
        value[value_len] = save;
        return result;
    }
    return 0.0f;
}

/* Extract a string value (strips quotes) */
bool json_get_string(char *json, size_t len, const char *query,
                     char *out, size_t out_size)
{
    char *value = NULL;
    size_t value_len = 0;
    if (JSON_Search(json, len, query, strlen(query),
                    &value, &value_len) == JSONSuccess && value_len > 0) {
        const char *src = value;
        size_t src_len = value_len;
        if (src_len >= 2 && src[0] == '"' && src[src_len - 1] == '"') {
            src++; src_len -= 2;
        }
        size_t copy = (src_len < out_size - 1) ? src_len : (out_size - 1);
        memcpy(out, src, copy);
        out[copy] = '\0';
        return true;
    }
    return false;
}
```

**coreJSON query syntax:** Nested objects use dots (`city.name`), arrays use brackets (`list[0].main.temp`).

---

## 4. Periodic Polling Pattern (FreeRTOS Task)

```c
#define POLL_INTERVAL_MS   (15 * 60 * 1000)  /* 15 minutes */

void http_poll_task(void *arg)
{
    /* Wait for WiFi to be ready */
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Allocate response buffer from heap (large buffers shouldn't be on stack) */
    char *resp_buf = pvPortMalloc(HTTP_BUF_SIZE);
    configASSERT(resp_buf != NULL);

    for (;;) {
        char *body = NULL;
        size_t body_len = http_get("api.example.com", "/data?key=val", &body);
        if (body_len > 0) {
            /* Parse and use the response */
            float temp = json_get_float(body, body_len, "main.temp");
            printf("[HTTP] Temperature: %.1f\r\n", temp);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
```

---

## 5. Buffer Sizing Guide

| Payload size | `HTTP_BUF_SIZE` | Notes |
|---|---|---|
| Small JSON (<1 KB) | 2048 | Includes ~300 bytes HTTP headers |
| Medium API response | 4096–6144 | Typical REST API responses |
| Large payloads | 8192+ | Watch CM33 NS heap (~190 KB total on PSOC Edge) |

**Heap allocation preferred** over static for large buffers. Use `pvPortMalloc()` in FreeRTOS context.

---

## 6. HTTPS (TLS) — Additional Steps

For HTTPS (port 443), add TLS configuration:

```c
/* Requires mbedtls library — see /wifi-mqtt for setup */
cy_socket_tls_auth_mode_t tls_mode = CY_SOCKET_TLS_VERIFY_NONE;  /* or REQUIRED */
cy_socket_setsockopt(socket_handle, CY_SOCKET_SOL_TLS,
                      CY_SOCKET_SO_TLS_AUTH_MODE, &tls_mode, sizeof(tls_mode));
```

Use port 443 instead of 80. Server certificate validation requires provisioning the root CA — see the `mqtt` library's TLS examples for the pattern.

---

## 7. Common Pitfalls

1. **Response buffer too small** → truncated JSON, parse failures. Size for your largest expected response + HTTP headers (~300 bytes).
2. **Missing `Connection: close`** → server keeps connection open, recv blocks until timeout.
3. **No socket timeout** → task hangs indefinitely if server unreachable.
4. **Stack-allocated response buffer** → stack overflow for buffers > 2 KB. Use heap.
5. **DNS failure after WiFi reconnect** → `cy_socket_gethostbyname` may fail transiently. Retry with backoff.
6. **Forgetting to null-terminate** → `response_buf[total] = '\0'` is essential before string operations.
