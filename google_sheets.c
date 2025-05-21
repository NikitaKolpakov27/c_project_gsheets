#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <curl/curl.h>
#include <cJSON.h>


// Объявляем прототипы функций для Windows
#if defined(_WIN32) && !defined(strnlen)
size_t strnlen(const char* s, size_t maxlen); 
#endif

char* strndup(const char* s, size_t n); // Явное объявление прототипа

// Реализация strnlen для Windows
#if defined(_WIN32) && !defined(strnlen)
size_t strnlen(const char* s, size_t maxlen) {
    const char* end = memchr(s, 0, maxlen);
    return end ? (size_t)(end - s) : maxlen;
}
#endif

char* strndup(const char* s, size_t n) {
    size_t len = strnlen(s, n);
    char* new = malloc(len + 1);
    if (new) {
        memcpy(new, s, len);
        new[len] = '\0';
    }
    return new;
}

// Структуры данных
typedef struct {
    char* access_token;
    char* spreadsheet_id;
} GSheetClient;

typedef struct {
    char*** data;
    size_t rows;
    size_t cols;
} SheetRange;

// Вспомогательные функции
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    char** response = (char**)userdata;
    *response = strndup(ptr, size * nmemb);
    return size * nmemb;
}

static char* build_auth_header(GSheetClient* client) {
    char* header = malloc(128);
    snprintf(header, 128, "Authorization: Bearer %s", client->access_token);
    return header;
}

void gsheet_free(GSheetClient* client) {
    if (!client) return;
    free(client->access_token);
    free(client->spreadsheet_id);
    free(client);
}

void gsheet_free_range(SheetRange* range) {
    if (!range) return;
    for (size_t i = 0; i < range->rows; i++) {
        for (size_t j = 0; j < range->cols; j++) {
            free(range->data[i][j]);
        }
        free(range->data[i]);
    }
    free(range->data);
    free(range);
}

// Основные методы
// 1. Initialization of client
GSheetClient* gsheet_init(const char* access_token, const char* spreadsheet_id) {
    GSheetClient* client = malloc(sizeof(GSheetClient));
    client->access_token = strdup(access_token);
    client->spreadsheet_id = strdup(spreadsheet_id);
    return client;
}

// 2. read gsheet in specified range
SheetRange* gsheet_read_range(GSheetClient* client, const char* range) {
    CURL* curl = curl_easy_init();
    char* response = NULL;
    char url[256];
    long http_code = 0;

    snprintf(url, sizeof(url),
        "https://sheets.googleapis.com/v4/spreadsheets/%s/values/%s",
        client->spreadsheet_id, range
    );

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, build_auth_header(client));

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    // Получите HTTP-статус ответа
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    printf("[DEBUG] HTTP Status: %ld\n", http_code);
    printf("[DEBUG] Response: %s\n", response);

    SheetRange* result = NULL;
    if (res == CURLE_OK && http_code == 200 && response) {
        cJSON* root = cJSON_Parse(response);
        if (root) {
            cJSON* values = cJSON_GetObjectItem(root, "values");
            if (values && cJSON_IsArray(values)) {
                result = malloc(sizeof(SheetRange));
                result->rows = cJSON_GetArraySize(values);
                result->cols = 0;
                result->data = malloc(result->rows * sizeof(char**));

                for (size_t i = 0; i < result->rows; i++) {
                    cJSON* row = cJSON_GetArrayItem(values, i);
                    if (row && cJSON_IsArray(row)) {
                        result->data[i] = malloc(cJSON_GetArraySize(row) * sizeof(char*));
                        result->cols = cJSON_GetArraySize(row);

                        for (size_t j = 0; j < result->cols; j++) {
                            cJSON* cell = cJSON_GetArrayItem(row, j);
                            result->data[i][j] = strdup(cJSON_GetStringValue(cell));
                        }
                    }
                }
            }
            cJSON_Delete(root);
        }
    } else {
        fprintf(stderr, "HTTP Error: %ld\n", http_code);
        if (response) fprintf(stderr, "Response: %s\n", response);
    }

    curl_easy_cleanup(curl);
    free(response);
    return result;
}

// 3. Write in smth range
boolean gsheet_write_range(GSheetClient* client, const char* range, SheetRange* data) {
    CURL* curl = curl_easy_init();
    char url[256];
    snprintf(url, sizeof(url),
        "https://sheets.googleapis.com/v4/spreadsheets/%s/values/%s?valueInputOption=RAW",
        client->spreadsheet_id, range
    );

    cJSON* root = cJSON_CreateObject();
    cJSON* values = cJSON_AddArrayToObject(root, "values");

    // Преобразование SheetRange в JSON
    for (size_t i = 0; i < data->rows; i++) {
        cJSON* row = cJSON_CreateArray();
        for (size_t j = 0; j < data->cols; j++) {
            cJSON_AddItemToArray(row, cJSON_CreateString(data->data[i][j]));
        }
        cJSON_AddItemToArray(values, row);
    }

    char* payload = cJSON_PrintUnformatted(root);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, build_auth_header(client));
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    boolean success = (res == CURLE_OK && http_code == 200);
    if (!success) {
        fprintf(stderr, "Write failed. HTTP Code: %ld\n", http_code);
    }

    cJSON_Delete(root);
    free(payload);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

// 1. Создать новую таблицу
char* gsheet_create_spreadsheet(GSheetClient* client, const char* title) {
    CURL* curl = curl_easy_init();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "properties.title", title);

    char* payload = cJSON_PrintUnformatted(root);
    char* response = NULL;
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, build_auth_header(client));
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, "https://sheets.googleapis.com/v4/spreadsheets");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    char* spreadsheet_id = NULL;
    
    if (res == CURLE_OK && response) {
        cJSON* json = cJSON_Parse(response);
        spreadsheet_id = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(json, "spreadsheetId")));
        cJSON_Delete(json);
    }

    cJSON_Delete(root);
    free(payload);
    curl_easy_cleanup(curl);
    return spreadsheet_id;
}

// 2. Добавить новый лист
boolean gsheet_add_sheet(GSheetClient* client, const char* sheet_title) {
    cJSON* root = cJSON_CreateObject();
    cJSON* requests = cJSON_AddArrayToObject(root, "requests");
    cJSON* add_sheet = cJSON_CreateObject();
    cJSON* props = cJSON_AddObjectToObject(add_sheet, "addSheet");
    cJSON_AddStringToObject(props, "title", sheet_title);
    cJSON_AddItemToArray(requests, add_sheet);

    char url[256];
    snprintf(url, sizeof(url), 
        "https://sheets.googleapis.com/v4/spreadsheets/%s:batchUpdate",
        client->spreadsheet_id
    );

    // Отправка запроса (аналогично gsheet_write_range)
    // ...
    return TRUE;
}


// 3. Получить информацию о листах
void gsheet_get_sheet_info(GSheetClient* client) {
    char url[256];
    snprintf(url, sizeof(url), 
        "https://sheets.googleapis.com/v4/spreadsheets/%s",
        client->spreadsheet_id
    );

    // GET-запрос и парсинг JSON с sheetId и названиями
    // ...
}

// 4. Очистить диапазон
boolean gsheet_clear_range(GSheetClient* client, const char* range) {
    char url[256];
    snprintf(url, sizeof(url), 
        "https://sheets.googleapis.com/v4/spreadsheets/%s/values/%s:clear",
        client->spreadsheet_id, range
    );

    // POST-запрос с пустым телом
    // ...
    return TRUE;
}


// 5. Удалить строку
boolean gsheet_delete_row(GSheetClient* client, int sheet_id, int row) {
    cJSON* root = cJSON_CreateObject();
    cJSON* requests = cJSON_AddArrayToObject(root, "requests");
    cJSON* delete_dim = cJSON_CreateObject();
    cJSON* range = cJSON_AddObjectToObject(delete_dim, "deleteDimension");
    cJSON* dim = cJSON_AddObjectToObject(range, "dimension");
    cJSON_AddNumberToObject(dim, "sheetId", sheet_id);
    cJSON_AddStringToObject(dim, "dimension", "ROWS");
    cJSON_AddNumberToObject(dim, "startIndex", row);
    cJSON_AddNumberToObject(dim, "endIndex", row + 1);
    cJSON_AddItemToArray(requests, delete_dim);

    // Отправка batchUpdate
    // ...
    return TRUE;
}


// 6. Переименовать лист
boolean gsheet_rename_sheet(GSheetClient* client, int sheet_id, const char* new_name) {
    cJSON* root = cJSON_CreateObject();
    cJSON* requests = cJSON_AddArrayToObject(root, "requests");
    cJSON* update_props = cJSON_CreateObject();
    cJSON* props = cJSON_AddObjectToObject(update_props, "updateSheetProperties");
    cJSON_AddNumberToObject(props, "sheetId", sheet_id);
    cJSON_AddStringToObject(props, "title", new_name);
    cJSON_AddItemToArray(requests, update_props);

    // Отправка batchUpdate
    // ...
    return TRUE;
}


// 8. Получить историю изменений
void gsheet_get_history(GSheetClient* client) {
    char url[256];
    snprintf(url, sizeof(url), 
        "https://sheets.googleapis.com/v4/spreadsheets/%s/revisions",
        client->spreadsheet_id
    );

    // GET-запрос и парсинг JSON
    // ...
}

// 9. Изменить форматирование ячейки
boolean gsheet_format_cell(GSheetClient* client, int sheet_id, const char* cell, int bg_color) {
    cJSON* root = cJSON_CreateObject();
    cJSON* requests = cJSON_AddArrayToObject(root, "requests");
    cJSON* format_req = cJSON_CreateObject();
    cJSON* cell_format = cJSON_AddObjectToObject(format_req, "repeatCell");
    
    // Формирование JSON для формата
    // ...
    
    // Отправка batchUpdate
    return TRUE;
}

// 10. Пакетное обновление
boolean gsheet_batch_update(GSheetClient* client, cJSON* requests) {
    char url[256];
    snprintf(url, sizeof(url), 
        "https://sheets.googleapis.com/v4/spreadsheets/%s:batchUpdate",
        client->spreadsheet_id
    );

    // Отправка POST-запроса с JSON из `requests`
    // ...
    return TRUE;
}



// 11. Удаляем лист
void gsheet_delete_sheet(GSheetClient* client, int sheet_id) {
    cJSON* root = cJSON_CreateObject();
    cJSON* requests = cJSON_AddArrayToObject(root, "requests");

    cJSON* delete_sheet = cJSON_CreateObject();
    cJSON_AddItemToObject(delete_sheet, "deleteSheet",
        cJSON_CreateObject());
    cJSON_AddNumberToObject(cJSON_GetObjectItem(delete_sheet, "deleteSheet"), "sheetId", sheet_id);

    cJSON_AddItemToArray(requests, delete_sheet);

    // Отправка batchUpdate
    // ... (реализация аналогично gsheet_write_range)
}

// 12. Чтение ячейки
SheetRange* gsheet_read_cell(GSheetClient* client, int row, int col) {
    char range[32];
    snprintf(range, sizeof(range), "R%dC%d", row, col);
    return gsheet_read_range(client, range);
}

// 13. Добавление строки в таблицу
boolean gsheet_append_row(GSheetClient* client, const char* sheet_name, char*** row_data, size_t cols) {
    char range[64];
    snprintf(range, sizeof(range), "%s!A:A", sheet_name);
    SheetRange data = { .rows = 1, .cols = cols, .data = row_data };
    return gsheet_write_range(client, range, &data);
}

// 14. Сортировка
int gsheet_sort_range(GSheetClient* client, const char* range, int column_index) {
    cJSON* requests = cJSON_CreateArray();
    cJSON* sort_request = cJSON_CreateObject();
    
    // Создаем объект sortRange
    cJSON* sort_range = cJSON_CreateObject();
    
    // Добавляем поле range
    cJSON_AddStringToObject(sort_range, "range", range);
    
    // Создаем массив sortSpecs
    cJSON* sort_specs = cJSON_CreateArray();
    cJSON* spec = cJSON_CreateObject();
    cJSON_AddNumberToObject(spec, "dimensionIndex", column_index);
    cJSON_AddStringToObject(spec, "sortOrder", "ASCENDING");
    cJSON_AddItemToArray(sort_specs, spec);
    printf("Hello there!\n");
    
    // Добавляем sortSpecs в sortRange
    cJSON_AddItemToObject(sort_range, "sortSpecs", sort_specs);
    
    // Собираем итоговый запрос
    cJSON_AddItemToObject(sort_request, "sortRange", sort_range);
    cJSON_AddItemToArray(requests, sort_request);
    
    // Отправляем запрос и возвращаем результат
    int result = gsheet_batch_update(client, requests);
    
    // Очищаем JSON-структуры
    cJSON_Delete(requests);
    return result;
}

// 15. Установка формулы
boolean gsheet_set_formula(GSheetClient* client, const char* cell, char*** formula) {
    SheetRange data = {
        .rows = 1,
        .cols = 1,
        .data = formula
    };
    return gsheet_write_range(client, cell, &data);
}

// 16. Объединение ячеек
int gsheet_merge_cells(GSheetClient* client, const char* range) {
    cJSON* requests = cJSON_CreateArray();
    
    // Создаем объект mergeCells
    cJSON* merge_request = cJSON_CreateObject();
    cJSON* merge_params = cJSON_CreateObject();
    
    // Добавляем параметры объединения
    cJSON_AddStringToObject(merge_params, "range", range);
    cJSON_AddStringToObject(merge_params, "mergeType", "MERGE_ALL");
    
    // Собираем структуру запроса
    cJSON_AddItemToObject(merge_request, "mergeCells", merge_params);
    cJSON_AddItemToArray(requests, merge_request);
    
    // Отправляем запрос и возвращаем результат
    int result = gsheet_batch_update(client, requests);
    
    // Очищаем JSON-структуры
    cJSON_Delete(requests);
    return result;
}

// 17. Поиск в таблице
char** gsheet_search(GSheetClient* client, const char* query, int* result_count) {
    CURL* curl = curl_easy_init();
    char* response = NULL;
    char** search_results = NULL;
    *result_count = 0;

    // Формируем JSON-запрос
    cJSON* requests = cJSON_CreateArray();
    cJSON* find_replace = cJSON_CreateObject();
    
    // Создаем структуру findReplace
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "find", query);
    cJSON_AddBoolToObject(params, "allSheets", cJSON_True);
    cJSON_AddItemToObject(find_replace, "findReplace", params);
    cJSON_AddItemToArray(requests, find_replace);

    // Отправляем batchUpdate запрос
    if(gsheet_batch_update(client, requests)) {
        // Если запрос успешен, парсим ответ
        cJSON* json = cJSON_Parse(response);
        if(json) {
            // Извлекаем результаты поиска (примерная логика)
            cJSON* matches = cJSON_GetObjectItem(json, "matches");
            if(matches && cJSON_IsArray(matches)) {
                *result_count = cJSON_GetArraySize(matches);
                search_results = malloc(*result_count * sizeof(char*));
                
                // Парсим каждое совпадение
                for(int i = 0; i < *result_count; i++) {
                    cJSON* match = cJSON_GetArrayItem(matches, i);
                    cJSON* cell = cJSON_GetObjectItem(match, "cell");
                    if(cell) {
                        search_results[i] = strdup(cJSON_GetStringValue(cell));
                    }
                }
            }
            cJSON_Delete(json);
        }
    }

    // Очищаем ресурсы
    cJSON_Delete(requests);
    curl_easy_cleanup(curl);
    free(response);
    
    return search_results;
}

// 18. Экспорт в CSV
boolean gsheet_export_csv(GSheetClient* client, const char* range, const char* filename) {
    SheetRange* data = gsheet_read_range(client, range);
    if (!data) return FALSE;

    FILE* fp = fopen(filename, "w");
    for (size_t i = 0; i < data->rows; i++) {
        fprintf(fp, "%s\n", data->data[i]);
    }
    fclose(fp);
    return TRUE;
}

int main() {
    
    // Инициализация клиента
    const char* access_token = "";
    const char* spreadsheet_id = "";
    GSheetClient* client = gsheet_init(access_token, spreadsheet_id);
    
    if (!client) {
        fprintf(stderr, "Cannot init client\n");
        return 1;
    }

    // Чтение данных из диапазона (например, лист "Sheet1", ячейки A1:C10)
    const char* range = "Sheet1!A1:E1";
    SheetRange* data = gsheet_read_range(client, range);

    if (!data) {
        fprintf(stderr, "Cannot read data from gsheet\n");
        return 1;
    }

    // Вывод данных в консоль
    printf("Data from range %s:\n", range);
    for (size_t i = 0; i < data->rows; i++) {
        for (size_t j = 0; j < data->cols; j++) {
            printf("%-20s", data->data[i][j]); // Форматированный вывод
        }
        printf("\n");
    }

    return 0;
}