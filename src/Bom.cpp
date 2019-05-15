//
// Created by david on 22-4-19.
//

#include <ctime>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <AssembleConnection.h>
#include "Bom.h"
#include <dirent.h>
#include <sys/stat.h>

Bom::Bom()
        : m_startTime(0), m_duration(0), m_file(""), m_displayError(false), m_userInput{0}, m_userInputActive(false) {

}

void Bom::WaitForArmed() {
    AssembleConnection connection;
    std::vector<int> settings;
    int input[0];
    std::ifstream localSetup("./setup.bom");

    if (localSetup) {
        connection.StartAssembler("./setup.bom");
        m_file = "";
    } else {
        m_display.Safe();
        SEARCH:

        usleep(100 * 1000);
        SearchBomFile(connection, "./setup.bom");

        if (m_file.empty()) goto SEARCH;
    }

    settings = connection.Execute(input, 0);
    if (settings.size() < 6) {
        std::cout << "ERR SETUP" << connection.GetFeedback() << std::endl;
        m_display.Safe();
        goto SEARCH;
    }

    SetCountdown(settings[0], settings[1], settings[2]);
    SetStartTime(settings[3], settings[4], settings[5]);
    std::cout << "Setup done" << std::endl;
}

void Bom::WaitForCountdown() {
    int ms = -1;
    int buffer[8] = {0};
    bool toggle = false;
    m_start = time(nullptr);
    m_start -= m_start % 86400; //Remove hours
    m_start -= m_start % 3600;  //Remove minutes
    m_start -= m_start % 60;    //Remove seconds
    m_start += m_startTime;
    m_display.SetLed(Peripherals::Display::STATUS, BLACK);


    while (time(nullptr) <= m_start) {
        if ((time(nullptr) & 0x01) == 0x01) {
            toggle = true;
        } else if (toggle) {
            toggle = false;
            m_display.SetLed(Peripherals::Display::ARMED, RED);
        } else {
            m_display.SetLed(Peripherals::Display::ARMED, BLACK);
        }

        if ((time(nullptr) & 0x03) == 0x03) {
            ExtractTime(buffer, m_start + 60 * 60 * 2, ms);

            for (int i = 0; i < 6; ++i) {
                std::cout << buffer[i] << ".";
                buffer[i] = m_display.To7Segment(buffer[i]);
            }
            std::cout << std::endl;
            m_display.DisplaySegments(buffer);
        } else {
            m_display.Clear();
        }

        usleep(1000 * 100);
    }
    std::cout << "START" << std::endl;
}

void Bom::CountDown() {
    time_t end = m_start + m_duration;
    std::thread displayer(&Bom::DisplayUpdater, this, end);

    m_display.SetLed(Peripherals::Display::STATUS, BLACK);
    while (time(nullptr) < end) {
        if (m_keypad.Check()) {
            m_display.SetLed(Peripherals::Display::STATUS, BLUE);
            m_userInputActive = true;
            ReadKeyPad();
        }
        usleep(1 * 1000 * 1000);
    }
    std::cout << "BIEM" << std::endl;

    system("rm ./setup.bom");
    m_duration = 0;
    displayer.join();
}

void Bom::SetCountdown(int h, int m, int s) {
    m_duration = h * 3600 + m * 60 + s;
}

void Bom::SetStartTime(int h, int m, int s) {
    m_startTime = h * 3600 + m * 60 + s;
}

void Bom::DisplayUpdater(time_t end) {
    AssembleConnection connection;
    int buffer[8] = {0};
    int ms = 99;

    while (m_duration > 0) {
        SearchBomFile(connection);
        ExtractTime(buffer, end - time(nullptr), ms);

        if (ms > 90) {
            m_display.SetLed(Peripherals::Display::ARMED, RED);
        } else {
            m_display.SetLed(Peripherals::Display::ARMED, BLACK);
        }

        if (m_userInputActive) {
            m_display.DisplayUserInputs();
        } else {
            connection = DisplayTime(connection, buffer);
        }
        std::cout << "File: " << m_file << std::endl;

        usleep(10 * 1000);
        ms--;
    }
}

int *Bom::ExtractTime(int *buffer, int time, int &ms) {
    static int prevSec = 99;
    int sec, min, hour, offset;

    sec = time % 60;
    time /= 60;

    min = time % 60;
    time /= 60;

    hour = time % 24;


    if (prevSec != sec) {
        prevSec = sec;
        ms = 99;
    }

    if (hour == 0) {
        offset = 2;

        buffer[0] = ms % 10;
        buffer[1] = (ms / 10) % 10;
    } else {
        offset = 0;

        buffer[4] = hour % 10;
        hour /= 10;
        buffer[5] = hour % 10;
    }


    buffer[offset + 0] = sec % 10;
    sec /= 10;
    buffer[offset + 1] = sec % 10;

    buffer[offset + 2] = min % 10;
    min /= 10;
    buffer[offset + 3] = min % 10;


    buffer[6] = ms % 10;
    buffer[7] = (ms / 10) % 10;

    return buffer;
}

void Bom::SearchBomFile(AssembleConnection &connection, const std::string &moveTo) {
    std::string file = SearchInDir("/media/");
    struct stat buffer;

    if (file == m_file && !m_file.empty()) {
        m_file = file;
        return;
    } else if (file.empty() && stat(moveTo.c_str(), &buffer) == 0) {
        connection.StartAssembler(moveTo);
        m_file = moveTo;
        return;
    }

    m_file = file;
    m_displayError = false;

    system(("cp " + file + " " + moveTo).c_str());
    connection.StartAssembler(moveTo);
}

std::string Bom::SearchInDir(const std::string &dirName) {
    DIR *d;
    struct dirent *dir;
    std::string name = "";

    d = opendir(dirName.c_str());
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_name[0] == '.') continue;
            if (dir->d_name[0] == 'D' || dir->d_name[0] == 'O') continue;

            if ((dir->d_type & DT_DIR) == DT_DIR) {
                name = SearchInDir(dirName + dir->d_name + "/");
                if (!name.empty()) {
                    closedir(d);
                    return name;
                }
            } else if (HasExtension(dir->d_name)) {
                closedir(d);
                return dirName + dir->d_name;
            }
        }
        closedir(d);
    }

    return name;
}

void Bom::AssembleError(AssembleConnection &connection) {
    if (!m_displayError && !m_file.empty()) {
        std::string feedback = connection.GetFeedback();
        if (feedback.empty()) {
            m_displayError = true;
            return;
        }

        std::ofstream file(m_file);
        if (!file)
            return;

        file << feedback;
        std::cout << feedback << std::endl;

        file.close();
        m_displayError = true;
    }
}

int *Bom::Encrypt(int *data, int *buffer) {
    buffer[0] = data[0] + data[6];
    buffer[1] = data[1] * 3 + 32 + data[6];

    buffer[2] = data[2] * 3 + 32 + data[6];
    buffer[3] = data[3] * 3 + 32 + data[7];
    buffer[4] = data[4] * 3 + 32 + data[6];
    buffer[5] = data[5] * 3 + 32 + data[7];

    buffer[6] = -data[6];
    buffer[7] = -data[7];

    return buffer;
}

bool Bom::HasExtension(char *filename) {
    std::string name(filename);
    return name.size() > 3 && name.compare(name.size() - 3, 3, "bom") == 0;
}

AssembleConnection &Bom::DisplayTime(AssembleConnection &connection, int *buffer) {
    int encrypted[8] = {0};
    std::vector<int> processed;

    for (int i = 0; i < 6; ++i) {
        buffer[i] = m_display.To7Segment(buffer[i]);

        Encrypt(buffer, encrypted);
        processed = connection.Execute(encrypted, 8);
    }

    if (processed.empty()) {
        AssembleError(connection);
    }

    for (int j = processed.size(); j < 6; ++j) {
        processed.push_back(encrypted[j]);
    }
    m_display.DisplaySegments(processed.data());
    return connection;
}

void Bom::ReadKeyPad() {
    enum Peripherals::Keypad::Key pressed = m_keypad.GetKey();
    switch (pressed) {
        case Peripherals::Keypad::NONE:
            break;
        case Peripherals::Keypad::KEY1:
            m_display.SetUserInput(Peripherals::Display::To7Segment(1));
            break;
        case Peripherals::Keypad::KEY2:
            m_display.SetUserInput(Peripherals::Display::To7Segment(2));
            break;
        case Peripherals::Keypad::KEY3:
            m_display.SetUserInput(Peripherals::Display::To7Segment(3));
            break;
        case Peripherals::Keypad::KEY4:
            m_display.SetUserInput(Peripherals::Display::To7Segment(4));
            break;
        case Peripherals::Keypad::KEY5:
            m_display.SetUserInput(Peripherals::Display::To7Segment(5));
            break;
        case Peripherals::Keypad::KEY6:
            m_display.SetUserInput(Peripherals::Display::To7Segment(6));
            break;
        case Peripherals::Keypad::KEY7:
            m_display.SetUserInput(Peripherals::Display::To7Segment(7));
            break;
        case Peripherals::Keypad::KEY8:
            m_display.SetUserInput(Peripherals::Display::To7Segment(8));
            break;
        case Peripherals::Keypad::KEY9:
            m_display.SetUserInput(Peripherals::Display::To7Segment(9));
            break;
        case Peripherals::Keypad::KEY0:
            m_display.SetUserInput(Peripherals::Display::To7Segment(0));
            break;
        case Peripherals::Keypad::KEY_ASTERISK:
            m_display.ResetUserInput();
            m_display.SetLed(Peripherals::Display::STATUS, BLACK);
            m_userInputActive = false;
            break;
        case Peripherals::Keypad::KEY_HASH_TAG:
            m_display.SetLed(Peripherals::Display::STATUS, YELLOW);
            m_userInputActive = false;
            break;
    }
}