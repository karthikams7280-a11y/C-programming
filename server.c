/* Smart Parking Slot Allocation and Billing System
   C backend: tiny web server + parking logic + file handling.
   Build (Windows): gcc server.c -o parking.exe -lws2_32
   Build (Mac/Linux): gcc server.c -o parking            */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#ifdef _WIN32
  #include <winsock2.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSESOCK close
#endif

#define PORT 8080
#define TOTAL_SLOTS 24
#define MAX_REC 500
#define DATA_FILE "records.txt"

typedef struct {            /* structure: one parking session */
    int id; char plate[16]; char type[8];
    int slot; time_t in, out; int amount;
} Record;

Record rec[MAX_REC];
int nrec = 0;
int slotRec[TOTAL_SLOTS + 1];   /* array: record index in each slot, -1 = free */
char out[200000];

int rate_for(const char *t) {
    if (!strcmp(t, "bike")) return 10;
    if (!strcmp(t, "truck")) return 60;
    return 30;                  /* car, rupees per hour */
}

void save_data(void) {
    FILE *f = fopen(DATA_FILE, "w");
    if (!f) return;
    for (int i = 0; i < nrec; i++)
        fprintf(f, "%d %s %s %d %lld %lld %d\n", rec[i].id, rec[i].plate, rec[i].type,
                rec[i].slot, (long long)rec[i].in, (long long)rec[i].out, rec[i].amount);
    fclose(f);
}

void load_data(void) {
    for (int i = 0; i <= TOTAL_SLOTS; i++) slotRec[i] = -1;
    FILE *f = fopen(DATA_FILE, "r");
    if (!f) return;
    Record r; long long a, b;
    while (nrec < MAX_REC && fscanf(f, "%d %15s %7s %d %lld %lld %d",
           &r.id, r.plate, r.type, &r.slot, &a, &b, &r.amount) == 7) {
        r.in = (time_t)a; r.out = (time_t)b;
        rec[nrec] = r;
        if (r.out == 0 && r.slot >= 1 && r.slot <= TOTAL_SLOTS) slotRec[r.slot] = nrec;
        nrec++;
    }
    fclose(f);
}

int get_param(const char *body, const char *key, char *dst, int n) {
    size_t kl = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1; int i = 0;
            while (*p && *p != '&' && i < n - 1) {
                if (*p == '+') dst[i++] = ' ';
                else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                    char h[3] = { p[1], p[2], 0 }; dst[i++] = (char)strtol(h, NULL, 16); p += 2;
                } else dst[i++] = *p;
                p++;
            }
            dst[i] = 0; return 1;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    dst[0] = 0; return 0;
}

void clean_plate(char *s) {      /* keep letters and digits, upper case */
    int j = 0;
    for (int i = 0; s[i] && j < 15; i++)
        if (isalnum((unsigned char)s[i])) s[j++] = (char)toupper((unsigned char)s[i]);
    s[j] = 0;
}

void fmt_time(time_t t, char *buf) {
    if (!t) { strcpy(buf, "-"); return; }
    strftime(buf, 40, "%d %b %Y, %I:%M %p", localtime(&t));
}

int find_parked(const char *plate) {
    for (int i = 0; i < nrec; i++)
        if (rec[i].out == 0 && !strcmp(rec[i].plate, plate)) return i;
    return -1;
}

void api_status(void) {
    int n = 0, parked = 0, exited = 0, revenue = 0;
    for (int i = 0; i < nrec; i++) {
        if (rec[i].out == 0) parked++; else { exited++; revenue += rec[i].amount; }
    }
    n += snprintf(out + n, sizeof out - n,
        "{\"total\":%d,\"available\":%d,\"parked\":%d,\"exited\":%d,\"revenue\":%d,"
        "\"rates\":{\"car\":30,\"bike\":10,\"truck\":60},\"slots\":[",
        TOTAL_SLOTS, TOTAL_SLOTS - parked, parked, exited, revenue);
    for (int s = 1; s <= TOTAL_SLOTS; s++) {
        int r = slotRec[s];
        n += snprintf(out + n, sizeof out - n, "%s{\"n\":%d,\"plate\":\"%s\",\"type\":\"%s\"}",
            s > 1 ? "," : "", s, r >= 0 ? rec[r].plate : "", r >= 0 ? rec[r].type : "");
    }
    n += snprintf(out + n, sizeof out - n, "],\"records\":[");
    for (int i = nrec - 1; i >= 0; i--) {
        char a[40], b[40]; fmt_time(rec[i].in, a); fmt_time(rec[i].out, b);
        n += snprintf(out + n, sizeof out - n,
            "%s{\"id\":%d,\"plate\":\"%s\",\"type\":\"%s\",\"slot\":%d,\"in\":\"%s\",\"out\":\"%s\",\"amount\":%d}",
            i < nrec - 1 ? "," : "", rec[i].id, rec[i].plate, rec[i].type, rec[i].slot, a, b, rec[i].amount);
    }
    snprintf(out + n, sizeof out - n, "]}");
}

void api_entry(const char *body) {
    char plate[32], type[16];
    get_param(body, "plate", plate, sizeof plate);
    get_param(body, "type", type, sizeof type);
    clean_plate(plate);
    if (strcmp(type, "bike") && strcmp(type, "truck")) strcpy(type, "car");
    if (!plate[0]) { strcpy(out, "{\"ok\":false,\"msg\":\"Enter a vehicle registration number.\"}"); return; }
    if (find_parked(plate) >= 0) { strcpy(out, "{\"ok\":false,\"msg\":\"This vehicle is already parked.\"}"); return; }
    if (nrec >= MAX_REC) { strcpy(out, "{\"ok\":false,\"msg\":\"Record storage is full.\"}"); return; }
    int slot = 0;                                   /* automatic allocation: first free slot */
    for (int s = 1; s <= TOTAL_SLOTS; s++) if (slotRec[s] < 0) { slot = s; break; }
    if (!slot) { strcpy(out, "{\"ok\":false,\"msg\":\"Parking is full. No slot available.\"}"); return; }
    Record r; memset(&r, 0, sizeof r);
    r.id = nrec ? rec[nrec - 1].id + 1 : 1;
    strcpy(r.plate, plate); strcpy(r.type, type);
    r.slot = slot; r.in = time(NULL);
    rec[nrec] = r; slotRec[slot] = nrec; nrec++;
    save_data();
    snprintf(out, sizeof out, "{\"ok\":true,\"slot\":%d,\"plate\":\"%s\"}", slot, plate);
}

void api_exit(const char *body) {
    char plate[32];
    get_param(body, "plate", plate, sizeof plate);
    clean_plate(plate);
    int i = find_parked(plate);
    if (i < 0) { strcpy(out, "{\"ok\":false,\"msg\":\"No parked vehicle found with this number.\"}"); return; }
    rec[i].out = time(NULL);
    double secs = difftime(rec[i].out, rec[i].in);
    int hours = (int)((secs + 3599) / 3600); if (hours < 1) hours = 1;   /* billed per hour, minimum 1 */
    int rate = rate_for(rec[i].type);
    rec[i].amount = hours * rate;
    slotRec[rec[i].slot] = -1;
    save_data();
    char a[40], b[40]; fmt_time(rec[i].in, a); fmt_time(rec[i].out, b);
    snprintf(out, sizeof out,
        "{\"ok\":true,\"plate\":\"%s\",\"type\":\"%s\",\"slot\":%d,\"in\":\"%s\",\"out\":\"%s\","
        "\"mins\":%d,\"hours\":%d,\"rate\":%d,\"amount\":%d}",
        rec[i].plate, rec[i].type, rec[i].slot, a, b, (int)(secs / 60), hours, rate, rec[i].amount);
}

void api_reset(void) {
    nrec = 0;
    for (int i = 0; i <= TOTAL_SLOTS; i++) slotRec[i] = -1;
    save_data();
    strcpy(out, "{\"ok\":true}");
}

void send_all(sock_t c, const char *d, int n) {
    while (n > 0) { int k = send(c, d, n, 0); if (k <= 0) return; d += k; n -= k; }
}

void respond(sock_t c, int code, const char *mime, const char *data, int len) {
    char h[256];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", code, code == 200 ? "OK" : "Not Found", mime, len);
    send_all(c, h, n); send_all(c, data, len);
}

const char *mime_of(const char *p) {
    if (strstr(p, ".css")) return "text/css";
    if (strstr(p, ".js")) return "application/javascript";
    if (strstr(p, ".html")) return "text/html; charset=utf-8";
    return "application/octet-stream";
}

void handle(sock_t c) {
    char buf[8192]; int len = 0, r;
    while (len < (int)sizeof buf - 1 && (r = recv(c, buf + len, sizeof buf - 1 - len, 0)) > 0) {
        len += r; buf[len] = 0;
        char *h = strstr(buf, "\r\n\r\n");
        if (h) {
            char *cl = strstr(buf, "Content-Length:");
            if (!cl) cl = strstr(buf, "content-length:");
            int need = cl ? atoi(cl + 15) : 0;
            if (len - (int)(h + 4 - buf) >= need) break;
        }
    }
    if (len <= 0) return;
    buf[len] = 0;
    char method[8], path[256];
    if (sscanf(buf, "%7s %255s", method, path) != 2) return;
    char *body = strstr(buf, "\r\n\r\n"); body = body ? body + 4 : buf + len;

    if (!strcmp(path, "/api/status")) api_status();
    else if (!strcmp(path, "/api/entry") && !strcmp(method, "POST")) api_entry(body);
    else if (!strcmp(path, "/api/exit") && !strcmp(method, "POST")) api_exit(body);
    else if (!strcmp(path, "/api/reset") && !strcmp(method, "POST")) api_reset();
    else {                                   /* static files from the public folder */
        if (!strcmp(path, "/")) strcpy(path, "/index.html");
        char file[300]; snprintf(file, sizeof file, "public%s", path);
        FILE *f = strstr(path, "..") ? NULL : fopen(file, "rb");
        if (!f) { respond(c, 404, "text/plain", "Not found", 9); return; }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        char *data = malloc(sz + 1);
        if (data) { fread(data, 1, sz, f); respond(c, 200, mime_of(path), data, (int)sz); free(data); }
        fclose(f); return;
    }
    respond(c, 200, "application/json", out, (int)strlen(out));
}

int main(void) {
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#endif
    load_data();
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(PORT);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 8) < 0) {
        printf("Could not start on port %d. Is another copy already running?\n", PORT);
        return 1;
    }
    printf("Parkwise is running at http://localhost:%d  (press Ctrl+C to stop)\n", PORT);
#ifdef _WIN32
    system("start \"\" http://localhost:8080");
#elif defined(__APPLE__)
    system("open http://localhost:8080");
#else
    system("xdg-open http://localhost:8080 >/dev/null 2>&1 &");
#endif
    for (;;) {
        sock_t c = accept(s, NULL, NULL);
        if (c < 0) continue;
        handle(c);
        CLOSESOCK(c);
    }
}
