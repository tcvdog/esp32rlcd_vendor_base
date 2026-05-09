/**
 * @file arithmetic.cpp
 * @brief 算术练习模块 — 比大小 + 竖式九宫格数字输入, 四种难度
 *
 * 出题规则:
 *   幼儿园: 1~10 比大小 + 加法(和≤10)
 *   一年级: 1~100 比大小 + 20以内加减
 *   二年级: 1~100 加减 + 1~9乘法
 *   三年级: 1~100 加减乘除(整除)
 *
 * 运算符统一用 ASCII: '+' '-' '*' '/' '?'
 * UI 显示时通过 Arith_GetOpDisplay() 转为 '×' '÷' 等。
 */

#include "arithmetic.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "Arith";

static const char *s_level_names[ARITH_LEVEL_COUNT] = {
    "幼儿园", "一年级", "二年级", "三年级"
};

const char* Arith_GetLevelName(ArithLevel_t level)
{
    if (level >= ARITH_LEVEL_COUNT) return "未知";
    return s_level_names[level];
}

const char* Arith_GetOpDisplay(char op)
{
    switch (op) {
        case '+': return "+";
        case '-': return "-";
        case '*': return "×";
        case '/': return "÷";
        case '?': return "?";
        case ARITH_LEVEL_COUNT:
        default:  return "?";
    }
}

static ArithState_t s_state;

/* ========== 工具函数 ========== */
static int rand_range(int min, int max)
{
    if (min >= max) return min;
    return min + rand() % (max - min + 1);
}

/* ========== 生成比大小题 ========== */
static void gen_compare(ArithQuestion_t *q, int max_num)
{
    q->type = ARITH_Q_COMPARE;
    int a = rand_range(1, max_num);
    int b = rand_range(1, max_num);
    while (b == a) b = rand_range(1, max_num);
    q->a = a;
    q->b = b;
    q->op = '?';
    q->answer = (a > b) ? 0 : 1;  /* 0=左边大, 1=右边大 */
    q->cursor_side = 0;
}

/* ========== 生成竖式题 (九宫格输入) ========== */
static void gen_numpad(ArithQuestion_t *q, int max_a, int max_b,
                       bool allow_sub, bool allow_mul, bool allow_div)
{
    q->type = ARITH_Q_NUMPAD;
    int a = 1, b = 1;
    char op = '+';

    /* 随机选运算符 (ASCII 单字节) */
    int ops_count = 1;
    char ops[4] = {'+'};
    if (allow_sub) ops[ops_count++] = '-';
    if (allow_mul) ops[ops_count++] = '*';
    if (allow_div) ops[ops_count++] = '/';
    op = ops[rand() % ops_count];

    /* 根据运算符生成操作数 */
    switch (op) {
        case '+':
            a = rand_range(1, max_a);
            b = rand_range(1, (max_b > a) ? max_b - a : max_b);
            if (b < 1) b = 1;
            break;
        case '-':
            a = rand_range(2, max_a);
            b = rand_range(1, a);
            break;
        case '*':
            a = rand_range(2, 9);
            b = rand_range(2, 9);
            break;
        case '/':
            b = rand_range(2, 12);
            a = b * rand_range(2, 12);
            break;
    }

    int answer = 0;
    switch (op) {
        case '+': answer = a + b; break;
        case '-': answer = a - b; break;
        case '*': answer = a * b; break;
        case '/': answer = a / b; break;
    }

    q->a = a;
    q->b = b;
    q->op = op;
    q->answer = answer;
}

/* ========== 生成一轮题目 ========== */
static void generate_session(ArithLevel_t level)
{
    for (int i = 0; i < ARITH_Q_PER_SESSION; i++) {
        ArithQuestion_t *q = &s_state.questions[i];
        memset(q, 0, sizeof(ArithQuestion_t));

        switch (level) {
            case ARITH_LEVEL_KINDERGARTEN:
                if (i < ARITH_Q_PER_SESSION / 2) {
                    gen_compare(q, 10);
                } else {
                    gen_numpad(q, 9, 10, false, false, false);
                }
                break;

            case ARITH_LEVEL_GRADE_1:
                if (i < 3) {
                    gen_compare(q, 100);
                } else {
                    gen_numpad(q, 19, 20, true, false, false);
                }
                break;

            case ARITH_LEVEL_GRADE_2:
                if (i < 3) {
                    gen_numpad(q, 99, 100, true, false, false);
                } else if (i < 6) {
                    gen_numpad(q, 9, 9, false, true, false);
                } else {
                    gen_numpad(q, 50, 100, true, false, false);
                }
                break;

            case ARITH_LEVEL_GRADE_3:
                {
                    bool div_ok = (i >= 3);
                    gen_numpad(q, 99, 100, true, true, div_ok);
                }
                break;
            default:
                break;
        }
    }
}

/* ========== API ========== */

void Arith_Init(ArithLevel_t level)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.level = level;
    srand(esp_log_timestamp());
    generate_session(level);
    ESP_LOGI(TAG, "%s: %d questions", s_level_names[level], ARITH_Q_PER_SESSION);
}

ArithState_t* Arith_GetState(void) { return &s_state; }

const ArithQuestion_t* Arith_GetCurrentQuestion(void)
{
    if (s_state.question_index >= ARITH_Q_PER_SESSION) return NULL;
    return &s_state.questions[s_state.question_index];
}

ArithQuestionType_t Arith_GetCurrentType(void)
{
    const ArithQuestion_t *q = Arith_GetCurrentQuestion();
    return q ? q->type : ARITH_Q_NUMPAD;
}

void Arith_CursorMove(int dir)
{
    const ArithQuestion_t *q = Arith_GetCurrentQuestion();
    if (!q || s_state.answered) return;

    if (q->type == ARITH_Q_COMPARE) {
        s_state.cursor = (s_state.cursor == 0) ? 1 : 0;
    }
}

bool Arith_Answer(void)
{
    const ArithQuestion_t *q = Arith_GetCurrentQuestion();
    if (!q || s_state.answered || q->type != ARITH_Q_COMPARE) return false;

    s_state.answered = true;
    bool correct = (s_state.cursor == q->answer);

    s_state.last_correct = correct;
    if (correct) {
        s_state.correct_count++;
        s_state.streak++;
        int bonus = (s_state.streak > 1) ? (s_state.streak - 1) * 2 : 0;
        if (bonus > 10) bonus = 10;
        s_state.score += 10 + bonus;
    } else {
        s_state.streak = 0;
    }
    return correct;
}

bool Arith_Next(void)
{
    if (s_state.question_index >= ARITH_Q_PER_SESSION - 1) {
        s_state.session_done = true;
        ESP_LOGI(TAG, "session done");
        return false;
    }
    s_state.question_index++;
    s_state.answered = false;
    s_state.cursor = 0;
    memset(&s_state.numpad, 0, sizeof(ArithNumPad_t));
    ESP_LOGI(TAG, "next -> q%d", s_state.question_index);
    return true;
}

/* ========== 九宫格操作 ========== */

static const char* s_numpad_keys[ARITH_NP_ROWS][ARITH_NP_COLS] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"←", "0", "OK"},
};

static int s_numpad_values[ARITH_NP_ROWS][ARITH_NP_COLS] = {
    {1, 2, 3},
    {4, 5, 6},
    {7, 8, 9},
    {-1, 0, -2},  /* -1=退格, -2=确认 */
};

void Arith_NumpadMove(int dx, int dy)
{
    int col = s_state.numpad.cursor_col + dx;
    int row = s_state.numpad.cursor_row + dy;

    if (col < 0) col = ARITH_NP_COLS - 1;
    if (col >= ARITH_NP_COLS) col = 0;
    if (row < 0) row = ARITH_NP_ROWS - 1;
    if (row >= ARITH_NP_ROWS) row = 0;

    s_state.numpad.cursor_col = col;
    s_state.numpad.cursor_row = row;
}

int Arith_NumpadInput(void)
{
    if (s_state.answered) return s_state.numpad.digit_count;

    int val = s_numpad_values[s_state.numpad.cursor_row][s_state.numpad.cursor_col];

    if (val == -1) {
        Arith_NumpadBackspace();
    } else if (val == -2) {
        if (s_state.numpad.digit_count > 0) {
            int input_val = Arith_NumpadGetValue();
            const ArithQuestion_t *q = Arith_GetCurrentQuestion();
            if (!q) return 0;

            s_state.answered = true;
            bool correct = (input_val == q->answer);

            s_state.last_correct = correct;
            ESP_LOGI(TAG, "submit q%d: input=%d answer=%d %s",
                     s_state.question_index, input_val, q->answer,
                     correct ? "OK" : "WRONG");
            if (correct) {
                s_state.correct_count++;
                s_state.streak++;
                int bonus = (s_state.streak > 1) ? (s_state.streak - 1) * 2 : 0;
                if (bonus > 10) bonus = 10;
                s_state.score += 10 + bonus;
            } else {
                s_state.streak = 0;
            }
        }
    } else {
        if (s_state.numpad.digit_count < ARITH_MAX_DIGITS) {
            s_state.numpad.digits[s_state.numpad.digit_count++] = val;
        }
    }

    return s_state.numpad.digit_count;
}

void Arith_NumpadBackspace(void)
{
    if (s_state.numpad.digit_count > 0) {
        s_state.numpad.digit_count--;
    }
}

const char* Arith_NumpadGetKey(int col, int row)
{
    if (col < 0 || col >= ARITH_NP_COLS || row < 0 || row >= ARITH_NP_ROWS)
        return "";
    return s_numpad_keys[row][col];
}

int Arith_NumpadGetValue(void)
{
    int val = 0;
    for (int i = 0; i < s_state.numpad.digit_count; i++) {
        val = val * 10 + s_state.numpad.digits[i];
    }
    return val;
}

bool Arith_NumpadCheckAnswer(void)
{
    const ArithQuestion_t *q = Arith_GetCurrentQuestion();
    if (!q) return false;
    return (Arith_NumpadGetValue() == q->answer);
}

void Arith_GetProgress(char *buf, int buf_size)
{
    snprintf(buf, buf_size, "%d/%d", s_state.question_index + 1, ARITH_Q_PER_SESSION);
}

int Arith_GetScore(void) { return s_state.score; }

void Arith_GetSummary(char *buf, int buf_size)
{
    int pct = s_state.correct_count * 100 / ARITH_Q_PER_SESSION;
    snprintf(buf, buf_size, "%s · %d/%d 对 · %d分 · 最佳连对%d",
        s_level_names[s_state.level],
        s_state.correct_count, ARITH_Q_PER_SESSION,
        s_state.score, s_state.streak);
}

void Arith_Reset(void) { Arith_Init(s_state.level); }

bool Arith_IsDone(void) { return s_state.session_done; }
