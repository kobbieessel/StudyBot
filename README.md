# StudyBot: AI-Powered Desk Companion

> **Status:** Active development | Some features work end to end; others are experimental or planned.

StudyBot is a small desktop companion designed to make studying more focused, interactive, and accessible. Instead of constantly switching to a computer or phone, a learner can interact with a dedicated device to manage study sessions, review learning statistics, read AI-generated explanations, and answer topic-based quizzes.

The project is also an ongoing engineering learning platform: I am building, testing, debugging, optimizing, and documenting the firmware, electronics, user interface, networking, and backend as I go.

## Why I Built StudyBot

Many study tools live inside the same devices that also contain distractions. StudyBot explores a different approach: a **purpose-built desk companion** that makes it easy to start a focused session, interact with an AI tutor, and check understanding through quizzes.

The larger goal is to create an extensible assistant that can eventually perform other assigned tasks, while keeping studying at the center of the experience. I am deliberately building this project incrementally, from low-level display control and button handling to Wi-Fi communication and local AI integration.

## Folder Structure

```text
StudyBot/
├── README.md
├── firmware/
│   └── StudyBot.ino
├── backend/
│   ├── app.py
│   └── requirements.txt
├── hardware/         
└── media/
    ├── StudyBot_demo.mp4
    └── System_Architecture.png            
```

## System Architecture

![System Architecture](/StudyBot/media/StudyBot_demo.mp4)

The **ESP32-C3** handles the display, buttons, navigation, and focus timer. A **Raspberry Pi 5** runs a **Flask** server and locally hosted **Ollama** models. The ESP32-C3 requests AI responses and quizzes from the server over Wi-Fi.

**Current technology:** ESP32-C3, ST7789 TFT, Raspberry Pi 5, Arduino/C++, Python, Flask, Ollama, Wi-Fi/HTTP. The backend uses **Gemma 3 1B** for tutoring and **Qwen 2.5 3B** for quiz generation.

## Development Progress

- [x] Home menu and button-based navigation
- [x] 25-minute focus timer, study statistics, and face animation
- [x] AI tutor responses displayed on the TFT screen
- [x] Multiple-choice quizzes with answer feedback
- [x] AI-generated quizzes with validation and fallback questions
- [x] Non-blocking UI updates and optimized partial-screen rendering

## Testing and Validation

The following checks have been exercised during development:

- Boot and draw the ESP32-C3/ST7789 interface.
- Navigate menus using the physical buttons, including debouncing.
- Start, pause, resume, and reset the 25-minute study timer.
- Send a request from the device to Flask on the Raspberry Pi.
- Receive and display the latest AI tutor response.
- Generate and display quiz content, including multiple-choice options and results.
- Use verified fallback content when generated quizzes do not meet constraints.
- Observe Flask logs and Serial Monitor output while debugging.
- Measure full-screen and partial-screen rendering times.

**Testing status:** validation has primarily consisted of manual hardware checks, integration trials, and log-based debugging. A repeatable automated test suite, long-duration reliability testing, and robust power/reconnection testing are still to be added.

## Demo
![StudyBot_demo.mp4](/StudyBot/media/StudyBot_demo.mp4)

## What's Next

- [ ] Improve quiz variety and study-progress tracking
- [ ] Strengthen Wi-Fi reconnection and error handling
- [ ] Build a cleaner enclosure and improve hardware integration
- [ ] Explore running the backend on NVIDIA Jetson Orin
- [ ] Add more assistant features beyond studying

## Author

**Kwabena Amoako** — Embedded Systems | Firmware | Robotics  
**Build journey:**  [@KobbieBuilds on YouTube](https://youtube.com/@kobbiebuilds)

*StudyBot is a work in progress. The README will evolve as the project develops.*