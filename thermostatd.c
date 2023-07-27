#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>
#include <string.h>

#define TRUE (1)

static const char *HEATER_ACTION_ON = "on";
static const char *HEATER_ACTION_OFF = "off";
static const char TEMP_FILE_PATH [] = "/tmp/temp";
static const char THERM_FILE_PATH [] = "/tmp/status";
static const char SERVER_URL [] = "http://18.220.79.28:8080/deviceTempProg";
static const char DAEMON_NAME [] = "theromstatd";
static const char HEATER_ON_RESPONSE [] = "{\"on\":1}";
static const char *req_start = "{\"deviceId\":0, \"temp\":\"";
static const char *req_end = "\"}";


char *build_request_body(char *temp);
char *get_current_temp();
int send_server_request(char *temp);
int output_therm_status(const char *status);
void signal_handler(const int signal);
void daemonize();



struct string {
    char *ptr;
    size_t len;
};

void str_init(struct string *s) {
    s->len = 0;
    s->ptr = malloc(s->len + 1);
    if (s->ptr == NULL) {
        exit(EXIT_FAILURE);
    }
    s->ptr[0] = '\0';
}

size_t str_write(void *ptr, size_t size, size_t nmemb, struct string *s)
{
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len+1);
    if (s->ptr == NULL) {
        exit(EXIT_FAILURE);
    }
    memcpy(s->ptr + s->len, ptr, size*nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;

    return size * nmemb;
}

void thermostatd_run() {
    while (TRUE) {
        // read temp file
        char *temp_str = get_current_temp();

        if (!strcmp(temp_str, "FAILURE")) {
            // send request
            int heater_on = send_server_request(temp_str);

            // write to temp programming file
            if (heater_on) {
                output_therm_status(HEATER_ACTION_ON);
            } else {
                output_therm_status(HEATER_ACTION_OFF);
            }
        }

        sleep(30);
    }
}

int send_server_request(char *temp) {
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if (curl) {
        struct string response;
        str_init(&response);
        char *request_body = build_request_body(temp);
        curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, str_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
        res = curl_easy_perform(curl);

        // log an error
        if (res !=  CURLE_OK) {
            syslog(LOG_ERR, "Error on request");
            return 0;
        }

        syslog(LOG_INFO, "Server response: %s", response.ptr);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return !strcmp(HEATER_ON_RESPONSE, response.ptr);
    }

    syslog(LOG_ERR, "Unable to open curl handle");
    return 0;
}

void signal_handler(const int signal) {
    switch (signal) {
        case SIGHUP:
            break;
        case SIGTERM:
            syslog(LOG_INFO, "thermostatd exiting on SIGTERM");
            closelog();
            exit(0);
        default:
            syslog(LOG_INFO, "unhandled signal");
    }
}

int output_therm_status(const char *status) {
    FILE *thermocouple_file;
    time_t now;

    thermocouple_file = fopen(THERM_FILE_PATH, "a");

    if (thermocouple_file != NULL) {
        time(&now);
        fprintf(thermocouple_file, "%s:%s\n", status, ctime(&now));
        fclose(thermocouple_file);
        return 1;
    }

    syslog(LOG_ERR, "Unable to open status file");
    return 0;
}

char *get_current_temp() {
    FILE *temp_file;
    long file_size;
    char *temp_str;

    temp_file = fopen(TEMP_FILE_PATH, "rb");

    if (temp_file != NULL) {
        fseek(temp_file, 0, SEEK_END);
        file_size = ftell(temp_file);
        fseek(temp_file, 0, SEEK_SET);

        temp_str = malloc(file_size + 1);
        fread(temp_str, file_size, 1, temp_file);
        temp_str[file_size] = '\0';

        fclose(temp_file);
        return temp_str;
    }

    syslog(LOG_ERR, "Unable to open temp file");
    return "FAILURE";
}

char *build_request_body(char *temp) {
    unsigned long l1 = strlen(req_start);
    unsigned long l2 = strlen(temp);
    unsigned long l3 = strlen(req_end);

    unsigned long len = l1 + l2 + l3;
    char *buff = malloc(len + 1);

    int i = 0;
    for (int j = 0; j < l1; j++) {
        buff[i++] = req_start[j];
    }
    for (int j = 0; j < l2; j++) {
        buff[i++] = temp[j];
    }
    for(int j = 0; j < l3; j++) {
        buff[i++] = req_end[j];
    }
    buff[len] = '\0';
    return buff;
}

void daemonize() {
    openlog(DAEMON_NAME, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);
    syslog(LOG_INFO, "iotd running");
    pid_t pid = fork();

    if (pid < 0) {
        syslog(LOG_ERR, "fork failure");
        exit(1);
    } else if (pid > 0) {
        return;
    }

    if (setsid() < -1) {
        syslog(LOG_ERR, "error on setsid()");
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDERR_FILENO);
    close(STDOUT_FILENO);

    umask(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "unable to choose directory");
        exit(0);
    }

    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
}

int main() {
    daemonize();
    thermostatd_run();
    return 0;
}
