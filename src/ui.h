#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

// ───────── 界面元素：ASCII Logo 与等待回复时的旋转动画 ─────────
inline std::string logo=R"(
██╗     ██╗     ███╗   ███╗       ██████╗██╗  ██╗ █████╗ ████████╗
██║     ██║     ████╗ ████║      ██╔════╝██║  ██║██╔══██╗╚══██╔══╝
██║     ██║     ██╔████╔██║█████╗██║     ███████║███████║   ██║   
██║     ██║     ██║╚██╔╝██║╚════╝██║     ██╔══██║██╔══██║   ██║   
███████╗███████╗██║ ╚═╝ ██║      ╚██████╗██║  ██║██║  ██║   ██║   
╚══════╝╚══════╝╚═╝     ╚═╝       ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝                                                                  
)";
inline void show_logo(){
    std::cout<<logo;
}
inline std::atomic<bool> spinning{false};

// ───────── 旋转动画：等待首个片段时原地打转，收到内容即停 ─────────
inline void start_spin() {
    spinning = true;
    std::thread([]{
        const char spin[] = {'-', '\\', '|', '/'};
        int i = 0;
        while (spinning) {
            std::cout << spin[i % 4] << "\b" << std::flush;
            i++;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        std::cout << " \b" << std::flush;
    }).detach();
}

inline void stop_spin() {
    spinning = false;
}
