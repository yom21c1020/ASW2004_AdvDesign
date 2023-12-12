// for serial comms, push notifications
extern "C" {
    #include "rs232/rs232.c"    // cloned from: gitlab.com/Teuniz/RS-232
}
#include "pushbullet/src/pushbullet.cpp" // cloned from: github.com/11mariom/pushbullet

// basic header files for c/c++
#include <cstdlib>
#include <cstdio>
#include <string>
#include <iostream>

// for file I/O
#include <fstream>

// for time calculation
#include <ctime>
#include <chrono>

// for std::pair<>, std::vector<> containers
#include <utility>
#include <vector>

// for multithread support
#include <mutex>
#include <thread>

// for asynchronous input handling
#include <unistd.h>

// string buffer, pushbullet api token
#define BUF_SIZE 128
#define API_TOKEN "PUSHBULLET_API_TOKEN"

using namespace std;

Pushbullet pb(API_TOKEN);
vector<pair<struct tm*, int>> pilltime;
mutex m;

extern "C" {
    bool InputAvailable() { // If stdin is not empty, returns True.
        fd_set fd;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
        FD_ZERO(&fd);
        FD_SET(STDIN_FILENO, &fd);
        select(STDIN_FILENO + 1, &fd, nullptr, nullptr, &tv);
        auto x = FD_ISSET(STDIN_FILENO, &fd);
        return x > 0;
    }
}
std::pair<int, string> sendSignal(int port, string signal);
void checkCover_worker(int port);
int checkTime(int port);
int parseInput(int port, string input);

int main()
{
    int i = 0;
    int cport_nr = -1; /* /dev/ttyUSB0 */
    int bdrate = 9600; /* 9600 baud */   
    char mode[] = {'8','N','1',0}; // 8 data bits, no parity, 1 stop bit

    fstream in("log.txt", ios::out | ios::trunc);

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    while(1) {   
        std::chrono::duration<double>sec = std::chrono::system_clock::now() - start;
        if(sec.count() > 60) break; // 60초 동안 시리얼 포트를 찾지 못하면 종료
        for(i = 16; i < 38; i++) { // tries all serial port to communicate.
            if(RS232_OpenComport(i, bdrate, mode, 0)) continue;
            else { cport_nr = i; break; }
        }
        if(i == 38) {
            printf("Can not open comport\n");
            printf("Retrying...\n\n");
        } else break;
        usleep(500000);
    }

    if(cport_nr == -1) {
        printf("Timeout Reached, Aborting\n");
        return 0;
    }
    printf("Serial Port Opened: %d\n", cport_nr);
    in << "Serial Port Opened: " << cport_nr << endl << endl;
    in.close();
    usleep(2000000);  /* waits 2000ms for stable condition */

    string input;
    printf(">> ");
    fflush(stdout);
    while(1) { // Main Loop 
        // 현재 시간을 가져오고, 비교 후 약을 뽑을지 비교
        if(InputAvailable()) { // stdin is not empty
            getline(cin, input);
            printf(">> ");
            fflush(stdout);
            int ret = parseInput(cport_nr, input);
            if(ret == -1) {
                break;
            }
        }
        checkTime(cport_nr);
        usleep(500000);  /* sleep for 0.5s */
    }
    RS232_CloseComport(cport_nr);

    return 0;
}


std::pair<int, string> sendSignal(int port, string signal) {
    /*
        시리얼 포트와 보낼 명령을 가지고 아두이노에 명령을 보냅니다.
        return value는 pair<int, string>입니다.

        int sendSignal().first : 0이면 str == ""입니다. 1이면 str != ""입니다. str 파싱이 요구됩니다.
        int sendSignal().second: 파싱할 string입니다.

        멀티스레드에 대응합니다. 공용 뮤텍스 m을 사용합니다.
        만약, 비동기 처리가 필요하다면 활용 가능합니다.
    */
    std::lock_guard<std::mutex> lock(m);
    
    fstream in("log.txt", ios::out | ios::app);

    if(signal[signal.length() - 1] != '\n') signal += '\n';
    
    RS232_cputs(port, signal.c_str());
    signal = signal.substr(0, signal.length() - 1);
    //cout << "Sent to Arduino: " << signal << endl; // using stdin
    in << "Sent to Arduino: " << signal << endl;     // using file
    usleep(500000);

    unsigned char recv[BUF_SIZE];

    int n = RS232_PollComport(port, recv, (int)BUF_SIZE);
    if(n > 0){
        recv[n] = 0;   /* always put a "null" at the end of a string! */
        // printf("Received %i bytes: '%s'\n", n, (char *)recv); // using stdin
        in << "Received " << n << " bytes: ";                    // using file
        in << (char*)recv << endl;
        for(int i = 0; i < n; i++) {
            if(recv[i] == '\n' || recv[i] == '\r') {
                recv[i] = 0;
                break;
            }
            // printf("%x ", recv[i]); // using stdin
            in << recv[i]; // using file
        }
        // cout << endl; // using stdin
        in << endl;     // using file

        in.close();
        string str((char*)recv);
        return make_pair(1, str);
    }
    in.close();
    return make_pair(0, "");
}

void checkCover_worker(int port) {
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    fstream in("log.txt", ios::out | ios::app);

    int sec = timeinfo->tm_sec;
    int min = timeinfo->tm_min;

    // 2초마다 1번, 10초동안 루프
    for(int i = 0; i < 5; i++) {
        auto ret = sendSignal(port, "checkCover");
        if(ret.first == 1) {
            if(ret.second == "coverOpened") {
                // cout << "Cover Opened -- Thread Terminated" << endl;
                in << "Cover Opened -- Thread Terminated" << endl;
                in.close();
                return;
            }
        }
        sleep(2);
    }
    // 10초 후에도 열리지 않으면, 계속해서 체크 및 버저 On
    sendSignal(port, "buzzerOn");
    pb.push_note("Alert!", "Check Pills");
    while(1) {
        auto ret = sendSignal(port, "checkCover");
        if(ret.first == 1) {
            if(ret.second == "coverOpened") {
                //cout << "Cover Opened -- Thread Terminated" << endl;
                in << "Cover Opened -- Thread Terminated" << endl;
                in.close();
                return;
            }
        }
        sleep(1);
    }
}

int checkTime(int port) 
{
    /*
        시간을 체크합니다. 0.5초마다 1번 동작합니다.
        localtime()으로 시간을 가져와, 현재 저장된 약 시간과 비교합니다.
        약 시간이 되면, 약을 뽑습니다.
    */
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    for(int i = 0; i < pilltime.size(); i++) {
        int sec = pilltime[i].first->tm_sec;
        int min = pilltime[i].first->tm_min;
        int hour = pilltime[i].first->tm_hour;

        if(timeinfo->tm_sec == sec && timeinfo->tm_min == min && timeinfo->tm_hour == hour) {
            for(int j = pilltime[i].second; j > 0; j--) {
                sendSignal(port, "controlSteppMotor");
                string retStr = sendSignal(port, "lcdON").second;
                if(retStr == "ErrorNeedRefill" || retStr == "runOutOfPills") {
                    cout << "Error: need refill" << endl;
                    break;
                }
                // sleep 0.1 sec
                usleep(100000);
            }
            std::thread t1(checkCover_worker, port);
            t1.detach(); // 스레드 분리
        }
    }
    return 0;
}

int parseInput(int port, string input) 
{
    /*
        input을 받아 Tokenize 한 후, 명령에 따라 제공 시간과 약 개수를 저장합니다.
        Command Type는 다음과 같습니다: hhmmss pillcount / hhmm pillcount / refill
        refill: 리필한 후 실행합니다. 아두이노에 약 개수를 재설정하는 명령을 보냅니다.
        hhmmss pillcount: 해당 시간에 pillcount만큼 돌리도록 설정합니다.

        hhmm pillcount: 위와 같으나, ss = 0으로 설정됩니다.
    */
    if(input == "refill") {
        sendSignal(port, input);
    } 
    if(input == "quit" || input == "exit") {
        return -1;
    }

    if(input.length() == 6) { // hhmm pillcount 포맷
        struct tm* t = new tm();
        t->tm_hour = ((input.c_str()[0] - '0') * 10 + input.c_str()[1] - '0');
        t->tm_min = ((input.c_str()[2] - '0') * 10 + input.c_str()[3] - '0');
        t->tm_sec = 0;
        pilltime.push_back(make_pair(t, (input.c_str()[5] - '0')));
    } else if (input.length() == 8) { // hhmmss pillcount 포맷
        struct tm* t = new tm();
        t->tm_hour = ((input.c_str()[0] - '0') * 10 + input.c_str()[1] - '0');
        t->tm_min = ((input.c_str()[2] - '0') * 10 + input.c_str()[3] - '0');
        t->tm_sec = ((input.c_str()[4] - '0') * 10 + input.c_str()[5] - '0');
        pilltime.push_back(make_pair(t, (input.c_str()[7] - '0')));
    } else return -1;
    return 0;
}
