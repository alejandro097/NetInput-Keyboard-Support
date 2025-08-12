#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Xinput.h>
#include <ViGEm/Client.h>
#include <stdint.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

#define LISTEN_PORT 60400

SOCKET sock = INVALID_SOCKET;
PVIGEM_CLIENT client = nullptr;
PVIGEM_TARGET pad = nullptr;

void ResetGamepad() {
    if (pad) {
        vigem_target_remove(client, pad);
        vigem_target_free(pad);
        pad = nullptr;
        printf("Gamepad reset.\n");
    }
}

void SendKey(WORD vkCode, BOOL keyDown) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
    if (vkCode == VK_LWIN || vkCode == VK_RWIN)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &input, sizeof(INPUT));
}

int main() {
    CoInitialize(NULL);
    printf("Starting receiver on UDP port %d...\n", LISTEN_PORT);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("Failed to create socket\n");
        WSACleanup();
        return 1;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    client = vigem_alloc();
    if (!client || !VIGEM_SUCCESS(vigem_connect(client))) {
        printf("ViGEm initialization failed\n");
        vigem_free(client);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Receiver ready.\n");

    sockaddr_in sender = {};
    int senderLen = sizeof(sender);
    uint8_t buffer[sizeof(XINPUT_STATE) + 1];

    while (true) {
        int bytes = recvfrom(sock, (char*)buffer, sizeof(buffer), 0, (sockaddr*)&sender, &senderLen);
        if (bytes <= 0) {
            continue;
        }

        if (bytes == 2) {
            WORD vkCode = buffer[0];
            BOOL keyDown = (buffer[1] == 0);
            SendKey(vkCode, keyDown);
        }
        else if (bytes == 1 && buffer[0] == 0xFFu) {
            ResetGamepad();
        }
        else if (bytes == sizeof(XINPUT_STATE) + 1 && buffer[0] == 0) {
            XINPUT_STATE* state = (XINPUT_STATE*)(buffer + 1);

            if (!pad) {
                pad = vigem_target_x360_alloc();
                if (!VIGEM_SUCCESS(vigem_target_add(client, pad))) {
                    printf("Failed to add virtual gamepad.\n");
                    vigem_target_free(pad);
                    pad = nullptr;
                    continue;
                }
                printf("Virtual gamepad connected.\n");
            }
            vigem_target_x360_update(client, pad, *(XUSB_REPORT*)&state->Gamepad);
        }
        else {
            printf("Received unknown packet of size %d\n", bytes);
        }
    }

    ResetGamepad();
    vigem_disconnect(client);
    vigem_free(client);
    closesocket(sock);
    WSACleanup();
    CoUninitialize();
    return 0;
}
