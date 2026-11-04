# server: python -m flask --app app run --host=0.0.0.0 --port=5000 --no-reload
# test: curl -s -X POST http://127.0.0.1:5000/quiz/generate -d "topic=C Programming"

from flask import Flask, request, Response, render_template_string, jsonify
from threading import Lock
import requests
import threading
import re
import json
import logging


app = Flask(__name__)
app.logger.setLevel(logging.INFO)

OLLAMA_URL = "http://127.0.0.1:11434/api/generate"
MODEL = "gemma3:1b"         # AI Tutor
QUIZ_MODEL = "qwen2.5:3b"   # Quiz generation

latest_answer = "Ask a question to begin."
answer_lock = threading.Lock()

HTML = """
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>StudyBot AI</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #101827;
      color: white;
      max-width: 550px;
      margin: 40px auto;
      padding: 20px;
    }

    textarea {
      box-sizing: border-box;
      width: 100%;
      padding: 12px;
      border-radius: 8px;
      font-size: 16px;
    }

    button {
      background: #0891b2;
      color: white;
      border: 0;
      border-radius: 8px;
      padding: 12px 20px;
      margin-top: 10px;
    }

    .answer {
      background: #1e293b;
      border-radius: 8px;
      padding: 20px;
      margin-top: 20px;
    }
  </style>
</head>

<body>
  <h1>StudyBot AI</h1>
  <p>Powered locally by Ollama</p>
  <form method="POST">
    <textarea name="question" rows="4"
      maxlength="300" required
      placeholder="Ask StudyBot anything..."></textarea>
    <button type="submit">Ask StudyBot</button>
  </form>

  {% if error %}
    <p style="color:#fca5a5">{{ error }}</p>
  {% endif %}

  <div class="answer">
    <h3>AI Response</h3>
    <p>{{ answer }}</p>
  </div>
</body>
</html>
"""

KNOWN_EXPANSIONS = {
    "pwm": "pulse width modulation",
    "spi": "serial peripheral interface",
    "uart": "universal asynchronous receiver transmitter",
    "adc": "analog to digital converter",
    "dac": "digital to analog converter"

}

PWM_SUBTOPICS = [
    "PWM duty cycle and its meaning",
    "PWM frequency and period",
    "Controlling LED brightness using PWM",
    "DC motor speed control with PWM",
    "PWM generation using microcontroller timers",
    "Filtering a PWM signal with an RC circuit",
    "The difference between 25% and 75% duty cycle",
    "PWM applications in embedded systems"
]

PWM_FALLBACKS = [
    {
        "question": "What does PWM duty cycle measure?",
        "options": [
            "Percentage of time HIGH",
            "Cycles generated per second",
            "Peak signal voltage",
            "Time between pulses"
        ],
        "correct_index": 0,
        "explanation": (
            "Duty cycle is the percentage of each "
            "period that the signal is HIGH."
        )
    },
    {
        "question": "How are PWM frequency and period related?",
        "options": [
            "Directly proportional",
            "Always equal",
            "Inversely proportional",
            "Not related"
        ],
        "correct_index": 2,
        "explanation": (
            "Period equals 1/frequency. "
            "Higher frequency means a shorter period."
        )
    },
    {
        "question": "What does a 25% PWM duty cycle mean?",
        "options": [
            "Always LOW",
            "HIGH for 25% of a period",
            "Frequency equals 25 Hz",
            "Voltage is always 25 V"
        ],
        "correct_index": 1,
        "explanation": (
            "The output is HIGH for one quarter "
            "of each PWM period."
        )
    },
    {
        "question": "How can PWM increase LED brightness?",
        "options": [
            "Lower the duty cycle",
            "Reduce the ON time",
            "Keep the output LOW",
            "Increase the duty cycle"
        ],
        "correct_index": 3,
        "explanation": (
            "A higher duty cycle increases "
            "the LED's average ON time."
        )
    }
]

C_QUIZ_BANK = [
    {
        "question": "In C, what does int count = 3; do?",
        "options": [
            "Declares count, sets it to 3",
            "Prints the value of count",
            "Creates an array of 3",
            "Runs a loop three times"
        ],
        "correct_index": 0,
        "explanation": (
            "This declares an integer variable "
            "named count and initializes it to 3."
        )
    },
    {
        "question": "Which operator tests equality in C?",
        "options": [
            "=",
            "==",
            "!=",
            ">="
        ],
        "correct_index": 1,
        "explanation": (
            "The == operator compares two values "
            "for equality. The = operator assigns."
        )
    },
    {
        "question": "Which C function prints formatted text?",
        "options": [
            "scanf()",
            "malloc()",
            "printf()",
            "strlen()"
        ],
        "correct_index": 2,
        "explanation": (
            "printf() writes formatted output "
            "to standard output."
        )
    },
    {
        "question": "What does return do inside a C function?",
        "options": [
            "Starts a loop",
            "Declares a pointer",
            "Allocates memory",
            "Exits the function"
        ],
        "correct_index": 3,
        "explanation": (
            "return exits the current function "
            "and can pass a value to its caller."
        )
    },
    {
        "question": "What does &value mean in C?",
        "options": [
            "The address of value",
            "Value multiplied by 2",
            "A bitwise OR",
            "A function call"
        ],
        "correct_index": 0,
        "explanation": (
            "When value is a variable, "
            "&value gives its memory address."
        )
    },
    {
        "question": "Which header declares printf in C?",
        "options": [
            "<stdlib.h>",
            "<stdio.h>",
            "<string.h>",
            "<math.h>"
        ],
        "correct_index": 1,
        "explanation": (
            "stdio.h declares standard "
            "input and output functions."
        )
    }
]

ELECTRONICS_QUIZ_BANK = [
    {
        "question": "What is the main purpose of a resistor?",
        "options": [
            "Store electrical charge",
            "Limit electrical current",
            "Generate electricity",
            "Convert AC into DC"
        ],
        "correct_index": 1,
        "explanation": (
            "A resistor opposes current flow, "
            "helping control current in a circuit."
        )
    },
    {
        "question": "What is the unit of capacitance?",
        "options": [
            "Ohm",
            "Volt",
            "Farad",
            "Ampere"
        ],
        "correct_index": 2,
        "explanation": (
            "Capacitance is measured in farads (F)."
        )
    },
    {
        "question": "What happens to current in a series circuit?",
        "options": [
            "It is the same throughout",
            "It doubles at each component",
            "It becomes zero",
            "It varies at every resistor"
        ],
        "correct_index": 0,
        "explanation": (
            "In a simple series circuit, "
            "the same current flows through "
            "every component."
        )
    },
    {
        "question": "How is a voltmeter connected?",
        "options": [
            "In series",
            "Across an open switch only",
            "In place of the power supply",
            "In parallel"
        ],
        "correct_index": 3,
        "explanation": (
            "A voltmeter connects in parallel "
            "across the two points whose "
            "voltage difference is measured."
        )
    }
]


c_quiz_index = 0
quiz_generation_count = 0
quiz_lock = Lock()
latest_quiz = None

QUIZ_SCHEMA = {
    "type": "object",
    "properties": {
        "question": {"type": "string"},
        "options": {
            "type": "array",
            "items": {"type": "string"},
            "minItems": 4,
            "maxItems": 4
        },
        "correct_index": {
            "type": "integer",
            "minimum": 0,
            "maximum": 3
        },
        "explanation": {"type": "string"}
    },
    "required": [
        "question",
        "options",
        "correct_index",
        "explanation"
    ],
    "additionalProperties": False
}

def serve_verified_c_quiz():
    global c_quiz_index, latest_quiz
    
    with quiz_lock:
        # Select the next verified question
        index = c_quiz_index % len(C_QUIZ_BANK)

        quiz = C_QUIZ_BANK[index].copy()

        # Move to the next question for future requests
        c_quiz_index += 1

        # Save for GET /quiz/latest
        latest_quiz = quiz

    app.logger.info(
        "QUIZ SOURCE: VERIFIED C BANK | Question: %s",
        quiz["question"]

    )

    return jsonify(quiz), 200

def verify_answer(question, options, correct_index):
    pattern = r"what does ([a-z0-9]+) stand for\?"

    match = re.fullmatch(pattern,question.strip().lower())

    if match:
        acronym = match.group(1)

        if acronym in KNOWN_EXPANSIONS:
            expected = KNOWN_EXPANSIONS[acronym]

            matching = [i for i, option in enumerate(options) if option.strip().lower() == expected]

            if len(matching) != 1:
                raise ValueError("Known correct answer missing or duplicated")

            return matching[0]

    # Unverified questions need additional validation
    return correct_index

def clean_quiz_text(value):
    return " ".join(value.split()).encode("ascii", "ignore").decode("ascii").strip()

def ask_ollama(question):
    prompt = (
        "You are StudyBot, a friendly AI study tutor. "
        "Explain concepts accurately to a beginner. "
        "Use 3 to 5 short sentences, under 650 characters. "
        "Use plain ASCII text without Markdown. "
        "Explain technical terms simply.\n\n"
        f"Question: {question}"
    )

    response = requests.post( 
        OLLAMA_URL,
        json={

            "model": MODEL,
            "prompt": prompt,
            "stream": False,
            "options": {
                "temperature": 0.3,
                "num_predict": 260
            }
        },
        timeout=90
    )

    response.raise_for_status()
    answer = response.json().get("response", "").strip()
    answer = re.sub(r"\s+", " ", answer)
    answer = answer.encode("ascii", "ignore").decode("ascii")



    if not answer:
        raise ValueError("Empty Ollama response")

    if len(answer) > 650:
        answer = answer[:647].rsplit(" ", 1)[0] + "..."
    return answer

@app.post("/quiz/generate")
def generate_quiz():

    global latest_quiz, quiz_generation_count
    topic = request.form.get("topic")

    if topic is None:
        data = request.get_json(silent=True) or {}
        topic = data.get("topic", "")

    if not isinstance(topic, str) or not ( 1 <= len(topic.strip()) <= 60):
        return jsonify({"error": "Invalid topic"}), 400

    if topic.strip().lower() in ("c programming", "c", "c language"):
        return serve_verified_c_quiz()

    with quiz_lock:
        if topic.strip().upper() == "PWM":
            focus = PWM_SUBTOPICS[quiz_generation_count % len(PWM_SUBTOPICS)]

        else:
            focus = f"a practical concept involving {topic}"

        quiz_generation_count += 1

        previous_question = ( latest_quiz["question"] if latest_quiz is not None else "")

    prompt = (
        f"Create exactly ONE multiple-choice question about {topic}.\n"
        f"Focus on: {focus}.\n"
        f"Do not repeat: {previous_question}\n\n"
        "You are an accurate engineering tutor.\n"
        "Create a beginner-level conceptual question.\n\n"

        "STRICT REQUIREMENTS:\n"
        "- Exactly four distinct answer options.\n"
        "- Exactly one factually correct answer.\n"
        "- Question: 65 characters maximum.\n"
        "- Each answer option: 24 characters maximum.\n"
        "- Explanation: 100 characters maximum.\n"
        "- Use short phrases for options, NOT full sentences.\n"
        "- Avoid acronym-definition questions.\n"
        "- Use ASCII characters only.\n"
        "- correct_index must identify the correct option.\n"
        "- Follow the supplied JSON schema.\n"
    )

    try:
        response = requests.post(
            OLLAMA_URL,
            json={
                "model": QUIZ_MODEL,
                "prompt": prompt,
                "format": QUIZ_SCHEMA,
                "stream": False,
                "options": {
                    "temperature": 0.4, # 0.5, # 0.65
                    "repeat_penalty": 1.15,
                    "num_predict": 512 # 768 # 450   # 300
                }
            },
            timeout=120
        )

        response.raise_for_status()

        result = response.json()
        raw_output = result.get("response", "")

        try:
            quiz = json.loads(raw_output)

        except json.JSONDecodeError as e:
            app.logger.warning(
                "Invalid Ollama JSON: %s. "
                "Stop reason: %s. Tokens: %s. "
                "Output tail: %r",
                e,
                result.get("done_reason"),
                result.get("eval_count"),
                raw_output[-150:]
            )

            if topic.strip().upper() == "PWM":
                fallback_index = (quiz_generation_count - 1) % len(PWM_FALLBACKS)
                fallback = PWM_FALLBACKS[fallback_index].copy()

                with quiz_lock:
                    latest_quiz = fallback

                app.logger.warning("QUIZ SOURCE: FALLBACK | Question: %s", fallback["question"])

                return jsonify(fallback), 200

            raise ValueError("Ollama returned malformed JSON") from e

        question = clean_quiz_text(quiz["question"])

        verified_quiz = build_verified_duty_cycle_quiz(question,quiz_generation_count)

        # Trusted duty-cycle generator takes priority for PWM only.
        if topic.strip().upper() == "PWM" and verified_quiz is not None:
            with quiz_lock:
                latest_quiz = verified_quiz
            app.logger.info(
                "QUIZ SOURCE: VERIFIED TEMPLATE | Question: %s",
                verified_quiz["question"]
            )
            return jsonify(verified_quiz), 200

        # verifier, use the reviewed question bank instead.
        if topic.strip().upper() == "PWM":
            fallback_index = (quiz_generation_count - 1) % len(PWM_FALLBACKS)
            fallback = PWM_FALLBACKS[fallback_index].copy()

            with quiz_lock:
                latest_quiz = fallback

            app.logger.warning("QUIZ SOURCE: VERIFIED BANK | Question: %s", fallback["question"])
            return jsonify(fallback), 200

        options = [clean_quiz_text(x) for x in quiz["options"]]
        explanation = clean_quiz_text(quiz["explanation"])
        explanation = shorten_explanation(explanation)

        correct = quiz["correct_index"]

        errors = []

        if not 1 <= len(question) <= 72:
            errors.append(f"Question length: {len(question)} (allowed 1-72)")

        if len(options) != 4:
            errors.append(f"Option count: {len(options)} (expected 4)")

        for i, option in enumerate(options):
            if not 1 <= len(option) <= 30:
                errors.append(f"Option {i}: length {len(option)} (allowed 1-30)")

        if len(set(x.casefold() for x in options)) != 4:
            errors.append("Duplicate answer options")

        if type(correct) is not int or not 0 <= correct <= 3:
            errors.append(f"Invalid correct_index: {correct!r}")

        if not 1 <= len(explanation) <= 140:
            errors.append(f"Explanation length: {len(explanation)} (allowed 1-140)")

        if errors:
            app.logger.warning("Ollama quiz rejected: %s", "; ".join(errors) )

            if topic.strip().upper() == "PWM":

                index = (quiz_generation_count - 1) % len(PWM_FALLBACKS)
                fallback = PWM_FALLBACKS[index]

                with quiz_lock:
                    latest_quiz = fallback

                app.logger.warning("QUIZ SOURCE: FALLBACK | Question: %s", fallback["question"])

                return jsonify(fallback), 200

            raise ValueError("; ".join(errors))

        correct = verify_answer(question, options, correct)

        if question.strip().casefold() == previous_question.strip().casefold():
            raise ValueError("Ollama repeated the previous question")

        new_quiz = {
            "question": question,
            "options": options,
            "correct_index": correct,
            "explanation": explanation
        }

        # Saves the successfully validated quiz
        with quiz_lock:
            latest_quiz = new_quiz

        # Logs the source of the question
        app.logger.info(
            "QUIZ SOURCE: OLLAMA | Model: %s | Question: %s",
            QUIZ_MODEL,
            new_quiz["question"]
        )
        return jsonify(new_quiz)

    except (requests.RequestException,ValueError, KeyError, TypeError, AttributeError) as e:
        app.logger.exception("Quiz generation failed")
        print(f"[QUIZ ERROR] {type(e).__name__}: {e}",flush=True) # prints the error message
        return jsonify({"error": "Quiz generation failed or invalid output"}), 502

def shorten_explanation(text, limit=140):
        
        text = " ".join(text.split())
        if len(text) <= limit:
            return text

        sentences = re.split(r"(?<=[.!?])\s+", text)

        if sentences and 0 < len(sentences[0]) <= limit:
            return sentences[0]

        # Fallback: trim at a word boundary
        shortened = text[:limit - 3].rsplit(" ", 1)[0]
        return shortened.rstrip(" .,:;") + "..."

def build_verified_duty_cycle_quiz(question, variation=0):
    pattern = (
        r"(?:PWM\s+duty\s+cycle\s+of\s+(\d{1,3})\s*%"
        r"|(\d{1,3})\s*%\s*PWM\s+duty\s+cycle)"
    )

    match = re.search(pattern, question, re.IGNORECASE)

    if not match:
        return None

    duty = int(match.group(1) or match.group(2))

    if not 0 <= duty <= 100:
        return None

    # Generates three incorrect percentages
    distractors = [ value for value in (0, 25, 50, 75, 100) if value != duty][:3]
    values = distractors.copy()

    # Varys the correct answer position
    correct_index = variation % 4
    values.insert(correct_index, duty)

    return {
        "question": (f"What does a {duty}% PWM duty cycle mean?" ),
        "options": [ f"HIGH for {value}% of period" for value in values],
        "correct_index": correct_index,
        "explanation": (
            f"The signal is HIGH for {duty}% "
            f"and LOW for {100 - duty}% of each period."
        )
    }

@app.get("/quiz/latest")
def get_latest_quiz():
    with quiz_lock:
        quiz = latest_quiz

    if quiz is None:
        return jsonify({"error": "No quiz generated yet" }), 404

    return jsonify(quiz)

@app.route("/", methods=["GET", "POST"])
def home():
    global latest_answer
    error = None

    if request.method == "POST":
        question = request.form.get("question", "").strip()

        if not question or len(question) > 300:
            error = "Enter a valid question under 300 characters."

        else:
            try:
                answer = ask_ollama(question)
                with answer_lock:
                    latest_answer = answer

            except (requests.RequestException, ValueError):
                app.logger.exception("Ollama request failed")
                error = "Ollama unavailable or model request failed."

    with answer_lock:
        current_answer = latest_answer

    return render_template_string(
        HTML,
        answer=current_answer,
        error=error
    )

@app.get("/latest")
def latest():
    with answer_lock:
        answer = latest_answer
    return Response(answer, mimetype="text/plain")

@app.get("/hello")
def hello():
    return Response(
        "Hello StudyBot! Your server is online.",
        mimetype="text/plain"
    )
