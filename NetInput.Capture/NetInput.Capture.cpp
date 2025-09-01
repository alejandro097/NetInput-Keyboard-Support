#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <objbase.h>
#include <Windows.h>
#include <XInput.h>
#include <winsock2.h>
#include <iostream>
#include <thread>
#include <chrono>

#include "vmci/vmci_sockets.h"

#pragma comment(lib, "ws2_32.lib")

SOCKET sock = INVALID_SOCKET;
sockaddr_vm addr{};
XINPUT_STATE lastSentInputState;
HHOOK keyboardHook;

void SendResetControllers() {
    uint8_t packet[] = { 0xFFu };
    sendto(sock, (const char*)packet, sizeof(packet), 0, (sockaddr*)&addr, sizeof(addr));
}

void PollController() {
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) != ERROR_SUCCESS) return;
    if (memcmp(&lastSentInputState, &state, sizeof(XINPUT_STATE)) == 0) return;

    uint8_t packet[sizeof(XINPUT_STATE) + 1];
    packet[0] = 0;
    memcpy(packet + 1, &state, sizeof(XINPUT_STATE));
    memcpy(&lastSentInputState, &state, sizeof(XINPUT_STATE));
    sendto(sock, (const char*)packet, sizeof(packet), 0, (sockaddr*)&addr, sizeof(addr));
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pkb = (KBDLLHOOKSTRUCT*)lParam;
        BYTE data[2] = {
            (BYTE)pkb->vkCode,
            (BYTE)((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) ? 1 : 0)
        };
        sendto(sock, (const char*)data, 2, 0, (sockaddr*)&addr, sizeof(addr));

        if (pkb->vkCode == VK_SPACE)
            return CallNextHookEx(NULL, nCode, wParam, lParam);

        return 1;
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    CoInitialize(NULL);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed.\n";
        return -1;
    }

    int af = VMCISock_GetAFValue();
    if (af == -1) {
        std::cout << "VMCI not available.\n";
        WSACleanup();
        return -1;
    }

    sock = socket(af, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cout << "Failed to create socket.\n";
        WSACleanup();
        return -1;
    }

    addr.svm_family = af;
    addr.svm_cid = VMADDR_CID_HOST;
    addr.svm_port = 0;

    SendResetControllers();

    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (!keyboardHook) {
        std::cout << "Failed to install keyboard hook.\n";
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    memset(&lastSentInputState, 0, sizeof(XINPUT_STATE));

    MSG msg;
    while (true) {
        PollController();

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    SendResetControllers();
    UnhookWindowsHookEx(keyboardHook);
    closesocket(sock);
    WSACleanup();
    CoUninitialize();

    return 0;
}
