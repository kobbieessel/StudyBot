#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <string.h>
#include <SPI.h>

// ===== DISPLAY PINS =====
#define TFT_SCLK 7
#define TFT_MOSI 6
#define TFT_RST  3
#define TFT_DC   5
#define TFT_CS   4

// Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// ===== BUTTON PINS =====
#define BTN_START 20
#define BTN_RESET 10

const uint32_t DEBOUNCE_MS = 30;
const uint32_t LONG_PRESS_MS = 900;

// ===== COLORS =====
const uint16_t BG = ST77XX_BLACK;
const uint16_t FACE = ST77XX_BLUE;
const uint16_t EYES = ST77XX_CYAN;

// ===== SCREEN STATES =====
enum Screen {
    HOME,
    FOCUS,
    STATS,
    AI_TUTOR,
    QUIZ,
    QUIZ_SUBJECTS
};

Screen currentScreen = HOME;
uint8_t menuIndex = 0;

const char* quizSubjects[] = {
    "PWM",
    "C Programming",
    "Electronics",
    "Robotics",
    "Mathematics"
};

const uint8_t SUBJECT_COUNT = 5;
uint8_t subjectIndex = 0;

const char* menuItems[] = {
    "Focus Timer",
    "Study Stats",
    "AI Tutor",
    "Quiz Mode"
};

const uint8_t MENU_COUNT = 4;

// ===== BUTTON STATE =====
enum ButtonEvent {
    NO_EVENT,
    PRESS_DOWN,
    SHORT_PRESS,
    LONG_PRESS
};

struct Button {
    uint8_t pin;
    bool lastReading;
    bool stableState;
    uint32_t lastChange;
    uint32_t pressedAt;
    bool longReported;
};

Button startButton = {BTN_START, HIGH, HIGH, 0, 0, false};
Button resetButton = {BTN_RESET, HIGH, HIGH, 0, 0, false};

// ===== TIMER STATE =====
const uint32_t STUDY_TIME = 25UL * 60;
uint32_t remainingSeconds = STUDY_TIME;
uint32_t lastTick = 0;
uint32_t completedSessions = 0;
bool timerRunning = false;

// ===== ANIMATION STATE =====
uint32_t lastBlink = 0;
uint32_t blinkStarted = 0;
const uint32_t BLINK_INTERVAL = 3000;
const uint32_t BLINK_DURATION = 150;
bool eyesClosed = false;

// ==== Wifi =====
const char* WIFI_SSID = "VSU-Guest";
const char* WIFI_PASSWORD = NULL;

// ==== Server IP ====
const char* SERVER_URL = "http://172.92.11.24:5000/latest";

struct NetworkReply {
    int httpCode;
    char message[128];
};

QueueHandle_t replyQueue = nullptr;

NetworkReply lastReply = {0, ""};

bool requestPending = false;
bool requestFinished = false;

// ===== Quiz ====
const char* QUIZ_URL = "http://172.92.11.24:5000/quiz/generate";

struct QuizData {
    char question[96];
    char options[4][40];
    char explanation[160];
    uint8_t correctIndex;
};

struct QuizReply {
    int httpCode;
    char body[768];
};

QuizData currentQuiz = {};

QueueHandle_t quizQueue = nullptr;

bool quizLoading = false;
bool quizLoaded = false;
bool quizSubmitted = false;
bool quizShowingResult = false;

uint8_t quizChoice = 0;

uint32_t quizzesAnswered = 0;
uint32_t quizzesCorrect = 0;

char quizStatus[64] = "No quiz loaded";

// Runs separately from the main display loop
void fetchQuizTask(void* parameter) {

    const char* requestedTopic = static_cast<const char*>(parameter);
    QuizReply reply = {};
    reply.httpCode = -1;

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        uint32_t started = millis();

        while (WiFi.status() != WL_CONNECTED &&
               millis() - started < 12000) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        if (http.begin(client, QUIZ_URL)) {
            http.setConnectTimeout(5000); // Connection timeout set to 5 seconds
            http.setTimeout(60000); // Allow extra time for Ollama on the Pi 5

            http.addHeader(
                "Content-Type",
                "application/x-www-form-urlencoded"
            );

            
            String topicValue = String(requestedTopic);

            
            topicValue.replace(" ", "+"); // Encode spaces for form submission

            String formBody = "topic=" + topicValue;

            Serial.print("Requesting quiz: ");
            Serial.println(requestedTopic);

            reply.httpCode = http.POST(formBody);

            if (reply.httpCode == HTTP_CODE_OK) {
                String body = http.getString();

                if (body.length() >= sizeof(reply.body)) {
                    reply.httpCode = -5;
                } 
                
                else {
                    body.toCharArray(
                        reply.body,
                        sizeof(reply.body)
                    );
                }
            }

            http.end();
        } 
        
        else {
            reply.httpCode = -2;
        }
    }

    xQueueOverwrite(quizQueue, &reply);
    vTaskDelete(nullptr);
}

void startQuizRequest() {
    if (quizLoading) return;

    if (quizQueue == nullptr) {
        snprintf(quizStatus, sizeof(quizStatus),"Quiz queue unavailable");
        return;
    }

    quizLoading = true;
    quizLoaded = false;
    quizSubmitted = false;
    quizShowingResult = false;
    quizChoice = 0;

    BaseType_t result = xTaskCreate(
        fetchQuizTask,
        "QuizHTTP",
        8192,
        const_cast<char*>(quizSubjects[subjectIndex]),
        1,
        nullptr
    );

    if (result != pdPASS) {
        quizLoading = false;
        snprintf(quizStatus, sizeof(quizStatus),
                 "Cannot create task");
    }
}

// ===== BUTTON DEBOUNCING =====
ButtonEvent readButton(Button &button, uint32_t now) {
    bool reading = digitalRead(button.pin);
    if (reading != button.lastReading) {
        button.lastReading = reading;
        button.lastChange = now;
    }

    // Wait for the electrical input to stabilize
    if (now - button.lastChange >= DEBOUNCE_MS && reading != button.stableState) {

        button.stableState = reading;

        if (reading == LOW) {
            button.pressedAt = now;
            button.longReported = false;

            // Respond immediately after debounce
            return PRESS_DOWN;
        }

        // Button was released
        if (!button.longReported) {
            return SHORT_PRESS;
        }
    }

    // Detect long press while the button is held
    if (button.stableState == LOW && !button.longReported && now - button.pressedAt >= LONG_PRESS_MS) {

        button.longReported = true;
        return LONG_PRESS;
    }

    return NO_EVENT;
}

// ===== FACE DRAWING =====
void drawEyes(bool closed) {
    
    int x = tft.width() / 2;
    int y = tft.height() / 2 - 20;

    tft.fillRect(x - 75, y - 35, 150, 48, FACE);

    if (closed) {
        tft.drawFastHLine(x - 55, y - 15, 35, EYES);
        tft.drawFastHLine(x + 20, y - 15, 35, EYES);
    } 
    
    else {
        tft.fillRoundRect(x - 55, y - 35, 35, 35, 12, EYES);
        tft.fillRoundRect(x + 20, y - 35, 35, 35, 12, EYES);
    }
}

void drawFace() {
    int w = tft.width();
    int h = tft.height();

    tft.fillScreen(BG);
    tft.fillRoundRect(18, 30, w - 36, h - 120, 25, FACE);

    drawEyes(false);

    int x = w / 2;
    int y = h / 2 - 20;

    tft.drawLine(x - 25, y + 38, x, y + 50, EYES);
    tft.drawLine(x, y + 50, x + 25, y + 38, EYES);
}

// ===== TIMER DRAWING =====
void drawTimer() {
    char buffer[12];

    snprintf(
        buffer, 
        sizeof(buffer),
        "%02lu:%02lu",
        (unsigned long)(remainingSeconds / 60),
        (unsigned long)(remainingSeconds % 60)
    );

    tft.fillRect(0, 205, tft.width(), 40, BG);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(4);
    tft.setCursor((tft.width() - 120) / 2, 210);
    tft.print(buffer);
}

void drawStatus() {
    tft.fillRect(0, 246, tft.width(), 34, BG);

    tft.setTextSize(2);
    tft.setTextColor(EYES);
    tft.setCursor(48, 250);

    if (remainingSeconds == 0) {
        tft.print("FINISHED!");
    } 
    
    else if (timerRunning) {
        tft.print("FOCUS MODE");
    } 
    
    else if (remainingSeconds == STUDY_TIME) {
        tft.print("READY");
    } 
    
    else {
        tft.print("PAUSED");
    }
}

// ===== SCREEN DRAWING =====
void drawHomeRow(uint8_t index) {
    int y = 92 + index * 37;

    bool selected = (index == menuIndex);

    tft.fillRoundRect(
        12, y, 216, 31, 7,
        selected ? EYES : FACE
    );

    tft.setTextColor(
        selected ? ST77XX_BLACK : ST77XX_WHITE
    );

    tft.setTextSize(2);
    tft.setCursor(28, y + 8);
    tft.print(menuItems[index]);
}

void drawHome() {
    tft.fillScreen(BG);

    tft.setTextSize(3);
    tft.setTextColor(EYES);
    tft.setCursor(45, 20);
    tft.println("STUDYBOT");

    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(17, 66);
    tft.println("Choose a mode:");

    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        drawHomeRow(i);
    }

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(25, 246);
    tft.print("START: NEXT");

    tft.setCursor(25, 261);
    tft.print("RESET: SELECT");
}

void drawStats() {
    tft.fillScreen(BG);

    tft.setTextSize(3);
    tft.setTextColor(EYES);
    tft.setCursor(20, 25);
    tft.println("STATS");

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);

    tft.setCursor(15, 100);
    tft.println("Completed:");

    tft.setTextSize(4);
    tft.setTextColor(EYES);
    tft.setCursor(100, 145);
    tft.println(completedSessions);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(25, 222);
    tft.println("Since last restart");

    tft.setCursor(25, 260);
    tft.println("RESET: BACK");
}

// ==== Networking Task ====
void fetchServerMessage(void *parameter) {
    
    NetworkReply reply = {};
    reply.httpCode = -1;

    snprintf(
        reply.message,
        sizeof(reply.message),
        "Wi-Fi connection failed"
    );

    // Connects to WIFI 
    if (WiFi.status() != WL_CONNECTED) {
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        uint32_t started = millis();

        while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {

            vTaskDelay(pdMS_TO_TICKS(100));
        
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        
        WiFiClient client;
        HTTPClient http;

        if (http.begin(client, SERVER_URL)) {

            http.setConnectTimeout(2500);
            http.setTimeout(3000);

            int code = http.GET();
            reply.httpCode = code;

            if (code == HTTP_CODE_OK) {
                
                String body = http.getString();

                body.toCharArray(
                    reply.message,
                    sizeof(reply.message)
                );

            } 
            
            else {

                snprintf(
                    reply.message,
                    sizeof(reply.message),
                    "HTTP request failed: %d",
                    code
                );

            }

            http.end();
        } 
        
        else {
            
            reply.httpCode = -2;

            snprintf(
                reply.message,
                sizeof(reply.message),
                "Invalid server URL"
            );

        }
    }

    // Sends the result safely to the main loop
    xQueueOverwrite(replyQueue, &reply);

    // Terminate task
    vTaskDelete(nullptr);
}

void startServerRequest() {
    if (requestPending) return;

    if (replyQueue == nullptr) {
        
        requestFinished = true;
        lastReply.httpCode = -3;
        
        snprintf(
            lastReply.message,
            sizeof(lastReply.message),
            "Queue unavailable"
        );

        return;
    }

    requestPending = true;
    requestFinished = false;

    BaseType_t result = xTaskCreate(
        fetchServerMessage,
        "StudyBotHTTP",
        8192,
        nullptr,
        1,
        nullptr
    );

    if (result != pdPASS) {
        
        requestPending = false;
        requestFinished = true;
        lastReply.httpCode = -4;
        
        snprintf(
            lastReply.message,
            sizeof(lastReply.message),
            "Could not start task"
        );

    }
}

void drawAITutor() {
    
    tft.fillScreen(BG);

    tft.setTextColor(EYES);
    tft.setTextSize(2);
    tft.setCursor(25, 24);
    tft.println("AI TUTOR");

    tft.drawFastHLine(15, 70, 210, EYES);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextWrap(true);
    tft.setCursor(15, 92);

    if (requestPending) {
        tft.println("Connecting...");
    }
    
    else if (requestFinished) {
        tft.println(lastReply.message);
    }
    
    else {
        tft.println("Ready");
    }

    tft.setTextSize(1);
    tft.setCursor(15, 240);

    if (requestFinished && lastReply.httpCode == 200) {

        tft.setTextColor(ST77XX_GREEN);
        tft.print("HTTP 200 - Connected");
    }
    
    else if (requestFinished) {
        
        tft.setTextColor(ST77XX_RED);
        tft.print("Connection failed");

    }
    
    else {
       
        tft.setTextColor(EYES);
        tft.print("Please wait");

    }

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(15, 265);
    tft.print("RESET: BACK");

    tft.setTextWrap(false);
}

bool parseQuiz(const char* json) {
    
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        // Serial.print("JSON error: ");
        Serial.println(error.c_str());
        return false;
    }

    JsonArrayConst options = doc["options"].as<JsonArrayConst>();

    if (options.isNull() || options.size() != 4 ||!doc["correct_index"].is<int>()) {
        return false;
    }

    int correct = doc["correct_index"].as<int>();

    const char* question = doc["question"] | "";
    const char* explanation = doc["explanation"] | "";

    if (correct < 0 || correct > 3 || strlen(question) == 0 || strlen(question) > 72 ||
        strlen(explanation) == 0 || strlen(explanation) > 140) {

        return false;

    }

    for (int i = 0; i < 4; i++) {
        
        const char* option = options[i] | "";

        if (strlen(option) == 0 ||
            strlen(option) > 30) {
            return false;
        }

        snprintf(
            currentQuiz.options[i],
            sizeof(currentQuiz.options[i]),
            "%s", option
        );
    }

    snprintf(
        currentQuiz.question,
        sizeof(currentQuiz.question),
        "%s", question
    );

    snprintf(
        currentQuiz.explanation,
        sizeof(currentQuiz.explanation),
        "%s", explanation
    );

    currentQuiz.correctIndex = correct;

    return true;
}

// === Partial screen update ===
void drawSubjectRow(uint8_t index) {
    
    int y = 52 + index * 38;

    bool selected = (index == subjectIndex);
    bool available = (index <= 1);

    uint16_t bgColor = selected ? EYES : FACE;
    uint16_t textColor = selected ? ST77XX_BLACK : ST77XX_WHITE;

    // Redraw only selected option's rectangle
    tft.fillRoundRect(
        12, y, 216, 32, 6, bgColor
    );

    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.setCursor(20, y + 8);
    tft.print(quizSubjects[index]);

    if (!available) {
        tft.setTextSize(1);
        tft.setCursor(205, y + 12);
        tft.print("*");
    }
}

void drawSubjectStatus() {
    
    // Clear only the status area
    tft.fillRect(12, 241, 216, 17, BG);

    tft.setTextSize(1);
    tft.setCursor(12, 245);

    if (subjectIndex <= 1) {
        tft.setTextColor(ST77XX_GREEN);
        tft.print("Verified quiz ready");
    } 
    
    else {
        tft.setTextColor(ST77XX_YELLOW);
        tft.print("Verified bank coming soon");
    }
}

//  ==== Subject Menu ====
void drawQuizSubjects() {

    // Clear only when entering the subject screen
    tft.fillScreen(BG);

    // ===== HEADER =====
    tft.setTextColor(EYES);
    tft.setTextSize(2);
    tft.setCursor(18, 15);
    tft.println("QUIZ SUBJECTS");

    tft.drawFastHLine(12, 42, 216, EYES);

    // ===== DRAW ALL SUBJECTS ONCE =====
    for (uint8_t i = 0; i < SUBJECT_COUNT; i++) {
        drawSubjectRow(i);
    }

    // ===== STATUS =====
    drawSubjectStatus();

    // ===== FOOTER =====
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(12, 263);
    tft.print("START:NEXT  RESET:SELECT");
}

// ===== Quiz Mode screen =======
void drawQuizResult() {
    
    tft.fillScreen(BG);

    bool correct =
        quizChoice == currentQuiz.correctIndex;

    tft.setTextSize(3);
    tft.setTextColor(correct ? ST77XX_GREEN : ST77XX_YELLOW);
    tft.setCursor(12, 22);
    tft.println(correct ? "CORRECT!" : "NOT QUITE");

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(12, 85);
    tft.print("Answer: ");
    tft.print(char('A' + currentQuiz.correctIndex));

    tft.setTextSize(1);
    tft.setCursor(12, 115);
    tft.print(currentQuiz.options[currentQuiz.correctIndex]);

    tft.setCursor(12, 145);
    tft.setTextColor(EYES);
    tft.println("EXPLANATION:");

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(12, 162);
    tft.setTextWrap(true);
    tft.print(currentQuiz.explanation);
    tft.setTextWrap(false);

    tft.setCursor(12, 238);
    tft.printf("Score: %lu / %lu", (unsigned long)quizzesCorrect, (unsigned long)quizzesAnswered);

    tft.setCursor(12, 258);
    tft.println("START: REVIEW");
}

void drawQuizOptionRow(uint8_t index) {
    
    int y = 148 + index * 25;

    bool selected = (index == quizChoice);

    tft.fillRoundRect(
        10, y, 220, 23, 5,
        selected ? EYES : FACE
    );

    tft.setTextColor(selected ? ST77XX_BLACK : ST77XX_WHITE);

    tft.setTextSize(2);
    tft.setCursor(17, y + 3);
    tft.print(char('A' + index));

    tft.setTextSize(1);
    tft.setCursor(43, y + 8);
    tft.print(currentQuiz.options[index]);
}

void drawQuiz() {
    if (quizShowingResult && quizLoaded) {
        drawQuizResult();
        return;
    }

    tft.fillScreen(BG);

    tft.setTextSize(2);
    tft.setTextColor(EYES);
    tft.setCursor(12, 10);
    tft.print("STUDYBOT QUIZ");

    tft.drawFastHLine(12, 34, 216, EYES);

    if (!quizLoaded) {
        
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(12, 65);

        if (quizLoading) {
            tft.println("Loading quiz...");
        } 
        
        else {
            tft.setTextSize(1);
            tft.setTextWrap(true);
            tft.print(quizStatus);
            tft.setTextWrap(false);
        }

        tft.setCursor(12, 250);
        tft.println("HOLD START: RETRY");
        return;
    }

    // Question
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextWrap(true);
    tft.setCursor(12, 42);
    tft.print(currentQuiz.question);
    tft.setTextWrap(false);

    // Answer options
    for (uint8_t i = 0; i < 4; i++) {
        drawQuizOptionRow(i);
    }

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(12, 259);

    if (quizSubmitted) {
        tft.print("RESET: VIEW RESULT");
    } 
    
    else {
        tft.print("START: NEXT RESET: SUBMIT");
    }
}

void drawCurrentScreen() {
    
    switch (currentScreen) {
        case HOME:
            drawHome();
            break;

        case FOCUS:
            drawFace();
            drawTimer();
            drawStatus();
            eyesClosed = false;
            lastBlink = millis();
            break;

        case STATS:
            drawStats();
            break;

        case AI_TUTOR:
            drawAITutor();
            break;

        case QUIZ:
            drawQuiz();
            break;

        case QUIZ_SUBJECTS:
            drawQuizSubjects();
            break;
    }

}

// ===== SCREEN NAVIGATION =====
void goHome() {
    timerRunning = false;
    currentScreen = HOME;
    drawCurrentScreen();
}

void selectMenu() {
    switch (menuIndex) {
        case 0:
            currentScreen = FOCUS;
            break;

        case 1:
            currentScreen = STATS;
            break;

        case 2:
            currentScreen = AI_TUTOR;
            break;

        case 3:
            currentScreen = QUIZ_SUBJECTS;
            subjectIndex = 0;
            break;
    }

    if (currentScreen == AI_TUTOR) {
        startServerRequest();
    }

    drawCurrentScreen();
}

// ===== TIMER CONTROL =====
void toggleTimer(uint32_t now) {
    
    if (remainingSeconds == 0) return;

    timerRunning = !timerRunning;
    lastTick = now;
    drawStatus();
}

void resetTimer(uint32_t now) {
    
    timerRunning = false;
    remainingSeconds = STUDY_TIME;
    lastTick = now;

    drawTimer();
    drawStatus();
}

void updateTimer(uint32_t now) {
    
    if (currentScreen != FOCUS || !timerRunning) return;

    uint32_t elapsed = (now - lastTick) / 1000;

    if (elapsed == 0) return;

    lastTick += elapsed * 1000;

    if (elapsed >= remainingSeconds) {
        remainingSeconds = 0;
        timerRunning = false;
        completedSessions++;

        Serial.println("Session completed!");
        drawStatus();
    } 
    
    else {
        remainingSeconds -= elapsed;
    }

    drawTimer();
}

// ===== ANIMATION =====
void updateAnimation(uint32_t now) {
    
    if (currentScreen != FOCUS) return;

    if (eyesClosed) {
        if (now - blinkStarted >= BLINK_DURATION) {
            eyesClosed = false;
            drawEyes(false);
            lastBlink = now;
        }
    } 
    
    else if (now - lastBlink >= BLINK_INTERVAL) {
        eyesClosed = true;
        blinkStarted = now;
        drawEyes(true);
    }
}

// ===== BUTTON CONTROL =====
void handleButtons(uint32_t now) {
    
    ButtonEvent startEvent = readButton(startButton, now);
    ButtonEvent resetEvent = readButton(resetButton, now);

    switch (currentScreen) {
        case HOME:
            // Move when press down and does not wait for release.
            if (startEvent == PRESS_DOWN) {
                uint8_t previousIndex = menuIndex;
                menuIndex = (menuIndex + 1) % MENU_COUNT;

                // Draws the role that changed
                drawHomeRow(previousIndex);
                drawHomeRow(menuIndex);
            }

            // Select on release allowing a clean short-press action.
            if (resetEvent == SHORT_PRESS) {
                selectMenu();
            }
            break;

        case FOCUS:
            if (resetEvent == LONG_PRESS) {
                goHome();
                return;
            }

            if (resetEvent == SHORT_PRESS) {
                resetTimer(now);
            } else if (startEvent == SHORT_PRESS) {
                toggleTimer(now);
            }
            break;

        case STATS:
        case AI_TUTOR:
    
            if (resetEvent == SHORT_PRESS) {
                goHome();
            }
            break;

        case QUIZ:
            if (resetEvent == LONG_PRESS) {
                goHome();
                return;
            }

            if (startEvent == LONG_PRESS) {
                startQuizRequest();
                drawQuiz(); // Full draw is appropriate for a new/loading quiz.
                break;
            }

            if (quizLoading || !quizLoaded) {
                break;
            }

            // While answering, highlight only the old and new rows.
            if (!quizSubmitted && startEvent == PRESS_DOWN) {
                uint8_t previousChoice = quizChoice;
                quizChoice = (quizChoice + 1) % 4;

                drawQuizOptionRow(previousChoice);
                drawQuizOptionRow(quizChoice);
            }

            // This is a real screen transition, full draw.
            if (quizSubmitted && quizShowingResult &&
                startEvent == SHORT_PRESS) {
                quizShowingResult = false;
                drawQuiz();
            }

            if (resetEvent == SHORT_PRESS) {
                if (!quizSubmitted) {
                    quizSubmitted = true;
                    quizzesAnswered++;
                    if (quizChoice == currentQuiz.correctIndex) {
                        quizzesCorrect++;
                    }
                }

                quizShowingResult = true;
                drawQuiz(); // Change to results screen.
            }
            break;

        case QUIZ_SUBJECTS:
            if (resetEvent == LONG_PRESS) {
                goHome();
                return;
            }

            if (startEvent == PRESS_DOWN) {
                uint8_t previousIndex = subjectIndex;
                subjectIndex = (subjectIndex + 1) % SUBJECT_COUNT;

                drawSubjectRow(previousIndex);
                drawSubjectRow(subjectIndex);
                drawSubjectStatus();
            }

            if (resetEvent == SHORT_PRESS) {

                if (subjectIndex <= 1) {
                    if (!quizLoading) {
                        currentScreen = QUIZ;
                        startQuizRequest();
                        drawCurrentScreen();
                    }
                } 
                
                else {
                    Serial.print("Not available: ");
                    Serial.println(quizSubjects[subjectIndex]);
                }
            }
            break;
    }
}

// ===== SERIAL CONTROL =====
void handleSerial(uint32_t now) {
    
    while (Serial.available()) {
        char cmd = Serial.read();

        if (cmd == 'm' || cmd == 'M') {
            goHome();
        }

        if (cmd == 's' || cmd == 'S') {
            
            if (currentScreen != FOCUS) {
                currentScreen = FOCUS;
                drawCurrentScreen();
            }

            toggleTimer(now);
        }

        if (cmd == 'r' || cmd == 'R') {

            if (currentScreen != FOCUS) {
                currentScreen = FOCUS;
                drawCurrentScreen();
            }

            resetTimer(now);
        }
    }
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);

    pinMode(BTN_START, INPUT_PULLUP);
    pinMode(BTN_RESET, INPUT_PULLUP);

    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 280);
    tft.setRotation(0);

    replyQueue = xQueueCreate(1, sizeof(NetworkReply));

    if (replyQueue == nullptr) {
        Serial.println("Queue creation failed");
    }

    quizQueue = xQueueCreate(1, sizeof(QuizReply));

    drawCurrentScreen();

    Serial.println("StudyBot Ready!");
    Serial.println("S: Start/Pause");
    Serial.println("R: Reset timer");
    Serial.println("M: Home menu");
}

void updateNetworkResponse() {
    if (replyQueue == nullptr) return;

    NetworkReply received;

    // Check the queue without blocking
    if (xQueueReceive(replyQueue, &received, 0) == pdTRUE) {

        lastReply = received;
        requestPending = false;
        requestFinished = true;

        Serial.print("HTTP status: ");
        Serial.println(lastReply.httpCode);

        Serial.println(lastReply.message);

        // Only redraws when AI Tutor is active
        if (currentScreen == AI_TUTOR) {
            drawAITutor();
        }
    }
}

// ===== Receive and process the quiz =====
void updateQuizNetwork() {
    
    if (quizQueue == nullptr) return;

    QuizReply reply = {};

    if (xQueueReceive(quizQueue, &reply, 0) != pdTRUE) {
        return;
    }

    quizLoading = false;

    if (reply.httpCode != 200) {
        
        quizLoaded = false;

        snprintf(
            quizStatus, sizeof(quizStatus),
            "HTTP error: %d", reply.httpCode
        );
    }
    
    else if (parseQuiz(reply.body)) {
        
        quizLoaded = true;
        quizChoice = 0;
        quizSubmitted = false;
        quizShowingResult = false;

        Serial.println("Quiz loaded successfully.");
        Serial.println(currentQuiz.question);
    }
    
    else {
        
        quizLoaded = false;

        snprintf(
            quizStatus, sizeof(quizStatus),
            "Invalid quiz JSON"
        );
    }

    if (currentScreen == QUIZ) {
        drawQuiz();
    }
}

// ===== MAIN LOOP =====
void loop() {

    uint32_t now = millis();

    updateAnimation(now);
    updateTimer(now);
    handleButtons(now);
    handleSerial(now);

    updateNetworkResponse();
    updateQuizNetwork();
    
}
