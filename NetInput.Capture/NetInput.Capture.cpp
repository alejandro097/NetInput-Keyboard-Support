#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <Windows.h>
#include <XInput.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <objbase.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

SOCKET sock = INVALID_SOCKET;
sockaddr_in addr;

XINPUT_STATE lastSentInputState;

HHOOK keyboardHook;

void SendResetControllers() {
    uint8_t packet[] = { 0xFFu };
    sendto(sock, (const char*)packet, sizeof(packet), 0, (sockaddr*)&addr, sizeof(addr));
}

void PollController() {
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) != ERROR_SUCCESS)
        return;

    if (memcmp(&lastSentInputState, &state, sizeof(XINPUT_STATE)) == 0)
        return;

    uint8_t packet[sizeof(XINPUT_STATE) + 1];
    packet[0] = 0;
    memcpy(packet + 1, &state, sizeof(XINPUT_STATE));
    memcpy(&lastSentInputState, &state, sizeof(XINPUT_STATE));

    sendto(sock, (const char*)packet, sizeof(packet), 0, (sockaddr*)&addr, sizeof(addr));
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pkb = (KBDLLHOOKSTRUCT*)lParam;
        BYTE data[2] = { (BYTE)pkb->vkCode, (BYTE)((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) ? 1 : 0) };
        sendto(sock, (const char*)data, 2, 0, (sockaddr*)&addr, sizeof(addr));

        if (pkb->vkCode == VK_SPACE)
            return CallNextHookEx(NULL, nCode, wParam, lParam);

        return 1; // Block key locally
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    std::string ip;
    std::ifstream input_file("target.txt");
    if (input_file.is_open()) {
        std::cout << "Reading IP from target.txt...\n";
        ip = std::string((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            std::cout << ip << " is not a valid IP. Please correct target.txt.\n";
            return -1;
        }
    }

    if (ip.empty()) {
        while (true) {
            std::cout << "Enter target IP: ";
            std::cin >> ip;
            if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) == 1)
                break;
            std::cout << ip << " is not a valid IP.\n";
        }
    }

    std::cout << "Target IP: " << ip << "\n";

    CoInitialize(NULL);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed.\n";
        return -2;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cout << "Failed to create socket.\n";
        WSACleanup();
        return -3;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(60400); // Unified port

    SendResetControllers();

    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (!keyboardHook) {
        std::cout << "Failed to install keyboard hook.\n";
        closesocket(sock);
        WSACleanup();
        return -4;
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
