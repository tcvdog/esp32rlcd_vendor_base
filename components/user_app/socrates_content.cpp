#include "socrates_content.h"
#include "sd_audio_player.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

static const char *TAG = "Socrates";

static SocratesDatabase_t s_db = {0};

static const char *s_embedded_json =
  "{\"meta\":{\"version\":\"1.0\",\"total\":1},\"questions\":["
    "{\"id\":1,\"category\":\"math\",\"grade\":\"G7\",\"difficulty\":1,"
    "\"question\":\"为什么任何数乘以0都等于0？\","
    "\"socratic_hint\":\"你觉得5×3是什么意思？5×0呢？\","
    "\"options\":["
      "{\"text\":\"乘以0表示0个几，什么都没有，结果就是0\",\"correct\":true,"
        "\"explanation\":\"乘法可理解为重复加法：5×0=0+0+0+0+0=0\"},"
      "{\"text\":\"因为0很小，所以结果也很小\",\"correct\":false,"
        "\"explanation\":\"不是小，是确切的0\"},"
      "{\"text\":\"因为0是特殊数字\",\"correct\":false,"
        "\"explanation\":\"这不是解释，只是陈述事实\"},"
      "{\"text\":\"因为乘法表里规定0×任何数=0\",\"correct\":false,"
        "\"explanation\":\"乘法表是规律总结，不是原因\"}"
    "],\"further_questions\":[\"0除以任何数等于几？\",\"为什么0不能做除数？\"]}"
  "]}";

static const char *s_embedded_methods_json =
  "{\"methods\":["
    "{\"id\":\"feynman\",\"name\":\"费曼学习法\",\"category\":\"理解\",\"icon\":\"F\","
      "\"summary\":\"用最简单的语言教会别人\","
      "\"steps\":[\"选择一个你想理解的概念\",\"用最简单的语言写下来\","
        "\"发现卡壳的地方重新学习\",\"简化语言用类比\"],"
      "\"socratic_tip\":\"你刚才说的，能用一个初中生的词来解释吗？\","
      "\"for_kids\":\"像讲故事一样讲给爸爸妈妈听\"},"
    "{\"id\":\"pomodoro\",\"name\":\"番茄工作法\",\"category\":\"专注\",\"icon\":\"P\","
      "\"summary\":\"25分钟专注+5分钟休息\","
      "\"steps\":[\"选一个要完成的任务\",\"设定25分钟计时器\",\"休息5分钟\","
        "\"每4个番茄后休息15-30分钟\"],"
      "\"socratic_tip\":\"你觉得为什么25分钟比2小时效率更高？\","
      "\"for_kids\":\"像玩游戏一样，25分钟专心闯关\"}"
  "]}";

static char* read_file_to_heap(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "Failed to open: %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0) { fclose(f); return NULL; }
    char *buf = (char*)malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void parse_question(cJSON *jq, SocraticQuestion_t *q)
{
    memset(q, 0, sizeof(*q));
    q->id = cJSON_GetObjectItem(jq, "id")->valueint;

    cJSON *jc = cJSON_GetObjectItem(jq, "category");
    if (jc) snprintf(q->category, sizeof(q->category), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jq, "grade");
    if (jc) snprintf(q->grade, sizeof(q->grade), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jq, "difficulty");
    if (jc) q->difficulty = jc->valueint;

    jc = cJSON_GetObjectItem(jq, "question");
    if (jc) snprintf(q->question, sizeof(q->question), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jq, "socratic_hint");
    if (jc) snprintf(q->socratic_hint, sizeof(q->socratic_hint), "%s", jc->valuestring);

    cJSON *jopts = cJSON_GetObjectItem(jq, "options");
    if (jopts) {
        int idx = 0;
        cJSON *jopt;
        cJSON_ArrayForEach(jopt, jopts) {
            if (idx >= SOCRATES_MAX_OPTIONS) break;
            jc = cJSON_GetObjectItem(jopt, "text");
            if (jc) snprintf(q->options[idx].text, sizeof(q->options[idx].text),
                            "%s", jc->valuestring);
            q->options[idx].correct = cJSON_IsTrue(cJSON_GetObjectItem(jopt, "correct"));
            jc = cJSON_GetObjectItem(jopt, "explanation");
            if (jc) snprintf(q->options[idx].explanation,
                            sizeof(q->options[idx].explanation), "%s", jc->valuestring);
            idx++;
        }
        q->option_count = idx;
    }

    cJSON *jfq = cJSON_GetObjectItem(jq, "further_questions");
    if (jfq) {
        int idx = 0;
        cJSON *jf;
        cJSON_ArrayForEach(jf, jfq) {
            if (idx >= SOCRATES_MAX_FURTHER) break;
            snprintf(q->further_questions[idx], sizeof(q->further_questions[0]),
                     "%s", jf->valuestring);
            idx++;
        }
        q->further_count = idx;
    }
}

static void parse_method(cJSON *jm, MemoryMethod_t *m)
{
    memset(m, 0, sizeof(*m));

    cJSON *jc = cJSON_GetObjectItem(jm, "id");
    if (jc) snprintf(m->id, sizeof(m->id), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jm, "name");
    if (jc) snprintf(m->name, sizeof(m->name), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jm, "category");
    if (jc) snprintf(m->category, sizeof(m->category), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jm, "icon");
    if (jc) snprintf(m->icon, sizeof(m->icon), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jm, "summary");
    if (jc) snprintf(m->summary, sizeof(m->summary), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jm, "socratic_tip");
    if (jc) snprintf(m->socratic_tip, sizeof(m->socratic_tip), "%s", jc->valuestring);

    jc = cJSON_GetObjectItem(jm, "for_kids");
    if (jc) snprintf(m->for_kids, sizeof(m->for_kids), "%s", jc->valuestring);

    cJSON *jsteps = cJSON_GetObjectItem(jm, "steps");
    if (jsteps) {
        int idx = 0;
        cJSON *js;
        cJSON_ArrayForEach(js, jsteps) {
            if (idx >= SOCRATES_METHOD_STEPS) break;
            snprintf(m->steps[idx], sizeof(m->steps[0]), "%s", js->valuestring);
            idx++;
        }
        m->step_count = idx;
    }
}

static void build_categories(void)
{
    s_db.category_count = 0;
    const char *seen_cats[SOCRATES_MAX_CATEGORIES] = {0};
    int seen_count = 0;

    for (int i = 0; i < s_db.question_count && s_db.category_count < SOCRATES_MAX_CATEGORIES; i++) {
        const char *cat = s_db.questions[i].category;
        if (!cat[0]) continue;

        bool found = false;
        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen_cats[j], cat) == 0) { found = true; break; }
        }
        if (found) continue;

        seen_cats[seen_count++] = cat;
        SocraticCategory_t *c = &s_db.categories[s_db.category_count];

        snprintf(c->id, sizeof(c->id), "%s", cat);

        if      (strcmp(cat, "math")      == 0) { snprintf(c->name, sizeof(c->name), "数学"); snprintf(c->icon, sizeof(c->icon), "M"); }
        else if (strcmp(cat, "physics")   == 0) { snprintf(c->name, sizeof(c->name), "物理"); snprintf(c->icon, sizeof(c->icon), "P"); }
        else if (strcmp(cat, "chemistry") == 0) { snprintf(c->name, sizeof(c->name), "化学"); snprintf(c->icon, sizeof(c->icon), "C"); }
        else if (strcmp(cat, "biology")   == 0) { snprintf(c->name, sizeof(c->name), "生物"); snprintf(c->icon, sizeof(c->icon), "B"); }
        else if (strcmp(cat, "history")   == 0) { snprintf(c->name, sizeof(c->name), "历史"); snprintf(c->icon, sizeof(c->icon), "H"); }
        else if (strcmp(cat, "geography") == 0) { snprintf(c->name, sizeof(c->name), "地理"); snprintf(c->icon, sizeof(c->icon), "G"); }
        else if (strcmp(cat, "tech")      == 0) { snprintf(c->name, sizeof(c->name), "科技"); snprintf(c->icon, sizeof(c->icon), "T"); }
        else if (strcmp(cat, "news")      == 0) { snprintf(c->name, sizeof(c->name), "新闻"); snprintf(c->icon, sizeof(c->icon), "N"); }
        else { snprintf(c->name, sizeof(c->name), "%s", cat); snprintf(c->icon, sizeof(c->icon), "?"); }

        c->question_count = 0;
        s_db.category_count++;
    }

    for (int i = 0; i < s_db.question_count; i++) {
        for (int j = 0; j < s_db.category_count; j++) {
            if (strcmp(s_db.questions[i].category, s_db.categories[j].id) == 0) {
                s_db.categories[j].question_count++;
                break;
            }
        }
    }
}

SocratesDatabase_t* Socrates_GetDB(void) { return &s_db; }
bool Socrates_IsLoaded(void) { return s_db.loaded; }
int  Socrates_GetQuestionCount(void) { return s_db.question_count; }
int  Socrates_GetMethodCount(void) { return s_db.method_count; }

const SocraticQuestion_t* Socrates_GetQuestion(int index)
{
    if (index < 0 || index >= s_db.question_count) return NULL;
    return &s_db.questions[index];
}

const MemoryMethod_t* Socrates_GetMethod(int index)
{
    if (index < 0 || index >= s_db.method_count) return NULL;
    return &s_db.methods[index];
}

int Socrates_GetQuestionsByCategory(const char *category,
                                    const SocraticQuestion_t **out,
                                    int max_out)
{
    int count = 0;
    for (int i = 0; i < s_db.question_count && count < max_out; i++) {
        if (strcmp(s_db.questions[i].category, category) == 0)
            out[count++] = &s_db.questions[i];
    }
    return count;
}

int Socrates_GetQuestionsByDifficulty(int difficulty,
                                     const SocraticQuestion_t **out,
                                     int max_out)
{
    int count = 0;
    for (int i = 0; i < s_db.question_count && count < max_out; i++) {
        if (s_db.questions[i].difficulty == difficulty)
            out[count++] = &s_db.questions[i];
    }
    return count;
}

const SocraticQuestion_t* Socrates_GetRandomQuestion(void)
{
    if (s_db.question_count == 0) return NULL;
    return &s_db.questions[rand() % s_db.question_count];
}

const SocraticQuestion_t* Socrates_GetRandomByCategory(const char *category)
{
    if (s_db.question_count == 0) return NULL;
    int count = 0;
    for (int i = 0; i < s_db.question_count; i++)
        if (strcmp(s_db.questions[i].category, category) == 0) count++;
    if (count == 0) return NULL;
    int target = rand() % count;
    int idx = 0;
    for (int i = 0; i < s_db.question_count; i++) {
        if (strcmp(s_db.questions[i].category, category) == 0) {
            if (idx == target) return &s_db.questions[i];
            idx++;
        }
    }
    return NULL;
}

int Socrates_GetCategories(SocraticCategory_t *out, int max_out)
{
    int count = 0;
    for (int i = 0; i < s_db.category_count && count < max_out; i++) {
        memcpy(&out[count], &s_db.categories[i], sizeof(SocraticCategory_t));
        count++;
    }
    return count;
}

int Socrates_GetCategoryQuestionCount(const char *category)
{
    for (int i = 0; i < s_db.category_count; i++)
        if (strcmp(s_db.categories[i].id, category) == 0)
            return s_db.categories[i].question_count;
    return 0;
}

int Socrates_GetMethodsByCategory(const char *category,
                                  const MemoryMethod_t **out,
                                  int max_out)
{
    int count = 0;
    for (int i = 0; i < s_db.method_count && count < max_out; i++) {
        if (strcmp(s_db.methods[i].category, category) == 0)
            out[count++] = &s_db.methods[i];
    }
    return count;
}

bool Socrates_LoadAll(void)
{
    memset(&s_db, 0, sizeof(s_db));

    char *json = read_file_to_heap("/sdcard/vocab/socrates.json");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *jqarr = cJSON_GetObjectItem(root, "questions");
            if (jqarr) {
                int idx = 0;
                cJSON *jq;
                cJSON_ArrayForEach(jq, jqarr) {
                    if (idx >= SOCRATES_MAX_QUESTIONS) break;
                    parse_question(jq, &s_db.questions[idx]);
                    idx++;
                }
                s_db.question_count = idx;
                ESP_LOGI(TAG, "Loaded %d questions from SD", idx);
            }
            cJSON_Delete(root);
        }
        free(json);
    } else {
        ESP_LOGW(TAG, "SD not available, using embedded questions");
        cJSON *root = cJSON_Parse(s_embedded_json);
        if (root) {
            cJSON *jqarr = cJSON_GetObjectItem(root, "questions");
            if (jqarr) {
                int idx = 0;
                cJSON *jq;
                cJSON_ArrayForEach(jq, jqarr) {
                    if (idx >= SOCRATES_MAX_QUESTIONS) break;
                    parse_question(jq, &s_db.questions[idx]);
                    idx++;
                }
                s_db.question_count = idx;
            }
            cJSON_Delete(root);
        }
    }

    json = read_file_to_heap("/sdcard/vocab/memory_methods.json");
    if (!json) json = strdup(s_embedded_methods_json);

    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *jarr = cJSON_GetObjectItem(root, "methods");
            if (jarr) {
                int idx = 0;
                cJSON *jm;
                cJSON_ArrayForEach(jm, jarr) {
                    if (idx >= SOCRATES_MAX_METHODS) break;
                    parse_method(jm, &s_db.methods[idx]);
                    idx++;
                }
                s_db.method_count = idx;
                ESP_LOGI(TAG, "Loaded %d memory methods", idx);
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    build_categories();

    s_db.loaded = (s_db.question_count > 0);
    ESP_LOGI(TAG, "Socrates DB: %d questions, %d methods, %d categories",
             s_db.question_count, s_db.method_count, s_db.category_count);
    return s_db.loaded;
}
