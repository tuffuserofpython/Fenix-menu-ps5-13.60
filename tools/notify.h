// Notificacao do sistema PS5 (o balao que aparece no canto).
// sceKernelSendNotificationRequest existe em libkernel_web.so.
#pragma once
#include <stdint.h>
#include <string.h>

extern "C" int sceKernelSendNotificationRequest(int device, void* req, size_t size, int blocking);

namespace gtav {

struct NotificationRequest {
    int32_t type;
    int32_t req_id;
    int32_t priority;
    int32_t msg_id;
    int32_t target_id;
    int32_t user_id;
    int32_t unk1;
    int32_t unk2;
    int32_t app_id;
    int32_t error_num;
    int32_t unk3;
    char    use_icon_image_uri;
    char    message[1024];
    char    icon_uri[1024];
    char    unk[1024];
};

inline void Notify(const char* msg) {
    NotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type              = 0;
    req.target_id         = -1;
    req.use_icon_image_uri = 1;
    strncpy(req.message, msg, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

} // namespace gtav
